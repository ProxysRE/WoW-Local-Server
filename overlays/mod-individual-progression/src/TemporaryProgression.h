// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef LOCAL_TEMPORARY_PROGRESSION_H
#define LOCAL_TEMPORARY_PROGRESSION_H

#include <cstdint>
#include <optional>

namespace LocalIP
{
// Session-only selection. The stored stage is supplied by IP and never owned here.
class TemporaryProgression
{
public:
    static constexpr bool Valid(std::uint32_t stage)
    {
        return stage <= 18 && stage != 11; // IP has no stage 11.
    }

    bool Select(std::uint32_t requested, std::uint8_t stored)
    {
        if (!Valid(requested) || requested > stored)
            return false;
        _stage = static_cast<std::uint8_t>(requested);
        return true;
    }

    void Clear() { _stage.reset(); }
    bool Active() const { return _stage.has_value(); }
    std::uint8_t Effective(std::uint8_t stored) const { return _stage.value_or(stored); }

private:
    std::optional<std::uint8_t> _stage;
};
}
#endif
