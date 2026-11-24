"""Compile patched production methods against instrumented movement/loot fixtures."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
REV = "405f81196161017eebae3ee7a612d786a3ed2a9d"
FILES = (
    "src/Ai/Base/Actions/CheckMountStateAction.cpp",
    "src/Ai/Base/Actions/CheckMountStateAction.h",
    "src/Bot/PlayerbotAI.cpp",
    "src/Bot/PlayerbotAI.h",
    "src/Ai/World/Rpg/Action/NewRpgAction.cpp",
    "src/Ai/World/Rpg/Action/NewRpgAction.h",
)


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        source = args.source or work
        if not args.source:
            for name in FILES:
                target = work / name
                target.parent.mkdir(parents=True, exist_ok=True)
                url = f"https://raw.githubusercontent.com/mod-playerbots/mod-playerbots/{REV}/{name}"
                with urllib.request.urlopen(url, timeout=60) as response:
                    target.write_bytes(response.read())
            subprocess.run(["git", "apply", str(ROOT / "patches/playerbots-movement-loot.patch")],
                           cwd=work, check=True)
        mount = (source / FILES[0]).read_text()
        ai = (source / FILES[2]).read_text()
        # Verify the guard is wired before the engine's first action, not merely declared.
        next_action = function(ai, "void PlayerbotAI::DoNextAction")
        assert next_action.index("if (UpdateTaxiFlight())") < next_action.index("currentEngine->DoNextAction")
        bodies = [function(mount, name) for name in (
            "bool CheckMountStateAction::Mount()",
            "bool CheckMountStateAction::IsLootInteractionActive()",
            "void CheckMountStateAction::ClearStaleFlightFlags()",
            "void CheckMountStateAction::CompleteDismount(Player* bot)",
        )]
        bodies.append(function(ai, "bool PlayerbotAI::UpdateTaxiFlight()"))
        harness = (ROOT / "tests/playerbots_movement_fixture.cpp").read_text()
        (work / "test.cpp").write_text(harness.replace("// PRODUCTION_METHODS", "\n\n".join(bodies)))
        binary = work / "movement-test"
        subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", str(work / "test.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        print("Production mount, dismount and taxi regression checks passed")


if __name__ == "__main__":
    main()
