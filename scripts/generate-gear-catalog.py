"""Build a positive item-source catalogue from pinned AtlasLootClassic data (GPLv2).

Usage: PYTHONPATH=<lupa location> python scripts/generate-gear-catalog.py <checkout>
Normal server builds use the checked-in catalogue and require neither Lua nor this checkout.
"""
from pathlib import Path
import csv
import io
import json
import re
import subprocess
import sys
from lupa.lua51 import LuaRuntime, lua_type

ROOT = Path(__file__).resolve().parents[1]
PIN = "8e99341e4e779328460bf7684c0d5b22ce50ddf1"
BOOT = r'''
local trans=setmetatable({},{__index=function(t,k)return tostring(k)end})
AtlasLoot={CLASSIC_VERSION_NUM=1,BC_VERSION_NUM=2,WRATH_VERSION_NUM=3,Locales=trans,IngameLocales=trans}
function AtlasLoot:GameVersion_LT(v,a,b) if VERSION<v then return a or true else return b end end
function AtlasLoot:GameVersion_GE(v,a,b) if VERSION>=v then return a or true else return b end end
function AtlasLoot:GetRetByFaction(a,b)return a end
function AtlasLoot.ReturnForGameVersion(a,b,c) if VERSION==1 then return a elseif VERSION==2 then return b or a else return c or b or a end end
function AtlasLoot:GetColoredClassNames()return trans end
function GetFactionInfoByID(id)return tostring(id)end
AtlasLoot.Data={Requirements=setmetatable({},{__index=function()return function()return ''end end}),Profession={GetColorSkillRankNoSpell=function()return ''end}}
AtlasLoot.ItemDB={}; DB={}
function AtlasLoot.ItemDB:Add() return DB end
for _,name in ipairs({'AddDifficulty','AddItemTableType','AddExtraItemTableType','AddContentType'}) do DB[name]=function(self,key) return 'type:'..key end end
function UnitFactionGroup()return 'Alliance'end
C_Map={GetAreaInfo=function(id)return tostring(id)end}
RAID_CLASS_COLORS=setmetatable({},{__index=function()return {colorStr='ffffffff'}end})
function GetSpellInfo(id)return tostring(id)end
function GetItemInfo(id)return tostring(id)end
PRIVATE=setmetatable({},{__index=function(t,k)return k end})
setmetatable(_G,{__index=function(t,k) return k end})
'''

CLASSIC_PHASE = {1: 0, 2: 1, 2.5: 1, 3: 1, 4: 3, 5: 5, 6: 6}
WRATH_SOURCES = {
    "Ulduar": 14, "TrialoftheCrusader": 15, "TrialoftheChampion": 15,
    "WrathOnyxiasLair": 15, "IcecrownCitadel": 16, "ForgeOfSouls": 16,
    "PitOfSaron": 16, "HallsOfReflection": 16, "RubySanctum": 17,
}
COLLECTIONS = {
    "WorldEpics": 0, "WorldEpicsBC": 8, "WorldEpicsWrath": 13,
    "BadgeofJustice": 8, "BadgeofJustice4": 11, "BadgeofJusticeP5": 12, "BCCSunmote": 12,
    "EmblemofHeroism": 13, "EmblemofValor": 13, "EmblemofConquest": 14,
    "EmblemofTriumph": 15, "EmblemofFrost": 16, "DalaranVendor": 13,
    "ZGSets": 3, "AQSets": 5, "SilithusAbyssal": 3, "AQOpening": 5,
    "ScourgeInvasion": 6, "ScourgeInvasionWrath": 13, "Darkmoon": 1,
}
FACTIONS = {
    "ZandalarTribe": 3, "CenarionCircle": 5, "BroodOfNozdormu": 5,
    "AshtongueDeathsworn": 10, "TheScaleOfTheSands": 10, "ShatteredSunOffensive": 12,
    "TheAshenVerdict": 16, "TheSilverCovenant": 14, "TheSunreavers": 14,
}


def build(source):
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != PIN:
        raise ValueError(f"Expected AtlasLootClassic {PIN}, got {actual}")
    sets = {}
    text = (source / "AtlasLootClassic/Data/ItemSet.lua").read_text()
    for number, items in re.findall(r"\[(\d+)\]\s*=\s*\{\{([\d, ]+)\}", text):
        sets[int(number)] = [int(v) for v in items.split(",") if v.strip()]
    stages, origins = {}, {}

    def add(item, stage, origin):
        if not isinstance(item, (int, float)) or not 0 < item < 100000:
            return
        item = int(item)
        if item not in stages or stage < stages[item]:
            stages[item], origins[item] = stage, origin

    def walk(table, version, stage, kind, origin):
        if lua_type(table) != "table":
            return
        phase = table["ContentPhaseBC"] if version == 2 else table["ContentPhase"]
        if phase:
            stage = max(stage, (7+int(phase)) if version == 2 else CLASSIC_PHASE[phase])
        kind = table["TableType"] or kind
        for key, value in table.items():
            if isinstance(key, str) and key.startswith("type:") and lua_type(value) == "table":
                # An item row's slot is [1], item/set ID is [2]. Extra quest/currency fields are not items.
                if key in ("type:Quest", "type:Price", "type:Set"):
                    continue
                for _, row in value.items():
                    if lua_type(row) != "table":
                        continue
                    number = row[2]
                    if kind == "type:Set":
                        for item in sets.get(number, []):
                            add(item, stage, origin)
                    else:
                        add(number, stage, origin)
                        for faction in ("ATLASLOOT_IT_ALLIANCE", "ATLASLOOT_IT_HORDE"):
                            add(row[faction], stage, origin)
            elif key == "items" and lua_type(value) == "table":
                for _, child in value.items():
                    walk(child, version, stage, kind, origin)

    for module in ("DungeonsAndRaids", "Factions", "Collections", "PvP"):
        for suffix, version in (("", 1), ("-tbc", 2), ("-wrath", 3)):
            path = source / f"AtlasLootClassic_{module}/data{suffix}.lua"
            lua = LuaRuntime(unpack_returned_tuples=True)
            lua.globals().VERSION = version
            lua.execute(BOOT)
            lua.execute("function LOAD(...) " + path.read_text() + '\nend\nLOAD("module",PRIVATE)')
            for name, table in lua.globals().DB.items():
                if lua_type(table) != "table":
                    continue
                stage = {1: 0, 2: 8, 3: 13}[version]
                if module == "DungeonsAndRaids":
                    if name == "VaultofArchavon":
                        continue  # Mixed-tier shortcut; the same gear has raid/season sources below.
                    stage = WRATH_SOURCES.get(name, stage) if version == 3 else stage
                elif module == "Collections":
                    if name not in COLLECTIONS:
                        continue  # No Wrath Classic catch-up currencies, heirlooms or shared tier overviews.
                    stage = COLLECTIONS[name]
                elif module == "Factions":
                    stage = FACTIONS.get(name, stage)
                elif module == "PvP":
                    match = re.match(r"ArenaS(\d+)PvP", name)
                    if match:
                        stage = {1:8, 2:9, 3:10, 4:12, 5:13, 6:14, 7:15, 8:16}[int(match[1])]
                    elif name == "Wintergrasp":
                        stage = 16  # Mixed patch rewards; item phase metadata can lower these below.
                walk(table, version, stage, "type:Item", f"{module}/{name}")

    # Crafting has no reliable per-row phase metadata in Classic/TBC. Only recipes with
    # a catalogued source may lower their output below the expansion's final stage.
    recipes = {}
    recipe_text = (source / "AtlasLootClassic/Data/Recipe.lua").read_text()
    for item, spell in re.findall(r"\[(\d+)\]\s*=\s*\{\d+,\d+,(\d+)\}", recipe_text):
        if int(item) in stages:
            recipes[int(spell)] = stages[int(item)]
    profession_text = (source / "AtlasLootClassic/Data/Profession.lua").read_text()
    expansions = [(m.start(), m.group(1)) for m in re.finditer(r"PROFESSION_DATA\.(CLASSIC|BCC|WRATH)\s*=", profession_text)]
    # The actual data table has the same assignment convention as RECIPE_DATA.
    if not expansions:
        expansions = [(m.start(), m.group(1)) for m in re.finditer(r"\w+\.(CLASSIC|BCC|WRATH)\s*=\s*\{", profession_text)]
    crafts = []
    for match in re.finditer(r"\[(\d+)\]\s*=\s*\{(\d+),\d+,\d+,\d+,\d+,\{([\d,]+)\}", profession_text):
        expansion = next((v for pos,v in reversed(expansions) if pos < match.start()), "CLASSIC")
        default = {"CLASSIC": 6, "BCC": 12, "WRATH": 17}[expansion]
        crafts.append((int(match[1]), int(match[2]), [int(v) for v in match[3].split(",")], default))
    for _ in range(5):
        for spell, item, materials, default in crafts:
            stage = recipes.get(spell, default)
            stage = max([stage] + [stages[v] for v in materials if v in stages])
            add(item, stage, "Craft/"+str(spell))

    # Wrath phase metadata also covers crafted and PvP items. It is an item-level
    # release constraint, so a shared vendor/secondary source must never lower it.
    phase_text = (source / "AtlasLootClassic/Data/ContentPhase.lua").read_text()
    wrath = phase_text.split("if AtlasLoot:GameVersion_EQ(AtlasLoot.WRATH_VERSION_NUM) then",1)[1]
    for item, phase in re.findall(r"\[(\d+)\]\s*=\s*([234])\b", wrath):
        item, stage = int(item), 12+int(phase)
        if item in stages and not origins[item].startswith("DungeonsAndRaids/"):
            stages[item] = max(stages[item], stage)
    # Shared TBC insignia tables include the patch-2.4 upgrade in every season.
    for item in (37864,37865):
        if item in stages:
            stages[item] = 12

    rows = sorted(stages.items())
    output = ROOT / "overlays/mod-bot-gear-progression/src/GearCatalog.inc"
    output.write_text("// Generated from Hoizame/AtlasLootClassic " + PIN + "; GPL-2.0-or-later.\n" +
                      "// item ID, minimum content stage (no item-level thresholds).\n" +
                      "".join(f"    {{{item}, {stage}}},\n" for item,stage in rows))
    buf = io.StringIO()
    writer = csv.writer(buf, lineterminator="\n")
    writer.writerow(["item", "stage", "source"])
    writer.writerows((item, stage, origins[item]) for item,stage in rows)
    (ROOT / "data/bot-gear-catalog.csv").write_text(buf.getvalue())
    (ROOT / "data/bot-gear-catalog-source.json").write_text(json.dumps({
        "repository": "Hoizame/AtlasLootClassic", "commit": PIN, "license": "GPL-2.0",
        "items": len(rows), "unknown_items": "deny while stage limit is enabled",
        "crafting": "unclassified recipes conservatively use the final expansion stage"
    }, indent=2)+"\n")
    print(f"Generated {len(rows)} item rules")


if __name__ == "__main__":
    build(Path(sys.argv[1]).resolve())
