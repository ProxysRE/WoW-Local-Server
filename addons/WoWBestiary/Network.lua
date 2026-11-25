local A=WoWBestiary
local PREFIX="WBE2"
local pending,job
local serial=0
local cacheOrder={}
local detailOrder={}
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
    if job and job.kind=="P" and A.PositionError then A.PositionError(text) end
    pending=nil;job=nil
    A.networkStatus=text
    refresh()
end
local function send()
    serial=serial%1000000000+1
    pending={id=serial,chunks={},count=0,started=GetTime()}
    SendAddonMessage(PREFIX,"Q|2|"..serial.."|"..job.kind.."|"..job.target.."|"..job.page,
        "WHISPER",UnitName("player"))
end
local function start(kind,target)
    pending=nil
    job={kind=kind,target=target,region=A.selectedZone,page=0,rows={},sendAt=GetTime()+0.2}
    local statuses={L="Загрузка локаций…",Z="Загрузка существ…",D="Загрузка добычи…",P="Запрос позиции…"}
    A.networkStatus=statuses[kind]
    refresh()
end
local function touch(id)
    for i=#cacheOrder,1,-1 do if cacheOrder[i]==id then table.remove(cacheOrder,i) end end
    table.insert(cacheOrder,id)
    while #cacheOrder>6 do A.data.zones[table.remove(cacheOrder,1)]=nil end
end
local function touchDetail(region,id)
    for i=#detailOrder,1,-1 do
        if detailOrder[i].region==region and detailOrder[i].id==id then table.remove(detailOrder,i) end
    end
    detailOrder[#detailOrder+1]={region=region,id=id}
    while #detailOrder>16 do
        local old=table.remove(detailOrder,1)
        for _,mob in ipairs(A.GetZoneMobs(old.region)) do
            if mob.id==old.id then
                mob.loot={};mob.skin={};mob.pick={};mob.detailLoaded=nil
                break
            end
        end
    end
end
function A.RequestRegions()
    if job and job.kind=="L" then return end
    start("L",0)
end
function A.RequestZone(force)
    if not A.regionsLoaded then A.RequestRegions();return end
    if job and job.kind=="Z" and job.target==A.selectedZone and not force then return end
    if not force and A.data.zones[A.selectedZone] then
        touch(A.selectedZone)
        A.networkStatus="Данные локации в кэше"
        refresh()
        return
    end
    A.selectedMob=nil
    start("Z",A.selectedZone)
end
function A.SelectZone(id)
    if not A.regionById or not A.regionById[id] then return end
    pending=nil;job=nil
    A.selectedZone=id;A.selectedMob=nil;A.lootTab="loot"
    WoWBestiarySaved=WoWBestiarySaved or {}
    WoWBestiarySaved.lastZone=id
    if A.mobScroll then FauxScrollFrame_SetOffset(A.mobScroll,0) end
    if A.lootScroll then FauxScrollFrame_SetOffset(A.lootScroll,0) end
    A.RequestZone()
end
function A.RequestDetail(mob)
    if not mob then return end
    if mob.detailLoaded then touchDetail(A.selectedZone,mob.id);return end
    if job and job.kind=="D" and job.target==mob.id and job.region==A.selectedZone then return end
    start("D",mob.id)
end
local lastPositionRequest=-10
function A.RequestPosition(mob)
    if not mob or not mob.track then return end
    if GetTime()-lastPositionRequest<3 then return end
    lastPositionRequest=GetTime()
    if A.ShowPositions then A.ShowPositions(mob,{}) end
    start("P",mob.id)
end
function A.ReloadServer()
    pending=nil;job=nil;cacheOrder={};detailOrder={}
    A.regionsLoaded=false;A.data.zones={};A.selectedMob=nil
    A.RequestRegions()
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
        if job.kind=="L" then
            local id,map,zone,category,mode,count,expansion,capacity=tonumber(f[2]),tonumber(f[3]),tonumber(f[4]),
                tonumber(f[5]),tonumber(f[6]),tonumber(f[8]),tonumber(f[9]),tonumber(f[10])
            if #f~=10 or f[1]~="L" or not id or not map or not zone or not category or not mode or
                not count or not expansion or not capacity then return fail("Некорректная запись локации") end
            job.rows[#job.rows+1]={id=id,map=map,zone=zone,category=category,mode=mode,
                name=decode(f[7]),count=count,expansion=expansion,capacity=capacity}
        elseif job.kind=="P" then
            if f[1]=="S" and #f==2 then job.rows[#job.rows+1]={status=f[2]}
            else
                local map,zone,x,y=tonumber(f[2]),tonumber(f[3]),tonumber(f[4]),tonumber(f[5])
                if #f~=7 or f[1]~="P" or not map or not zone or not x or not y or x<0 or x>100 or
                    y<0 or y>100 or not string.match(f[6],"^[%w_]+$") then return fail("Некорректная позиция") end
                job.rows[#job.rows+1]={map=map,zone=zone,x=x,y=y,texture=f[6],name=decode(f[7])}
            end
        elseif job.kind=="Z" then
            local id,lo,hi,rank,spawns,creatureType,base=tonumber(f[2]),tonumber(f[4]),tonumber(f[5]),
                tonumber(f[6]),tonumber(f[7]),tonumber(f[8]),tonumber(f[9])
            if (#f~=9 and #f~=10) or f[1]~="M" or not id or not lo or not hi or not rank or not spawns or not creatureType or not base then
                return fail("Некорректная запись существа")
            end
            job.rows[#job.rows+1]={id=id,name=decode(f[3]),min=lo,max=hi,rank=rank,spawns=spawns,
                creatureType=creatureType,base=base,track=f[10]=="1",loot={},skin={},pick={}}
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
    if job.kind=="L" then
        A.zones=job.rows
        table.sort(A.zones,function(a,b)
            if a.category~=b.category then return a.category<b.category end
            local an,bn=A.Lower(a.name),A.Lower(b.name)
            if an~=bn then return an<bn end
            if a.mode~=b.mode then return a.mode<b.mode end
            return a.id<b.id
        end)
        A.regionById={}
        for _,zone in ipairs(A.zones) do A.regionById[zone.id]=zone end
        A.regionsLoaded=true
        job=nil
        if not A.regionById[A.selectedZone] then A.selectedZone=A.zones[1] and A.zones[1].id end
        if A.zoneScroll then FauxScrollFrame_SetOffset(A.zoneScroll,0) end
        if A.selectedZone then A.SelectZone(A.selectedZone) else fail("Нет локаций в каталоге сервера") end
        return
    elseif job.kind=="P" then
        local mob=A.selectedMob
        if mob and mob.id==job.target and A.ShowPositions then A.ShowPositions(mob,job.rows) end
        job=nil
        A.networkStatus="Снимок позиции получен"
        refresh()
        if mob and not mob.detailLoaded then A.RequestDetail(mob) end
        return
    elseif job.kind=="Z" then
        A.data.zones[job.target]={mobs=job.rows}
        touch(job.target)
        A.data.source="Текущий сервер • данные при запуске worldserver"
    else
        for _,mob in ipairs(A.GetZoneMobs(job.region)) do
            if mob.id==job.target then
                mob.loot={};mob.skin={};mob.pick={}
                for _,row in ipairs(job.rows) do table.insert(mob[row.tab],row) end
                mob.detailLoaded=true
                touchDetail(job.region,mob.id)
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
        pending=nil;job=nil;cacheOrder={};detailOrder={}
        A.regionsLoaded=false;A.data.zones={};A.selectedMob=nil;A.zones={};A.regionById={}
        if A.mapViewer then A.mapViewer:Hide() end
        A.networkStatus="Откройте справочник для подключения"
        if A.frame and A.frame:IsShown() then A.RequestRegions() end
        refresh()
        return
    end
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
        if GetTime()-pending.started>10 then fail("Нет ответа. Обновите модуль и аддон до 0.3.0; нажмите «Обновить»") end
    elseif job and GetTime()>=job.sendAt then send() end
end)
