// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace BotGear
{
struct Rule
{
    uint32_t item;
    int stage;
    uint8_t raidSize;
    bool heroic;
    bool hardMode;
};
inline constexpr Rule Catalog[] = {
#include "GearCatalog.inc"
};

inline int StageFor(uint32_t item)
{
    auto it = std::lower_bound(std::begin(Catalog), std::end(Catalog), item,
        [](Rule const& rule, uint32_t id) { return rule.item < id; });
    return it != std::end(Catalog) && it->item == item ? it->stage : -1;
}

inline bool Allowed(int configuredStage, int itemStage)
{
    return configuredStage < 0 || (itemStage >= 0 && itemStage <= configuredStage);
}

inline bool SourceAllowed(int stage, uint8_t raidSize, bool heroic, bool hardModes, Rule const& source)
{
    if (!Allowed(stage, source.stage))
        return false;
    // Earlier tiers are fully unlocked. Classic 20/40 and TBC fixed-size raids
    // must never be restricted by a WotLK 10/25 setting.
    if (stage < 0 || source.stage < stage || source.stage < 13 || source.raidSize == 0)
        return true;
    return source.raidSize <= raidSize && (!source.heroic || heroic) && (!source.hardMode || hardModes);
}

inline bool ItemAllowed(uint32_t item, int stage, uint8_t raidSize, bool heroic, bool hardModes)
{
    if (stage < 0)
        return true;
    auto it = std::lower_bound(std::begin(Catalog), std::end(Catalog), item,
        [](Rule const& rule, uint32_t id) { return rule.item < id; });
    for (; it != std::end(Catalog) && it->item == item; ++it)
        if (SourceAllowed(stage, raidSize, heroic, hardModes, *it))
            return true;
    return false;
}
}
