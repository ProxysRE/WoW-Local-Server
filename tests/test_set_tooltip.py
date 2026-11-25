"""Retain the already delivered 0.1.1 tooltip fix in the combined update."""
from pathlib import Path
import unittest
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


class SetTooltipTests(unittest.TestCase):
    def check_count(self, old, actual):
        lua = LuaRuntime(unpack_returned_tuples=True)
        source = (ROOT / 'addons/ACWardrobe/SetTooltip.lua').read_text()
        repair = lua.execute(source[:source.index('local function Update')] + '\nreturn Repair')
        lines = lua.table_from([
            lua.table_from({'text': f'Комплект ({old}/5)', 'color': lua.table_from([1, 1, 1])}),
            *[lua.table_from({'text': f'Часть {i}', 'color': lua.table_from([.5, .5, .5])})
              for i in range(1, 6)],
            lua.table_from({'text': '(4) Бонус', 'color': lua.table_from([.5, .5, .5])}),
        ])
        equipped = lua.table_from({f'Часть {i}': True for i in range(1, actual + 1)})
        self.assertTrue(repair(lines, equipped, 'Часть 1'))
        self.assertEqual(lines[1]['text'], f'Комплект ({actual}/5)')
        self.assertEqual(lines[7]['color'][2], 1 if actual >= 4 else .5)

    def test_transmog_does_not_lose_set_piece(self):
        self.check_count(3, 4)

    def test_actual_unequip_loses_set_piece(self):
        self.check_count(4, 3)


if __name__ == '__main__':
    unittest.main()
