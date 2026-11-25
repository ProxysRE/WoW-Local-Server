"""Compile the production world-thread invitation pump against an instrumented game fixture."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "overlays/mod-group-builder/src/mod_group_builder.cpp").read_text()
start = source.index("void Pump(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
fixture = (ROOT / "tests/group_builder_invites_fixture.cpp").read_text()
fixture = fixture.replace("// PRODUCTION_PUMP", source[start:end])
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    (work / "test.cpp").write_text(fixture)
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "overlays/mod-group-builder/src"),
                    str(work / "test.cpp"), "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
print("Production invitation pump: normal invites, acceptance, timeouts, group changes, roles and capacity passed")
