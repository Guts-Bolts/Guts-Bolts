-- Checks ClickDetectors in a real play test: the real mouse (hover, click, right-click) and,
-- through the test hook, range, models, tools and the events' player.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_clickdetector.lua)" \
--       --test-mouse "click:640:360 right:640:360" --screenshot /tmp/click.png --frames 400
--
-- (--test-mouse moves the mouse to the middle of the window at frame 60 and clicks, then
-- right-clicks there. The character is boxed in by a clickable model, so whichever way the
-- camera looks, the pointer is on it.)
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "CLICKDETECTOR TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end

    local me = game.Players.LocalPlayer
    local humanoid = me.Character.Humanoid
    local at = me.Character.Head.Position

    -- The object itself.
    local cd = Instance.new("ClickDetector")
    check(cd.ClassName == "ClickDetector" and cd:IsA("ClickDetector") and cd:IsA("Instance"), "Instance.new makes a ClickDetector")
    check(cd.MaxActivationDistance == 16, "MaxActivationDistance starts at 16 (Roblox's 32 studs)", cd.MaxActivationDistance)
    check(cd.CursorIcon == "", "CursorIcon starts empty")
    cd.MaxActivationDistance = 12
    cd.CursorIcon = "images/hand.png"
    local copy = cd:Clone()
    check(copy.MaxActivationDistance == 12 and copy.CursorIcon == "images/hand.png", "Clone keeps its settings")
    cd:Destroy()
    copy:Destroy()

    -- 1. The real mouse: a box of walls round the character, one ClickDetector in the model.
    local box = Instance.new("Model")
    box.Name = "ClickBox"
    local walls = {
        {Vector3.new(6, 0, 0), Vector3.new(0.5, 12, 12)}, {Vector3.new(-6, 0, 0), Vector3.new(0.5, 12, 12)},
        {Vector3.new(0, 0, 6), Vector3.new(12, 12, 0.5)}, {Vector3.new(0, 0, -6), Vector3.new(12, 12, 0.5)},
        {Vector3.new(0, 6, 0), Vector3.new(12, 0.5, 12)},
    }
    for i, w in ipairs(walls) do
        local p = Instance.new("Part")
        p.Name = "Wall" .. i
        p.Anchored = true
        p.CanCollide = false
        p.Size = w[2]
        p.Position = at + w[1]
        p.Parent = box
    end
    local boxDetector = Instance.new("ClickDetector")
    boxDetector.Parent = box
    box.Parent = workspace
    local got = {}
    local function listen(d, tag)
        d.MouseClick:Connect(function(player) table.insert(got, tag .. ":click:" .. (player and player.Name or "nil")) end)
        d.RightMouseClick:Connect(function(player) table.insert(got, tag .. ":right:" .. (player and player.Name or "nil")) end)
        d.MouseHoverEnter:Connect(function(player) table.insert(got, tag .. ":enter:" .. (player and player.Name or "nil")) end)
        d.MouseHoverLeave:Connect(function(player) table.insert(got, tag .. ":leave:" .. (player and player.Name or "nil")) end)
    end
    local function has(e) for _, v in ipairs(got) do if v == e then return true end end return false end
    listen(boxDetector, "box")
    -- The mouse clicks at frame 60 and right-clicks at frame 68 (we started at frame 12).
    for _ = 1, 120 do
        if has("box:click:" .. me.Name) and has("box:right:" .. me.Name) then break end
        RunService.Heartbeat:Wait()
    end
    check(has("box:enter:" .. me.Name), "pointing the mouse at it fires MouseHoverEnter(player)", table.concat(got, " "))
    check(has("box:click:" .. me.Name), "clicking it fires MouseClick(player)", table.concat(got, " "))
    check(has("box:right:" .. me.Name), "right-clicking it fires RightMouseClick(player)", table.concat(got, " "))
    box:Destroy()
    frames()

    -- 2. The test hook: the same thing the mouse does, on parts we choose.
    if not __gb_testInput then
        warn("FAIL no __gb_testInput (run with --test-command)")
        fails = fails + 1
    else
        got = {}
        local function part(name, offset)
            local p = Instance.new("Part")
            p.Name = name
            p.Anchored = true
            p.CanCollide = false
            p.Size = Vector3.new(1, 1, 1)
            p.Position = at + offset
            p.Parent = workspace
            return p
        end
        local button = part("Button", Vector3.new(3, 0, 0))
        local d = Instance.new("ClickDetector")
        d.Parent = button
        listen(d, "button")
        local clicked = 0
        button.Clicked:Connect(function() clicked = clicked + 1 end)

        check(__gb_testInput("click", button) == true, "a near part with a ClickDetector can be clicked")
        check(has("button:click:" .. me.Name), "MouseClick passes the player who clicked", table.concat(got, " "))
        check(clicked == 1, "part.Clicked still fires too", clicked)
        __gb_testInput("right", button)
        check(has("button:right:" .. me.Name), "RightMouseClick(player)")
        got = {}
        check(__gb_testInput("hover", button) == true, "hovering it says it's clickable (the hand)")
        check(has("button:enter:" .. me.Name), "MouseHoverEnter(player)")
        __gb_testInput("hover", nil)
        check(has("button:leave:" .. me.Name), "MouseHoverLeave(player) when the pointer moves off")
        got = {}
        check(__gb_testInput("pad", button) == true and has("button:click:" .. me.Name), "a controller's RT clicks it")

        -- Too far away: nothing (16 units by default).
        got = {}
        local far = part("FarButton", Vector3.new(25, 0, 0))
        local fd = Instance.new("ClickDetector")
        fd.Parent = far
        listen(fd, "far")
        check(__gb_testInput("click", far) == false and not has("far:click:" .. me.Name), "too far away: no MouseClick", table.concat(got, " "))
        fd.MaxActivationDistance = 40
        check(__gb_testInput("click", far) == true and has("far:click:" .. me.Name), "a bigger MaxActivationDistance reaches it")
        d.MaxActivationDistance = 0
        got = {}
        check(__gb_testInput("click", button) == false and not has("button:click:" .. me.Name), "MaxActivationDistance 0: can't be clicked")
        d.MaxActivationDistance = 16

        -- In a model: any part in it.
        local lever = Instance.new("Model")
        lever.Name = "Lever"
        local stick = part("Stick", Vector3.new(0, 0, 3))
        stick.Parent = lever
        local inner = Instance.new("Model")
        inner.Parent = lever
        local knob = part("Knob", Vector3.new(0, 1, 3))
        knob.Parent = inner
        local ld = Instance.new("ClickDetector")
        ld.Parent = lever
        lever.Parent = workspace
        listen(ld, "lever")
        got = {}
        __gb_testInput("click", stick)
        __gb_testInput("click", knob)
        local n = 0
        for _, v in ipairs(got) do if v == "lever:click:" .. me.Name then n = n + 1 end end
        check(n == 2, "a ClickDetector in a model makes every part in it clickable", table.concat(got, " "))
        check(__gb_testInput("click", part("Plain", Vector3.new(-3, 0, 0))) == false, "a part without one isn't clickable")

        -- Holding a tool: the click is the tool's (like Roblox).
        local tool = Instance.new("Tool")
        local handle = Instance.new("Part")
        handle.Name = "Handle"
        handle.Size = Vector3.new(0.5, 2, 0.5)
        handle.Parent = tool
        tool.Parent = me.Backpack
        humanoid:EquipTool(tool)
        frames()
        got = {}
        local holding = tool.Parent == me.Character
        check(holding, "the test tool is held")
        if holding then
            check(__gb_testInput("click", button) == false and not has("button:click:" .. me.Name), "holding a tool: no MouseClick")
        end
        humanoid:UnequipTools()
        frames()
        got = {}
        check(__gb_testInput("click", button) == true and has("button:click:" .. me.Name), "put the tool away: clickable again")

        -- Destroyed: gone.
        d:Destroy()
        check(__gb_testInput("click", button) == false, "after Destroy the part isn't clickable")
    end

    print(fails == 0 and "ALL PASS" or (fails .. " FAILED"))
    print("CLICKDETECTOR TEST DONE")
end)
