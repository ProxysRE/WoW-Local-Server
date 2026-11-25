"""Run the real region browser and its callbacks on a Wrath UI fixture."""
from pathlib import Path
import unittest
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]


class RegionUITests(unittest.TestCase):
    def test_scroll_search_and_selection(self):
        lua = LuaRuntime(unpack_returned_tuples=True)
        lua.execute('''
            frames={};now=0;sent={};SlashCmdList={};UISpecialFrames={}
            local methods={}
            function methods:SetScript(k,v) self.scripts[k]=v end
            function methods:CreateFontString() return CreateFrame("FontString") end
            function methods:CreateTexture() return CreateFrame("Texture") end
            function methods:SetText(v) self.text=v end
            function methods:GetText() return self.text or "" end
            function methods:Show() self.shown=true end
            function methods:Hide() self.shown=false end
            function methods:IsShown() return self.shown end
            function methods:GetFrameLevel() return 1 end
            function methods:SetSize(w,h) self.width=w;self.height=h end
            function methods:GetWidth() return self.width or 1280 end
            function methods:GetHeight() return self.height or 720 end
            function methods:SetPoint(...) self.anchor={...} end
            local meta={__index=function(t,k)
                if methods[k] then return methods[k] end
                if k:match("^[A-Z]") then return function() end end
            end}
            function CreateFrame(kind,name)
                local f=setmetatable({scripts={},shown=true,offset=0},meta)
                frames[#frames+1]=f
                if name then _G[name]=f end
                return f
            end
            function FauxScrollFrame_GetOffset(f) return f.offset end
            function FauxScrollFrame_SetOffset(f,n) f.offset=n end
            function FauxScrollFrame_Update(f,n,visible,height) f.total=n end
            function GetTime() return now end
            function UnitName() return "Sigma" end
            function SendAddonMessage(p,m,c,t) sent[#sent+1]=m end
            UIParent=CreateFrame("Frame");frames={}
            GameTooltip=CreateFrame("Tooltip");frames={}
        ''')
        for name in ("Data.lua", "Zones.lua", "Core.lua", "Network.lua", "Map.lua", "UI.lua"):
            lua.execute((ROOT / "addons/WoWBestiary" / name).read_text())
        lua.execute('''
            WoWBestiary.BuildUI()
            local A=WoWBestiary
            A.regionsLoaded=true;A.regionById={};A.zones={}
            for i=1,30 do
                local z={id=i,name="Локация "..i,category=0,mode=0,map=0,count=10}
                A.zones[i]=z;A.regionById[i]=z
            end
            A.zoneScroll.offset=5;A.Refresh()
        ''')
        self.assertEqual(lua.eval('WoWBestiary.zoneScroll.rows[1].zone.id'), 6)
        lua.execute('WoWBestiary.zoneScroll.rows[1].scripts.OnClick(WoWBestiary.zoneScroll.rows[1])')
        self.assertEqual(lua.eval('WoWBestiary.selectedZone'), 6)
        lua.execute('now=1;frames[2].scripts.OnUpdate()')
        self.assertEqual(lua.eval('sent[1]'), 'Q|2|1|Z|6|0')
        lua.execute('''
            local edit=WoWBestiary.regionSearch
            edit:SetText("ЛОКАЦИЯ 2");edit.scripts.OnTextChanged(edit,true)
        ''')
        self.assertEqual(lua.eval('WoWBestiary.zoneScroll.total'), 11)
        self.assertEqual(lua.eval('WoWBestiary.zoneScroll.offset'), 0)
        lua.execute('''
            local edit=WoWBestiary.regionSearch
            edit:SetText("не существующая зона");edit.scripts.OnTextChanged(edit,true)
        ''')
        self.assertIsNone(lua.eval('WoWBestiary.zoneScroll.rows[1].zone'))
        self.assertFalse(lua.eval('WoWBestiary.zoneScroll.rows[1]:IsShown()'))
        lua.execute('''
            WoWBestiary.ShowPositions({id=521,name="Тестовый редкий"},
                {{status="OK"},{map=0,zone=12,x=25,y=75,texture="Elwynn",name="Элвинн"}})
        ''')
        self.assertTrue(lua.eval('WoWBestiary.mapViewer.pins[1]:IsShown()'))
        self.assertAlmostEqual(lua.eval('WoWBestiary.mapViewer.pins[1].anchor[4]'),
                               lua.eval('WoWBestiary.mapViewer.canvas:GetWidth()') * .25)
        lua.execute('now=31;WoWBestiary.mapViewer.scripts.OnUpdate(WoWBestiary.mapViewer)')
        self.assertFalse(lua.eval('WoWBestiary.mapViewer.pins[1]:IsShown()'))
        lua.execute('WoWBestiary.ShowPositions({id=521,name="Тест"},{{status="NO_DATA"}})')
        self.assertIsNone(lua.eval('WoWBestiary.mapViewer.stamp'))
        self.assertFalse(lua.eval('WoWBestiary.mapViewer.pins[1]:IsShown()'))


if __name__ == '__main__':
    unittest.main()
