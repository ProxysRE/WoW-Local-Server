"""Exercise the production role classifier with mismatched AI flags and active talent trees."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "overlays/mod-group-builder/src/mod_group_builder.cpp").read_text()
start = source.index("unsigned DetectRole(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
fixture = r'''
#include "GroupBuilderPolicy.h"
#include <cassert>
#include <map>
using uint8=std::uint8_t;
using uint32=std::uint32_t;
constexpr unsigned CLASS_WARRIOR=1,CLASS_PALADIN=2,CLASS_HUNTER=3,CLASS_ROGUE=4,CLASS_PRIEST=5,
 CLASS_DEATH_KNIGHT=6,CLASS_SHAMAN=7,CLASS_MAGE=8,CLASS_WARLOCK=9,CLASS_DRUID=11;
constexpr unsigned INVENTORY_SLOT_BAG_0=0,EQUIPMENT_SLOT_OFFHAND=16,EQUIPMENT_SLOT_END=19,
 INVTYPE_SHIELD=14,MAX_ITEM_PROTO_STATS=10,ITEM_MOD_DEFENSE_SKILL_RATING=12,ITEM_MOD_DODGE_RATING=13,
 ITEM_MOD_PARRY_RATING=14;
struct Stat { unsigned ItemStatType=0; int ItemStatValue=0; };
struct Template { unsigned InventoryType=0,StatsCount=0; Stat ItemStat[MAX_ITEM_PROTO_STATS]; };
struct Item { Template proto; Template const* GetTemplate() { return &proto; } };
struct Player
{
    unsigned cls=CLASS_PRIEST;
    bool tank=false,heal=false,dps=true;
    std::map<unsigned,Item*> items;
    unsigned getClass() { return cls; }
    Item* GetItemByPos(unsigned,unsigned slot) { auto i=items.find(slot);return i==items.end()?nullptr:i->second; }
};
struct PlayerbotAI
{
    static bool IsTank(Player* p) { return p->tank; }
    static bool IsHeal(Player* p) { return p->heal; }
    static bool IsDps(Player* p) { return p->dps; }
};
namespace GroupBuilder
{
// PRODUCTION_ROLE
}
int main()
{
    using namespace GroupBuilder;
    Player p;
    // Reproduce the report: an ungrouped Holy priest can carry a general DPS AI strategy.
    assert(DetectRole(&p,2)==Any);
    assert(DetectRole(&p,1)==Any);
    p.heal=true;
    assert(DetectRole(&p,1)==Healer && DetectRole(&p,2)==Healer);
    p.heal=false;
    assert(DetectRole(&p,3)==Damage);
    p.cls=CLASS_SHAMAN;
    assert(DetectRole(&p,3)==Any && DetectRole(&p,1)==Damage && DetectRole(&p,2)==Damage);
    p.heal=true; assert(DetectRole(&p,3)==Healer); p.heal=false;
    p.cls=CLASS_DRUID;
    assert(DetectRole(&p,3)==Any && DetectRole(&p,1)==Damage && DetectRole(&p,2)==Damage);
    p.heal=true; assert(DetectRole(&p,3)==Healer); p.heal=false;
    p.cls=CLASS_PALADIN;
    assert(DetectRole(&p,1)==Any && DetectRole(&p,2)==Any && DetectRole(&p,3)==Damage);
    p.heal=true; assert(DetectRole(&p,1)==Healer); p.heal=false;
    p.cls=CLASS_WARRIOR;
    assert(DetectRole(&p,3)==Any && DetectRole(&p,1)==Damage && DetectRole(&p,2)==Damage);
    Item shield; shield.proto.InventoryType=INVTYPE_SHIELD; p.items[16]=&shield; p.tank=true;
    assert(DetectRole(&p,3)==Tank);
    p.tank=false; p.items.clear(); p.cls=CLASS_DEATH_KNIGHT;
    assert(DetectRole(&p,1)==Damage); // Blood DPS must remain valid.
    p.tank=true; assert(DetectRole(&p,1)==Any); // No evidence of tank gear.
    p.tank=false; p.cls=CLASS_MAGE; assert(DetectRole(&p,1)==Damage);
    p.cls=CLASS_PRIEST; assert(DetectRole(&p,0)==Any);
    p.heal=p.tank=true; assert(DetectRole(&p,2)==Any);
}
'''.replace("// PRODUCTION_ROLE", source[start:end])
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / "test.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I"+str(ROOT/"overlays/mod-group-builder/src"),
                    str(work/"test.cpp"), "-o", str(work/"test")], check=True)
    subprocess.run([str(work/"test")], check=True)
print("Production role classifier: Holy/Disc, restoration, protection and valid Blood DPS scenarios passed")
