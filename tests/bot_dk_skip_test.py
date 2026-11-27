"""Exercise the real skip module against saved and newly created bot state."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
fixture = r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
using uint32 = uint32_t;
using uint8 = uint8_t;
enum {CLASS_DEATH_KNIGHT=6, TEAM_ALLIANCE=0, PLAYER_XP=0, CONFIG_MAX_PLAYER_LEVEL=0};
enum {RACE_HUMAN=1, RACE_ORC, RACE_DWARF, RACE_NIGHTELF, RACE_UNDEAD_PLAYER,
      RACE_TAUREN, RACE_GNOME, RACE_TROLL, RACE_BLOODELF=10, RACE_DRAENEI};
enum {QUEST_STATUS_NONE, QUEST_STATUS_COMPLETE, QUEST_STATUS_INCOMPLETE};
enum {PLAYERHOOK_ON_LOGIN, PLAYER_FLAGS_NO_XP_GAIN};
struct WorldLocation
{
    uint32 map;
    WorldLocation(uint32 m,float,float,float,float):map(m){}
};
struct WorldSession
{
    bool headless=true;
    bool IsHeadless(){return headless;}
    uint32 GetAccountId(){return 12;}
};
struct Quest {uint32 id;};
struct ObjectMgr
{
    std::map<uint32,Quest> quests;
    uint32 missing=0;
    Quest const* GetQuestTemplate(uint32 id)
    {
        if (id==missing) return nullptr;
        quests[id]={id}; return &quests[id];
    }
} objects;
ObjectMgr* sObjectMgr=&objects;
struct Config
{
    template<class T> T GetOption(char const*,T def){return def;}
} config;
Config* sConfigMgr=&config;
struct World {uint32 getIntConfig(int){return 80;}} world;
World* sWorld=&world;
struct AIConfig {bool random=true; bool IsInRandomAccountList(uint32){return random;}};
AIConfig sPlayerbotAIConfig;
struct Player
{
    WorldSession session;
    uint32 cls=6,map=609,race=1,team=0,level=55,xp=123;
    bool alive=true,combat=false,teleporting=false,hearthstone=false,removedWhisper=false;
    std::map<uint32,int> statuses;
    std::set<uint32> rewarded;
    int rewards=0,teleports=0,saves=0,talents=0;
    uint32 homeMap=609;
    bool xpLocked=false;
    bool HasPlayerFlag(int){return xpLocked;}
    void SetPlayerFlag(int){xpLocked=true;}
    void RemovePlayerFlag(int){xpLocked=false;}
    uint32 getClass(){return cls;}
    uint32 GetMapId(){return map;}
    uint8 getRace(){return race;}
    uint32 GetTeamId(){return team;}
    WorldSession* GetSession(){return &session;}
    bool IsAlive(){return alive;}
    bool IsInCombat(){return combat;}
    bool IsBeingTeleported(){return teleporting;}
    std::string GetName(){return "Fixture";}
    uint32 GetLevel(){return level;}
    uint32 GetUInt32Value(int){return xp;}
    void SetUInt32Value(int,uint32 value){xp=value;}
    bool GetQuestRewardStatus(uint32 id){return rewarded.contains(id);}
    int GetQuestStatus(uint32 id){return statuses[id];}
    void AddQuest(Quest const* q,void*){statuses[q->id]=QUEST_STATUS_INCOMPLETE;}
    void SetQuestStatus(uint32 id,int status){statuses[id]=status;}
    void RewardQuest(Quest const* q,int,Player*,bool)
    {
        assert(statuses[q->id]==QUEST_STATUS_COMPLETE);
        assert(!rewarded.contains(q->id));
        rewarded.insert(q->id); ++rewards; assert(xpLocked);
    }
    void GiveLevel(uint32 value){level=value;}
    void InitTalentForLevel(){++talents;}
    void RemoveAurasDueToSpell(uint32 id){assert(id==58207);removedWhisper=true;}
    bool HasItemCount(uint32,uint32,bool){return hearthstone;}
    void AddItem(uint32,uint32){hearthstone=true;}
    void SetHomebind(WorldLocation const& location,uint32){homeMap=location.map;}
    bool TeleportTo(WorldLocation const& location){map=location.map;++teleports;return true;}
    void SaveToDB(bool,bool){++saves;}
};
struct WorldScript
{
    WorldScript(char const*){}
    virtual void OnAfterConfigLoad(bool){}
};
struct PlayerScript
{
    PlayerScript(char const*,std::initializer_list<int>){}
    virtual void OnPlayerLogin(Player*){}
};
#define LOG_ERROR(...) TestLog(__VA_ARGS__)
#define LOG_INFO(...) TestLog(__VA_ARGS__)
template<class... Args> void TestLog(Args const&...) {}
'''
tests = r'''
int main()
{
    Player fresh;
    SkipIntroduction(&fresh,true);
    assert(fresh.level==58 && fresh.xp==0 && fresh.rewards==33);
    assert(fresh.rewarded.contains(12742) && fresh.rewarded.contains(13188));
    assert(!fresh.xpLocked);
    assert(fresh.homeMap==0 && fresh.map==0 && fresh.teleports==1 && fresh.hearthstone);
    SkipIntroduction(&fresh,true);
    assert(fresh.rewards==33 && fresh.teleports==1);
    fresh.map=609;
    SkipIntroduction(&fresh,true);
    assert(fresh.rewards==33 && fresh.teleports==2);

    Player horde;horde.race=RACE_ORC;horde.team=1;horde.level=70;horde.xp=999;horde.xpLocked=true;
    horde.statuses[12593]=QUEST_STATUS_INCOMPLETE;
    SkipIntroduction(&horde,true);
    assert(horde.xpLocked);
    assert(horde.level==70 && horde.xp==999 && horde.rewards==33);
    assert(horde.rewarded.contains(12748) && horde.rewarded.contains(13189));
    assert(!horde.rewarded.contains(13188) && horde.homeMap==1 && horde.map==1);

    // Factory rebuild: rewards restored before talent allocation, without a teleport/save.
    fresh.rewarded.clear();fresh.statuses.clear();fresh.rewards=0;
    CompleteBotDKIntroduction(&fresh);
    assert(fresh.rewards==33 && fresh.teleports==2 && fresh.saves==2);

    Player human;human.session.headless=false;SkipIntroduction(&human,true);
    assert(human.rewards==0 && human.teleports==0);
    Player personal;sPlayerbotAIConfig.random=false;SkipIntroduction(&personal,true);
    assert(personal.rewards==0);sPlayerbotAIConfig.random=true;
    Player ordinary;ordinary.cls=1;SkipIntroduction(&ordinary,true);assert(ordinary.rewards==0);
    Player outside;outside.map=0;SkipIntroduction(&outside,true);assert(outside.rewards==0);
    Player missing;objects.missing=12779;SkipIntroduction(&missing,true);
    assert(missing.rewards==0 && missing.teleports==0);objects.missing=0;
    Player disabled;enabled=false;SkipIntroduction(&disabled,true);assert(disabled.rewards==0);enabled=true;
    Player fighting;fighting.combat=true;SkipIntroduction(&fighting,true);assert(fighting.rewards==0);
    for (uint32 race : {1,2,3,4,5,6,7,8,10,11}) assert(RacialQuest(race)!=0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / "fixture.h").write_text(fixture)
    for header in ["Config.h", "Log.h", "ObjectMgr.h", "Player.h", "PlayerbotAIConfig.h",
                   "ScriptMgr.h", "World.h", "WorldSession.h"]:
        (tmp / header).write_text('#include "fixture.h"\n')
    source = ROOT / "overlays/mod-bot-dk-skip/src/BotDKSkip.cpp"
    (tmp / "test.cpp").write_text(f'#include "{source}"\n' + tests)
    binary = tmp / "dk-skip-test"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I"+str(tmp),
                    str(tmp / "test.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("DK skip: both factions, saved bots, active quests, rebuild, replay and account isolation passed")
