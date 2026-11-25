// SPDX-License-Identifier: GPL-3.0-or-later
#include "BotBuffPolicy.h"
#include <algorithm>
#include <cassert>
#include <deque>
#include <map>
#include <set>
#include <string>
using uint32 = std::uint32_t;
constexpr unsigned POWER_MANA = 0;
unsigned now = 1000;
unsigned GetMSTimeDiffToNow(unsigned time) { return now - time; }
struct SpellInfo
{
    unsigned id = 0, first = 0, rank = 1;
    bool positive = true, passive = false;
    bool IsPositive() const { return positive; }
    bool IsPassive() const { return passive; }
};
struct SpellMgr
{
    std::map<unsigned, SpellInfo> spells;
    unsigned GetFirstSpellInChain(unsigned id) { return spells.count(id) ? spells[id].first : id; }
    unsigned GetSpellRank(unsigned id) { return spells.count(id) ? spells[id].rank : 1; }
    SpellInfo* GetSpellInfo(unsigned id)
    {
        auto i = spells.find(id);
        return i == spells.end() ? nullptr : &i->second;
    }
} spellMgr;
auto* sSpellMgr = &spellMgr;
struct Aura
{
    unsigned id = 0, caster = 0;
    unsigned GetId() { return id; }
    unsigned GetCasterGUID() { return caster; }
};
struct AuraApplication
{
    Aura aura;
    Aura* GetBase() { return &aura; }
};
struct Gcd
{
    bool active = false;
    bool HasGlobalCooldown(SpellInfo*) { return active; }
};
struct Player
{
    unsigned cls = BotBuff::Mage, power = POWER_MANA, guid = 1;
    bool casting = false, mounted = false, moving = false, available = true;
    Gcd gcd;
    std::map<unsigned, int> spells;
    std::map<unsigned, AuraApplication*> auras;
    unsigned getClass() { return cls; }
    unsigned getPowerType() { return power; }
    auto const& GetSpellMap() { return spells; }
    bool HasSpell(unsigned id) { return spells.count(id); }
    auto const& GetAppliedAuras() { return auras; }
    unsigned GetGUID() { return guid; }
    bool IsNonMeleeSpellCast(bool) { return casting; }
    bool IsMounted() { return mounted; }
    Gcd& GetGlobalCooldownMgr() { return gcd; }
    bool isMoving() { return moving; }
    void StopMoving() { moving = false; }
};
struct PlayerbotAI
{
    std::set<unsigned> blocked;
    unsigned casts = 0, lastSpell = 0, delay = 0;
    bool CanCastSpell(unsigned id, Player*, bool known)
    {
        assert(known);
        return !blocked.count(id);
    }
    bool CastSpell(unsigned id, Player*)
    {
        ++casts;
        lastSpell = id;
        return true;
    }
    void SetNextCheckDelay(unsigned time) { delay = time; }
} ai;
struct Mgr
{
    PlayerbotAI* GetPlayerbotAI(Player*) { return &ai; }
} manager;
auto& sPlayerbotsMgr = manager;
struct CheckMountStateAction
{
    static void CompleteDismount(Player* p) { p->mounted = false; }
};
namespace BotBuff
{
constexpr unsigned Lifetime = 20000, StepDelay = 1800;
struct Request
{
    std::string mode;
    std::deque<Id> families;
    unsigned started = 1000, casts = 0, delay = 0;
    bool initialized = false;
};
void Tell(Player*, char const*) {}
bool Eligible(Player* bot, Player* target) { return bot && target && bot->available && target->available; }
// PRODUCTION_METHODS
} // namespace BotBuff
int main()
{
    using namespace BotBuff;
    assert(Mode("") && Mode("might") && !Mode("anything"));
    assert(Families(Paladin, "", true, true) == std::vector<Id>({Kings, Wisdom, Might}));
    assert(Families(Paladin, "", true, false) == std::vector<Id>({Wisdom, Might}));
    assert(Families(Paladin, "", false, false) == std::vector<Id>({Might, Wisdom}));
    assert(Families(Paladin, "might", true, true) == std::vector<Id>{Might});
    assert(Families(1, "", false, false).empty()); // Party-only warrior shouts are not fabricated for strangers.
    assert(Families(Mage, "kings", true, false).empty());
    Player bot, target;
    target.guid = 2;
    for (auto [id, first, rank] :
         {std::tuple{1459u, 1459u, 1u}, std::tuple{1460u, 1459u, 2u}, std::tuple{20217u, 20217u, 1u}})
        spellMgr.spells[id] = {id, first, rank};
    bot.spells = {{1459, 1}, {1460, 1}};
    auto ranks = Known(&bot, Intellect);
    assert(ranks.size() == 2 && ranks.front() == 1460);
    Request r;
    ai.blocked.insert(1460);
    assert(!Step(&bot, &target, r) && ai.lastSpell == 1459 &&
           ai.casts == 1); // Target level or mana prevents the top rank.
    assert(r.casts == 1 && r.families.empty() && ai.delay == StepDelay);
    assert(Step(&bot, &target, r));
    AuraApplication stronger{{1460, 9}};
    target.auras = {{1460, &stronger}};
    assert(Covered(&bot, &target, Intellect, 1459, true));
    ai.casts = 0;
    r = Request{};
    assert(Step(&bot, &target, r) && ai.casts == 0);
    AuraApplication ownKings{{Kings, 1}};
    target.auras = {{Kings, &ownKings}};
    assert(Covered(&bot, &target, Might, Might, true));
    assert(!Covered(&bot, &target, Might, Might,
                    false)); // Explicit blessing choice can replace this caster's other blessing.
    AuraApplication groupBuff{{Brilliance, 9}};
    target.auras = {{Brilliance, &groupBuff}};
    assert(Covered(&bot, &target, Intellect, 1460, true));
    target.auras.clear();
    r = Request{};
    bot.gcd.active = true;
    assert(!Step(&bot, &target, r) && r.families.size() == 1 &&
           ai.casts == 0); // Wait; do not silently drop buffs on GCD.
    bot.gcd.active = false;
    bot.mounted = true;
    assert(!Step(&bot, &target, r) && !bot.mounted && ai.casts == 0);
    bot.casting = true;
    assert(!Step(&bot, &target, r) && ai.casts == 0);
    bot.casting = false;
    bot.moving = true;
    assert(!Step(&bot, &target, r) && !bot.moving && ai.casts == 1);
    r = Request{};
    target.available = false;
    assert(Step(&bot, &target, r));
    target.available = true;
    now = Lifetime + 2000;
    assert(Step(&bot, &target, r));
    assert(Step(&bot, nullptr, r));
    now = 1000;
    bot.cls = Paladin;
    bot.spells = {{Kings, 1}, {Might, 1}};
    ai.casts = 0;
    ai.blocked.clear();
    spellMgr.spells[Might] = {Might, Might, 1};
    AuraApplication otherKings{{Kings, 9}};
    target.auras = {{Kings, &otherKings}};
    r = Request{};
    assert(!Step(&bot, &target, r) && ai.lastSpell == Might && ai.casts == 1 && r.families.empty());
}
