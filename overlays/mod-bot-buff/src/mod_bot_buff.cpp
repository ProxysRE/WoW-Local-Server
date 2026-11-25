// SPDX-License-Identifier: GPL-3.0-or-later
#include "BotBuffPolicy.h"
#include "Chat.h"
#include "CheckMountStateAction.h"
#include "LFGMgr.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "PlayerbotSecurity.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "WorldSession.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace BotBuff
{
constexpr float Range = 30.0f;
constexpr uint32 Lifetime = 20000, Cooldown = 5000, StepDelay = 1800;
constexpr std::size_t MaxRequests = 32;
struct Client
{
    uint32 last = 0;
    uint64 session = 0;
};
uint64 nextSerial = 0;
struct Request
{
    ObjectGuid requester;
    uint64 session = 0, serial = 0;
    std::string mode;
    std::deque<Id> families;
    uint32 started = 0, lastStep = 0, delay = 0, casts = 0;
    bool initialized = false, processing = false;
};
std::mutex mutex;
std::unordered_map<ObjectGuid, Request> requests;
std::unordered_map<ObjectGuid, Client> clients;
std::atomic<bool> hasWork{false};
void Tell(Player* player, char const* message)
{
    if (player && player->GetSession())
        player->SendSystemMessage(message);
}
bool Eligible(Player* bot, Player* player)
{
    if (!bot || !player || bot == player || !bot->GetSession() || !bot->GetSession()->IsHeadless() ||
        !player->GetSession() || player->GetSession()->IsHeadless() || !bot->IsInWorld() || !player->IsInWorld() ||
        bot->GetSession()->PlayerLogout() || player->GetSession()->PlayerLogout() || !bot->IsAlive() ||
        !player->IsAlive() || bot->IsInCombat() || player->IsInCombat() || bot->IsInFlight() || player->IsInFlight() ||
        bot->IsBeingTeleported() || player->IsBeingTeleported() || bot->InBattleground() || player->InBattleground() ||
        bot->InBattlegroundQueue() || sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE || bot->duel ||
        player->duel || !bot->IsInMap(player) || !bot->InSamePhase(player) || bot->GetTeamId() != player->GetTeamId() ||
        !bot->IsFriendlyTo(player) || !bot->IsWithinDistInMap(player, Range) || !bot->IsWithinLOSInMap(player))
        return false;
    auto* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
    return ai && (!ai->GetMaster() || ai->GetMaster() == player || !IsRealPlayer(ai->GetMaster())) &&
           ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_TALK, true, player);
}
std::vector<Id> Known(Player* bot, Id family)
{
    std::vector<Id> result;
    for (auto const& [id, data] : bot->GetSpellMap())
        if (bot->HasSpell(id) && sSpellMgr->GetFirstSpellInChain(id) == family)
        {
            auto* info = sSpellMgr->GetSpellInfo(id);
            if (info && info->IsPositive() && !info->IsPassive())
                result.push_back(id);
        }
    std::sort(result.begin(), result.end(),
              [](Id a, Id b) { return sSpellMgr->GetSpellRank(a) > sSpellMgr->GetSpellRank(b); });
    return result;
}
bool Covered(Player* bot, Player* player, Id family, Id proposed, bool preserveOwnBlessing)
{
    // Keep equal/stronger buffs and group versions. Auto mode preserves this paladin's other blessing.
    for (auto const& [key, application] : player->GetAppliedAuras())
    {
        Aura* aura = application->GetBase();
        Id first = sSpellMgr->GetFirstSpellInChain(aura->GetId());
        if (preserveOwnBlessing && Blessing(family) && Blessing(first) && aura->GetCasterGUID() == bot->GetGUID())
            return true;
        if (Equivalent(first) != family)
            continue;
        if (first != family || sSpellMgr->GetSpellRank(aura->GetId()) >= sSpellMgr->GetSpellRank(proposed))
            return true;
    }
    return false;
}
// Runs only in the selected bot's map update. No game-object pointer survives this call.
bool Step(Player* bot, Player* player, Request& request)
{
    if (GetMSTimeDiffToNow(request.started) > Lifetime || !Eligible(bot, player))
    {
        Tell(player, "BotBuff: запрос прекращён — время истекло, бой или бот больше не рядом.");
        return true;
    }
    auto* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!request.initialized)
    {
        bool kings = !Known(bot, Kings).empty();
        auto families = Families(bot->getClass(), request.mode, player->getPowerType() == POWER_MANA, kings);
        request.families = std::deque<Id>(families.begin(), families.end());
        request.initialized = true;
        if (request.families.empty())
        {
            Tell(player, "BotBuff: у этого класса нет подходящих одиночных баффов для такого запроса.");
            return true;
        }
    }
    if (bot->IsNonMeleeSpellCast(false))
    {
        request.delay = 500;
        return false;
    }
    if (bot->IsMounted())
    {
        CheckMountStateAction::CompleteDismount(bot);
        ai->SetNextCheckDelay(StepDelay);
        request.delay = 500;
        return false;
    }
    while (!request.families.empty())
    {
        Id family = request.families.front();
        auto ranks = Known(bot, family);
        for (Id spell : ranks)
        {
            if (Covered(bot, player, family, spell, request.mode.empty()))
                break;
            if (bot->GetGlobalCooldownMgr().HasGlobalCooldown(sSpellMgr->GetSpellInfo(spell)))
            {
                request.delay = 500;
                return false;
            }
            // Normal spell checks select a legal rank for the target and enforce mana/reagents/cooldowns.
            if (!ai->CanCastSpell(spell, player, true))
                continue;
            if (bot->isMoving())
                bot->StopMoving();
            if (ai->CastSpell(spell, player))
            {
                ++request.casts;
                request.families.pop_front();
                if (bot->getClass() == Paladin)
                    request.families.clear();
                ai->SetNextCheckDelay(StepDelay);
                request.delay = StepDelay;
                return false;
            }
            // GCD, facing or stand-up may need one more ordinary AI update; never force the cast.
            request.delay = 500;
            return false;
        }
        request.families.pop_front();
    }
    Tell(player, request.casts
                     ? "BotBuff: баффы наложены."
                     : "BotBuff: новых баффов не наложено — уже есть подходящие либо заклинания сейчас недоступны.");
    return true;
}
class Hooks : public PlayerScript
{
  public:
    Hooks()
        : PlayerScript("bot_buff_requests", {PLAYERHOOK_ON_BEFORE_UPDATE, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT})
    {
    }
    void OnPlayerLogin(Player* player) override
    {
        if (!player->GetSession() || player->GetSession()->IsHeadless())
            return;
        std::lock_guard<std::mutex> guard(mutex);
        clients[player->GetGUID()] = Client{0, ++nextSerial};
    }
    void OnPlayerLogout(Player* player) override
    {
        std::lock_guard<std::mutex> guard(mutex);
        clients.erase(player->GetGUID());
        requests.erase(player->GetGUID());
        std::erase_if(requests, [&](auto const& i) { return i.second.requester == player->GetGUID(); });
        hasWork.store(!requests.empty(), std::memory_order_relaxed);
    }
    void OnPlayerBeforeUpdate(Player* bot, uint32) override
    {
        if (!hasWork.load(std::memory_order_relaxed))
            return;
        Request request;
        {
            std::lock_guard<std::mutex> guard(mutex);
            auto i = requests.find(bot->GetGUID());
            if (i == requests.end() || i->second.processing ||
                (i->second.lastStep && GetMSTimeDiffToNow(i->second.lastStep) < i->second.delay))
                return;
            auto client = clients.find(i->second.requester);
            if (client == clients.end() || client->second.session != i->second.session)
            {
                requests.erase(i);
                hasWork.store(!requests.empty(), std::memory_order_relaxed);
                return;
            }
            i->second.processing = true;
            request = i->second;
        }
        Player* player = ObjectAccessor::GetPlayer(*bot, request.requester);
        bool finished = Step(bot, player, request);
        request.processing = false;
        request.lastStep = getMSTime();
        {
            std::lock_guard<std::mutex> guard(mutex);
            auto i = requests.find(bot->GetGUID());
            if (i != requests.end() && i->second.serial == request.serial)
            {
                if (finished)
                    requests.erase(i);
                else
                    i->second = std::move(request);
            }
            hasWork.store(!requests.empty(), std::memory_order_relaxed);
        }
    }
};
class Cleanup : public WorldScript
{
  public:
    Cleanup() : WorldScript("bot_buff_cleanup") {}
    void OnUpdate(uint32 diff) override
    {
        if (!hasWork.load(std::memory_order_relaxed))
            return;
        _elapsed += diff;
        if (_elapsed < 1000)
            return;
        _elapsed = 0;
        std::lock_guard<std::mutex> guard(mutex);
        std::erase_if(requests, [](auto const& i)
                      { return !i.second.processing && GetMSTimeDiffToNow(i.second.started) > Lifetime; });
        hasWork.store(!requests.empty(), std::memory_order_relaxed);
    }

  private:
    uint32 _elapsed = 0;
};
class Commands : public CommandScript
{
  public:
    Commands() : CommandScript("bot_buff_command") {}
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable commands = {
            {"botbuff", Handle, SEC_PLAYER, Acore::ChatCommands::Console::No}};
        return commands;
    }
    static bool Handle(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetPlayer();
        if (!player || player->GetSession()->IsHeadless())
            return true;
        std::string mode, extra;
        std::istringstream input(args ? args : "");
        input >> mode;
        if ((input >> extra) || !Mode(mode))
        {
            Tell(player, "BotBuff: .botbuff [kings|might|wisdom|sanctuary]");
            return true;
        }
        Player* bot = handler->getSelectedPlayer();
        if (!Eligible(bot, player))
        {
            Tell(player, "BotBuff: выдели дружественного свободного бота рядом, вне боя и перелёта.");
            return true;
        }
        {
            std::lock_guard<std::mutex> guard(mutex);
            auto& client = clients[player->GetGUID()];
            if (!client.session)
                client.session = ++nextSerial;
            if (client.last && GetMSTimeDiffToNow(client.last) < Cooldown)
            {
                Tell(player, "BotBuff: подожди несколько секунд перед новым запросом.");
                return true;
            }
            // A bot that logged out cannot retain a permanent reservation.
            std::erase_if(requests, [](auto const& i)
                          { return !i.second.processing && GetMSTimeDiffToNow(i.second.started) > Lifetime; });
            if (requests.size() >= MaxRequests || requests.count(bot->GetGUID()))
            {
                Tell(player, "BotBuff: бот уже выполняет запрос, попробуй позже.");
                return true;
            }
            Request request;
            request.requester = player->GetGUID();
            request.session = client.session;
            request.serial = ++nextSerial;
            request.mode = mode;
            request.started = getMSTime();
            requests.emplace(bot->GetGUID(), std::move(request));
            client.last = getMSTime();
            hasWork.store(true, std::memory_order_relaxed);
        }
        Tell(player, "BotBuff: запрос принят. Останься рядом с ботом на несколько секунд.");
        return true;
    }
};
} // namespace BotBuff
void Addmod_bot_buffScripts()
{
    new BotBuff::Commands();
    new BotBuff::Hooks();
    new BotBuff::Cleanup();
}
