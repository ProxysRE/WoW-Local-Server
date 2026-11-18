// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace LocalQoL
{
inline std::int32_t CraftTime(std::int32_t original, float multiplier, std::int32_t minimum)
{
    if (original <= 0 || !std::isfinite(multiplier) || multiplier <= 0 || multiplier >= 1)
        return original;
    return std::min(original, std::max(std::max(1, minimum),
        static_cast<std::int32_t>(original * static_cast<double>(multiplier))));
}

inline std::uint32_t SaleValue(std::uint32_t price, std::uint32_t count,
    std::uint32_t money, std::uint32_t maximum)
{
    std::uint64_t total = static_cast<std::uint64_t>(price) * count;
    return total && money <= maximum && total <= maximum - money
        ? static_cast<std::uint32_t>(total) : 0;
}

enum class FishPhase { Idle, Waiting, Loot, Recast };
inline bool UnsafeFishing(bool alive, bool combat, bool moving, bool teleport,
    bool mounted, bool pole, bool enabled)
{
    return !alive || combat || moving || teleport || mounted || !pole || !enabled;
}
}
