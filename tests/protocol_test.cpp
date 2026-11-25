#include "../overlays/mod-bestiary/src/BestiaryProtocol.h"
#include "../overlays/mod-bestiary/src/BestiaryRegions.h"
#include <cassert>
#include <set>
#include <iostream>

int main()
{
    Bestiary::Request request;
    assert(Bestiary::Parse("Q|2|123|Z|10|0", request));
    assert(request.target == 10 && request.page == 0 && request.kind == 'Z');
    for (auto text : {"Q|1|123|Z|10|0", "Q|2|0|Z|10|0", "Q|2|1|X|10|0",
        "Q|2|1|Z|-10|0", "Q|2|1|Z|10|0junk", "Q|2|1|Z|10|4294967296", "Q|2|1|Z|10|0|extra"})
        assert(!Bestiary::Parse(text, request));
    assert(Bestiary::Parse("Q|2|124|L|0|0", request));
    assert(Bestiary::Parse("Q|2|126|P|521|0", request));
    assert(!Bestiary::ShouldTrack(0, true, 20, true));
    assert(!Bestiary::ShouldTrack(1, true, 20, true));
    assert(!Bestiary::ShouldTrack(4, true, 1, false));
    assert(Bestiary::ShouldTrack(4, true, 2, false));
    assert(Bestiary::ShouldTrack(2, true, 1, true));
    assert(!Bestiary::ShouldTrack(4, false, 2, true));
    assert(Bestiary::Parse("Q|2|125|Z|2147483963|0", request));
    std::set<unsigned> keys;
    for (unsigned map : {0u, 1u, 30u, 249u, 530u, 571u, 631u, 724u})
        for (unsigned mode = 0; mode < 4; ++mode)
        {
            auto key = Bestiary::MapRegion(map, mode);
            assert(key >= 0x80000000u && keys.insert(key).second);
        }
    unsigned alternatives[] = {101, 0, 0};
    auto lookup = [&alternatives](unsigned i) { return alternatives[i]; };
    assert(Bestiary::DifficultyEntry(100, 3, true, lookup) == 101); // 25 heroic -> 25 normal
    assert(Bestiary::DifficultyEntry(100, 2, true, lookup) == 100); // 10 heroic -> base
    assert(Bestiary::DifficultyEntry(100, 1, false, lookup) == 101); // dungeon heroic
    alternatives[2] = 103;
    assert(Bestiary::DifficultyEntry(100, 3, true, lookup) == 103);
    alternatives[0] = 0;
    assert(Bestiary::DifficultyEntry(100, 1, true, lookup) == 100);
    assert(Bestiary::Escape("name|100%\n\t") == "name%7C100%25%0A%09");
    std::string payload = "H|Z|10|0|1\nM|521|" + Bestiary::Escape("Лупос|%") + "|23|23|4|3|1|521";
    payload += std::string(500, 'a');
    std::string rebuilt;
    auto frames = Bestiary::Frames(1000000000, payload);
    for (auto const& frame : frames)
    {
        assert(frame.size() <= 255);
        auto start = frame.find('|', frame.find('|', frame.find('|') + 1) + 1);
        rebuilt += frame.substr(start + 1);
    }
    assert(rebuilt == payload);
    // Emit actual C++ transport frames for the Lua 5.1 cross-language test.
    for (auto const& frame : Bestiary::Frames(1, "H|Z|10|0|1\nM|521|" +
        Bestiary::Escape("Лупос|%") + "|23|23|4|3|1|521"))
        std::cout << frame << '\0';
}
