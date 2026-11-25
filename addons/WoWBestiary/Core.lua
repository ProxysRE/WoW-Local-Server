WoWBestiary = WoWBestiary or {}
local A = WoWBestiary
A.version = "0.2.0"
A.data = WoWBestiaryData or {zones={}}
A.zones = WoWBestiaryZones or {}
A.continents = WoWBestiaryContinents or {}
A.selectedZone = 10
A.query = ""
A.rankFilter = -1
A.selectedMob = nil
A.lootTab = "loot"
A.sortOrder = "level"
A.ranks = {[0]="Обычный", [1]="Элитный", [2]="Редкий элитный", [3]="Босс", [4]="Редкий"}
A.types = {[1]="Животное",[2]="Дракон",[3]="Демон",[4]="Элементаль",[5]="Великан",
    [6]="Нежить",[7]="Гуманоид",[8]="Существо",[9]="Механизм",[10]="Без типа",
    [11]="Тотем",[12]="Питомец",[13]="Газовое облако"}
local from, to = {}, {}
for n=0,31 do
    local up, lo = 0xD0, 0xB0+n
    if n < 16 then up,lo = 0xD0,0x90+n else up,lo = 0xD0,0x90+n end
    -- Uppercase А-П are D0 90..9F, Р-Я are D0 A0..AF
    local upper = string.char(0xD0, 0x90+n)
    local lower = n < 16 and string.char(0xD0,0xB0+n) or string.char(0xD1,0x80+n-16)
    from[#from+1]=upper; to[upper]=lower
end
from[#from+1]="Ё"; to["Ё"]="ё"
function A.Lower(s)
    s=string.lower(tostring(s or ""))
    for i=1,#from do s=string.gsub(s,from[i],to[from[i]]) end
    return s
end
function A.GetZoneMobs(zoneId)
    local data=A.data.zones and A.data.zones[zoneId]
    return data and data.mobs or {}
end
function A.GetFilteredMobs()
    local src=A.GetZoneMobs(A.selectedZone)
    local items={}
    local q=A.Lower(A.query)
    for _,mob in ipairs(src) do
        local passes=A.rankFilter==-1 or mob.rank==A.rankFilter
        local matches=q=="" or string.find(A.Lower(mob.name),q,1,true) or string.find(tostring(mob.id),q,1,true)
        if passes and matches then items[#items+1]=mob end
    end
    table.sort(items, function(a,b)
        if A.sortOrder=="name" then
            local an,bn=A.Lower(a.name),A.Lower(b.name)
            if an~=bn then return an<bn end
            return a.id<b.id
        end
        if a.min~=b.min then return a.min<b.min end
        if a.max~=b.max then return a.max<b.max end
        return a.id<b.id
    end)
    return items
end
function A.FormatChance(item)
    if not item.chance then return "?" end
    local s=string.format("%.2f",item.chance)
    s=string.gsub(s,"0+$","")
    s=string.gsub(s,"%.$","")
    return (item.approx and "~" or "")..s.."%"
end
function A.GetLoot(mob,tab)
    if not mob then return {} end
    return mob[tab] or {}
end
local initialized=false
local eventFrame=CreateFrame("Frame")
eventFrame:RegisterEvent("ADDON_LOADED")
eventFrame:SetScript("OnEvent",function(self,event,name)
    if name~="WoWBestiary" or initialized then return end
    initialized=true
    if type(WoWBestiarySaved)~="table" then WoWBestiarySaved={} end
    if WoWBestiarySaved.lastZone then A.selectedZone=WoWBestiarySaved.lastZone end
    A.selectedZone=10
    if A.BuildUI then A.BuildUI() end
end)
SLASH_WOWBESTIARY1="/wb"
SLASH_WOWBESTIARY2="/bestiary"
SLASH_WOWBESTIARY3="/бестиарий"
SlashCmdList["WOWBESTIARY"]=function(msg)
    if msg=="reset" then
        WoWBestiarySaved={}
        A.selectedZone=10; A.rankFilter=-1; A.query=""; A.selectedMob=nil
        if A.Refresh then A.Refresh() end
        print("|cffcaa763WoW Bestiary:|r настройки сброшены")
        return
    end
    if not A.frame and A.BuildUI then A.BuildUI() end
    if A.frame then
        if A.frame:IsShown() then A.frame:Hide() else
            A.frame:Show()
            if A.RequestZone then A.RequestZone() end
            A.Refresh()
        end
    end
end
