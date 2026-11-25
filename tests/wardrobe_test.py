"""Exercise production plan parsing, appearance validation and complete-outfit pricing."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    code = (ROOT / "overlays/mod-transmog/src/Wardrobe.cpp").read_text()
    bodies = "\n".join(function(code, signature) for signature in (
        "std::string Validate(", "bool BuildQuote("))
    fixture = (ROOT / "tests/wardrobe_fixture.cpp").read_text().replace("// PRODUCTION_METHODS", bodies)
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        (work / "test.cpp").write_text(fixture)
        subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        "-I" + str(ROOT / "overlays/mod-transmog/src"), str(work / "test.cpp"),
                        "-o", str(work / "test")], check=True)
        subprocess.run([str(work / "test")], check=True)
    print("Wardrobe: parser, forged ownership, compatibility, multi-slot cost and stale-item snapshots passed")


if __name__ == "__main__":
    main()
