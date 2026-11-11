// Local-server arena bridge. GPL-3.0-or-later.
#ifndef LOCAL_ARENA_BOT_POLICY_H
#define LOCAL_ARENA_BOT_POLICY_H

#include <cmath>
#include <cstdint>

namespace LocalArena
{
struct Candidate
{
    unsigned level;
    unsigned progression;
    float itemLevel;
};

inline bool IsSuitable(Candidate human, Candidate bot, bool compareProgression, float maxItemLevelGap)
{
    return human.level == bot.level && (!compareProgression || human.progression == bot.progression) &&
           std::isfinite(human.itemLevel) && std::isfinite(bot.itemLevel) &&
           std::abs(human.itemLevel - bot.itemLevel) <= maxItemLevelGap;
}

enum class PairResult { Keep, Cancel, Complete };

struct PairSnapshot
{
    bool humanOnline;
    bool botOnline;
    bool humanPresent;
    bool botPresent;
    bool sameArena;
    bool ended;
    bool hadSharedArena;
    std::uint32_t elapsed;
};

inline PairResult EvaluatePair(PairSnapshot state, std::uint32_t joinTimeout, std::uint32_t matchTimeout)
{
    if (!state.humanOnline || !state.botOnline)
        return PairResult::Cancel;
    if (state.sameArena)
    {
        if (state.ended)
            return PairResult::Complete;
        return state.elapsed >= matchTimeout ? PairResult::Cancel : PairResult::Keep;
    }
    if (state.hadSharedArena || !state.humanPresent || !state.botPresent || state.elapsed >= joinTimeout)
        return PairResult::Cancel;
    return PairResult::Keep;
}
}
#endif
