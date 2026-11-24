"""Package the install tree and the exact sources needed by the database updater."""
import json
import os
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]


def copy_tree(src, dst):
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns(".git", "__pycache__"), dirs_exist_ok=True)


def main():
    dist = ROOT / "dist"
    server = dist / "Server"
    if dist.exists():
        raise RuntimeError("Package directory already exists")
    copy_tree(ROOT / "out", server)
    for name in ("authserver.exe", "worldserver.exe"):
        if not (server / name).is_file():
            raise RuntimeError(f"Missing installed executable: {name}")
    mysql = Path(r"C:\tools\mysql\current")
    openssl = Path(os.environ["OPENSSL_ROOT_DIR"])
    dlls = [*mysql.glob("lib/*.dll"), *mysql.glob("bin/*.dll"), *openssl.glob("bin/*.dll")]
    for path in dlls:
        shutil.copy2(path, server / path.name)
    # mysql.exe is needed by the database updater. The MySQL service is installed separately.
    shutil.copy2(mysql / "bin/mysql.exe", server / "mysql.exe")
    copy_tree(ROOT / "core/data", server / "Source/data")
    copy_tree(ROOT / "core/modules", server / "Source/modules")
    # Retain the complete pinned source code and licenses for reproducibility.
    copy_tree(ROOT / "core", dist / "Source")
    copy_tree(ROOT / "dependencies", dist / "Dependencies")
    for path in ("README.md", "sources.lock.json"):
        shutil.copy2(ROOT / path, dist / path)
    copy_tree(ROOT / "docs", dist / "docs")
    copy_tree(ROOT / "scripts", dist / "BuildScripts")
    copy_tree(ROOT / "patches", dist / "patches")
    copy_tree(ROOT / "overlays", dist / "overlays")
    copy_tree(ROOT / "profiles", dist / "profiles")
    auto_config = ROOT / "core/modules/mod-autobalance/conf/AutoBalance.conf.dist"
    (server / "configs/modules").mkdir(parents=True, exist_ok=True)
    shutil.copy2(auto_config, server / "configs/modules/AutoBalance.conf")
    shutil.copy2(ROOT / "scripts/configure-local.ps1", server / "configure-local.ps1")
    for exe in ("authserver", "worldserver"):
        (server / f"start-{exe}.bat").write_text(
            f'@echo off\ncd /d "%~dp0"\n{exe}.exe -c configs\\{exe}.conf\npause\n', encoding="ascii")
    summary = {"target": "Windows x64", "status": "compiled; in-game validation pending",
               "autobalance": "instance player count including Playerbots; original creature levels; in-game validation pending",
               "playerbots_movement": "loot/remount arbitration and taxi protection/recovery; in-game validation pending",
               "temporary_progression": "session-only historical visits; in-game validation pending",
               "local_qol": "workshop crafting, gray-item sale and opt-in fishing loop; in-game validation pending",
               "starter_professions": "first human login: 14 apprentice professions and 4 tools; in-game validation pending",
               "arena_1v1": "experimental unrated Playerbot bridge; in-game validation pending",
               "new_races": False}
    (dist / "BUILD_STATUS.json").write_text(json.dumps(summary, indent=2) + "\n")
    visit = dist / "Update_TemporaryProgression"
    (visit / "Server").mkdir(parents=True)
    shutil.copy2(server / "worldserver.exe", visit / "Server/worldserver.exe")
    shutil.copy2(ROOT / "docs/TEMPORARY_PROGRESSION_RU.md", visit / "UPDATE_RU.md")
    movement = dist / "Update_PlayerbotsMovement"
    (movement / "Server").mkdir(parents=True)
    shutil.copy2(server / "worldserver.exe", movement / "Server/worldserver.exe")
    shutil.copy2(ROOT / "docs/PLAYERBOTS_MOVEMENT_RU.md", movement / "UPDATE_RU.md")
    update = dist / "Update_LocalQoL"
    (update / "Server/configs/modules").mkdir(parents=True)
    shutil.copy2(server / "worldserver.exe", update / "Server/worldserver.exe")
    for module, filename in (("mod-local-qol", "local_qol.conf.dist"),
                             ("mod-starter-professions", "starter_professions.conf.dist")):
        shutil.copy2(ROOT / "overlays" / module / "conf" / filename,
                     update / "Server/configs/modules" / filename)
    shutil.copy2(ROOT / "docs/LOCAL_QOL_RU.md", update / "UPDATE_RU.md")
    balance = dist / "Update_AutoBalance"
    (balance / "Server").mkdir(parents=True)
    shutil.copy2(server / "worldserver.exe", balance / "Server/worldserver.exe")
    shutil.copy2(ROOT / "docs/AUTOBALANCE_RU.md", balance / "UPDATE_RU.md")
    # Every standalone executable in this build includes AutoBalance and needs its profile.
    for destination in (visit, movement, update, balance):
        configs = destination / "Server/configs/modules"
        configs.mkdir(parents=True, exist_ok=True)
        shutil.copy2(auto_config, configs / "AutoBalance.conf")
        shutil.copy2(auto_config, configs / "AutoBalance.conf.dist")
        shutil.copy2(ROOT / "docs/AUTOBALANCE_RU.md", destination / "AUTOBALANCE_RU.md")


if __name__ == "__main__":
    main()
