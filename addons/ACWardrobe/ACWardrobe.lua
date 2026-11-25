-- SPDX-License-Identifier: AGPL-3.0-or-later
local W = {seq=0, revision=0, latest={}, queue={}, pending={}, slots={}, plan={}, rows={}, slot=0, page=0,
    mode="collected", search="", sets={}, near=false, sticky=false, hidden=false}
local ru = GetLocale() == "ruRU"
local function T(a,b) if ru then return a else return b end end
local names = {
    [0]=T("Голова","Head"), [2]=T("Плечи","Shoulders"), [3]=T("Рубашка","Shirt"),
    [4]=T("Грудь","Chest"), [5]=T("Пояс","Waist"), [6]=T("Ноги","Legs"),
    [7]=T("Ступни","Feet"), [8]=T("Запястья","Wrists"), [9]=T("Кисти рук","Hands"),
    [14]=T("Плащ","Back"), [15]=T("Правая рука","Main hand"), [16]=T("Левая рука","Off hand"),
    [17]=T("Дальний бой","Ranged"), [18]=T("Накидка","Tabard")}
local order = {0,2,3,4,5,6,7,8,9,14,15,16,17,18}
local reason = {
    NOT_COLLECTED=T("Облик не собран","Not collected"), REQUIREMENTS=T("Не выполнены требования","Requirements not met"),
    INCOMPATIBLE=T("Несовместимо с надетой вещью","Incompatible with equipped item"), EMPTY=T("В слоте нет вещи","Nothing equipped"),
    NPC=T("Нужен трансмогрификатор рядом; выйдите из боя","Visit the transmogrifier out of combat"),
    INVALID_PLAN=T("Наряд недоступен: обновите коллекцию и проверьте слоты","Outfit unavailable; refresh and check slots"),
    QUOTE_EXPIRED=T("Расчёт устарел или экипировка изменилась — рассчитайте снова","Quote expired or equipment changed; request again"),
    MONEY=T("Недостаточно денег","Not enough money"), TOKENS=T("Недостаточно жетонов","Not enough tokens"),
    DISABLED=T("Система коллекции выключена на сервере","Server collection system is disabled"),
    RATE=T("Слишком частые запросы — повторите","Too many requests; retry"),
    HIDDEN=T("Скрытие слотов выключено на сервере","Server does not allow hiding slots")}
local function Bool(s) return s == "1" or s == "true" end
local function Hex(s)
    if not s or s == "" then return "-" end
    return (s:gsub(".", function(c) return string.format("%02x",string.byte(c)) end))
end
local function Unhex(s)
    if s == "-" then return "" end
    return (s or ""):gsub("%x%x",function(c) return string.char(tonumber(c,16)) end)
end
local function Split(s)
    local out = {}
    for part in (s..":"):gmatch("(.-):") do out[#out+1]=part end
    return out
end
local function Copy(src) local dst={} for k,v in pairs(src) do dst[k]=v end return dst end
local function Pack(plan)
    local out={}
    for _,slot in ipairs(order) do if plan[slot] ~= nil then out[#out+1]=slot.."="..plan[slot] end end
    return table.concat(out,",")
end
local function Unpack(s)
    local out={}
    for slot,id in (s or ""):gmatch("(%d+)=(%d+)") do out[tonumber(slot)]=tonumber(id) end
    return out
end
local function Request(op,args,kind)
    -- Replace queued refreshes after rapid equipment/filter changes.
    if op=="STATE" or op=="LIST" or op=="SETS" or op=="QUOTE" then
        for i=#W.queue,1,-1 do
            if W.queue[i].op==op then W.pending[W.queue[i].id]=nil; table.remove(W.queue,i) end
        end
    end
    W.seq=W.seq+1
    local id=W.seq
    W.latest[op]=id
    W.pending[id]={kind=kind or op,revision=W.revision}
    W.queue[#W.queue+1]={id=id,op=op,text=".wardrobe "..id.." "..op..(args and (" "..args) or "")}
    return id
end
local function Invalidate()
    W.revision=W.revision+1; W.quote=nil
end
local f=CreateFrame("Frame","ACWardrobeFrame",UIParent)
f:SetSize(1000,650); f:SetPoint("CENTER"); f:SetFrameStrata("DIALOG")
f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=8,right=8,top=8,bottom=8}})
f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart",f.StartMoving); f:SetScript("OnDragStop",f.StopMovingOrSizing)
f:Hide(); tinsert(UISpecialFrames,"ACWardrobeFrame")
local function Label(parent,size,x,y,text)
    local l=parent:CreateFontString(nil,"OVERLAY",size or "GameFontNormal")
    l:SetPoint("TOPLEFT",x,y); l:SetText(text or ""); return l
end
local function Button(parent,text,x,y,width,fn)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetSize(width or 100,24); b:SetPoint("TOPLEFT",x,y); b:SetText(text); b:SetScript("OnClick",fn); return b
end
Label(f,"GameFontNormalLarge",24,-20,T("Гардероб","Wardrobe"))
local close=CreateFrame("Button",nil,f,"UIPanelCloseButton"); close:SetPoint("TOPRIGHT",-5,-5)
local status=Label(f,"GameFontHighlightSmall",180,-587,""); status:SetWidth(790); status:SetJustifyH("LEFT")
local function Status(s) status:SetText(s) end
local title=Label(f,"GameFontNormal",180,-59,"")
local pageLabel=Label(f,"GameFontHighlightSmall",370,-550,"")
local preview=CreateFrame("DressUpModel",nil,f); preview:SetSize(255,425); preview:SetPoint("TOPLEFT",720,-88)
preview:SetUnit("player"); preview:EnableMouse(true)
preview:SetScript("OnMouseDown",function(self) self.drag=true; self.x=GetCursorPosition() end)
preview:SetScript("OnMouseUp",function(self) self.drag=false end)
preview:SetScript("OnUpdate",function(self)
    if self.drag then local x=GetCursorPosition(); self:SetFacing((self:GetFacing() or 0)+(x-self.x)/100); self.x=x end
end)
preview:EnableMouseWheel(true)
preview:SetScript("OnMouseWheel",function(self,d) self.z=(self.z or 0)+d*0.1; self:SetPosition(0,0,self.z) end)
local function DrawModel(model,plan,solo)
    model:SetUnit("player")
    if solo then model:Undress() end
    for _,slot in ipairs(order) do
        local entry=plan[slot]
        if entry ~= nil then
            if entry == 0 then entry=W.slots[slot] and W.slots[slot].item or 0 end
            if entry == 1 then
                if model.UndressSlot then pcall(model.UndressSlot,model,slot+1) end
            elseif entry > 1 then model:TryOn("item:"..entry) end
        end
    end
end
local function UpdatePreview()
    local plan={}
    for _,slot in ipairs(order) do
        local item=W.slots[slot]
        if item then plan[slot]=item.fake ~= 0 and item.fake or item.item end
    end
    for slot,entry in pairs(W.plan) do plan[slot]=entry end
    DrawModel(preview,plan,false)
end
local function AddLook(slot,id)
    W.plan[slot]=id; Invalidate(); UpdatePreview()
    local count=0 for _ in pairs(W.plan) do count=count+1 end
    Status(T("В примерке слотов: ","Staged slots: ")..count)
end
for i,slot in ipairs(order) do
    Button(f,names[slot],22,-58-(i-1)*33,138,function()
        W.slot=slot; W.page=0; W.Load()
    end)
end
local tiles={}
for i=1,12 do
    local tile=CreateFrame("Button",nil,f)
    tile:SetSize(128,140); tile:SetPoint("TOPLEFT",180+((i-1)%4)*132,-93-math.floor((i-1)/4)*148)
    tile:SetBackdrop({bgFile="Interface\\Buttons\\WHITE8X8"}); tile:SetBackdropColor(0.08,0.08,0.10,0.9)
    tile.model=CreateFrame("DressUpModel",nil,tile); tile.model:SetPoint("TOP",0,-4); tile.model:SetSize(122,105)
    tile.model:EnableMouse(false)
    tile.name=Label(tile,"GameFontHighlightSmall",4,-107,""); tile.name:SetWidth(120); tile.name:SetHeight(29)
    tile:SetScript("OnClick",function(self,button)
        if not self.row then return end
        if button == "RightButton" then
            if self.row.why == "OK" then AddLook(W.slot,self.row.id) else Status(reason[self.row.why] or self.row.why) end
        else
            local p=Copy(W.plan); p[W.slot]=self.row.id; DrawModel(preview,p,false)
            Status(T("Примерка. Правой кнопкой — добавить в наряд.","Preview. Right-click to stage in outfit."))
        end
    end)
    tile:RegisterForClicks("LeftButtonUp","RightButtonUp")
    tile:SetScript("OnEnter",function(self)
        if not self.row then return end
        GameTooltip:SetOwner(self,"ANCHOR_RIGHT"); GameTooltip:SetHyperlink("item:"..self.row.id)
        GameTooltip:AddLine(self.row.owned and T("Облик собран","Collected") or T("Облик не собран","Not collected"),0.6,0.8,1)
        if self.row.why ~= "OK" then GameTooltip:AddLine(reason[self.row.why] or self.row.why,1,0.35,0.3,true) end
        GameTooltip:AddLine("ID: "..self.row.id..T("; уровень: ","; level: ")..self.row.level,0.7,0.7,0.7)
        GameTooltip:Show()
    end)
    tile:SetScript("OnLeave",function() GameTooltip:Hide() end)
    tiles[i]=tile
end
local function Render()
    for i,tile in ipairs(tiles) do
        tile.row=W.rows[i]
        if tile.row then
            tile:Show(); tile.name:SetText(tile.row.name)
            local color=ITEM_QUALITY_COLORS[tile.row.quality] or ITEM_QUALITY_COLORS[1]
            tile.name:SetTextColor(color.r,color.g,color.b)
            DrawModel(tile.model,{[W.slot]=tile.row.id},true)
            tile:SetAlpha(tile.row.owned and 1 or 0.5)
        else tile:Hide() end
    end
    title:SetText(names[W.slot]..T(" — обликов: "," — appearances: ")..(W.total or 0))
    pageLabel:SetText((W.page+1).." / "..(W.pages or 1))
end
local search=CreateFrame("EditBox",nil,f,"InputBoxTemplate"); search:SetSize(205,24); search:SetPoint("TOPLEFT",430,-52)
search:SetAutoFocus(false); search:SetMaxBytes(60)
search:SetScript("OnEnterPressed",function(self) W.search=self:GetText(); self:ClearFocus(); W.page=0; W.Load() end)
search:SetScript("OnEscapePressed",function(self) self:ClearFocus() end)
local modes={{"collected",T("Собрано","Collected")},{"usable",T("Можно применить","Usable")},{"all",T("Все облики","All looks")}}
local modeIndex=1
local filter
filter=Button(f,modes[1][2],180,-51,185,function()
    modeIndex=modeIndex%#modes+1; W.mode=modes[modeIndex][1]; filter:SetText(modes[modeIndex][2]); W.page=0; W.Load()
end)
Button(f,T("Найти","Find"),641,-51,65,function() W.search=search:GetText(); W.page=0; W.Load() end)
Button(f,"<",180,-545,55,function() if W.page>0 then W.page=W.page-1; W.Load() end end)
Button(f,">",646,-545,55,function() if W.page+1<(W.pages or 1) then W.page=W.page+1; W.Load() end end)
Button(f,T("Обновить","Refresh"),22,-535,138,function() Request("STATE"); W.Load() end)
Button(f,T("Исходный вид слота","Reset slot"),720,-510,255,function() AddLook(W.slot,0) end)
local hide=Button(f,T("Скрыть слот","Hide slot"),720,-538,125,function()
    if W.hidden then AddLook(W.slot,1) else Status(reason.HIDDEN) end
end)
Button(f,T("Сброс примерки","Clear preview"),850,-538,125,function() W.plan={}; Invalidate(); UpdatePreview() end)
local apply=Button(f,T("Рассчитать и применить","Quote and apply"),720,-576,255,function()
    local plan=Pack(W.plan)
    if plan=="" then Status(T("Сначала добавьте облики правой кнопкой.","Right-click looks to stage an outfit first.")); return end
    Request("QUOTE",plan)
end)
function W.Load()
    W.list=Request("LIST",W.slot.." "..W.page.." "..W.mode.." "..Hex(W.search),"LIST")
    W.rows={}; Render(); Status(T("Загрузка…","Loading…"))
end

-- New named outfits live in this addon's SavedVariables; existing server sets are imported read-only.
local sets=CreateFrame("Frame",nil,f); sets:SetSize(550,420); sets:SetPoint("CENTER")
sets:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=24,insets={left=6,right=6,top=6,bottom=6}})
sets:SetFrameLevel(f:GetFrameLevel()+20); sets:EnableMouse(true); sets:Hide()
Label(sets,"GameFontNormalLarge",20,-18,T("Сохранённые наряды","Saved outfits"))
local setName=CreateFrame("EditBox",nil,sets,"InputBoxTemplate"); setName:SetSize(260,24); setName:SetPoint("TOPLEFT",25,-53)
setName:SetAutoFocus(false); setName:SetMaxBytes(60)
local setRows={}; local setPage=0
local function Db()
    ACWardrobeDB=ACWardrobeDB or {version=1,characters={}}
    ACWardrobeDB.characters=ACWardrobeDB.characters or {}
    local key=GetRealmName()..":"..UnitName("player")
    ACWardrobeDB.characters[key]=ACWardrobeDB.characters[key] or {}
    return ACWardrobeDB.characters[key]
end
local function SetRows()
    local list={}
    for name,plan in pairs(Db()) do list[#list+1]={name=name,plan=plan} end
    for _,s in pairs(W.sets) do list[#list+1]={name="[NPC] "..s.name,plan=s.plan,legacy=true} end
    table.sort(list,function(a,b) return a.name<b.name end)
    setPage=math.min(setPage,math.max(0,math.ceil(#list/8)-1))
    for i,row in ipairs(setRows) do
        row.data=list[setPage*8+i]
        if row.data then row:Show(); row.text:SetText(row.data.name) else row:Hide() end
    end
end
Button(sets,T("Сохранить примерку","Save preview"),305,-51,220,function()
    local name=setName:GetText():gsub("^%s+",""):gsub("%s+$","")
    if name=="" then return end
    local plan={}
    for _,slot in ipairs(order) do
        local item=W.slots[slot]
        if item and item.item>0 then plan[slot]=item.fake ~= 0 and item.fake or 0 end
    end
    for slot,entry in pairs(W.plan) do plan[slot]=entry end
    Db()[name]=plan; setName:ClearFocus(); SetRows()
end)
for i=1,8 do
    local row=CreateFrame("Frame",nil,sets); row:SetSize(510,32); row:SetPoint("TOPLEFT",20,-90-(i-1)*34)
    row.text=Label(row,"GameFontHighlightSmall",3,-6,""); row.text:SetWidth(265); row.text:SetJustifyH("LEFT")
    Button(row,T("Примерить","Try on"),280,0,100,function()
        if row.data then W.plan=Copy(row.data.plan); Invalidate(); UpdatePreview(); sets:Hide() end
    end)
    Button(row,T("Удалить","Delete"),385,0,115,function()
        if row.data and not row.data.legacy then Db()[row.data.name]=nil; SetRows() end
    end)
    setRows[i]=row
end
Button(sets,"<",20,-369,50,function() setPage=math.max(0,setPage-1); SetRows() end)
Button(sets,">",80,-369,50,function() setPage=setPage+1; SetRows() end)
Button(sets,T("Закрыть","Close"),415,-369,110,function() sets:Hide() end)
Button(f,T("Наряды","Outfits"),22,-575,138,function()
    W.sets={}; Request("SETS"); SetRows(); sets:Show()
end)

StaticPopupDialogs.ACW_CONFIRM={
    text="%s",button1=T("Применить","Apply"),button2=CANCEL,timeout=30,whileDead=false,hideOnEscape=true,
    OnAccept=function(self)
        if W.quote and W.quote.nonce==self.data then Request("APPLY",tostring(self.data)); W.quote=nil end
    end,
}
local events=CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_SYSTEM"); events:RegisterEvent("PLAYER_EQUIPMENT_CHANGED"); events:RegisterEvent("GOSSIP_SHOW")
events:SetScript("OnEvent",function(self,event,msg)
    if event=="PLAYER_EQUIPMENT_CHANGED" then
        Invalidate()
        if f:IsShown() then Request("STATE"); W.Load() end
        return
    end
    if event=="GOSSIP_SHOW" then
        local guid=UnitGUID("npc") or ""
        local entry=tonumber(guid:sub(7,12),16)
        if entry==190010 then W.Open(); if GossipFrame then GossipFrame:Hide() end end
        return
    end
    if type(msg)~="string" or msg:sub(1,5)~="ACW1:" then return end
    local p=Split(msg); local request=tonumber(p[2]); local pending=W.pending[request]
    if not pending then return end
    if pending.kind~="APPLY" and W.latest[pending.kind]~=request then W.pending[request]=nil; return end
    if pending.kind=="QUOTE" and pending.revision~=W.revision then W.pending[request]=nil; return end
    local kind=p[3]
    if kind=="ERROR" then Status(reason[p[4]] or p[4]); W.pending[request]=nil
    elseif kind=="STATE" then W.near=Bool(p[5]); W.sticky=Bool(p[6]); W.hidden=Bool(p[8]); W.slots={}
    elseif kind=="SLOT" then W.slots[tonumber(p[4])]={item=tonumber(p[5]),fake=tonumber(p[6])}
    elseif kind=="PAGE" and request==W.list then W.page=tonumber(p[5]); W.pages=tonumber(p[6]); W.total=tonumber(p[7]); W.rows={}
    elseif kind=="ITEM" and request==W.list then
        W.rows[#W.rows+1]={id=tonumber(p[4]),display=tonumber(p[5]),quality=tonumber(p[6]),level=tonumber(p[7]),owned=Bool(p[8]),why=p[9],name=Unhex(p[10])}
    elseif kind=="SET" then W.sets[p[4]]={name=Unhex(p[5]),plan=Unpack(p[6])}
    elseif kind=="QUOTE" then
        W.quote={nonce=tonumber(p[4])}
        local price=GetCoinTextureString(tonumber(p[5]))
        local tokens=tonumber(p[6]); if tokens>0 then price=price.." + "..tokens..T(" жетонов (ID "," tokens (ID ")..p[7]..")" end
        local popup=StaticPopup_Show("ACW_CONFIRM",T("Применить наряд?\nСтоимость: ","Apply outfit?\nCost: ")..price)
        if popup then popup.data=W.quote.nonce end
        W.pending[request]=nil
    elseif kind=="APPLIED" then
        W.plan={}; W.pending[request]=nil; Request("STATE"); W.Load()
    elseif kind=="DONE" then
        if pending.kind=="LIST" and request==W.list then Render(); Status(W.sticky and T("Коллекция получена с сервера. Облики сохраняются по слотам.","Server collection loaded. Appearances persist by slot.") or T("Для сохранения по слотам нужен EnablePlayerSettings = 1.","Slot persistence requires EnablePlayerSettings = 1.")) end
        if pending.kind=="STATE" then UpdatePreview() end
        if pending.kind=="SETS" then SetRows() end
        W.pending[request]=nil
    end
end)
ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM",function(self,event,msg)
    return type(msg)=="string" and msg:sub(1,5)=="ACW1:"
end)
local tick=0
events:SetScript("OnUpdate",function(self,elapsed)
    tick=tick+elapsed
    if tick<0.35 then return end
    tick=0
    local send=table.remove(W.queue,1)
    if send then
        W.pending[send.id].created=GetTime()
        SendChatMessage(send.text,"WHISPER",nil,UnitName("player"))
    end
    for id,pending in pairs(W.pending) do
        if pending.created and GetTime()-pending.created>20 then
            W.pending[id]=nil
            if f:IsShown() then Status(T("Нет ответа. Нужен сервер с модулем AC Wardrobe.","No response. Install the matching AC Wardrobe server update.")) end
        end
    end
end)
function W.Open()
    f:Show(); W.plan={}; Invalidate(); W.search=search:GetText(); Request("STATE"); W.Load()
end
SLASH_ACWARDROBE1="/wardrobe"; SLASH_ACWARDROBE2="/acw"; SLASH_ACWARDROBE3="/гардероб"
SlashCmdList.ACWARDROBE=function() if f:IsShown() then f:Hide() else W.Open() end end
