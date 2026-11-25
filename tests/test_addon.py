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
            self.lua.execute((ROOT / "addons/WoWBestiary" / name).read_text())
        self.lua.execute('''
            WoWBestiary.regionsLoaded=true
            WoWBestiary.regionById={[10]={id=10,name="Сумеречный лес",category=0,mode=0}}
            WoWBestiary.zones={WoWBestiary.regionById[10]}
        ''')
        # Compile every addon chunk under the actual target language version.
        for path in (ROOT / "addons/WoWBestiary").glob("*.lua"):
            self.lua.compile(path.read_text())

    def event(self, msg, sender="Sigma"):
        self.lua.globals().message = msg
        self.lua.globals().sender = sender
        self.lua.execute('frames[2].scripts.OnEvent(frames[2],"CHAT_MSG_ADDON","WBE2",message,"WHISPER",sender)')

    def tick(self, t):
        self.lua.globals().now = t
        self.lua.execute('frames[2].scripts.OnUpdate()')

    def test_cpp_response_and_lazy_loot(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        frames = subprocess.check_output(["/tmp/bestiary-protocol-test"], text=True).rstrip('\0').split('\0')
        for frame in reversed(frames):
            self.event(frame.split("\t", 1)[1])
        self.assertTrue(self.lua.eval('WoWBestiary.data.zones[10]~=nil'))
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
        self.event('1|1|1|H|Z|10|0|2\nM|521|Spoof|23|23|4|3|1|521', 'OtherPlayer')
        self.assertFalse(bool(self.lua.eval('WoWBestiary.data.zones[10]~=nil')))
        payload = 'H|Z|10|0|2\nM|521|Лупос|23|23|4|3|1|521'
        self.event('1|2|2|' + payload[20:])
        self.event('1|2|2|' + payload[20:])
        self.event('1|1|2|' + payload[:20])
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 0)
        self.tick(2)
        self.event('2|1|1|H|Z|10|1|2\nM|574|Нараксис|27|27|4|1|1|574')
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 2)

    def test_timeout_and_stale_response(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.tick(12)
        self.assertIn('Нет ответа', self.lua.eval('WoWBestiary.networkStatus'))
        self.lua.execute('WoWBestiary.ReloadServer()')
        self.tick(13)
        self.event('1|1|1|H|Z|10|0|1')
        self.assertFalse(bool(self.lua.eval('WoWBestiary.data.zones[10]~=nil')))
        self.event('2|1|1|H|L|0|0|1\nL|10|0|10|0|0|Сумеречный лес|140|0|0')
        self.tick(14)
        self.event('3|1|1|H|Z|10|0|1')
        self.assertTrue(self.lua.eval('WoWBestiary.data.zones[10]~=nil'))

    def test_bounded_frame_count(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.event('1|1|999999|junk')
        self.assertIn('Ошибка передачи', self.lua.eval('WoWBestiary.networkStatus'))

    def test_selection_cancels_old_detail(self):
        self.lua.execute('WoWBestiary.RequestZone()')
        self.tick(1)
        self.event('1|1|1|H|Z|10|0|1\nM|521|Лупос|23|23|4|3|1|521\nM|574|Нараксис|27|27|4|1|1|574')
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
        self.event('1|1|1|H|Z|10|0|1\nM|521|Лупос|23|23|4|3|1|521\nM|bad|Broken|x|x')
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)'), 0)
        self.assertIn('Некорректная запись', self.lua.eval('WoWBestiary.networkStatus'))

    def test_regions_and_instance_mode_select_effective_template(self):
        self.lua.execute('WoWBestiary.regionsLoaded=false;WoWBestiary.RequestZone()')
        self.tick(1)
        self.assertEqual(self.lua.eval('sent[1][2]'), 'Q|2|1|L|0|0')
        self.event('1|1|1|H|L|0|0|1\nL|10|0|10|0|0|Сумеречный лес|140|0|0\n'
                   'L|2147488699|631|0|5|3|Цитадель Ледяной Короны|80|2|25')
        self.tick(2)
        self.assertEqual(self.lua.eval('sent[2][2]'), 'Q|2|2|Z|10|0')
        self.event('2|1|1|H|Z|10|0|1\nM|521|Лупос|23|23|4|3|1|521')
        self.lua.execute('WoWBestiary.SelectZone(2147488699)')
        self.tick(3)
        self.assertEqual(self.lua.eval('sent[3][2]'), 'Q|2|3|Z|2147488699|0')
        self.event('3|1|1|H|Z|2147488699|0|1\nM|900|Тестовый босс|83|83|3|1|6|800')
        self.assertEqual(self.lua.eval('WoWBestiary.GetZoneMobs(2147488699)[1].base'), 800)
        self.lua.execute('WoWBestiary.RequestDetail(WoWBestiary.GetZoneMobs(2147488699)[1])')
        self.tick(4)
        self.assertEqual(self.lua.eval('sent[4][2]'), 'Q|2|4|D|900|0')
        self.event('4|1|1|H|D|900|0|1')
        self.assertTrue(self.lua.eval('WoWBestiary.GetZoneMobs(2147488699)[1].detailLoaded'))
        self.assertFalse(bool(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].detailLoaded')))

    def test_zone_cache_is_bounded_and_evicts_oldest(self):
        self.lua.execute('WoWBestiary.regionById={}; for i=1,8 do WoWBestiary.regionById[i]={id=i} end')
        for i in range(1, 9):
            self.lua.execute(f'WoWBestiary.SelectZone({i})')
            self.tick(i)
            self.event(f'{i}|1|1|H|Z|{i}|0|1')
        self.assertIsNone(self.lua.eval('WoWBestiary.data.zones[1]'))
        self.assertIsNone(self.lua.eval('WoWBestiary.data.zones[2]'))
        self.assertTrue(self.lua.eval('WoWBestiary.data.zones[8]~=nil'))
        self.assertEqual(self.lua.eval('(function() local n=0;for _ in pairs(WoWBestiary.data.zones) do n=n+1 end;return n end)()'), 6)

    def test_location_search_and_raid_labels(self):
        self.lua.execute('''
            WoWBestiary.zones={
                {id=1,name="Огненные Недра",category=5,capacity=40,mode=0},
                {id=2,name="Цитадель Ледяной Короны",category=5,capacity=25,mode=3},
                {id=3,name="Борейская тундра",category=3,capacity=0,mode=0},
            }
            WoWBestiary.category=5;WoWBestiary.regionQuery="ЦИТАДЕЛЬ"
        ''')
        self.assertEqual(self.lua.eval('#WoWBestiary.GetVisibleZones()'), 1)
        self.assertIn('25 гер.', self.lua.eval('WoWBestiary.RegionName(WoWBestiary.GetVisibleZones()[1])'))
        self.assertIn('40 об.', self.lua.eval('WoWBestiary.RegionName(WoWBestiary.zones[1])'))
        self.lua.execute('WoWBestiary.category=3;WoWBestiary.regionQuery=""')
        self.assertEqual(self.lua.eval('WoWBestiary.GetVisibleZones()[1].id'), 3)

    def test_detail_cache_is_bounded(self):
        self.lua.execute('''
            local mobs={}
            for i=1,18 do mobs[i]={id=i,loot={},skin={},pick={}} end
            WoWBestiary.data.zones[10]={mobs=mobs}
        ''')
        for i in range(1, 19):
            self.lua.execute(f'WoWBestiary.RequestDetail(WoWBestiary.GetZoneMobs(10)[{i}])')
            self.tick(i)
            self.event(f'{i}|1|1|H|D|{i}|0|1\nI|loot|99|Предмет|2|25|0|note')
        self.assertFalse(bool(self.lua.eval('WoWBestiary.GetZoneMobs(10)[1].detailLoaded')))
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)[1].loot'), 0)
        self.assertTrue(self.lua.eval('WoWBestiary.GetZoneMobs(10)[18].detailLoaded'))
        self.assertEqual(self.lua.eval('#WoWBestiary.GetZoneMobs(10)[18].loot'), 1)


if __name__ == '__main__':
    unittest.main()
