// SPDX-License-Identifier: GPL-3.0-or-later
#include "AiFactory.h"
#include "Chat.h"
#include "DataMap.h"
#include "Group.h"
#include "GroupBuilderPolicy.h"
#include "Item.h"
#include "LFGMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "PlayerbotOperation.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"
#include "StringFormat.h"
#include "Timer.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace GroupBuilder
{
constexpr char DataKey[] = "mod-group-builder";
constexpr uint32 PlanLife = 60000;
constexpr uint32 InviteLife = 20000;
std::atomic<uint64> nextSession{0};
struct State : DataMap::Base
{
    uint64 session = ++nextSession;
    Options options;
    std::vector<Filter> filters = std::vector<Filter>(5);
    std::vector<Candidate> chosen = std::vector<Candidate>(5);
    uint32 nonce = 0, planned = 0, lastSearch = 0, lastRequest = 0;
    bool configured = false;
};
struct Job
{
    uint64 session = 0;
    uint32 request = 0, started = 0;
    ObjectGuid expectedGroup;
    ObjectGuid waiting;
    std::set<ObjectGuid> members;
    std::deque<std::pair<unsigned, Candidate>> pending;
};
// Only the world thread reads or writes states/jobs, including all bot lookups.
std::map<ObjectGuid, Job> jobs;
uint32 nextNonce = 0;
std::mutex requestMutex;
std::set<ObjectGuid> requests;
State& Data(Player* player) { return *player->CustomData.GetDefault<State>(DataKey); }
ObjectGuid Guid(uint32 id) { return ObjectGuid::Create<HighGuid::Player>(id); }
void Send(Player* player, uint32 request, std::string_view kind, std::string_view payload = "")
{
    player->SendSystemMessage(Acore::StringFormat("ACG1:{}:{}:{}", request, kind, payload));
}
void Error(Player* player, uint32 request, std::string_view message) { Send(player, request, "ERROR", message); }
bool Leader(Player* player)
{
    Group* group = player->GetGroup();
    return !group || (group->IsLeader(player->GetGUID()) && !group->isLFGGroup());
}
bool Active(Player* player)
{
    return player && player->GetSession() && !player->GetSession()->PlayerLogout() && player->IsInWorld() &&
           player->IsAlive() && !player->IsInCombat() && !player->IsInFlight() && !player->IsBeingTeleported() &&
           !player->InBattleground() && !player->InBattlegroundQueue() && !player->duel &&
           (!player->GetGroupInvite() || player->GetGroupInvite()->GetLeaderGUID() == player->GetGUID()) &&
           sLFGMgr->GetState(player->GetGUID()) == lfg::LFG_STATE_NONE;
}
bool Available(Player* owner, Player* bot, Options const& options)
{
    if (!Active(bot) || bot == owner || !bot->GetSession()->IsHeadless() || !sRandomPlayerbotMgr.IsRandomBot(bot) ||
        bot->GetTeamId() != owner->GetTeamId() || bot->GetGroup() || bot->GetGroupInvite() ||
        bot->GetMap()->IsDungeon() || bot->GetLevel() < options.minLevel || bot->GetLevel() > options.maxLevel ||
        (options.guild && (!owner->GetGuildId() || bot->GetGuildId() != owner->GetGuildId())))
        return false;
    auto* ai = sPlayerbotsMgr.GetPlayerbotAI(bot);
    if (!ai || (ai->GetMaster() && ai->GetMaster() != owner && IsRealPlayer(ai->GetMaster())))
        return false;
    return ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, true, owner);
}
unsigned Spec(Player* player)
{
    auto tabs = AiFactory::GetPlayerSpecTabs(player);
    unsigned highest = 0, chosen = 0, ties = 0;
    for (auto const& [tab, points] : tabs)
    {
        if (points > highest)
        {
            highest = points;
            chosen = tab + 1;
            ties = 1;
        }
        else if (points && points == highest)
            ++ties;
    }
    return highest && ties == 1 ? chosen : 0;
}
unsigned DetectRole(Player* player, unsigned spec)
{
    unsigned cls = player->getClass();
    bool tank = PlayerbotAI::IsTank(player), heal = PlayerbotAI::IsHeal(player);
    if (tank && heal)
        return Any;
    if (cls == CLASS_MAGE || cls == CLASS_WARLOCK || cls == CLASS_ROGUE || cls == CLASS_HUNTER)
        return Damage;
    if (!spec)
        return Any;
    if (tank)
    {
        if (cls == CLASS_WARRIOR || cls == CLASS_PALADIN)
        {
            Item* shield = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            unsigned protection = cls == CLASS_WARRIOR ? 3 : 2;
            return spec == protection && shield && shield->GetTemplate()->InventoryType == INVTYPE_SHIELD ? Tank : Any;
        }
        if (cls == CLASS_DRUID)
            return spec == 2 ? Tank : Any;
        if (cls == CLASS_DEATH_KNIGHT)
        {
            // Blood DPS exists in 3.3.5: talent branch alone is insufficient evidence of tank equipment.
            unsigned tankPieces = 0;
            for (uint8 slot = 0; slot < EQUIPMENT_SLOT_END; ++slot)
                if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                {
                    auto* proto = item->GetTemplate();
                    for (uint32 i = 0; i < proto->StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
                        if (proto->ItemStat[i].ItemStatValue > 0 &&
                            (proto->ItemStat[i].ItemStatType == ITEM_MOD_DEFENSE_SKILL_RATING ||
                             proto->ItemStat[i].ItemStatType == ITEM_MOD_DODGE_RATING ||
                             proto->ItemStat[i].ItemStatType == ITEM_MOD_PARRY_RATING))
                        {
                            ++tankPieces;
                            break;
                        }
                }
            return tankPieces >= 2 ? Tank : Any;
        }
        return Any;
    }
    if (heal)
    {
        bool healingTree = (cls == CLASS_PRIEST && spec != 3) || (cls == CLASS_PALADIN && spec == 1) ||
                           ((cls == CLASS_DRUID || cls == CLASS_SHAMAN) && spec == 3);
        return healingTree ? Healer : Any;
    }
    if (PlayerbotAI::IsDps(player) && DamageTree(cls, spec))
        return Damage;
    return Any;
}
Candidate Describe(Player* player, bool member = false)
{
    Candidate c;
    c.guid = player->GetGUID().GetCounter();
    c.cls = player->getClass();
    c.spec = Spec(player);
    c.role = DetectRole(player, c.spec);
    c.level = player->GetLevel();
    float ilvl = player->GetAverageItemLevelForDF();
    c.ilvl = std::isfinite(ilvl) ? static_cast<unsigned>(std::clamp(ilvl, 0.0f, 1000.0f)) : 0;
    c.member = member;
    c.name = player->GetName();
    return c;
}
std::vector<Candidate> Collect(Player* owner, Options const& options, bool members)
{
    std::vector<Candidate> result;
    std::set<uint32> seen;
    if (members)
    {
        if (Group* group = owner->GetGroup())
        {
            for (auto const& slot : group->GetMemberSlots())
            {
                Player* player = ObjectAccessor::FindPlayer(slot.guid);
                if (!player)
                    return {}; // Offline members still consume a place; never silently replace them.
                result.push_back(Describe(player, true));
                seen.insert(result.back().guid);
            }
        }
        else
        {
            result.push_back(Describe(owner, true));
            seen.insert(result.back().guid);
        }
    }
    for (auto const& [guid, bot] : sRandomPlayerbotMgr.GetAllBots())
        if (!seen.count(guid.GetCounter()) && Available(owner, bot, options))
            result.push_back(Describe(bot));
    std::sort(result.begin(), result.end(), [](auto const& a, auto const& b) { return a.guid < b.guid; });
    return result;
}
std::string Row(unsigned slot, Candidate const& c)
{
    return Acore::StringFormat("{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", slot, c.guid, c.cls, c.spec, c.role, c.level, c.ilvl,
                               c.member ? 1 : 0, Hex(c.name), c.guid ? "READY" : "EMPTY");
}
void Show(Player* player, uint32 request, State& state)
{
    state.nonce = ++nextNonce;
    if (!state.nonce)
        state.nonce = ++nextNonce;
    state.planned = getMSTime();
    Send(player, request, "PLAN", Acore::StringFormat("{}:{}", state.nonce, state.options.size));
    for (unsigned i = 0; i < state.chosen.size(); ++i)
        Send(player, request, "ROW", Row(i, state.chosen[i]));
    Send(player, request, "DONE");
}
void Plan(Player* player, uint32 request, State& state)
{
    state.nonce = 0;
    if (!Leader(player))
    {
        Error(player, request, "LEADER");
        return;
    }
    if (GetMSTimeDiffToNow(state.lastSearch) < 2000 && state.lastSearch)
    {
        Error(player, request, "RATE");
        return;
    }
    state.lastSearch = getMSTime();
    auto candidates = Collect(player, state.options, true);
    unsigned expected = player->GetGroup() ? player->GetGroup()->GetMembersCount() : 1;
    if (candidates.empty() || expected > state.options.size)
    {
        Error(player, request, "GROUP_SIZE");
        return;
    }
    auto allocation = Assign(state.filters, candidates, state.options.ilvl);
    state.chosen.assign(state.options.size, Candidate{});
    unsigned included = 0;
    for (unsigned i = 0; i < allocation.size(); ++i)
        if (allocation[i] >= 0)
        {
            state.chosen[i] = candidates[allocation[i]];
            included += state.chosen[i].member;
        }
    if (included != expected)
    {
        Error(player, request, "MEMBER_FILTERS");
        return;
    }
    Show(player, request, state);
}
void Alternatives(Player* player, uint32 request, State& state, unsigned slot, unsigned page)
{
    if (!state.nonce || slot >= state.options.size || state.chosen[slot].member)
    {
        Error(player, request, "PLAN_REQUIRED");
        return;
    }
    if (GetMSTimeDiffToNow(state.lastSearch) < 500 && state.lastSearch)
    {
        Error(player, request, "RATE");
        return;
    }
    state.lastSearch = getMSTime();
    auto candidates = Collect(player, state.options, false);
    auto filter = state.filters[slot];
    filter.pin = 0;
    std::set<uint32> used;
    for (unsigned i = 0; i < state.chosen.size(); ++i)
        if (i != slot)
            used.insert(state.chosen[i].guid);
    std::erase_if(candidates, [&](auto const& c) { return used.count(c.guid) || !Matches(filter, c); });
    std::sort(candidates.begin(), candidates.end(),
              [&](auto const& a, auto const& b)
              {
                  auto ca = Cost(filter, a, state.options.ilvl), cb = Cost(filter, b, state.options.ilvl);
                  return ca != cb ? ca < cb : a.guid < b.guid;
              });
    unsigned pages = std::max(1U, static_cast<unsigned>((candidates.size() + 7) / 8));
    page = std::min(page, pages - 1);
    Send(player, request, "ALTS", Acore::StringFormat("{}:{}:{}", slot, page, pages));
    for (unsigned i = page * 8; i < candidates.size() && i < (page + 1) * 8; ++i)
        Send(player, request, "BOT", Row(slot, candidates[i]));
    Send(player, request, "DONE");
}
void Pump(Player* player, Job& job)
{
    auto& state = Data(player);
    Group* group = player->GetGroup();
    Player* waiting = job.waiting ? ObjectAccessor::FindPlayer(job.waiting) : nullptr;
    bool changed = job.expectedGroup ? (!group || group->GetGUID() != job.expectedGroup)
                                     : (group && (!waiting || waiting->GetGroup() != group));
    if (group)
    {
        auto expected = job.members;
        if (waiting && waiting->GetGroup() == group)
            expected.insert(job.waiting);
        std::set<ObjectGuid> actual;
        for (auto const& member : group->GetMemberSlots())
            actual.insert(member.guid);
        changed = changed || expected != actual;
    }
    if (state.session != job.session || !Active(player) || !Leader(player) || changed)
    {
        if (waiting && waiting->GetGroupInvite() && waiting->GetGroupInvite()->GetLeaderGUID() == player->GetGUID())
            waiting->UninviteFromGroup();
        Error(player, job.request, "STOPPED");
        job.pending.clear();
        job.waiting.Clear();
        return;
    }
    if (job.waiting)
    {
        Player* bot = ObjectAccessor::FindPlayer(job.waiting);
        auto slot = job.pending.front().first;
        bool joined = bot && group && bot->GetGroup() == group;
        if (joined)
        {
            job.expectedGroup = group->GetGUID();
            job.members.insert(bot->GetGUID());
            Candidate current = Describe(bot, true);
            Send(player, job.request, "ROW", Row(slot, current));
            Send(player, job.request, "STATUS",
                 Acore::StringFormat("{}:{}", slot, Matches(state.filters[slot], current) ? "JOINED" : "ROLE_CHANGED"));
        }
        else if (bot && bot->GetGroupInvite() && bot->GetGroupInvite()->GetLeaderGUID() == player->GetGUID() &&
                 GetMSTimeDiffToNow(job.started) < InviteLife)
            return;
        else
        {
            if (bot && bot->GetGroupInvite() && bot->GetGroupInvite()->GetLeaderGUID() == player->GetGUID())
                bot->UninviteFromGroup();
            Send(player, job.request, "STATUS", Acore::StringFormat("{}:UNAVAILABLE", slot));
        }
        job.waiting.Clear();
        job.pending.pop_front();
    }
    if (job.pending.empty())
        return;
    auto [slot, candidate] = job.pending.front();
    Player* bot = ObjectAccessor::FindPlayer(Guid(candidate.guid));
    if (!Available(player, bot, state.options) || !Matches(state.filters[slot], Describe(bot)) ||
        (group && group->GetMembersCount() >= state.options.size))
    {
        Send(player, job.request, "STATUS", Acore::StringFormat("{}:UNAVAILABLE", slot));
        job.pending.pop_front();
        return;
    }
    if (group && state.options.size > 5 && !group->isRaidGroup())
        group->ConvertToRaid();
    WorldPacket invite;
    invite << bot->GetName() << uint32(0);
    player->GetSession()->HandleGroupInviteOpcode(invite);
    if (bot->GetGroupInvite() && bot->GetGroupInvite()->GetLeaderGUID() == player->GetGUID())
    {
        job.waiting = bot->GetGUID();
        job.started = getMSTime();
        Send(player, job.request, "STATUS", Acore::StringFormat("{}:INVITED", slot));
    }
    else
    {
        Send(player, job.request, "STATUS", Acore::StringFormat("{}:UNAVAILABLE", slot));
        job.pending.pop_front();
    }
}
class Request : public PlayerbotOperation
{
  public:
    Request(ObjectGuid owner, uint32 id, std::string text) : _owner(owner), _id(id), _text(std::move(text)) {}
    ~Request() override
    {
        std::lock_guard<std::mutex> guard(requestMutex);
        requests.erase(_owner);
    }
    std::string GetName() const override { return "GroupBuilder"; }
    uint32 GetPriority() const override { return 50; }
    bool Execute() override
    {
        Player* player = ObjectAccessor::FindPlayer(_owner);
        if (!player || !player->GetSession() || player->GetSession()->IsHeadless())
            return true;
        auto& state = Data(player);
        if (state.lastRequest && GetMSTimeDiffToNow(state.lastRequest) < 150)
        {
            Error(player, _id, "RATE");
            return true;
        }
        state.lastRequest = getMSTime();
        std::istringstream input(_text);
        std::string op, extra, batch;
        input >> op;
        if (op == "CANCEL" && !(input >> extra))
        {
            auto j = jobs.find(_owner);
            if (j != jobs.end())
            {
                Player* bot = ObjectAccessor::FindPlayer(j->second.waiting);
                if (bot && bot->GetGroupInvite() && bot->GetGroupInvite()->GetLeaderGUID() == _owner)
                    bot->UninviteFromGroup();
                jobs.erase(j);
            }
            state.nonce = 0;
            Send(player, _id, "CANCELLED");
            return true;
        }
        if (jobs.count(_owner))
        {
            Error(player, _id, "BUSY");
            return true;
        }
        if (op == "CONFIG")
        {
            Options o;
            if (!(input >> o.size >> o.minLevel >> o.maxLevel >> o.ilvl >> o.guild) || (input >> extra) ||
                (o.size != 5 && o.size != 10 && o.size != 25 && o.size != 40) || o.minLevel < 1 || o.maxLevel > 80 ||
                o.minLevel > o.maxLevel || o.ilvl > 1000 || o.guild > 1)
            {
                Error(player, _id, "REQUEST");
                return true;
            }
            state.options = o;
            state.filters.assign(o.size, Filter{});
            state.chosen.assign(o.size, Candidate{});
            state.nonce = 0;
            state.configured = true;
            Send(player, _id, "OK");
            return true;
        }
        if (!state.configured)
        {
            Error(player, _id, "PLAN_REQUIRED");
            return true;
        }
        if (op == "SLOTS")
        {
            if (!(input >> batch) || (input >> extra) || !ReadSlots(batch, state.options.size, state.filters))
                Error(player, _id, "REQUEST");
            else
            {
                state.nonce = 0;
                Send(player, _id, "OK");
            }
            return true;
        }
        if (op == "PLAN" && !(input >> extra))
        {
            Plan(player, _id, state);
            return true;
        }
        if (op == "ALTS")
        {
            unsigned slot = 0, page = 0;
            if (!(input >> slot >> page) || (input >> extra))
                Error(player, _id, "REQUEST");
            else
                Alternatives(player, _id, state, slot, page);
            return true;
        }
        if (op == "PICK")
        {
            unsigned slot = 0;
            uint32 id = 0;
            if (!(input >> slot >> id) || (input >> extra) || slot >= state.options.size || !state.nonce ||
                state.chosen[slot].member)
            {
                Error(player, _id, "REQUEST");
                return true;
            }
            auto filter = state.filters[slot];
            filter.pin = 0;
            Player* bot = ObjectAccessor::FindPlayer(Guid(id));
            bool used = std::any_of(state.chosen.begin(), state.chosen.end(),
                                    [&](auto const& c) { return c.guid == id && c.guid != state.chosen[slot].guid; });
            if (used || !Available(player, bot, state.options) || !Matches(filter, Describe(bot)))
            {
                Error(player, _id, "UNAVAILABLE");
                return true;
            }
            state.filters[slot].pin = id;
            state.chosen[slot] = Describe(bot);
            Show(player, _id, state);
            return true;
        }
        if (op == "INVITE")
        {
            uint32 nonce = 0;
            if (!(input >> nonce) || (input >> extra) || !state.nonce || nonce != state.nonce ||
                GetMSTimeDiffToNow(state.planned) > PlanLife || !Active(player) || !Leader(player))
            {
                Error(player, _id, "PLAN_EXPIRED");
                return true;
            }
            state.nonce = 0;
            if (!jobs.count(_owner) && jobs.size() >= 16)
            {
                Error(player, _id, "BUSY");
                return true;
            }
            Job job;
            job.session = state.session;
            job.request = _id;
            Group* group = player->GetGroup();
            if (group)
                job.expectedGroup = group->GetGUID();
            unsigned existing = 0;
            for (unsigned i = 0; i < state.chosen.size(); ++i)
            {
                auto const& c = state.chosen[i];
                Player* member = c.guid ? ObjectAccessor::FindPlayer(Guid(c.guid)) : nullptr;
                if (c.member)
                {
                    if (!member || (group ? member->GetGroup() != group : member != player))
                    {
                        Error(player, _id, "PLAN_EXPIRED");
                        return true;
                    }
                    ++existing;
                    job.members.insert(member->GetGUID());
                }
                else if (c.guid)
                    job.pending.emplace_back(i, c);
            }
            if (existing != (group ? group->GetMembersCount() : 1))
            {
                Error(player, _id, "PLAN_EXPIRED");
                return true;
            }
            if (job.pending.empty())
            {
                Send(player, _id, "FINISHED");
                return true;
            }
            jobs.emplace(_owner, std::move(job));
            Send(player, _id, "STARTED");
            return true;
        }
        Error(player, _id, "REQUEST");
        return true;
    }

  private:
    ObjectGuid _owner;
    uint32 _id;
    std::string _text;
};
class Commands : public CommandScript
{
  public:
    Commands() : CommandScript("group_builder_commands") {}
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        static Acore::ChatCommands::ChatCommandTable commands = {
            {"gbuilder", Handle, SEC_PLAYER, Acore::ChatCommands::Console::No}};
        return commands;
    }
    static bool Handle(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetPlayer();
        if (!player || player->GetSession()->IsHeadless() || !args || std::char_traits<char>::length(args) > 235)
            return true;
        std::istringstream input(args);
        std::string number, command;
        uint32 id = 0;
        if (!(input >> number) || !Number(number, id))
            return false;
        std::getline(input, command);
        {
            std::lock_guard<std::mutex> guard(requestMutex);
            if (requests.size() >= 128 || !requests.insert(player->GetGUID()).second)
            {
                Error(player, id, "BUSY");
                return true;
            }
        }
        if (!PlayerbotWorldThreadProcessor::instance().QueueOperation(
                std::make_unique<Request>(player->GetGUID(), id, command)))
            Error(player, id, "BUSY");
        return true;
    }
};
class Tick : public WorldScript
{
  public:
    Tick() : WorldScript("group_builder_invites") {}
    void OnUpdate(uint32 diff) override
    {
        _elapsed += diff;
        if (_elapsed < 1000)
            return;
        _elapsed = 0;
        for (auto i = jobs.begin(); i != jobs.end();)
        {
            Player* player = ObjectAccessor::FindPlayer(i->first);
            if (!player || !player->GetSession())
            {
                Player* bot = ObjectAccessor::FindPlayer(i->second.waiting);
                if (bot && bot->GetGroupInvite() && bot->GetGroupInvite()->GetLeaderGUID() == i->first)
                    bot->UninviteFromGroup();
                i = jobs.erase(i);
                continue;
            }
            Pump(player, i->second);
            if (i->second.pending.empty())
            {
                Send(player, i->second.request, "FINISHED");
                i = jobs.erase(i);
            }
            else
                ++i;
        }
    }

  private:
    uint32 _elapsed = 0;
};
} // namespace GroupBuilder
void Addmod_group_builderScripts()
{
    new GroupBuilder::Commands();
    new GroupBuilder::Tick();
}
