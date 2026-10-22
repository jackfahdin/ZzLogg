#include "transaction.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace zzlogg::portable {
namespace {
std::string folded(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){ return char(std::tolower(c)); });
    return value;
}
void require(bool value, const std::string& error) { if(!value) throw std::runtime_error(error); }
bool pathExists(const fs::path& path) { return fs::symlink_status(path).type()!=fs::file_type::not_found; }
void noLink(const fs::path& path) {
    require(!fs::is_symlink(fs::symlink_status(path)),"Symbolic link in update path: " + path.u8string());
#ifdef _WIN32
    const auto flags=GetFileAttributesW(path.c_str());
    require(flags==INVALID_FILE_ATTRIBUTES || !(flags&FILE_ATTRIBUTE_REPARSE_POINT),
            "Reparse point in update path: " + path.u8string());
#endif
}
void checkSet(const std::vector<std::string>& paths) {
    std::set<std::string> names;
    for(const auto& path:paths) {
        require(safePath(path),"Unsafe package path: " + path);
        require(names.insert(folded(path)).second,"Duplicate package path: " + path);
    }
    for(const auto& path:names) {
        auto slash=path.find('/');
        while(slash!=std::string::npos) {
            require(!names.count(path.substr(0,slash)),"File/directory alias in package: " + path);
            slash=path.find('/',slash+1);
        }
    }
}
}
bool safePath(const std::string& path) {
    if(path.empty() || path.size()>220 || path.front()=='/' || path.back()=='/') return false;
    for(char value:path) {
        const auto c=static_cast<unsigned char>(value);
        if(c<32 || c>=127 || c=='\\' || c==':' || c=='<' || c=='>' || c=='"' || c=='|' || c=='?' || c=='*' || c=='~') return false;
    }
    const auto lower=folded(path);
    const auto first=lower.substr(0,lower.find('/'));
    static const std::set<std::string> reserved={"data","config","settings","session","sessions","logs",
        "zzlogg.conf","zzlogg_session.conf","zzlogg.ini","zzlogg_session.ini","klogg.conf",
        "klogg_session.conf","storage-manifest.ini",".zzlogg-update.lock"};
    if(reserved.count(first)) return false;
    std::size_t start=0;
    while(start<lower.size()) {
        const auto end=lower.find('/',start);
        const auto component=lower.substr(start,end==std::string::npos?end:end-start);
        if(component.empty() || component.back()=='.' || component.back()==' ') return false;
        const auto stem=component.substr(0,component.find('.'));
        if(stem=="con" || stem=="prn" || stem=="aux" || stem=="nul" || stem=="conin$" || stem=="conout$") return false;
        if(stem.size()==4 && (stem.substr(0,3)=="com" || stem.substr(0,3)=="lpt") && stem[3]>='0' && stem[3]<='9') return false;
        if(component.find(".zzlogg-update-")==0) return false;
        if(end==std::string::npos) break;
        start=end+1;
    }
    return true;
}
void validateTreePath(const fs::path& root, const std::string& relative) {
    require(safePath(relative),"Unsafe update path: " + relative);
    require(root.is_absolute(),"Update root must be absolute");
    fs::path cursor;
    for(const auto& component:root.lexically_normal()) { cursor/=component; noLink(cursor); }
    require(fs::is_directory(root),"Update directory is missing");
    cursor=root;
    const auto rel=fs::u8path(relative);
    for(auto it=rel.begin();it!=rel.end();++it) {
        cursor/=*it; noLink(cursor);
        auto next=it; ++next;
        if(next!=rel.end() && pathExists(cursor)) require(fs::is_directory(cursor),"File blocks package directory: " + cursor.u8string());
    }
}
void validateTransaction(const fs::path& root, const fs::path& payload,
                         const std::vector<std::string>& oldFiles,
                         const std::vector<File>& newFiles, const Hasher& hash) {
    require(!newFiles.empty() && newFiles.size()<=20000 && oldFiles.size()<=20000,"Invalid package file count");
    checkSet(oldFiles);
    std::vector<std::string> paths; std::set<std::string> oldNames;
    for(const auto& name:oldFiles) {oldNames.insert(folded(name)); validateTreePath(root,name);
        require(!pathExists(root/fs::u8path(name)) || fs::is_regular_file(root/fs::u8path(name)),"Owned target is not a regular file: " + name);}
    for(const auto& file:newFiles) paths.push_back(file.path);
    checkSet(paths);
    for(const auto& file:newFiles) {
        validateTreePath(root,file.path); validateTreePath(payload,file.path);
        const auto target=root/fs::u8path(file.path), source=payload/fs::u8path(file.path);
        require(!pathExists(target) || (oldNames.count(folded(file.path)) && fs::is_regular_file(target)),"An unknown file blocks the update: " + file.path);
        require(fs::is_regular_file(source) && fs::file_size(source)==file.size,"Staged file size mismatch: " + file.path);
        require(hash(source)==file.sha256,"Staged file hash mismatch: " + file.path);
    }
}
void replaceFiles(const fs::path& root, const fs::path& payload, const fs::path& backup,
                  const std::vector<std::string>& oldFiles,
                  const std::vector<File>& newFiles, const Hasher& hash) {
    validateTransaction(root,payload,oldFiles,newFiles,hash);
    require(!pathExists(backup),"Backup directory already exists; preserve it for recovery");
    fs::create_directories(backup);
    std::vector<std::string> backedUp, changed;
    std::set<std::string> newNames;
    // Finish every backup before touching any installed byte.
    for(const auto& name:oldFiles) {
        const auto source=root/fs::u8path(name);
        if(!pathExists(source)) continue;
        const auto destination=backup/fs::u8path(name);
        fs::create_directories(destination.parent_path());
        fs::copy_file(source,destination);
        backedUp.push_back(name);
    }
    fs::path temporary;
    try {
        for(const auto& file:newFiles) {
            validateTreePath(root,file.path);
            const auto destination=root/fs::u8path(file.path);
            fs::create_directories(destination.parent_path());
            const auto temporaryName=destination.parent_path()/".zzlogg-update-new";
            require(!pathExists(temporaryName),"An unfinished update file blocks replacement");
            temporary=temporaryName;
            fs::copy_file(payload/fs::u8path(file.path),temporary);
            require(fs::file_size(temporary)==file.size && hash(temporary)==file.sha256,"Copied file verification failed: " + file.path);
            changed.push_back(file.path);
            fs::remove(destination);
            fs::rename(temporary,destination);
            temporary.clear(); newNames.insert(folded(file.path));
        }
        for(const auto& name:oldFiles) if(!newNames.count(folded(name))) {
            validateTreePath(root,name); changed.push_back(name); fs::remove(root/fs::u8path(name));
        }
    } catch(const std::exception& original) {
        std::string recoveryError;
        if(!temporary.empty()) { std::error_code ec; fs::remove(temporary,ec); }
        for(auto it=changed.rbegin();it!=changed.rend();++it) {
            try {
                validateTreePath(root,*it);
                const auto target=root/fs::u8path(*it);
                require(!pathExists(target) || fs::is_regular_file(target),"Rollback target changed type");
                fs::remove(target);
                if(std::find(backedUp.begin(),backedUp.end(),*it)!=backedUp.end()) fs::copy_file(backup/fs::u8path(*it),target);
            } catch(const std::exception& error) { recoveryError += error.what(); recoveryError += "; "; }
        }
        if(!recoveryError.empty()) throw std::runtime_error(std::string(original.what()) + "; rollback incomplete: " + recoveryError + "Backup: " + backup.u8string());
        throw;
    }
}
}
