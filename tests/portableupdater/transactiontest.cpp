#include "transaction.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace zzlogg::portable;
static void check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
static void put(const fs::path& p, const std::string& data) {
    fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << data;
}
static std::string get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
template<class F> static void rejects(F f, const char* what) {
    bool caught=false; try { f(); } catch(const std::exception&) { caught=true; }
    check(caught, what);
}
int main() {
    const auto base=fs::temp_directory_path()/ ("zzlogg-portable-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        // These catch traversal/Windows aliases and attempts to claim user storage.
        for(const auto* bad : {"../a", "a/../b", "/a", "C:a", "a\\b", "a.", "a ",
             "CON", "foo/NUL.txt", "data/file", "config/x", "session/x", "logs/x",
             "ZzLogg.conf", "ZzLogg_session.conf", "a//b", "a:stream", "a\nfile", "PROGRA~1/file", "file~1.dll"})
            check(!safePath(bad), bad);
        check(safePath("platforms/qwindows.dll"), "regular payload path rejected");
        const auto root=base/"app", payload=base/"payload";
        put(root/"ZzLogg.exe", "old"); put(root/"obsolete.dll", "oldlib");
        put(root/"data/config/ZzLogg.ini", "settings"); put(root/"notes.txt", "notes");
        put(payload/"ZzLogg.exe", "new"); put(payload/"plugins/new.dll", "newlib");
        const std::vector<std::string> old={"ZzLogg.exe", "obsolete.dll"};
        // The transaction treats hashes as opaque; the executable supplies BCrypt SHA-256.
        const std::vector<File> files={{"ZzLogg.exe",3,"new"},{"plugins/new.dll",6,"newlib"}};
        const Hasher hash=[](const fs::path& p){return get(p);};
        replaceFiles(root,payload,base/"backup",old,files,hash);
        check(get(root/"ZzLogg.exe")=="new", "new executable not installed");
        check(!fs::exists(root/"obsolete.dll"), "obsolete package file not removed");
        check(get(root/"data/config/ZzLogg.ini")=="settings", "settings changed");
        check(get(root/"notes.txt")=="notes", "unknown file changed");
        check(get(base/"backup/ZzLogg.exe")=="old", "backup not retained");
        put(root/"ZzLogg.exe", "old");
        auto corrupt=files; corrupt[0].sha256="incorrect";
        rejects([&]{replaceFiles(root,payload,base/"badbackup",old,corrupt,hash);}, "bad hash accepted");
        check(get(root/"ZzLogg.exe")=="old", "bad hash modified installation");
        rejects([&]{replaceFiles(root,payload,base/"conflict",old,files,hash);}, "unknown collision overwritten");
        fs::remove(root/"plugins/new.dll");
        auto aliases=files; aliases.push_back({"zzlogg.exe",3,"new"});
        rejects([&]{replaceFiles(root,payload,base/"aliases",old,aliases,hash);}, "case alias accepted");
        std::error_code linkError;
        fs::create_directory_symlink(base,root/"link",linkError);
        if(!linkError) rejects([&]{validateTreePath(root,"link/escape");}, "symlink parent accepted");
#ifndef _WIN32
        else throw std::runtime_error("Cannot create symlink fixture");
#endif
        // A source disappearing after validation forces a real filesystem failure
        // after originals were backed up and exercises restoration of old bytes.
        bool removed=false;
        const Hasher disappearing=[&](const fs::path& p){ auto bytes=get(p);
            if(!removed && p.filename()==".zzlogg-update-new") { fs::remove(payload/"plugins/new.dll"); removed=true; }
            return bytes; };
        rejects([&]{replaceFiles(root,payload,base/"rollback",old,files,disappearing);}, "missing source accepted");
        check(get(root/"ZzLogg.exe")=="old", "rollback did not restore executable");
        check(get(root/"notes.txt")=="notes", "rollback damaged unknown file");
        put(payload/"plugins/new.dll", "newlib");
        put(root/".zzlogg-update-new", "preexisting");
        rejects([&]{replaceFiles(root,payload,base/"unfinished",old,files,hash);}, "unfinished file accepted");
        check(get(root/".zzlogg-update-new")=="preexisting", "preexisting transaction file removed");
        fs::remove_all(base); std::cout << "portable transaction tests passed\n";
        return 0;
    } catch(const std::exception& e) {
        fs::remove_all(base); std::cerr << e.what() << '\n'; return 1;
    }
}
