#pragma once
#include "localchannel_win_p.h"
#include "runtimecopy_win_p.h"
#include <memory>
namespace zzlogg::updater::detail {
inline constexpr int ExecutionDisabled=40;
inline constexpr int BootstrapRejected=41;
// Fixed-size, process-local anonymous mapping; only its numeric read handle is in argv.
struct BootstrapData {
    uint32_t magic=0x42555a5a,version=1;
    uint64_t parentHandle=0;
    ProcessStamp parent{};
    TransactionId transaction{};
    SessionToken token{};
};
bool launchCopy(const RuntimeCopy&,const TransactionId&,const SessionToken&,ProcessIdentity&);
class ChildBootstrap {
public:
    ChildBootstrap();~ChildBootstrap();
    bool open(int,wchar_t**);
    const BootstrapData& data() const { return data_; }
    const ProcessIdentity& parent() const { return parent_; }
private:
    BootstrapData data_{};
    ProcessIdentity parent_;
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
