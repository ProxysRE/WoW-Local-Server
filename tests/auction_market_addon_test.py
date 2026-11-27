"""Run the actual quantity-purchase addon on Lua 5.1, including stale confirmations."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

ROOT = Path(__file__).resolve().parents[1]
lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
tinsert=table.insert;frames={}; sent={}; clock=0; StaticPopupDialogs={}; SlashCmdList={}; UISpecialFrames={}; UIParent={}; CANCEL="Cancel"
local methods={}
function methods:SetScript(k,v) self.scripts[k]=v end
function methods:CreateFontString() return CreateFrame("FontString") end
function methods:SetText(t) self.text=t;if self.scripts.OnTextChanged then self.scripts.OnTextChanged(self) end end
function methods:GetText() return self.text or "" end
function methods:Show() self.shown=true end
function methods:Hide() self.shown=false;if self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
local meta={__index=function(t,k) if methods[k] then return methods[k] end;if k:match("^[A-Z]") then return function() end end end}
function CreateFrame(kind,name,parent,template)
 local f=setmetatable({kind=kind,name=name,parent=parent,scripts={},shown=true},meta)
 frames[#frames+1]=f;if name then _G[name]=f end;return f
end
AuctionFrame=CreateFrame("Frame")
GameTooltip=CreateFrame("Frame")
function GetTime() return clock end
function GetCoinTextureString(v) return tostring(v) end
function UnitName() return "Tester" end
function UnitGUID() return "0x00000000000004d2" end
function SendAddonMessage(prefix,msg,channel,receiver)
 assert(prefix=="ACAM1" and channel=="WHISPER" and receiver=="Tester");sent[#sent+1]=msg
end
function StaticPopup_Show(key,text) popup={data=nil,key=key};return popup end
function StaticPopup_Hide() popup=nil end
function click(text)
 for _,f in ipairs(frames) do if f.kind=="Button" and f.text==text then f.scripts.OnClick(f);return end end
 error("missing button "..text)
end
function event(ev,...)
 for _,f in ipairs(frames) do if f.scripts.OnEvent then f.scripts.OnEvent(f,ev,...) end end
end
function pump() clock=clock+0.4;for _,f in ipairs(frames) do if f.scripts.OnUpdate then f.scripts.OnUpdate(f,0.4) end end end
function reply(t) event("CHAT_MSG_ADDON","ACAM1",ACAuctionMarket.pending.id.."|"..t,"WHISPER","Tester") end
''')
lua.execute((ROOT / "addons/ACAuctionMarket/ACAuctionMarket.lua").read_text())
lua.execute(r'''
event("AUCTION_HOUSE_SHOW");click("Товары");pump()
assert(sent[1]:match("^1|L|00000000000004d2|0|$"))
reply("P|0|0");reply("I|2770|436f70706572|200|10");reply("D")
local a=ACAuctionMarket
assert(#a.rows==1 and a.rows[1].name=="Copper")
a.buttons[1].scripts.OnClick(a.buttons[1]);click("Рассчитать");pump()
assert(sent[#sent]:match("|Q|.*|2770|20$"))
reply("Q|77|2770|20|200");assert(popup.data.token==77)
local old=popup.data
a.quantity:SetText("21");assert(a.quote==nil and popup==nil)
local before=#sent;StaticPopupDialogs.ACAM_CONFIRM.OnAccept({data=old});pump();assert(#sent==before)
a.quantity:SetText("20");click("Рассчитать");pump();reply("Q|78|2770|20|200")
StaticPopupDialogs.ACAM_CONFIRM.OnAccept(popup);pump();assert(sent[#sent]:match("|B|.*|78$"))
local count=#sent;StaticPopupDialogs.ACAM_CONFIRM.OnAccept({data=old});pump();assert(#sent==count)
reply("OK|20|200");assert(a.status.text:find("20") and a.pending==nil)
click("Рассчитать");pump();a.quantity:SetText("19");reply("Q|79|2770|20|200");assert(a.quote==nil)
a.quantity:SetText("20");click("Рассчитать");pump();reply("Q|80|2770|20|200")
local q=popup.data;event("AUCTION_HOUSE_CLOSED");count=#sent
StaticPopupDialogs.ACAM_CONFIRM.OnAccept({data=q});pump();assert(#sent==count and not a.opened)
print("Auction Market Lua 5.1: paging, quote, exact quantity, stale edit, replay and closed-auction guards passed")
''')
