// SPDX-License-Identifier: AGPL-3.0-or-later
#include "QoLPolicy.h"
#include <cassert>
#include <limits>

int main()
{
    using namespace LocalQoL;
    assert(CraftTime(3000, .1f, 200) == 300);
    assert(CraftTime(1000, .1f, 200) == 200);
    assert(CraftTime(100, .1f, 200) == 100); // never slow an already fast spell
    assert(CraftTime(0, .1f, 200) == 0);
    assert(CraftTime(3000, 0, 200) == 3000);
    assert(CraftTime(3000, -1, 200) == 3000);
    assert(CraftTime(3000, 2, 200) == 3000);
    assert(CraftTime(3000, std::numeric_limits<float>::quiet_NaN(), 200) == 3000);
    assert(CraftTime(3000, std::numeric_limits<float>::infinity(), 200) == 3000);
    assert(CraftTime(3000, .000001f, -100) == 1);
    assert(SaleValue(12, 20, 0, 1000) == 240); // entire stack, not last looted count
    assert(SaleValue(12, 20, 800, 1000) == 0); // no item loss at money cap
    assert(SaleValue(12, 20, 760, 1000) == 240);
    assert(SaleValue(0, 20, 0, 1000) == 0);
    assert(SaleValue(12, 0, 0, 1000) == 0);
    assert(SaleValue(0xffffffffu, 0xffffffffu, 0, 0xfffffffeu) == 0);
    assert(SaleValue(1, 1, 1001, 1000) == 0);
    assert(!UnsafeFishing(true, false, false, false, false, true, true));
    assert(UnsafeFishing(false, false, false, false, false, true, true));
    assert(UnsafeFishing(true, true, false, false, false, true, true));
    assert(UnsafeFishing(true, false, true, false, false, true, true));
    assert(UnsafeFishing(true, false, false, true, false, true, true));
    assert(UnsafeFishing(true, false, false, false, true, true, true));
    assert(UnsafeFishing(true, false, false, false, false, false, true));
    assert(UnsafeFishing(true, false, false, false, false, true, false));
}
