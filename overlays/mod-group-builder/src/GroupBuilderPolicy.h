// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AC_GROUP_BUILDER_POLICY_H
#define AC_GROUP_BUILDER_POLICY_H
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace GroupBuilder
{
constexpr unsigned MaxSlots = 40;
constexpr std::int64_t Impossible = 100000000000000000LL;
constexpr std::int64_t Empty = 1000000000000000LL;
constexpr std::int64_t NewMember = 1000000000000LL;
enum Role : unsigned
{
    Any = 0,
    Tank = 1,
    Healer = 2,
    Damage = 3
};
struct Filter
{
    unsigned role = Damage, cls = 0, spec = 0;
    std::uint32_t pin = 0;
};
struct Candidate
{
    std::uint32_t guid = 0;
    unsigned role = Any, cls = 0, spec = 0, level = 0, ilvl = 0;
    bool member = false;
    std::string name;
};
struct Options
{
    unsigned size = 5, minLevel = 1, maxLevel = 80, ilvl = 0, guild = 0;
};
inline bool Class(unsigned cls) { return cls <= 11 && cls != 10; }
enum PlayerClass : unsigned
{
    WarriorClass = 1, PaladinClass = 2, HunterClass = 3, RogueClass = 4, PriestClass = 5,
    DeathKnightClass = 6, ShamanClass = 7, MageClass = 8, WarlockClass = 9, DruidClass = 11
};
// Active talent pages are 1-based; a generic damage strategy is not proof of a DPS build.
inline bool DamageTree(unsigned cls, unsigned spec)
{
    switch (cls)
    {
        case HunterClass:
        case RogueClass:
        case MageClass:
        case WarlockClass:
            return true;
        case WarriorClass:
        case ShamanClass:
        case DruidClass:
            return spec == 1 || spec == 2;
        case PaladinClass:
        case PriestClass:
            return spec == 3;
        case DeathKnightClass:
            // Blood DPS is valid. Tank equipment and AI are checked separately.
            return spec >= 1 && spec <= 3;
        default:
            return false;
    }
}
inline bool Number(std::string_view text, std::uint32_t& n)
{
    if (text.empty())
        return false;
    auto r = std::from_chars(text.data(), text.data() + text.size(), n);
    return r.ec == std::errc{} && r.ptr == text.data() + text.size();
}
inline bool Matches(Filter const& f, Candidate const& c)
{
    return (!f.pin || f.pin == c.guid) && (!f.role || f.role == c.role) && (!f.cls || f.cls == c.cls) &&
           (!f.spec || f.spec == c.spec);
}
inline std::int64_t Cost(Filter const& f, Candidate const& c, unsigned preferred)
{
    if (!Matches(f, c))
        return Impossible;
    // Existing members must be retained. Ilvl is a preference; role/class/spec never relax.
    std::int64_t gear = c.ilvl >= preferred ? 1000 - std::min(1000U, c.ilvl) : 1000000 + (preferred - c.ilvl) * 1000;
    return (c.member ? 0 : NewMember) + gear;
}
// Rectangular Hungarian assignment: global matching avoids spending a scarce spec on a generic slot.
// Each candidate can occupy one slot. Dummy columns permit unfilled, impossible strict slots.
inline std::vector<int> Assign(std::vector<Filter> const& filters, std::vector<Candidate> const& bots,
                               unsigned preferred)
{
    std::size_t n = filters.size(), m = bots.size() + n;
    std::vector<std::int64_t> u(n + 1), v(m + 1);
    std::vector<std::size_t> p(m + 1), way(m + 1);
    for (std::size_t i = 1; i <= n; ++i)
    {
        p[0] = i;
        std::size_t j0 = 0;
        std::vector<std::int64_t> best(m + 1, Impossible);
        std::vector<bool> used(m + 1);
        do
        {
            used[j0] = true;
            std::size_t i0 = p[j0], j1 = 0;
            std::int64_t delta = Impossible;
            for (std::size_t j = 1; j <= m; ++j)
                if (!used[j])
                {
                    auto cost = j <= bots.size() ? Cost(filters[i0 - 1], bots[j - 1], preferred) : Empty;
                    auto current = cost - u[i0] - v[j];
                    if (current < best[j])
                    {
                        best[j] = current;
                        way[j] = j0;
                    }
                    if (best[j] < delta)
                    {
                        delta = best[j];
                        j1 = j;
                    }
                }
            for (std::size_t j = 0; j <= m; ++j)
                if (used[j])
                {
                    u[p[j]] += delta;
                    v[j] -= delta;
                }
                else
                    best[j] -= delta;
            j0 = j1;
        } while (p[j0]);
        do
        {
            auto j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0);
    }
    std::vector<int> result(n, -1);
    for (std::size_t j = 1; j <= bots.size(); ++j)
        if (p[j] && Cost(filters[p[j] - 1], bots[j - 1], preferred) < Empty)
            result[p[j] - 1] = static_cast<int>(j - 1);
    return result;
}
inline bool ReadSlots(std::string_view text, unsigned size, std::vector<Filter>& filters)
{
    if (!size || size > MaxSlots || filters.size() != size)
        return false;
    auto copy = filters;
    std::set<unsigned> seen;
    while (!text.empty())
    {
        auto comma = text.find(',');
        auto row = text.substr(0, comma);
        std::uint32_t parts[5]{};
        for (unsigned i = 0; i < 5; ++i)
        {
            auto colon = row.find(':');
            if (!Number(row.substr(0, colon), parts[i]) || (i < 4 && colon == std::string_view::npos) ||
                (i == 4 && colon != std::string_view::npos))
                return false;
            if (colon != std::string_view::npos)
                row.remove_prefix(colon + 1);
        }
        if (parts[0] >= size || parts[1] > Damage || !Class(parts[2]) || parts[3] > 3 || (!parts[2] && parts[3]) ||
            !seen.insert(parts[0]).second)
            return false;
        copy[parts[0]] = {parts[1], parts[2], parts[3], parts[4]};
        if (comma == std::string_view::npos)
            break;
        text.remove_prefix(comma + 1);
        if (text.empty())
            return false;
    }
    if (seen.empty())
        return false;
    filters = std::move(copy);
    return true;
}
inline std::string Hex(std::string_view text)
{
    constexpr char d[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : text)
    {
        result += d[c >> 4];
        result += d[c & 15];
    }
    return result.empty() ? "-" : result;
}
} // namespace GroupBuilder
#endif
