-- Temucraft Missions: a window for mod-temucraft-followers (prefixes TMT and TCP).
-- Talks to the server by whispering itself addon messages; the server answers the same way.
-- /missions (or /tmt) opens it; "Open the mission table window" at a mission table opens it too.

-- the client MPQ builds the same window into the interface; then this addon copy does nothing
if TemucraftMissionsFrame then return end

local TMT, TCP = "TMT", "TCP"
local ROWS = 9

local THREATS = { "Hazardous Ground", "Lethal Lackeys", "Area Assault", "Hexes", "Crushing Blow",
                  "Hordes", "Arcane Barrage", "Race Against Time", "Berserk Fury" }
-- trait id -> { name, what it does } (same list as TemucraftMissionChance.h on the server)
local TRAIT_INFO = {
    [1] = { "Swift Feet", "Missions take 25% less time." }, [2] = { "Gold Nose", "+50% gold from missions." },
    [3] = { "Keen Mind", "+50% follower experience." }, [4] = { "Lucky", "A flat bonus to the success chance." },
    [5] = { "Watchful", "Better on patrol and exploration missions." }, [6] = { "Lone Wolf", "Better when sent alone." },
    [7] = { "Mentor", "The others count at the best level and item level of the group." },
    [8] = { "Long Haul", "Better on missions of 8 hours or more." }, [9] = { "Quick Strike", "Better on missions shorter than 4 hours." },
    [10] = { "Veteran", "A little better on every mission." },
    [11] = { "Friend of Humans", "Better with a Human follower." }, [12] = { "Friend of Orcs", "Better with an Orc follower." },
    [13] = { "Friend of Dwarves", "Better with a Dwarf follower." }, [14] = { "Friend of Elves", "Better with a Night Elf or Blood Elf follower." },
    [15] = { "Friend of Gnomes", "Better with a Gnome follower." }, [16] = { "Friend of Tauren", "Better with a Tauren follower." },
    [17] = { "Friend of Trolls", "Better with a Troll follower." }, [18] = { "Friend of the Forsaken", "Better with an Undead follower." },
    [19] = { "Friend of Draenei", "Better with a Draenei follower." },
}
local ENVS = { "Forest", "Mountains", "Plains", "Snow", "Desert", "Underground", "Swamp", "Jungle", "Town" }
local ENEMIES = { "Undead", "Beast", "Dragonkin", "Elemental", "Demon", "Giant", "Humanoid", "Troll", "Vrykul", "Nerubian",
                  "Construct", "Ogre", "Orc", "Harpy", "Aberration" }
local RACES = { [1] = "Human", [2] = "Orc", [3] = "Dwarf", [4] = "Night Elf", [5] = "Undead", [6] = "Tauren", [7] = "Gnome",
                [8] = "Troll", [10] = "Blood Elf", [11] = "Draenei" }
do
    local env = { "Woodland Guide", "Mountain Born", "Plains Runner", "Frost Hardened", "Sand Walker", "Tunnel Rat", "Bog Wader", "Jungle Stalker", "Street Wise" }
    for i, n in ipairs(env) do TRAIT_INFO[20 + i] = { n, "Better in " .. string.lower(ENVS[i]) .. " missions." } end
    local slayer = { "Undead", "Beast", "Dragon", "Elemental", "Demon", "Giant", "Bandit", "Troll", "Vrykul", "Nerubian", "Construct", "Ogre", "Orc", "Harpy", "Void" }
    for i, n in ipairs(slayer) do TRAIT_INFO[30 + i] = { n .. " Slayer", "Better against " .. string.lower(ENEMIES[i]) .. " enemies." } end
    local prof = { { "Alchemist", "Alchemy" }, { "Blacksmith", "Blacksmithing" }, { "Enchanter", "Enchanting" }, { "Engineer", "Engineering" },
                   { "Herbalist", "Herbalism" }, { "Scribe", "Inscription" }, { "Jeweler", "Jewelcrafting" }, { "Leatherworker", "Leatherworking" },
                   { "Miner", "Mining" }, { "Skinner", "Skinning" }, { "Tailor", "Tailoring" }, { "Cook", "Cooking" }, { "Angler", "Fishing" },
                   { "Medic", "First Aid" } }
    for i, n in ipairs(prof) do TRAIT_INFO[50 + i] = { n[1], n[2] .. ": can take " .. string.lower(n[2]) .. " missions." } end
end
local TRAITS = {}
for id, t in pairs(TRAIT_INFO) do TRAITS[id] = t[1] end
local TYPES = { [0] = "", [1] = " (patrol)", [2] = " (treasure)", [3] = " (rare)", [4] = " (exploration)", [5] = " (training)",
                [6] = " (logistics)", [7] = " (profession)" }
local STATES = { [0] = "", [1] = "|cffff9900on a mission|r", [2] = "|cff66ccffcompanion|r" }
local ROLE_BITS = { { 1, "Tank" }, { 2, "Healer" }, { 4, "Damage" } }

-- data from the server
local M = { followers = {}, order = {}, offers = {}, running = {}, near = false, money = 0, stamp = 0, server = false }
local C = { list = {}, active = 0, role = 255, here = 0, server = false }
local pendingM, pendingC = nil, nil
local selected, picked, chance = nil, {}, nil   -- the offer being staffed
local explain = nil                              -- the breakdown of the chance being collected (XB/XF/XT/XS lines)
local tab, scroll = 1, 0

-- ----------------------------------------------------------------------------------------------- helpers
local function Send(prefix, text)
    SendAddonMessage(prefix, text, "WHISPER", UnitName("player"))
end

local function Split(s, sep)
    local out = {}
    if not s or s == "" then return out end
    for part in string.gmatch(s .. sep, "(.-)" .. sep) do table.insert(out, part) end
    return out
end

local function Ids(s)
    local out = {}
    for _, v in ipairs(Split(s, ",")) do local n = tonumber(v); if n then table.insert(out, n) end end
    return out
end

local function Roles(mask)
    local out = {}
    for _, r in ipairs(ROLE_BITS) do if bit.band(mask, r[1]) ~= 0 then table.insert(out, r[2]) end end
    return #out > 0 and table.concat(out, "/") or "-"
end

local function Names(ids, names)
    local out = {}
    for _, id in ipairs(ids) do if names[id] then table.insert(out, names[id]) end end
    return #out > 0 and table.concat(out, ", ") or "-"
end

local function Time(sec)
    sec = math.max(0, math.floor(sec))
    if sec >= 3600 then return string.format("%dh %02dm", math.floor(sec / 3600), math.floor(sec % 3600 / 60)) end
    if sec >= 60 then return string.format("%dm %02ds", math.floor(sec / 60), sec % 60) end
    return sec .. "s"
end

local function Coins(copper)
    if copper <= 0 then return "free" end
    return GetCoinTextureString and GetCoinTextureString(copper) or (math.floor(copper / 10000) .. "g")
end

local function Colored(f)
    local q = ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[f.quality or 2]
    return q and (q.hex .. f.name .. "|r") or f.name
end

local function Size(f, w, h) f:SetWidth(w); f:SetHeight(h) end
local function Shown(f, on) if on then f:Show() else f:Hide() end end
local afterAction = false   -- a companion action may change follower states on the mission side

-- ----------------------------------------------------------------------------------------------- window
local W = CreateFrame("Frame", "TemucraftMissionsFrame", UIParent)
Size(W, 680, 460)
W:SetPoint("CENTER")
W:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
                tile = true, tileSize = 32, edgeSize = 32, insets = { left = 11, right = 12, top = 12, bottom = 11 } })
W:SetMovable(true); W:EnableMouse(true); W:RegisterForDrag("LeftButton")
W:SetScript("OnDragStart", W.StartMoving)
W:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
W:SetFrameStrata("HIGH")
W:Hide()
tinsert(UISpecialFrames, "TemucraftMissionsFrame")

local title = W:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -18)
title:SetText("Mission Table")

local close = CreateFrame("Button", nil, W, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local status = W:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
status:SetPoint("BOTTOMLEFT", 22, 20)
status:SetJustifyH("LEFT")

local function Button(parent, text, w, h)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    Size(b, w or 100, h or 22)
    b:SetText(text)
    return b
end

-- tabs
local TAB_NAMES = { "Missions", "In Progress", "Followers", "Companion" }
local tabs = {}
local Refresh
for i, name in ipairs(TAB_NAMES) do
    local b = Button(W, name, 110, 24)
    b:SetPoint("TOPLEFT", 20 + (i - 1) * 114, -44)
    b:SetScript("OnClick", function() tab = i; scroll = 0; Refresh() end)
    tabs[i] = b
end

-- list rows on the left (or full width)
local list = CreateFrame("Frame", nil, W)
list:SetPoint("TOPLEFT", 20, -76)
Size(list, 400, ROWS * 38)
list:EnableMouseWheel(true)
local rows = {}
for i = 1, ROWS do
    local r = CreateFrame("Button", nil, list)
    Size(r, 400, 36)
    r:SetPoint("TOPLEFT", 0, -(i - 1) * 38)
    r:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    r.bg = r:CreateTexture(nil, "BACKGROUND")
    r.bg:SetAllPoints()
    r.bg:SetTexture(0, 0, 0, 0.25)
    r.sel = r:CreateTexture(nil, "BORDER")
    r.sel:SetAllPoints()
    r.sel:SetTexture(1, 0.82, 0, 0.18)
    r.sel:Hide()
    r.top = r:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    r.top:SetPoint("TOPLEFT", 6, -4); r.top:SetPoint("RIGHT", -110, 0); r.top:SetJustifyH("LEFT")
    r.sub = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.sub:SetPoint("BOTTOMLEFT", 6, 4); r.sub:SetPoint("RIGHT", -6, 0); r.sub:SetJustifyH("LEFT")
    r.right = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.right:SetPoint("TOPRIGHT", -6, -5); r.right:SetJustifyH("RIGHT")
    r.btn = Button(r, "", 80, 18)
    r.btn:SetPoint("RIGHT", -4, 0)
    r.btn:Hide()
    rows[i] = r
end

-- detail panel on the right (missions tab)
local detail = CreateFrame("Frame", nil, W)
detail:SetPoint("TOPLEFT", 432, -76)
Size(detail, 228, 340)
detail:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
                     tile = true, tileSize = 16, edgeSize = 16, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
detail:SetBackdropColor(0, 0, 0, 0.6)
local dText = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
dText:SetPoint("TOPLEFT", 10, -10); dText:SetPoint("RIGHT", -10, 0)
dText:SetJustifyH("LEFT"); dText:SetJustifyV("TOP")
local dChance = detail:CreateFontString(nil, "OVERLAY", "GameFontNormal")
dChance:SetPoint("BOTTOMLEFT", 10, 40)
-- hover the chance for the breakdown the server sent (who adds what)
local chanceHover = CreateFrame("Frame", nil, detail)
chanceHover:SetPoint("BOTTOMLEFT", 6, 34)
Size(chanceHover, 216, 22)
chanceHover:EnableMouse(true)
local startBtn = Button(detail, "Start mission", 120, 22)
startBtn:SetPoint("BOTTOMLEFT", 8, 10)
local clearBtn = Button(detail, "Clear", 80, 22)
clearBtn:SetPoint("BOTTOMRIGHT", -8, 10)

-- buttons under the list
local claimBtn = Button(W, "Claim rewards", 130, 22)
claimBtn:SetPoint("BOTTOMRIGHT", -24, 18)
local refreshBtn = Button(W, "Refresh", 90, 22)
refreshBtn:SetPoint("RIGHT", claimBtn, "LEFT", -6, 0)
local dismissBtn = Button(W, "Dismiss", 90, 22)
local roleBtns = {}
for i, r in ipairs(ROLE_BITS) do
    local b = Button(W, r[2], 80, 22)
    roleBtns[i] = b
    b:SetScript("OnClick", function() Send(TCP, "ROLE~" .. (i - 1)) end)
end
dismissBtn:SetPoint("BOTTOMLEFT", 20, 40)
roleBtns[1]:SetPoint("LEFT", dismissBtn, "RIGHT", 16, 0)
roleBtns[2]:SetPoint("LEFT", roleBtns[1], "RIGHT", 4, 0)
roleBtns[3]:SetPoint("LEFT", roleBtns[2], "RIGHT", 4, 0)
-- follower dungeons: fill the group's free places with followers (5-player dungeons)
local partyBtn = Button(W, "Fill party", 90, 22)
partyBtn:SetPoint("LEFT", roleBtns[3], "RIGHT", 16, 0)
local partyOffBtn = Button(W, "Send party back", 120, 22)
partyOffBtn:SetPoint("LEFT", partyBtn, "RIGHT", 4, 0)

local function RequestAll()
    Send(TMT, "LIST")
    Send(TCP, "LIST")
end

local function PickedCsv()
    local ids = {}
    for _, id in ipairs(picked) do table.insert(ids, id) end
    return table.concat(ids, ",")
end

local function AskChance()
    if selected and #picked > 0 then Send(TMT, "CHANCE~" .. selected .. "~" .. PickedCsv()) else chance = nil end
end

local function IsPicked(id)
    for i, v in ipairs(picked) do if v == id then return i end end
end

-- idle followers that can do missions, for the selected offer
local function Candidates()
    local out = {}
    for _, id in ipairs(M.order) do
        local f = M.followers[id]
        if f.state == 0 and bit.band(f.mask, 1) ~= 0 then table.insert(out, f) end
    end
    return out
end

local function ShowRows(items, fill)
    local maxScroll = math.max(0, #items - ROWS)
    if scroll > maxScroll then scroll = maxScroll end
    for i = 1, ROWS do
        local r, item = rows[i], items[i + scroll]
        r.btn:Hide(); r.sel:Hide(); r:SetScript("OnClick", nil); r.btn:SetScript("OnClick", nil)
        r:SetScript("OnEnter", nil); r:SetScript("OnLeave", nil)
        if item then
            r.top:SetText(""); r.sub:SetText(""); r.right:SetText("")
            fill(r, item); r:Show()
        else
            r:Hide()
        end
    end
end

local function OfferList()
    local out = {}
    for slot, o in pairs(M.offers) do table.insert(out, o) end
    table.sort(out, function(a, b) if a.level ~= b.level then return a.level < b.level end return a.slot < b.slot end)
    return out
end

local function RunningList()
    local out = {}
    for slot, r in pairs(M.running) do table.insert(out, r) end
    table.sort(out, function(a, b) return a.ends < b.ends end)
    return out
end

local function NameOf(id)
    local f = M.followers[id]
    return f and Colored(f) or ("#" .. tostring(id))
end

local function Tenths(v) return string.format("%.1f", (v or 0) / 10) end

-- the success chance breakdown, as a tooltip
local function ShowExplain(owner)
    local o = selected and M.offers[selected]
    if not o or not chance then return end
    GameTooltip:SetOwner(owner, "ANCHOR_RIGHT")
    GameTooltip:AddLine("Success chance " .. chance.chance .. "%")
    local x = chance.explain
    if not x then
        GameTooltip:AddLine("No breakdown from the server.", 0.7, 0.7, 0.7)
        GameTooltip:Show()
        return
    end
    GameTooltip:AddLine("Base " .. x.base .. "% + (100 - base) x points / 100", 0.8, 0.8, 0.8)
    GameTooltip:AddDoubleLine("Points", Tenths(x.points), 1, 1, 1, 1, 1, 1)
    GameTooltip:AddLine("Total weight " .. x.weight .. " (" .. o.slots .. " x 100 + threats)", 0.6, 0.6, 0.6)
    if x.mentor then GameTooltip:AddLine("Mentor: everyone counts at the best level and item level.", 0.4, 0.8, 1) end
    GameTooltip:AddLine(" ")
    GameTooltip:AddLine("Followers", 1, 0.82, 0)
    for _, fp in ipairs(x.followers) do
        local b = fp.bias / 100
        local r, g = b < 0 and 1 or 0.6, b < 0 and 0.4 or 1
        GameTooltip:AddDoubleLine(NameOf(fp.id) .. string.format("  L%d / %d (bias %+.2f)", fp.level, fp.ilvl, b), "+" .. Tenths(fp.points), 1, 1, 1, r, g, 0.4)
    end
    if #x.threats > 0 then
        GameTooltip:AddLine(" ")
        GameTooltip:AddLine("Threats", 1, 0.82, 0)
        for _, tp in ipairs(x.threats) do
            local label = (THREATS[tp.threat] or "?") .. (tp.count > 1 and (" x" .. tp.count) or "") .. " (" .. tp.weight .. ")"
            if #tp.by > 0 then
                local who = {}
                for _, id in ipairs(tp.by) do table.insert(who, NameOf(id)) end
                GameTooltip:AddDoubleLine(label .. " - " .. table.concat(who, ", "), "+" .. Tenths(tp.points), 0.6, 1, 0.6, 0.6, 1, 0.6)
            else
                GameTooltip:AddDoubleLine(label, "not countered", 1, 0.4, 0.4, 1, 0.4, 0.4)
            end
        end
    end
    if #x.traits > 0 then
        GameTooltip:AddLine(" ")
        GameTooltip:AddLine("Traits", 1, 0.82, 0)
        for _, tp in ipairs(x.traits) do
            GameTooltip:AddDoubleLine((TRAITS[tp.trait] or "?") .. " - " .. NameOf(tp.follower), "+" .. Tenths(tp.points), 0.4, 0.8, 1, 0.4, 0.8, 1)
        end
    end
    if x.lucky > 0 then GameTooltip:AddDoubleLine("Lucky", "+" .. Tenths(x.lucky) .. "%", 0.4, 0.8, 1, 0.4, 0.8, 1) end
    if chance.chance > 100 then
        GameTooltip:AddLine(" ")
        GameTooltip:AddLine("Bonus reward chance: " .. (chance.chance - 100) .. "%", 0.2, 1, 0.2)
    end
    GameTooltip:Show()
end
chanceHover:SetScript("OnEnter", ShowExplain)
chanceHover:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- a follower's traits, with what they do
local function FollowerTooltip(owner, f)
    GameTooltip:SetOwner(owner, "ANCHOR_RIGHT")
    GameTooltip:AddLine(Colored(f))
    GameTooltip:AddLine((RACES[f.race] or "") .. "  Level " .. f.level .. ", item level " .. f.ilvl, 0.8, 0.8, 0.8)
    if #f.abilities > 0 then GameTooltip:AddLine("Counters: " .. Names(f.abilities, THREATS), 0.6, 1, 0.6, true) end
    for _, t in ipairs(f.traits) do
        local info = TRAIT_INFO[t]
        if info then GameTooltip:AddLine(info[1] .. ": " .. info[2], 0.4, 0.8, 1, true) end
    end
    GameTooltip:Show()
end

local function UpdateDetail()
    local o = selected and M.offers[selected]
    if not o then
        dText:SetText("Choose a mission on the left.\n\nThen pick followers below it with the Send buttons.")
        dChance:SetText(""); startBtn:Disable(); clearBtn:Disable()
        return
    end
    local lines = { "|cffffd100" .. o.name .. (TYPES[o.type] or "") .. "|r",
                    "Level " .. o.level .. (o.ilvl > 0 and (", item level " .. o.ilvl) or "") .. (o.base and ("    Base " .. o.base .. "%") or ""),
                    (o.location ~= "" and (o.location .. "  -  ") or "") .. (ENVS[o.env] or "anywhere"),
                    "Enemies: " .. (ENEMIES[o.enemy] or "mixed"),
                    "Duration: " .. Time(o.seconds) .. "    Cost: " .. Coins(o.cost),
                    "Followers: " .. #picked .. " / " .. o.slots .. "    Expires in " .. Time(o.expires - (GetTime() - M.stamp)) }
    if o.req > 0 then table.insert(lines, "|cffff9900Needs a follower with " .. (TRAITS[o.req] or "?") .. "|r") end
    table.insert(lines, ""); table.insert(lines, "|cffffd100Threats|r (weight)")
    if #o.threats == 0 then table.insert(lines, "  none") end
    for i, t in ipairs(o.threats) do table.insert(lines, "  " .. (THREATS[t] or "?") .. "  |cffaaaaaa(" .. (o.weights[i] or 300) .. ")|r") end
    table.insert(lines, ""); table.insert(lines, "|cffffd100Sent|r")
    if #picked == 0 then table.insert(lines, "  nobody yet") end
    for _, id in ipairs(picked) do local f = M.followers[id]; if f then table.insert(lines, "  " .. Colored(f)) end end
    dText:SetText(table.concat(lines, "\n"))
    if chance then
        local c = chance.chance
        local color = c >= 100 and "|cff20ff20" or c >= 60 and "|cffffff00" or "|cffff4040"
        dChance:SetText("Success: " .. color .. c .. "%|r  (" .. chance.countered .. "/" .. chance.threats .. " countered)  |cffaaaaaa[?]|r")
    else
        dChance:SetText("")
    end
    if #picked == o.slots and M.near then startBtn:Enable() else startBtn:Disable() end
    if #picked > 0 then clearBtn:Enable() else clearBtn:Disable() end
end

function Refresh()
    if not W:IsShown() then return end
    for i, b in ipairs(tabs) do if i == tab then b:LockHighlight() else b:UnlockHighlight() end end
    Shown(tabs[4], C.server)
    Shown(detail, tab == 1)
    list:SetWidth(tab == 1 and 400 or 640)
    for _, r in ipairs(rows) do r:SetWidth(tab == 1 and 400 or 640) end
    Shown(claimBtn, tab ~= 4); refreshBtn:Show()
    Shown(dismissBtn, tab == 4)
    Shown(partyBtn, tab == 4)
    Shown(partyOffBtn, tab == 4)
    for _, b in ipairs(roleBtns) do Shown(b, tab == 4) end

    local ready = 0
    for _, r in pairs(M.running) do if r.ends - GetTime() <= 0 then ready = ready + 1 end end
    if ready > 0 and M.near then claimBtn:Enable() else claimBtn:Disable() end
    claimBtn:SetText(ready > 0 and ("Claim rewards (" .. ready .. ")") or "Claim rewards")

    if not M.server then
        status:SetText("|cffff4040No answer from the server yet.|r The server needs mod-temucraft-followers.")
    elseif not M.near then
        status:SetText("You can look from here; go to a mission table to start missions or claim rewards.")
    else
        status:SetText("At the mission table.  Gold: " .. Coins(M.money))
    end

    if tab == 1 then
        -- offers, and when one is chosen, the followers that can go
        local items = {}
        for _, o in ipairs(OfferList()) do table.insert(items, { kind = "offer", o = o }) end
        if selected and M.offers[selected] then
            for _, f in ipairs(Candidates()) do table.insert(items, { kind = "pick", f = f }) end
        end
        ShowRows(items, function(r, it)
            if it.kind == "offer" then
                local o = it.o
                r.top:SetText((o.type == 3 and "|cffa335ee" or "|cffffd100") .. o.name .. TYPES[o.type] .. "|r")
                r.sub:SetText("Level " .. o.level .. "  -  " .. o.slots .. " follower(s)  -  " .. Time(o.seconds) .. "  -  " .. Coins(o.cost))
                r.right:SetText((ENVS[o.env] and (ENVS[o.env] .. "  -  ") or "") .. #o.threats .. " threat(s)")
                if o.slot == selected then r.sel:Show() end
                r:SetScript("OnClick", function() selected = o.slot; picked = {}; chance = nil; Refresh() end)
            else
                local f = it.f
                r.top:SetText("   " .. Colored(f) .. "  |cffaaaaaa(" .. Roles(f.roles) .. ")|r")
                local o = M.offers[selected]
                local counters = {}
                for _, a in ipairs(f.abilities) do
                    local hit = false
                    for _, t in ipairs(o.threats) do if t == a then hit = true end end
                    table.insert(counters, (hit and "|cff20ff20" or "") .. (THREATS[a] or "?") .. (hit and "|r" or ""))
                end
                r.sub:SetText("   Level " .. f.level .. ", item level " .. f.ilvl .. "  -  counters: " .. (#counters > 0 and table.concat(counters, ", ") or "-")
                    .. "  -  " .. Names(f.traits, TRAITS))
                r:SetScript("OnEnter", function(self) FollowerTooltip(self, f) end)
                r:SetScript("OnLeave", function() GameTooltip:Hide() end)
                local n = IsPicked(f.id)
                if n then r.sel:Show() end
                r.btn:SetText(n and "Remove" or "Send"); r.btn:Show()
                r.btn:SetScript("OnClick", function()
                    local o = M.offers[selected]
                    if not o then return end
                    local i = IsPicked(f.id)
                    if i then table.remove(picked, i) elseif #picked < o.slots then table.insert(picked, f.id) end
                    chance = nil; AskChance(); Refresh()
                end)
            end
        end)
        UpdateDetail()
    elseif tab == 2 then
        ShowRows(RunningList(), function(r, m)
            local left = m.ends - GetTime()
            r.top:SetText("|cffffd100" .. m.name .. "|r")
            local names = {}
            for _, id in ipairs(m.followers) do local f = M.followers[id]; if f then table.insert(names, Colored(f)) end end
            r.sub:SetText(table.concat(names, ", "))
            r.right:SetText(m.chance .. "%  -  " .. (left > 0 and Time(left) or "|cff20ff20done|r"))
        end)
    elseif tab == 3 then
        local items = {}
        for _, id in ipairs(M.order) do table.insert(items, M.followers[id]) end
        ShowRows(items, function(r, f)
            r.top:SetText(Colored(f) .. "  |cffaaaaaa(" .. Roles(f.roles) .. ")|r  " .. (STATES[f.state] or ""))
            r.sub:SetText((RACES[f.race] and (RACES[f.race] .. "   ") or "") .. "Counters: " .. Names(f.abilities, THREATS) .. "   Traits: " .. Names(f.traits, TRAITS))
            r:SetScript("OnEnter", function(self) FollowerTooltip(self, f) end)
            r:SetScript("OnLeave", function() GameTooltip:Hide() end)
            r.right:SetText("Level " .. f.level .. (f.xpNext > 0 and (" (" .. f.xp .. "/" .. f.xpNext .. " xp)") or " (max)") .. "  -  ilvl " .. f.ilvl)
        end)
    elseif tab == 4 then
        ShowRows(C.list, function(r, c)
            local here = bit.band(c.mask, C.here) ~= 0
            local roleName = ""
            if c.id == C.active then
                for i, rb in ipairs(ROLE_BITS) do if C.role == i - 1 then roleName = " as " .. rb[2] end end
            end
            r.top:SetText(c.name .. "  |cffaaaaaa(" .. Roles(c.roles) .. ")|r  " .. (c.id == C.active and ("|cff66ccffwith you" .. roleName .. "|r") or (STATES[c.state] or "")))
            r.sub:SetText("Companion level " .. c.level .. (c.xpNext > 0 and (" (" .. c.xp .. "/" .. c.xpNext .. " xp)") or " (max)")
                .. (here and "" or "  -  |cffff4040can't come here|r"))
            if c.id ~= C.active then
                r.btn:SetText("Summon"); r.btn:Show()
                if here and c.state ~= 1 then r.btn:Enable() else r.btn:Disable() end
                r.btn:SetScript("OnClick", function() afterAction = true; Send(TCP, "SUMMON~" .. c.id) end)
            end
        end)
        local activeRoles = 0
        for _, c in ipairs(C.list) do if c.id == C.active then activeRoles = c.roles end end
        if C.active > 0 then dismissBtn:Enable() else dismissBtn:Disable() end
        for i, b in ipairs(roleBtns) do
            if C.active > 0 and bit.band(activeRoles, ROLE_BITS[i][1]) ~= 0 and C.role ~= i - 1 then b:Enable() else b:Disable() end
        end
        if not C.server then status:SetText("|cffff4040No answer from the server.|r The server needs mod-temucraft-followers.") end
    end
end

list:SetScript("OnMouseWheel", function(_, delta) scroll = math.max(0, scroll - delta); Refresh() end)
startBtn:SetScript("OnClick", function() if selected then Send(TMT, "START~" .. selected .. "~" .. PickedCsv()) end end)
clearBtn:SetScript("OnClick", function() picked = {}; chance = nil; Refresh() end)
claimBtn:SetScript("OnClick", function() Send(TMT, "CLAIM") end)
refreshBtn:SetScript("OnClick", RequestAll)
dismissBtn:SetScript("OnClick", function() afterAction = true; Send(TCP, "DISMISS") end)
partyBtn:SetScript("OnClick", function() afterAction = true; Send(TCP, "PARTY") end)
partyOffBtn:SetScript("OnClick", function() afterAction = true; Send(TCP, "PARTYDISMISS") end)
W:SetScript("OnShow", function() RequestAll(); Refresh() end)

-- countdowns
local tick = 0
W:SetScript("OnUpdate", function(_, elapsed)
    tick = tick + elapsed
    if tick >= 1 then tick = 0; if tab == 1 or tab == 2 then Refresh() end end
end)

-- ----------------------------------------------------------------------------------------------- messages
local function OnMission(msg)
    local a = Split(msg, "~")
    local k = a[1]
    if k == "HI" then M.server = true
    elseif k == "OPEN" then if not W:IsShown() then W:Show() end
    elseif k == "BEGIN" then
        pendingM = { followers = {}, order = {}, offers = {}, running = {}, money = tonumber(a[3]) or 0, near = a[4] == "1", stamp = GetTime(), server = true }
    elseif not pendingM then return
    elseif k == "F" then
        local f = { id = tonumber(a[2]), name = a[3], roles = tonumber(a[4]) or 4, level = tonumber(a[5]) or 1, xp = tonumber(a[6]) or 0,
                    xpNext = tonumber(a[7]) or 0, ilvl = tonumber(a[8]) or 0, quality = tonumber(a[9]) or 2, state = tonumber(a[10]) or 0,
                    mask = tonumber(a[11]) or 0, abilities = Ids(a[12]), traits = Ids(a[13]), race = tonumber(a[14]) or 0 }
        pendingM.followers[f.id] = f; table.insert(pendingM.order, f.id)
    elseif k == "O" then
        local o = { slot = tonumber(a[2]), name = a[3], type = tonumber(a[4]) or 0, level = tonumber(a[5]) or 1, ilvl = tonumber(a[6]) or 0,
                    seconds = tonumber(a[7]) or 0, slots = tonumber(a[8]) or 1, cost = tonumber(a[9]) or 0, threats = Ids(a[10]), expires = tonumber(a[11]) or 0,
                    env = tonumber(a[12]) or 0, enemy = tonumber(a[13]) or 0, base = tonumber(a[14]), weights = Ids(a[15]),
                    req = tonumber(a[16]) or 0, location = a[17] or "" }
        pendingM.offers[o.slot] = o
    elseif k == "R" then
        local r = { slot = tonumber(a[2]), name = a[3], chance = tonumber(a[4]) or 0, ends = GetTime() + (tonumber(a[5]) or 0), followers = Ids(a[6]) }
        pendingM.running[r.slot] = r
    elseif k == "END" then
        M = pendingM; pendingM = nil
        if selected and not M.offers[selected] then selected = nil; picked = {}; chance = nil end
        local keep = {}
        for _, id in ipairs(picked) do local f = M.followers[id]; if f and f.state == 0 then table.insert(keep, id) end end
        picked = keep
        AskChance(); Refresh()
    end
end

local function OnChance(msg)
    local a = Split(msg, "~")
    local k = a[1]
    if tonumber(a[2]) ~= selected then return end
    if k == "XB" then
        explain = { base = tonumber(a[3]) or 0, weight = tonumber(a[4]) or 0, points = tonumber(a[5]) or 0, lucky = tonumber(a[6]) or 0,
                    mentor = a[7] == "1", followers = {}, threats = {}, traits = {} }
    elseif k == "XF" and explain then
        table.insert(explain.followers, { id = tonumber(a[3]), level = tonumber(a[4]) or 0, ilvl = tonumber(a[5]) or 0,
                                          bias = tonumber(a[6]) or 0, points = tonumber(a[7]) or 0 })
    elseif k == "XT" and explain then
        table.insert(explain.threats, { threat = tonumber(a[3]) or 0, count = tonumber(a[4]) or 1, weight = tonumber(a[5]) or 0,
                                        removed = tonumber(a[6]) or 0, points = tonumber(a[7]) or 0, by = Ids(a[8]) })
    elseif k == "XS" and explain then
        table.insert(explain.traits, { follower = tonumber(a[3]), trait = tonumber(a[4]) or 0, points = tonumber(a[5]) or 0 })
    elseif k == "C" then
        chance = { chance = tonumber(a[3]) or 0, countered = tonumber(a[4]) or 0, threats = tonumber(a[5]) or 0, explain = explain }
        explain = nil
        Refresh()
    end
end

local function OnCompanion(msg)
    local a = Split(msg, "~")
    local k = a[1]
    if k == "HI" then C.server = true
    elseif k == "BEGIN" then pendingC = { list = {}, active = tonumber(a[2]) or 0, role = tonumber(a[3]) or 255, here = tonumber(a[4]) or 0, server = true }
    elseif not pendingC then return
    elseif k == "F" then
        table.insert(pendingC.list, { id = tonumber(a[2]), name = a[3], roles = tonumber(a[4]) or 4, level = tonumber(a[5]) or 1, xp = tonumber(a[6]) or 0,
                                      xpNext = tonumber(a[7]) or 0, state = tonumber(a[8]) or 0, mask = tonumber(a[9]) or 0, role = tonumber(a[10]) or 255 })
    elseif k == "END" then
        C = pendingC; pendingC = nil
        if afterAction and W:IsShown() then Send(TMT, "LIST") end   -- follower states may have changed
        afterAction = false
        Refresh()
    end
end

local ev = CreateFrame("Frame")
ev:RegisterEvent("CHAT_MSG_ADDON")
ev:RegisterEvent("PLAYER_ENTERING_WORLD")
ev:SetScript("OnEvent", function(_, event, prefix, msg, channel, sender)
    if event == "PLAYER_ENTERING_WORLD" then
        Send(TMT, "HELLO"); Send(TCP, "HELLO")
        return
    end
    if channel ~= "WHISPER" or sender ~= UnitName("player") then return end
    if prefix == TMT then
        local k = string.sub(msg, 1, 3)
        if string.sub(msg, 1, 2) == "C~" or k == "XB~" or k == "XF~" or k == "XT~" or k == "XS~" then OnChance(msg) else OnMission(msg) end
    elseif prefix == TCP then
        OnCompanion(msg)
    end
end)

SLASH_TEMUCRAFTMISSIONS1 = "/missions"
SLASH_TEMUCRAFTMISSIONS2 = "/tmt"
SlashCmdList["TEMUCRAFTMISSIONS"] = function()
    if W:IsShown() then W:Hide() else W:Show() end
end
SLASH_TEMUCRAFTCOMPANION1 = "/companions"
SlashCmdList["TEMUCRAFTCOMPANION"] = function() tab = 4; scroll = 0; if W:IsShown() then Refresh() else W:Show() end end
