"""Exercise the actual client-only set tooltip repair under Wrath's Lua 5.1."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "addons/ACWardrobe/SetTooltip.lua").read_text()
lua = LuaRuntime(unpack_returned_tuples=True)
lua.globals().Repair = lua.execute(source[:source.index("local function Update(")] + "\nreturn Repair")
lua.execute('''
local function tooltip(count)
    return {
        {text="Настоящие наплечники",color={1,1,1}},
        {text="Комплект ("..count.."/5)",color={1,0.8,0}},
        {text="Настоящие наплечники",color={0.5,0.5,0.5}},
        {text="Шлем",color={0,1,0}}, {text="Грудь",color={0,1,0}},
        {text="Перчатки",color={0,1,0}}, {text="Ноги",color={0.5,0.5,0.5}},
        {text="(2) Комплект: бонус",color={0,1,0}},
        {text="(4) Комплект: бонус",color={0.5,0.5,0.5}},
    }
end
local worn={['Настоящие наплечники']=true,['Шлем']=true,['Грудь']=true,['Перчатки']=true}
local t=tooltip(3)
assert(Repair(t,worn,"Настоящие наплечники"))
assert(t[2].text=="Комплект (4/5)" and t[3].color[2]==1 and t[9].color[2]==1)
-- A borrowed appearance from another set must not grant its set membership.
t=tooltip(4); worn['Настоящие наплечники']=nil
assert(Repair(t,worn,"Настоящие наплечники"))
assert(t[2].text=="Комплект (3/5)" and t[9].color[2]==0.5)
-- Leave red profession restrictions and the associated inactive bonus intact.
t=tooltip(3); worn['Настоящие наплечники']=true
t[10]={text="Требуется профессия",color={1,0,0}}
Repair(t,worn,"Настоящие наплечники")
assert(t[2].text=="Комплект (4/5)" and t[9].color[2]==0.5 and t[10].color[1]==1)
-- Do not reinterpret arbitrary fractions or a different tooltip's set section.
t=tooltip(3); assert(not Repair(t,worn,"Другой предмет"))
assert(t[2].text=="Комплект (3/5)")
t={{text="Задание (1/3)"},{text="A"},{text="B"},{text="C"}}
assert(not Repair(t,worn,"Наплечники"))
print("Set tooltip Lua 5.1: real membership, 4-piece bonus, reverse correction and red requirements passed")
''')
# Load the complete hook integration as well, including inspecting another player.
lua.execute('''
local function frame()
    return {scripts={}, HookScript=function(self,k,fn) self.scripts[k]=fn end}
end
GameTooltip=frame(); ItemRefTooltip=frame(); ShoppingTooltip1=frame(); ShoppingTooltip2=frame()
function hooksecurefunc(self,key,fn) self.inventoryHook=fn end
function UnitIsUnit(a,b) return a==b end
function GetInventoryItemLink() error("Another player's inventory tooltip was modified") end
''')
lua.execute(source)
lua.execute('''
GameTooltip.scripts.OnTooltipSetItem(GameTooltip)
GameTooltip.inventoryHook(GameTooltip,"target")
GameTooltip.scripts.OnUpdate(GameTooltip)
assert(GameTooltip.acwSetPending==nil)
GameTooltip.scripts.OnTooltipCleared(GameTooltip)
assert(GameTooltip.acwSetUnit==nil)
print("Tooltip integration: inspection exclusion and context clearing passed")
''')
