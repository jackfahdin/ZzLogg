#pragma once
#include "installlock_win.h"
#include <memory>
#include <string>
namespace zzlogg::updater {
enum class ActivityError { None, InvalidRoot, Unavailable, Blocked, Abandoned };
// Read-only installation activity lease built on operating-system handle
// lifetime; nothing is written into the installation directory. Multiple
// ordinary instances coexist, and ordinary file activity beneath the
// installation keeps working (the lease shares read/write and denies delete).
// reserveUpdate() claims the same directory identity mutex as the 3B.3
// InstallLock (existing objects stay fail-closed), proves under that mutex
// that no other instance holds an activity lease, then restores this
// instance's stable lease before returning. Move, cancellation and
// destruction must occur on the entering thread (Windows mutex ownership is
// thread-affine). This is coordination, never elevation authority, and never
// proof that every file is replaceable.
class InstallationActivity {
public:
    InstallationActivity();
    ~InstallationActivity();
    InstallationActivity(InstallationActivity&&) noexcept;
    InstallationActivity& operator=(InstallationActivity&&) noexcept;
    InstallationActivity(const InstallationActivity&)=delete;
    InstallationActivity& operator=(const InstallationActivity&)=delete;
    ActivityError enter(const std::wstring& root);
    ActivityError reserveUpdate();
    void cancelUpdate();
    bool entered() const;
    bool updateReserved() const;
    bool identityUnchanged() const;
    // Leaf directory identity; immutable while entered. Consumers (task3)
    // derive the observer mutex name from it via InstallLock::mutexName.
    DirectoryIdentity identity() const;
    // Resolves the directory identity without retaining any lease. Used for
    // directory-scoped single-instance naming at entry points.
    static bool probeIdentity(const std::wstring& directory, DirectoryIdentity& identity);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
