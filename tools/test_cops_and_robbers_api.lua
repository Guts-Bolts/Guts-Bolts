-- Checks the "cops and robbers" scripting additions in a real play test, and leaves a
-- private PlayerGui label (with RichText) on screen for a screenshot. Run it in Studio's
-- play test:
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_cops_and_robbers_api.lua)" \
--       --screenshot /tmp/cops.png --frames 600
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "COPS API TEST DONE n".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local Players = game:GetService("Players")
    local me = Players.LocalPlayer
    while not me.Character do task.wait() end
    frames(10)

    -- Tree events
    local folder = Instance.new("Folder")
    folder.Name = "Box"
    folder.Parent = workspace
    local added, removed, descAdded, descRemoving, ancestry, destroying = {}, {}, {}, {}, 0, 0
    folder.ChildAdded:Connect(function(c) added[#added + 1] = c.Name end)
    folder.ChildRemoved:Connect(function(c) removed[#removed + 1] = c.Name end)
    workspace.DescendantAdded:Connect(function(d) if d.Name == "Inner" then descAdded[#descAdded + 1] = d.Name end end)
    workspace.DescendantRemoving:Connect(function(d) if d.Name == "Inner" then descRemoving[#descRemoving + 1] = d.Name end end)
    local part = Instance.new("Part")
    part.Name = "Inner"
    part.Anchored = true
    part.AncestryChanged:Connect(function() ancestry = ancestry + 1 end)
    part.Destroying:Connect(function() destroying = destroying + 1 end)
    part.Parent = folder
    frames(2)
    check(added[1] == "Inner", "ChildAdded fires", added[1])
    check(descAdded[1] == "Inner", "DescendantAdded fires on workspace", descAdded[1])
    check(ancestry == 1, "AncestryChanged fires", ancestry)
    part:Destroy()
    frames(2)
    check(destroying == 1, "Destroying fires", destroying)
    check(removed[1] == "Inner", "ChildRemoved fires", removed[1])
    check(descRemoving[1] == "Inner", "DescendantRemoving fires", descRemoving[1])

    -- WaitForChild waits
    task.delay(0.3, function() local v = Instance.new("IntValue") v.Name = "Late" v.Parent = folder end)
    local late = folder:WaitForChild("Late")
    check(late ~= nil and late.Name == "Late", "WaitForChild waits for a child that comes later")
    check(folder:WaitForChild("Never", 0.2) == nil, "WaitForChild with a timeout gives nil")

    -- Teams
    local teams = Instance.new("Folder")
    teams.Name = "Teams"
    teams.Parent = workspace
    local cops = Instance.new("Team")
    cops.Name = "Cops"
    cops.TeamColor = BrickColor.new("Bright blue")
    cops.Parent = teams
    me.Team = cops
    local list = cops:GetPlayers()
    check(#list == 1 and list[1] == me, "team:GetPlayers()", #list)
    check(Players:GetPlayerByUserId(me.UserId) == me, "Players:GetPlayerByUserId")

    -- Respawning
    check(Players.RespawnTime == 4 and Players.CharacterAutoLoads == true, "RespawnTime / CharacterAutoLoads defaults")
    Players.RespawnTime = 1
    check(Players.RespawnTime == 1, "RespawnTime can be set")
    local spawned = 0
    me.CharacterAdded:Connect(function() spawned = spawned + 1 end)
    local hum = me.Character:WaitForChild("Humanoid")
    local healths = {}
    hum.HealthChanged:Connect(function(h) healths[#healths + 1] = h end)
    frames(2)
    hum.Health = hum.MaxHealth - 10   -- (TakeDamage does nothing under the spawn ForceField)
    frames(3)
    check(#healths >= 1 and math.abs(healths[1] - (hum.MaxHealth - 10)) < 0.01, "HealthChanged fires", healths[1])
    me:LoadCharacter()
    frames(5)
    check(spawned >= 1, "LoadCharacter respawns (CharacterAdded)", spawned)
    check(hum.Health == hum.MaxHealth, "LoadCharacter gives full health", hum.Health)
    Players.RespawnTime = 4

    -- Private PlayerGui
    local pg = me.PlayerGui
    check(pg.ClassName == "PlayerGui", "player.PlayerGui is a PlayerGui", pg.ClassName)
    check(pg ~= game:GetService("StarterGui"), "PlayerGui isn't StarterGui")
    local shared = Instance.new("ScreenGui")
    shared.Name = "SharedHud"
    shared.Parent = game:GetService("StarterGui")
    check(pg:FindFirstChild("SharedHud") == shared, "PlayerGui finds StarterGui's things")
    check(pg.SharedHud == shared, "PlayerGui.Name finds StarterGui's things")
    local role = Instance.new("ScreenGui")
    role.Name = "RoleGui"
    role.Parent = pg
    local label = Instance.new("TextLabel")
    label.Name = "Role"
    label.Size = UDim2.fromOffset(420, 80)
    label.Position = UDim2.new(0.5, -210, 0, 40)
    label.TextSize = 28
    label.RichText = true
    label.Text = 'You are a <b><font color="#3070ff">Cop</font></b>! <u>Catch</u> the <font color="rgb(255,60,60)" size="34">robbers</font>'
    label.Parent = role
    check(label.RichText == true, "RichText can be turned on")
    check(label.ContentText == "You are a Cop! Catch the robbers", "ContentText leaves out the tags", label.ContentText)
    check(pg:FindFirstChild("RoleGui") == role, "things put in PlayerGui stay there")
    local ok = pcall(function() pg:Destroy() end)
    check(not ok, "PlayerGui can't be destroyed")

    -- TweenPosition / TweenSize
    local done
    label:TweenSize(UDim2.fromOffset(440, 80), Enum.EasingDirection.Out, Enum.EasingStyle.Quad, 0.2, true, function(s) done = s end)
    task.wait(0.5)
    check(label.Size == UDim2.fromOffset(440, 80), "TweenSize gets there", tostring(label.Size))
    check(done ~= nil, "TweenSize calls back")

    -- DateTime, TouchEnabled, BindToClose
    local now = DateTime.now()
    check(type(now.UnixTimestamp) == "number" and now.UnixTimestamp > 1700000000, "DateTime.now()")
    check(DateTime.fromUnixTimestamp(0):ToIsoDate() == "1970-01-01T00:00:00Z", "DateTime ToIsoDate")
    check(UserInputService.TouchEnabled == false and UserInputService.KeyboardEnabled == true, "TouchEnabled on a computer")
    game:BindToClose(function() print("BINDTOCLOSE RAN") end)
    check(true, "BindToClose accepts a function")

    -- TouchEnded / GetTouchingParts
    local pad = Instance.new("Part")
    pad.Name = "Pad"
    pad.Anchored = true
    pad.CanCollide = false
    pad.Size = Vector3.new(6, 6, 6)
    pad.Position = me.Character.HumanoidRootPart.Position
    local ended = 0
    pad.TouchEnded:Connect(function() ended = ended + 1 end)
    pad.Parent = workspace
    frames(10)
    check(#pad:GetTouchingParts() > 0, "GetTouchingParts sees the character", #pad:GetTouchingParts())
    pad.Position = pad.Position + Vector3.new(0, 200, 0)
    frames(10)
    check(ended > 0, "TouchEnded fires when the character stops touching", ended)

    print("COPS API TEST DONE " .. fails)
end)
