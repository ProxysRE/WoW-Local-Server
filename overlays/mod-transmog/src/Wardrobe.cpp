// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Chat.h"
#include "Creature.h"
#include "DataMap.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "Transmogrification.h"
#include "Util.h"
#include "WardrobePolicy.h"
#include "World.h"
#include "WorldSession.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <tuple>

namespace Wardrobe
{
constexpr char Settings[] = "mod-wardrobe";
constexpr uint32 PageSize = 12;
constexpr uint32 QuoteLife = 30 * IN_MILLISECONDS;
struct Quote
{
    Plan plan;
    std::map<uint32, ObjectGuid> items;
    uint32 nonce = 0, created = 0, money = 0, tokens = 0, tokenEntry = 0;
};
struct Session : DataMap::Base
{
    uint32 lastRequest = 0;
    Quote quote;
};
std::array<std::vector<uint32>, EQUIPMENT_SLOT_END> catalog;

Session& Data(Player* player) { return *player->CustomData.GetDefault<Session>(Settings); }

void Send(Player* player, uint32 request, std::string_view kind, std::string_view payload = "")
{
    player->SendSystemMessage(Acore::StringFormat("ACW1:{}:{}:{}", request, kind, payload));
}

bool SettingsEnabled() { return sWorld->getBoolConfig(CONFIG_PLAYER_SETTINGS_ENABLED); }

bool NearNpc(Player* player)
{
    Creature* npc = player->FindNearestCreature(TMOG_VENDOR_CREATURE_ID, INTERACTION_DISTANCE, true);
    return npc && player->IsWithinLOSInMap(npc);
}

bool Owns(Player* player, uint32 entry)
{
    auto const& collections = sTransmogrification->collectionCache;
    auto account = collections.find(player->GetSession()->GetAccountId());
    return account != collections.end() && account->second.contains(entry);
}

bool FitsSlot(ItemTemplate const* item, uint32 slot)
{
    switch (slot)
    {
    case EQUIPMENT_SLOT_HEAD:
        return item->InventoryType == INVTYPE_HEAD;
    case EQUIPMENT_SLOT_SHOULDERS:
        return item->InventoryType == INVTYPE_SHOULDERS;
    case EQUIPMENT_SLOT_BODY:
        return item->InventoryType == INVTYPE_BODY;
    case EQUIPMENT_SLOT_CHEST:
        return item->InventoryType == INVTYPE_CHEST || item->InventoryType == INVTYPE_ROBE;
    case EQUIPMENT_SLOT_WAIST:
        return item->InventoryType == INVTYPE_WAIST;
    case EQUIPMENT_SLOT_LEGS:
        return item->InventoryType == INVTYPE_LEGS;
    case EQUIPMENT_SLOT_FEET:
        return item->InventoryType == INVTYPE_FEET;
    case EQUIPMENT_SLOT_WRISTS:
        return item->InventoryType == INVTYPE_WRISTS;
    case EQUIPMENT_SLOT_HANDS:
        return item->InventoryType == INVTYPE_HANDS;
    case EQUIPMENT_SLOT_BACK:
        return item->InventoryType == INVTYPE_CLOAK;
    case EQUIPMENT_SLOT_MAINHAND:
        return item->InventoryType == INVTYPE_WEAPON || item->InventoryType == INVTYPE_WEAPONMAINHAND ||
               item->InventoryType == INVTYPE_2HWEAPON;
    case EQUIPMENT_SLOT_OFFHAND:
        return item->InventoryType == INVTYPE_WEAPON || item->InventoryType == INVTYPE_WEAPONOFFHAND ||
               item->InventoryType == INVTYPE_SHIELD || item->InventoryType == INVTYPE_HOLDABLE ||
               item->InventoryType == INVTYPE_2HWEAPON;
    case EQUIPMENT_SLOT_RANGED:
        return item->InventoryType == INVTYPE_RANGED || item->InventoryType == INVTYPE_RANGEDRIGHT ||
               item->InventoryType == INVTYPE_THROWN;
    case EQUIPMENT_SLOT_TABARD:
        return item->InventoryType == INVTYPE_TABARD;
    default:
        return false;
    }
}

std::string Name(Player* player, ItemTemplate const* item)
{
    std::string name = item->Name1;
    if (auto const* locale = sObjectMgr->GetItemLocale(item->ItemId))
        ObjectMgr::GetLocaleString(locale->Name, player->GetSession()->GetSessionDbLocaleIndex(), name);
    return name;
}

std::string Validate(Player* player, uint32 slot, uint32 entry)
{
    auto* transmog = sTransmogrification;
    if (!transmog->IsTransmogEnabled || !transmog->GetUseCollectionSystem())
        return "DISABLED";
    if (!Slot(slot))
        return "SLOT";
    Item* target = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!target)
        return "EMPTY";
    if (!entry)
        return "OK";
    if (entry == HIDDEN_ITEM_ID)
        return transmog->GetAllowHiddenTransmog() ? "OK" : "HIDDEN";
    auto const* source = sObjectMgr->GetItemTemplate(entry);
    if (!source || !FitsSlot(source, slot))
        return "ITEM";
    if (!Owns(player, entry))
        return "NOT_COLLECTED";
    if (!transmog->SuitableForTransmogrification(player, source))
        return "REQUIREMENTS";
    if (!transmog->SuitableForTransmogrification(player, target->GetTemplate()))
        return "REQUIREMENTS";
    if (target->GetTemplate()->DisplayInfoID == source->DisplayInfoID)
        return "OK";
    return transmog->CanTransmogrifyItemWithItem(player, target->GetTemplate(), source) ? "OK" : "INCOMPATIBLE";
}

bool BuildQuote(Player* player, Plan const& plan, Quote& quote)
{
    auto* transmog = sTransmogrification;
    quote = Quote{};
    quote.plan = plan;
    quote.tokenEntry = transmog->GetTokenEntry();
    uint64 money = 0, tokens = 0;
    for (auto const& [slot, entry] : plan)
    {
        if (Validate(player, slot, entry) != "OK")
            return false;
        Item* target = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        quote.items[slot] = target->GetGUID();
        uint32 fake = transmog->GetFakeEntry(target->GetGUID());
        auto const* source = sObjectMgr->GetItemTemplate(entry);
        if (!entry || fake == entry || (source && source->DisplayInfoID == target->GetTemplate()->DisplayInfoID))
            continue;
        if (entry == HIDDEN_ITEM_ID && transmog->HiddenTransmogIsFree)
            continue;
        double cost = std::trunc(transmog->GetSpecialPrice(target->GetTemplate()) * transmog->GetScaledCostModifier()) +
                      transmog->GetCopperCost();
        if (!std::isfinite(cost) || cost < 0 || cost > MAX_MONEY_AMOUNT)
            return false;
        money += static_cast<uint32>(cost);
        if (money > MAX_MONEY_AMOUNT)
            return false;
        if (entry != HIDDEN_ITEM_ID && transmog->GetRequireToken())
            tokens += transmog->GetTokenAmount();
        if (tokens > UINT32_MAX)
            return false;
    }
    quote.money = money;
    quote.tokens = tokens;
    return true;
}

// Called only after validation; the virtual appearance has no physical source
// item to consume.
void Apply(Player* player, uint32 slot, uint32 entry)
{
    auto* transmog = sTransmogrification;
    Item* target = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    auto const* source = sObjectMgr->GetItemTemplate(entry);
    if (!entry || (source && source->DisplayInfoID == target->GetTemplate()->DisplayInfoID))
        transmog->DeleteFakeEntry(player, slot, target);
    else
    {
        transmog->SetFakeEntry(player, entry, slot, target);
        target->UpdatePlayedTime(player);
        target->SetNotRefundable(player);
        target->ClearSoulboundTradeable(player);
    }
}

void State(Player* player, uint32 request)
{
    Send(player, request, "STATE",
         Acore::StringFormat("{}:{}:{}:{}:{}", player->GetSession()->GetAccountId(), NearNpc(player), SettingsEnabled(),
                             sTransmogrification->GetTokenEntry(), sTransmogrification->GetAllowHiddenTransmog()));
    for (uint32 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
        if (Slot(slot))
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            Send(player, request, "SLOT",
                 Acore::StringFormat("{}:{}:{}", slot, item ? item->GetEntry() : 0,
                                     item ? sTransmogrification->GetFakeEntry(item->GetGUID()) : 0));
        }
    Send(player, request, "DONE");
}

void List(Player* player, uint32 request, uint32 slot, uint32 page, std::string const& mode, std::string const& search)
{
    std::wstring wide;
    if (!Utf8toWStr(search, wide))
    {
        Send(player, request, "ERROR", "SEARCH");
        return;
    }
    wstrToLower(wide);
    using Key = std::tuple<uint32, uint32, uint32, uint32>;
    std::map<Key, uint32> grouped;
    for (uint32 entry : catalog[slot])
    {
        auto const* item = sObjectMgr->GetItemTemplate(entry);
        if (!wide.empty() && !Utf8FitTo(Name(player, item), wide))
            continue;
        bool owned = Owns(player, entry);
        if (mode == "collected" && !owned)
            continue;
        if (mode == "usable" && Validate(player, slot, entry) != "OK")
            continue;
        Key key{item->DisplayInfoID, item->Class, item->SubClass, item->InventoryType};
        auto old = grouped.find(key);
        if (old == grouped.end() || (owned && !Owns(player, old->second)) ||
            (owned && Validate(player, slot, entry) == "OK" && Validate(player, slot, old->second) != "OK"))
            grouped[key] = entry;
    }
    std::vector<uint32> items;
    for (auto const& [key, entry] : grouped)
        items.push_back(entry);
    std::sort(items.begin(), items.end());
    uint32 pages = std::max<uint32>(1, (items.size() + PageSize - 1) / PageSize);
    page = std::min(page, pages - 1);
    Send(player, request, "PAGE", Acore::StringFormat("{}:{}:{}:{}", slot, page, pages, items.size()));
    for (uint32 i = page * PageSize; i < items.size() && i < (page + 1) * PageSize; ++i)
    {
        auto const* item = sObjectMgr->GetItemTemplate(items[i]);
        std::string name = Name(player, item);
        utf8truncate(name, 26);
        Send(player, request, "ITEM",
             Acore::StringFormat("{}:{}:{}:{}:{}:{}:{}", item->ItemId, item->DisplayInfoID, item->Quality,
                                 item->RequiredLevel, Owns(player, item->ItemId), Validate(player, slot, item->ItemId),
                                 Hex(name)));
    }
    Send(player, request, "DONE");
}

class Commands : public CommandScript
{
  public:
    Commands() : CommandScript("wardrobe_commands") {}
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable commands = {
            {"wardrobe", Handle, SEC_PLAYER, Acore::ChatCommands::Console::No}};
        return commands;
    }
    static bool Handle(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetPlayer();
        if (!player || !player->GetSession() || player->GetSession()->IsHeadless())
            return true;
        std::istringstream input(args ? args : "");
        std::string id, op, first, second, mode, hex, extra;
        uint32 request = 0;
        if (!(input >> id >> op) || !Number(id, request))
            return false;
        Session& state = Data(player);
        if (state.lastRequest && GetMSTimeDiffToNow(state.lastRequest) < 200)
        {
            Send(player, request, "ERROR", "RATE");
            return true;
        }
        state.lastRequest = getMSTime();
        if (op == "STATE" && !(input >> extra))
        {
            State(player, request);
            return true;
        }
        if (op == "LIST")
        {
            uint32 slot = 0, page = 0;
            std::string search;
            if (!(input >> first >> second >> mode >> hex) || (input >> extra) || !Number(first, slot) || !Slot(slot) ||
                !Number(second, page) || (mode != "all" && mode != "collected" && mode != "usable") ||
                !Unhex(hex, search))
            {
                Send(player, request, "ERROR", "REQUEST");
                return true;
            }
            List(player, request, slot, page, mode, search);
            return true;
        }
        if (op == "SETS" && !(input >> extra))
        {
            for (auto const& [id, name] : sTransmogrification->presetByName[player->GetGUID()])
            {
                std::ostringstream plan;
                for (auto const& [slot, entry] : sTransmogrification->presetById[player->GetGUID()][id])
                    if (Slot(slot))
                        plan << (plan.tellp() > std::streampos(0) ? "," : "") << uint32(slot) << '=' << entry;
                Send(player, request, "SET", Acore::StringFormat("{}:{}:{}", uint32(id), Hex(name), plan.str()));
            }
            Send(player, request, "DONE");
            return true;
        }
        if (op == "QUOTE")
        {
            state.quote = Quote{};
            Plan plan;
            Quote quote;
            if (!(input >> first) || (input >> extra) || !ReadPlan(first, plan))
                Send(player, request, "ERROR", "REQUEST");
            else if (!NearNpc(player) || !player->IsAlive() || player->IsInCombat() || player->IsInFlight() ||
                     player->IsBeingTeleported() || player->IsNonMeleeSpellCast(false))
                Send(player, request, "ERROR", "NPC");
            else if (!BuildQuote(player, plan, quote))
                Send(player, request, "ERROR", "INVALID_PLAN");
            else
            {
                quote.nonce = urand(1, UINT32_MAX);
                quote.created = getMSTime();
                state.quote = quote;
                Send(player, request, "QUOTE",
                     Acore::StringFormat("{}:{}:{}:{}", quote.nonce, quote.money, quote.tokens,
                                         sTransmogrification->GetTokenEntry()));
            }
            return true;
        }
        if (op == "APPLY")
        {
            uint32 nonce = 0;
            Quote quote = state.quote;
            state.quote = Quote{};
            Quote current;
            bool valid = (input >> first) && !(input >> extra) && Number(first, nonce) && nonce == quote.nonce &&
                         nonce && GetMSTimeDiffToNow(quote.created) <= QuoteLife && NearNpc(player) &&
                         player->IsAlive() && !player->IsInCombat() && !player->IsInFlight() &&
                         !player->IsBeingTeleported() && !player->IsNonMeleeSpellCast(false) &&
                         BuildQuote(player, quote.plan, current) && current.items == quote.items &&
                         current.money == quote.money && current.tokens == quote.tokens &&
                         current.tokenEntry == quote.tokenEntry;
            if (!valid)
                Send(player, request, "ERROR", "QUOTE_EXPIRED");
            else if (!player->HasEnoughMoney(quote.money))
                Send(player, request, "ERROR", "MONEY");
            else if (quote.tokens && !player->HasItemCount(sTransmogrification->GetTokenEntry(), quote.tokens))
                Send(player, request, "ERROR", "TOKENS");
            else
            {
                if (quote.tokens)
                    player->DestroyItemCount(sTransmogrification->GetTokenEntry(), quote.tokens, true);
                player->ModifyMoney(-static_cast<int32>(quote.money));
                for (auto const& [slot, entry] : quote.plan)
                {
                    Apply(player, slot, entry);
                    if (SettingsEnabled())
                        player->UpdatePlayerSetting(Settings, slot, entry);
                }
                Send(player, request, "APPLIED");
            }
            return true;
        }
        Send(player, request, "ERROR", "REQUEST");
        return true;
    }
};

class Hooks : public PlayerScript
{
  public:
    Hooks() : PlayerScript("wardrobe_slot_persistence") {}
    static void Restore(Player* player, uint32 slot)
    {
        if (player->GetSession()->IsHeadless() || !SettingsEnabled() || !Slot(slot))
            return;
        uint32 desired = player->GetPlayerSetting(Settings, slot).value;
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (desired && item && Validate(player, slot, desired) == "OK" &&
            sTransmogrification->GetFakeEntry(item->GetGUID()) != desired)
            Apply(player, slot, desired);
    }
    void OnPlayerEquip(Player* player, Item* item, uint8, uint8, bool) override
    {
        if (item && item->IsEquipped())
            Restore(player, item->GetSlot());
    }
    void OnPlayerLogin(Player* player) override
    {
        if (player->GetSession()->IsHeadless())
            return;
        for (uint32 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
            Restore(player, slot);
    }
};

class Catalog : public WorldScript
{
  public:
    Catalog() : WorldScript("wardrobe_catalog") {}
    void OnStartup() override
    {
        for (auto& entries : catalog)
            entries.clear();
        for (auto const& [entry, item] : *sObjectMgr->GetItemTemplateStore())
            if ((item.Class == ITEM_CLASS_ARMOR || item.Class == ITEM_CLASS_WEAPON) && item.DisplayInfoID)
                for (uint32 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                    if (Slot(slot) && FitsSlot(&item, slot))
                        catalog[slot].push_back(entry);
    }
};
} // namespace Wardrobe

void AddSC_wardrobe()
{
    new Wardrobe::Commands();
    new Wardrobe::Hooks();
    new Wardrobe::Catalog();
}
