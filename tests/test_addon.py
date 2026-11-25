"""Lua 5.1 protocol integration; no game client or world DB required."""
from pathlib import Path
import subprocess
import unittest
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


class AddonTests(unittest.TestCase):
    def setUp(self):
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.lua.execute('''
            frames={}; now=0; sent={}; SlashCmdList={}
            function CreateFrame()
                local f={scripts={}}
                function f:RegisterEvent() end
                function f:SetScript(name,callback) self.scripts[name]=callback end
                table.insert(frames,f); return f
            end
            function GetTime() return now end
            function UnitName() return "Sigma" end
            function SendAddonMessage(prefix,msg,channel,target)
                table.insert(sent,{prefix,msg,channel,target})
            end
        ''')
        for name in ("Data.lua", "Zones.lua", "Core.lua", "Network.lua"):
            self.lua.execute((ROOT / "client/WoWBestiary" / name).read_text())
        # Compile every addon chunk under the actual target language version.
        for path in (ROOT / "client/WoWBestiary").glob("*.lua"):
            self.lua.compile(path.read_text())

    def event(self, msg, sender="Sigma"):
        self.lua.globals().message = msg
        self.lua.globals().sender = sender
        self.lua.execute('frames[2].scripts.OnEvent(frames[2],"CHAT_MSG_ADDON","WBE1",message,"WHISPER",sender)')

    def tick(self, t):
        self.lua.globals().now = t
        self.lua.execute('frames[2].scripts.OnUpdate()')

    def test_cpp_response_and_lazy_loot(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        frames = subprocess.check_output(["/tmp/bestiary-protocol-test"], text=True).rstrip('\0').split('\0')
        for frame in reversed(frames):
            self.event(frame.split("\t", 1)[1])
        self.assertTrue(self.lua.eval('WoWBestiary.zoneLoaded'))
        self.assertEqual(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].name'), 'Лупос|%')
        self.lua.execute('WoWBestiary.RequestDetail(WoWBestiary.GetZoneMobs(10)[1])')
        self.tick(2)
        self.event('2|1|1|H|D|521|0|1\nI|skin|2319|Кожа|1|25|0|Count 1-2\nI|loot|99|Предмет|2|-1|1|group 1')
        self.assertTrue(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].detailLoaded'))
        self.assertEqual(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].skin[1].chance'), 25)
        self.assertIsNone(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].loot[1].chance'))

    def test_atomic_pages_duplicates_and_sender(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.event('1|1|1|H|Z|10|0|2\nM|521|Spoof|23|23|4|3|1', 'OtherPlayer')
        self.assertFalse(bool(self.lua.eval('WoWBestiary.zoneLoaded')))
        payload = 'H|Z|10|0|2\nM|521|Лупос|23|23|4|3|1'
        self.event('1|2|2|' + payload[20:])
        self.event('1|2|2|' + payload[20:])
        self.event('1|1|2|' + payload[:20])
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 0)
        self.tick(2)
        self.event('2|1|1|H|Z|10|1|2\nM|574|Нараксис|27|27|4|1|1')
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 2)

    def test_timeout_and_stale_response(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.tick(12)
        self.assertIn('Нет ответа', self.lua.eval('WoWBestiary.networkStatus'))
        self.lua.execute('WoWBestiary.ReloadServer()')
        self.tick(13)
        self.event('1|1|1|H|Z|10|0|1')
        self.assertFalse(bool(self.lua.eval('WoWBestiary.zoneLoaded')))
        self.event('2|1|1|H|Z|10|0|1')
        self.assertTrue(self.lua.eval('WoWBestiary.zoneLoaded'))

    def test_bounded_frame_count(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.event('1|1|999999|junk')
        self.assertIn('Ошибка передачи', self.lua.eval('WoWBestiary.networkStatus'))

    def test_selection_cancels_old_detail(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.event('1|1|1|H|Z|10|0|1\nM|521|Лупос|23|23|4|3|1\nM|574|Нараксис|27|27|4|1|1')
        self.lua.execute('WoWBestiary.RequestDetail(WoWBestiary.GetZoneMobs(10)[1])')
        self.tick(2)
        self.lua.execute('WoWBestiary.RequestDetail(WoWBestiary.GetZoneMobs(10)[2])')
        self.tick(3)
        self.event('2|1|1|H|D|521|0|1\nI|loot|99|Old|2|25|0|note')
        self.assertFalse(bool(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].detailLoaded')))
        self.event('3|1|1|H|D|574|0|1')
        self.assertTrue(self.lua.eval('WoWBestiary.GetZoneMobs(10)[2].detailLoaded'))
        self.lua.execute('frames[2].scripts.OnEvent(frames[2],"PLAYER_ENTERING_WORLD")')
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 0)

    def test_reject_bad_rows_without_partial_commit(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.event('1|1|1|H|Z|10|0|1\nM|521|Лупос|23|23|4|3|1\nM|bad|Broken|x|x')
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 0)
        self.assertIn('Некорректная запись', self.lua.eval('WoWBestiary.networkStatus'))


if __name__ == '__main__':
    unittest.main()
