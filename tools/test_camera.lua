-- Checks workspace.CurrentCamera and the player's camera options in a real play test.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_camera.lua)" \
--       --screenshot /tmp/camera.png --frames 900
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "CAMERA TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function near(a, b, eps) return (a - b).Magnitude <= (eps or 1e-3) end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end

    local cam = workspace.CurrentCamera
    local me = game.Players.LocalPlayer
    local humanoid = me.Character.Humanoid
    local head = me.Character.Head

    -- Defaults: existing games keep the camera they had.
    check(cam == workspace.CurrentCamera and cam == game:GetService("Workspace").CurrentCamera, "the same camera every time")
    check(cam.ClassName == "Camera" and cam:IsA("Camera") and cam:IsA("Instance") and cam.Parent == workspace, "it's a Camera in the Workspace")
    check(cam.CameraType == Enum.CameraType.Custom, "CameraType starts Custom", cam.CameraType)
    check(cam.FieldOfView == 60, "FieldOfView starts at the engine's 60", cam.FieldOfView)
    check(cam.CameraSubject == humanoid, "CameraSubject starts as your Humanoid")
    check(me.CameraMode == Enum.CameraMode.Classic, "CameraMode starts Classic", me.CameraMode)
    check(me.CameraMinZoomDistance == 0.5 and me.CameraMaxZoomDistance == 60, "zoom limits start at 0.5 / 60",
          me.CameraMinZoomDistance .. " " .. me.CameraMaxZoomDistance)
    frames(5)
    check((cam.Focus.Position - head.Position).Magnitude < 2, "Focus is at the head", cam.Focus.Position)
    local d0 = (cam.CFrame.Position - cam.Focus.Position).Magnitude
    check(d0 > 2 and d0 < 400, "the camera starts behind the character", d0)
    check(cam.ViewportSize.X > 1 and cam.ViewportSize.Y > 1, "ViewportSize is the view in pixels", cam.ViewportSize)

    -- Zoom limits are kept to (the wheel can't go past them, and the camera moves in now).
    me.CameraMaxZoomDistance = 5
    frames()
    local d = (cam.CFrame.Position - cam.Focus.Position).Magnitude
    check(d <= 5.01, "CameraMaxZoomDistance pulls the camera in", d)
    me.CameraMinZoomDistance = 8
    me.CameraMaxZoomDistance = 20
    frames()
    d = (cam.CFrame.Position - cam.Focus.Position).Magnitude
    check(d >= 7.99 and d <= 20.01, "CameraMinZoomDistance pushes it out", d)
    check(me.CameraMinZoomDistance == 8 and me.CameraMaxZoomDistance == 20, "the limits read back")

    -- First person: the camera sits in the head, and the character turns to face where it looks.
    me.CameraMode = Enum.CameraMode.LockFirstPerson
    check(me.CameraMode == "LockFirstPerson", "CameraMode reads back")
    cam.CFrame = CFrame.new(Vector3.new(0, 0, 0), Vector3.new(1, 0, 0))   -- look along +X
    frames(10)
    d = (cam.CFrame.Position - head.Position).Magnitude
    check(d < 1.5, "LockFirstPerson puts the camera in the head", d)
    check(near(cam.CFrame.LookVector, Vector3.new(1, 0, 0), 0.05), "setting CFrame in Custom turns the camera", cam.CFrame.LookVector)
    -- (Characters are built facing +Z, so facing +X is a 90 degree turn.)
    local turned = me.Character.Orientation.Y % 360
    check(math.abs(turned - 90) < 1, "in first person the character faces where the camera looks", turned)
    me.CameraMode = Enum.CameraMode.Classic
    me.CameraMinZoomDistance = 0.5
    me.CameraMaxZoomDistance = 60
    frames()

    -- Scriptable: the engine leaves it exactly where scripts put it.
    cam.CameraType = Enum.CameraType.Scriptable
    local shot = CFrame.new(Vector3.new(0, 40, 40), Vector3.new(0, 0, 0))
    cam.CFrame = shot
    frames(10)
    check(near(cam.CFrame.Position, shot.Position) and near(cam.CFrame.LookVector, shot.LookVector), "Scriptable keeps the script's CFrame")
    local p, onScreen = cam:WorldToViewportPoint(Vector3.new(0, 0, 0))
    local mid = cam.ViewportSize / 2
    check(onScreen and math.abs(p.X - mid.X) < 1 and math.abs(p.Y - mid.Y) < 1, "WorldToViewportPoint: what it looks at is in the middle", p)
    check(math.abs(p.Z - shot.Position.Magnitude) < 0.01, "WorldToViewportPoint's Z is the depth", p.Z)
    local _, behind = cam:WorldToScreenPoint(Vector3.new(0, 80, 80))
    check(not behind, "a point behind the camera isn't on screen")
    local ray = cam:ViewportPointToRay(mid.X, mid.Y)
    check(near(ray.Origin, shot.Position) and near(ray.Direction, shot.LookVector), "ViewportPointToRay through the middle")
    local corner = cam:ScreenPointToRay(0, 0)
    local back, _ = cam:WorldToViewportPoint(corner.Origin + corner.Direction * 10)
    check(math.abs(back.X) < 0.5 and math.abs(back.Y) < 0.5, "a ray through a pixel comes back to that pixel", back)
    cam.FieldOfView = 90
    frames()
    check(cam.FieldOfView == 90, "FieldOfView can be changed")
    cam.FieldOfView = 500
    check(cam.FieldOfView == 120, "FieldOfView stops at 120")
    cam.FieldOfView = 60

    -- Fixed: it stays where it is.
    cam.CameraType = Enum.CameraType.Fixed
    cam.CFrame = CFrame.new(Vector3.new(10, 12, 10), Vector3.new(0, 0, 0))
    pcall(function() humanoid:MoveTo(Vector3.new(30, 0, 30)) end)
    frames(20)
    check(near(cam.CFrame.Position, Vector3.new(10, 12, 10), 0.01), "Fixed stays put while the character moves", cam.CFrame.Position)

    -- Back to Custom, following a part instead of the character.
    cam.CameraType = Enum.CameraType.Custom
    local ball = Instance.new("Part")
    ball.Anchored = true
    ball.Position = Vector3.new(-30, 6, -30)
    ball.Parent = workspace
    cam.CameraSubject = ball
    check(cam.CameraSubject == ball, "CameraSubject can be a part")
    frames(60)
    check((cam.Focus.Position - ball.Position).Magnitude < 1, "the camera follows its subject", cam.Focus.Position)
    cam.CameraSubject = humanoid
    check(cam.CameraSubject == humanoid, "and back to the Humanoid")
    frames(60)
    check((cam.Focus.Position - head.Position).Magnitude < 2, "the camera is back on the character", cam.Focus.Position)

    -- Bad values say what's wrong.
    check(not pcall(function() cam.CameraType = "Sideways" end), "an unknown CameraType is an error")
    check(not pcall(function() me.CameraMode = "Upside" end), "an unknown CameraMode is an error")
    check(not pcall(function() return cam.Banana end), "an unknown property is an error")

    print(fails == 0 and "CAMERA TEST DONE: all passed" or ("CAMERA TEST DONE: " .. fails .. " failed"))
    -- Leave it looking down at the world from a script's CFrame for the screenshot.
    cam.CameraType = Enum.CameraType.Scriptable
    cam.CFrame = CFrame.new(Vector3.new(0, 30, 30), Vector3.new(0, 0, 0))
end)
