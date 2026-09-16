#pragma once
#include "processidentity_win_p.h"
#include <memory>
namespace zzlogg::updater::detail {
// Atomically creates one relative child directory and returns its stable lease.
// Existing names (including reparse nodes) fail; there is no open-existing fallback.
Handle createExclusiveDirectory(HANDLE parent,const std::wstring& leaf);
// Ordinary-user staging only. No installation authority or publisher trust.
class RuntimeCopy {
public:
    RuntimeCopy();
    ~RuntimeCopy();
    RuntimeCopy(RuntimeCopy&&) noexcept;
    RuntimeCopy& operator=(RuntimeCopy&&) noexcept;
    bool create(const std::wstring& source,const std::wstring& base);
    bool unchanged() const;
    const std::wstring& path() const;
    const std::wstring& directory() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
