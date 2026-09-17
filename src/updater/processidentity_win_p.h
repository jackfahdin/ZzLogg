#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
namespace zzlogg::updater::detail {
class Handle {
public:
    explicit Handle(HANDLE h=nullptr):h_(h) {}
    ~Handle() { reset(); }
    Handle(Handle&& h) noexcept:h_(h.release()) {}
    Handle& operator=(Handle&& h) noexcept { if(this!=&h) reset(h.release()); return *this; }
    Handle(const Handle&)=delete;
    HANDLE get() const { return h_; }
    explicit operator bool() const { return h_ && h_!=INVALID_HANDLE_VALUE; }
    HANDLE release() { return std::exchange(h_,nullptr); }
    void reset(HANDLE h=nullptr) { if(*this) CloseHandle(h_); h_=h; }
private: HANDLE h_;
};
struct ProcessStamp { DWORD pid=0; uint64_t created=0; };
class ProcessIdentity {
public:
    bool open(DWORD);
    bool adopt(HANDLE,ProcessStamp);
    bool matches(ProcessStamp) const;
    bool samePrincipal(const ProcessIdentity&) const;
    bool alive() const;
    bool elevated() const { return elevated_; }
    HANDLE handle() const { return handle_.get(); }
    ProcessStamp stamp() const { return stamp_; }
private:
    Handle handle_;
    ProcessStamp stamp_{};
    std::vector<BYTE> user_,logon_;
    DWORD session_=0;
    bool elevated_=false;
};
bool randomBytes(void*,ULONG);
std::wstring logonSecurityDescriptor();
// Current-user-only protected DACL for private credential files: the elevated
// same-user engine reads through it, any other principal is denied.
std::wstring userSecurityDescriptor();
class SecurityAttributes {
public:
    SecurityAttributes();
    ~SecurityAttributes();
    SECURITY_ATTRIBUTES* get() { return descriptor_?&attributes_:nullptr; }
private:
    PSECURITY_DESCRIPTOR descriptor_=nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};
}
