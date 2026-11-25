// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BESTIARY_PROTOCOL_H
#define BESTIARY_PROTOCOL_H

#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Bestiary
{
constexpr std::string_view Prefix = "WBE2\t";
constexpr std::size_t PageSize = 8;
constexpr std::size_t ChunkSize = 180;

inline std::string Escape(std::string_view value)
{
    std::string result;
    char const* hex = "0123456789ABCDEF";
    for (unsigned char c : value)
    {
        if (c == '%' || c == '|' || c < 32 || c == 127)
        {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 15];
        }
        else
            result += c;
    }
    return result;
}

inline std::vector<std::string_view> Split(std::string_view value)
{
    std::vector<std::string_view> result;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= value.size(); ++i)
        if (i == value.size() || value[i] == '|')
        {
            result.push_back(value.substr(start, i - start));
            start = i + 1;
        }
    return result;
}

inline bool Number(std::string_view text, std::uint32_t& value)
{
    if (text.empty() || text.size() > 10)
        return false;
    auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

struct Request
{
    std::uint32_t id = 0;
    char kind = 0;
    std::uint32_t target = 0;
    std::uint32_t page = 0;
};

inline bool Parse(std::string_view text, Request& request)
{
    auto const fields = Split(text);
    if (fields.size() != 6 || fields[0] != "Q" || fields[1] != "2" ||
        (fields[3] != "L" && fields[3] != "Z" && fields[3] != "D"))
        return false;
    request.kind = fields[3][0];
    return Number(fields[2], request.id) && request.id > 0 && request.id <= 1000000000 &&
        Number(fields[4], request.target) && Number(fields[5], request.page);
}

// Each frame stays below the 255-byte 3.3.5a addon-message limit, including prefix.
inline std::vector<std::string> Frames(std::uint32_t id, std::string const& payload)
{
    std::vector<std::string> result;
    std::size_t count = (payload.size() + ChunkSize - 1) / ChunkSize;
    for (std::size_t i = 0; i < count; ++i)
        result.push_back(std::string(Prefix) + std::to_string(id) + "|" + std::to_string(i + 1) +
            "|" + std::to_string(count) + "|" + payload.substr(i * ChunkSize, ChunkSize));
    return result;
}
}
#endif
