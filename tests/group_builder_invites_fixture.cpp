// SPDX-License-Identifier: GPL-3.0-or-later
#include "GroupBuilderPolicy.h"
#include <cassert>
#include <deque>
#include <map>
#include <string>
#include <tuple>
using uint32 = std::uint32_t;
struct ObjectGuid
{
    unsigned id = 0;
    explicit operator bool() const { return id != 0; }
    bool operator==(ObjectGuid const&) const = default;
    bool operator<(ObjectGuid const& other) const { return id < other.id; }
    void Clear() { id = 0; }
};
unsigned now = 1000;
unsigned getMSTime() { return now; }
unsigned GetMSTimeDiffToNow(unsigned t) { return now - t; }
struct Player;
struct Group
{
    ObjectGuid guid{99}, leader{1};
    unsigned count = 1;
    struct MemberSlot
    {
        ObjectGuid guid;
    };
    std::vector<MemberSlot> members = {{{1}}};
    auto const& GetMemberSlots() { return members; }
    bool raid = false;
    ObjectGuid GetGUID() { return guid; }
    ObjectGuid GetLeaderGUID() { return leader; }
    unsigned GetMembersCount() { return count; }
    bool isRaidGroup() { return raid; }
    void ConvertToRaid() { raid = true; }
};
Group party;
struct WorldPacket
{
    std::string name;
    WorldPacket& operator<<(std::string const& text)
    {
        name = text;
        return *this;
    }
    WorldPacket& operator<<(uint32) { return *this; }
};
struct Session
{
    unsigned invites = 0;
    void HandleGroupInviteOpcode(WorldPacket&);
};
struct Player
{
    ObjectGuid guid;
    std::string name;
    Group* group = nullptr;
    Group* invite = nullptr;
    bool active = true, leader = true, available = true;
    unsigned role = GroupBuilder::Damage;
    Session session;
    ObjectGuid GetGUID() { return guid; }
    std::string GetName() { return name; }
    Group* GetGroup() { return group; }
    Group* GetGroupInvite() { return invite; }
    Session* GetSession() { return &session; }
    void UninviteFromGroup() { invite = nullptr; }
};
std::map<unsigned, Player*> players;
namespace ObjectAccessor
{
Player* FindPlayer(ObjectGuid guid)
{
    auto i = players.find(guid.id);
    return i == players.end() ? nullptr : i->second;
}
} // namespace ObjectAccessor
void Session::HandleGroupInviteOpcode(WorldPacket& p)
{
    ++invites;
    for (auto [id, player] : players)
        if (player->name == p.name)
            player->invite = &party;
}
namespace Acore
{
template <class A, class B> std::string StringFormat(char const*, A a, B b)
{
    return std::to_string(a) + ":" + std::string(b);
}
template <class A> std::string StringFormat(char const*, A a) { return std::to_string(a) + ":UNAVAILABLE"; }
} // namespace Acore
namespace GroupBuilder
{
constexpr unsigned InviteLife = 20000;
struct State
{
    unsigned session = 1;
    Options options;
    std::vector<Filter> filters = std::vector<Filter>(5);
} state;
struct Job
{
    unsigned session = 1;
    uint32 request = 1, started = 0;
    ObjectGuid expectedGroup, waiting;
    std::set<ObjectGuid> members;
    std::deque<std::pair<unsigned, Candidate>> pending;
};
std::vector<std::tuple<std::string, std::string>> messages;
State& Data(Player*) { return state; }
ObjectGuid Guid(unsigned id) { return ObjectGuid{id}; }
bool Active(Player* p) { return p && p->active; }
bool Leader(Player* p) { return p->leader; }
bool Available(Player*, Player* b, Options const&) { return b && b->available && !b->group && !b->invite; }
Candidate Describe(Player* p, bool member = false)
{
    Candidate c;
    c.guid = p->guid.id;
    c.role = p->role;
    c.member = member;
    return c;
}
std::string Row(unsigned, Candidate const&) { return ""; }
void Send(Player*, unsigned, std::string_view kind, std::string_view payload = "")
{
    messages.emplace_back(kind, payload);
}
void Error(Player* p, unsigned r, std::string_view msg) { Send(p, r, "ERROR", msg); }
// PRODUCTION_PUMP
} // namespace GroupBuilder
int main()
{
    using namespace GroupBuilder;
    Player owner, bot;
    owner.guid = {1};
    owner.name = "Owner";
    bot.guid = {2};
    bot.name = "Bot";
    players = {{1, &owner}, {2, &bot}};
    state.options.size = 10;
    auto start = [&]()
    {
        Job j;
        j.members.insert(owner.guid);
        if (owner.group)
        {
            j.expectedGroup = owner.group->guid;
            for (auto const& slot : owner.group->members)
                j.members.insert(slot.guid);
        }
        j.pending.emplace_back(0, Describe(&bot));
        return j;
    };
    auto job = start();
    Pump(&owner, job);
    assert(owner.session.invites == 1 && job.waiting.id == 2 && bot.invite == &party);
    assert(!bot.group); // An invite is not forced membership or an automatic acceptance.
    Pump(&owner, job);
    assert(job.pending.size() == 1 && owner.session.invites == 1);
    bot.group = owner.group = &party;
    party.count = 2;
    party.members.push_back({{2}});
    bot.invite = nullptr;
    Pump(&owner, job);
    assert(job.pending.empty() && job.expectedGroup.id == 99);
    owner.group = nullptr;
    bot.group = nullptr;
    party.count = 1;
    party.members = {{{1}}};
    job = start();
    Pump(&owner, job);
    now += InviteLife + 1;
    Pump(&owner, job);
    assert(job.pending.empty() && bot.invite == nullptr); // Only the outstanding invite is cancelled.
    owner.group = &party;
    bot.available = false;
    job = start();
    auto count = owner.session.invites;
    Pump(&owner, job);
    assert(owner.session.invites == count);
    bot.available = true;
    job = start();
    state.filters[0].role = Healer;
    Pump(&owner, job);
    assert(owner.session.invites == count); // Role changed since proposal.
    state.filters[0].role = Damage;
    party.count = 10;
    job = start();
    Pump(&owner, job);
    assert(owner.session.invites == count);
    party.count = 2;
    job = start();
    Pump(&owner, job);
    assert(party.raid);
    job.expectedGroup = party.guid;
    owner.group = nullptr;
    Pump(&owner, job);
    assert(job.pending.empty() && !bot.invite); // Disbanding stops recruitment, never recreates a party.
    owner.group = &party;
    job = start();
    job.expectedGroup = {88};
    Pump(&owner, job);
    assert(job.pending.empty()); // Joining a different group cannot redirect queued invitations.
    owner.group = nullptr;
    bot.invite = nullptr;
    job = start();
    Pump(&owner, job);
    owner.active = false;
    Pump(&owner, job);
    assert(!bot.invite && job.pending.empty());
    owner.active = true;
    job = start();
    state.session = 2; // A fast logout/relogin must not replay an old recruitment job.
    Pump(&owner, job);
    assert(job.pending.empty() && !job.waiting);
}
