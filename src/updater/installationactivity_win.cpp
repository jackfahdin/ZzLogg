#include "installationactivity_win.h"
#include "installlock_win_p.h"
#include <vector>
namespace zzlogg::updater {
namespace {
using detail::Handle;
struct Node { Handle handle; std::wstring path; DirectoryIdentity identity; };
Handle openActivityDirectory(const std::wstring& path) {
    // Long-lived activity lease: read/write sharing keeps ordinary file
    // activity beneath the installation working (locator and settings writes
    // next to a portable executable) while denying delete sharing still pins
    // the directory name against replacement. Behaviorally proven in the
    // activity tests, including the exclusive quiescence probe conflict.
    return Handle(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES|FILE_LIST_DIRECTORY,
        FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
}
bool verifyNode(const Node& node) {
    DirectoryIdentity current{},reopened{};
    auto check=detail::openLeasedDirectory(node.path);
    return node.handle && detail::directoryIdentity(node.handle.get(),current)
        && detail::sameDirectoryIdentity(current,node.identity)
        && detail::directoryMatchesPath(node.handle.get(),node.path) && check
        && detail::directoryIdentity(check.get(),reopened)
        && detail::sameDirectoryIdentity(current,reopened);
}
Handle openIdentityPin(const std::wstring& path) {
    // Zero-access handles are excluded from sharing checks (behaviorally
    // proven in the activity tests), so this pin survives the quiescence probe
    // and keeps observing the leaf identity across the access-mode transition.
    return Handle(CreateFileW(path.c_str(),0,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,
        OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
}
bool restoreStableLease(Node& leaf) {
    Handle stable=openActivityDirectory(leaf.path);
    DirectoryIdentity identity{};
    if(!stable || !detail::directoryIdentity(stable.get(),identity)
        || !detail::sameDirectoryIdentity(identity,leaf.identity)
        || !detail::directoryMatchesPath(stable.get(),leaf.path)) return false;
    leaf.handle=std::move(stable);
    return true;
}
}
struct InstallationActivity::Impl {
    std::vector<Node> directories;
    Handle mutex;
    bool owned=false;
    bool reserved=false;
    ~Impl() { if(owned) ReleaseMutex(mutex.get()); }
    bool unchanged() const {
        for(const auto& node:directories) if(!verifyNode(node)) return false;
        return !directories.empty();
    }
};
InstallationActivity::InstallationActivity()=default;
InstallationActivity::~InstallationActivity()=default;
InstallationActivity::InstallationActivity(InstallationActivity&&) noexcept=default;
InstallationActivity& InstallationActivity::operator=(InstallationActivity&&) noexcept=default;
ActivityError InstallationActivity::enter(const std::wstring& root) {
    // Do not silently drop an existing lease when a caller attempts re-entry.
    if(impl_) return ActivityError::Blocked;
    if(!detail::standardInstallationPath(root) || GetDriveTypeW(root.substr(0,3).c_str())!=DRIVE_FIXED)
        return ActivityError::InvalidRoot;
    auto lease=std::make_unique<Impl>();
    for(std::size_t end=2;;) {
        auto path=root.substr(0,end==2?3:end);
        auto handle=openActivityDirectory(path);
        DirectoryIdentity identity{};
        if(!handle || !detail::directoryIdentity(handle.get(),identity) || !detail::directoryMatchesPath(handle.get(),path))
            return ActivityError::Unavailable;
        lease->directories.push_back({std::move(handle),std::move(path),identity});
        if(end==root.size()) break;
        end=root.find(L'\\',end+1);
        if(end==std::wstring::npos) end=root.size();
    }
    const auto name=InstallLock::mutexName(lease->directories.back().identity);
    if(name.empty()) return ActivityError::Unavailable;
    // The lease chain is already held: a reservation racing us cannot pass its
    // quiescence probe, and a completed reservation keeps its mutex, so the
    // check-to-entry sequence has no pass-through window. Any existing object
    // (owned, unowned or access-denied) is fail-closed.
    Handle existing(OpenMutexW(SYNCHRONIZE,FALSE,name.c_str()));
    if(existing) return ActivityError::Blocked;
    if(GetLastError()!=ERROR_FILE_NOT_FOUND) return ActivityError::Unavailable;
    impl_=std::move(lease);
    return ActivityError::None;
}
ActivityError InstallationActivity::reserveUpdate() {
    if(!impl_) return ActivityError::Unavailable;
    if(impl_->owned) return ActivityError::Blocked;
    if(!impl_->unchanged()) return ActivityError::Unavailable;
    auto& leaf=impl_->directories.back();
    const auto name=InstallLock::mutexName(leaf.identity);
    Handle mutex;
    const auto claim=detail::claimDirectoryMutex(name,mutex);
    if(claim==detail::MutexClaim::Error) return ActivityError::Unavailable;
    if(claim==detail::MutexClaim::Blocked) return ActivityError::Blocked;
    if(claim==detail::MutexClaim::Abandoned) return ActivityError::Abandoned;
    impl_->mutex=std::move(mutex);
    impl_->owned=true;
    // From here every exit restores this instance's lease before releasing the
    // update mutex.
    const auto fail=[&](ActivityError error) {
        const bool restored=restoreStableLease(leaf);
        if(impl_->owned) ReleaseMutex(impl_->mutex.get());
        impl_->owned=false;
        impl_->mutex.reset();
        if(!restored) impl_.reset();
        return error;
    };
    // Quiesce: replace this instance's data-access lease with a zero-access
    // identity pin so the exclusive probe measures only OTHER activity. The
    // ancestor pins stay held and the leaf identity is held continuously; the
    // leaf is re-verified against it before the reservation completes.
    Handle pin=openIdentityPin(leaf.path);
    DirectoryIdentity pinIdentity{};
    if(!pin || !detail::directoryIdentity(pin.get(),pinIdentity)
        || !detail::sameDirectoryIdentity(pinIdentity,leaf.identity)) return fail(ActivityError::Unavailable);
    leaf.handle.reset();
    {
        Handle probe(CreateFileW(leaf.path.c_str(),FILE_LIST_DIRECTORY,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
        if(!probe) {
            const auto error=GetLastError();
            return fail(error==ERROR_SHARING_VIOLATION?ActivityError::Blocked:ActivityError::Unavailable);
        }
    }
    // No other activity reader remains. Restore the stable lease so
    // RuntimeCopy/StablePackage may open the source directory, and keep the
    // update mutex so cooperating new entries stay denied.
    if(!restoreStableLease(leaf)) return fail(ActivityError::Unavailable);
    impl_->reserved=true;
    return ActivityError::None;
}
void InstallationActivity::cancelUpdate() {
    if(!impl_ || !impl_->owned) return;
    // The stable lease was restored before the reservation completed, so this
    // instance's lease is already back in place when the mutex is released.
    ReleaseMutex(impl_->mutex.get());
    impl_->owned=false;
    impl_->mutex.reset();
    impl_->reserved=false;
}
bool InstallationActivity::entered() const { return impl_!=nullptr; }
bool InstallationActivity::updateReserved() const { return impl_ && impl_->reserved; }
bool InstallationActivity::identityUnchanged() const { return impl_ && impl_->unchanged(); }
DirectoryIdentity InstallationActivity::identity() const {
    return impl_?impl_->directories.back().identity:DirectoryIdentity{};
}
bool InstallationActivity::probeIdentity(const std::wstring& directory,DirectoryIdentity& identity) {
    if(!detail::standardInstallationPath(directory)
        || GetDriveTypeW(directory.substr(0,3).c_str())!=DRIVE_FIXED) return false;
    auto handle=detail::openLeasedDirectory(directory);
    return handle && detail::directoryIdentity(handle.get(),identity)
        && detail::directoryMatchesPath(handle.get(),directory);
}
std::wstring singleInstancePipeName(const DirectoryIdentity& identity,const std::wstring& imageFileName) {
    if(!identity.volumeSerial || imageFileName.empty()) return {};
    DWORD session=0;
    if(!ProcessIdToSessionId(GetCurrentProcessId(),&session)) return {};
    // Byte-identical to KDSingleApplication's Windows socket name over
    // ApplicationUpdateGuard::singleInstanceName: kdsingleapp-<session>-<name>
    // where the name is <image>-<volume serial hex16><file id hex32>.
    std::wstring name=L"\\\\.\\pipe\\kdsingleapp-"+std::to_wstring(session)+L"-"+imageFileName+L"-";
    constexpr wchar_t hex[]=L"0123456789abcdef";
    for(int i=15;i>=0;--i) name+=hex[(identity.volumeSerial>>(i*4))&15];
    for(const auto byte:identity.fileId){name+=hex[byte>>4];name+=hex[byte&15];}
    return name;
}
}
