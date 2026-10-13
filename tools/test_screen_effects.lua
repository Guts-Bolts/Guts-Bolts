-- Checks the Sky and the screen effects (ColorCorrectionEffect, SunRaysEffect,
-- DepthOfFieldEffect, BloomEffect, BlurEffect) in a real play test.
-- Run it in Studio's play test (the Command Bar runs it inside the game), from the
-- repository's folder (the skybox uses the clothing template pictures in assets/):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_screen_effects.lua)" \
--       --screenshot /tmp/effects.png --frames 400
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "SCREEN EFFECTS TEST DONE".
-- It leaves a skybox, a warm colour correction and bloom on for the screenshot.
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function near(a, b) return math.abs(a - b) < 1e-4 end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local Lighting = game:GetService("Lighting")
    local cam = workspace.CurrentCamera

    -- Every class can be made, says what it is, and starts with the usual defaults.
    local classes = { "ColorCorrectionEffect", "SunRaysEffect", "DepthOfFieldEffect", "BloomEffect", "BlurEffect" }
    for _, cls in ipairs(classes) do
        local e = Instance.new(cls)
        check(e.ClassName == cls and e:IsA(cls) and e:IsA("PostEffect") and e:IsA("Instance"), cls .. " is made and IsA PostEffect")
        check(e.Enabled == true, cls .. " starts Enabled")
        e:Destroy()
    end
    local cc = Instance.new("ColorCorrectionEffect")
    check(cc.Brightness == 0 and cc.Contrast == 0 and cc.Saturation == 0 and cc.TintColor == Color3.new(1, 1, 1),
          "ColorCorrectionEffect defaults")
    local rays = Instance.new("SunRaysEffect")
    check(near(rays.Intensity, 0.25) and rays.Spread == 1, "SunRaysEffect defaults", rays.Intensity)
    local dof = Instance.new("DepthOfFieldEffect")
    check(near(dof.FarIntensity, 0.75) and near(dof.FocusDistance, 0.05) and dof.InFocusRadius == 10 and near(dof.NearIntensity, 0.75),
          "DepthOfFieldEffect defaults")
    local bloom = Instance.new("BloomEffect")
    check(near(bloom.Intensity, 0.4) and bloom.Size == 24 and near(bloom.Threshold, 0.95), "BloomEffect defaults")
    local blur = Instance.new("BlurEffect")
    check(blur.Size == 24, "BlurEffect defaults")
    local sky = Instance.new("Sky")
    check(sky.ClassName == "Sky" and sky.StarCount == 3000 and sky.CelestialBodiesShown == true and
          sky.SunAngularSize == 21 and sky.MoonAngularSize == 11 and sky.SkyboxBk == "", "Sky defaults")

    -- Properties read back, and stay in range.
    cc.Brightness = 0.1
    cc.Contrast = 0.2
    cc.Saturation = 5
    cc.TintColor = Color3.fromRGB(255, 200, 150)
    check(near(cc.Brightness, 0.1) and near(cc.Contrast, 0.2), "ColorCorrection Brightness / Contrast read back")
    check(cc.Saturation == 1, "Saturation stops at 1", cc.Saturation)
    check(math.abs(cc.TintColor.G - 200 / 255) < 1e-3, "TintColor reads back", cc.TintColor)
    rays.Intensity = 0.5
    rays.Spread = 0.6
    check(near(rays.Intensity, 0.5) and near(rays.Spread, 0.6), "SunRays Intensity / Spread read back")
    dof.FocusDistance = 30
    dof.InFocusRadius = 8
    dof.FarIntensity = 1
    dof.NearIntensity = 0
    check(dof.FocusDistance == 30 and dof.InFocusRadius == 8 and dof.FarIntensity == 1 and dof.NearIntensity == 0,
          "DepthOfField settings read back")
    bloom.Size = 100
    check(bloom.Size == 56, "Bloom Size stops at 56", bloom.Size)
    blur.Size = 12
    check(blur.Size == 12, "Blur Size reads back")
    sky.StarCount = 99999
    check(sky.StarCount == 5000, "StarCount stops at 5000", sky.StarCount)
    sky.CelestialBodiesShown = false
    check(sky.CelestialBodiesShown == false, "CelestialBodiesShown reads back")
    sky.SunAngularSize = 30
    sky.MoonAngularSize = 15
    check(sky.SunAngularSize == 30 and sky.MoonAngularSize == 15, "Sun / Moon angular size read back")
    sky.SkyboxUp = "assets/templates/shirt_template.png"
    check(sky.SkyboxUp == "assets/templates/shirt_template.png", "Skybox pictures read back")

    -- They go in Lighting, and Lighting finds them.
    cc.Parent = Lighting
    sky.Parent = Lighting
    bloom.Parent = Lighting
    check(cc.Parent == Lighting and sky.Parent == game.Lighting, "Parent is Lighting")
    check(Lighting:FindFirstChildOfClass("Sky") == sky, "Lighting:FindFirstChildOfClass('Sky')")
    check(Lighting:FindFirstChild("ColorCorrectionEffect") == cc and Lighting.Sky == sky, "Lighting finds them by name")
    check(#Lighting:GetChildren() >= 3, "Lighting:GetChildren()", #Lighting:GetChildren())
    check(cc:IsDescendantOf(Lighting), "IsDescendantOf(Lighting)")
    check(Lighting.ClockTime ~= nil and Lighting:IsA("Lighting"), "Lighting's own settings still work")
    local made = Instance.new("BlurEffect", Lighting)
    check(made.Parent == Lighting, "Instance.new(class, Lighting)")
    made:Destroy()
    check(Lighting:FindFirstChildOfClass("BlurEffect") == nil, "Destroy takes it out of Lighting")

    -- And in the camera (only on this screen).
    blur.Parent = cam
    rays.Parent = Lighting
    dof.Parent = cam
    check(blur.Parent == cam and cam:FindFirstChildOfClass("BlurEffect") == blur, "an effect in the Camera")
    check(#cam:GetChildren() == 2, "camera:GetChildren()", #cam:GetChildren())
    check(cam.BlurEffect == blur, "camera.BlurEffect")
    local copy = blur:Clone()
    check(copy.Size == 12 and copy.ClassName == "BlurEffect", "Clone keeps the settings")
    copy:Destroy()
    frames(10)   -- (draw a few frames with all of them on)
    blur.Enabled = false
    check(blur.Enabled == false, "Enabled can be switched off")
    blur.Parent = nil
    check(blur.Parent == nil and #cam:GetChildren() == 1, "taken out of the camera")
    blur.Parent = cam
    blur:Destroy()
    dof:Destroy()

    -- Several stack.
    local cc2 = Instance.new("ColorCorrectionEffect")
    cc2.Parent = Lighting
    local n = 0
    for _, c in ipairs(Lighting:GetChildren()) do if c:IsA("ColorCorrectionEffect") then n = n + 1 end end
    check(n == 2, "two ColorCorrectionEffects at once", n)
    cc2:Destroy()

    -- Bad values say what's wrong.
    check(not pcall(function() cc.Banana = 1 end), "an unknown property is an error")
    check(not pcall(function() return blur.Intensity end), "BlurEffect has no Intensity")
    check(not pcall(function() Instance.new("ColourCorrection") end), "an unknown class is an error")
    check(not pcall(function() sky.StarCount = "lots" end), "StarCount needs a number")

    -- Leave a look for the screenshot: a skybox (two pictures), a warm grade, glow, sun rays.
    sky.CelestialBodiesShown = true
    sky.StarCount = 3000
    sky.SkyboxBk = "assets/templates/shirt_template.png"
    sky.SkyboxFt = "assets/templates/shirt_template.png"
    sky.SkyboxLf = "assets/templates/pants_template.png"
    sky.SkyboxRt = "assets/templates/pants_template.png"
    sky.SkyboxUp = "assets/templates/shirt_template.png"
    sky.SkyboxDn = "assets/templates/pants_template.png"
    cc.Brightness = 0.05
    cc.Contrast = 0.15
    cc.Saturation = 0.4
    bloom.Size = 40
    print(fails == 0 and "SCREEN EFFECTS TEST DONE: all passed" or ("SCREEN EFFECTS TEST DONE: " .. fails .. " failed"))
    cam.CameraType = Enum.CameraType.Scriptable
    cam.CFrame = CFrame.new(Vector3.new(0, 8, 25), Vector3.new(0, 14, 0))
end)
