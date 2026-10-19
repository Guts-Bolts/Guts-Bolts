-- Checks UIGradient, UIScale, UIAspectRatioConstraint and ViewportFrame in a real play test,
-- and leaves them on screen for a screenshot. Run it in Studio's play test:
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_ui_pieces.lua)" \
--       --screenshot /tmp/ui_pieces.png --frames 400
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "UI PIECES TEST DONE".
-- The screenshot shows, left to right: a rainbow gradient panel (with gradient text), a
-- panel made twice as big by a UIScale, a square kept square by a UIAspectRatioConstraint,
-- and a ViewportFrame with a red ball, a blue block and a yellow cylinder in it.
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function near(a, b, eps) return math.abs(a - b) <= (eps or 0.5) end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end

    local gui = Instance.new("ScreenGui")
    gui.Name = "UIPieces"
    gui.Parent = game:GetService("StarterGui")
    local function panel(name, x)
        local f = Instance.new("Frame")
        f.Name = name
        f.Size = UDim2.fromOffset(160, 160)
        f.Position = UDim2.new(x, 0, 0.5, 0)
        f.AnchorPoint = Vector2.new(0, 0.5)
        f.BackgroundColor3 = Color3.fromRGB(255, 255, 255)
        f.BorderSizePixel = 0
        f.Parent = gui
        return f
    end

    -- 1. UIGradient: red -> yellow -> blue across, fading out at the bottom when turned.
    local g1 = panel("Gradient", 0.02)
    Instance.new("UICorner", g1).CornerRadius = UDim.new(0, 16)
    local grad = Instance.new("UIGradient")
    check(grad.ClassName == "UIGradient" and grad:IsA("UIComponent"), "UIGradient is a UIComponent")
    check(grad.Enabled == true and grad.Rotation == 0 and grad.Offset == Vector2.new(0, 0), "UIGradient defaults")
    check(#grad.Color.Keypoints == 2 and grad.Color.Keypoints[1].Value == Color3.new(1, 1, 1), "default Color is white")
    grad.Color = ColorSequence.new({
        ColorSequenceKeypoint.new(0, Color3.fromRGB(255, 0, 0)),
        ColorSequenceKeypoint.new(0.5, Color3.fromRGB(255, 255, 0)),
        ColorSequenceKeypoint.new(1, Color3.fromRGB(0, 80, 255)),
    })
    grad.Transparency = NumberSequence.new({NumberSequenceKeypoint.new(0, 0), NumberSequenceKeypoint.new(1, 0.6)})
    grad.Rotation = 45
    grad.Offset = Vector2.new(0.05, 0)
    grad.Parent = g1
    check(#grad.Color.Keypoints == 3, "Color keeps 3 keypoints", #grad.Color.Keypoints)
    check(grad.Color.Keypoints[2].Time == 0.5 and grad.Color.Keypoints[2].Value == Color3.fromRGB(255, 255, 0), "keypoint 2 reads back")
    check(near(grad.Transparency.Keypoints[2].Value, 0.6, 1e-4), "Transparency reads back", grad.Transparency.Keypoints[2].Value)
    check(grad.Rotation == 45 and grad.Offset == Vector2.new(0.05, 0), "Rotation and Offset read back")
    check(not pcall(function() grad.Color = "red" end), "a Color that isn't a ColorSequence is an error")
    local label = Instance.new("TextLabel")
    label.Size = UDim2.new(1, 0, 0, 40)
    label.Position = UDim2.new(0, 0, 1, 8)
    label.BackgroundTransparency = 1
    label.Text = "GRADIENT"
    label.TextSize = 30
    label.Font = Enum.Font.SourceSansBold
    label.TextColor3 = Color3.new(1, 1, 1)
    label.Parent = g1
    local lg = Instance.new("UIGradient")
    lg.Color = ColorSequence.new(Color3.fromRGB(255, 120, 0), Color3.fromRGB(160, 0, 255))
    lg.Parent = label

    -- 2. UIScale: a 100 x 80 panel made twice as big (and what's inside it).
    local s1 = panel("Scaled", 0.25)
    s1.Size = UDim2.fromOffset(100, 80)
    s1.BackgroundColor3 = Color3.fromRGB(40, 160, 90)
    Instance.new("UICorner", s1).CornerRadius = UDim.new(0, 8)
    local inner = Instance.new("TextLabel")
    inner.Size = UDim2.fromOffset(60, 20)
    inner.Position = UDim2.fromOffset(20, 10)
    inner.Text = "x2"
    inner.TextSize = 14
    inner.BackgroundColor3 = Color3.fromRGB(230, 255, 230)
    inner.Parent = s1
    frames()
    check(near(s1.AbsoluteSize.X, 100) and near(s1.AbsoluteSize.Y, 80), "before UIScale: 100 x 80", s1.AbsoluteSize)
    local scale = Instance.new("UIScale")
    check(scale.Scale == 1 and scale:IsA("UIComponent"), "UIScale starts at 1")
    scale.Scale = 2
    scale.Parent = s1
    frames()
    check(near(s1.AbsoluteSize.X, 200) and near(s1.AbsoluteSize.Y, 160), "UIScale 2: 200 x 160", s1.AbsoluteSize)
    check(near(inner.AbsoluteSize.X, 120) and near(inner.AbsoluteSize.Y, 40), "what's inside is scaled too", inner.AbsoluteSize)
    check(near(inner.AbsolutePosition.X - s1.AbsolutePosition.X, 40), "and its Position offset", inner.AbsolutePosition.X - s1.AbsolutePosition.X)
    check(near(s1.AbsolutePosition.Y + s1.AbsoluteSize.Y / 2, gui.AbsolutePosition.Y + gui.AbsoluteSize.Y / 2), "it grows around its AnchorPoint")
    check(not pcall(function() scale.Scale = -1 end), "a negative Scale is an error")

    -- 3. UIAspectRatioConstraint: half the screen's height tall, but always square.
    local a1 = panel("Square", 0.53)
    a1.Size = UDim2.new(0, 160, 0.4, 0)   -- wider than tall without the constraint
    a1.BackgroundColor3 = Color3.fromRGB(60, 110, 230)
    Instance.new("UICorner", a1).CornerRadius = UDim.new(0, 8)
    local ar = Instance.new("UIAspectRatioConstraint")
    check(ar.AspectRatio == 1 and ar.AspectType == Enum.AspectType.FitWithinMaxSize and ar.DominantAxis == Enum.DominantAxis.Width,
          "UIAspectRatioConstraint defaults")
    check(ar:IsA("UIConstraint") and ar:IsA("UIComponent"), "it's a UIConstraint")
    ar.Parent = a1
    frames()
    local h = gui.AbsoluteSize.Y * 0.4
    local side = math.min(h, 160)
    check(near(a1.AbsoluteSize.Y, side, 1) and near(a1.AbsoluteSize.X, side, 1), "FitWithinMaxSize: square, as big as fits", a1.AbsoluteSize)
    ar.AspectRatio = 2
    frames()
    check(near(a1.AbsoluteSize.X, 160, 1) and near(a1.AbsoluteSize.Y, 80, 1), "AspectRatio 2 fits the width", a1.AbsoluteSize)
    ar.AspectType = Enum.AspectType.ScaleWithParentSize
    ar.DominantAxis = Enum.DominantAxis.Height
    frames()
    check(near(a1.AbsoluteSize.Y, h, 1) and near(a1.AbsoluteSize.X, h * 2, 1), "ScaleWithParentSize + Height: width follows", a1.AbsoluteSize)
    check(ar.AspectType == Enum.AspectType.ScaleWithParentSize and ar.DominantAxis == Enum.DominantAxis.Height, "enums read back")
    check(not pcall(function() ar.AspectRatio = 0 end), "AspectRatio 0 is an error")
    check(not pcall(function() ar.AspectType = "Banana" end), "an unknown AspectType is an error")
    check(not pcall(function() ar.DominantAxis = "Diagonal" end), "an unknown DominantAxis is an error")
    -- Back to a square for the picture.
    ar.AspectRatio = 1
    ar.AspectType = Enum.AspectType.FitWithinMaxSize
    a1.Size = UDim2.new(0, 300, 0.4, 0)

    -- 4. ViewportFrame: a little 3D picture of what's inside it.
    local vf = Instance.new("ViewportFrame")
    vf.Name = "Preview"
    vf.Size = UDim2.fromOffset(190, 190)
    vf.Position = UDim2.new(0.74, 0, 0.5, 0)
    vf.AnchorPoint = Vector2.new(0, 0.5)
    vf.BackgroundColor3 = Color3.fromRGB(30, 34, 48)
    vf.Parent = gui
    Instance.new("UICorner", vf).CornerRadius = UDim.new(0, 12)
    check(vf:IsA("GuiObject") and vf.CurrentCamera == nil, "a ViewportFrame is a GuiObject with no camera yet")
    check(vf.Ambient == Color3.fromRGB(200, 200, 200) and vf.LightColor == Color3.fromRGB(140, 140, 140)
          and vf.LightDirection == Vector3.new(-1, -1, -1), "Roblox's lighting defaults")
    local ball = Instance.new("Part")
    ball.Shape = Enum.PartType.Ball
    ball.Size = Vector3.new(2, 2, 2)
    ball.Position = Vector3.new(-1.6, 1, 0)
    ball.Color = Color3.fromRGB(230, 50, 50)
    ball.Anchored = false   -- (in a ViewportFrame it doesn't fall)
    ball.Parent = vf
    local world = Instance.new("WorldModel")
    check(world:IsA("WorldModel") and world:IsA("Model"), "WorldModel is a Model")
    world.Parent = vf
    local block = Instance.new("Part")
    block.Size = Vector3.new(1.8, 1.8, 1.8)
    block.Position = Vector3.new(1.4, 0.9, 0.3)
    block.Orientation = Vector3.new(0, 30, 0)
    block.Color = Color3.fromRGB(60, 120, 255)
    block.Anchored = true
    block.Parent = world
    local cyl = Instance.new("Part")
    cyl.Shape = Enum.PartType.Cylinder
    cyl.Size = Vector3.new(1, 0.6, 1)
    cyl.Position = Vector3.new(0, 2.4, 1.2)
    cyl.Color = Color3.fromRGB(255, 210, 40)
    cyl.Material = Enum.Material.Neon
    cyl.Parent = world
    local cam = Instance.new("Camera")
    check(cam.ClassName == "Camera" and cam.FieldOfView == 70, "Instance.new(\"Camera\") with FieldOfView 70")
    cam.FieldOfView = 50
    cam.CFrame = CFrame.new(Vector3.new(0, 4, -7.5), Vector3.new(0, 1, 0))
    vf.CurrentCamera = cam
    check(vf.CurrentCamera == cam, "CurrentCamera reads back")
    check(cam.Parent == vf, "a Camera with no Parent goes into the frame")
    check(cam.FieldOfView == 50, "FieldOfView reads back")
    check(not pcall(function() vf.CurrentCamera = ball end), "a CurrentCamera that isn't a Camera is an error")
    check(not pcall(function() vf.CurrentCamera = workspace.CurrentCamera end), "the player's own camera can't be a CurrentCamera")
    vf.LightDirection = Vector3.new(-1, -2, 1)
    vf.Ambient = Color3.fromRGB(150, 150, 160)
    check(vf.LightDirection == Vector3.new(-1, -2, 1) and vf.Ambient == Color3.fromRGB(150, 150, 160), "lighting reads back")

    frames(30)
    -- The parts in it are only a picture: they don't fall, and rays in the world miss them.
    check(math.abs(ball.Position.Y - 1) < 1e-3, "an unanchored part in a ViewportFrame doesn't fall", ball.Position.Y)
    local hit = workspace:Raycast(Vector3.new(-1.6, 50, 0), Vector3.new(0, -100, 0))
    check(not hit or (hit.Instance ~= ball and not hit.Instance:IsDescendantOf(vf)), "world rays miss the ViewportFrame's parts")
    -- A copy looks through its own copied camera.
    local copy = vf:Clone()
    local copyCam = copy:FindFirstChildOfClass("Camera")
    check(copyCam and copy.CurrentCamera == copyCam and copyCam ~= cam, "a cloned ViewportFrame uses its cloned Camera")
    copy:Destroy()
    check(vf.AbsoluteSize.X > 189 and vf.AbsoluteSize.Y > 189, "the ViewportFrame is on screen", vf.AbsoluteSize)

    print(fails == 0 and "UI PIECES TEST DONE: all passed" or ("UI PIECES TEST DONE: " .. fails .. " failed"))
end)
