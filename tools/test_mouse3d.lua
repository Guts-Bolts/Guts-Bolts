-- Checks the mouse in 3D (getmouse3d, mouse.Hit / Target / TargetSurface,
-- UserInputService:GetMouseLocation) in a real play test.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_mouse3d.lua)" \
--       --screenshot /tmp/mouse3d.png --frames 600
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "MOUSE 3D TEST DONE".
-- (__gb_testInput("mouse", x, y) pretends the mouse is at that pixel.)
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra ~= nil and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function near(a, b, eps) return (a - b).Magnitude <= (eps or 1e-2) end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local function finite(v) return v.X == v.X and v.Y == v.Y and v.Z == v.Z end

    local cam = workspace.CurrentCamera
    local me = game.Players.LocalPlayer
    local mouse = me:GetMouse()

    -- A wall high in the sky, away from whatever the game has, and the camera looking at it.
    local wall = Instance.new("Part")
    wall.Name = "MouseWall"
    wall.Anchored = true
    wall.Size = Vector3.new(20, 20, 2)
    wall.Position = Vector3.new(0, 500, -30)
    wall.Parent = workspace
    cam.CameraType = Enum.CameraType.Scriptable
    cam.CFrame = CFrame.new(Vector3.new(0, 500, 0), Vector3.new(0, 500, -50))
    frames(5)
    local size = cam.ViewportSize
    local cx, cy = size.X / 2, size.Y / 2
    __gb_testInput("mouse", cx, cy)

    -- getmouse() is still the 2D spot, and GetMouseLocation is the same thing.
    local p2 = getmouse()
    check(near(p2, Vector2.new(cx, cy)), "getmouse() is the 2D pixel", p2)
    check(near(UserInputService:GetMouseLocation(), p2), "UserInputService:GetMouseLocation() matches getmouse()")

    -- getmouse3d(): the spot on the wall's face, the wall, and the face's normal.
    local pos, part, normal = getmouse3d()
    check(part == wall, "getmouse3d() finds the part under the mouse", part)
    check(pos ~= nil and near(pos, Vector3.new(0, 500, -29), 0.05), "getmouse3d() gives the 3D spot on its face", pos)
    check(normal ~= nil and near(normal, Vector3.new(0, 0, 1), 0.01), "getmouse3d() gives the face's normal", normal)

    -- Roblox's way gives the same spot.
    check(mouse.Target == wall, "mouse.Target is the wall", mouse.Target)
    check(near(mouse.Hit.Position, pos, 0.05), "mouse.Hit.Position matches getmouse3d()", mouse.Hit.Position)
    check(near(mouse.Hit.LookVector, Vector3.new(0, 0, -1), 0.01), "mouse.Hit faces along the mouse's ray", mouse.Hit.LookVector)
    check(mouse.TargetSurface == "Back" and mouse.TargetSurface == Enum.NormalId.Back,
          "mouse.TargetSurface says which face (Back = +Z)", mouse.TargetSurface)
    local ray = cam:ViewportPointToRay(p2.X, p2.Y)
    local hit = workspace:Raycast(ray.Origin, ray.Direction * 1000)
    check(hit ~= nil and hit.Instance == wall and near(hit.Position, pos, 0.05),
          "camera:ViewportPointToRay + workspace:Raycast lands on the same spot")

    -- Moving the mouse right moves the spot right.
    __gb_testInput("mouse", cx + size.X / 8, cy)
    local right = getmouse3d()
    check(right.X > 0.5 and math.abs(right.Z + 29) < 0.05, "the 3D spot follows the mouse", right)

    -- Options: how far to look, and things to look through.
    __gb_testInput("mouse", cx, cy)
    local short, shortPart = getmouse3d(5)
    check(shortPart == nil and near(short, Vector3.new(0, 500, -5), 0.05), "getmouse3d(5) stops 5 studs out", short)
    local _, through = getmouse3d(nil, wall)
    check(through ~= wall, "getmouse3d(nil, part) looks through that part", through)
    local _, throughList = getmouse3d(nil, { wall })
    check(throughList ~= wall, "getmouse3d(nil, {list}) looks through those too", throughList)
    mouse.TargetFilter = wall
    check(mouse.Target ~= wall, "mouse.TargetFilter still works", mouse.Target)
    mouse.TargetFilter = nil

    -- Pointing at the sky: no part, a spot far away along the ray.
    wall.Parent = nil
    local sky, skyPart = getmouse3d()
    check(skyPart == nil and near(sky, Vector3.new(0, 500, -1000), 0.5), "pointing at the sky gives no part, a far spot", sky)
    wall.Parent = workspace

    -- Looking straight down (top-down games) used to make a broken CFrame.
    wall.CFrame = CFrame.new(0, 480, 0)   -- (its top is 10 studs up: y = 490)
    cam.CFrame = CFrame.new(Vector3.new(0, 520, 0), Vector3.new(0, 400, 0))
    frames(2)
    check(finite(cam.CFrame.LookVector) and near(cam.CFrame.LookVector, Vector3.new(0, -1, 0), 0.01),
          "CFrame.new(pos, straight down) is a real rotation", cam.CFrame.LookVector)
    check(finite(CFrame.lookAt(Vector3.new(), Vector3.new(0, 5, 0)).LookVector), "CFrame.lookAt straight up too")
    local down, downPart = getmouse3d()
    check(downPart == wall and math.abs(down.Y - 490) < 0.05, "getmouse3d() works from a top-down camera", down)
    check(finite(mouse.Hit.Position) and finite(mouse.Hit.LookVector), "mouse.Hit from a top-down camera has no NaNs")

    __gb_testInput("mouse")
    wall:Destroy()
    cam.CameraType = Enum.CameraType.Custom
    print("MOUSE 3D TEST DONE (" .. fails .. " failed)")
end)
