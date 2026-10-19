-- Checks collision groups (PhysicsService, BasePart.CollisionGroup) in a real play test.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_collision_groups.lua)" \
--       --screenshot /tmp/groups.png --frames 1500
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "COLLISION GROUPS TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local function errors(f) local ok, err = pcall(f) return not ok, err end

    local PS = game:GetService("PhysicsService")
    check(PS ~= nil and PS.ClassName == "PhysicsService", "PhysicsService is a service")

    -- Defaults: every game starts with just Default, which collides with itself.
    check(PS:GetMaxCollisionGroups() == 32, "GetMaxCollisionGroups is 32")
    check(PS:IsCollisionGroupRegistered("Default"), "Default is registered")
    local list = PS:GetRegisteredCollisionGroups()
    check(#list == 1 and list[1].name == "Default" and list[1].id == 0 and list[1].mask == -1,
          "GetRegisteredCollisionGroups starts as just Default", #list)
    check(PS:CollisionGroupsAreCollidable("Default", "Default"), "Default collides with Default")
    local p = Instance.new("Part")
    check(p.CollisionGroup == "Default" and p.CollisionGroupId == 0, "a new part is in Default")
    p:Destroy()

    -- Registering and the table.
    PS:RegisterCollisionGroup("Ghosts")
    check(PS:IsCollisionGroupRegistered("Ghosts"), "RegisterCollisionGroup registers it")
    check(not PS:IsCollisionGroupRegistered("ghosts"), "names are case-sensitive")
    PS:RegisterCollisionGroup("Ghosts")   -- again: nothing happens
    check(#PS:GetRegisteredCollisionGroups() == 2, "registering twice doesn't add it twice")
    check(PS:CollisionGroupsAreCollidable("Ghosts", "Default") and PS:CollisionGroupsAreCollidable("Ghosts", "Ghosts"),
          "a new group collides with everything")
    PS:CollisionGroupSetCollidable("Ghosts", "Default", false)
    check(not PS:CollisionGroupsAreCollidable("Ghosts", "Default") and not PS:CollisionGroupsAreCollidable("Default", "Ghosts"),
          "CollisionGroupSetCollidable works both ways round")
    check(PS:CollisionGroupsAreCollidable("Ghosts", "Ghosts"), "and leaves the other pairs alone")
    PS:CollisionGroupSetCollidable("Ghosts", "Default", true)
    check(PS:CollisionGroupsAreCollidable("Ghosts", "Default"), "and can turn it back on")

    -- Mistakes are errors that say what's wrong.
    check(errors(function() PS:CollisionGroupSetCollidable("Ghosts", "Nobody", false) end), "SetCollidable with an unknown group errors")
    check(errors(function() PS:CollisionGroupsAreCollidable("Nobody", "Default") end), "AreCollidable with an unknown group errors")
    check(errors(function() PS:CollisionGroupSetCollidable("Ghosts", "Default", "no") end), "SetCollidable needs a boolean")
    check(errors(function() PS:RegisterCollisionGroup("") end), "an empty name errors")
    check(errors(function() PS:RegisterCollisionGroup(42) end), "a name that isn't a string errors")
    check(errors(function() PS:RegisterCollisionGroup(string.rep("x", 101)) end), "a name over 100 characters errors")
    check(errors(function() PS:UnregisterCollisionGroup("Default") end), "Default can't be unregistered")
    check(errors(function() PS:RenameCollisionGroup("Default", "Base") end), "Default can't be renamed")
    check(errors(function() PS:RenameCollisionGroup("Nobody", "Base") end), "renaming an unknown group errors")
    check(errors(function() PS:RenameCollisionGroup("Ghosts", "Default") end), "renaming onto a taken name errors")
    local bad, msg = errors(function() PS:CollisionGroupSetCollidable("Ghosts", "Nobody", false) end)
    check(bad and tostring(msg):find("Nobody") ~= nil, "the error names the group", msg)
    check(errors(function() local q = Instance.new("Part") q.CollisionGroup = 5 end), "CollisionGroup must be a string")

    -- The 32 limit.
    for i = 1, 30 do PS:RegisterCollisionGroup("Extra" .. i) end
    check(#PS:GetRegisteredCollisionGroups() == 32, "32 groups fit", #PS:GetRegisteredCollisionGroups())
    check(errors(function() PS:RegisterCollisionGroup("OneTooMany") end), "a 33rd group errors")
    for i = 1, 30 do PS:UnregisterCollisionGroup("Extra" .. i) end
    check(#PS:GetRegisteredCollisionGroups() == 2 and not PS:IsCollisionGroupRegistered("Extra5"), "UnregisterCollisionGroup removes them")
    PS:UnregisterCollisionGroup("NeverWas")   -- (nothing to do: not an error)

    -- Renaming takes its parts along; ids; the old API.
    local r = Instance.new("Part")
    r.Parent = workspace
    r.Anchored = true
    r.Position = Vector3.new(-200, 300, -200)
    PS:RegisterCollisionGroup("Spirits")
    r.CollisionGroup = "Spirits"
    PS:RenameCollisionGroup("Spirits", "Wraiths")
    check(r.CollisionGroup == "Wraiths", "RenameCollisionGroup moves its parts too", r.CollisionGroup)
    check(not PS:IsCollisionGroupRegistered("Spirits") and PS:IsCollisionGroupRegistered("Wraiths"), "and the group has the new name")
    check(r.CollisionGroupId == PS:GetCollisionGroupId("Wraiths") and r.CollisionGroupId > 0, "CollisionGroupId is its group's number")
    check(PS:GetCollisionGroupName(r.CollisionGroupId) == "Wraiths", "GetCollisionGroupName")
    r.CollisionGroupId = 0
    check(r.CollisionGroup == "Default", "setting CollisionGroupId moves it")
    check(errors(function() r.CollisionGroupId = 31 end), "an unused CollisionGroupId errors")
    PS:SetPartCollisionGroup(r, "Wraiths")
    check(r.CollisionGroup == "Wraiths" and PS:CollisionGroupContainsPart("Wraiths", r), "SetPartCollisionGroup (the old way)")
    check(errors(function() PS:CreateCollisionGroup("Wraiths") end), "CreateCollisionGroup on a taken name errors (the old way)")
    PS:UnregisterCollisionGroup("Wraiths")
    check(r.CollisionGroup == "Wraiths" and r.CollisionGroupId == 0, "a part in an unregistered group acts as Default")
    r:Destroy()

    -- Real physics: a part dropped onto a floor its group doesn't collide with falls through it.
    PS:RegisterCollisionGroup("Floors")
    PS:CollisionGroupSetCollidable("Ghosts", "Floors", false)
    local floorY = 60
    local floor = Instance.new("Part")
    floor.Name = "GroupFloor"
    floor.Anchored = true
    floor.Size = Vector3.new(30, 2, 30)
    floor.Position = Vector3.new(120, floorY, 0)
    floor.CollisionGroup = "Floors"
    floor.Parent = workspace
    local function drop(name, x, group)
        local b = Instance.new("Part")
        b.Name = name
        b.Size = Vector3.new(2, 2, 2)
        b.Position = Vector3.new(x, floorY + 6, 0)
        b.CollisionGroup = group
        b.Anchored = false
        b.Parent = workspace
        return b
    end
    local ghost = drop("GhostBox", 115, "Ghosts")
    local solid = drop("SolidBox", 125, "Default")
    frames(120)
    local ghostY = ghost.Parent and ghost.Position.Y or -1e9
    check(ghostY < floorY - 2, "a part whose group doesn't collide with the floor's falls through it", ghostY)
    check(solid.Parent ~= nil and math.abs(solid.Position.Y - (floorY + 2)) < 0.5, "a colliding one lands on it", solid.Parent and solid.Position.Y)

    -- Two loose parts in a group that doesn't collide with itself pass through each other.
    PS:CollisionGroupSetCollidable("Ghosts", "Floors", true)
    PS:CollisionGroupSetCollidable("Ghosts", "Ghosts", false)
    local low = drop("GhostLow", 120, "Ghosts")
    low.Position = Vector3.new(118, floorY + 3, 6)
    local high = drop("GhostHigh", 120, "Ghosts")
    high.Position = Vector3.new(118, floorY + 8, 6)
    frames(120)
    check(math.abs(low.Position.Y - (floorY + 2)) < 0.5 and math.abs(high.Position.Y - (floorY + 2)) < 0.5,
          "two parts in a group that doesn't collide with itself end up inside each other", low.Position.Y .. " " .. high.Position.Y)

    -- Raycasts: RaycastParams.CollisionGroup skips what that group passes through.
    PS:CollisionGroupSetCollidable("Ghosts", "Floors", false)
    local params = RaycastParams.new()
    check(params.CollisionGroup == "Default", "RaycastParams.CollisionGroup starts as Default")
    local from, down = Vector3.new(120, floorY + 20, -10), Vector3.new(0, -40, 0)
    local hit = workspace:Raycast(from, down, params)
    check(hit and hit.Instance == floor, "a Default ray hits the floor", hit and hit.Instance.Name)
    params.CollisionGroup = "Ghosts"
    hit = workspace:Raycast(from, down, params)
    check(not hit or hit.Instance ~= floor, "a Ghosts ray goes through it", hit and hit.Instance.Name)
    params.CollisionGroup = "Default"
    floor.CanCollide = false
    params.RespectCanCollide = true
    hit = workspace:Raycast(from, down, params)
    check(not hit or hit.Instance ~= floor, "RespectCanCollide skips CanCollide-off parts")
    params.RespectCanCollide = false
    hit = workspace:Raycast(from, down, params)
    check(hit and hit.Instance == floor, "without it they're still hit")
    floor.CanCollide = true

    -- Characters: a team door only the red team's characters walk through.
    local me = game.Players.LocalPlayer
    local char = me.Character
    local root = char.HumanoidRootPart
    PS:RegisterCollisionGroup("Red")
    PS:RegisterCollisionGroup("RedDoor")
    PS:CollisionGroupSetCollidable("Red", "RedDoor", false)
    local start = root.Position
    local door = Instance.new("Part")
    door.Name = "RedTeamDoor"
    door.Anchored = true
    door.Size = Vector3.new(1, 12, 20)
    door.Position = Vector3.new(start.X + 6, start.Y + 3, start.Z)
    door.CollisionGroup = "RedDoor"
    door.Parent = workspace
    local function push(dir, n)   -- shove the character along (it walks through the real character physics)
        for _ = 1, n do root.AssemblyLinearVelocity = dir frames(1) end
        frames(30)
    end
    -- Not on the team: the door stops us.
    push(Vector3.new(20, 0, 0), 50)
    check(root.Position.X < door.Position.X, "the door stops a character in Default", root.Position.X - door.Position.X)
    -- On the team (like a script putting every part of the character in the group).
    for _, part in ipairs(char:GetDescendants()) do
        if part:IsA("BasePart") then part.CollisionGroup = "Red" end
    end
    check(root.CollisionGroup == "Red", "the character's parts are in Red")
    push(Vector3.new(20, 0, 0), 50)
    check(root.Position.X > door.Position.X + 1, "a Red character walks through the red door", root.Position.X - door.Position.X)
    -- A wall in Default still stops it.
    local wall = Instance.new("Part")
    wall.Name = "PlainWall"
    wall.Anchored = true
    wall.Size = Vector3.new(1, 12, 20)
    wall.Position = Vector3.new(root.Position.X + 6, start.Y + 3, start.Z)
    wall.Parent = workspace
    push(Vector3.new(20, 0, 0), 50)
    check(root.Position.X < wall.Position.X, "but not through a wall", root.Position.X - wall.Position.X)
    -- Players walking through each other: a group that doesn't collide with itself.
    PS:RegisterCollisionGroup("Players")
    PS:CollisionGroupSetCollidable("Players", "Players", false)
    check(not PS:CollisionGroupsAreCollidable("Players", "Players") and PS:CollisionGroupsAreCollidable("Players", "Default"),
          "Players pass through Players but not the world")
    -- Red still collides with Default, so the character kept standing on the Baseplate.
    check(math.abs(root.Position.Y - start.Y) < 1, "the character stayed on the ground", root.Position.Y - start.Y)

    print(fails == 0 and "COLLISION GROUPS TEST DONE: all passed" or ("COLLISION GROUPS TEST DONE: " .. fails .. " failed"))
    local cam = workspace.CurrentCamera
    cam.CameraType = Enum.CameraType.Scriptable
    cam.CFrame = CFrame.new(Vector3.new(start.X + 4, start.Y + 14, start.Z + 26), Vector3.new(start.X + 6, start.Y, start.Z))
end)
