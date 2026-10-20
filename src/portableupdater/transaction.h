#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
namespace zzlogg::portable {
namespace fs = std::filesystem;
struct File { std::string path; std::uintmax_t size; std::string sha256; };
using Hasher = std::function<std::string(const fs::path&)>;
bool safePath(const std::string& path);
void validateTreePath(const fs::path& root, const std::string& relative);
void validateTransaction(const fs::path& root, const fs::path& payload,
                         const std::vector<std::string>& oldFiles,
                         const std::vector<File>& newFiles, const Hasher& hash);
// All source bytes are verified before any installed byte is changed. On failure
// the original owned files are restored; unknown files are never removed.
void replaceFiles(const fs::path& root, const fs::path& payload, const fs::path& backup,
                  const std::vector<std::string>& oldFiles,
                  const std::vector<File>& newFiles, const Hasher& hash);
}
