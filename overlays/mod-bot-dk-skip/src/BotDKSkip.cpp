// SPDX-License-Identifier: MIT
// Quest order, racial quest mapping and destinations adapted from AzerothCore
// mod-skip-dk-starting-area, SkipDK.cpp at f16befc79cba29cf2ba42770487350b987d5e529.
// Original concept: conanhun513; TC rewrite: MDic; review: JinnaiX.
// See ../LICENSE for the upstream copyright and license.

#include "Config.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerbotAIConfig.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>

bool IsBotDKSkipEnabled(Player* player);

namespace
{
bool enabled = true;
constexpr uint32 StarterMap = 609;
constexpr uint32 LichKingWhisper = 58207;
constexpr uint32 GraduationLevel = 58;

uint32 RacialQuest(uint8 race)
{
    switch (race)
    {
        case RACE_TAUREN: return 12739;
        case RACE_HUMAN: return 12742;
        case RACE_NIGHTELF: return 12743;
        case RACE_DWARF: return 12744;
        case RACE_GNOME: return 12745;
        case RACE_DRAENEI: return 12746;
        case RACE_BLOODELF: return 12747;
        case RACE_ORC: return 12748;
        case RACE_TROLL: return 12749;
        case RACE_UNDEAD_PLAYER: return 12750;
        default: return 0;
    }
}

void SkipIntroduction(Player* player, bool teleport)
{
    if (!IsBotDKSkipEnabled(player) || (teleport && player->GetMapId() != StarterMap))
        return;

    if (!player->IsAlive() || player->IsInCombat() || player->IsBeingTeleported())
        return;

    uint32 racialQuest = RacialQuest(player->getRace());
    if (!racialQuest)
        return;

    bool alliance = player->GetTeamId() == TEAM_ALLIANCE;
    std::array<uint32, 33> quests = {{
        12593, 12619, 12842, 12848, 12636, 12641, 12657, 12678, 12679, 12680, 12687,
        12698, 12701, 12706, 12716, 12719, 12720, 12722, 12724, 12725, 12727, 12733,
        racialQuest, 12751, 12754, 12755, 12756, 12757, 12779, 12801, 13165, 13166,
        alliance ? 13188u : 13189u
    }};

    // Validate every quest before granting any reward; tolerate already completed intros.
    for (uint32 id : quests)
        if (!sObjectMgr->GetQuestTemplate(id))
        {
            LOG_ERROR("playerbots", "BotDKSkip: missing quest {}; cannot finish introduction for {}.",
                id, player->GetName());
            return;
        }

    uint32 oldLevel = player->GetLevel();
    uint32 oldXP = player->GetUInt32Value(PLAYER_XP);
    bool experienceLocked = player->HasPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    player->SetPlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);
    for (uint32 id : quests)
    {
        if (player->GetQuestRewardStatus(id))
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(id);
        if (player->GetQuestStatus(id) == QUEST_STATUS_NONE)
            player->AddQuest(quest, nullptr);
        player->SetQuestStatus(id, QUEST_STATUS_COMPLETE);
        player->RewardQuest(quest, 0, player, false);
    }

    if (!experienceLocked)
        player->RemovePlayerFlag(PLAYER_FLAGS_NO_XP_GAIN);

    // Use quest rewards for spells/talent points; do not reset a higher-level bot's progress.
    uint32 targetLevel = std::max(oldLevel, GraduationLevel);
    targetLevel = std::min(targetLevel, sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
    player->GiveLevel(targetLevel);
    player->SetUInt32Value(PLAYER_XP, oldLevel == targetLevel ? oldXP : 0);
    player->InitTalentForLevel();
    player->RemoveAurasDueToSpell(LichKingWhisper);
    if (!teleport)
        return;

    if (!player->HasItemCount(6948, 1, true))
        player->AddItem(6948, 1);

    WorldLocation destination = alliance
        ? WorldLocation(0, -8866.55f, 671.39f, 97.90f, 5.27f)
        : WorldLocation(1, 1637.62f, -4440.22f, 15.78f, 2.42f);
    player->SetHomebind(destination, alliance ? 1519 : 1637);
    bool teleported = player->TeleportTo(destination);
    player->SaveToDB(false, false);
    LOG_INFO("playerbots", "BotDKSkip: finished introduction for {} at level {}; capital teleport: {}.",
        player->GetName(), player->GetLevel(), teleported);
}

class BotDKSkipWorld final : public WorldScript
{
public:
    BotDKSkipWorld() : WorldScript("BotDKSkipWorld") { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        enabled = sConfigMgr->GetOption<bool>("BotDKSkip.Enable", true);
    }
};

class BotDKSkipPlayer final : public PlayerScript
{
public:
    BotDKSkipPlayer() : PlayerScript("BotDKSkipPlayer", {PLAYERHOOK_ON_LOGIN}) { }

    void OnPlayerLogin(Player* player) override
    {
        SkipIntroduction(player, true);
    }
};
}

bool IsBotDKSkipEnabled(Player* player)
{
    WorldSession* session = player->GetSession();
    return enabled && player->getClass() == CLASS_DEATH_KNIGHT && session && session->IsHeadless() &&
        sPlayerbotAIConfig.IsInRandomAccountList(session->GetAccountId());
}

// The bot factory clears quest/spell state on a full rebuild. Restore the introduction
// rewards before it allocates talents, even when generic PreQuests is disabled.
void CompleteBotDKIntroduction(Player* player)
{
    SkipIntroduction(player, false);
}

void Addmod_bot_dk_skipScripts()
{
    new BotDKSkipWorld();
    new BotDKSkipPlayer();
}
