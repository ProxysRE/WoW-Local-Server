"""Exercise the patched production storage methods against an instrumented Player.

The fixture records every progression-quest mutation. It also uses the real core
DataMap rather than replacing session storage with a test-only implementation.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
IP_REV = "e1795553d381e561ab5f37585566c22e23cdff73"
CORE_REV = "e7e2e288e7d9ec30e2dfe6f417178a95986fcbb2"


def function(source, name):
    start = source.index(name)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def download(url, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url, timeout=60) as response:
        target.write_bytes(response.read())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--data-map", type=Path)
    parser.add_argument("--core-quests", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        if args.source:
            source = (args.source / "IndividualProgression.cpp").read_text()
            shutil.copy2(args.data_map, work / "DataMap.h")
            quest_source = args.core_quests.read_text()
        else:
            # Fetch exactly the pinned sources touched by the patch, not moving master.
            for filename in ("IndividualProgression.cpp", "IndividualProgression.h",
                             "IndividualProgressionPlayer.cpp", "cs_individualProgression.cpp"):
                download(f"https://raw.githubusercontent.com/ZhengPeiRu21/mod-individual-progression/{IP_REV}/src/{filename}",
                         work / "src" / filename)
            subprocess.run(["git", "apply", str(ROOT / "patches/temporary-progression.patch")], cwd=work, check=True)
            source = (work / "src/IndividualProgression.cpp").read_text()
            quest_path = "src/server/game/Entities/Player/PlayerQuest.cpp"
            download(f"https://raw.githubusercontent.com/mod-playerbots/azerothcore-wotlk/{CORE_REV}/{quest_path}", work / quest_path)
            subprocess.run(["git", "apply", str(ROOT / "patches/temporary-quest-rewards.patch")], cwd=work, check=True)
            quest_source = (work / quest_path).read_text()
            download(f"https://raw.githubusercontent.com/mod-playerbots/azerothcore-wotlk/{CORE_REV}/src/common/Utilities/DataMap.h",
                     work / "DataMap.h")

        bodies = [function(source, name) for name in (
            "bool IndividualProgression::HasTemporaryProgression",
            "uint8 IndividualProgression::GetPlayerProgressionFromQuests",
            "bool IndividualProgression::SetTemporaryProgression",
            "void IndividualProgression::ClearTemporaryProgression",
            "uint8 IndividualProgression::GetStoredProgression",
            "void IndividualProgression::UpdateProgressionState",
            "void IndividualProgression::ForceUpdateProgressionState",
        )]
        bodies.extend(function(quest_source, name) for name in (
            "bool Player::GetQuestRewardStatus", "QuestStatus Player::GetQuestStatus"))
        state = source[source.index("namespace\n{"):source.index("IndividualProgression* IndividualProgression::instance()")]
        harness = r'''
#include "DataMap.h"
#include "TemporaryProgression.h"
#include "TemporaryQuestRewards.h"
#include <map>
#include <cassert>
#include <set>
#include <cstdint>
using uint8 = std::uint8_t;
using uint32 = std::uint32_t;
using ProgressionState = uint8;
constexpr uint8 PROGRESSION_MOLTEN_CORE = 1, PROGRESSION_WOTLK_TIER_5 = 18;
using QuestStatus = int;
constexpr int QUEST_STATUS_REWARDED = 1, QUEST_STATUS_NONE = 0;
struct Quest {
    uint32 id;
    bool IsSeasonal() const { return false; }
    bool IsRepeatable() const { return false; }
};
struct StatusRecord { int Status = 0; };
using QuestStatusMap = std::map<uint32, StatusRecord>;
struct ObjectMgr {
    Quest quest{};
    Quest const* GetQuestTemplate(uint32 id) { quest.id = id; return &quest; }
} objectMgr;
auto* sObjectMgr = &objectMgr;
struct Player {
    DataMap CustomData;
    bool inWorld = true;
    std::set<uint32> rewarded;
    int writes = 0, refreshes = 0;
    bool IsInWorld() const { return inWorld; }
    QuestStatusMap m_QuestStatus;
    int GetQuestStatus(uint32 id) const;
    bool GetQuestRewardStatus(uint32 id) const;
    bool IsQuestRewarded(uint32 id) const { return rewarded.count(id); }
    bool SatisfyQuestSeasonal(Quest const*, bool) const { return true; }
    void AddQuest(Quest const*, void*) { ++writes; }
    void CompleteQuest(uint32) { ++writes; }
    void RewardQuest(Quest const* q, int, Player*, bool, bool) { ++writes; rewarded.insert(q->id); }
    void RemoveRewardedQuest(uint32 id) { ++writes; rewarded.erase(id); }
};
struct IndividualProgression {
    bool enabled = true;
    int progressionLimit = 0;
    static bool HasTemporaryProgression(Player const*);
    uint8 GetStoredProgression(Player*) const;
    uint8 GetPlayerProgressionFromQuests(Player*) const;
    bool SetTemporaryProgression(Player*, uint32);
    void ClearTemporaryProgression(Player*);
    void RefreshTemporaryProgression(Player* p) { ++p->refreshes; }
    void UpdateProgressionState(Player*, ProgressionState) const;
    static void ForceUpdateProgressionState(Player*, ProgressionState);
} progression;
auto* sIndividualProgression = &progression;
'''
        checks = r'''
int main() {
    Player veteran, other;
    for (uint32 i = 1; i <= 18; ++i) veteran.rewarded.insert(66000 + i);
    for (uint32 i = 1; i <= 8; ++i) other.rewarded.insert(66000 + i);
    veteran.rewarded.insert(6403); // A real story quest must never be reset by the view.
    auto original = veteran.rewarded;
    assert(progression.GetPlayerProgressionFromQuests(&veteran) == 18);
    assert(progression.SetTemporaryProgression(&veteran, 6));
    assert(progression.GetPlayerProgressionFromQuests(&veteran) == 6);
    assert(progression.GetStoredProgression(&veteran) == 18);
    assert(veteran.GetQuestRewardStatus(66006));
    assert(!veteran.GetQuestRewardStatus(66007));
    assert(veteran.GetQuestStatus(66007) == QUEST_STATUS_NONE);
    assert(veteran.GetQuestStatus(66006) == QUEST_STATUS_REWARDED);
    assert(veteran.IsQuestRewarded(66018)); // The actual saved container stays intact.
    assert(veteran.GetQuestRewardStatus(6403));
    assert(progression.GetPlayerProgressionFromQuests(&other) == 8);
    progression.UpdateProgressionState(&veteran, 7); // Completing a visit milestone.
    assert(progression.GetPlayerProgressionFromQuests(&veteran) == 7);
    assert(veteran.GetQuestRewardStatus(66007));
    assert(!veteran.GetQuestRewardStatus(66008));
    assert(veteran.rewarded == original && veteran.writes == 0);
    progression.ForceUpdateProgressionState(&veteran, 8);
    assert(veteran.rewarded == original && veteran.writes == 0);
    assert(!progression.SetTemporaryProgression(&veteran, 11));
    assert(progression.GetPlayerProgressionFromQuests(&veteran) == 7);
    assert(progression.SetTemporaryProgression(&veteran, 0));
    assert(progression.GetPlayerProgressionFromQuests(&veteran) == 0);
    assert(!veteran.GetQuestRewardStatus(66001));
    assert(veteran.GetQuestRewardStatus(6403));
    progression.ClearTemporaryProgression(&veteran);
    assert(progression.GetPlayerProgressionFromQuests(&veteran) == 18);
    assert(veteran.rewarded == original && veteran.writes == 0);
    assert(!progression.HasTemporaryProgression(&veteran));
    assert(veteran.GetQuestRewardStatus(66018));
    assert(veteran.GetQuestStatus(66018) == QUEST_STATUS_REWARDED);
    assert(!progression.SetTemporaryProgression(&other, 13));
    assert(!progression.HasTemporaryProgression(&other));
    progression.UpdateProgressionState(&other, 9); // Normal progression still persists.
    assert(other.writes > 0 && progression.GetStoredProgression(&other) == 9);
    assert(veteran.refreshes >= 4);
}
'''
        cpp = work / "integration.cpp"
        cpp.write_text(harness + state + "\n".join(bodies) + checks)
        executable = work / "integration"
        subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        f"-I{work}", f"-I{ROOT / 'overlays/mod-individual-progression/src'}",
                        f"-I{ROOT / 'overlays/core/src/server/game/Entities/Player'}",
                        str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
        print("Production temporary progression: storage isolation, stage 0, transitions and restoration passed")


if __name__ == "__main__":
    main()
