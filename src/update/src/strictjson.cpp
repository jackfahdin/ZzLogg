#include "strictjson.h"
#include <set>
#include <vector>
namespace zzlogg::update::detail {
namespace {
struct ParseRejected {};
constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}
std::optional<nlohmann::json> parseStrictJson(std::string_view input, size_t limit) {
    if (input.size() > limit || input.find('\0') != input.npos
        || input.substr(0,3) == "\xef\xbb\xbf")
        return {};
    std::vector<std::set<std::string>> objects;
    using Event = nlohmann::json::parse_event_t;
    auto callback = [&](int depth, Event event, nlohmann::json& value) {
        if ((event == Event::object_start || event == Event::array_start) && depth >= 16)
            throw ParseRejected{};
        if (event == Event::object_start)
            objects.emplace_back();
        else if (event == Event::object_end)
            objects.pop_back();
        else if (event == Event::key
                 && !objects.back().insert(value.get<std::string>()).second)
            throw ParseRejected{};
        return true;
    };
    try {
        return nlohmann::json::parse(input, callback, true, false);
    } catch (const ParseRejected&) {
        return {};
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

std::string encodeBase64(std::string_view input) {
    std::string result;
    for (size_t i=0; i<input.size(); i+=3) {
        const auto a=static_cast<unsigned char>(input[i]);
        const auto b=i+1<input.size() ? static_cast<unsigned char>(input[i+1]) : 0;
        const auto c=i+2<input.size() ? static_cast<unsigned char>(input[i+2]) : 0;
        result += alphabet[a>>2];
        result += alphabet[((a&3)<<4)|(b>>4)];
        result += i+1<input.size() ? alphabet[((b&15)<<2)|(c>>6)] : '=';
        result += i+2<input.size() ? alphabet[c&63] : '=';
    }
    return result;
}

std::optional<std::string> decodeBase64(std::string_view input, size_t limit) {
    if (input.size()%4) return {};
    if (input.empty()) return std::string{};
    const size_t padding = (input.back()=='=') + (input[input.size()-2]=='=');
    const size_t decodedSize = input.size()/4*3-padding;
    if (decodedSize > limit) return {};
    std::string result;
    result.reserve(decodedSize);
    for (size_t i=0; i<input.size(); i+=4) {
        unsigned word=0;
        for (size_t j=0; j<4; ++j) {
            const char c=input[i+j];
            if (c=='=' && i+j>=input.size()-padding) {
                word <<= 6;
            } else {
                const auto index=alphabet.find(c);
                if (index==alphabet.npos) return {};
                word=(word<<6)|static_cast<unsigned>(index);
            }
        }
        result += static_cast<char>(word>>16);
        if (result.size()<decodedSize) result += static_cast<char>(word>>8);
        if (result.size()<decodedSize) result += static_cast<char>(word);
    }
    if (encodeBase64(result)!=input) return {};
    return result;
}
}
