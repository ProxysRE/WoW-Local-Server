#include "ArenaBotPolicy.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace LocalArena;
    Candidate player{80, 13, 200.0f};
    assert(IsSuitable(player, {80, 13, 215.0f}, true, 15.0f));
    assert(!IsSuitable(player, {80, 13, 215.1f}, true, 15.0f));
    assert(!IsSuitable(player, {79, 13, 200.0f}, true, 15.0f));
    assert(!IsSuitable(player, {80, 18, 200.0f}, true, 15.0f));
    assert(IsSuitable(player, {80, 18, 200.0f}, false, 15.0f));
    assert(!IsSuitable(player, {80, 13, std::numeric_limits<float>::quiet_NaN()}, true, 15.0f));
    PairSnapshot state{true, true, true, true, false, false, false, 1};
    assert(EvaluatePair(state, 90, 1200) == PairResult::Keep);
    state.humanOnline = false;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Cancel);
    state.humanOnline = true;
    state.botPresent = false;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Cancel);
    state.botPresent = true;
    state.elapsed = 90;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Cancel);
    state.sameArena = true;
    state.hadSharedArena = true;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Keep);
    state.elapsed = 1200;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Cancel);
    state.ended = true;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Complete);
    state.sameArena = false;
    state.elapsed = 5;
    assert(EvaluatePair(state, 90, 1200) == PairResult::Cancel);
}
