local A=WoWBestiary
local PREFIX="WBE1"
local pending,job
local serial=0
A.networkStatus="Откройте справочник для подключения"
local function split(s)
    local t={}
    for value in string.gmatch(s.."|","(.-)|") do t[#t+1]=value end
    return t
end
local function decode(s)
    return (string.gsub(s or "","%%(%x%x)",function(h) return string.char(tonumber(h,16)) end))
end
local function refresh()
    if A.Refresh then A.Refresh() end
end
local function fail(text)
    pending=nil;job=nil
    A.networkStatus=text
    refresh()
end
local function send()
    serial=serial%1000000000+1
    pending={id=serial,chunks={},count=0,started=GetTime()}
    SendAddonMessage(PREFIX,"Q|1|"..serial.."|"..job.kind.."|"..job.target.."|"..job.page,
        "WHISPER",UnitName("player"))
end
local function start(kind,target)
    pending=nil
    job={kind=kind,target=target,page=0,rows={},sendAt=GetTime()+0.2}
    A.networkStatus=kind=="Z" and "Загрузка существ с сервера…" or "Загрузка добычи…"
    refresh()
end
function A.RequestZone(force)
    if job and job.kind=="Z" then return end
    if not force and A.zoneLoaded then return end
    A.selectedMob=nil
    start("Z",10)
end
function A.RequestDetail(mob)
    if not mob or mob.detailLoaded then return end
    if job and job.kind=="D" and job.target==mob.id then return end
    start("D",mob.id)
end
function A.ReloadServer()
    A.zoneLoaded=false
    A.RequestZone(true)
end
local function consume(payload)
    local lines={}
    for line in string.gmatch(payload,"[^\n]+") do lines[#lines+1]=split(line) end
    local h=lines[1]
    if not h then return fail("Пустой ответ сервера") end
    if h[1]=="X" then return fail("Ошибка сервера: "..tostring(h[2])) end
    local page,pages=tonumber(h[4]),tonumber(h[5])
    if #h~=5 or h[1]~="H" or h[2]~=job.kind or tonumber(h[3])~=job.target or
        page~=job.page or not pages or pages<1 or pages>4096 or pages~=math.floor(pages) or page>=pages then
        return fail("Некорректный ответ сервера")
    end
    if job.pages and job.pages~=pages then return fail("Каталог изменился; нажмите «Обновить»") end
    job.pages=pages
    for i=2,#lines do
        local f=lines[i]
        if job.kind=="Z" then
            local id,lo,hi,rank,spawns,creatureType=tonumber(f[2]),tonumber(f[4]),tonumber(f[5]),
                tonumber(f[6]),tonumber(f[7]),tonumber(f[8])
            if #f~=8 or f[1]~="M" or not id or not lo or not hi or not rank or not spawns or not creatureType then
                return fail("Некорректная запись существа")
            end
            job.rows[#job.rows+1]={id=id,name=decode(f[3]),min=lo,max=hi,rank=rank,spawns=spawns,
                creatureType=creatureType,loot={},skin={},pick={}}
        else
            local id,quality,chance,quest=tonumber(f[3]),tonumber(f[5]),tonumber(f[6]),tonumber(f[7])
            if #f~=8 or f[1]~="I" or (f[2]~="loot" and f[2]~="skin" and f[2]~="pick") or
                not id or not quality or not chance or not quest then return fail("Некорректная запись добычи") end
            job.rows[#job.rows+1]={tab=f[2],id=id,name=decode(f[4]),quality=quality,
                chance=chance>=0 and chance or nil,quest=quest==1,note=decode(f[8])}
        end
    end
    pending=nil
    if page+1<pages then
        job.page=page+1
        job.sendAt=GetTime()+0.2
        A.networkStatus="Загрузка: "..(page+1).."/"..pages
        refresh()
        return
    end
    if job.kind=="Z" then
        A.data.zones[10]={mobs=job.rows}
        A.zoneLoaded=true
        A.data.source="Текущий сервер • данные при запуске worldserver"
    else
        for _,mob in ipairs(A.GetZoneMobs(10)) do
            if mob.id==job.target then
                mob.loot={};mob.skin={};mob.pick={}
                for _,row in ipairs(job.rows) do table.insert(mob[row.tab],row) end
                mob.detailLoaded=true
                break
            end
        end
    end
    job=nil
    A.networkStatus="Данные загружены с сервера"
    refresh()
end
local frame=CreateFrame("Frame")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:SetScript("OnEvent",function(self,event,prefix,message,channel,sender)
    if event=="PLAYER_ENTERING_WORLD" then
        pending=nil;job=nil
        A.zoneLoaded=false;A.data.zones={};A.selectedMob=nil
        A.networkStatus="Откройте справочник для подключения"
        if A.frame and A.frame:IsShown() then A.RequestZone() end
        refresh()
        return
    end
    -- Server responses are self-whispers. Other players cannot inject catalogue entries.
    if prefix~=PREFIX or channel~="WHISPER" or sender~=UnitName("player") or not pending or not job then return end
    local id,index,total,chunk=string.match(message,"^(%d+)|(%d+)|(%d+)|(.*)$")
    id,index,total=tonumber(id),tonumber(index),tonumber(total)
    if id~=pending.id then return end
    if not index or not total or total<1 or total>512 or index<1 or index>total or
        (pending.total and pending.total~=total) or #chunk>180 then return fail("Ошибка передачи данных") end
    pending.total=total
    if not pending.chunks[index] then
        pending.chunks[index]=chunk
        pending.count=pending.count+1
    end
    if pending.count==total then consume(table.concat(pending.chunks)) end
end)
frame:SetScript("OnUpdate",function()
    if pending then
        if GetTime()-pending.started>10 then fail("Нет ответа. Проверьте модуль сервера и нажмите «Обновить»") end
    elseif job and GetTime()>=job.sendAt then send() end
end)
