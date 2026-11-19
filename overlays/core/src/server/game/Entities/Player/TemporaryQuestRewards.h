// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef AC_TEMPORARY_QUEST_REWARDS_H
#define AC_TEMPORARY_QUEST_REWARDS_H

#include "DataMap.h"
#include <cstdint>
#include <optional>

// A session-only view for a contiguous range of synthetic progression quests.
// The actual rewarded quest container and persistence remain untouched.
struct TemporaryQuestRewards : DataMap::Base
{
    static constexpr char Key[] = "local-temporary-quest-reward-view";
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    std::uint32_t completedThrough = 0;

    std::optional<bool> Rewarded(std::uint32_t quest) const
    {
        if (!first || quest < first || quest > last)
            return std::nullopt;
        return quest <= completedThrough;
    }
};
#endif
