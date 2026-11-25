"""Run the actual Group Builder addon in Lua 5.1, exercising menus and the command protocol."""
from pathlib import Path
from lupa.lua51 import LuaRuntime
ROOT = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
frames={}; sent={}; clock=0; menus={}
local methods={}
function methods:SetScript(k,v) self.scripts[k]=v end
function methods:CreateFontString() return CreateFrame("FontString") end
function methods:SetText(v) self.text=tostring(v) end
function methods:GetText() return self.text or "" end
function methods:Show() self.shown=true end
function methods:Hide() self.shown=false end
function methods:IsShown() return self.shown end
function methods:GetFrameLevel() return 1 end
function methods:GetWidth() return 1280 end
function methods:GetHeight() return 720 end
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
function UnitName() return "Tester" end
function ChatFrame_AddMessageEventFilter(...) end
function SendChatMessage(text,channel,language,target)
    assert(channel=="WHISPER" and target=="Tester" and #text<=255)
    sent[#sent+1]=text
end
function StaticPopup_Show(key) popup={}; return popup end
function EasyMenu(items) menus=items end
UIParent=CreateFrame("Frame"); UISpecialFrames={}; tinsert=table.insert; SlashCmdList={}
StaticPopupDialogs={}; CANCEL="Cancel"; ACCEPT="Accept"
function button(text)
    for _,f in ipairs(frames) do if f.kind=="Button" and f.text==text then return f end end
    error("Button missing: "..text)
end
function click(text) local f=button(text); f.scripts.OnClick(f,"LeftButton") end
function choose(text)
    for _,item in ipairs(menus) do if item.text==text then item.func(); return end end
    error("Menu entry missing: "..text)
end
function event(name,msg) for _,f in ipairs(frames) do if f.scripts.OnEvent then f.scripts.OnEvent(f,name,msg) end end end
function pump(n)
    for i=1,n do clock=clock+0.36; for _,f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f,0.36) end end end
end
function request(op)
    for i=#sent,1,-1 do local id=sent[i]:match("^%.gbuilder (%d+) "..op.."[%s]?"); if id then return id end end
    error("Missing request "..op)
end
function reply(id,kind,payload) event("CHAT_MSG_SYSTEM","ACG1:"..id..":"..kind..":"..(payload or "")) end
''')
lua.execute((ROOT / "addons/ACGroupBuilder/ACGroupBuilder.lua").read_text())
lua.execute(r'''
SlashCmdList.ACGROUPBUILDER()
-- Set the first slot to strict DPS / mage / arcane using actual menus.
click("Танк"); choose("ДД")
click("Любой класс"); choose("Маг")
click("Любая специализация"); choose("Тайная магия")
click("Предложить состав"); pump(5)
assert(sent[1]:find("CONFIG 5 1 80 0 0$"))
assert(sent[2]:find("0:3:8:1:0"),"Strict class/spec did not reach server")
local plan=request("PLAN")
reply(plan,"PLAN","100:5")
reply(plan,"ROW","0:123:8:1:3:70:123:0:4d616765:READY")
reply(plan,"ROW","1:124:5:2:2:70:125:1:507269657374:READY")
reply(plan,"DONE")
click("Пригласить выбранных"); assert(popup.data==100)
StaticPopupDialogs.ACGB_INVITE.OnAccept(popup); pump(1)
assert(sent[#sent]:find("INVITE 100$"))
local invite=request("INVITE"); reply(invite,"STARTED")
reply(invite,"STATUS","0:INVITED"); reply(invite,"STATUS","0:JOINED"); reply(invite,"FINISHED")
-- A roster change prevents reusing an earlier confirmation.
click("Предложить состав"); pump(5); plan=request("PLAN")
reply(plan,"PLAN","101:5"); reply(plan,"DONE")
click("Пригласить выбранных"); local stale=popup
click("ДД"); choose("Любая роль")
local count=#sent; StaticPopupDialogs.ACGB_INVITE.OnAccept(stale); pump(1)
assert(#sent==count,"Stale confirmation sent an invitation")
-- A 40-player configuration must be split into valid chat-sized batches.
click("Подземелье — 5"); click("Рейд — 10"); click("Рейд — 25")
click("Предложить состав"); pump(15)
assert(sent[#sent]:find("PLAN$"))
local found=false; for _,s in ipairs(sent) do if s:find("39:") then found=true end end
assert(found,"Last raid slot missing")
print("Group builder Lua 5.1: role/class/spec menus, command batches, server roster, invite confirmation and stale protection passed")
''')
