local A=WoWBestiary
local gold="|cffffcc66"
local grey="|cff999999"
local white="|cffffffff"
local cyan="|cff88c9ff"
local function Txt(parent,text,x,y,w,h,font)
  local fs=parent:CreateFontString(nil,"OVERLAY",font or "GameFontNormal")
  fs:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
  fs:SetWidth(w); fs:SetHeight(h); fs:SetJustifyH("LEFT");fs:SetJustifyV("MIDDLE")
  fs:SetText(text or "")
  return fs
end
local function Button(parent,text,x,y,w,h,click)
  local b=CreateFrame("Button",nil,parent,"UIPanelButtonTemplate")
  b:SetSize(w,h);b:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
  b:SetText(text); b:SetScript("OnClick",click)
  return b
end
local function Pane(parent,x,y,w,h)
  local p=CreateFrame("Frame",nil,parent)
  p:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
  p:SetSize(w,h)
  p:SetBackdrop({bgFile="Interface\\ChatFrame\\ChatFrameBackground",edgeFile="Interface\\Tooltips\\UI-Tooltip-Border",edgeSize=12,insets={left=3,right=3,top=3,bottom=3}})
  p:SetBackdropColor(.07,.08,.12,.96)
  p:SetBackdropBorderColor(.32,.31,.26,1)
  return p
end
local function Scroll(parent,x,y,w,h,visible,rowHeight)
  local name="WoWBestiaryScroll"..tostring(A.scrollCount or 0)
  A.scrollCount=(A.scrollCount or 0)+1
  local scroll=CreateFrame("ScrollFrame",name,parent,"FauxScrollFrameTemplate")
  scroll:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y)
  scroll:SetSize(w,h)
  scroll:SetScript("OnVerticalScroll",function(self,offset)
    FauxScrollFrame_OnVerticalScroll(self,offset,rowHeight,A.Refresh)
  end)
  scroll.rows={}
  for i=1,visible do
    local r=CreateFrame("Button",nil,parent)
    r:SetHeight(rowHeight);r:SetWidth(w-20)
    r:SetPoint("TOPLEFT",parent,"TOPLEFT",x,y-(i-1)*rowHeight)
    local tex=r:CreateTexture(nil,"BACKGROUND")
    tex:SetAllPoints(r)
    tex:SetTexture("Interface\\ChatFrame\\ChatFrameBackground")
    tex:SetVertexColor(.18,.22,.31,.5)
    r:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight","ADD")
    r.label=r:CreateFontString(nil,"OVERLAY","GameFontNormalSmall")
    r.label:SetJustifyH("LEFT")
    r.label:SetPoint("LEFT",r,"LEFT",6,0)
    r.label:SetPoint("RIGHT",r,"RIGHT",-4,0)
    r.label:SetHeight(rowHeight)
    scroll.rows[i]=r
  end
  return scroll
end
function A.BuildUI()
  if A.frame then return end
  local f=CreateFrame("Frame","WoWBestiaryMainFrame",UIParent)
  A.frame=f
  f:SetWidth(940);f:SetHeight(540)
  f:SetPoint("CENTER",UIParent,"CENTER",0,0)
  f:SetFrameStrata("DIALOG")
  f:SetToplevel(true)
  f:EnableMouse(true);f:SetMovable(true);f:RegisterForDrag("LeftButton")
  f:SetScript("OnDragStart",f.StartMoving)
  f:SetScript("OnDragStop",f.StopMovingOrSizing)
  f:SetClampedToScreen(true)
  f:SetBackdrop({bgFile="Interface\\DialogFrame\\UI-DialogBox-Background",edgeFile="Interface\\DialogFrame\\UI-DialogBox-Border",edgeSize=30,insets={left=10,right=10,top=10,bottom=10}})
  f:SetBackdropColor(.1,.12,.16,.97)
  local title=Txt(f,"Бестиарий Азерота",24,-19,430,25,"GameFontNormalLarge")
  Txt(f,"WoW 3.3.5a • данные AzerothCore",25,-47,340,16,"GameFontDisableSmall")
  Button(f,"X",886,-18,30,25,function() f:Hide() end)
  local zonePane=Pane(f,16,-81,182,429)
  local mobPane=Pane(f,206,-81,329,429)
  local detailPane=Pane(f,543,-81,380,429)
  Txt(zonePane,"Локации",12,-9,150,24)
  local cont=Button(zonePane,"Восточные королевства",8,-42,166,25,function() end)
  cont:Disable()
  Txt(zonePane,"Открытый мир",10,-77,152,20,"GameFontHighlightSmall")
  A.zoneButtons={}
  for i,z in ipairs(A.zones) do
    local b=Button(zonePane,z.name,8,-102-(i-1)*28,166,26,function()
      A.selectedZone=z.id
      A.selectedMob=nil; A.lootTab="loot"
      WoWBestiarySaved.lastZone=z.id
      FauxScrollFrame_SetOffset(A.mobScroll,0)
      A.Refresh()
    end)
    A.zoneButtons[z.id]=b
  end
  Button(zonePane,"Обновить",8,-337,166,26,function() A.ReloadServer() end)
  Txt(zonePane,"Каталог текущего сервера",11,-376,155,18,"GameFontDisableSmall")
  A.countLabel=Txt(mobPane,"Существа",13,-8,290,22)
  local edit=CreateFrame("EditBox",nil,mobPane,"InputBoxTemplate")
  A.search=edit
  edit:SetAutoFocus(false);edit:SetSize(285,22)
  edit:SetPoint("TOPLEFT",mobPane,"TOPLEFT",23,-41)
  edit:SetMaxLetters(80);edit:SetText("")
  edit:SetScript("OnEscapePressed",function(self) self:ClearFocus() end)
  edit:SetScript("OnEnterPressed",function(self) self:ClearFocus() end)
  edit:SetScript("OnTextChanged",function(self,userInput)
    if not userInput then return end
    A.query=self:GetText() or ""
    A.selectedMob=nil
    FauxScrollFrame_SetOffset(A.mobScroll,0)
    A.Refresh()
  end)
  Txt(mobPane,"Поиск по имени или ID",13,-66,245,17,"GameFontDisableSmall")
  A.sortButton=Button(mobPane,"Уровень ↑",10,-90,148,23,function()
    A.sortOrder = A.sortOrder=="level" and "name" or "level"
    A.Refresh()
  end)
  A.filterButton=Button(mobPane,"Все существа",165,-90,150,23,function()
    local r={[-1]=0,[0]=1,[1]=2,[2]=3,[3]=4,[4]=-1}
    A.rankFilter=r[A.rankFilter] or -1
    A.selectedMob=nil
    FauxScrollFrame_SetOffset(A.mobScroll,0)
    A.Refresh()
  end)
  A.mobScroll=Scroll(mobPane,11,-121,301,264,11,24)
  for _,row in ipairs(A.mobScroll.rows) do
    row:SetScript("OnClick",function(self)
      A.selectedMob=self.mob
      A.lootTab="loot"
      FauxScrollFrame_SetOffset(A.lootScroll,0)
      A.Refresh()
      A.RequestDetail(self.mob)
    end)
  end
  A.detailTitle=Txt(detailPane,"Выберите существо",13,-12,350,40,"GameFontNormalLarge")
  A.detailMeta=Txt(detailPane,"",13,-55,350,35,"GameFontHighlightSmall")
  A.detailID=Txt(detailPane,"",13,-86,350,20,"GameFontDisableSmall")
  A.tabs={}
  local titles={{"loot","Добыча"},{"skin","Шкуры"},{"pick","Карманы"}}
  for i,t in ipairs(titles) do
    A.tabs[t[1]]=Button(detailPane,t[2],11+(i-1)*117,-116,114,25,function()
      A.lootTab=t[1]
      FauxScrollFrame_SetOffset(A.lootScroll,0)
      A.Refresh()
    end)
  end
  A.lootLabel=Txt(detailPane,"",13,-150,348,19,"GameFontDisableSmall")
  A.lootScroll=Scroll(detailPane,13,-177,344,194,8,24)
  for _,r in ipairs(A.lootScroll.rows) do
    r:SetScript("OnEnter",function(self)
      if not self.item then return end
      GameTooltip:SetOwner(self,"ANCHOR_RIGHT")
      local _,link=GetItemInfo(self.item.id)
      if link then GameTooltip:SetHyperlink(link)
      else
        GameTooltip:AddLine(self.item.name or ("Item "..self.item.id),1,1,1)
      end
      GameTooltip:AddLine("ID: "..self.item.id,.6,.6,.6)
      GameTooltip:AddLine("Шанс: "..A.FormatChance(self.item),1,.85,.35)
      if self.item.note and self.item.note~="" then GameTooltip:AddLine(self.item.note,.8,.8,.8,true) end
      GameTooltip:Show()
    end)
    r:SetScript("OnLeave",function() GameTooltip:Hide() end)
    r:SetScript("OnClick",function(self,button)
      if self.item and IsShiftKeyDown() then
        local link=select(2,GetItemInfo(self.item.id))
        if link and ChatEdit_InsertLink then ChatEdit_InsertLink(link) end
      end
    end)
    local text=r.label
    text:SetPoint("RIGHT",r,"RIGHT",-75,0)
    r.value=r:CreateFontString(nil,"OVERLAY","GameFontHighlightSmall")
    r.value:SetPoint("RIGHT",r,"RIGHT",-5,0)
    r.value:SetWidth(78);r.value:SetJustifyH("RIGHT")
    r.icon=r:CreateTexture(nil,"ARTWORK")
    r.icon:SetPoint("LEFT",r,"LEFT",2,0)
    r.icon:SetSize(18,18)
    r.label:ClearAllPoints()
    r.label:SetPoint("LEFT",r,"LEFT",24,0)
    r.label:SetPoint("RIGHT",r,"RIGHT",-76,0)
    r.label:SetHeight(24)
  end
  A.help=Txt(detailPane,"Базовые шансы БД, без рейтов и скриптов.\n? — сложная таблица; детали в подсказке.",13,-380,345,38,"GameFontDisableSmall")
  A.footer=Txt(f,"/wb • /bestiary • /бестиарий",23,-516,880,19,"GameFontDisableSmall")
  A.source=Txt(f,"Источник: "..(A.data.source or "неизвестен"),390,-47,525,18,"GameFontDisableSmall")
  f:Hide()
  table.insert(UISpecialFrames,"WoWBestiaryMainFrame")
  A.Refresh()
end
function A.Refresh()
  if not A.frame then return end
  A.source:SetText(A.networkStatus or A.data.source)
  for id,b in pairs(A.zoneButtons) do
    local name="Локация "..id
    for _,zone in ipairs(A.zones) do if zone.id==id then name=zone.name;break end end
    b:SetText(id==A.selectedZone and ("> "..name) or name)
  end
  A.sortButton:SetText(A.sortOrder=="level" and "Уровень ↑" or "По названию")
  A.filterButton:SetText(A.rankFilter==-1 and "Все существа" or (A.ranks[A.rankFilter] or "Все"))
  local mobs=A.GetFilteredMobs()
  A.countLabel:SetText("Существа: "..#mobs)
  FauxScrollFrame_Update(A.mobScroll,#mobs,#A.mobScroll.rows,24)
  local offset=FauxScrollFrame_GetOffset(A.mobScroll)
  for i,r in ipairs(A.mobScroll.rows) do
    local mob=mobs[i+offset]
    r.mob=mob
    if mob then
      r:Show()
      r.label:SetText((mob==A.selectedMob and gold or white)..mob.name.."|r  "..grey..(mob.min==mob.max and tostring(mob.min) or (mob.min.."-"..mob.max)).."|r"..(mob.rank==4 and " |cff66d9ff★|r" or mob.rank==1 and " |cffff5e5eЭ|r" or mob.rank==2 and " |cffff5e5e★Э|r" or mob.rank==3 and " |cffff5e5eБ|r" or ""))
    else r:Hide() end
  end
  local m=A.selectedMob
  if not m then
    A.detailTitle:SetText("Выберите существо")
    if #(A.GetZoneMobs(A.selectedZone))==0 then
      A.detailMeta:SetText(A.networkStatus or "Нажмите «Обновить»")
    else A.detailMeta:SetText("Слева список существ по уровню.") end
    A.detailID:SetText("")
  else
    A.detailTitle:SetText(m.name)
    A.detailMeta:SetText((A.ranks[m.rank] or "Существо").." • ур. "..m.min..
      (m.min==m.max and "" or "-"..m.max).." • "..(A.types[m.creatureType] or "Без типа"))
    A.detailID:SetText("ID: "..m.id.."    • точек появления: "..(m.spawns or "не импортированы"))
  end
  local loot=A.GetLoot(m,A.lootTab)
  if m and not m.detailLoaded then
    A.lootLabel:SetText("Добыча ещё не загружена")
  else
    A.lootLabel:SetText(m and ("Предметы: "..#loot.."   (наведите для подробностей)") or "")
  end
  FauxScrollFrame_Update(A.lootScroll,#loot,#A.lootScroll.rows,24)
  offset=FauxScrollFrame_GetOffset(A.lootScroll)
  for i,r in ipairs(A.lootScroll.rows) do
    local it=loot[i+offset]
    r.item=it
    if it then
      r:Show()
      local color=it.quality==4 and "|cffa335ee" or it.quality==3 and "|cff0070dd" or it.quality==2 and "|cff1eff00" or "|cffffffff"
      r.label:SetText(color..it.name.."|r"..(it.quest and " |cffffff00[Квест]|r" or ""))
      r.value:SetText(A.FormatChance(it))
      r.icon:SetTexture(GetItemIcon(it.id) or "Interface\\Icons\\INV_Misc_QuestionMark")
    else r:Hide() end
  end
  for tab,b in pairs(A.tabs) do if tab==A.lootTab then b:Disable() else b:Enable() end end
end
