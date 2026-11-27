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
}
