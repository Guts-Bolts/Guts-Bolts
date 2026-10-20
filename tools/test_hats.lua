-- Checks that hats and accessories stay on the head when an animation turns it.
-- Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_hats.lua)" --frames 600
--
-- Every check prints "PASS ..." or "FAIL ...", then "HATS TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local char = game.Players.LocalPlayer.Character
    frames(60)
    local head = char.Head
    local acc = Instance.new("Model")
    acc.Name = "Accessory (hat)"
    local brim = Instance.new("Part")
    brim.Name = "Brim"
    brim.Anchored = true
    brim.CanCollide = false
    brim.Size = Vector3.new(1, 0.2, 1)
    brim.Position = head.Position + Vector3.new(0, 0.6, 0.3)
    brim.Parent = acc
    acc.Parent = char
    local top = Instance.new("Part")   -- like the built-in hats: a "Hat..." part in the character
    top.Name = "Hat Test"
    top.Anchored = true
    top.CanCollide = false
    top.Size = Vector3.new(0.8, 0.6, 0.8)
    top.Position = head.Position + Vector3.new(0, 0.7, 0)
    top.Parent = char
    frames(5)
    local function offset() return head.CFrame:Inverse() * brim.CFrame end
    local before = offset()
    local topBefore = head.CFrame:Inverse() * top.CFrame
    local topDrift = 0
    local look0 = head.CFrame.LookVector
    check(char.Humanoid:PlayEmote("dance") == true, "dance plays")
    local turned, drift = 0, 0
    for _ = 1, 120 do
        frames(1)
        turned = math.max(turned, (head.CFrame.LookVector - look0).Magnitude)
        local o = offset()
        local t = head.CFrame:Inverse() * top.CFrame
        topDrift = math.max(topDrift, (t.Position - topBefore.Position).Magnitude)
        drift = math.max(drift, (o.Position - before.Position).Magnitude + (o.LookVector - before.LookVector).Magnitude)
    end
    check(turned > 0.1, "the head turned while dancing", turned)
    check(drift < 0.02, "the hat stayed on the head the whole time", drift)
    check(topDrift < 0.02, "the built-in kind of hat did too", topDrift)
    print("HATS TEST DONE fails=" .. fails)
end)
