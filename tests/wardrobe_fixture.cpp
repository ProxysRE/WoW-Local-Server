// SPDX-License-Identifier: AGPL-3.0-or-later
#include "WardrobePolicy.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <set>
#include <map>
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;
constexpr uint32 INVENTORY_SLOT_BAG_0 = 0, HIDDEN_ITEM_ID = 1, MAX_MONEY_AMOUNT = 2147483647;
using ObjectGuid = uint32;
struct ItemTemplate
{
    uint32 ItemId = 0, DisplayInfoID = 0, slot = 0, armor = 0, price = 100, level = 1;
};
struct Item
{
    ObjectGuid guid = 0;
    ItemTemplate info;
    ObjectGuid GetGUID() { return guid; }
    ItemTemplate const* GetTemplate() { return &info; }
};
struct Player
{
    uint32 level = 70;
    std::map<uint32, Item> inventory;
    std::set<uint32> collected;
    Item* GetItemByPos(uint32, uint32 slot)
    {
        auto it = inventory.find(slot);
        return it == inventory.end() ? nullptr : &it->second;
    }
};
struct Manager
{
    std::map<uint32, ItemTemplate> items;
    ItemTemplate const* GetItemTemplate(uint32 id)
    {
        auto it = items.find(id);
        return it == items.end() ? nullptr : &it->second;
    }
} manager;
auto* sObjectMgr = &manager;
struct Transmog
{
    bool IsTransmogEnabled = true, collection = true, hidden = true, HiddenTransmogIsFree = false;
    bool tokens = false;
    uint32 amount = 2, token = 123;
    float multiplier = 1.0f;
    int copper = 0;
    std::map<ObjectGuid, uint32> applied;
    bool GetUseCollectionSystem() { return collection; }
    bool GetAllowHiddenTransmog() { return hidden; }
    bool SuitableForTransmogrification(Player* p, ItemTemplate const* item) { return item->level <= p->level; }
    bool CanTransmogrifyItemWithItem(Player*, ItemTemplate const* a, ItemTemplate const* b) { return a->armor == b->armor; }
    uint32 GetFakeEntry(ObjectGuid id) { return applied[id]; }
    uint32 GetTokenEntry() { return token; }
    uint32 GetTokenAmount() { return amount; }
    bool GetRequireToken() { return tokens; }
    uint32 GetSpecialPrice(ItemTemplate const* item) { return item->price; }
    float GetScaledCostModifier() { return multiplier; }
    int GetCopperCost() { return copper; }
} transmog;
auto* sTransmogrification = &transmog;
namespace Wardrobe
{
struct Quote
{
    Plan plan;
    std::map<uint32, ObjectGuid> items;
    uint32 nonce = 0, created = 0, money = 0, tokens = 0, tokenEntry = 0;
};
bool Owns(Player* player, uint32 entry) { return player->collected.contains(entry); }
bool FitsSlot(ItemTemplate const* item, uint32 slot) { return item->slot == slot; }
// PRODUCTION_METHODS
}
int main()
{
    using namespace Wardrobe;
    Plan plan;
    assert(ReadPlan("0=100,2=200,4=0", plan) && plan.size() == 3);
    for (auto const* invalid : {"", "1=100", "19=100", "0=-1", "0=1,0=2", "0=2,", "0=4294967296", "0=1junk", "0=1;DELETE"})
        assert(!ReadPlan(invalid, plan));
    std::string text;
    assert(Unhex(Hex("appearance"), text) && text == "appearance");
    assert(!Unhex("00", text) && !Unhex("0g", text) && !Unhex("0", text));
    Player player;
    player.inventory[0] = Item{11, ItemTemplate{10, 20, 0, 4, 100, 1}};
    player.inventory[2] = Item{12, ItemTemplate{11, 21, 2, 4, 250, 1}};
    manager.items[100] = ItemTemplate{100, 30, 0, 4, 0, 1};
    manager.items[200] = ItemTemplate{200, 31, 2, 4, 0, 1};
    manager.items[300] = ItemTemplate{300, 32, 0, 1, 0, 1};
    manager.items[400] = ItemTemplate{400, 33, 0, 4, 0, 80};
    Quote quote;
    assert(!BuildQuote(&player, {{0, 100}}, quote)); // Arbitrary catalog IDs do not grant collection rights.
    player.collected = {100, 200, 300, 400};
    assert(Validate(&player, 0, 300) == "INCOMPATIBLE");
    assert(Validate(&player, 0, 400) == "REQUIREMENTS");
    assert(Validate(&player, 2, 100) == "ITEM");
    assert(Validate(&player, 4, 100) == "EMPTY");
    assert(!BuildQuote(&player, {{0,100},{2,400}}, quote)); // Whole outfit rejected before any mutation.
    assert(BuildQuote(&player, {{0,100},{2,200}}, quote));
    assert(quote.money == 350 && quote.items.at(0) == 11 && quote.items.at(2) == 12);
    Quote current;
    player.inventory[0].guid = 999;
    assert(BuildQuote(&player, quote.plan, current) && current.items != quote.items);
    player.inventory[0].guid = 11;
    transmog.tokens = true;
    assert(BuildQuote(&player, {{0,100},{2,200}}, quote) && quote.tokens == 4 && quote.tokenEntry == 123);
    transmog.amount = UINT32_MAX;
    assert(!BuildQuote(&player, {{0,100},{2,200}}, quote));
    transmog.tokens = false;
    transmog.copper = -101;
    assert(!BuildQuote(&player, {{0,100}}, quote));
    transmog.copper = 0;
    transmog.multiplier = std::numeric_limits<float>::infinity();
    assert(!BuildQuote(&player, {{0,100}}, quote));
    transmog.multiplier = 1;
    transmog.applied[11] = 100;
    assert(BuildQuote(&player, {{0,100}}, quote) && quote.money == 0);
    assert(BuildQuote(&player, {{0,0}}, quote) && quote.money == 0);
    transmog.hidden = false;
    assert(!BuildQuote(&player, {{0,1}}, quote));
    transmog.hidden = true;
    transmog.HiddenTransmogIsFree = true;
    assert(BuildQuote(&player, {{0,1}}, quote) && quote.money == 0);
    transmog.IsTransmogEnabled = false;
    assert(!BuildQuote(&player, {{0,100}}, quote));
}
