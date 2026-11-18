// SPDX-License-Identifier: AGPL-3.0-or-later
#include "QoLPolicy.h"
#include "Bag.h"
#include "CellImpl.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DataMap.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "LootMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Spell.h"
#include "SpellMgr.h"
#include "Trainer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <list>
#include <sstream>
#include <unordered_set>

namespace LocalQoL
{
constexpr char Settings[] = "mod-local-qol";
constexpr uint32 JunkSetting = 1; // 0=server default, 1=off, 2=on
constexpr uint32 FishSetting = 2; // opt-in, 1=on

bool Human(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->IsHeadless();
}

struct State : DataMap::Base
{
    bool sellPending = false;
    uint32 tick = 0;
    FishPhase phase = FishPhase::Idle;
    uint32 spell = 0, delay = 0, elapsed = 0, map = 0;
    Position anchor;
    ObjectGuid loot;
};

State& Data(Player* player) { return *player->CustomData.GetDefault<State>(Settings); }

std::unordered_set<uint32> Ids(std::string const& text)
{
    std::istringstream input(text);
    std::unordered_set<uint32> result;
    uint32 id;
    while (input >> id) result.insert(id);
    return result;
}

bool JunkEnabled(Player* p)
{
    uint32 setting = p->GetPlayerSetting(Settings, JunkSetting).value;
    return sConfigMgr->GetOption<bool>("LocalQoL.Junk.Enable", true) &&
        (setting == 2 || (setting == 0 && sConfigMgr->GetOption<bool>("LocalQoL.Junk.DefaultOn", true)));
}

bool FishEnabled(Player* p)
{
    return sConfigMgr->GetOption<bool>("LocalQoL.Fish.Enable", true) &&
        p->GetPlayerSetting(Settings, FishSetting).IsEnabled();
}

bool FishingSpell(SpellInfo const* info)
{
    // Match the actual bobber effect, not a hard-coded skill rank.
    if (!info || !info->IsChanneled()) return false;
    for (auto const& effect : info->Effects)
        if ((effect.Effect == SPELL_EFFECT_TRANS_DOOR || effect.Effect == SPELL_EFFECT_SUMMON_OBJECT_WILD) &&
            effect.MiscValue > 0)
            if (auto const* object = sObjectMgr->GetGameObjectTemplate(effect.MiscValue))
                if (object->type == GAMEOBJECT_TYPE_FISHINGNODE) return true;
    return false;
}

bool HasPole(Player* p)
{
    Item* weapon = p->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
    return weapon && weapon->GetTemplate()->Class == ITEM_CLASS_WEAPON &&
        weapon->GetTemplate()->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE;
}

bool CraftSpell(SpellInfo const* info)
{
    if (!info || info->IsChanneled()) return false;
    bool effect = false;
    for (auto const& e : info->Effects)
        if (e.Effect == SPELL_EFFECT_CREATE_ITEM || e.Effect == SPELL_EFFECT_CREATE_ITEM_2 ||
            e.Effect == SPELL_EFFECT_TRADE_SKILL || e.Effect == SPELL_EFFECT_ENCHANT_ITEM ||
            e.Effect == SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC || e.Effect == SPELL_EFFECT_PROSPECTING ||
            e.Effect == SPELL_EFFECT_MILLING || e.Effect == SPELL_EFFECT_DISENCHANT)
            effect = true;
    if (!effect) return false;
    auto bounds = sSpellMgr->GetSkillLineAbilityMapBounds(info->Id);
    for (auto i = bounds.first; i != bounds.second; ++i)
    {
        uint32 skill = i->second->SkillLine;
        if (IsProfessionSkill(skill) && skill != SKILL_FISHING && skill != SKILL_HERBALISM && skill != SKILL_SKINNING)
            return true;
    }
    return false;
}

bool NearWorkshop(Player* p)
{
    float radius = std::clamp(sConfigMgr->GetOption<float>("LocalQoL.Craft.Radius", 25.0f), 1.0f, 60.0f);
    auto focuses = Ids(sConfigMgr->GetOption<std::string>("LocalQoL.Craft.FocusIds", "1 2 3 4 663"));
    std::list<GameObject*> objects;
    auto objectCheck = [&](GameObject* go)
    {
        return go->isSpawned() && p->IsWithinDistInMap(go, radius) &&
            go->GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS && focuses.contains(go->GetGOInfo()->spellFocus.focusId);
    };
    Acore::GameObjectListSearcher objectSearcher(p, objects, objectCheck);
    Cell::VisitObjects(p, objectSearcher, radius);
    if (!objects.empty()) return true;

    std::list<Creature*> trainers;
    auto trainerCheck = [&](Creature* npc)
    {
        if (!npc->IsAlive() || !npc->IsFriendlyTo(p) || !p->IsWithinDistInMap(npc, radius)) return false;
        auto* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
        return trainer && trainer->GetTrainerType() == Trainer::Type::Tradeskill;
    };
    Acore::CreatureListSearcher trainerSearcher(p, trainers, trainerCheck);
    Cell::VisitObjects(p, trainerSearcher, radius);
    return !trainers.empty();
}

void StopFish(Player* p, char const* reason)
{
    State& state = Data(p);
    bool active = state.phase != FishPhase::Idle;
    state.phase = FishPhase::Idle;
    state.loot.Clear();
    if (active && reason) ChatHandler(p->GetSession()).SendSysMessage(reason);
}

void SellJunk(Player* p)
{
    if (!JunkEnabled(p) || p->IsInCombat()) return;
    auto excluded = Ids(sConfigMgr->GetOption<std::string>("LocalQoL.Junk.ExcludedItems", ""));
    uint64 earned = 0;
    uint32 units = 0;
    auto sell = [&](Item* item)
    {
        if (!item || item->IsInTrade() || excluded.contains(item->GetEntry())) return;
        auto const* info = item->GetTemplate();
        if (info->Quality != ITEM_QUALITY_POOR || info->Class == ITEM_CLASS_QUEST || info->StartQuest ||
            p->HasQuestForItem(item->GetEntry())) return;
        uint32 count = item->GetCount();
        uint32 money = SaleValue(info->SellPrice, count, p->GetMoney(), MAX_MONEY_AMOUNT);
        if (!money || !p->ModifyMoney(static_cast<int32>(money))) return;
        // Sell the whole actual stack and pay for that same whole stack.
        // Run after the loot hook returns, not while other loot hooks use item.
        p->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        earned += money;
        units += count;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        sell(p->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
        if (Bag* container = p->GetBagByPos(bag))
            for (uint32 slot = 0; slot < container->GetBagSize(); ++slot)
                sell(container->GetItemByPos(slot));
    if (units)
    {
        p->SaveToDB(false, false);
        ChatHandler(p->GetSession()).PSendSysMessage("Auto-junk: sold {} items for {}g {}s {}c.",
            units, earned / GOLD, (earned % GOLD) / SILVER, earned % SILVER);
    }
}

void UpdateFish(Player* p, State& state, uint32 diff)
{
    if (state.phase == FishPhase::Idle) return;
    if (UnsafeFishing(p->IsAlive(), p->IsInCombat(), p->isMoving(), p->IsBeingTeleported(),
        p->IsMounted(), HasPole(p), FishEnabled(p)) || p->GetMapId() != state.map ||
        p->GetExactDistSq(&state.anchor) > 0.25f || p->HasPlayerFlag(PLAYER_FLAGS_NO_PLAY_TIME))
    {
        StopFish(p, "Auto-fishing stopped: movement, combat or equipment changed.");
        return;
    }
    state.elapsed += diff;
    if (state.elapsed > 45000)
    {
        StopFish(p, "Auto-fishing stopped: the current cast timed out.");
        return;
    }
    if (state.delay > diff) { state.delay -= diff; return; }
    state.delay = 0;

    if (state.phase == FishPhase::Waiting)
    {
        Spell* channel = p->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!channel || channel->GetSpellInfo()->Id != state.spell)
        {
            StopFish(p, "Auto-fishing stopped: fishing was interrupted.");
            return;
        }
        GameObject* bobber = p->GetGameObject(state.spell);
        if (!bobber || bobber->GetOwnerGUID() != p->GetGUID() || bobber->GetGoType() != GAMEOBJECT_TYPE_FISHINGNODE)
            return;
        if (bobber->getLootState() != GO_READY) return;
        state.phase = FishPhase::Loot; // before Use: it finishes the fishing channel
        bobber->Use(p); // normal catch chance, skill-up and pool handling
        state.loot = p->GetLootGUID();
        state.delay = 300;
        return;
    }
    if (state.phase == FishPhase::Loot)
    {
        ObjectGuid guid = p->GetLootGUID();
        if (!guid.IsEmpty())
        {
            if (guid != state.loot || !guid.IsGameObject()) { StopFish(p, "Auto-fishing stopped: another loot window opened."); return; }
            GameObject* go = p->GetMap()->GetGameObject(guid);
            if (!go || (go->GetGoType() != GAMEOBJECT_TYPE_FISHINGNODE && go->GetGoType() != GAMEOBJECT_TYPE_FISHINGHOLE))
            { StopFish(p, "Auto-fishing stopped: fishing loot is unavailable."); return; }
            Loot* loot = &go->loot;
            uint32 slots = std::min(loot->GetMaxSlotInLootFor(p), 256u);
            for (uint32 slot = 0; slot < slots; ++slot)
            {
                LootItem* item = loot->LootItemInSlot(slot, p);
                if (!item || item->is_looted) continue;
                ItemPosCountVec destination;
                if (p->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item->itemid, item->count) != EQUIP_ERR_OK)
                { StopFish(p, "Auto-fishing stopped: bags are full or an item limit was reached. Take the remaining loot manually."); return; }
                WorldPacket packet;
                packet << uint8(slot);
                p->GetSession()->HandleAutostoreLootItemOpcode(packet);
            }
            if (loot->gold)
            {
                WorldPacket packet;
                p->GetSession()->HandleLootMoneyOpcode(packet);
            }
            if (!loot->isLooted()) { StopFish(p, "Auto-fishing stopped: some loot could not be collected."); return; }
            p->GetSession()->DoLootRelease(guid);
        }
        state.phase = FishPhase::Recast;
        state.delay = std::clamp(sConfigMgr->GetOption<uint32>("LocalQoL.Fish.RecastDelayMs", 700), 300u, 10000u);
        return;
    }
    if (state.phase == FishPhase::Recast)
    {
        if (!p->HasActiveSpell(state.spell) || p->IsNonMeleeSpellCast(false) || !p->GetLootGUID().IsEmpty())
        { StopFish(p, "Auto-fishing stopped: another action is in progress."); return; }
        // Untriggered cast preserves skill, pole, water and other normal checks.
        if (p->CastSpell(p, state.spell, false) != SPELL_CAST_OK)
            StopFish(p, "Auto-fishing stopped: unable to cast. Check water, fishing pole and skill.");
    }
}

class QoLSpells : public AllSpellScript
{
public:
    QoLSpells() : AllSpellScript("LocalQoLSpells", {
        ALLSPELLHOOK_ON_CALC_CAST_TIME, ALLSPELLHOOK_ON_PREPARE, ALLSPELLHOOK_ON_CAST_CANCEL
    }) { }

    void OnSpellCalcCastTime(Spell* spell, int32& time) override
    {
        Player* p = spell->GetCaster()->ToPlayer();
        if (time <= 0 || !Human(p) || p->IsInCombat() ||
            !sConfigMgr->GetOption<bool>("LocalQoL.Craft.Enable", true) || !CraftSpell(spell->GetSpellInfo()) || !NearWorkshop(p)) return;
        time = CraftTime(time, sConfigMgr->GetOption<float>("LocalQoL.Craft.Multiplier", 0.1f),
            sConfigMgr->GetOption<int32>("LocalQoL.Craft.MinimumMs", 200));
    }

    void OnSpellPrepare(Spell*, Unit* caster, SpellInfo const* info) override
    {
        Player* p = caster ? caster->ToPlayer() : nullptr;
        if (!Human(p) || !FishEnabled(p) || !FishingSpell(info)) return;
        State& s = Data(p);
        s.phase = FishPhase::Waiting;
        s.spell = info->Id;
        s.elapsed = s.delay = 0;
        s.loot.Clear();
        s.map = p->GetMapId();
        s.anchor.Relocate(p);
    }

    void OnSpellCastCancel(Spell*, Unit* caster, SpellInfo const* info, bool) override
    {
        Player* p = caster ? caster->ToPlayer() : nullptr;
        if (Human(p) && FishingSpell(info)) StopFish(p, nullptr);
    }
};

class QoLPlayer : public PlayerScript
{
public:
    QoLPlayer() : PlayerScript("LocalQoLPlayer", { PLAYERHOOK_ON_AFTER_UPDATE, PLAYERHOOK_ON_LOOT_ITEM }) { }
    void OnPlayerLootItem(Player* p, Item* item, uint32, ObjectGuid) override
    {
        if (Human(p) && item && item->GetTemplate()->Quality == ITEM_QUALITY_POOR && JunkEnabled(p))
            Data(p).sellPending = true;
    }
    void OnPlayerAfterUpdate(Player* p, uint32 diff) override
    {
        if (!Human(p)) return;
        State* state = p->CustomData.Get<State>(Settings);
        if (!state) return;
        state->tick += diff;
        if (state->tick < 200) return;
        uint32 elapsed = state->tick;
        state->tick = 0;
        if (state->sellPending && !p->IsInCombat()) { state->sellPending = false; SellJunk(p); }
        UpdateFish(p, *state, elapsed);
    }
};

class QoLCommands : public CommandScript
{
public:
    QoLCommands() : CommandScript("LocalQoLCommands") { }
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable commands = {
            { "fish", Fish, SEC_PLAYER, Console::No },
            { "junk", Junk, SEC_PLAYER, Console::No },
            { "status", Status, SEC_PLAYER, Console::No },
        };
        static ChatCommandTable root = { { "qol", commands } };
        return root;
    }
    static bool Fish(ChatHandler* handler, bool enabled)
    {
        Player* p = handler->GetPlayer();
        if (!Human(p)) return false;
        p->UpdatePlayerSetting(Settings, FishSetting, enabled ? 1 : 0);
        StopFish(p, nullptr);
        if (!enabled)
            if (Spell* channel = p->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
                if (FishingSpell(channel->GetSpellInfo())) p->InterruptSpell(CURRENT_CHANNELED_SPELL);
        p->SaveToDB(false, false);
        handler->SendSysMessage(enabled ? "Auto-fishing enabled. Cast your fishing skill once to start." : "Auto-fishing disabled.");
        return true;
    }
    static bool Junk(ChatHandler* handler, bool enabled)
    {
        Player* p = handler->GetPlayer();
        if (!Human(p)) return false;
        p->UpdatePlayerSetting(Settings, JunkSetting, enabled ? 2 : 1);
        Data(p).sellPending = enabled;
        p->SaveToDB(false, false);
        handler->SendSysMessage(enabled ? "Auto-junk enabled: gray items in your bags will be sold." : "Auto-junk disabled.");
        return true;
    }
    static bool Status(ChatHandler* handler)
    {
        Player* p = handler->GetPlayer();
        if (!Human(p)) return false;
        handler->PSendSysMessage("Auto-junk: {}. Auto-fishing: {}. Crafting workshop nearby: {}.",
            JunkEnabled(p) ? "on" : "off", FishEnabled(p) ? "on" : "off", NearWorkshop(p) ? "yes" : "no");
        return true;
    }
};
}

void Addmod_local_qolScripts()
{
    new LocalQoL::QoLSpells();
    new LocalQoL::QoLPlayer();
    new LocalQoL::QoLCommands();
}
