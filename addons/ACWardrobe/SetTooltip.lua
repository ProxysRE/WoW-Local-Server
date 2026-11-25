-- SPDX-License-Identifier: AGPL-3.0-or-later
-- Wrath counts set pieces from PLAYER_VISIBLE_ITEM_*; transmog changes those IDs.
-- Only repair tooltip text/colors. Never change equipment or server set effects.
local function Plain(text)
    return (text or ""):gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", ""):gsub("^%s+", ""):gsub("%s+$", "")
end

local function Repair(lines, equipped, itemName)
    for index, line in ipairs(lines) do
        local prefix, oldCount, total = Plain(line.text):match("^(.-)%((%d+)%s*/%s*(%d+)%)$")
        total = tonumber(total)
        if prefix and total and total >= 2 and total <= 17 and index + total <= #lines then
            local names, count, belongs = {}, 0, false
            for offset = 1, total do
                local name = Plain(lines[index + offset].text)
                if name == Plain(itemName) then belongs = true end
                names[offset] = name
                if equipped[name] then count = count + 1 end
            end
            -- Avoid unrelated fractions or tooltip sections supplied by other addons.
            if belongs then
                if count == tonumber(oldCount) then return false end
                line.text = prefix .. "(" .. count .. "/" .. total .. ")"
                for offset, name in ipairs(names) do
                    local piece = lines[index + offset]
                    piece.text = name
                    piece.color = equipped[name] and {0, 1, 0} or {0.5, 0.5, 0.5}
                end
                -- Preserve unmet red requirements (for example a profession requirement).
                local blocked = false
                for i, candidate in ipairs(lines) do
                    if i < index or i > index + total then
                        local c = candidate.color
                        if c and c[1] > 0.8 and c[2] < 0.25 and c[3] < 0.25 then blocked = true end
                    end
                end
                for i = index + total + 1, #lines do
                    local bonus = lines[i]
                    local threshold = tonumber(Plain(bonus.text):match("^%((%d+)%)%s"))
                    local c = bonus.color
                    local gray = c and math.abs(c[1]-c[2]) < 0.08 and math.abs(c[2]-c[3]) < 0.08
                    local green = c and c[1] < 0.15 and c[2] > 0.8 and c[3] < 0.15
                    if threshold and threshold <= total and (gray or green) then
                        if count < threshold or not blocked then
                            bonus.text = Plain(bonus.text)
                            bonus.color = count >= threshold and {0, 1, 0} or {0.5, 0.5, 0.5}
                        end
                    end
                end
                return true
            end
        end
    end
    return false
end

local function Update(tooltip)
    if tooltip.acwSetBusy or not tooltip.acwSetPending then return end
    tooltip.acwSetPending = nil
    if tooltip.acwSetUnit and not UnitIsUnit(tooltip.acwSetUnit, "player") then return end
    local itemName = tooltip:GetItem()
    local name = tooltip:GetName()
    if not itemName or not name then return end
    local equipped = {}
    for slot = 1, 19 do
        local link = GetInventoryItemLink("player", slot)
        if link then
            local actualName = GetItemInfo(link)
            -- Do not correct a tooltip with an incomplete inventory cache.
            if not actualName then return end
            equipped[Plain(actualName)] = true
        end
    end
    local lines = {}
    for i = 1, tooltip:NumLines() do
        local font = _G[name .. "TextLeft" .. i]
        if not font then return end
        local r, g, b = font:GetTextColor()
        lines[i] = {text=font:GetText(), color={r,g,b}, font=font}
    end
    if Repair(lines, equipped, itemName) then
        tooltip.acwSetBusy = true
        for _, line in ipairs(lines) do
            line.font:SetText(line.text)
            line.font:SetTextColor(unpack(line.color))
        end
        tooltip.acwSetBusy = nil
    end
end

for _, tooltip in ipairs({GameTooltip, ItemRefTooltip, ShoppingTooltip1, ShoppingTooltip2}) do
    tooltip:HookScript("OnTooltipCleared", function(self) self.acwSetPending=nil; self.acwSetUnit=nil end)
    tooltip:HookScript("OnTooltipSetItem", function(self) self.acwSetPending=true end)
    -- Run after SetInventoryItem has established the inspected unit, not inside it.
    hooksecurefunc(tooltip, "SetInventoryItem", function(self, unit) self.acwSetUnit=unit end)
    tooltip:HookScript("OnUpdate", Update)
end
