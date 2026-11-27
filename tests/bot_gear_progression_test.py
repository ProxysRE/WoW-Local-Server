"""Compile the actual stage module and exercise startup rerolls and protection."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
fixture = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>
using uint32=uint32_t;using uint16=uint16_t;using uint8=uint8_t;
enum {ITEM_CLASS_WEAPON=2, ITEM_CLASS_ARMOR=4, INVTYPE_BODY=4, INVTYPE_TABARD=19,
      ITEM_QUALITY_EPIC=4, EQUIPMENT_SLOT_START=0, EQUIPMENT_SLOT_END=19,
      INVENTORY_SLOT_BAG_0=0, PLAYERHOOK_ON_LOGIN, PLAYERHOOK_CAN_EQUIP_ITEM};
struct ObjectGuid {uint32 id=0;uint32 GetCounter() const{return id;}};
struct ItemTemplate {uint32 ItemId=28770,Class=4,InventoryType=1;};
struct Item {ItemTemplate item;ItemTemplate const* GetTemplate(){return &item;}};
struct WorldSession {bool headless=true;uint32 account=12;bool IsHeadless(){return headless;}uint32 GetAccountId(){return account;}};
struct Player
{
    ObjectGuid guid{1};WorldSession session;uint32 guild=0;bool ai=true,combat=false,teleport=false;
    bool world=true,alive=true,bg=false,queue=false,flight=false;int saves=0;
    std::map<int,Item> items;
    WorldSession* GetSession(){return &session;}
    uint32 GetLevel(){return 70;}
    uint32 GetGuildId(){return guild;} ObjectGuid GetGUID(){return guid;}
    std::string GetName(){return "Fixture";}
    bool IsInWorld(){return world;}bool IsAlive(){return alive;}
    bool IsInCombat(){return combat;}bool IsBeingTeleported(){return teleport;}
    bool InBattleground(){return bg;}bool InBattlegroundQueue(){return queue;}bool IsInFlight(){return flight;}
    Item* GetItemByPos(int,int slot){auto it=items.find(slot);return it==items.end()?nullptr:&it->second;}
    void DestroyItem(int,int slot,bool){items.erase(slot);}
    void SaveToDB(bool,bool){++saves;}
};
struct AIConfig {std::vector<uint32> RandomBotAccounts{12}; bool IsInRandomAccountList(uint32 account){return account==12;}};
AIConfig sPlayerbotAIConfig;
struct PlayerbotGuildMgr
{
    static PlayerbotGuildMgr& instance(){static PlayerbotGuildMgr mgr;return mgr;}
    bool IsRealGuild(uint32 id){return id==100;} // 100 has a player leader; 200 has a random-bot leader.
};
struct Field {uint32 data;template<class T>T Get(){return static_cast<T>(data);}};
struct Result {Field fields[2];Field* Fetch(){return fields;}bool NextRow(){return false;}};
using QueryResult=std::shared_ptr<Result>;
struct DB {QueryResult Query(char const*){return {};}} CharacterDatabase;
struct Config
{
    template<class T>T GetOption(char const*,T fallback){return fallback;}
} configMgr;
Config* sConfigMgr=&configMgr;
namespace ObjectAccessor
{
std::map<uint32,Player*> players;
Player* FindConnectedPlayer(ObjectGuid id){auto it=players.find(id.id);return it==players.end()?nullptr:it->second;}
}
#define GET_PLAYERBOT_AI(player) ((player)->ai)
struct WorldScript
{
    WorldScript(char const*){} virtual void OnAfterConfigLoad(bool){}
    virtual void OnStartup(){} virtual void OnUpdate(uint32){}
};
struct PlayerScript
{
    PlayerScript(char const*,std::initializer_list<int>){} virtual void OnPlayerLogin(Player*){}
    virtual bool OnPlayerCanEquipItem(Player*,uint8,uint16&,Item*,bool,bool){return true;}
};
struct PlayerbotFactory
{
    static int rerolls,destroys;
    static void DestroyEquippedGear(Player* player){++destroys;player->items.clear();}
    static void AutoGear(Player*,uint32,uint32,bool,bool,bool);
};
int PlayerbotFactory::rerolls=0;int PlayerbotFactory::destroys=0;
template<class... Args>void TestLog(Args const&...){}
#define LOG_INFO(...) TestLog(__VA_ARGS__)
#define LOG_ERROR(...) TestLog(__VA_ARGS__)
'''
tests = r'''
void PlayerbotFactory::AutoGear(Player* player,uint32,uint32,bool incremental,bool second,bool finish)
{
    assert(!incremental && !second && finish);++rerolls;
    ItemTemplate early{28770,4,1};ItemTemplate late{34331,4,1};
    assert(BotGearStageAllows(player,&early) && !BotGearStageAllows(player,&late));
    player->items[0]=Item{early};
}
int main()
{
    assert(BotGear::StageFor(28770)==8); // Karazhan
    assert(BotGear::StageFor(29011)==8); // T4 set, expanded from Gruul's token
    assert(BotGear::StageFor(30106)==9); // SSC, even though its ilvl could overlap a manual cap
    assert(BotGear::StageFor(32471)==10); // Black Temple
    assert(BotGear::StageFor(34331)==12); // Sunwell
    assert(BotGear::StageFor(40343)==13); // Naxxramas
    assert(BotGear::StageFor(50737)==16); // ICC
    assert(BotGear::StageFor(54590)==17); // Ruby Sanctum
    assert(BotGear::StageFor(21869)>=8); // TBC craft must never leak into vanilla
    assert(!BotGear::Allowed(8,9) && BotGear::Allowed(8,8) && !BotGear::Allowed(8,-1));
    for (std::size_t i=1;i<std::size(BotGear::Catalog);++i)
        assert(BotGear::Catalog[i-1].item<BotGear::Catalog[i].item);
    BotGearProgressionWorld world;BotGearProgressionPlayer hooks;
    world.OnAfterConfigLoad(false);world.OnStartup();
    Player bot;ObjectAccessor::players[1]=&bot;
    ItemTemplate unknown{99999,4,1},late{34331,4,1};
    assert(!BotGearStageAllows(&bot,&unknown) && !BotGearStageAllows(&bot,&late));
    bot.items[0]=Item{late};hooks.OnPlayerLogin(&bot);world.OnUpdate(1000);
    assert(PlayerbotFactory::rerolls==1 && PlayerbotFactory::destroys==1 && bot.saves==1);
    assert(bot.items.at(0).item.ItemId==28770);
    hooks.OnPlayerLogin(&bot);world.OnUpdate(1000);assert(PlayerbotFactory::rerolls==1);

    Player friendBot;friendBot.guid.id=2;ObjectAccessor::players[2]=&friendBot;
    friendBot.items[0]=Item{late};friendOwners[2]={42};
    hooks.OnPlayerLogin(&friendBot);world.OnUpdate(1000);
    assert(friendBot.items.at(0).item.ItemId==34331 && PlayerbotFactory::rerolls==1);
    assert(BotGearStageAllows(&friendBot,&late));
    Player guildBot;guildBot.guid.id=3;guildBot.guild=100;ObjectAccessor::players[3]=&guildBot;
    hooks.OnPlayerLogin(&guildBot);world.OnUpdate(1000);assert(PlayerbotFactory::rerolls==1);
    Player autoGuild;autoGuild.guid.id=4;autoGuild.guild=200;ObjectAccessor::players[4]=&autoGuild;
    friendOwners[4]={12}; // A bot's friend list must not protect another random bot.
    hooks.OnPlayerLogin(&autoGuild);world.OnUpdate(1000);assert(PlayerbotFactory::rerolls==2);

    Player busy;busy.guid.id=5;busy.combat=true;ObjectAccessor::players[5]=&busy;
    hooks.OnPlayerLogin(&busy);world.OnUpdate(1000);assert(PlayerbotFactory::rerolls==2);
    busy.combat=false;world.OnUpdate(1000);assert(PlayerbotFactory::rerolls==3);
    Player human;human.session.headless=false;assert(BotGearStageAllows(&human,&late));
    Player ownBot;ownBot.session.account=42;assert(BotGearStageAllows(&ownBot,&late));
    uint16 dest=0;Item saved{late};
    assert(hooks.OnPlayerCanEquipItem(&bot,0,dest,&saved,false,false)); // loading must not strip saved gear
    assert(!hooks.OnPlayerCanEquipItem(&bot,0,dest,&saved,false,true));
    auto updated=std::make_shared<Settings>();updated->overrides[99999]=8;
    settings.store(updated);
    assert(BotGearStageAllows(&bot,&unknown));
    updated=std::make_shared<Settings>();updated->stage=-1;
    settings.store(updated);
    assert(BotGearStageAllows(&bot,&late));
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / "fixture.h").write_text(fixture)
    for header in ["Config.h", "DatabaseEnv.h", "Item.h", "Log.h", "ObjectAccessor.h", "Player.h",
                   "PlayerbotAI.h", "PlayerbotAIConfig.h", "PlayerbotFactory.h", "PlayerbotGuildMgr.h",
                   "ScriptMgr.h", "WorldSession.h", "ItemTemplate.h", "QueryResult.h"]:
        (tmp / header).write_text('#include "fixture.h"\n')
    source = ROOT / "overlays/mod-bot-gear-progression/src/BotGearProgression.cpp"
    (tmp / "test.cpp").write_text(f'#include "{source}"\n'+tests)
    binary = tmp / "gear-stage-test"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I"+str(tmp),
                    str(tmp / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Gear stages: source boundaries, sets, unknowns, reroll-once, friends, player/bot guilds, busy bots passed")
