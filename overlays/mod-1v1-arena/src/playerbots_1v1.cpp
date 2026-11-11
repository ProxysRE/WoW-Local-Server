/*
 * Local integration of azerothcore/mod-1v1-arena and mod-playerbots.
 * GPL-3.0-or-later. Upstream code and author notices remain in their original files.
 */
#include "playerbots_1v1.h"
#include "ArenaBotPolicy.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Chat.h"
#include "Config.h"
#include "LFGMgr.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "TaskScheduler.h"
#include "Timer.h"
#include "WorldSession.h"
#include "npc_1v1arena.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <vector>

namespace
{
constexpr auto BotQueue = static_cast<BattlegroundQueueTypeId>(BATTLEGROUND_QUEUE_5v5 + 1);
constexpr std::uint8_t SoloArenaType = 1;
constexpr std::uint32_t JoinTimeout = 90 * IN_MILLISECONDS;
constexpr std::uint32_t MatchTimeout = 20 * MINUTE * IN_MILLISECONDS;
constexpr std::size_t MaxPendingRequests = 128;

struct Pair
{
    ObjectGuid human;
    ObjectGuid bot;
    std::uint32_t since;
    std::uint32_t instance = 0;
    unsigned bracket;
    bool hadSharedArena = false;
};

std::atomic<bool> enabled{false};
std::mutex requestMutex;
std::set<ObjectGuid> pending;
std::set<ObjectGuid> cancellations;

bool IsAvailable(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->PlayerLogout() && player->IsInWorld() &&
           player->IsAlive() && !player->IsInCombat() && !player->IsInFlight() && !player->IsBeingTeleported() &&
           !player->InBattleground() && !player->InBattlegroundQueue() && !player->GetGroup() && !player->duel &&
           !player->HasAura(26013) && sLFGMgr->GetState(player->GetGUID()) == lfg::LFG_STATE_NONE;
}

unsigned GetProgression(Player* player)
{
    unsigned result = 0;
    for (unsigned stage = 1; stage <= 18; ++stage)
        if (player->GetQuestStatus(66000 + stage) == QUEST_STATUS_REWARDED)
            result = stage;
    return result;
}

void Tell(Player* player, char const* message)
{
    if (player && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(message);
}

void RemoveQueue(ObjectGuid guid)
{
    auto& queue = sBattlegroundMgr->GetBattlegroundQueue(BotQueue);
    Player* player = ObjectAccessor::FindConnectedPlayer(guid);
    if (player && player->InBattlegroundQueueForBattlegroundQueueType(BotQueue) && !player->InBattleground())
    {
        auto slot = player->GetBattlegroundQueueIndex(BotQueue);
        queue.RemovePlayer(guid, true);
        player->RemoveBattlegroundQueueId(BotQueue);
        WorldPacket packet;
        auto* arena = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        sBattlegroundMgr->BuildBattlegroundStatusPacket(&packet, arena, slot, STATUS_NONE, 0, 0, 0, TEAM_NEUTRAL);
        player->SendDirectMessage(&packet);
    }
    else
    {
        GroupQueueInfo info;
        if (queue.GetPlayerGroupInfoData(guid, &info))
            queue.RemovePlayer(guid, true);
    }
}

class ArenaBotWorld : public WorldScript
{
public:
    ArenaBotWorld() : WorldScript("local_arena_bot_world", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_STARTUP, WORLDHOOK_ON_UPDATE }) { }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        enabled = sConfigMgr->GetOption<bool>("Arena1v1.Enable", true) &&
                  sConfigMgr->GetOption<bool>("Arena1v1.Playerbots.Enable", false);
        _maxItemLevelGap = std::clamp(sConfigMgr->GetOption<float>("Arena1v1.Playerbots.MaxItemLevelGap", 15.0f),
                                    0.0f, 100.0f);
        _compareProgression = sConfigMgr->GetOption<bool>("IndividualProgression.Enable", false);
        _minLevel = sConfigMgr->GetOption<unsigned>("Arena1v1.MinLevel", 80);
    }

    void OnStartup() override
    {
        _scheduler.Schedule(std::chrono::seconds(1), [this](TaskContext context)
        {
            Tick();
            context.Repeat(std::chrono::seconds(1));
        });
    }

    void OnUpdate(std::uint32_t diff) override { _scheduler.Update(diff); }

private:
    void Release(Pair const& pair, bool cancel)
    {
        RemoveQueue(pair.human);
        RemoveQueue(pair.bot);
        if (cancel)
        {
            for (ObjectGuid guid : {pair.human, pair.bot})
            {
                Player* player = ObjectAccessor::FindConnectedPlayer(guid);
                if (!player)
                    continue;
                Battleground* bg = player->GetBattleground();
                if (pair.instance && bg && bg->GetArenaType() == SoloArenaType &&
                    bg->GetInstanceID() == pair.instance)
                    player->LeaveBattleground();
            }
            Tell(ObjectAccessor::FindConnectedPlayer(pair.human), "1v1: cancelled; opponent released.");
        }
        LOG_INFO("module.arena1v1", "Playerbot pair {} / {} {}", pair.human.ToString(), pair.bot.ToString(),
                 cancel ? "cancelled" : "completed");
    }

    void Start(ObjectGuid guid)
    {
        Player* human = ObjectAccessor::FindConnectedPlayer(guid);
        if (!human || GET_PLAYERBOT_AI(human))
            return;
        if (!enabled || !IsAvailable(human) || human->GetLevel() < _minLevel)
        {
            Tell(human, "1v1: join solo, alive, out of combat, outside all queues, at the required level.");
            return;
        }
        auto* arena = sBattlegroundMgr->GetBattlegroundTemplate(BATTLEGROUND_AA);
        auto* bracket = arena ? GetBattlegroundBracketByLevel(arena->GetMapId(), human->GetLevel()) : nullptr;
        if (!bracket)
            return;
        unsigned bracketId = bracket->GetBracketId();
        if (std::any_of(_pairs.begin(), _pairs.end(), [bracketId](Pair const& pair)
            { return pair.bracket == bracketId; }) ||
            !sBattlegroundMgr->GetBattlegroundQueue(BotQueue).IsAllQueuesEmpty(bracket->GetBracketId()))
        {
            Tell(human, "1v1: this level bracket is busy. Try again after the current match.");
            return;
        }
        LocalArena::Candidate humanInfo{human->GetLevel(), GetProgression(human), human->GetAverageItemLevel()};
        Player* selected = nullptr;
        float nearest = _maxItemLevelGap + 1.0f;
        for (auto itr = sRandomPlayerbotMgr.GetPlayerBotsBegin(); itr != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++itr)
        {
            Player* bot = itr->second;
            if (!IsAvailable(bot) || !sRandomPlayerbotMgr.IsRandomBot(bot))
                continue;
            PlayerbotAI* ai = GET_PLAYERBOT_AI(bot);
            if (!ai || ai->GetMaster())
                continue;
            if (std::any_of(_pairs.begin(), _pairs.end(), [bot](Pair const& pair)
                { return pair.bot == bot->GetGUID(); }))
                continue;
            LocalArena::Candidate botInfo{bot->GetLevel(), GetProgression(bot), bot->GetAverageItemLevel()};
            if (!LocalArena::IsSuitable(humanInfo, botInfo, _compareProgression, _maxItemLevelGap))
                continue;
            float gap = std::abs(humanInfo.itemLevel - botInfo.itemLevel);
            if (gap < nearest)
            {
                nearest = gap;
                selected = bot;
            }
        }
        if (!selected)
        {
            Tell(human, "1v1: no free bot of your level, progression and gear range. No character was changed.");
            return;
        }
        if (!npc_1v1arena::JoinQueueArena(human, nullptr, false, false))
            return;
        if (!npc_1v1arena::JoinQueueArena(selected, nullptr, false, false))
        {
            RemoveQueue(human->GetGUID());
            Tell(human, "1v1: opponent could not join; your queue was cancelled.");
            return;
        }
        _pairs.push_back({human->GetGUID(), selected->GetGUID(), getMSTime(), 0, bracketId, false});
        Tell(human, "1v1: a Playerbot opponent is queued. Accept the normal arena invitation.");
        LOG_INFO("module.arena1v1", "Queued {} vs {} (level {}, item-level gap {})", human->GetName(),
                 selected->GetName(), human->GetLevel(), nearest);
    }

    void Tick()
    {
        std::set<ObjectGuid> requests;
        std::set<ObjectGuid> cancelled;
        {
            std::lock_guard<std::mutex> lock(requestMutex);
            requests.swap(pending);
            cancelled.swap(cancellations);
        }
        for (auto itr = _pairs.begin(); itr != _pairs.end();)
        {
            Player* human = ObjectAccessor::FindConnectedPlayer(itr->human);
            Player* bot = ObjectAccessor::FindConnectedPlayer(itr->bot);
            Battleground* humanBg = human ? human->GetBattleground() : nullptr;
            Battleground* botBg = bot ? bot->GetBattleground() : nullptr;
            bool same = human && bot && human->IsInWorld() && bot->IsInWorld() && humanBg && botBg &&
                        humanBg->GetArenaType() == SoloArenaType &&
                        humanBg->GetInstanceID() == botBg->GetInstanceID() &&
                        human->GetInstanceId() == humanBg->GetInstanceID() &&
                        bot->GetInstanceId() == botBg->GetInstanceID();
            for (Battleground* bg : {humanBg, botBg})
                if (!itr->instance && bg && bg->GetArenaType() == SoloArenaType)
                    itr->instance = bg->GetInstanceID();
            if (same && !itr->hadSharedArena)
            {
                itr->hadSharedArena = true;
                itr->since = getMSTime();
                LOG_INFO("module.arena1v1", "Playerbot pair {} / {} entered arena instance {}",
                         itr->human.ToString(), itr->bot.ToString(), itr->instance);
            }
            auto present = [](Player* player)
            {
                return player && (player->InBattlegroundQueueForBattlegroundQueueType(BotQueue) ||
                                  player->InBattleground() || player->IsBeingTeleported());
            };
            LocalArena::PairSnapshot snapshot{human != nullptr, bot != nullptr, present(human), present(bot),
                same, same && humanBg->GetStatus() == STATUS_WAIT_LEAVE, itr->hadSharedArena,
                getMSTimeDiff(itr->since, getMSTime())};
            auto result = LocalArena::EvaluatePair(snapshot, JoinTimeout, MatchTimeout);
            if (!enabled || cancelled.count(itr->human))
                result = LocalArena::PairResult::Cancel;
            if (result == LocalArena::PairResult::Keep)
            {
                ++itr;
                continue;
            }
            Release(*itr, result == LocalArena::PairResult::Cancel);
            itr = _pairs.erase(itr);
        }
        for (ObjectGuid guid : requests)
            if (!cancelled.count(guid))
                Start(guid);
    }

    TaskScheduler _scheduler;
    std::vector<Pair> _pairs;
    float _maxItemLevelGap = 15.0f;
    unsigned _minLevel = 80;
    bool _compareProgression = false;
};
}

bool ArenaBotBridgeEnabled() { return enabled.load(); }

bool RequestArenaBotOpponent(ObjectGuid player)
{
    std::lock_guard<std::mutex> lock(requestMutex);
    if (pending.size() >= MaxPendingRequests)
        return false;
    return pending.insert(player).second;
}

void CancelArenaBotOpponent(ObjectGuid player)
{
    std::lock_guard<std::mutex> lock(requestMutex);
    if (cancellations.size() < MaxPendingRequests)
        cancellations.insert(player);
}

void AddSC_playerbots_1v1() { new ArenaBotWorld(); }
