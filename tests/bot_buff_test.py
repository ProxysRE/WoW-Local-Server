"""Exercise the production rank selection, aura protection and queued casting step."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "overlays/mod-bot-buff/src/mod_bot_buff.cpp").read_text()
def function(signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]
production = "\n".join(function(s) for s in ("std::vector<Id> Known(", "bool Covered(", "bool Step("))
fixture = (ROOT / "tests/bot_buff_fixture.cpp").read_text().replace("// PRODUCTION_METHODS", production)
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / "test.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I"+str(ROOT/"overlays/mod-bot-buff/src"),
                    str(work/"test.cpp"), "-o", str(work/"test")], check=True)
    subprocess.run([str(work/"test")], check=True)
print("BotBuff: whitelist, ranks, downranking, aura protection, GCD, mounting and request expiry passed")
