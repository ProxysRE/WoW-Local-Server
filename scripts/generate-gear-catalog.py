"""Generate original 3.3.5 item sources, preserving alternative raid modes.

Usage: python scripts/generate-gear-catalog.py <AtlasLoot-5.11.04-checkout> <Classic-checkout>
Requires lupa 2.8 only for regeneration, not normal server builds.
"""
from pathlib import Path
import csv
import io
import json
import re
import subprocess
import sys
from lupa.lua51 import LuaRuntime, lua_type
import gear_catalog_classic_base as base

ROOT = Path(__file__).resolve().parents[1]
PIN = "092b7fcf14f0529dd54baebcb7118c8e53136d48"
BOOT = r'''
TRANS=setmetatable({},{__index=function(t,k)return k end})
function LibStub()return {GetLocale=function()return TRANS end}end
function AtlasLoot_GetLocaleLibBabble()return TRANS end
function GetSpellInfo(id)return tostring(id),'rank' end
LOCALIZED_CLASS_NAMES_MALE=TRANS; LOCALIZED_CLASS_NAMES_FEMALE=TRANS
AtlasLoot_Data={};AtlasLoot_CraftingDB={};AtlasLoot_CraftingData={};AtlasLootWishList={}
setmetatable(_G,{__index=function(t,k)return function()end end})
'''


def load(path):
    lua = LuaRuntime()
    lua.execute(BOOT)
    lua.execute(path.read_text(encoding="utf-8-sig"))
    return lua, lua.globals().AtlasLoot_Data


def context(name):
    """Only positively identified tables; mixed summaries never create generic sources."""
    size, heroic = (25 if "25Man" in name else 10), int("HEROIC" in name)
    if name.startswith("Naxx80") or name.startswith(("Sartharion", "Malygos")):
        return 13, size, 0, 0
    if name.startswith("Ulduar"):
        return 14, size, 0, int("Algalon" in name)
    if name.startswith("TrialoftheCrusader"):
        return 15, size, heroic, 0
    if name.startswith("Onyxia_"):
        return 15, size, 0, 0
    if name.startswith("ICC"):
        return 16, size, heroic, 0
    if name.startswith("Halion"):
        return 17, size, heroic, 0
    if name.startswith("VaultofArchavon"):
        stage = next((value for boss,value in [("Archavon",13),("Emalon",14),("Koralon",15),("Toravon",16)]
                      if re.search("VaultofArchavon"+boss, name)), None)
        # The first boss is named VaultofArchavon1 rather than ...Archavon1.
        return (stage or 13), size, 0, 0
    if name.startswith("Naxx"):
        return 13, 10, 0, 0  # T7/T8 class sets: headers set the actual stage and size.
    if name.startswith("T9"):
        return 15, 0, 0, 0
    if name.startswith("T10"):
        return 16, 0, 0, 0
    for prefix,stage,raid in [("EmblemofHeroism",13,0),("EmblemofValor",13,25),
                              ("EmblemofConquest",14,25),("EmblemofTriumph",15,0),("EmblemofFrost",16,0)]:
        if name.startswith(prefix):
            return stage, raid, 0, 0
    if name.startswith(("PvP", "Wrathful", "LakeWintergrasp")):
        return 16, 0, 0, 0  # Each item is refined by its named season below; ambiguous rewards stay late.
    if name.startswith(("TrialoftheChampion",)):
        return 15, 0, 0, 0
    if name.startswith(("FoS", "PoS", "HoR", "FHTrash")):
        return 16, 0, 0, 0
    if name.startswith("AshenVerdict"):
        return 16, 0, 0, 0
    if name.startswith(("TheSilverCovenant", "TheSunreavers")):
        return 14, 0, 0, 0
    prefixes = ("UtgardeKeep", "TheNexus", "AzjolNerub", "Ahnkahet", "DrakTharonKeep", "VioletHold",
                "Gundrak", "HallsofStone", "HallsofLightning", "CoTStratholme", "UP", "Ocu",
                "AllianceVanguard", "HordeExpedition", "ArgentCrusade", "FrenzyheartTribe", "KirinTor",
                "KnightsoftheEbonBlade", "TheKaluak", "TheOracles", "TheSonsofHodir", "TheWyrmrestAccord",
                "WinterfinRetreat", "VentureBay", "WorldEpicsWrath")
    return (13, 0, 0, 0) if name.startswith(prefixes) else None


def generate(legacy, classic):
    actual = subprocess.check_output(["git", "-C", str(legacy), "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN:
        raise ValueError(f"Expected original AtlasLoot {PIN}, got {actual}")
    supplemental, origins = base.build(classic)
    rules = {}

    def add(item, ctx, source):
        if not isinstance(item, (int,float)) or not 0 < item < 100000:
            return
        key = (int(item), *ctx)
        rules.setdefault(key, source)

    # The Classic branch supplies restored Naxx40 and TBC annotations, never WotLK loot.
    for item,stage in supplemental.items():
        if stage < 13:
            add(item, (stage,0,0,0), "ClassicTBC/"+origins[item])
    lua, tables = load(legacy / "Modules/WrathoftheLichKing/wrathofthelichking.lua")
    for name, table in sorted(tables.items()):
        if lua_type(table) != "table" or context(name) is None:
            continue
        initial = context(name)
        ctx = initial
        header = 0
        rows = sorted((key,row) for key,row in table.items() if isinstance(key,(int,float)) and lua_type(row)=="table")
        for _,row in rows:
            item = row[2]
            text = " ".join(str(row[i] or "") for i in range(4,7))
            if item == 0:
                header = int(row[1])
                if name.startswith("Naxx") and not name.startswith("Naxx80"):
                    match = re.search(r"#t([78])", text)
                    if match:
                        ctx = (13 if match[1]=="7" else 14, 25 if "25 Man" in text else 10,0,0)
                if "#j53#" in text or (name.startswith("Sartharion") and "Bonus Loot" in text):
                    ctx = (*ctx[:3],1)
                continue
            if not isinstance(item,(int,float)):
                continue
            if name.startswith(("T9","T10")):
                stage = initial[0]
                if header < 8:
                    add(item,(stage,0,0,0),name+"/base-badges")
                elif header < 16:
                    add(item,(stage,25,0,0),name+"/normal-token")
                    add(item,(stage,10,1,0),name+"/heroic10-token")
                else:
                    add(item,(stage,25,1,0),name+"/heroic-token")
                continue
            if name.startswith(("PvP","Wrathful","LakeWintergrasp")):
                season = next((stage for prefix,stage in [("Wrathful",16),("Relentless",15),("Furious",14),
                              ("Deadly",13),("Hateful",13),("Savage",13)] if prefix in text),16)
                add(item,(season,0,0,0),name)
            elif name.startswith("KirinTor") and "Kirin Tor" in text:
                # Mixed upgrade rings: no guess based on their item level.
                add(item,(16,0,0,0),name+"/unresolved-ring-upgrade")
            else:
                add(item,ctx,name)

    # Retain only original-3.3.5 crafting outputs, conservatively staging unresolved recipes.
    craft_lua, crafts = load(legacy / "Modules/Crafting/crafting.lua")
    craft_items = set()
    for _,table in crafts.items():
        if lua_type(table) != "table":
            continue
        for _,row in table.items():
            if lua_type(row) != "table" or not isinstance(row[2],str) or not row[2].startswith("s"):
                continue
            try:
                craft_items.add(int(row[3]))
            except (TypeError,ValueError):
                pass
    for item in craft_items:
        if item in supplemental and origins[item].startswith("Craft/"):
            add(item,(supplemental[item],0,0,0),"Original335/"+origins[item])

    rows = sorted(rules)
    target = ROOT / "overlays/mod-bot-gear-progression/src/GearCatalog.inc"
    target.write_text("// Original AtlasLoot 5.11.04: "+PIN+"; GPL-2.0-or-later.\n"
                      "// item, stage, raid size (0=nonraid), heroic, hard mode. Alternative sources are separate rows.\n"+
                      "".join("    {"+", ".join(map(str,row))+"},\n" for row in rows))
    out=io.StringIO();writer=csv.writer(out,lineterminator="\n")
    writer.writerow(["item","stage","raid_size","heroic","hard_mode","source"])
    writer.writerows((*row,rules[row]) for row in rows)
    (ROOT / "data/bot-gear-catalog.csv").write_text(out.getvalue())
    (ROOT / "data/bot-gear-catalog-source.json").write_text(json.dumps({
        "repository":"nanderson11/AtlasLootEnhanced", "commit":PIN,"version":"5.11.04 / 2010-08-07",
        "supplemental_repository":"Hoizame/AtlasLootClassic", "supplemental_commit":base.PIN,
        "supplemental_scope":"Classic/TBC and original-3.3.5 crafting only; no Classic WotLK loot",
        "license":"GPL-2.0", "items":len({row[0] for row in rows}),"sources":len(rows),
        "unknown_items":"deny while stage limit is enabled",
        "crafting":"unclassified recipes conservatively use the final expansion stage"
    },indent=2)+"\n")
    print(f"Generated {len(rows)} source rules for {len({row[0] for row in rows})} items")


if __name__ == "__main__":
    generate(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
