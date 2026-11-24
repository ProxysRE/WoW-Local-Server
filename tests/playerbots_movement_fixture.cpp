// Instrumented dependencies for the production methods extracted by the Python test.
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

using uint32 = std::uint32_t;
using int32 = std::int32_t;
constexpr uint32 IN_MILLISECONDS = 1000;
constexpr int FLIGHT_MOTION_TYPE = 1;
constexpr int FORM_TRAVEL = 1, FORM_FLIGHT = 2, FORM_FLIGHT_EPIC = 3;
constexpr int UNIT_FLAG_TAXI_FLIGHT = 1;
constexpr int MOVEMENTFLAG_FLYING = 1, MOVEMENTFLAG_DISABLE_GRAVITY = 2, MOVEMENTFLAG_CAN_FLY = 4;
constexpr int MOVEMENTFLAG_FALLING = 8, MOVEMENTFLAG_FALLING_FAR = 16;
constexpr int TELE_TO_NOT_LEAVE_TAXI = 1;
uint32 now = 100;
uint32 getMSTime() { return now; }
uint32 GetMSTimeDiffToNow(uint32 start) { return now - start; }
int warnings = 0;
#define LOG_WARN(...) (++warnings)

struct Position { void Relocate(float, float, float) {} };
struct MovementInfo { Position pos; };
struct TaxiPathNodeEntry { uint32 mapid; float x = 1, y = 2, z = 50; };
struct Player;
struct FlightPathMovementGenerator {
    std::vector<TaxiPathNodeEntry const*> path;
    uint32 current = 0;
    int resets = 0;
    auto const& GetPath() { return path; }
    uint32 GetCurrentNode() { return current; }
    uint32 GetPathAtMapEnd() {
        if (current >= path.size()) return path.size();
        for (uint32 i = current; i < path.size(); ++i)
            if (path[i]->mapid != path[current]->mapid) return i;
        return path.size();
    }
    void SetCurrentNodeAfterTeleport() { current = GetPathAtMapEnd(); }
    void SkipCurrentNode() { ++current; }
    void Reset(Player*);
};
struct MotionMaster {
    int type = 0, falls = 0;
    FlightPathMovementGenerator flight;
    int GetCurrentMovementGeneratorType() { return type; }
    auto* top() { return &flight; }
    void MoveFall() { ++falls; }
};
struct Spline { bool done = false; bool Finalized() { return done; } };
struct Player {
    bool casting = false, inFlight = false, inWorld = true, teleporting = false;
    bool taxiFlag = false, self = false, lootOpen = false, flyAura = false;
    int flags = 0, movementUpdates = 0, fallDamageCalls = 0, teleports = 0;
    uint32 map = 0;
    MotionMaster motion;
    Spline spline;
    Spline* movespline = &spline;
    MovementInfo m_movementInfo;
    bool IsNonMeleeSpellCast(bool) { return casting; }
    bool GetLootGUID() { return lootOpen; }
    bool IsInFlight() { return inFlight; }
    bool IsInWorld() { return inWorld; }
    bool IsBeingTeleported() { return teleporting; }
    bool HasUnitFlag(int) { return taxiFlag; }
    auto* GetMotionMaster() { return &motion; }
    uint32 GetMapId() { return map; }
    std::string GetName() { return "testbot"; }
    float GetOrientation() { return 0; }
    float GetPositionX() { return 1; }
    float GetPositionY() { return 2; }
    float GetPositionZ() { return 50; }
    void UpdateAllowedPositionZ(float, float, float& z) { z = 0; }
    void SetFallInformation(uint32, float) {}
    void HandleFall(MovementInfo const&) { ++fallDamageCalls; }
    void RemoveUnitMovementFlag(int mask) { flags &= ~mask; }
    bool HasUnitMovementFlag(int mask) { return flags & mask; }
    bool HasIncreaseMountedFlightSpeedAura() { return flyAura; }
    bool HasFlyAura() { return flyAura; }
    bool IsRooted() { return false; }
    void SendMovementFlagUpdate() { ++movementUpdates; }
    void TeleportTo(uint32 dest, float, float, float, float, int) { map = dest; ++teleports; }
};
void FlightPathMovementGenerator::Reset(Player* bot) { ++resets; bot->spline.done = false; }
bool IsSelfBot(Player* bot) { return bot->self; }

struct LootObject {
    int guid = 1;
    bool possible = true, near = true;
    bool IsEmpty() { return !guid; }
    bool IsLootPossible(Player*) { return possible; }
    bool IsAtInteractDistance(Player*) { return near; }
};
struct LootObjectStack {
    bool pending = false, ready = true;
    bool IsLootPending() { return pending; }
    bool CanAttemptLoot(int) { return ready; }
} lootStack;
LootObject lootTarget;
template <class T> T Value() {
    if constexpr (std::is_same_v<T, LootObject>) return lootTarget;
    else return &lootStack;
}
#define AI_VALUE(T, name) Value<T>()
#define AI_VALUE2(T, name, qualifier) T{}
struct Item {};
struct MountData { std::map<uint32, std::map<int32, std::vector<uint32>>> allSpells; };
MountData CollectMountData(Player*) { return {}; }
struct MountAI {
    int changedForms = 0;
    void RemoveShapeshift() { ++changedForms; }
    void RemoveAura(char const*) {}
};
struct CheckMountStateAction {
    Player* bot;
    Player* master = nullptr;
    MountAI ai{};
    MountAI* botAI = &ai;
    int botInShapeshiftForm = 0, mounts = 0;
    bool Mount();
    bool IsLootInteractionActive();
    void ClearStaleFlightFlags();
    static void CompleteDismount(Player*);
    bool TryPreferredMount(Player*) { ++mounts; return true; }
    int GetMountType(Player*) { return 0; }
    int CalculateMasterMountSpeed(Player*) { return 0; }
    bool TryForms(Player*, int, int) { return false; }
    bool TryRandomMountFiltered(std::map<int32, std::vector<uint32>> const&, int) { return false; }
    bool UseItemAuto(Item*) { return false; }
};
struct NewRpgInfo {
    struct TravelFlight { bool inFlight = false; };
    std::variant<std::monostate, TravelFlight> data;
};
struct PlayerbotAI {
    Player* bot;
    NewRpgInfo rpgInfo;
    uint32 _taxiSplineStoppedAt = 0;
    bool UpdateTaxiFlight();
};

// PRODUCTION_METHODS

int main() {
    Player bot;
    CheckMountStateAction mount{&bot};
    // Reproduce the dismount -> next tick remount race at a live node.
    assert(!mount.Mount() && mount.mounts == 0 && mount.ai.changedForms == 0);
    lootTarget.near = false;
    assert(mount.Mount()); // Travel toward distant resources still uses mounts.
    lootTarget.near = true;
    lootTarget.possible = false;
    assert(mount.Mount()); // A vanished/unharvestable node must not trap the bot.
    lootTarget.possible = true;
    lootStack.ready = false;
    assert(mount.Mount()); // Deferred loot permits other activity.
    lootStack.ready = true;
    lootTarget.guid = 0;
    lootStack.pending = true;
    assert(!mount.Mount());
    lootStack.pending = false;
    bot.lootOpen = true;
    assert(!mount.Mount());
    bot.lootOpen = false;
    bot.casting = true;
    assert(!mount.Mount());
    bot.casting = false;
    assert(mount.Mount()); // Normal mount resumes once interaction ends.

    PlayerbotAI ai{&bot, {}, 0};
    assert(!ai.UpdateTaxiFlight());
    TaxiPathNodeEntry a{0}, b{0}, c{0}, d{1}, e{1};
    bot.motion.flight.path = {&a, &b, &c};
    bot.inFlight = true;
    bot.motion.type = FLIGHT_MOTION_TYPE;
    ai.rpgInfo.data = NewRpgInfo::TravelFlight{};
    assert(ai.UpdateTaxiFlight());
    assert(std::get<NewRpgInfo::TravelFlight>(ai.rpgInfo.data).inFlight);
    assert(!bot.motion.flight.resets && !bot.teleports);
    bot.spline.done = true;
    assert(ai.UpdateTaxiFlight());
    now += 9999;
    ai.UpdateTaxiFlight();
    assert(!bot.motion.flight.resets);
    ++now;
    ai.UpdateTaxiFlight();
    assert(bot.motion.flight.resets == 1 && !bot.spline.done && !bot.teleports);
    ai.UpdateTaxiFlight();
    assert(bot.motion.flight.resets == 1);

    // A stopped spline on a cross-map route is not automatically a map boundary.
    bot.motion.flight.path = {&a, &b, &c, &d, &e};
    bot.motion.flight.current = 0;
    bot.spline.done = true;
    ai.UpdateTaxiFlight();
    assert(!bot.teleports);
    bot.motion.flight.current = 2;
    ai.UpdateTaxiFlight();
    assert(bot.teleports == 1 && bot.map == 1 && bot.motion.flight.current == 4);
    assert(ai._taxiSplineStoppedAt == 0);
    bot.motion.flight.current = bot.motion.flight.path.size();
    assert(ai.UpdateTaxiFlight()); // Invalid/end index never dereferences path.

    bot.self = true;
    bot.motion.flight.current = 0;
    bot.map = 0;
    ai.UpdateTaxiFlight();
    assert(bot.teleports == 1 && bot.motion.flight.resets == 1);
    bot.self = false;
    bot.inFlight = false;
    bot.motion.type = 0;
    assert(!ai.UpdateTaxiFlight()); // Normal actions resume after landing.

    CheckMountStateAction::CompleteDismount(nullptr);
    bot.inFlight = true;
    CheckMountStateAction::CompleteDismount(&bot);
    bot.inFlight = false;
    bot.motion.type = FLIGHT_MOTION_TYPE;
    CheckMountStateAction::CompleteDismount(&bot);
    bot.motion.type = 0;
    bot.taxiFlag = true;
    CheckMountStateAction::CompleteDismount(&bot);
    bot.taxiFlag = false;
    bot.teleporting = true;
    CheckMountStateAction::CompleteDismount(&bot);
    assert(!bot.motion.falls && !bot.fallDamageCalls);
    bot.teleporting = false;
    CheckMountStateAction::CompleteDismount(&bot);
    assert(bot.motion.falls == 1 && bot.fallDamageCalls == 1);

    bot.flags = MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_CAN_FLY;
    bot.inFlight = true;
    mount.ClearStaleFlightFlags();
    assert(bot.flags && !bot.movementUpdates);
    bot.inFlight = false;
    bot.motion.type = FLIGHT_MOTION_TYPE;
    mount.ClearStaleFlightFlags();
    assert(bot.flags && !bot.movementUpdates);
    bot.motion.type = 0;
    mount.ClearStaleFlightFlags();
    assert(!bot.flags && bot.movementUpdates == 1);
}
