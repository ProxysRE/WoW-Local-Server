// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef AC_BOT_BUFF_POLICY_H
#define AC_BOT_BUFF_POLICY_H
#include <cstdint>
#include <string_view>
#include <vector>
namespace BotBuff
{
using Id = std::uint32_t;
enum Class : unsigned
{
    Paladin = 2,
    Priest = 5,
    Mage = 8,
    Warlock = 9,
    Druid = 11
};
enum Spell : Id
{
    Intellect = 1459,
    Fortitude = 1243,
    Spirit = 14752,
    ShadowProtection = 976,
    Mark = 1126,
    Thorns = 467,
    Breath = 5697,
    Might = 19740,
    Wisdom = 19742,
    Kings = 20217,
    Sanctuary = 20911,
    GreaterMight = 25782,
    GreaterWisdom = 25894,
    GreaterKings = 25898,
    GreaterSanctuary = 25899,
    Brilliance = 23028,
    PrayerFortitude = 21562,
    PrayerSpirit = 27681,
    PrayerShadow = 27683,
    Gift = 21849
};
inline bool Mode(std::string_view mode)
{
    return mode.empty() || mode == "kings" || mode == "might" || mode == "wisdom" || mode == "sanctuary";
}
inline std::vector<Id> Families(unsigned cls, std::string_view mode, bool mana, bool knowsKings)
{
    if (!mode.empty() && cls != Paladin)
        return {};
    switch (cls)
    {
    case Mage:
        return {Intellect};
    case Priest:
        return {Fortitude, Spirit, ShadowProtection};
    case Druid:
        return {Mark, Thorns};
    case Warlock:
        return {Breath};
    case Paladin:
        if (mode == "might")
            return {Might};
        if (mode == "wisdom")
            return {Wisdom};
        if (mode == "kings")
            return {Kings};
        if (mode == "sanctuary")
            return {Sanctuary};
        if (knowsKings)
            return {Kings, mana ? Wisdom : Might, mana ? Might : Wisdom};
        return {mana ? Wisdom : Might, mana ? Might : Wisdom};
    default:
        return {};
    }
}
// Group versions satisfy the same request, but are never cast on an unrelated player.
inline Id Equivalent(Id first)
{
    switch (first)
    {
    case Brilliance:
        return Intellect;
    case PrayerFortitude:
        return Fortitude;
    case PrayerSpirit:
        return Spirit;
    case PrayerShadow:
        return ShadowProtection;
    case Gift:
        return Mark;
    case GreaterMight:
        return Might;
    case GreaterWisdom:
        return Wisdom;
    case GreaterKings:
        return Kings;
    case GreaterSanctuary:
        return Sanctuary;
    default:
        return first;
    }
}
inline bool Blessing(Id first)
{
    auto id = Equivalent(first);
    return id == Might || id == Wisdom || id == Kings || id == Sanctuary;
}
} // namespace BotBuff
#endif
