"""Exercise the patched upstream aura handler during saved-aura restoration."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path("src/server/scripts/EasternKingdoms/zone_the_scarlet_enclave.cpp")
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    source = tmp / SOURCE
    source.parent.mkdir(parents=True)
    source.write_text((ROOT / "tests/lich_king_whisper_upstream.cpp").read_text())
    patch = (ROOT / "patches/lich-king-whisper-null-caster.patch").read_bytes()
    subprocess.run(["git", "apply", "--check", "-"], input=patch, cwd=tmp, check=True)
    subprocess.run(["git", "apply", "-"], input=patch, cwd=tmp, check=True)
    code = source.read_text()
    start = code.index("    void HandleEffectApply(")
    end = code.index("    void Register()", start)
    handler = code[start:end]
    fixture = r'''
#include <cassert>
using uint32 = unsigned int;
struct AuraEffect {};
enum AuraEffectHandleModes { AURA_EFFECT_HANDLE_REAL };
constexpr uint32 SPELL_LICHKINGDK001 = 58208;
constexpr uint32 SPELL_LICHKINGDK016 = 58223;
uint32 urand(uint32 first, uint32 last) { assert(first <= last); return first; }
struct Player;
struct Unit
{
    Player* player = nullptr;
    Player* ToPlayer() { return player; }
    int casts = 0;
    Player* lastTarget = nullptr;
    void CastSpell(Player* target, uint32 spell, bool triggered)
    {
        assert(spell >= SPELL_LICHKINGDK001 && spell <= SPELL_LICHKINGDK016);
        assert(triggered);
        ++casts;
        lastTarget = target;
    }
};
struct Player : Unit { Player() { player = this; } };
struct Handler
{
    Unit* target;
    Unit* caster;
    int casterLookups = 0;
    Unit* GetTarget() { return target; }
    Unit* GetCaster() { ++casterLookups; return caster; }
HANDLER
};
int main()
{
    Player player;
    Unit creature;
    Handler restored{&player, nullptr};
    restored.HandleEffectApply(nullptr, AURA_EFFECT_HANDLE_REAL);
    assert(restored.casterLookups == 1);
    Handler live{&player, &creature};
    live.HandleEffectApply(nullptr, AURA_EFFECT_HANDLE_REAL);
    assert(live.casterLookups == 1 && creature.casts == 1 && creature.lastTarget == &player);
    Handler nonPlayer{&creature, nullptr};
    nonPlayer.HandleEffectApply(nullptr, AURA_EFFECT_HANDLE_REAL);
    assert(nonPlayer.casterLookups == 0 && creature.casts == 1);
    Handler self{&player, &player};
    self.HandleEffectApply(nullptr, AURA_EFFECT_HANDLE_REAL);
    assert(player.casts == 1 && player.lastTarget == &player);
}
'''.replace("HANDLER", handler)
    test = tmp / "test.cpp"
    test.write_text(fixture)
    binary = tmp / "aura-test"
    subprocess.run(["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror",
                    str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Lich King whisper: saved aura without caster and normal casts passed")
