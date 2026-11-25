#include "../overlays/mod-bestiary/src/BestiaryProtocol.h"
#include <cassert>
#include <iostream>

int main()
{
    Bestiary::Request request;
    assert(Bestiary::Parse("Q|1|123|Z|10|0", request));
    assert(request.target == 10 && request.page == 0 && request.kind == 'Z');
    for (auto text : {"Q|2|123|Z|10|0", "Q|1|0|Z|10|0", "Q|1|1|X|10|0",
        "Q|1|1|Z|-10|0", "Q|1|1|Z|10|0junk", "Q|1|1|Z|10|4294967296", "Q|1|1|Z|10|0|extra"})
        assert(!Bestiary::Parse(text, request));
    assert(Bestiary::Escape("name|100%\n\t") == "name%7C100%25%0A%09");
    std::string payload = "H|Z|10|0|1\nM|521|" + Bestiary::Escape("Лупос|%") + "|23|23|4|3|1";
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
        Bestiary::Escape("Лупос|%") + "|23|23|4|3|1"))
        std::cout << frame << '\0';
}
