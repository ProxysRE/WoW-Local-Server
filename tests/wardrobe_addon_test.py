"""Run the actual addon on Lua 5.1 with a small Wrath UI/event fixture (pip install lupa)."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
frames={}; sent={}; clock=0
local methods={}
function methods:SetScript(k,v) self.scripts[k]=v end
function methods:CreateFontString() return CreateFrame("FontString") end
function methods:SetText(v) self.text=v end
function methods:GetText() return self.text or "" end
function methods:Show() self.shown=true end
function methods:Hide() self.shown=false end
function methods:IsShown() return self.shown end
function methods:GetFrameLevel() return 1 end
function methods:GetFacing() return 0 end
function methods:SetUnit(unit) assert(unit=="player") end
function methods:TryOn(item) assert(item:match("^item:%d+$")) end
local meta={__index=function(t,k)
    if methods[k] then return methods[k] end
    if k:match("^[A-Z]") then return function() end end
end}
function CreateFrame(kind,name,parent,template)
    local f=setmetatable({kind=kind,name=name,parent=parent,scripts={},shown=true},meta)
    frames[#frames+1]=f; if name then _G[name]=f end; return f
end
function GetLocale() return "ruRU" end
function GetTime() return clock end
function GetRealmName() return "Local" end
function UnitName() return "Tester" end
function GetCoinTextureString(n) return tostring(n) end
function ChatFrame_AddMessageEventFilter(...) end
function SendChatMessage(text,channel,language,target)
    assert(channel=="WHISPER" and target=="Tester"); sent[#sent+1]=text
end
function StaticPopup_Show(key,text) popup={data=nil}; return popup end
UIParent={}; UISpecialFrames={}; tinsert=table.insert; SlashCmdList={}
StaticPopupDialogs={}; CANCEL="Cancel"; ITEM_QUALITY_COLORS={[1]={r=1,g=1,b=1}}
GameTooltip=setmetatable({},meta)
function button(text)
    for _,f in ipairs(frames) do if f.kind=="Button" and f.text==text then return f end end
    error("Button missing: "..text)
end
function click(text) local f=button(text); f.scripts.OnClick(f,"LeftButton") end
function event(name,msg) for _,f in ipairs(frames) do if f.scripts.OnEvent then f.scripts.OnEvent(f,name,msg) end end end
function pump(n)
    for i=1,n do clock=clock+0.36; for _,f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f,0.36) end end end
end
function request(op)
    for i=#sent,1,-1 do local id=sent[i]:match("^%.wardrobe (%d+) "..op.."[%s]?"); if id then return id end end
    error("Missing request "..op)
end
function reply(id,kind,payload) event("CHAT_MSG_SYSTEM","ACW1:"..id..":"..kind..":"..(payload or "")) end
''')
lua.execute((ROOT / "addons/ACWardrobe/ACWardrobe.lua").read_text())
lua.execute(r'''
SlashCmdList.ACWARDROBE(); pump(3)
local state=request("STATE"); reply(state,"STATE","101:true:true:0:true")
reply(state,"SLOT","0:123:0"); reply(state,"DONE")
local list=request("LIST")
reply(list,"PAGE","0:0:1:1")
reply(list,"ITEM","456:900:1:1:false:NOT_COLLECTED:54657374"); reply(list,"DONE")
local tile
for _,f in ipairs(frames) do if type(f.row)=="table" and f.row.id==456 then tile=f; break end end
assert(tile,"Server appearance not rendered")
tile.scripts.OnClick(tile,"RightButton")
click("Рассчитать и применить"); pump(2)
assert(not sent[#sent]:find("QUOTE"),"Uncollected look staged")
reply(list,"PAGE","0:0:1:1") -- completed response must be ignored
click("Обновить"); pump(3); list=request("LIST")
reply(list,"PAGE","0:0:1:1")
reply(list,"ITEM","456:900:1:1:true:OK:54657374"); reply(list,"DONE")
tile.scripts.OnClick(tile,"RightButton"); click("Рассчитать и применить"); pump(1)
local quote=request("QUOTE")
click("Исходный вид слота"); reply(quote,"QUOTE","555:100:0:0")
assert(popup==nil,"Stale quote opened confirmation")
click("Рассчитать и применить"); pump(1); quote=request("QUOTE")
reply(quote,"QUOTE","556:100:0:0"); assert(popup.data==556)
StaticPopupDialogs.ACW_CONFIRM.OnAccept(popup); pump(1)
assert(sent[#sent]:find("APPLY 556$"),"Confirmation nonce not sent")
local before=#sent
for i=1,14 do event("PLAYER_EQUIPMENT_CHANGED") end
pump(3); assert(#sent==before+2,"Equipment refreshes not coalesced")
-- Slot button closures must keep their own slot under Lua 5.1.
click("Плечи"); pump(1); assert(sent[#sent]:find("LIST 2 "))
click("Накидка"); pump(1); assert(sent[#sent]:find("LIST 18 "))
print("Wardrobe Lua 5.1: protocol, ownership, stale quotes, confirmation, refresh coalescing and slot closures passed")
''')
