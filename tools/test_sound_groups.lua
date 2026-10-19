-- Checks SoundGroups and sound effects in a real play test: the objects and their
-- properties, how loud a Sound really plays (its Volume times its groups'), which effects it
-- goes through and in what order, and what each effect does to a test tone (the same sound
-- processing the mixer runs, so this works without a sound card). Run it like the others:
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_sound_groups.lua)" \
--       --screenshot /tmp/sound.png --frames 600
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "SOUND GROUPS TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function near(a, b, eps) return type(a) == "number" and math.abs(a - b) <= (eps or 1e-4) end
    local mix = __gb_soundMix   -- (volume, effects in order, loudness of a test tone, its pitch)

    local SoundService = game:GetService("SoundService")
    check(SoundService ~= nil, "game:GetService(\"SoundService\")")

    -- SoundGroup
    local music = Instance.new("SoundGroup")
    music.Name = "Music"
    music.Parent = SoundService
    check(music.ClassName == "SoundGroup" and music:IsA("SoundGroup") and music:IsA("Instance"), "a SoundGroup is a SoundGroup")
    check(near(music.Volume, 0.5), "a SoundGroup's Volume starts at 0.5", music.Volume)
    check(music.Parent == SoundService, "it goes in SoundService")

    local part = Instance.new("Part", workspace)
    part.Anchored = true
    local sound = Instance.new("Sound", part)
    sound.Volume = 0.8
    check(sound.SoundGroup == nil, "a Sound has no SoundGroup to start with")
    local v = mix(sound)
    check(near(v, 0.8), "without a group a Sound plays at its own Volume", v)
    sound.SoundGroup = music
    check(sound.SoundGroup == music, "Sound.SoundGroup reads back")
    v = mix(sound)
    check(near(v, 0.4), "the group's Volume multiplies the Sound's (0.8 x 0.5)", v)
    music.Volume = 2
    check(near(mix(sound), 1.6), "changing the group's Volume changes the Sound's", mix(sound))

    -- Nested groups multiply again.
    local master = Instance.new("SoundGroup")
    master.Name = "Master"
    master.Volume = 0.25
    master.Parent = SoundService
    music.Parent = master
    check(near(mix(sound), 0.8 * 2 * 0.25), "a group inside another group multiplies again", mix(sound))
    music.Parent = SoundService
    check(near(mix(sound), 1.6), "taking it out stops that", mix(sound))

    -- Limits and mistakes.
    music.Volume = 50
    check(near(music.Volume, 10), "Volume is kept to 0-10", music.Volume)
    music.Volume = -3
    check(near(music.Volume, 0) and near(mix(sound), 0), "a group at 0 silences its sounds")
    music.Volume = 1
    local ok = pcall(function() sound.SoundGroup = part end)
    check(not ok, "Sound.SoundGroup must be a SoundGroup")
    sound.SoundGroup = nil
    check(sound.SoundGroup == nil and near(mix(sound), 0.8), "SoundGroup = nil takes it out of the group")
    sound.SoundGroup = music
    music:Destroy()
    check(sound.SoundGroup == nil and near(mix(sound), 0.8), "a destroyed group no longer counts")

    -- Every effect: its class, Enabled, Priority and starting settings (Roblox's).
    local defaults = {
        ReverbSoundEffect = { DecayTime = 1.5, Density = 1, Diffusion = 1, DryLevel = 0, WetLevel = 0 },
        EchoSoundEffect = { Delay = 1, DryLevel = 0, Feedback = 0.5, WetLevel = 0 },
        EqualizerSoundEffect = { HighGain = 0, LowGain = 0, MidGain = 0 },
        DistortionSoundEffect = { Level = 0.75 },
        PitchShiftSoundEffect = { Octave = 1.25 },
        ChorusSoundEffect = { Depth = 0.15, Mix = 0.5, Rate = 0.5 },
        CompressorSoundEffect = { Attack = 0.1, GainMakeup = 0, Ratio = 40, Release = 0.1, Threshold = -40 },
        FlangeSoundEffect = { Depth = 0.45, Mix = 0.85, Rate = 5 },
        TremoloSoundEffect = { Depth = 1, Duty = 0.5, Frequency = 5 },
    }
    for cls, props in pairs(defaults) do
        local fx = Instance.new(cls)
        local good = fx.ClassName == cls and fx:IsA("SoundEffect") and fx.Enabled == true and fx.Priority == 0
        local bad
        for k, want in pairs(props) do
            if not near(fx[k], want, 1e-3) then good, bad = false, k .. " = " .. tostring(fx[k]) end
        end
        check(good, cls .. " starts like Roblox's", bad)
        fx:Destroy()
    end
    local p = Instance.new("PitchShiftSoundEffect")
    p.Octave = 5
    check(near(p.Octave, 2), "settings are kept in range (Octave at most 2)", p.Octave)
    p:Destroy()

    -- Which effects a Sound goes through: its own (highest Priority first), then its group's.
    local sfx = Instance.new("SoundGroup", SoundService)
    sfx.Volume = 1
    local s2 = Instance.new("Sound", workspace)
    s2.Volume = 1
    s2.SoundGroup = sfx
    local echo = Instance.new("EchoSoundEffect", s2)
    local reverb = Instance.new("ReverbSoundEffect", s2)
    reverb.Priority = 5
    local eq = Instance.new("EqualizerSoundEffect", sfx)
    local _, order = mix(s2)
    check(table.concat(order, ",") == "ReverbSoundEffect,EchoSoundEffect,EqualizerSoundEffect",
          "a Sound's effects (by Priority) come before its group's", table.concat(order, ","))
    echo.Enabled = false
    _, order = mix(s2)
    check(table.concat(order, ",") == "ReverbSoundEffect,EqualizerSoundEffect", "a disabled effect is skipped", table.concat(order, ","))
    echo:Destroy()
    reverb:Destroy()

    -- What the effects do to a test tone. (Group effects count for the group's sounds.)
    local _, _, loud = mix(s2, 440)
    check(near(loud, 1, 0.02), "an equalizer at 0 dB leaves the sound as it was", loud)
    eq.HighGain = -80
    _, _, loud = mix(s2, 9000)
    check(loud < 0.1, "HighGain -80 silences high sounds", loud)
    _, _, loud = mix(s2, 150)
    check(loud > 0.8, "...but not low ones", loud)
    eq.HighGain = 0
    eq.LowGain = -80
    _, _, loud = mix(s2, 80)
    check(loud < 0.15, "LowGain -80 silences low sounds", loud)
    eq.LowGain = 0
    eq.MidGain = 10
    _, _, loud = mix(s2, 1200)
    check(loud > 2, "MidGain +10 makes the middle louder", loud)
    eq:Destroy()

    local function only(cls, settings, freq)
        for _, c in ipairs(s2:GetChildren()) do c:Destroy() end
        local fx = Instance.new(cls)
        for k, val in pairs(settings or {}) do fx[k] = val end
        fx.Parent = s2
        local _, _, l, pitch = mix(s2, freq or 440)
        return l, pitch, fx
    end
    local l, pitch = only("PitchShiftSoundEffect", { Octave = 2 })
    check(math.abs(pitch - 880) < 90, "PitchShift Octave 2 plays an octave higher", pitch)
    l, pitch = only("PitchShiftSoundEffect", { Octave = 0.5 })
    check(math.abs(pitch - 220) < 30, "PitchShift Octave 0.5 plays an octave lower", pitch)
    l, pitch = only("DistortionSoundEffect", { Level = 1 })
    check(l > 1.05, "Distortion pushes a quiet tone into clipping (louder, squarer)", l)
    l = only("DistortionSoundEffect", { Level = 0 })
    check(near(l, 1, 0.01), "Distortion at Level 0 is clean", l)
    l = only("CompressorSoundEffect", { Threshold = -40, Ratio = 40 })
    check(l < 0.1, "a Compressor turns a loud tone right down", l)
    l = only("CompressorSoundEffect", { Threshold = -40, Ratio = 40, GainMakeup = 30 })
    check(l > 0.2, "GainMakeup brings it back up", l)
    l = only("TremoloSoundEffect", { Depth = 1, Duty = 0.5, Frequency = 5 })
    check(l > 0.5 and l < 0.85, "Tremolo pulses the volume (about half the time quiet)", l)
    l = only("TremoloSoundEffect", { Depth = 0 })
    check(near(l, 1, 0.01), "Tremolo with Depth 0 does nothing", l)
    l = only("EchoSoundEffect", { Delay = 0.1, Feedback = 0.5, DryLevel = -80, WetLevel = 0 })
    check(l > 0.3, "an Echo's echoes are heard (only the echoes here)", l)
    l = only("ReverbSoundEffect", { DryLevel = -80, WetLevel = 0 })
    check(l > 0.02 and l < 5, "a Reverb's tail is heard (only the reverb here)", l)
    l = only("ReverbSoundEffect", { DryLevel = 0, WetLevel = -80 })
    check(near(l, 1, 0.01), "a Reverb with WetLevel -80 is just the sound", l)
    l, pitch = only("ChorusSoundEffect", { Mix = 1, Depth = 0.5 })
    check(l > 0.5 and l < 1.5 and math.abs(pitch - 440) < 30, "Chorus keeps the note", pitch)
    l = only("FlangeSoundEffect", { Mix = 0 })
    check(near(l, 1, 0.01), "Flange with Mix 0 does nothing", l)
    l = only("FlangeSoundEffect", { Mix = 0.85, Rate = 5 })
    check(l > 0.1 and l < 3, "Flange sweeps the sound", l)

    -- Copies keep everything, and a copied group comes with its sounds.
    for _, c in ipairs(s2:GetChildren()) do c:Destroy() end
    Instance.new("ChorusSoundEffect", s2).Priority = 3
    local copy = s2:Clone()
    copy.Parent = workspace
    check(copy.SoundGroup == sfx, "a copied Sound keeps its SoundGroup")
    local cfx = copy:FindFirstChildOfClass("ChorusSoundEffect")
    check(cfx ~= nil and cfx.Priority == 3, "a copied Sound keeps its effects")
    local box = Instance.new("Model", workspace)
    local g3 = Instance.new("SoundGroup", box)
    local s3 = Instance.new("Sound", box)
    s3.SoundGroup = g3
    local box2 = box:Clone()
    box2.Parent = workspace
    local g4, s4 = box2:FindFirstChildOfClass("SoundGroup"), box2:FindFirstChildOfClass("Sound")
    check(s4.SoundGroup == g4 and g4 ~= g3, "copying a group with its sounds keeps them together")

    -- Playing (with no sound card here it's quiet, but nothing may go wrong).
    local okPlay = pcall(function()
        s2:Play()
        sfx.Volume = 0.3
        task.wait(0.2)
        s2:Stop()
    end)
    check(okPlay, "playing a Sound in a group with effects works")

    print("SOUND GROUPS TEST DONE (" .. fails .. " failed)")
end)
