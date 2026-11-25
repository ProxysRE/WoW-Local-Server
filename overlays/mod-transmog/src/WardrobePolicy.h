// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef AC_WARDROBE_POLICY_H
#define AC_WARDROBE_POLICY_H

#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace Wardrobe
{
using Plan = std::map<std::uint32_t, std::uint32_t>;
inline bool Slot(std::uint32_t slot) { return slot == 0 || (slot >= 2 && slot <= 9) || (slot >= 14 && slot <= 18); }

inline bool Number(std::string_view text, std::uint32_t& value)
{
    if (text.empty())
        return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

inline bool ReadPlan(std::string_view text, Plan& result)
{
    result.clear();
    if (text.empty() || text.size() > 190)
        return false;
    while (!text.empty())
    {
        auto comma = text.find(',');
        auto pair = text.substr(0, comma);
        auto equal = pair.find('=');
        std::uint32_t slot = 0, entry = 0;
        if (equal == std::string_view::npos || !Number(pair.substr(0, equal), slot) ||
            !Number(pair.substr(equal + 1), entry) || !Slot(slot) || !result.emplace(slot, entry).second)
            return false;
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
        if (text.empty())
            return false;
    }
    return !result.empty();
}

inline std::string Hex(std::string_view text)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char byte : text)
    {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result.empty() ? "-" : result;
}

inline bool Unhex(std::string_view text, std::string& result)
{
    result.clear();
    if (text == "-")
        return true;
    if (text.size() > 120 || text.size() % 2)
        return false;
    auto digit = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2)
    {
        int high = digit(text[i]), low = digit(text[i + 1]);
        if (high < 0 || low < 0 || (high == 0 && low < 32) || (high == 7 && low == 15))
            return false;
        result += static_cast<char>(high * 16 + low);
    }
    return true;
}
} // namespace Wardrobe
#endif
