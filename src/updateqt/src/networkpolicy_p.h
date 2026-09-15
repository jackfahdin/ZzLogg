#pragma once
#include <QUrl>
#include "zzlogg/update/urlpolicy.h"

namespace zzlogg::updateqt::detail {
inline bool allowed(const QUrl& url,const std::vector<std::string>& hosts)
{
    return url.isValid() && update::isAllowedUpdateUrl(url.toEncoded(QUrl::FullyEncoded).toStdString(),hosts);
}
inline bool redirectStatus(int status)
{
    return status==301 || status==302 || status==303 || status==307 || status==308;
}
inline bool validLocation(const QByteArray& value)
{
    if(value.isEmpty() || value.size()>8192) return false;
    // Validate the original bytes before QUrl normalizes whitespace or escapes.
    constexpr std::string_view chars=
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?[]@!$&'()*+,;=%";
    auto hex=[](char c) { return c>='0' && c<='9' ? c-'0'
        : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : -1; };
    for(qsizetype i=0;i<value.size();++i) {
        if(chars.find(value[i])==chars.npos) return false;
        if(value[i]=='%') {
            if(i+2>=value.size() || hex(value[i+1])<0 || hex(value[i+2])<0) return false;
            const int byte=hex(value[i+1])*16+hex(value[i+2]);
            if(byte<32 || byte==127 || byte=='\\') return false;
            i+=2;
        }
    }
    return true;
}
}
