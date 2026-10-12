-- Particle effects test: ParticleEmitter, Fire, Smoke and Sparkles.
-- Run it in a playtest and take a picture of the result:
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_particles.lua)" \
--       --screenshot particles.png --frames 150
-- It prints "PARTICLES OK" (or "PARTICLES FAILED: ...") and leaves the effects in front
-- of the camera so the screenshot shows them.

local failures = {}
local function check(ok, what)
    if not ok then table.insert(failures, what) end
end
local function near(a, b) return math.abs(a - b) < 1e-3 end
local function fails(f) return not pcall(f) end

-- In front of the character, where the camera is looking.
local spawnPad = workspace:FindFirstChild("SpawnLocation")
local base = spawnPad and spawnPad.Position + Vector3.new(0, 3, 0) or Vector3.new(0, 3, 0)
local look = Vector3.new(-0.6, 0, -1).Unit   -- (about the way the play camera starts out looking)
print("particles test: around " .. tostring(base))
local side = Vector3.new(-look.Z, 0, look.X)
local function pad(name, offset)
    local p = Instance.new("Part")
    p.Name = name
    p.Anchored = true
    p.Size = Vector3.new(1, 1, 1)
    p.Position = base + look * 8 + side * offset + Vector3.new(0, -1, 0)
    p.Color = Color3.fromRGB(60, 60, 70)
    p.Parent = workspace
    return p
end

-- NumberRange
local r = NumberRange.new(2, 4)
check(r.Min == 2 and r.Max == 4, "NumberRange.new(2, 4)")
check(NumberRange.new(3).Min == 3 and NumberRange.new(3).Max == 3, "NumberRange.new(3)")
check(fails(function() NumberRange.new(5, 1) end), "NumberRange.new(5, 1) should fail")
check(fails(function() NumberRange.new("x") end), "NumberRange.new('x') should fail")

-- ParticleEmitter: defaults
local emitterPart = pad("EmitterPad", -6)
local pe = Instance.new("ParticleEmitter")
check(pe.ClassName == "ParticleEmitter", "ParticleEmitter ClassName")
check(pe.Enabled == true, "ParticleEmitter.Enabled default")
check(pe.Rate == 20, "ParticleEmitter.Rate default")
check(pe.Lifetime.Min == 5 and pe.Lifetime.Max == 10, "ParticleEmitter.Lifetime default")
check(pe.Speed.Min == 5 and pe.Speed.Max == 5, "ParticleEmitter.Speed default")
check(pe.EmissionDirection == "Top", "ParticleEmitter.EmissionDirection default")
check(pe.LockedToPart == false, "ParticleEmitter.LockedToPart default")
-- ...and setting everything
pe.Rate = 60
pe.Lifetime = NumberRange.new(1.5, 2.5)
pe.Speed = NumberRange.new(6, 9)
pe.SpreadAngle = Vector2.new(25, 25)
pe.Color = ColorSequence.new(Color3.fromRGB(80, 200, 255), Color3.fromRGB(255, 80, 200))
pe.Size = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.6), NumberSequenceKeypoint.new(1, 0.1) })
pe.Transparency = NumberSequence.new(0, 1)
pe.LightEmission = 0.8
pe.Acceleration = Vector3.new(0, -6, 0)
pe.Drag = 0.5
pe.EmissionDirection = Enum.NormalId.Top
pe.Rotation = NumberRange.new(0, 90)
pe.RotSpeed = NumberRange.new(-45, 45)
pe.LockedToPart = false
pe.Texture = ""
pe.Parent = emitterPart
check(pe.Rate == 60, "Rate")
check(near(pe.Lifetime.Min, 1.5) and near(pe.Lifetime.Max, 2.5), "Lifetime")
check(pe.Speed.Min == 6 and pe.Speed.Max == 9, "Speed")
check(pe.SpreadAngle.X == 25 and pe.SpreadAngle.Y == 25, "SpreadAngle")
check(#pe.Color.Keypoints == 2 and near(pe.Color.Keypoints[1].Value.R, 80 / 255), "Color")
check(near(pe.Size.Keypoints[1].Value, 0.6) and near(pe.Size.Keypoints[2].Value, 0.1), "Size")
check(near(pe.Transparency.Keypoints[2].Value, 1), "Transparency")
check(near(pe.LightEmission, 0.8), "LightEmission")
check(pe.Acceleration.Y == -6, "Acceleration")
check(near(pe.Drag, 0.5), "Drag")
check(pe.Rotation.Max == 90 and pe.RotSpeed.Min == -45, "Rotation / RotSpeed")
check(pe.Parent == emitterPart, "Parent")
pe.Speed = 7   -- a plain number is a NumberRange from 7 to 7
check(pe.Speed.Min == 7 and pe.Speed.Max == 7, "Speed = number")
pe.Speed = NumberRange.new(6, 9)
pe.Texture = "rbxasset://textures/particles/sparkles_main.dds"
check(pe.Texture == "sparkle", "a Roblox texture becomes the built-in sparkle")
pe.Texture = ""
pe.Size = 0.5   -- a plain number is the same all its life
check(near(pe.Size.Keypoints[1].Value, 0.5), "Size = number")
pe.Size = NumberSequence.new({ NumberSequenceKeypoint.new(0, 0.6), NumberSequenceKeypoint.new(1, 0.1) })

-- Bad values are errors
check(fails(function() pe.Rate = "lots" end), "Rate = 'lots' should fail")
check(fails(function() pe.Lifetime = "long" end), "Lifetime = 'long' should fail")
check(fails(function() pe.EmissionDirection = "Sideways" end), "EmissionDirection = 'Sideways' should fail")
check(fails(function() pe.SpreadAngle = 5 end), "SpreadAngle = 5 should fail (it's a Vector2)")
check(fails(function() pe.Color = "red" end), "Color = 'red' should fail")

-- Emit / Clear
check(pcall(function() pe:Emit(30) end), "Emit(30)")
check(pcall(function() pe:Emit() end), "Emit()")
check(pcall(function() pe:Emit(1e9) end), "Emit(1e9) is capped, not frozen")
check(pcall(function() pe:Clear() end), "Clear()")
check(pcall(function() pe:Emit(40) end), "Emit(40) after Clear")
check(fails(function() pe:Emit("many") end), "Emit('many') should fail")

-- A huge Rate can't freeze anything (the count is capped).
local flood = Instance.new("ParticleEmitter")
flood.Rate = 1000000
flood.Lifetime = NumberRange.new(0.2)
flood.Size = NumberSequence.new(0.05)
flood.Transparency = NumberSequence.new(1)
flood.Parent = pad("FloodPad", 0)
check(flood.Rate == 1000000, "Rate = 1000000 is kept (the count is capped)")

-- Fire
local fire = Instance.new("Fire")
check(fire.ClassName == "Fire", "Fire ClassName")
check(fire.Size == 5 and fire.Heat == 9 and fire.Enabled, "Fire defaults")
check(near(fire.Color.R, 236 / 255) and near(fire.SecondaryColor.G, 80 / 255), "Fire colour defaults")
fire.Size = 6
fire.Heat = 12
fire.Color = Color3.fromRGB(255, 120, 30)
fire.SecondaryColor = Color3.fromRGB(120, 40, 20)
fire.Parent = pad("FirePad", -2)
check(fire.Size == 6 and fire.Heat == 12, "Fire Size / Heat")
fire.Size = 1000
check(fire.Size == 30, "Fire.Size is at most 30")
fire.Size = 6
check(fails(function() fire.Heat = "hot" end), "Fire.Heat = 'hot' should fail")
check(fails(function() fire:Emit(5) end), "Fire:Emit should fail (only ParticleEmitters have it)")

-- Smoke
local smoke = Instance.new("Smoke")
check(smoke.ClassName == "Smoke", "Smoke ClassName")
check(near(smoke.Opacity, 0.5) and smoke.RiseVelocity == 1 and smoke.Size == 1, "Smoke defaults")
smoke.Color = Color3.fromRGB(90, 90, 95)
smoke.Opacity = 0.6
smoke.RiseVelocity = 3
smoke.Size = 2
smoke.Parent = pad("SmokePad", 2)
check(near(smoke.Opacity, 0.6) and smoke.RiseVelocity == 3 and smoke.Size == 2, "Smoke properties")

-- Sparkles
local sparkles = Instance.new("Sparkles")
check(sparkles.ClassName == "Sparkles", "Sparkles ClassName")
check(near(sparkles.SparkleColor.R, 144 / 255), "Sparkles default colour")
sparkles.SparkleColor = Color3.fromRGB(255, 220, 60)
local sparklePad = pad("SparklesPad", 6)
sparklePad.Position = sparklePad.Position + Vector3.new(0, 1.5, 0)
sparkles.Parent = sparklePad
check(sparkles.Enabled, "Sparkles.Enabled")

-- Enabled off and on, Clone and Destroy
smoke.Enabled = false
check(smoke.Enabled == false, "Smoke.Enabled = false")
smoke.Enabled = true
local copy = fire:Clone()
check(copy.ClassName == "Fire" and copy.Size == 6 and copy.Heat == 12, "Clone keeps Fire's settings")
copy:Destroy()
check(game:GetService("Workspace") ~= nil, "still running")

if #failures == 0 then
    print("PARTICLES OK")
else
    print("PARTICLES FAILED: " .. table.concat(failures, "; "))
end
