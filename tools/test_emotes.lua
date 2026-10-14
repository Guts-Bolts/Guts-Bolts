-- Checks emotes in a real play test: "/e dance" and friends as keyframed animations,
-- humanoid:PlayEmote, the emote menu (GuiService / StarterGui:SetCoreGuiEnabled), and
-- that moving or jumping stops an emote. Run it in Studio's play test (the Command Bar
-- runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_emotes.lua)" \
--       --screenshot /tmp/emotes.png --frames 1500
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "EMOTES TEST DONE".
-- The screenshot shows the character dancing with the emote menu open.
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local function seconds(s) frames(math.floor(s * 60 + 0.5)) end

    local me = game.Players.LocalPlayer
    local char = me.Character
    local humanoid = char.Humanoid
    local animator = humanoid.Animator
    local rightArm, leftArm, torso = char["Right Arm"], char["Left Arm"], char.Torso
    local function armUp(arm) return (arm.Position - torso.Position).Y end   -- how high the arm's middle is, from the Torso's
    local function emoteTrack()
        for _, t in ipairs(animator:GetPlayingAnimationTracks()) do return t end
        return nil
    end
    seconds(1)   -- land on the ground first
    local restArm = armUp(rightArm)

    -- Waving: the right hand goes up, as an AnimationTrack everyone sees, then ends by itself.
    check(humanoid:PlayEmote("wave") == true, "PlayEmote(\"wave\") plays")
    seconds(0.6)
    local track = emoteTrack()
    check(track ~= nil and track.Name == "Wave" and track.IsPlaying, "the wave is an AnimationTrack on the Animator", track and track.Name)
    check(track ~= nil and not track.Looped and track.Length > 1, "a wave plays once", track and track.Length)
    check(armUp(rightArm) > restArm + 0.6, "the right arm is up in the air", armUp(rightArm) - restArm)
    check(math.abs(armUp(leftArm) - restArm) < 0.2, "the left arm stays down", armUp(leftArm) - restArm)
    seconds(2.6)
    check(emoteTrack() == nil, "the wave ends by itself")
    check(math.abs(armUp(rightArm) - restArm) < 0.2, "and the arm comes back down", armUp(rightArm) - restArm)

    -- Every built-in emote plays, and the names are forgiving.
    for _, name in ipairs({ "dance", "dance2", "dance3", "point", "cheer", "laugh", "Dance1", "DANCE" }) do
        check(humanoid:PlayEmote(name) == true, "PlayEmote(\"" .. name .. "\")")
        seconds(0.3)
    end
    check(humanoid:PlayEmote("moonwalk") == false, "an emote nobody has doesn't play")
    check(not pcall(function() humanoid:PlayEmote() end), "PlayEmote needs a name")

    -- Dances go on until you move.
    humanoid:PlayEmote("dance")
    seconds(3)
    track = emoteTrack()
    check(track ~= nil and track.Name == "Dance" and track.Looped and track.IsPlaying, "a dance keeps going", track and track.Name)
    -- Scripts can hold it on one frame, like any track.
    track:AdjustSpeed(0)
    track.TimePosition = 0.26
    seconds(0.3)
    local held = armUp(leftArm)
    seconds(0.3)
    check(math.abs(armUp(leftArm) - held) < 0.01 and armUp(leftArm) > restArm + 0.6, "AdjustSpeed(0) holds the pose (left arm up)", armUp(leftArm) - restArm)
    track:AdjustSpeed(1)

    -- Jumping stops it.
    humanoid.Jump = true
    seconds(0.4)
    local t = emoteTrack()
    check(t == nil or not t.IsPlaying, "jumping stops the dance")
    seconds(1.5)

    -- Walking stops it too (something pushes the character along).
    humanoid:PlayEmote("dance2")
    seconds(0.5)
    check(emoteTrack() ~= nil, "dancing again")
    local push = Instance.new("BodyVelocity")
    push.MaxForce = Vector3.new(1e6, 0, 1e6)
    push.Velocity = Vector3.new(8, 0, 0)
    push.Parent = char.HumanoidRootPart
    seconds(0.6)
    push:Destroy()
    t = emoteTrack()
    check(t == nil or not t.IsPlaying, "walking stops the dance")
    seconds(1)

    -- In the air you can't start one.
    humanoid.Jump = true
    seconds(0.15)
    check(humanoid:PlayEmote("cheer") == false, "no emotes while jumping")
    seconds(1.5)

    -- The emote menu: on by default; scripts can open it, and games can turn it off.
    local StarterGui = game:GetService("StarterGui")
    local GuiService = game:GetService("GuiService")
    check(StarterGui:GetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu) == true, "the emote menu is on to start with")
    check(GuiService:GetEmotesMenuOpen() == false, "and closed")
    GuiService:SetEmotesMenuOpen(true)
    check(GuiService:GetEmotesMenuOpen() == true, "SetEmotesMenuOpen(true) opens it")
    GuiService:SetEmotesMenuOpen(false)
    check(GuiService:GetEmotesMenuOpen() == false, "SetEmotesMenuOpen(false) closes it")
    StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false)
    check(StarterGui:GetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu) == false, "SetCoreGuiEnabled(EmotesMenu, false) turns it off")
    GuiService:SetEmotesMenuOpen(true)
    check(GuiService:GetEmotesMenuOpen() == false, "a game that turned it off keeps it shut")
    StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.All, true)
    check(StarterGui:GetCoreGuiEnabled("EmotesMenu") == true, "SetCoreGuiEnabled(All, true) turns it back on")
    StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.All, false)
    check(StarterGui:GetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu) == false and StarterGui:GetCoreGuiEnabled(Enum.CoreGuiType.All) == false,
          "All, false turns everything off")
    StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, true)
    check(StarterGui:GetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu) == true, "then the emote menu alone back on")
    check(not pcall(function() StarterGui:SetCoreGuiEnabled("Banana", false) end), "an unknown CoreGuiType is an error")
    check(not pcall(function() workspace:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false) end), "SetCoreGuiEnabled is StarterGui's")
    -- Emotes still play from scripts with the menu off (the menu is just a way in).
    StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false)
    check(humanoid:PlayEmote("wave") == true, "PlayEmote works with the menu off")
    seconds(2.6)
    StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, true)

    print(fails == 0 and "EMOTES TEST DONE: all passed" or ("EMOTES TEST DONE: " .. fails .. " failed"))
    -- For the screenshot: dancing (held on a big move), the menu open, the camera in front.
    humanoid:PlayEmote("dance")
    seconds(0.2)
    local d = emoteTrack()
    if d then d:AdjustSpeed(0) d.TimePosition = 0.26 end
    local cam = workspace.CurrentCamera
    cam.CameraType = Enum.CameraType.Scriptable
    local p = char.HumanoidRootPart.Position
    cam.CFrame = CFrame.new(p + Vector3.new(0, 1, 9), p + Vector3.new(0, 0.5, 0))
    GuiService:SetEmotesMenuOpen(true)
end)
