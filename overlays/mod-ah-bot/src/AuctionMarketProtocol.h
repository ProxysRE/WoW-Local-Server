#ifndef AUCTION_MARKET_PROTOCOL_H
#define AUCTION_MARKET_PROTOCOL_H
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
namespace AuctionMarket
{
inline bool Number(std::string_view text, uint64_t& result, int base = 10)
{
    if (text.empty())
        return false;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), result, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}
inline std::vector<std::string_view> Fields(std::string_view text)
{
    std::vector<std::string_view> fields;
    while (fields.size() <= 6)
    {
        auto end = text.find('|');
        fields.push_back(text.substr(0, end));
        if (end == text.npos)
            break;
        text.remove_prefix(end + 1);
    }
    return fields;
}
inline std::string Hex(std::string const& text)
{
    std::string result;
    for (unsigned char c : text)
    {
        result += "0123456789abcdef"[c >> 4];
        result += "0123456789abcdef"[c & 15];
    }
    return result;
}
inline bool Unhex(std::string_view text, std::string& result)
{
    result.clear();
    if (text.size() > 128 || text.size() % 2)
        return false;
    for (size_t i = 0; i < text.size(); i += 2)
    {
        uint64_t value;
        if (!Number(text.substr(i, 2), value, 16) || value < 32 || value == 127)
            return false;
        result += char(value);
    }
    return true;
}
struct Request
{
    uint32_t id = 0;
    char action = 0;
    uint64_t npc = 0;
    uint32_t target = 0;
    uint32_t quantity = 0;
    std::string query;
};
inline bool Parse(std::string_view text, Request& request)
{
    if (text.size() > 230)
        return false;
    auto f = Fields(text);
    uint64_t id, npc, target, quantity;
    if (f.size() < 4 || !Number(f[0], id) || !id || id > UINT32_MAX ||
        f[1].size() != 1 || !Number(f[2], npc, 16) || !npc || !Number(f[3], target) || target > UINT32_MAX)
        return false;
    request = {};
    request.id = uint32_t(id);
    request.action = f[1][0];
    request.npc = npc;
    request.target = uint32_t(target);
    if (request.action == 'L')
        return f.size() == 5 && target <= 500 && Unhex(f[4], request.query);
    if (request.action == 'Q')
    {
        if (f.size() != 5 || !target || !Number(f[4], quantity) || !quantity || quantity > 1000)
            return false;
        request.quantity = uint32_t(quantity);
        return true;
    }
    return request.action == 'B' && f.size() == 4 && target != 0;
}
}
#endif
