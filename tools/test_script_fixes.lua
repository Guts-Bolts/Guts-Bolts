-- Checks the Roblox-script fixes in a real play test: CharacterAdded / CharacterRemoving,
-- Model.PrimaryPart and pivots, PhysicalProperties, SpawnLocation teams, TextLabel.Bold
-- and Luau syntax (+=, continue, types) in scripts made in Studio.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_script_fixes.lua)" \
--       --screenshot /tmp/script_fixes.png --frames 3000
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "SCRIPT FIXES TEST DONE".
-- (This file itself uses += and continue: the Command Bar compiles it like any script.)
task.spawn(function()
    local fails, passes = 0, 0
    local function check(ok, what, extra)
        if ok then passes += 1 print("PASS " .. what)
        else fails += 1 warn("FAIL " .. what .. (extra ~= nil and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function near(a, b, eps) return (a - b).Magnitude <= (eps or 1e-3) end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local function waitFor(test, seconds)
        local t = 0
        while not test() and t < (seconds or 5) do t += task.wait() end
        return test()
    end

    -- Legacy enum enumeration: old scripts use this to select a random material.
    local materials = Enum.Material:GetEnumItems()
    check(type(materials) == "table" and #materials > 0, "Enum.Material:GetEnumItems returns material names")
    local hasNeon = false
    for _, material in ipairs(materials) do if material == "Neon" then hasNeon = true end end
    check(hasNeon, "material enum list contains Neon")
    check(type(Enum.Font:GetEnumItems()) == "table" and #Enum.Font:GetEnumItems() > 0,
          "Enum.Font:GetEnumItems returns font names")

    local Players = game:GetService("Players")
    local me = Players.LocalPlayer
    local character = me.Character

    -- 6. Luau extras ------------------------------------------------------------------
    -- In the Command Bar (this file) ...
    local sum: number = 0
    for i = 1, 10 do
        if i % 2 == 0 then continue end
        sum += i
    end
    check(sum == 25, "Luau += / continue / types in the Command Bar", sum)
    -- ... in a Script made in Studio (not imported) ...
    local s = Instance.new("Script")
    s.Name = "LuauScript"
    s.Source = [[
type Point = { x: number, y: number }
local total: number = 0
for i = 1, 10 do
    if i % 2 == 0 then
        continue
    end
    total += i
end
local p: Point = { x = 1, y = 2 }
local words = `sum {total} at {p.x}`
workspace:SetAttribute("LuauTotal", total)
workspace:SetAttribute("LuauWords", words)
]]
    s.Parent = workspace
    check(waitFor(function() return workspace:GetAttribute("LuauTotal") ~= nil end, 2), "a Luau Script runs")
    check(workspace:GetAttribute("LuauTotal") == 25, "+= and continue work in a Script", workspace:GetAttribute("LuauTotal"))
    check(workspace:GetAttribute("LuauWords") == "sum 25 at 1", "string interpolation works", workspace:GetAttribute("LuauWords"))
    -- ... and in ModuleScripts, keeping line numbers (blank lines inside a removed type too).
    local function module(name, source)
        local m = Instance.new("ModuleScript")
        m.Name = name
        m.Source = source
        m.Parent = workspace
        return m
    end
    local ok, err = pcall(require, module("LuauRuntime",
        "type Point = {\n\n  x: number,\n  y: number\n}\nlocal n: number = 1\nn += 1\nif n > 1 then\n  error(\"boom\")\nend\nreturn n\n"))
    check(not ok and tostring(err):find("LuauRuntime:9: boom", 1, true) ~= nil, "a runtime error points at the right line", err)
    ok, err = pcall(require, module("LuauSyntax", "local a: number = 1\na += 1\n\nlocal = 5\nreturn a\n"))
    check(not ok and tostring(err):find("LuauSyntax:4:", 1, true) ~= nil, "a syntax error points at the right line", err)
    -- Plain Lua is left exactly as written (a variable called continue, goto labels).
    ok, err = pcall(require, module("PlainContinue", "local continue = 41\ncontinue = continue + 1\nreturn continue\n"))
    check(ok and err == 42, "plain Lua with a variable called continue", err)
    ok, err = pcall(require, module("PlainGoto",
        "local n = 0\nfor i = 1, 3 do\n  if i == 2 then goto skip end\n  n = n + i\n  ::skip::\nend\nreturn n\n"))
    check(ok and err == 4, "plain Lua goto labels", err)

    -- 5. TextLabel.Bold ---------------------------------------------------------------
    for _, cls in ipairs({ "TextLabel", "TextButton", "TextBox" }) do
        local label = Instance.new(cls)
        check(label.Bold == false and label.Font == "SourceSans", cls .. ".Bold starts false")
        label.Bold = true
        check(label.Bold == true and label.Font == "SourceSansBold", cls .. ".Bold = true makes Font SourceSansBold", label.Font)
        label.Font = Enum.Font.SourceSans
        check(label.Bold == false, cls .. ".Font = SourceSans turns Bold off")
        label.Font = Enum.Font.GothamBold
        check(label.Bold == true, cls .. ".Font = GothamBold turns Bold on")
        label:Destroy()
    end

    -- 3. PhysicalProperties -----------------------------------------------------------
    local pp = PhysicalProperties.new(0.5, 0.1, 0.9, 2, 3)
    check(pp.Density == 0.5 and pp.Friction == 0.1 and pp.Elasticity == 0.9 and pp.FrictionWeight == 2 and pp.ElasticityWeight == 3,
          "PhysicalProperties.new keeps its five values", pp)
    local three = PhysicalProperties.new(2, 0.4, 0.2)
    check(three.FrictionWeight == 1 and three.ElasticityWeight == 1, "the weights default to 1")
    check(not pcall(function() pp.Density = 3 end), "PhysicalProperties can't be changed")
    local wood = PhysicalProperties.new(Enum.Material.Wood)
    check(math.abs(wood.Density - 0.7) < 1e-6, "PhysicalProperties.new(Enum.Material.Wood)", wood)
    check(PhysicalProperties.new(200, -1, 5).Density == 100, "values are kept in range")
    local part = Instance.new("Part")
    part.Anchored = true
    part.Position = Vector3.new(0, 40, 0)
    part.Parent = workspace
    check(part.CustomPhysicalProperties == nil, "CustomPhysicalProperties starts nil")
    check(math.abs(part.CurrentPhysicalProperties.Density - 1) < 1e-6, "CurrentPhysicalProperties is the material's")
    part.CustomPhysicalProperties = pp
    local back = part.CustomPhysicalProperties
    check(back ~= nil and math.abs(back.Density - 0.5) < 1e-4 and math.abs(back.Friction - 0.1) < 1e-4 and
          math.abs(back.Elasticity - 0.9) < 1e-4 and back.FrictionWeight == 2 and back.ElasticityWeight == 3,
          "CustomPhysicalProperties reads back", back)
    check(math.abs(part.Density - 0.5) < 1e-4 and math.abs(part:GetMass() - 0.5 * part.Size.X * part.Size.Y * part.Size.Z) < 0.05, "it changes the part's density and mass", part:GetMass())
    local copy = part:Clone()   -- (copies go through the save format)
    local cb = copy.CustomPhysicalProperties
    check(cb ~= nil and math.abs(cb.Density - 0.5) < 1e-4 and cb.FrictionWeight == 2 and cb.ElasticityWeight == 3,
          "CustomPhysicalProperties is saved (a copy has it)")
    part.CustomPhysicalProperties = nil
    check(part.CustomPhysicalProperties == nil and math.abs(part.Density - 1) < 1e-6, "setting nil goes back to the material")
    copy:Destroy()
    part:Destroy()
    -- It changes the physics: a ball with Elasticity 1 (and a big ElasticityWeight) bounces
    -- far higher than a plain one dropped next to it.
    local floor = Instance.new("Part")
    floor.Anchored = true
    floor.Size = Vector3.new(40, 1, 20)
    floor.Position = Vector3.new(300, 0, 300)
    floor.Parent = workspace
    local function ball(x, props)
        local b = Instance.new("Part")
        b.Shape = Enum.PartType.Ball
        b.Size = Vector3.new(2, 2, 2)
        b.Position = Vector3.new(x, 12, 300)
        b.CustomPhysicalProperties = props
        b.Anchored = false
        b.Parent = workspace
        return b
    end
    local bouncy = ball(292, PhysicalProperties.new(1, 0.3, 1, 1, 100))
    local plain = ball(308, nil)
    local landed, best = {}, {}
    local t = 0
    while t < 3.5 do
        t += task.wait()
        for _, b in ipairs({ bouncy, plain }) do
            local y = b.Position.Y
            if y < 2.5 then landed[b] = true end
            if landed[b] then best[b] = math.max(best[b] or 0, y) end
        end
    end
    check(landed[bouncy] and landed[plain], "both balls landed")
    check((best[bouncy] or 0) > (best[plain] or 0) + 3, "the bouncy ball bounces higher",
          tostring(best[bouncy]) .. " vs " .. tostring(best[plain]))
    bouncy:Destroy() plain:Destroy() floor:Destroy()

    -- 2. Model.PrimaryPart, GetPivot / PivotTo / SetPrimaryPartCFrame ---------------------
    local m = Instance.new("Model")
    m.Name = "PivotModel"
    local a = Instance.new("Part")
    a.Name = "A"
    a.Anchored = true
    a.Size = Vector3.new(2, 2, 2)
    a.Position = Vector3.new(0, 50, 0)
    a.Parent = m
    local b = Instance.new("Part")
    b.Name = "B"
    b.Anchored = true
    b.Size = Vector3.new(2, 2, 2)
    b.Position = Vector3.new(4, 50, 0)
    b.Parent = m
    m.Parent = workspace
    check(m.PrimaryPart == nil, "PrimaryPart starts nil")
    m.PrimaryPart = b
    check(m.PrimaryPart == b, "PrimaryPart reads back")
    check(near(m:GetPivot().Position, b.Position), "GetPivot is the PrimaryPart's CFrame", m:GetPivot().Position)
    m:PivotTo(CFrame.new(10, 60, 10))
    check(near(b.Position, Vector3.new(10, 60, 10)), "PivotTo puts the PrimaryPart there", b.Position)
    check(near(a.Position, Vector3.new(6, 60, 10)), "...and the rest of the model comes along", a.Position)
    m:SetPrimaryPartCFrame(CFrame.new(0, 70, 0) * CFrame.Angles(0, math.rad(90), 0))
    check(near(b.Position, Vector3.new(0, 70, 0)), "SetPrimaryPartCFrame moves it", b.Position)
    check(math.abs((a.Position - b.Position).Magnitude - 4) < 1e-3 and math.abs(a.Position.Y - 70) < 1e-3 and
          math.abs(a.Position.X) < 1e-3, "...and turns the model round it", a.Position)
    check(near(m:GetPrimaryPartCFrame().Position, Vector3.new(0, 70, 0)), "GetPrimaryPartCFrame")
    local mc = m:Clone()
    check(mc.PrimaryPart ~= nil and mc.PrimaryPart ~= b and mc.PrimaryPart.Parent == mc and mc.PrimaryPart.Name == "B",
          "a copied model's PrimaryPart is its own copy (saved and remapped)")
    mc:Destroy()
    local outside = Instance.new("Part")
    check(not pcall(function() m.PrimaryPart = outside end), "PrimaryPart must be inside the model")
    outside:Destroy()
    m.PrimaryPart = nil
    check(m.PrimaryPart == nil, "PrimaryPart = nil clears it")
    check(not pcall(function() m:SetPrimaryPartCFrame(CFrame.new()) end), "SetPrimaryPartCFrame needs a PrimaryPart")
    m:Destroy()
    check(character.PrimaryPart == character.HumanoidRootPart, "a character's PrimaryPart is its HumanoidRootPart")

    -- 4. SpawnLocation teams ----------------------------------------------------------
    local red, blue = Color3.new(1, 0, 0), Color3.new(0, 0, 1)
    local teams = workspace:FindFirstChild("Teams") or Instance.new("Folder")
    teams.Name = "Teams"
    teams.Parent = workspace
    local function team(name, color)
        local t = Instance.new("Model")
        t.Name = name
        t:SetAttribute("RobloxClass", "Team")
        t:SetAttribute("TeamColor", color)
        t:SetAttribute("AutoAssignable", false)
        t.Parent = teams
        return t
    end
    local redTeam, blueTeam = team("Red", red), team("Blue", blue)
    -- Every spawn already in the game belongs to nobody for now (so where we spawn is certain).
    local old = {}
    for _, d in ipairs(workspace:GetDescendants()) do
        if d:IsA("SpawnLocation") then old[d] = d.Neutral d.Neutral = false d.TeamColor = Color3.new(0.5, 0.5, 0.5) end
    end
    local function spawnAt(name, pos, color, allow)
        local sp = Instance.new("SpawnLocation")
        sp.Name = name
        sp.Anchored = true
        sp.Size = Vector3.new(6, 1, 6)
        sp.Position = pos
        check(sp.Neutral == true and sp.AllowTeamChangeOnTouch == false, name .. " starts Neutral, no team change")
        sp.Neutral = false
        sp.TeamColor = color
        sp.AllowTeamChangeOnTouch = allow
        sp.Parent = workspace
        return sp
    end
    local redSpawn = spawnAt("RedSpawn", Vector3.new(100, 0.5, 100), red, false)
    local blueSpawn = spawnAt("BluePad", Vector3.new(140, 0.5, 100), blue, true)
    check(redSpawn.ClassName == "SpawnLocation" and redSpawn:IsA("Part") and redSpawn:IsA("BasePart"), "a renamed SpawnLocation is still one")
    check(redSpawn.TeamColor == red and redSpawn.Neutral == false, "SpawnLocation TeamColor / Neutral read back")
    local sc = redSpawn:Clone()
    check(sc.TeamColor == red and sc.Neutral == false and sc.ClassName == "SpawnLocation", "SpawnLocation's team is saved (a copy has it)")
    sc:Destroy()

    me.Team = redTeam
    check(me.Team == redTeam and me.TeamColor == red and me.Neutral == false, "player.Team sets TeamColor and Neutral")
    -- 1. CharacterAdded / CharacterRemoving (and 4: respawning on the team's spawn).
    local events = {}
    local c1 = me.CharacterAdded:Connect(function(char) table.insert(events, { "added", char }) end)
    local c2 = me.CharacterRemoving:Connect(function(char)
        table.insert(events, { "removing", char, (char.HumanoidRootPart.Position - redSpawn.Position).Magnitude })
    end)
    character:PivotTo(CFrame.new(0, 30, -60))   -- somewhere else, then die
    frames()
    character.Humanoid.Health = 0
    check(waitFor(function() return #events >= 2 end, 8), "CharacterRemoving and CharacterAdded fired on respawn", #events)
    check(events[1] and events[1][1] == "removing" and events[1][2] == character, "CharacterRemoving first, with the old character")
    check(events[1] and events[1][3] and events[1][3] > 20, "CharacterRemoving fires before the body moves back", events[1] and events[1][3])
    check(events[2] and events[2][1] == "added" and events[2][2] == me.Character, "then CharacterAdded with the character")
    frames(5)
    local d = (character.HumanoidRootPart.Position - redSpawn.Position) * Vector3.new(1, 0, 1)
    check(d.Magnitude < 4, "a Red player respawns on the Red SpawnLocation", d.Magnitude)
    local waited = me.CharacterAdded:Once(function() end)
    check(waited ~= nil, "CharacterAdded:Once works")
    c1:Disconnect() c2:Disconnect()

    -- AllowTeamChangeOnTouch: step onto the blue pad.
    character:PivotTo(CFrame.new(140, 4, 100))
    check(waitFor(function() return me.Team == blueTeam end, 4), "touching an AllowTeamChangeOnTouch spawn changes team", me.Team)
    check(me.TeamColor == blue, "...and TeamColor")
    me.TeamColor = red
    check(me.Team == redTeam, "setting TeamColor picks the matching Team")
    me.Neutral = true
    check(me.Team == nil and me.Neutral == true, "Neutral = true leaves the team")

    -- Put things back.
    for sp, neutral in pairs(old) do sp.Neutral = neutral end
    redSpawn:Destroy() blueSpawn:Destroy() redTeam:Destroy() blueTeam:Destroy()

    print(string.format("SCRIPT FIXES TEST DONE: %d passed, %d failed", passes, fails))
end)
