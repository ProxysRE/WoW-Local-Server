#include "TemporaryProgression.h"
#include <cassert>
#include <limits>

int main()
{
    LocalIP::TemporaryProgression visit;
    assert(!visit.Active());
    assert(visit.Effective(18) == 18);
    assert(visit.Select(6, 18));
    assert(visit.Active() && visit.Effective(18) == 6);
    assert(!visit.Select(11, 18));
    assert(!visit.Select(19, 18));
    assert(!visit.Select(std::numeric_limits<unsigned>::max(), 18));
    assert(visit.Effective(18) == 6); // Invalid input cannot end/change a visit.
    assert(!visit.Select(8, 7)); // Can't unlock a future era.
    assert(visit.Effective(18) == 6);
    assert(visit.Select(0, 18)); // Stage zero is a real override, not "off".
    assert(visit.Active() && visit.Effective(18) == 0);
    assert(visit.Select(7, 18));
    visit.Clear();
    assert(!visit.Active() && visit.Effective(18) == 18);
    assert(visit.Effective(8) == 8);
    LocalIP::TemporaryProgression nextSession;
    assert(!nextSession.Active() && nextSession.Effective(18) == 18);
}
