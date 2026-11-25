// SPDX-License-Identifier: GPL-3.0-or-later
#include "GroupBuilderPolicy.h"
#include <cassert>
#include <iostream>
using namespace GroupBuilder;
Candidate Bot(unsigned id, unsigned role, unsigned cls, unsigned spec, unsigned ilvl, bool member = false)
{
    Candidate c;
    c.guid = id;
    c.role = role;
    c.cls = cls;
    c.spec = spec;
    c.ilvl = ilvl;
    c.member = member;
    return c;
}
int main()
{
    std::vector<Filter> slots = {{Damage, 0, 0, 0}, {Damage, 8, 1, 0}};
    std::vector<Candidate> bots = {Bot(1, Damage, 8, 1, 125), Bot(2, Damage, 9, 3, 126)};
    auto result = Assign(slots, bots, 125);
    assert(result[0] == 1 && result[1] == 0); // Generic DPS must not consume the only arcane mage.
    bots = {Bot(1, Damage, 8, 2, 140), Bot(2, Damage, 8, 1, 123), Bot(3, Damage, 8, 1, 122)};
    result = Assign({slots[1]}, bots, 125);
    assert(result[0] == 1); // Strict spec, closest lower ilvl; never substitute a stronger fire mage.
    bots.push_back(Bot(4, Damage, 8, 1, 125));
    assert(Assign({slots[1]}, bots, 125)[0] == 3);
    slots[1].pin = 3;
    assert(Assign({slots[1]}, bots, 125)[0] == 2); // A pin outranks gear preference.
    slots[1].pin = 99;
    assert(Assign({slots[1]}, bots, 125)[0] == -1);
    slots[1].pin = 0;
    assert(Assign({{Healer, 8, 1, 0}}, bots, 125)[0] == -1); // No role relaxation.
    bots = {Bot(1, Damage, 8, 1, 100, true), Bot(2, Damage, 8, 1, 200)};
    assert(Assign({slots[0]}, bots, 125)[0] == 0); // Existing members cannot be replaced by better-geared bots.
    result = Assign(std::vector<Filter>(40), {Bot(1, Damage, 8, 1, 125)}, 125);
    unsigned occupied = 0;
    for (auto r : result)
        occupied += r >= 0;
    assert(occupied == 1); // No duplicate invitations, including in a 40-player roster.
    auto fields = std::vector<Filter>(5);
    assert(ReadSlots("0:1:1:3:0,1:2:5:2:0,2:3:8:1:123", 5, fields));
    assert(fields[2].cls == 8 && fields[2].spec == 1 && fields[2].pin == 123);
    auto before = fields;
    for (auto text : {"0:3:8:1:0,0:1:1:1:0", "5:3:8:1:0", "0:4:8:1:0", "0:3:10:1:0", "0:3:0:1:0", "0:3:8:4:0",
                      "0:3:8:1:-1", "0:3:8:1:4294967296", "0:3:8:1:0,", "0:3:8:1:0:9"})
    {
        assert(!ReadSlots(text, 5, fields));
        assert(fields[2].pin == before[2].pin); // Invalid batches are atomic.
    }
    assert(!ReadSlots("0:3:8:1:0", 40, fields));
    assert(Assign({}, {}, 125).empty());
    std::cout
        << "Group builder: global strict matching, soft ilvl, pins, existing members, duplicates and parsing passed\n";
}
