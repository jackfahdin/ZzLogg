#pragma once
#include "installlock_win.h"
#include "localchannel_win_p.h"
#include "runtimecopy_win_p.h"
#include <memory>
namespace zzlogg::updater::detail {
inline constexpr int ExecutionDisabled=40;
inline constexpr int BootstrapRejected=41;
// Fixed-size, process-local anonymous mapping; only its numeric read handle is in argv.
struct BootstrapData {
    uint32_t magic=0x42555a5a,version=2;
    uint64_t parentHandle=0;
    ProcessStamp parent{};
    TransactionId transaction{};
    SessionToken token{};
    // Reserved installation directory identity; valid only with DirectoryReserved.
    DirectoryIdentity directory{};
    uint32_t flags=0;
};
// bootstrap carries a reserved directory identity: the child opens the
// computed existing mutex with SYNCHRONIZE and holds it (observer only) before
// Hello. Never waited on, never released, never installation authority.
inline constexpr uint32_t DirectoryReserved=1;
bool launchCopy(const RuntimeCopy&,const TransactionId&,const SessionToken&,const DirectoryIdentity* reserved,ProcessIdentity&);
class ChildBootstrap {
public:
    ChildBootstrap();~ChildBootstrap();
    bool open(int,wchar_t**);
    const BootstrapData& data() const { return data_; }
    const ProcessIdentity& parent() const { return parent_; }
    bool directoryReserved() const;
private:
    BootstrapData data_{};
    ProcessIdentity parent_;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
