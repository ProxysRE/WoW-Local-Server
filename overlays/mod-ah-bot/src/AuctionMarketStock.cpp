#include "AuctionMarket.h"
#include "AuctionHouseBotConfig.h"
#include "AuctionHouseBotCommon.h"
#include "AuctionHouseMgr.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Random.h"
#include "DBCStores.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include <algorithm>
#include <ctime>
#include <set>
#include <tuple>

namespace AuctionMarket
{
namespace
{
bool enabled = true;
uint32 listingLimit = 6000;
struct Override
{
    Limits limits;
    bool allow = false;
};
std::map<uint32, Override> overrides;
// Kept across config reloads; timestamps are wall-clock seconds, never Player/Item pointers.
std::map<std::pair<uint32, uint32>, time_t> nextSupply;

uint32 Group(ItemTemplate const& item)
{
    uint32 band = (item.Class == 2 || item.Class == 4) ? std::min(item.RequiredLevel / 10, 8u) : 0;
    return (item.Class << 24) | (item.SubClass << 16) | (item.InventoryType << 8) | band;
}

uint32 Weight(uint32 itemClass)
{
    return itemClass == 7 ? 4 : itemClass == 0 ? 3 : 1;
}
}

void Load()
{
    enabled = sConfigMgr->GetOption<bool>("AuctionHouseBot.Market.Enable", true);
    listingLimit = std::clamp(sConfigMgr->GetOption<uint32>(
        "AuctionHouseBot.Market.MaxListings", 6000), 100u, 20000u);
    overrides.clear();
    // Also protect fresh installations if custom SQL has not yet been installed.
    for (uint32 id : {17203u, 18562u, 19018u, 17010u, 17011u, 12363u, 22450u, 23572u,
        30183u, 34664u, 36908u, 43102u, 45087u, 47556u, 49908u})
        overrides[id] = {{2, 2, 1, 3600}, false};
    overrides[17203].allow = true;
    overrides[13468] = {{10, 2, 5, 1800}, false};
    overrides[22794] = {{10, 2, 5, 1800}, false};
    QueryResult exists = WorldDatabase.Query("SHOW TABLES LIKE 'mod_auctionmarket_item'");
    if (exists)
    {
        QueryResult rows = WorldDatabase.Query(
            "SELECT item, max_units, max_lots, stack_size, restock_seconds, allow_filtered FROM mod_auctionmarket_item");
        if (rows)
        {
            do
            {
                Field* f = rows->Fetch();
                overrides[f[0].Get<uint32>()] = {{std::min(f[1].Get<uint32>(), 10000u),
                    std::min(f[2].Get<uint32>(), 20u), std::min(f[3].Get<uint32>(), 1000u),
                    std::clamp(f[4].Get<uint32>(), 60u, 604800u)}, f[5].Get<bool>()};
            } while (rows->NextRow());
        }
    }
    LOG_INFO("module", "AuctionMarket: enabled={}, listing limit={}, item rules={}",
        enabled, listingLimit, overrides.size());
}

void AddCraftedItems(std::set<uint32>& items)
{
    if (!enabled)
        return;
    // Read products of actual profession spells, not merely a hand-maintained SQL list.
    std::set<uint32> const professions = {129, 164, 165, 171, 185, 186, 197, 202, 333, 755, 773};
    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        auto ability = sSkillLineAbilityStore.LookupEntry(i);
        if (!ability || !professions.count(ability->SkillLine))
            continue;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(ability->Spell);
        if (!spell)
            continue;
        for (auto const& effect : spell->Effects)
            if (effect.Effect == SPELL_EFFECT_CREATE_ITEM && effect.ItemType)
                items.insert(effect.ItemType);
    }
}

bool Enabled()
{
    return enabled;
}

uint32 ListingLimit()
{
    return listingLimit;
}
bool AllowOverride(uint32 item)
{
    auto it = overrides.find(item);
    return enabled && it != overrides.end() && it->second.allow;
}

StockCycle Snapshot(AHBConfig* config, AuctionHouseObject* house)
{
    StockCycle cycle;
    cycle.house = config->GetAHID();
    std::set<uint32> const* bins[] = {&config->GreyTradeGoodsBin, &config->WhiteTradeGoodsBin,
        &config->GreenTradeGoodsBin, &config->BlueTradeGoodsBin, &config->PurpleTradeGoodsBin,
        &config->OrangeTradeGoodsBin, &config->YellowTradeGoodsBin, &config->GreyItemsBin,
        &config->WhiteItemsBin, &config->GreenItemsBin, &config->BlueItemsBin, &config->PurpleItemsBin,
        &config->OrangeItemsBin, &config->YellowItemsBin};
    for (uint32 bin = 0; bin < 14; ++bin)
    {
        for (uint32 id : *bins[bin])
        {
            ItemTemplate const* item = sObjectMgr->GetItemTemplate(id);
            if (!item || !item->Quality || item->Quality > 4 || item->Class == 12)
                continue;
            Limits limits = DefaultLimits(item->Class, item->Quality, item->GetMaxStackSize());
            auto rule = overrides.find(id);
            if (rule != overrides.end())
                limits = rule->second.limits;
            if (limits.units && limits.lots && limits.stack)
                cycle.entries.push_back({id, bin, Group(*item), item->Class, item->GetMaxStackSize(), limits});
        }
    }
    for (auto it = house->GetAuctionsBegin(); it != house->GetAuctionsEnd(); ++it)
    {
        AuctionEntry const* auction = it->second;
        if (!gBotsId.count(auction->owner.GetCounter()))
            continue;
        cycle.units[auction->item_template] += auction->itemCount;
        ++cycle.lots[auction->item_template];
        if (ItemTemplate const* item = sObjectMgr->GetItemTemplate(auction->item_template))
            ++cycle.groupLots[Group(*item)];
    }
    return cycle;
}

uint32 StockCycle::Pick(uint32& bin, uint32& count)
{
    time_t now = time(nullptr);
    Entry const* selected = nullptr;
    uint32 ties = 0;
    std::tuple<uint32, uint64, uint32> best{9, UINT64_MAX, UINT32_MAX};
    for (Entry const& entry : entries)
    {
        auto supply = nextSupply.find({house, entry.item});
        if (supply != nextSupply.end() && now < supply->second)
            continue;
        uint32 amount = AvailableToList(entry.limits, units[entry.item], lots[entry.item], entry.maxStack);
        if (!amount)
            continue;
        // Cover missing commodities first, then missing equipment/recipes, then repetitions.
        bool commodity = entry.itemClass == 7 || entry.itemClass == 0 || entry.itemClass == 6;
        uint32 tier = lots[entry.item] ? 2 : commodity ? 0 : 1;
        auto rank = std::make_tuple(tier,
            uint64(groupLots[entry.group]) * 12 / Weight(entry.itemClass), lots[entry.item]);
        if (rank < best)
        {
            best = rank;
            selected = &entry;
            ties = 1;
        }
        else if (rank == best && urand(1, ++ties) == 1)
            selected = &entry;
    }
    if (!selected)
        return 0;
    bin = selected->bin;
    count = AvailableToList(selected->limits, units[selected->item], lots[selected->item], selected->maxStack);
    return selected->item;
}

void StockCycle::Posted(uint32 item, uint32 count)
{
    for (Entry const& entry : entries)
    {
        if (entry.item != item)
            continue;
        units[item] += count;
        ++lots[item];
        ++groupLots[entry.group];
        nextSupply[{house, item}] = time(nullptr) + entry.limits.interval;
        return;
    }
}
}
