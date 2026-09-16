#pragma once
#include "processidentity_win_p.h"
#include "handoffprotocol.h"
#include <optional>
namespace zzlogg::updater::detail {
using Deadline=ULONGLONG;
Deadline after(DWORD milliseconds);
DWORD remaining(Deadline);
// One bounded availability wait after CreateFile reports ERROR_PIPE_BUSY.
bool waitForPipeInstance(const std::wstring&,Deadline);
std::wstring endpointName(const TransactionId&);
class LocalChannel {
public:
    bool create(const TransactionId&);
    bool accept(const ProcessIdentity&,Deadline);
    bool connect(const TransactionId&,const ProcessIdentity&,Deadline);
    bool send(const Message&,Deadline);
    std::optional<Message> receive(Deadline);
    void close() { pipe_.reset(); }
private:
    bool transfer(bool write,void*,DWORD,Deadline);
    Handle pipe_;
};
}
