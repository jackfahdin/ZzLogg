#pragma once
#include "installlock_win.h"
#include "processidentity_win_p.h"
#include <string>
namespace zzlogg::updater::detail {
// Shared directory-identity primitives for the updater lease (InstallLock) and
// the entry-point activity lease (InstallationActivity). One implementation
// keeps both closures' security policy identical; do not fork these checks.
bool standardInstallationPath(const std::wstring& path);
// Deny write/delete sharing: keeps every ancestor from becoming a junction or
// being renamed while still allowing ordinary file activity beneath it.
// Attribute-only opens do not participate in all sharing checks, so directory
// read access is requested as well and the denial of delete sharing pins the name.
Handle openLeasedDirectory(const std::wstring& path);
bool directoryIdentity(HANDLE handle, DirectoryIdentity& identity);
bool directoryMatchesPath(HANDLE handle, const std::wstring& path);
bool sameDirectoryIdentity(const DirectoryIdentity& a, const DirectoryIdentity& b);
// Creates the directory-identity mutex with the shared security policy:
// network logons denied, authenticated local users get exactly wait and
// release rights. Any preexisting object is fail-closed, even when unowned.
enum class MutexClaim { Owned, Blocked, Abandoned, Error };
MutexClaim claimDirectoryMutex(const std::wstring& name, Handle& out);
}
