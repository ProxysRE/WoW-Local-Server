"""Exercise the actual module against a small fake server boundary.

These tests cover grant policy and inventory/mail routing. The Windows build
separately compiles against real pinned core headers and links worldserver.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "overlays/mod-starter-professions/src/StarterProfessions.cpp"

STUB = r'''
#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using uint32 = std::uint32_t;
constexpr int PLAYERHOOK_ON_FIRST_LOGIN = 1;
constexpr int CONFIG_MAX_PRIMARY_TRADE_SKILL = 1;
constexpr int NULL_BAG = 0, NULL_SLOT = 0, EQUIP_ERR_OK = 0;
constexpr int MAIL_NORMAL = 0, MAIL_STATIONERY_GM = 61;
template<class... T> void Log(T const&...) {}
#define LOG_ERROR(...) Log(__VA_ARGS__)
#define LOG_INFO(...) Log(__VA_ARGS__)
struct Config {
    bool enabled = true;
    template<class T> T GetOption(char const*, bool) { return enabled; }
} config;
inline auto* sConfigMgr = &config;
struct World { uint32 limit = 11; uint32 getIntConfig(int) { return limit; } } world;
inline auto* sWorld = &world;
struct Session { bool headless = false; bool IsHeadless() { return headless; } };
struct Transaction { int mailItems = 0, playerSaves = 0; };
using CharacterDatabaseTransaction = std::shared_ptr<Transaction>;
struct Database {
    int commits = 0, mailItems = 0, playerSaves = 0;
    CharacterDatabaseTransaction BeginTransaction() { return std::make_shared<Transaction>(); }
    void CommitTransaction(CharacterDatabaseTransaction t) {
        ++commits; mailItems += t->mailItems; playerSaves += t->playerSaves;
    }
} CharacterDatabase;
struct Player;
struct Item {
    uint32 entry;
    static Item* CreateItem(uint32 id, int, Player*) { return new Item{id}; }
    void SaveToDB(CharacterDatabaseTransaction) {}
};
using ItemPosCountVec = std::vector<int>;
struct Player {
    Session session;
    std::map<uint32, uint32> skills, inventory;
    std::vector<std::unique_ptr<Item>> owned;
    int capacity = 16, learned = 0;
    Session* GetSession() { return &session; }
    std::string GetName() { return "Tester"; }
    bool HasSkill(uint32 id) { return skills.contains(id); }
    void learnSpell(uint32 id);
    bool HasItemCount(uint32 id, int, bool) { return inventory[id] != 0; }
    int CanStoreNewItem(int, int, ItemPosCountVec&, uint32, int) { return capacity ? 0 : 1; }
    Item* StoreNewItem(ItemPosCountVec const&, uint32 id, bool) {
        --capacity; ++inventory[id]; owned.emplace_back(new Item{id}); return owned.back().get();
    }
    void SendNewItem(Item*, int, bool, bool) {}
    void SaveToDB(CharacterDatabaseTransaction t, bool, bool) { ++t->playerSaves; }
};
struct ChatHandler { explicit ChatHandler(Session*) {} void SendSysMessage(char const*) {} };
struct Spells {
    uint32 missing = 0;
    void* GetSpellInfo(uint32 id) { return id == missing ? nullptr : this; }
} spells;
inline auto* sSpellMgr = &spells;
struct Objects {
    uint32 missing = 0;
    void* GetItemTemplate(uint32 id) { return id == missing ? nullptr : this; }
} objects;
inline auto* sObjectMgr = &objects;
struct MailReceiver { explicit MailReceiver(Player*) {} };
struct MailSender { MailSender(int, int, int) {} };
struct MailDraft {
    std::vector<std::unique_ptr<Item>> items;
    MailDraft(char const*, char const*) {}
    void AddItem(Item* item) { items.emplace_back(item); }
    void SendMailTo(CharacterDatabaseTransaction t, MailReceiver, MailSender) { t->mailItems += items.size(); }
};
struct PlayerScript {
    PlayerScript(char const*, std::vector<int>) {}
    virtual ~PlayerScript() = default;
    virtual void OnPlayerFirstLogin(Player*) {}
};
'''

HARNESS = r'''
void Player::learnSpell(uint32 id) {
    for (auto const& p : Professions)
        if (p.apprenticeSpell == id) { skills[p.skill] = 1; ++learned; return; }
    assert(false);
}
void Reset() {
    config.enabled = true; world.limit = 11; spells.missing = objects.missing = 0;
    CharacterDatabase = Database{};
}
int main() {
    StarterProfessionsPlayer script;
    { Reset(); Player p; script.OnPlayerFirstLogin(&p);
      assert(p.skills.size() == 14 && p.learned == 14);
      for (auto id : Tools) assert(p.inventory[id] == 1);
      assert(CharacterDatabase.commits == 1 && CharacterDatabase.playerSaves == 1);
      script.OnPlayerFirstLogin(&p); // also safe if another script repeats the hook
      assert(p.learned == 14);
      for (auto id : Tools) assert(p.inventory[id] == 1); }
    { Reset(); Player p; p.session.headless = true; script.OnPlayerFirstLogin(&p);
      assert(p.skills.empty() && CharacterDatabase.commits == 0); }
    { Reset(); config.enabled = false; Player p; script.OnPlayerFirstLogin(&p);
      assert(p.skills.empty() && CharacterDatabase.commits == 0); }
    { Reset(); world.limit = 2; Player p; script.OnPlayerFirstLogin(&p);
      assert(p.skills.empty() && CharacterDatabase.commits == 0); }
    { Reset(); Player p; p.skills[171] = 450; p.inventory[2901] = 1;
      script.OnPlayerFirstLogin(&p);
      assert(p.skills[171] == 450 && p.learned == 13 && p.inventory[2901] == 1); }
    { Reset(); Player p; p.capacity = 0; script.OnPlayerFirstLogin(&p);
      assert(p.skills.size() == 14 && CharacterDatabase.mailItems == 4);
      assert(CharacterDatabase.playerSaves == 1); }
    { Reset(); Player p; p.capacity = 2; script.OnPlayerFirstLogin(&p);
      assert(CharacterDatabase.mailItems == 2 && p.capacity == 0); }
    { Reset(); spells.missing = 45357; Player p; script.OnPlayerFirstLogin(&p);
      assert(p.skills.empty() && CharacterDatabase.commits == 0); }
    { Reset(); objects.missing = 6256; Player p; script.OnPlayerFirstLogin(&p);
      assert(p.skills.empty() && CharacterDatabase.commits == 0); }
}
'''

with tempfile.TemporaryDirectory(prefix="starter-professions-") as directory:
    temp = Path(directory)
    (temp / "ServerStub.h").write_text(STUB)
    for name in ["Chat", "Config", "DatabaseEnv", "Item", "Log", "Mail", "ObjectMgr",
                 "Player", "ScriptMgr", "SpellMgr", "World", "WorldSession"]:
        (temp / f"{name}.h").write_text('#include "ServerStub.h"\n')
    test = temp / "test.cpp"
    test.write_text(f'#include "{SOURCE.as_posix()}"\n' + HARNESS)
    binary = temp / "test"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    f"-I{temp}", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Starter professions: 9 behavior scenarios passed")
