#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace zzlogg::updater {
struct DirectoryIdentity {
    std::uint64_t volumeSerial=0;
    std::array<std::uint8_t,16> fileId{};
};
enum class InstallLockError { None, InvalidRoot, Unavailable, Blocked, Abandoned };
// Same primitive for the updater lease and entry-point probe: acquire and keep
// it through the protected operation, or acquire and immediately destroy it.
// This is coordination, never elevation authority. Move/destruction must occur
// on the acquiring thread (Windows mutex ownership is thread-affine).
// Existing objects are fail-closed, even when unowned. Abandonment is observable
// only while another handle keeps the object alive; durable crash recovery is
// the responsibility of the future protected transaction journal (phase 3C).
class InstallLock {
public:
    InstallLock();
    ~InstallLock();
    InstallLock(InstallLock&&) noexcept;
    InstallLock& operator=(InstallLock&&) noexcept;
    InstallLock(const InstallLock&)=delete;
    InstallLock& operator=(const InstallLock&)=delete;
    InstallLockError acquire(const std::wstring& directory);
    bool ownsLock() const;
    bool identityUnchanged() const;
    DirectoryIdentity identity() const;
    // Public deterministic identity mapping also lets entry points coordinate.
    static std::wstring mutexName(const DirectoryIdentity&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
