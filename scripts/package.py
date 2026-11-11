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
    shutil.copy2(ROOT / "scripts/configure-local.ps1", server / "configure-local.ps1")
    for exe in ("authserver", "worldserver"):
        (server / f"start-{exe}.bat").write_text(
            f'@echo off\ncd /d "%~dp0"\n{exe}.exe -c configs\\{exe}.conf\npause\n', encoding="ascii")
    summary = {"target": "Windows x64", "status": "compiled; in-game validation pending",
               "arena_1v1": "not included: Playerbots queue integration unverified",
               "new_races": False}
    (dist / "BUILD_STATUS.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
