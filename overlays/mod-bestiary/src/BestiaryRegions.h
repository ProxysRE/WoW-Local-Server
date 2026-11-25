// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef BESTIARY_REGIONS_H
#define BESTIARY_REGIONS_H

#include <cstdint>
#include <cstddef>

namespace Bestiary
{
constexpr bool ShouldTrack(std::uint32_t rank, bool worldMap, std::size_t spawns, bool moving)
{
    return worldMap && (rank == 2 || rank == 4) && (moving || spawns > 1);
}

// Area IDs occupy the low half; map/mode keys never collide with world zones.
constexpr std::uint32_t MapRegion(std::uint32_t map, std::uint32_t mode = 0)
{
    return 0x80000000u | (map << 3) | mode;
}

// Match Creature::InitEntry: heroic raids fall back to the matching normal size,
// dungeon heroic and raid 25 normal fall back to the base template.
template<class Lookup>
std::uint32_t DifficultyEntry(std::uint32_t base, std::uint32_t mode, bool raid, Lookup lookup)
{
    while (mode)
    {
        if (std::uint32_t entry = lookup(mode - 1))
            return entry;
        mode = raid && mode >= 2 ? mode - 2 : mode - 1;
    }
    return base;
}
}
#endif
