"""Fetch exactly the revisions in sources.lock.json and prepare teleporter SQL."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import shutil

ROOT = Path(__file__).resolve().parents[1]


def prepare_teleporter(root=ROOT):
    source = root / "dependencies/sql-npc-teleporter/data/sql/db-world/teleporter_capital.dist"
    sql = source.read_text(encoding="utf-8")
    # The pinned upstream row has more values than its declared column list.
    pattern = r"(INSERT INTO creature_template \([^;]+?\) VALUES\s*)\([^;]+?\);"
    replacement = (
        r'\1(@ENTRY, @NAME, @SUBNAME, "Directions", @GOSSIP_MENU, 71, 71, '
        r'35, 3, 1, 1.14286, 1, 2, 7, 138936390, 1, 2, "SmartAI", "");'
    )
    sql, count = re.subn(pattern, replacement, sql, count=1)
    if count != 1:
        raise RuntimeError("Teleporter template layout changed; review adaptation")
    # Custom SQL runs after core updates. 2026_06_16_00 renames creature.id1
    # to id, so preserve the upstream teleporter's id column here.
    # The pinned spawns have phaseMask=0, making them invisible until the core
    # repairs them in memory. Write a valid mask into the database instead.
    before_spawns, marker, spawns = sql.partition("-- Teleporter spawns:")
    if not marker:
        raise RuntimeError("Teleporter spawn section missing")
    spawns, count = re.subn(r"(?m)^(\(@ENTRY, (?:0|1|530|571), 1, )0,", r"\g<1>1,", spawns)
    if count != 12:
        raise RuntimeError("Teleporter spawn layout changed; review phase masks")
    sql = before_spawns + marker + spawns
    # Avoid removing unrelated gameobjects in an existing world.
    sql = sql.replace("DELETE from gameobject WHERE ID = @RUNE AND guid >= 200000;",
                      "-- Decorative rune deletion omitted in the local-server adaptation.")
    # Decorative runes are unnecessary and would accumulate on a manual re-import.
    sql = re.sub(r"INSERT INTO gameobject \([^;]+;", "", sql, flags=re.S)
    sql = "-- Adapted from Zoidwaffle/sql-npc-teleporter; original credits follow.\n" + sql
    output = root / "core/data/sql/custom/db_world/2026_11_11_00_local_teleporter.sql"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(sql, encoding="utf-8", newline="\n")
    # Repair existing installations where the original custom import succeeded.
    output.with_name("2026_11_11_01_local_teleporter_phase.sql").write_text(
        "UPDATE creature SET phaseMask = 1 WHERE id = 190000 AND phaseMask = 0;\n",
        encoding="utf-8", newline="\n")
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--local", action="store_true", help="Use already checked out sources")
    args = parser.parse_args()
    lock = json.loads((ROOT / "sources.lock.json").read_text())
    for entry in lock["sources"]:
        sha = entry["commit"]
        if not re.fullmatch(r"[0-9a-f]{40}", sha):
            raise ValueError("Invalid pinned revision")
        target = ROOT / entry["path"]
        if not args.local:
            if target.exists():
                raise RuntimeError(f"Refusing to overwrite existing checkout: {target}")
            target.mkdir(parents=True)
            subprocess.run(["git", "init", str(target)], check=True)
            subprocess.run(["git", "-C", str(target), "remote", "add", "origin",
                            f"https://github.com/{entry['repository']}.git"], check=True)
            subprocess.run(["git", "-C", str(target), "fetch", "--depth", "1", "origin", sha], check=True)
            subprocess.run(["git", "-C", str(target), "checkout", "--detach", "FETCH_HEAD"], check=True)
        actual = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"], text=True).strip()
        if actual != sha:
            raise RuntimeError(f"Wrong source revision for {entry['name']}")
    arena = ROOT / "core/modules/mod-1v1-arena"
    # Normalize the patch even when a Windows checkout used CRLF.
    patch = (ROOT / "patches/arena-playerbots.patch").read_text(encoding="utf-8").encode("utf-8")
    subprocess.run(["git", "-C", str(arena), "apply", "--check", "-"], input=patch, check=True)
    subprocess.run(["git", "-C", str(arena), "apply", "-"], input=patch, check=True)
    shutil.copytree(ROOT / "overlays/mod-1v1-arena", arena, dirs_exist_ok=True)
    config = arena / "conf/1v1arena.conf.dist"
    with config.open("a", encoding="utf-8") as output:
        output.write('\n# Local Playerbot opponent bridge: unrated only.\n'
                     'Arena1v1.Playerbots.Enable = 0\n'
                     '# Maximum difference in average equipped item level.\n'
                     'Arena1v1.Playerbots.MaxItemLevelGap = 15\n')
    print(prepare_teleporter())


if __name__ == "__main__":
    main()
