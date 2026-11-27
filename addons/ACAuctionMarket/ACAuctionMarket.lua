local A = {rows={}, page=0, serial=0, generation=0, opened=false, lastSend=-1}
ACAuctionMarket = A
local PREFIX = "ACAM1"
local function hex(s) return (s:gsub(".", function(c) return string.format("%02x", string.byte(c)) end)) end
local function unhex(s) return (s:gsub("%x%x", function(h) return string.char(tonumber(h,16)) end)) end
local function split(s)
    local t={}; for v in (s.."|"):gmatch("(.-)|") do t[#t+1]=v end; return t
end
local function money(n) return GetCoinTextureString(tonumber(n) or 0) end
local errors={NOT_ENOUGH_STOCK="Недостаточно товара. Обновите список.",
    NOT_ENOUGH_MONEY="Недостаточно золота.", QUOTE_EXPIRED="Расчёт устарел. Рассчитайте покупку ещё раз.",
    OFFERS_CHANGED="Предложения изменились. Рассчитайте покупку ещё раз.",
    AT_AUCTIONEER_ONLY="Подойдите к аукционисту и откройте аукцион.",
    WAIT="Подождите немного и повторите запрос.", ITEM_ERROR="Не удалось подготовить товары.",
    BAD_SEARCH="Не удалось прочитать текст поиска."}
local function status(text) A.status:SetText(text) end
local function invalidate()
    A.generation=A.generation+1; A.quote=nil
    if StaticPopup_Hide then StaticPopup_Hide("ACAM_CONFIRM") end
end
local function request(kind, tail)
    if not A.opened or not A.npc then status("Откройте аукцион у выбранного аукциониста."); return end
    if A.pending or A.queued then status("Дождитесь ответа сервера."); return end
    A.serial=A.serial%1000000000+1
    A.queued={id=A.serial, kind=kind, tail=tail, generation=A.generation}
    status("Запрос…")
end
local function refresh()
    for i,b in ipairs(A.buttons) do
        local row=A.rows[i]; b.row=row
        if row then
            b.name:SetText(row.name)
            b.stock:SetText(tostring(row.stock))
            b.price:SetText(money(row.price))
            b:Show()
        else b:Hide() end
    end
    A.pageLabel:SetText("Страница "..(A.page+1))
end
local function list(page)
    if A.pending or A.queued then return end
    invalidate(); A.selected=nil; A.selectedText:SetText("Выберите товар")
    A.page=page or 0; A.rows={}; refresh()
    request("L",A.page.."|"..hex(A.search:GetText() or ""))
end
local function label(parent,text,x,y,width)
    local f=parent:CreateFontString(nil,"OVERLAY","GameFontNormal")
    f:SetPoint("TOPLEFT",x,y); if width then f:SetWidth(width); f:SetJustifyH("LEFT") end
    f:SetText(text); return f
end
local function button(parent,text,x,y,w,action)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetSize(w or 110,24); b:SetPoint("TOPLEFT",x,y); b:SetText(text); b:SetScript("OnClick",action); return b
end
local f=CreateFrame("Frame","ACAuctionMarketFrame",UIParent)
A.frame=f; f:SetSize(790,540); f:SetPoint("CENTER"); f:SetFrameStrata("DIALOG")
f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=11,right=12,top=12,bottom=11}})
f:EnableMouse(true); f:SetMovable(true); f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart",function(self) self:StartMoving() end)
f:SetScript("OnDragStop",function(self) self:StopMovingOrSizing() end)
f:SetScript("OnHide",function() invalidate(); A.pending=nil; A.queued=nil end)
tinsert(UISpecialFrames,"ACAuctionMarketFrame")
label(f,"Товары аукциона",22,-22)
label(f,"Покупка нужного количества из предложений AHBot",22,-46)
local close=CreateFrame("Button",nil,f,"UIPanelCloseButton");close:SetPoint("TOPRIGHT",-8,-8)
A.search=CreateFrame("EditBox",nil,f,"InputBoxTemplate"); A.search:SetSize(320,24);A.search:SetPoint("TOPLEFT",28,-78)
A.search:SetAutoFocus(false); A.search:SetMaxLetters(32)
A.search:SetScript("OnEnterPressed",function(self) self:ClearFocus(); list(0) end)
A.search:SetScript("OnEscapePressed",function(self) self:ClearFocus() end)
button(f,"Поиск",360,-78,100,function() A.search:ClearFocus(); list(0) end)
button(f,"Обновить",470,-78,110,function() list(A.page) end)
label(f,"Товар",28,-116);label(f,"В наличии",458,-116);label(f,"Цена за штуку",590,-116)
A.buttons={}
for i=1,12 do
    local b=CreateFrame("Button",nil,f)
    b:SetSize(730,23);b:SetPoint("TOPLEFT",25,-140-(i-1)*23)
    b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
    b.name=label(b,"",3,-3,420);b.stock=label(b,"",433,-3,100);b.price=label(b,"",565,-3,165)
    b:SetScript("OnClick",function(self)
        if A.pending or A.queued then return end
        invalidate();A.selected=self.row
        A.selectedText:SetText(self.row.name.." — "..money(self.row.price).." за шт.")
    end)
    b:SetScript("OnEnter",function(self)
        if self.row then GameTooltip:SetOwner(self,"ANCHOR_RIGHT");GameTooltip:SetHyperlink("item:"..self.row.id);GameTooltip:Show() end
    end)
    b:SetScript("OnLeave",function() GameTooltip:Hide() end)
    A.buttons[i]=b
end
button(f,"Назад",25,-425,100,function() if A.page>0 then list(A.page-1) end end)
A.pageLabel=label(f,"Страница 1",145,-430)
button(f,"Далее",300,-425,100,function() if A.hasNext then list(A.page+1) end end)
A.selectedText=label(f,"Выберите товар",25,-465,495)
A.quantity=CreateFrame("EditBox",nil,f,"InputBoxTemplate");A.quantity:SetSize(60,24);A.quantity:SetPoint("TOPLEFT",530,-460)
A.quantity:SetAutoFocus(false);A.quantity:SetNumeric(true);A.quantity:SetMaxLetters(4);A.quantity:SetText("20")
A.quantity:SetScript("OnTextChanged",function() invalidate() end)
button(f,"Рассчитать",610,-460,140,function()
    if not A.selected then status("Сначала выберите товар."); return end
    local count=tonumber(A.quantity:GetText())
    if not count or count<1 or count>1000 or count~=math.floor(count) then status("Введите количество от 1 до 1000.");return end
    A.quantity:ClearFocus(); invalidate()
    request("Q",A.selected.id.."|"..count)
end)
A.status=label(f,"Откройте аукцион",25,-505,730)
f:Hide()
StaticPopupDialogs.ACAM_CONFIRM={text="Купить %s?",button1="Купить",button2=CANCEL,timeout=0,whileDead=false,hideOnEscape=true,
    OnAccept=function(self)
        local q=self.data
        if not q or q~=A.quote or q.generation~=A.generation or GetTime()>q.expires or not A.opened or not f:IsShown() then return end
        A.quote=nil;request("B",tostring(q.token))
    end,
    OnCancel=function() A.quote=nil end}
local function show()
    if not A.opened then return end
    if f:IsShown() then f:Hide();return end
    f:Show();list(0)
end
SLASH_ACAUCTIONMARKET1="/amarket"
SlashCmdList.ACAUCTIONMARKET=show
local events=CreateFrame("Frame")
events:RegisterEvent("AUCTION_HOUSE_SHOW");events:RegisterEvent("AUCTION_HOUSE_CLOSED");events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent",function(self,event,prefix,message,channel,sender)
    if event=="AUCTION_HOUSE_SHOW" then
        A.opened=true
        local guid=UnitGUID("npc") or UnitGUID("target")
        A.npc=guid and guid:gsub("^0[xX]","") or nil
        if AuctionFrame and not A.tab then
            A.tab=button(AuctionFrame,"Товары",190,-35,90,show)
        end
        return
    elseif event=="AUCTION_HOUSE_CLOSED" then
        A.opened=false; A.npc=nil; f:Hide();invalidate();A.pending=nil;A.queued=nil;return
    end
    local pending=A.pending
    if prefix~=PREFIX or channel~="WHISPER" or sender~=UnitName("player") or not pending then return end
    local t=split(message)
    if tonumber(t[1])~=pending.id then return end
    local kind=t[2]
    if kind=="X" then
        A.pending=nil;A.quote=nil;status(errors[t[3]] or ("Ошибка: "..tostring(t[3])));return
    end
    if pending.generation~=A.generation then A.pending=nil;return end
    if pending.kind=="L" then
        if kind=="P" then A.hasNext=t[4]=="1"
        elseif kind=="I" then
            if #A.rows>=12 then return end
            A.rows[#A.rows+1]={id=tonumber(t[3]),name=unhex(t[4]),stock=tonumber(t[5]),price=tonumber(t[6])}
        elseif kind=="D" then A.pending=nil;refresh();status("Выберите товар и укажите количество.") end
    elseif pending.kind=="Q" and kind=="Q" then
        A.pending=nil
        local item,qty,price=tonumber(t[4]),tonumber(t[5]),tonumber(t[6])
        if not A.selected or item~=A.selected.id or qty~=tonumber(A.quantity:GetText()) or not price then return end
        local q={token=tonumber(t[3]),generation=A.generation,expires=GetTime()+28}
        A.quote=q
        local popup=StaticPopup_Show("ACAM_CONFIRM",qty.." × "..A.selected.name.." за "..money(price))
        if popup then popup.data=q end
        status("Цена рассчитана. Подтвердите покупку.")
    elseif pending.kind=="B" and kind=="OK" then
        A.pending=nil;A.quote=nil
        status("Куплено "..t[3].." шт. за "..money(t[4])..". Товары отправлены почтой.")
        -- Keep the receipt visible; the user can refresh the changed stock explicitly.
    end
end)
events:SetScript("OnUpdate",function()
    local now=GetTime()
    if A.pending and now-A.pending.started>8 then A.pending=nil;A.quote=nil;status("Сервер не ответил. Проверьте обновление сервера и повторите запрос.") end
    if A.queued and now-A.lastSend>=0.3 then
        local job=A.queued;A.queued=nil;job.started=now;A.pending=job;A.lastSend=now
        SendAddonMessage(PREFIX,job.id.."|"..job.kind.."|"..A.npc.."|"..job.tail,"WHISPER",UnitName("player"))
    end
end)
