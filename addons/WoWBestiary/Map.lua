local A=WoWBestiary
local viewer
local function button(parent,text,x,y,width,callback)
    local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
    b:SetSize(width,24);b:SetPoint("TOPLEFT",x,y);b:SetText(text);b:SetScript("OnClick",callback)
    return b
end
local function label(parent,x,y,width)
    local t=parent:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    t:SetPoint("TOPLEFT",x,y);t:SetWidth(width);t:SetHeight(18);t:SetJustifyH("LEFT")
    return t
end
local function build()
    if viewer then return end
    local screenWidth,screenHeight=UIParent:GetWidth(),UIParent:GetHeight()
    local width=math.min(1000,screenWidth*.9)
    local mapWidth=width-28
    local mapHeight=mapWidth*668/1002
    if mapHeight+130>screenHeight*.9 then
        mapHeight=screenHeight*.9-130;mapWidth=mapHeight*1002/668;width=mapWidth+28
    end
    local f=CreateFrame("Frame","WoWBestiaryMapFrame",UIParent)
    viewer=f;A.mapViewer=f
    f:SetSize(width,mapHeight+130);f:SetPoint("CENTER");f:SetFrameStrata("FULLSCREEN_DIALOG")
    f:SetClampedToScreen(true);f:EnableMouse(true);f:SetMovable(true);f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart",f.StartMoving);f:SetScript("OnDragStop",f.StopMovingOrSizing)
    f:SetScript("OnHide",function(self) self.dismissed=true end)
    f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",edgeSize=20,
        insets={left=5,right=5,top=5,bottom=5}})
    f.title=label(f,16,-15,width-65)
    button(f,"X",width-42,-9,27,function() f:Hide() end)
    f.canvas=CreateFrame("Frame",nil,f);f.canvas:SetSize(mapWidth,mapHeight)
    f.canvas:SetPoint("TOPLEFT",14,-43)
    f.tiles={};f.pins={}
    for i=1,12 do
        local col,row=(i-1)%4,math.floor((i-1)/4)
        local w,h=math.min(256,1002-col*256),math.min(256,668-row*256)
        local tile=f.canvas:CreateTexture(nil,"BACKGROUND")
        tile:SetSize(w*mapWidth/1002,h*mapHeight/668)
        tile:SetPoint("TOPLEFT",col*256*mapWidth/1002,-row*256*mapHeight/668)
        tile:SetTexCoord(0,w/256,0,h/256);f.tiles[i]=tile
    end
    f.status=label(f,16,-mapHeight-50,width-32)
    f.age=label(f,16,-mapHeight-69,width-32)
    button(f,"Обновить позицию",14,-mapHeight-94,170,function()
        if A.RequestPosition and f.mob then A.RequestPosition(f.mob) end
    end)
    button(f,"Следующая карта",width-188,-mapHeight-94,174,function()
        if not f.groups or #f.groups<2 then return end
        f.index=f.index%#f.groups+1;A.DrawPositions()
    end)
    f:SetScript("OnUpdate",function(self)
        if not self.stamp then return end
        local age=math.floor(GetTime()-self.stamp)
        if age>=30 then
            for _,pin in ipairs(self.pins) do pin:Hide() end
            self.age:SetText("Снимок устарел. Обновите позицию.")
        else self.age:SetText("Снимок: "..age.." с назад • метки действуют 30 секунд") end
    end)
    table.insert(UISpecialFrames,"WoWBestiaryMapFrame")
end
function A.DrawPositions()
    local f=viewer
    for _,pin in ipairs(f.pins) do pin:Hide() end
    local group=f.groups and f.groups[f.index]
    if not group then
        for _,tile in ipairs(f.tiles) do tile:Hide() end
        f.title:SetText(f.mob and f.mob.name or "Поиск существа")
        return
    end
    f.title:SetText(f.mob.name.." • "..group.name.." ("..f.index.."/"..#f.groups..")")
    for i,tile in ipairs(f.tiles) do
        tile:SetTexture("Interface\\WorldMap\\"..group.texture.."\\"..group.texture..i);tile:Show()
    end
    for i,point in ipairs(group.points) do
        local pin=f.pins[i]
        if not pin then
            pin=CreateFrame("Button",nil,f.canvas);pin:SetSize(18,18)
            local icon=pin:CreateTexture(nil,"OVERLAY");icon:SetAllPoints()
            icon:SetTexture("Interface\\TargetingFrame\\UI-RaidTargetingIcon_8")
            pin:SetScript("OnEnter",function(self)
                GameTooltip:SetOwner(self,"ANCHOR_RIGHT")
                GameTooltip:AddLine(f.mob.name,1,.8,.2)
                GameTooltip:AddLine(string.format("%.1f, %.1f",self.point.x,self.point.y),1,1,1)
                GameTooltip:Show()
            end)
            pin:SetScript("OnLeave",function() GameTooltip:Hide() end)
            f.pins[i]=pin
        end
        pin.point=point;pin:ClearAllPoints()
        pin:SetPoint("CENTER",f.canvas,"TOPLEFT",point.x*f.canvas:GetWidth()/100,-point.y*f.canvas:GetHeight()/100)
        if f.stamp and GetTime()-f.stamp<30 then pin:Show() end
    end
end
function A.ShowPositions(mob,rows)
    local loading=not rows or #rows==0
    if viewer and viewer.dismissed and not loading then return end
    build()
    local f=viewer
    if loading then f.dismissed=false end
    f.mob=mob;f.groups={};f.index=1
    local groups={}
    local status="LOADING"
    for _,row in ipairs(rows or {}) do
        if row.status then status=row.status
        else
            local key=row.map..":"..row.zone
            local group=groups[key]
            if not group then
                group={name=row.name,texture=row.texture,points={}}
                groups[key]=group;f.groups[#f.groups+1]=group
            end
            group.points[#group.points+1]=row
        end
    end
    local messages={LOADING="Запрос живой позиции…",OK="Живые позиции, известные серверу. Только подходящая фаза.",
        NO_DATA="Нет живой позиции: область выгружена, моб мёртв или неактивен.",
        NO_MAP="Моб найден, но для его области нет подходящей карты клиента."}
    f.stamp=status=="OK" and GetTime() or nil
    f.status:SetText(messages[status] or status)
    f.age:SetText("");A.DrawPositions();f:Show()
end
function A.PositionError(text)
    if viewer then A.ShowPositions(viewer.mob,{{status=text}}) end
end
