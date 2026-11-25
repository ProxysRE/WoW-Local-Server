-- SPDX-License-Identifier: GPL-3.0-or-later
local W={seq=0,revision=0,queue={},pending={},filters={},rows={},selected=1,page=0,size=5,
    min=1,max=80,ilvl=0,guild=0,ready=false,busy=false,alts={},altPage=0,altPages=1}
local ru=GetLocale()=="ruRU"
local function T(a,b) if ru then return a else return b end end
local roles={[0]=T("Любая роль","Any role"),T("Танк","Tank"),T("Лекарь","Healer"),T("ДД","DPS")}
local classes={[0]=T("Любой класс","Any class"),[1]=T("Воин","Warrior"),[2]=T("Паладин","Paladin"),
    [3]=T("Охотник","Hunter"),[4]=T("Разбойник","Rogue"),[5]=T("Жрец","Priest"),[6]=T("Рыцарь смерти","Death knight"),
    [7]=T("Шаман","Shaman"),[8]=T("Маг","Mage"),[9]=T("Чернокнижник","Warlock"),[11]=T("Друид","Druid")}
local classOrder={0,1,2,3,4,5,6,7,8,9,11}
local specs={
    [1]={T("Оружие","Arms"),T("Неистовство","Fury"),T("Защита","Protection")},
    [2]={T("Свет","Holy"),T("Защита","Protection"),T("Воздаяние","Retribution")},
    [3]={T("Повелитель зверей","Beast mastery"),T("Стрельба","Marksmanship"),T("Выживание","Survival")},
    [4]={T("Ликвидация","Assassination"),T("Бой","Combat"),T("Скрытность","Subtlety")},
    [5]={T("Послушание","Discipline"),T("Свет","Holy"),T("Тьма","Shadow")},
    [6]={T("Кровь","Blood"),T("Лёд","Frost"),T("Нечестивость","Unholy")},
    [7]={T("Стихии","Elemental"),T("Совершенствование","Enhancement"),T("Исцеление","Restoration")},
    [8]={T("Тайная магия","Arcane"),T("Огонь","Fire"),T("Лёд","Frost")},
    [9]={T("Колдовство","Affliction"),T("Демонология","Demonology"),T("Разрушение","Destruction")},
    [11]={T("Баланс","Balance"),T("Сила зверя","Feral"),T("Исцеление","Restoration")}}
local reasons={REQUEST=T("Некорректный запрос","Invalid request"),RATE=T("Подождите пару секунд и повторите","Wait a moment and retry"),
    LEADER=T("Нужны права лидера обычной группы или рейда","You must lead a non-LFG group"),
    GROUP_SIZE=T("Проверьте размер группы и офлайн-участников","Check group size and offline members"),
    MEMBER_FILTERS=T("Нынешние участники не помещаются в заданные роли/фильтры. Добавьте места с любой ролью.","Current members do not fit the filters. Add slots with any role."),
    PLAN_REQUIRED=T("Сначала предложите состав","Propose a roster first"),
    PLAN_EXPIRED=T("Состав устарел или группа изменилась. Повторите подбор вне боя.","Roster expired or group changed. Propose again out of combat."),
    UNAVAILABLE=T("Кандидат больше недоступен","Candidate is no longer available"),
    STOPPED=T("Сбор остановлен: состояние персонажа или группа изменились","Recruitment stopped: player state or group changed"),
    BUSY=T("Уже идёт сбор или сервер занят","Recruitment is active or server is busy")}
local statuses={READY=T("Готов","Ready"),EMPTY=T("Не найден","Unfilled"),INVITED=T("Приглашён","Invited"),
    JOINED=T("В группе","In group"),ROLE_CHANGED=T("Проверьте роль","Check role"),UNAVAILABLE=T("Недоступен","Unavailable")}
local function Copy(t) local r={} for k,v in pairs(t) do r[k]=v end return r end
local function Unhex(s) if s=="-" then return "" end return (s or ""):gsub("%x%x",function(c) return string.char(tonumber(c,16)) end) end
local function Split(s) local r={} for p in (s..":"):gmatch("(.-):") do r[#r+1]=p end return r end
local function Spec(cls,spec) return specs[cls] and specs[cls][spec] or T("Не определена","Unknown") end
local function Req(op,args,kind)
    W.seq=W.seq+1
    local id=W.seq
    W.pending[id]={kind=kind or op,revision=W.revision}
    W.queue[#W.queue+1]={id=id,text=".gbuilder "..id.." "..op..(args and (" "..args) or "")}
    return id
end
local f=CreateFrame("Frame","ACGroupBuilderFrame",UIParent)
f:SetSize(1060,650); f:SetPoint("CENTER"); f:SetFrameStrata("DIALOG")
f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=8,right=8,top=8,bottom=8}})
f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart",f.StartMoving); f:SetScript("OnDragStop",f.StopMovingOrSizing)
f:Hide(); tinsert(UISpecialFrames,"ACGroupBuilderFrame")
local function Label(parent,x,y,text,width,style)
    local a=parent:CreateFontString(nil,"OVERLAY",style or "GameFontNormal")
    a:SetPoint("TOPLEFT",x,y); a:SetText(text or ""); if width then a:SetWidth(width); a:SetJustifyH("LEFT") end; return a
end
local function Button(parent,x,y,width,text,fn)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate"); b:SetSize(width,25); b:SetPoint("TOPLEFT",x,y)
    b:SetText(text); b:SetScript("OnClick",fn); return b
end
local function Edit(parent,x,y,width,text)
    local b=CreateFrame("EditBox",nil,parent,"InputBoxTemplate"); b:SetSize(width,24); b:SetPoint("TOPLEFT",x,y)
    b:SetAutoFocus(false); b:SetText(text or ""); b:SetScript("OnEscapePressed",b.ClearFocus); return b
end
local close=CreateFrame("Button",nil,f,"UIPanelCloseButton"); close:SetPoint("TOPRIGHT",-5,-5)
Label(f,24,-21,T("Сборщик группы","Group builder"),700,"GameFontNormalLarge")
local message=Label(f,24,-568,"",1005,"GameFontHighlightSmall"); message:SetHeight(40)
local function Status(s) message:SetText(s) end
local function Dirty()
    W.ready=false; W.nonce=nil; W.revision=W.revision+1
    Status(T("Условия изменены. Нажмите «Предложить состав».","Filters changed. Click Propose roster."))
end
local function Editable()
    if W.busy then Status(reasons.BUSY); return false end
    return true
end
local rosterButtons={}
local selectedTitle=Label(f,720,-157,"",305)
local condition=Label(f,720,-301,"",305,"GameFontHighlightSmall"); condition:SetHeight(80)
local roleButton,classButton,specButton,lockButton
local pageLabel=Label(f,310,-529,"",300,"GameFontHighlightSmall")
local function Draw()
    for i,b in ipairs(rosterButtons) do
        local index=W.page*10+i
        b.index=index
        local filter=W.filters[index]; local row=W.rows[index]
        if filter then
            b:Show()
            local requirement=roles[filter.role]..(filter.cls>0 and (" / "..classes[filter.cls]) or "")..(filter.spec>0 and (" / "..Spec(filter.cls,filter.spec)) or "")
            b.requirement:SetText(index..". "..requirement..(filter.pin>0 and " *" or ""))
            if row and row.guid>0 then
                b.who:SetText(row.name.." · "..classes[row.cls].." / "..Spec(row.cls,row.spec).." · "..(row.role==0 and T("роль?","role?") or roles[row.role]))
                b.gear:SetText(row.level..T(" ур. · ilvl "," lvl · ilvl ")..row.ilvl)
                b.state:SetText(row.state=="ROLE_CHANGED" and statuses.ROLE_CHANGED or (row.member and statuses.JOINED or (statuses[row.state] or row.state)))
            else b.who:SetText("—"); b.gear:SetText(""); b.state:SetText(statuses.EMPTY) end
            b:SetBackdropColor(index==W.selected and 0.13 or 0.05,0.09,0.12,0.9)
        else b:Hide() end
    end
    local r=W.filters[W.selected]
    if r then
        selectedTitle:SetText(T("Место № ","Slot #")..W.selected)
        roleButton:SetText(roles[r.role]); classButton:SetText(classes[r.cls])
        specButton:SetText(r.spec>0 and Spec(r.cls,r.spec) or T("Любая специализация","Any spec"))
        lockButton:SetText(r.pin>0 and T("Открепить кандидата","Unpin candidate") or T("Закрепить кандидата","Pin candidate"))
        condition:SetText(T("Роль, класс и специализация — строгие условия.\nIlvl — предпочтение; ниже порога выбирается ближайший доступный.\n* — закреплённый кандидат.","Role, class and spec are strict.\nIlvl is preferred; the closest lower candidate can fill a slot.\n* means pinned."))
    end
    pageLabel:SetText((W.page+1).." / "..math.ceil(W.size/10))
end
for i=1,10 do
    local b=CreateFrame("Button",nil,f); b:SetPoint("TOPLEFT",23,-153-(i-1)*36); b:SetSize(675,34)
    b:SetBackdrop({bgFile="Interface\\Buttons\\WHITE8X8"})
    b.requirement=Label(b,5,-3,"",660,"GameFontNormalSmall")
    b.who=Label(b,5,-17,"",415,"GameFontHighlightSmall")
    b.gear=Label(b,425,-17,"",140,"GameFontHighlightSmall")
    b.state=Label(b,565,-17,"",110,"GameFontHighlightSmall")
    b:SetScript("OnClick",function(self) W.selected=self.index; Draw() end)
    rosterButtons[i]=b
end
local dropdown=CreateFrame("Frame","ACGroupBuilderDropdown",UIParent,"UIDropDownMenuTemplate")
local function Pick(field,values)
    if not Editable() then return end
    local entries={}
    for _,value in ipairs(values) do
        local v=value[1]
        entries[#entries+1]={text=value[2],checked=W.filters[W.selected][field]==v,func=function()
            local r=W.filters[W.selected]; r[field]=v; r.pin=0
            if field=="cls" then r.spec=0 end
            Dirty(); Draw()
        end}
    end
    EasyMenu(entries,dropdown,"cursor",0,0,"MENU")
end
roleButton=Button(f,720,-191,305,"",function()
    local values={} for i=0,3 do values[#values+1]={i,roles[i]} end; Pick("role",values)
end)
classButton=Button(f,720,-224,305,"",function()
    local values={} for _,i in ipairs(classOrder) do values[#values+1]={i,classes[i]} end; Pick("cls",values)
end)
specButton=Button(f,720,-257,305,"",function()
    local r=W.filters[W.selected]
    if r.cls==0 then Status(T("Сначала выберите класс","Choose a class first")); return end
    local values={{0,T("Любая специализация","Any spec")}}
    for i=1,3 do values[#values+1]={i,Spec(r.cls,i)} end
    Pick("spec",values)
end)
lockButton=Button(f,720,-391,305,"",function()
    if not Editable() then return end
    local r=W.filters[W.selected]; local row=W.rows[W.selected]
    if r.pin>0 then r.pin=0 elseif row and row.guid>0 then r.pin=row.guid else return end
    Dirty(); Draw()
end)
Label(f,24,-62,T("Шаблон размера","Size preset"),160)
local presets={{5,1,1,T("Подземелье — 5","Dungeon — 5")},{10,2,3,T("Рейд — 10","Raid — 10")},
    {25,2,6,T("Рейд — 25","Raid — 25")},{40,3,10,T("Рейд — 40","Raid — 40")}}
local preset=1; local presetButton
local function Defaults(p)
    W.size=p[1]; W.page=0; W.selected=1; W.rows={}; W.filters={}
    for i=1,W.size do W.filters[i]={role=i<=p[2] and 1 or (i<=p[2]+p[3] and 2 or 3),cls=0,spec=0,pin=0} end
end
presetButton=Button(f,24,-87,190,presets[1][4],function()
    if not Editable() then return end
    preset=preset%#presets+1; Defaults(presets[preset]); presetButton:SetText(presets[preset][4]); Dirty(); Draw()
end)
Label(f,235,-62,T("Уровень от / до","Level min / max"),160)
local minimum=Edit(f,237,-87,58,"1"); local maximum=Edit(f,310,-87,58,"80")
Label(f,400,-62,T("Предпочтительный ilvl","Preferred ilvl"),180)
local gear=Edit(f,403,-87,90,"0")
local function LockOptions(busy)
    for _,edit in ipairs({minimum,maximum,gear}) do
        edit:EnableMouse(not busy); if busy then edit:ClearFocus() end
    end
end
for _,edit in ipairs({minimum,maximum,gear}) do
    edit:SetNumeric(true); edit:SetMaxLetters(4)
    edit:SetScript("OnTextChanged",function(self,user) if user then Dirty() end end)
    edit:SetScript("OnEnterPressed",edit.ClearFocus)
end
local guildButton
guildButton=Button(f,535,-87,165,T("Все свободные","All available"),function()
    if not Editable() then return end
    W.guild=1-W.guild; guildButton:SetText(W.guild==1 and T("Моя гильдия","My guild") or T("Все свободные","All available")); Dirty()
end)
Label(f,24,-126,T("Место и условия / кандидат","Slot requirements / candidate"),675,"GameFontNormal")
Button(f,24,-521,65,"<",function() W.page=math.max(0,W.page-1); Draw() end)
Button(f,96,-521,65,">",function() W.page=math.min(math.ceil(W.size/10)-1,W.page+1); Draw() end)

-- Manual replacements are returned and revalidated by the server; no client-submitted fake candidates.
local alternatives=CreateFrame("Frame",nil,f); alternatives:SetSize(800,440); alternatives:SetPoint("CENTER")
alternatives:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",tile=true,tileSize=32,edgeSize=32,insets={left=8,right=8,top=8,bottom=8}})
alternatives:SetFrameLevel(f:GetFrameLevel()+20); alternatives:EnableMouse(true); alternatives:Hide()
Label(alternatives,22,-22,T("Замена выбранного кандидата","Replace selected candidate"),740,"GameFontNormalLarge")
local altButtons={}
local function DrawAlts()
    for i,b in ipairs(altButtons) do
        b.row=W.alts[i]
        if b.row then
            local r=b.row
            b:SetText(r.name.." · "..classes[r.cls].." / "..Spec(r.cls,r.spec).." · "..r.level.." · ilvl "..r.ilvl); b:Show()
        else b:Hide() end
    end
end
for i=1,8 do
    local b=Button(alternatives,25,-65-(i-1)*37,745,"",function(self)
        if not self.row or not W.ready or W.busy then return end
        W.pickSlot=W.altSlot; W.pickGuid=self.row.guid
        Req("PICK",(W.altSlot-1).." "..self.row.guid,"PICK"); alternatives:Hide()
    end)
    altButtons[i]=b
end
local function LoadAlts(page)
    if not W.ready or W.busy then Status(reasons.PLAN_REQUIRED); return end
    W.alts={}; DrawAlts(); W.altRequest=Req("ALTS",(W.selected-1).." "..page,"ALTS")
    alternatives:Show()
end
Button(f,720,-425,305,T("Выбрать замену вручную","Choose replacement"),function() LoadAlts(0) end)
Button(alternatives,25,-382,65,"<",function() if W.altPage>0 then LoadAlts(W.altPage-1) end end)
Button(alternatives,100,-382,65,">",function() if W.altPage+1<W.altPages then LoadAlts(W.altPage+1) end end)
Button(alternatives,625,-382,145,T("Закрыть","Close"),function() alternatives:Hide() end)
local function Propose()
    if not Editable() then return end
    local min,max,ilvl=tonumber(minimum:GetText()),tonumber(maximum:GetText()),tonumber(gear:GetText())
    if not min or not max or not ilvl or min<1 or max>80 or min>max or ilvl>1000 then Status(reasons.REQUEST); return end
    W.min=min; W.max=max; W.ilvl=ilvl
    Dirty(); W.queue={}; W.pending={}
    Req("CONFIG",W.size.." "..min.." "..max.." "..ilvl.." "..W.guild)
    local batch={}; local length=0
    for i,r in ipairs(W.filters) do
        local row=(i-1)..":"..r.role..":"..r.cls..":"..r.spec..":"..r.pin
        if length+#row+1>185 then Req("SLOTS",table.concat(batch,",")); batch={}; length=0 end
        batch[#batch+1]=row; length=length+#row+1
    end
    if #batch>0 then Req("SLOTS",table.concat(batch,",")) end
    W.planRequest=Req("PLAN",nil,"PLAN"); Status(T("Подбор состава…","Finding candidates…"))
end
Button(f,24,-612,230,T("Предложить состав","Propose roster"),Propose)
Button(f,270,-612,230,T("Пригласить выбранных","Invite selected"),function()
    if W.busy or not W.ready then Status(reasons.PLAN_REQUIRED); return end
    local popup=StaticPopup_Show("ACGB_INVITE"); if popup then popup.data=W.nonce end
end)
Button(f,516,-612,185,T("Остановить сбор","Stop recruitment"),function() Req("CANCEL"); W.queue={W.queue[#W.queue]}; W.ready=false end)
StaticPopupDialogs.ACGB_INVITE={text=T("Пригласить предложенных ботов? Группа будет преобразована в рейд, если выбран размер больше 5.","Invite the proposed bots? A size above 5 will convert the party to a raid."),
    button1=ACCEPT,button2=CANCEL,timeout=0,whileDead=false,hideOnEscape=true,
    OnAccept=function(self) if W.ready and not W.busy and self.data==W.nonce then W.inviteRequest=Req("INVITE",tostring(W.nonce),"INVITE"); W.busy=true; W.ready=false; LockOptions(true) end end}

-- Named composition templates are local SavedVariables, including strict per-slot filters and pins.
local function Db()
    ACGroupBuilderDB=ACGroupBuilderDB or {templates={}}
    ACGroupBuilderDB.templates=ACGroupBuilderDB.templates or {}
    return ACGroupBuilderDB.templates
end
local templateName=Edit(f,723,-87,300,""); templateName:SetMaxBytes(60)
local savedIndex=0
Button(f,720,-120,145,T("Сохранить","Save"),function()
    local name=templateName:GetText():gsub("^%s+",""):gsub("%s+$","")
    if name=="" then Status(T("Введите название шаблона","Enter a template name")); return end
    local t={size=W.size,min=tonumber(minimum:GetText()) or 1,max=tonumber(maximum:GetText()) or 80,ilvl=tonumber(gear:GetText()) or 0,guild=W.guild,filters={}}
    for i,r in ipairs(W.filters) do t.filters[i]=Copy(r) end
    Db()[name]=t; templateName:ClearFocus(); Status(T("Шаблон сохранён: ","Template saved: ")..name)
end)
Button(f,880,-120,145,T("Загрузить ▸","Load next"),function()
    if not Editable() then return end
    local names={} for name in pairs(Db()) do names[#names+1]=name end; table.sort(names)
    if #names==0 then Status(T("Нет сохранённых шаблонов","No saved templates")); return end
    savedIndex=savedIndex%#names+1; local name=names[savedIndex]; local t=Db()[name]
    W.size=t.size; W.guild=t.guild; W.filters={}; W.rows={}; W.page=0; W.selected=1
    for i,r in ipairs(t.filters) do W.filters[i]=Copy(r) end
    minimum:SetText(t.min); maximum:SetText(t.max); gear:SetText(t.ilvl); templateName:SetText(name)
    presetButton:SetText(T("Состав — ","Roster — ")..t.size)
    guildButton:SetText(W.guild==1 and T("Моя гильдия","My guild") or T("Все свободные","All available")); Dirty(); Draw()
end)
Button(f,720,-465,305,T("Удалить указанный шаблон","Delete named template"),function()
    Db()[templateName:GetText()]=nil; Status(T("Шаблон удалён","Template deleted"))
end)
Label(f,720,-62,T("Название своего шаблона","Custom template name"),305)
local function Decode(p)
    return {guid=tonumber(p[5]),cls=tonumber(p[6]),spec=tonumber(p[7]),role=tonumber(p[8]),level=tonumber(p[9]),
        ilvl=tonumber(p[10]),member=p[11]=="1",name=Unhex(p[12]),state=p[13]}
end
local events=CreateFrame("Frame")
events:RegisterEvent("CHAT_MSG_SYSTEM"); events:RegisterEvent("PARTY_MEMBERS_CHANGED"); events:RegisterEvent("RAID_ROSTER_UPDATE")
events:SetScript("OnEvent",function(self,event,msg)
    if event~="CHAT_MSG_SYSTEM" then
        if not W.busy and W.ready then Dirty() end
        return
    end
    if type(msg)~="string" or msg:sub(1,5)~="ACG1:" then return end
    local p=Split(msg); local id=tonumber(p[2]); local pending=W.pending[id]
    if not pending then return end
    local kind=p[3]
    if pending.kind~="INVITE" and pending.kind~="CANCEL" and pending.revision~=W.revision then W.pending[id]=nil; return end
    if kind=="ERROR" then
        Status(reasons[p[4]] or p[4]); W.pending[id]=nil
        if pending.kind=="CONFIG" or pending.kind=="SLOTS" then W.queue={}; W.ready=false end
        if pending.kind=="INVITE" then W.busy=false; LockOptions(false) end
    elseif kind=="OK" then W.pending[id]=nil
    elseif kind=="PLAN" then W.nonce=tonumber(p[4]); W.rows={}
    elseif kind=="ROW" then W.rows[tonumber(p[4])+1]=Decode(p)
    elseif kind=="ALTS" and id==W.altRequest then
        W.altSlot=tonumber(p[4])+1; W.altPage=tonumber(p[5]); W.altPages=tonumber(p[6]); W.alts={}
    elseif kind=="BOT" and id==W.altRequest then W.alts[#W.alts+1]=Decode(p)
    elseif kind=="DONE" then
        W.pending[id]=nil
        if pending.kind=="PLAN" or pending.kind=="PICK" then
            if pending.kind=="PICK" then W.filters[W.pickSlot].pin=W.pickGuid end
            W.ready=true; Draw(); Status(T("Проверьте состав. Можно закрепить кандидатов или выбрать замену.","Review the roster. Pin candidates or choose replacements."))
        elseif pending.kind=="ALTS" then DrawAlts() end
    elseif kind=="STARTED" then pending.started=GetTime(); W.busy=true; Status(T("Сбор начат. Приглашения отправляются по очереди.","Recruitment started. Invitations are sent sequentially."))
    elseif kind=="STATUS" then
        local row=W.rows[tonumber(p[4])+1]; if row then row.state=p[5]; if p[5]=="JOINED" then row.member=true end end; Draw()
    elseif kind=="FINISHED" or kind=="CANCELLED" then
        W.busy=false; W.ready=false; LockOptions(false); W.pending[id]=nil
        if W.inviteRequest then W.pending[W.inviteRequest]=nil end
        Status(kind=="FINISHED" and T("Сбор завершён. Для добора повторите подбор.","Recruitment finished. Propose again to fill vacancies.") or T("Сбор остановлен; принятые участники остаются в группе.","Recruitment stopped; joined members stay in the group."))
    end
end)
ChatFrame_AddMessageEventFilter("CHAT_MSG_SYSTEM",function(self,event,msg) return type(msg)=="string" and msg:sub(1,5)=="ACG1:" end)
local tick=0
events:SetScript("OnUpdate",function(self,elapsed)
    tick=tick+elapsed; if tick<0.35 then return end; tick=0
    local send=table.remove(W.queue,1)
    if send then
        local pending=W.pending[send.id]
        if pending then pending.sent=GetTime(); SendChatMessage(send.text,"WHISPER",nil,UnitName("player")) end
    end
    for id,pending in pairs(W.pending) do
        if pending.sent and GetTime()-pending.sent>(pending.kind=="INVITE" and 1000 or 25) then
            W.pending[id]=nil; W.ready=false
            Status(T("Нет ответа сервера. Проверьте, установлен ли модуль сборщика групп.","No server response. Check that the group builder module is installed."))
        end
    end
end)
Defaults(presets[1]); Draw()
local function Open()
    local scale=math.min(1,(UIParent:GetWidth()-30)/1060,(UIParent:GetHeight()-30)/650)
    f:SetScale(math.max(0.4,scale)); f:Show()
    Status(T("Выберите размер и условия, затем нажмите «Предложить состав».","Choose size and filters, then click Propose roster."))
end
SLASH_ACGROUPBUILDER1="/gbuilder"; SLASH_ACGROUPBUILDER2="/сбор"
SlashCmdList.ACGROUPBUILDER=function() if f:IsShown() then f:Hide() else Open() end end
