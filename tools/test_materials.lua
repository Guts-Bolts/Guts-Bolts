-- Checks every part material in a real play test, and lines them all up for a picture.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_materials.lua)" \
--       --screenshot /tmp/materials.png --frames 300
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "MATERIALS TEST DONE".
-- The picture: three rows of six blocks (left to right, back row first) in the order of `list`.
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end

    -- Each material, and a colour like the one Roblox gives it by default.
    local list = {
        { "Plastic",       Color3.new(0.64, 0.64, 0.66) },
        { "SmoothPlastic", Color3.new(0.80, 0.25, 0.22) },
        { "Metal",         Color3.new(0.65, 0.66, 0.70) },
        { "DiamondPlate",  Color3.new(0.55, 0.56, 0.58) },
        { "Foil",          Color3.new(0.78, 0.78, 0.80) },
        { "Neon",          Color3.new(0.20, 0.80, 1.00) },
        { "Wood",          Color3.new(0.63, 0.45, 0.28) },
        { "Glass",         Color3.new(0.55, 0.75, 0.85) },
        { "Ice",           Color3.new(0.72, 0.84, 0.95) },
        { "Concrete",      Color3.new(0.62, 0.62, 0.60) },
        { "Brick",         Color3.new(0.62, 0.28, 0.20) },
        { "Cobblestone",   Color3.new(0.55, 0.53, 0.50) },
        { "Slate",         Color3.new(0.38, 0.40, 0.42) },
        { "Granite",       Color3.new(0.60, 0.58, 0.56) },
        { "Marble",        Color3.new(0.90, 0.89, 0.86) },
        { "Sand",          Color3.new(0.85, 0.75, 0.52) },
        { "Grass",         Color3.new(0.30, 0.55, 0.20) },
        { "Fabric",        Color3.new(0.20, 0.30, 0.60) },
    }

    -- Build the row, out of the player's way, and point the camera at it.
    local folder = Instance.new("Model")
    folder.Name = "MaterialSamples"
    folder.Parent = workspace
    local base = Vector3.new(0, 30, 60)
    local parts = {}
    for i, m in ipairs(list) do
        local row, col = (i - 1) // 6, (i - 1) % 6
        local p = Instance.new("Part")
        p.Name = m[1]
        p.Anchored = true
        p.Size = Vector3.new(4, 4, 4)
        p.Position = base + Vector3.new((col - 2.5) * 4.6, -row * 2, row * 5)
        p.Color = m[2]
        p.Material = m[1]
        p.Parent = folder
        parts[i] = p
    end
    local floor = Instance.new("Part")
    floor.Name = "Floor"
    floor.Anchored = true
    floor.Size = Vector3.new(40, 1, 30)
    floor.Position = base + Vector3.new(0, -6.5, 5)
    floor.Color = Color3.new(0.3, 0.3, 0.32)
    floor.Parent = folder

    -- Enum.Material.X and "X" both work, and read back as the same name.
    for i, m in ipairs(list) do
        local name = m[1]
        local p = parts[i]
        check(p.Material == name, name .. ": the string form sticks", p.Material)
        check(Enum.Material[name] == name and p.Material == Enum.Material[name], name .. ": Enum.Material." .. name .. " is the same")
        p.Material = Enum.Material.Plastic
        p.Material = Enum.Material[name]
        check(p.Material == name, name .. ": set from Enum.Material reads back", p.Material)
        check(type(p.Density) == "number" and p.Density > 0, name .. ": has a density", p.Density)
        check(type(p.Friction) == "number" and p.Friction >= 0 and p.Friction <= 2, name .. ": has a friction", p.Friction)
    end

    -- They are really different materials, not other names for the old ones.
    local seen = {}
    for _, m in ipairs(list) do
        check(not seen[m[1]], m[1] .. " is its own material")
        seen[m[1]] = true
    end

    -- Physics defaults, like Roblox's: heavy stones, slippery ice, grippy brick.
    local function get(name) for i, m in ipairs(list) do if m[1] == name then return parts[i] end end end
    check(get("Ice").Friction < get("Plastic").Friction, "Ice is more slippery than Plastic")
    check(get("Brick").Friction > get("Plastic").Friction, "Brick grips more than Plastic")
    check(get("SmoothPlastic").Friction < get("Plastic").Friction, "SmoothPlastic is a little more slippery than Plastic")
    check(get("Granite").Density > 1.3 and get("Slate").Density > 1.3 and get("Cobblestone").Density > 1.3, "stones sink (water is 1.3)")
    check(get("Fabric").Density < 1.3, "Fabric floats")
    check(get("DiamondPlate").Density >= get("Foil").Density, "DiamondPlate is at least as heavy as Foil")

    -- Some other Roblox names still work, drawn with the closest material we have.
    local p = parts[1]
    p.Material = "WoodPlanks"     check(p.Material == "Wood", "WoodPlanks -> Wood", p.Material)
    p.Material = "CorrodedMetal"  check(p.Material == "Metal", "CorrodedMetal -> Metal", p.Material)
    p.Material = "Pebble"         check(p.Material == "Cobblestone", "Pebble -> Cobblestone", p.Material)
    p.Material = "LeafyGrass"     check(p.Material == "Grass", "LeafyGrass -> Grass", p.Material)

    -- An unknown name is an error that lists the real ones (and leaves the part alone).
    p.Material = "Plastic"
    local ok, err = pcall(function() p.Material = "Cheese" end)
    check(not ok, "an unknown material is an error")
    err = tostring(err)
    check(err:find("Cheese", 1, true) ~= nil, "the error names what was asked for", err)
    for _, m in ipairs(list) do
        if not err:find(m[1], 1, true) then check(false, "the error lists " .. m[1], err) end
    end
    check(err:find("SmoothPlastic", 1, true) and err:find("DiamondPlate", 1, true), "the error lists the new materials", err)
    check(p.Material == "Plastic", "a bad name leaves the material as it was", p.Material)

    -- A copy keeps its material.
    local c = get("Marble"):Clone()
    check(c.Material == "Marble", "Clone keeps the material", c.Material)
    c:Destroy()

    -- Look at them.
    game.Lighting.FogEnabled = false   -- (the picture is about the materials, not the haze)
    local cam = workspace.CurrentCamera
    cam.CameraType = Enum.CameraType.Scriptable
    cam.FieldOfView = 50
    cam.CFrame = CFrame.new(base + Vector3.new(0, 10, 27), base + Vector3.new(0, -2, 4))
    frames(10)

    print("MATERIALS TEST DONE: " .. (fails == 0 and "all passed" or (fails .. " failed")))
end)
