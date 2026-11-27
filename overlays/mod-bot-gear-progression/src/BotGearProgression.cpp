// SPDX-License-Identifier: GPL-2.0-or-later
#include "GearStagePolicy.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "PlayerbotGuildMgr.h"
#include "Playerbots.h"
#include "QueryResult.h"
#include "ScriptMgr.h"
#include "WorldSession.h"

#include <atomic>
#include <deque>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
struct Settings
{
    int stage = 8;
    uint8 raidSize = 10;
    bool heroic = false;
    bool hardModes = false;
    bool reroll = true;
    uint32 interval = 200;
    bool accountsReady = false;
    std::unordered_set<uint32> accounts;
    std::unordered_map<uint32, int> overrides;
};
std::atomic<std::shared_ptr<Settings const>> settings{std::make_shared<Settings>()};
std::unordered_map<uint32, std::vector<uint32>> friendOwners;
std::deque<ObjectGuid> pending;
std::unordered_set<uint32> queued;
std::unordered_set<uint32> processed;
uint32 timer = 0;

bool AutomaticAccount(uint32 account, Settings const& config)
{
    return config.accountsReady ? config.accounts.count(account) != 0 :
        sPlayerbotAIConfig.IsInRandomAccountList(account);
}

bool IsAutomaticBot(Player* player, Settings const& config)
{
    WorldSession* session = player->GetSession();
    return session && session->IsHeadless() &&
        AutomaticAccount(session->GetAccountId(), config);
}

bool IsProtected(Player* player, Settings const& config)
{
    if (PlayerbotGuildMgr::instance().IsRealGuild(player->GetGuildId()))
        return true;

    auto friends = friendOwners.find(player->GetGUID().GetCounter());
    if (friends != friendOwners.end())
        for (uint32 account : friends->second)
            if (!AutomaticAccount(account, config))
                return true;
    return false;
}

}

// Also used before candidates enter the factory's limited-size equipment pool.
bool BotGearStageActive(Player* player)
{
    auto config = settings.load();
    return config->stage >= 0 && IsAutomaticBot(player, *config) && !IsProtected(player, *config);
}

bool BotGearStageAllows(Player* player, ItemTemplate const* item)
{
    if (!item)
        return false;
    auto config = settings.load();
    if (config->stage < 0 || !IsAutomaticBot(player, *config) || IsProtected(player, *config))
        return true;
    if (item->Class != ITEM_CLASS_WEAPON && item->Class != ITEM_CLASS_ARMOR)
        return true;
    // Cosmetic slots have no combat power and are not replaced by the factory.
    if (item->InventoryType == INVTYPE_BODY || item->InventoryType == INVTYPE_TABARD)
        return true;
    auto override = config->overrides.find(item->ItemId);
    if (override != config->overrides.end())
        return BotGear::Allowed(config->stage, override->second);
    return BotGear::ItemAllowed(item->ItemId, config->stage, config->raidSize, config->heroic, config->hardModes);
}

namespace
{
class BotGearProgressionWorld final : public WorldScript
{
public:
    BotGearProgressionWorld() : WorldScript("BotGearProgressionWorld") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        auto config = std::make_shared<Settings>();
        auto previous = settings.load();
        config->accountsReady = previous->accountsReady;
        config->accounts = previous->accounts;
        config->stage = sConfigMgr->GetOption<int>("BotGearProgression.Stage", 8);
        if (config->stage < -1 || config->stage > 17)
        {
            LOG_ERROR("playerbots", "BotGearProgression: invalid stage {}; retaining the previous settings.",
                config->stage);
            return;
        }
        config->reroll = sConfigMgr->GetOption<bool>("BotGearProgression.RerollOnStartup", true);
        uint32 raidSize = sConfigMgr->GetOption<uint32>("BotGearProgression.CurrentTierRaidSize", 10);
        if (raidSize != 10 && raidSize != 25)
        {
            LOG_ERROR("playerbots", "BotGearProgression: raid size must be 10 or 25; retaining previous settings.");
            return;
        }
        config->raidSize = static_cast<uint8>(raidSize);
        config->heroic = sConfigMgr->GetOption<bool>("BotGearProgression.CurrentTierHeroic", false);
        config->hardModes = sConfigMgr->GetOption<bool>("BotGearProgression.CurrentTierHardModes", false);
        config->interval = std::clamp(sConfigMgr->GetOption<uint32>(
            "BotGearProgression.RerollIntervalMs", 200), 50u, 10000u);
        std::istringstream input(sConfigMgr->GetOption<std::string>("BotGearProgression.ItemOverrides", ""));
        std::string entry;
        while (std::getline(input, entry, ','))
        {
            std::istringstream value(entry);
            uint32 id;
            int stage;
            char separator;
            if (!(value >> id >> separator >> stage) || separator != ':' || !id || stage < 0 || stage > 99 ||
                !(value >> std::ws).eof())
            {
                LOG_ERROR("playerbots", "BotGearProgression: invalid item override '{}'; retaining previous settings.",
                    entry);
                return;
            }
            config->overrides[id] = stage;
        }
        settings.store(config);
        LOG_INFO("playerbots", "BotGearProgression: stage {}, raid size {}, heroic {}, hard modes {}, "
            "{} source rules; startup equipment reroll: {}.", config->stage, config->raidSize, config->heroic,
            config->hardModes, std::size(BotGear::Catalog), config->reroll);
    }

    void OnStartup() override
    {
        auto config = std::make_shared<Settings>(*settings.load());
        config->accounts.insert(sPlayerbotAIConfig.RandomBotAccounts.begin(),
            sPlayerbotAIConfig.RandomBotAccounts.end());
        config->accountsReady = true;
        settings.store(config);
        // One startup aggregation, never a synchronous query on bot/map update threads.
        // Keep the owners' account IDs: the bot account list may still be initializing.
        QueryResult result = CharacterDatabase.Query(
            "SELECT s.friend, c.account FROM character_social s "
            "INNER JOIN characters c ON c.guid = s.guid WHERE (s.flags & 1) <> 0");
        if (!result)
            return;
        do
        {
            Field* fields = result->Fetch();
            friendOwners[fields[0].Get<uint32>()].push_back(fields[1].Get<uint32>());
        } while (result->NextRow());
    }

    void OnUpdate(uint32 diff) override
    {
        auto config = settings.load();
        if (pending.empty() || !config->reroll || config->stage < 0)
            return;
        if (timer > diff)
        {
            timer -= diff;
            return;
        }
        timer = config->interval;
        ObjectGuid guid = pending.front();
        pending.pop_front();
        queued.erase(guid.GetCounter());
        Player* player = ObjectAccessor::FindConnectedPlayer(guid);
        if (!player || !IsAutomaticBot(player, *config))
            return;
        if (IsProtected(player, *config))
        {
            processed.insert(guid.GetCounter());
            LOG_INFO("playerbots", "BotGearProgression: protected friend/player-guild bot {}; equipment retained.",
                player->GetName());
            return;
        }
        if (!GET_PLAYERBOT_AI(player) || !player->IsInWorld() || !player->IsAlive() || player->IsInCombat() ||
            player->IsBeingTeleported() || player->InBattleground() || player->InBattlegroundQueue() ||
            player->IsInFlight())
        {
            pending.push_back(guid);
            queued.insert(guid.GetCounter());
            return;
        }

        if (player->GetLevel() < 5)
        {
            // The native factory keeps the original starting outfit at these levels.
            processed.insert(guid.GetCounter());
            return;
        }

        // Match native clear+autogear semantics: full bags cannot prevent replacement.
        // Only disposable random-bot gear reaches here; protected characters were excluded above.
        PlayerbotFactory::DestroyEquippedGear(player);
        PlayerbotFactory::AutoGear(player, ITEM_QUALITY_EPIC, 0, false, false, true);
        uint32 unequipped = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (item && !BotGearStageAllows(player, item->GetTemplate()))
            {
                // These are generated random-bot items, never protected/personal characters' gear.
                player->DestroyItem(INVENTORY_SLOT_BAG_0, slot, true);
                ++unequipped;
            }
        }
        player->SaveToDB(false, false);
        processed.insert(guid.GetCounter());
        LOG_INFO("playerbots", "BotGearProgression: {} rerolled for stage {}; {} unavailable leftovers removed.",
            player->GetName(), config->stage, unequipped);
    }
};

class BotGearProgressionPlayer final : public PlayerScript
{
public:
    BotGearProgressionPlayer() : PlayerScript("BotGearProgressionPlayer", {
        PLAYERHOOK_ON_LOGIN, PLAYERHOOK_CAN_EQUIP_ITEM
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        auto config = settings.load();
        uint32 id = player->GetGUID().GetCounter();
        if (IsAutomaticBot(player, *config) && !processed.count(id) && queued.insert(id).second)
            pending.push_back(player->GetGUID());
    }

    bool OnPlayerCanEquipItem(Player* player, uint8 /*slot*/, uint16& /*dest*/, Item* item,
        bool /*swap*/, bool notLoading) override
    {
        return !notLoading || !item || BotGearStageAllows(player, item->GetTemplate());
    }
};
}

void Addmod_bot_gear_progressionScripts()
{
    new BotGearProgressionWorld();
    new BotGearProgressionPlayer();
}
