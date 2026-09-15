#include "zzlogg/update/urlpolicy.h"
#include <algorithm>
namespace zzlogg::update {
namespace {
int hex(char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    if(c>='A'&&c<='F') return c-'A'+10;
    return -1;
}
bool dnsName(std::string_view host) {
    if(host.empty() || host.size()>253) return false;
    size_t start=0;
    while(start<host.size()) {
        auto end=host.find('.',start);
        if(end==host.npos) end=host.size();
        const auto label=host.substr(start,end-start);
        if(label.empty() || label.size()>63 || label.front()=='-' || label.back()=='-') return false;
        for(char c:label) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) return false;
        start=end+1;
    }
    return host.back()!='.';
}
}
bool isAllowedUpdateUrl(std::string_view url,const std::vector<std::string>& hosts) {
    if(url.substr(0,8)!="https://" || url.size()>8192) return false;
    constexpr std::string_view uriChars=
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?[]@!$&'()*+,;=%";
    for(size_t i=0;i<url.size();++i) {
        if(uriChars.find(url[i])==uriChars.npos) return false;
        if(url[i]=='%') {
            if(i+2>=url.size() || hex(url[i+1])<0 || hex(url[i+2])<0) return false;
            const int byte=hex(url[i+1])*16+hex(url[i+2]);
            if(byte<32 || byte==127 || byte=='\\') return false;
            i+=2;
        }
    }
    const auto end=url.find_first_of("/?",8);
    auto authority=url.substr(8,end==url.npos ? url.size()-8 : end-8);
    const auto colon=authority.find(':');
    if(colon!=authority.npos) {
        if(authority.substr(colon)!=":443") return false;
        authority=authority.substr(0,colon);
    }
    std::string host(authority);
    for(auto& c:host) if(c>='A'&&c<='Z') c=char(c-'A'+'a');
    if(!dnsName(host)) return false;
    return std::any_of(hosts.begin(),hosts.end(),[&](const std::string& allowed){
        return dnsName(allowed) && allowed==host;
    });
}
}
