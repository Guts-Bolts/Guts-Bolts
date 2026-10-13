#include "Premades.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../renderer/MeshLibrary.h"

#include <memory>
#include <glm/gtx/euler_angles.hpp>
#include <string>

namespace {

// ---------------------------------------------------------------------------
// Scripts that ship inside the premade objects. They're written to be read!
// ---------------------------------------------------------------------------

const char* kKillBrick = R"(-- Kill Brick: touching this part knocks out the player.
local part = script.Parent

part.Touched:Connect(function(hit)
    -- 'hit' is the body part that touched us (like "Left Leg").
    -- Its Parent is the character, which has a Humanoid.
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid then
        humanoid.Health = 0
    end
end)
)";

const char* kSpinner = R"(-- Spinner: turns around and around, forever.
local part = script.Parent
local speed = 90   -- degrees per second (try a negative number!)

-- Heartbeat runs this function every frame. 'dt' is the time since the last one.
game:GetService("RunService").Heartbeat:Connect(function(dt)
    part.Orientation = part.Orientation + Vector3.new(0, speed * dt, 0)
end)
)";

const char* kJumpPad = R"(-- Jump Pad: launches the player high into the air.
local pad = script.Parent
local launchSpeed = 20   -- bigger number = higher jump

pad.Touched:Connect(function(hit)
    local character = hit.Parent
    local root = character:FindFirstChild("HumanoidRootPart")
    if root and character:FindFirstChild("Humanoid") then
        root.AssemblyLinearVelocity = Vector3.new(0, launchSpeed, 0)
        Sounds.Play("boing", pad.Position)
    end
end)
)";

const char* kCoin = R"(-- Coin: spins, and disappears when you collect it.
-- _G is shared by every script, so all the coins add to the same counter.
local coin = script.Parent
_G.coins = _G.coins or 0
Gui.Label("Coins", "Coins: " .. _G.coins)

local collected = false

coin.Touched:Connect(function(hit)
    if collected then return end
    if hit.Parent:FindFirstChild("Humanoid") then
        collected = true
        _G.coins = _G.coins + 1
        Gui.Label("Coins", "Coins: " .. _G.coins)
        Sounds.Play("coin", coin.Position)
        coin:Destroy()
    end
end)

-- Spin until we get collected (a destroyed coin has no Parent).
while coin.Parent do
    coin.Orientation = coin.Orientation + Vector3.new(0, 3, 0)
    wait()
end
)";

const char* kDisappearing = R"(-- Disappearing Platform: fades away when stepped on, then comes back.
local part = script.Parent
local busy = false   -- a "debounce" so it only runs once at a time

part.Touched:Connect(function(hit)
    if busy or not hit.Parent:FindFirstChild("Humanoid") then return end
    busy = true

    for i = 1, 10 do
        part.Transparency = i / 10
        wait(0.08)
    end
    part.CanCollide = false   -- now you fall through!

    wait(3)
    part.CanCollide = true
    part.Transparency = 0
    busy = false
end)
)";

const char* kMoving = R"(-- Moving Platform: slides back and forth. Stand on it to ride along!
local part = script.Parent
local start = part.Position
local distance = 8     -- how far it travels
local speed = 0.4      -- how fast

game:GetService("RunService").Heartbeat:Connect(function()
    local offset = math.sin(time() * speed * math.pi) * distance / 2
    part.Position = start + Vector3.new(offset, 0, 0)
end)
)";

const char* kSpeedPad = R"(-- Speed Pad: makes the player run fast for a few seconds.
local pad = script.Parent
local boosted = false

pad.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid and not boosted then
        boosted = true
        local normal = humanoid.WalkSpeed
        humanoid.WalkSpeed = normal * 2.5
        Gui.Message("Speed boost!", 2)
        Sounds.Play("boing", pad.Position)
        wait(5)
        humanoid.WalkSpeed = normal
        boosted = false
    end
end)
)";

const char* kClickButton = R"(-- Click Button: click it with the mouse while playing.
local button = script.Parent
local presses = 0

button.Clicked:Connect(function()
    presses = presses + 1
    Sounds.Play("click", button.Position)
    button.Color = Color3.fromHSV(math.random(), 0.8, 1)
    Gui.Message("Button pressed " .. presses .. " times!", 2)
end)
)";

const char* kDayNight = R"(-- Day / Night Cycle: makes time pass. A full day takes about a minute.
local Lighting = game:GetService("Lighting")
Lighting.ClockTime = 7   -- 7 in the morning

while true do
    Lighting.ClockTime = Lighting.ClockTime + 0.02
    wait(0.05)
end
)";

const char* kDisco = R"(-- Disco Floor: every tile (and the light) changes colour to the beat.
local floor = script.Parent

while true do
    for _, tile in ipairs(floor:GetChildren()) do
        if tile:IsA("BasePart") then
            tile.Color = Color3.fromHSV(math.random(), 0.9, 1)
        end
    end
    floor.PartyLight.Color = Color3.fromHSV(math.random(), 0.8, 1)
    wait(0.4)
end
)";

const char* kLandmine = R"(-- Landmine: explodes when the player steps on it.
local mine = script.Parent
local armed = true

mine.Touched:Connect(function(hit)
    if armed and hit.Parent:FindFirstChild("Humanoid") then
        armed = false
        Explode(mine.Position, 8)    -- position, radius
        mine:Destroy()
    end
end)

-- Blink the light so you can (just about) see it.
while mine.Parent do
    mine.Color = Color3.fromRGB(255, 40, 40)
    wait(0.5)
    mine.Color = Color3.fromRGB(60, 10, 10)
    wait(0.5)
end
)";

const char* kSawBlade = R"(-- Saw Blade: spins fast and slices whoever touches it.
local saw = script.Parent

game:GetService("RunService").Heartbeat:Connect(function(dt)
    saw.Orientation = saw.Orientation + Vector3.new(0, 0, 720 * dt)
end)

saw.Touched:Connect(function(hit)
    local character = hit.Parent
    if character:FindFirstChild("Humanoid") then
        Effects.Sparks(saw.Position, 30)
        character:BreakJoints()      -- a very messy death
    end
end)
)";

const char* kSpikeTrap = R"(-- Spike Trap: the spikes shoot up every few seconds.
local trap = script.Parent
local spikes = trap:FindFirstChild("Spikes")
local down = spikes.Position
local up = down + Vector3.new(0, 1, 0)

spikes.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid and spikes.Position.Y > down.Y + 0.5 then
        humanoid:TakeDamage(45)
    end
end)

while true do
    wait(2)
    spikes.Position = up      -- stab!
    wait(0.6)
    spikes.Position = down
end
)";

const char* kTimeBomb = R"(-- Time Bomb: click it to start the timer. 5 seconds later, BOOM (and fires).
local bomb = script.Parent
local armed = false

bomb.Clicked:Connect(function()
    if armed then return end
    armed = true
    for i = 5, 1, -1 do
        bomb.Color = Color3.new(1, 0.2, 0.1)
        Sounds.Play("click", bomb.Position)
        task.wait(0.15)
        bomb.Color = Color3.new(0.15, 0.15, 0.15)
        task.wait(0.85)
    end
    local e = Instance.new("Explosion")
    e.Position = bomb.Position
    e.BlastRadius = 16
    e.BlastPressure = 800000
    e.Fire = 10            -- fires keep burning for 10 seconds
    e.Parent = workspace
    bomb:Destroy()
end)
)";

// --- Gear: Tools you hold. Click (or tap) to use them. ---

const char* kGearSword = R"(-- Classic Sword: click to slash. A hit takes 25 health from other players (and zombies).
local tool = script.Parent
local blade = tool.Blade
local slashing = false
local hitThisSlash = {}

tool.Activated:Connect(function()
    if slashing then return end
    slashing = true
    hitThisSlash = {}
    Sounds.Play("hit", blade.Position)
    task.wait(0.5)
    slashing = false
end)

local function slash(hit)
    if not slashing then return end
    local character = hit.Parent
    local humanoid = character and character:FindFirstChild("Humanoid")
    -- Not yourself, and only once per slash.
    if not humanoid or character == tool.Parent or hitThisSlash[character] then return end
    hitThisSlash[character] = true
    humanoid:TakeDamage(25)
end

blade.Touched:Connect(slash)
tool.Handle.Touched:Connect(slash)
)";

const char* kGearRocket = R"(-- Rocket Launcher: click to fire a rocket the way you're facing. It explodes on whatever it hits.
local tool = script.Parent
local ready = true

tool.Activated:Connect(function()
    local character = tool.Parent
    local root = character and character:FindFirstChild("HumanoidRootPart")
    if not ready or not root then return end
    ready = false
    local forward = root.CFrame.LookVector
    local pos = root.Position + forward * 1.5 + Vector3.new(0, 0.8, 0)

    local rocket = Instance.new("Part")
    rocket.Name = "Rocket"
    rocket.Size = Vector3.new(0.4, 0.4, 0.4)
    rocket.Color = Color3.new(0.55, 0.55, 0.6)
    rocket.Anchored = true
    rocket.CanCollide = false
    rocket.Position = pos
    rocket.Parent = workspace

    -- Fly forward a bit every frame until the rocket hits something (or goes too far).
    local params = RaycastParams.new()
    params.FilterDescendantsInstances = { character, rocket }
    for i = 1, 120 do
        local step = forward * 2
        local hit = workspace:Raycast(pos, step, params)
        if hit then pos = hit.Position break end
        pos = pos + step
        rocket.Position = pos
        task.wait(1 / 30)
    end
    rocket:Destroy()

    local boom = Instance.new("Explosion")
    boom.Position = pos
    boom.BlastRadius = 5
    boom.Parent = workspace

    task.wait(1.5)   -- reloading
    ready = true
end)
)";

const char* kGearSpeedCoil = R"(-- Speed Coil: hold it to run twice as fast.
local tool = script.Parent
local humanoid, oldSpeed

tool.Equipped:Connect(function()
    humanoid = tool.Parent:FindFirstChild("Humanoid")
    if humanoid then
        oldSpeed = humanoid.WalkSpeed
        humanoid.WalkSpeed = oldSpeed * 2
    end
end)

tool.Unequipped:Connect(function()
    if humanoid and oldSpeed then humanoid.WalkSpeed = oldSpeed end
    humanoid = nil
end)
)";

const char* kGearGravityCoil = R"(-- Gravity Coil: hold it to jump three times as high.
local tool = script.Parent
local humanoid, oldJump

tool.Equipped:Connect(function()
    humanoid = tool.Parent:FindFirstChild("Humanoid")
    if humanoid then
        oldJump = humanoid.JumpHeight
        humanoid.JumpHeight = oldJump * 3
    end
end)

tool.Unequipped:Connect(function()
    if humanoid and oldJump then humanoid.JumpHeight = oldJump end
    humanoid = nil
end)
)";

const char* kGearBomb = R"(-- Bomb: click to drop a bomb in front of you. 3 seconds later, BOOM.
local tool = script.Parent
local ready = true

tool.Activated:Connect(function()
    local character = tool.Parent
    local root = character and character:FindFirstChild("HumanoidRootPart")
    if not ready or not root then return end
    ready = false

    local bomb = Instance.new("Part")
    bomb.Name = "Bomb"
    bomb.Shape = "Ball"
    bomb.Size = Vector3.new(0.7, 0.7, 0.7)
    bomb.Color = Color3.new(0.12, 0.12, 0.12)
    bomb.Anchored = false
    bomb.Position = root.Position + root.CFrame.LookVector * 1.5
    bomb.Parent = workspace

    for i = 1, 3 do
        bomb.Color = Color3.new(1, 0.2, 0.1)
        Sounds.Play("click", bomb.Position)
        task.wait(0.15)
        bomb.Color = Color3.new(0.12, 0.12, 0.12)
        task.wait(0.85)
    end
    local boom = Instance.new("Explosion")
    boom.Position = bomb.Position
    boom.BlastRadius = 7
    boom.Parent = workspace
    bomb:Destroy()

    task.wait(2)
    ready = true
end)
)";

const char* kNuke = R"(-- Nuke: click it, run. 10 seconds later: a shockwave, a fireball and a mushroom cloud.
-- Near water it makes a tsunami. Change BlastRadius to make it bigger or smaller.
local nuke = script.Parent
local armed = false

nuke.Clicked:Connect(function()
    if armed then return end
    armed = true
    Gui.Message("NUKE ARMED: 10 seconds. RUN!", 3)
    for i = 10, 1, -1 do
        Gui.Label("Nuke", "Detonation in " .. i)
        Sounds.Play("click", nuke.Position)
        task.wait(1)
    end
    Gui.Label("Nuke", "")
    local e = Instance.new("Explosion")
    e.Position = nuke.Position
    e.BlastRadius = 120
    e.BlastPressure = 4000000
    e.Destroy = true         -- rips anchored parts loose near the middle (a crater)
    e.Fire = 25
    e.MushroomCloud = true
    e.Parent = workspace
    nuke:Destroy()
end)
)";

const char* kDepthCharge = R"(-- Depth Charge: drop it in water (or click it on a boat). Under the surface
-- it blows: a huge column of spray, and in big water, a tsunami.
local charge = script.Parent
local gone = false

local function boom()
    if gone then return end
    gone = true
    Explode(charge.Position, 45, 2)
    charge:Destroy()
end

charge.Clicked:Connect(function()
    charge.Anchored = false
    task.wait(4)   -- time to sink
    boom()
end)
charge.Touched:Connect(function(hit)
    if hit.Name == "Water" and not gone then
        task.wait(2)
        boom()
    end
end)
)";

const char* kBarrel = R"(-- Exploding Barrel: click it, or knock it over, and BOOM.
local barrel = script.Parent
local done = false

local function boom()
    if done then return end
    done = true
    Explode(barrel.Position, 10, 1.5)
    barrel:Destroy()
end

barrel.Clicked:Connect(boom)
barrel.Touched:Connect(function(hit)
    -- Blow up if something slams into it fast.
    if hit.AssemblyLinearVelocity.Magnitude > 12 then boom() end
end)
)";

const char* kCart = R"(-- Motor Cart: hold the Up / Down arrow keys to drive. Hop on first!
local cart = script.Parent
local input = game:GetService("UserInputService")

game:GetService("RunService").Heartbeat:Connect(function()
    local speed = 0
    if input:IsKeyDown(Enum.KeyCode.Up) then speed = 9
    elseif input:IsKeyDown(Enum.KeyCode.Down) then speed = -9 end
    -- Look the motors up every time, so a cart that got blown apart still works.
    for _, thing in ipairs(cart:GetDescendants()) do
        if thing:IsA("HingeConstraint") then
            thing.AngularVelocity = speed
        end
    end
end)
)";

const char* kTrampoline = R"(-- Trampoline: bounces the player (and anything else) up high.
local pad = script.Parent

pad.Touched:Connect(function(hit)
    local character = hit.Parent
    local root = character:FindFirstChild("HumanoidRootPart")
    if root and character:FindFirstChild("Humanoid") then
        root.AssemblyLinearVelocity = Vector3.new(0, 26, 0)
        Sounds.Play("boing", pad.Position)
    elseif not hit.Anchored then
        hit.AssemblyLinearVelocity = hit.AssemblyLinearVelocity + Vector3.new(0, 30, 0)
    end
end)
)";

const char* kZombie = R"(-- Zombie: chases the nearest player and bites them.
-- It uses PathfindingService to find its way around walls.
local zombie = script.Parent
local humanoid = zombie.Humanoid
local root = zombie.HumanoidRootPart
local PathfindingService = game:GetService("PathfindingService")
local Players = game:GetService("Players")

local SIGHT = 60       -- how far away it notices you
local DAMAGE = 15      -- health taken per bite
local BITE_TIME = 1    -- seconds between bites
humanoid.WalkSpeed = 4 -- slow and spooky (players walk at 6)

-- The closest living player's HumanoidRootPart, and how far away it is.
local function nearestPlayer()
    local best, bestDist = nil, SIGHT
    for _, player in ipairs(Players:GetPlayers()) do
        local character = player.Character
        local hrp = character and character:FindFirstChild("HumanoidRootPart")
        local hum = character and character:FindFirstChild("Humanoid")
        if hrp and hum and hum.Health > 0 then
            local distance = (hrp.Position - root.Position).Magnitude
            if distance < bestDist then best, bestDist = hrp, distance end
        end
    end
    return best, bestDist
end

-- When it dies, the body stays for a few seconds, then goes away.
humanoid.Died:Connect(function()
    wait(5)
    zombie:Destroy()
end)

local lastBite = 0
local chasing = nil
while humanoid.Health > 0 do
    local target, distance = nearestPlayer()
    if not target then
        wait(0.5)   -- nobody around: stand and groan
    elseif distance < 3 then
        -- Close enough to bite!
        humanoid:MoveTo(target.Position)
        if time() - lastBite > BITE_TIME then
            lastBite = time()
            target.Parent.Humanoid:TakeDamage(DAMAGE)
        end
        wait(0.1)
    else
        -- Chase them on the navmesh: the engine finds the way around walls, jumps
        -- up ledges and over gaps, and keeps following as the player moves.
        if humanoid.PathfindStatus ~= "Walking" or target ~= chasing then
            chasing = target
            humanoid:PathfindStart(target)
        end
        wait(0.3)   -- then look again (someone else might be closer now)
    end
end
)";

SceneNode* addPart(Scene& scene, const char* name, PrimitiveType shape, glm::vec3 pos,
                   glm::vec3 size, glm::vec3 color, Material mat = Material::Plastic) {
    SceneNode* n = scene.addNode(name, shape, MeshLibrary::get(shape));
    n->transform.position = pos;
    n->transform.scale    = size;
    n->color              = color;
    n->material           = mat;
    return n;
}

void addScript(Scene& scene, SceneNode* parent, const char* source) {
    auto s = std::make_unique<SceneNode>("Script", NodeKind::Script);
    s->source = source;
    scene.insert(std::move(s), parent);
}

// A Tool (gear) with its Handle; more parts go in with toolPart. Positions are inside the tool.
SceneNode* makeTool(Scene& scene, const char* name, const char* tip, glm::vec3 at) {
    auto t = std::make_unique<SceneNode>(name, NodeKind::Tool);
    t->transform.position = at + glm::vec3(0.0f, 1.0f, 0.0f);
    t->toolTip = tip;
    t->starterTool = true;   // everyone starts with it, so Play tests it straight away (untick In StarterPack to leave it lying there)
    return scene.insert(std::move(t), nullptr);
}
SceneNode* toolPart(Scene& scene, SceneNode* tool, const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 size,
                    glm::vec3 color, Material mat = Material::Plastic) {
    auto p = std::make_unique<SceneNode>(name);
    p->primitiveType = shape;
    p->mesh = MeshLibrary::get(shape);
    p->transform.position = pos;
    p->transform.scale = size;
    p->color = color;
    p->material = mat;
    p->canCollide = false;
    return scene.insert(std::move(p), tool);
}

} // namespace

SceneNode* makeConstraint(Scene& scene, ConstraintType type, SceneNode* a, const glm::vec3& pa,
                          SceneNode* b, const glm::vec3& pb, const glm::vec3& axis, bool motor) {
    auto c = std::make_unique<SceneNode>(std::string(kConstraintNames[(int)type]) + (motor ? "Motor" : "Constraint"),
                                         NodeKind::Constraint);
    c->constraintType = type;
    c->color = type == ConstraintType::Rope ? glm::vec3(0.45f, 0.32f, 0.2f) : glm::vec3(0.6f, 0.62f, 0.66f);
    if (motor) { c->motorTorque = 3000.0f; c->motorSpeed = 3.0f; }
    if (type == ConstraintType::Weld) {
        c->ref0 = a->id;
        c->ref1 = b->id;
        c->visible = false;
    } else {
        auto makeAttachment = [&](SceneNode* part, const glm::vec3& worldPoint) {
            auto att = std::make_unique<SceneNode>("Attachment", NodeKind::Attachment);
            glm::mat4 w = part->worldMatrix();
            att->transform.position = glm::vec3(glm::inverse(w) * glm::vec4(worldPoint, 1.0f));
            // The attachment's X axis points along `axis` (the hinge axis).
            glm::vec3 x = glm::normalize(axis);
            glm::vec3 y = std::abs(x.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 z = glm::normalize(glm::cross(x, y));
            y = glm::cross(z, x);
            glm::vec3 sc(glm::length(glm::vec3(w[0])), glm::length(glm::vec3(w[1])), glm::length(glm::vec3(w[2])));
            glm::mat3 parentRot(w);
            for (int i = 0; i < 3; ++i) parentRot[i] /= std::max(sc[i], 1e-6f);
            glm::mat3 localRot = glm::transpose(parentRot) * glm::mat3(x, y, z);
            float rz, ry, rx;
            glm::extractEulerAngleZYX(glm::mat4(localRot), rz, ry, rx);
            att->transform.rotation = glm::degrees(glm::vec3(rx, ry, rz));
            att->transform.scale = glm::vec3(1.0f) / glm::max(sc, glm::vec3(1e-3f));   // not stretched by the part
            return scene.insert(std::move(att), part);
        };
        c->ref0 = makeAttachment(a, pa)->id;
        // (Joints that hold two points together start with both at the first click.)
        const bool samePoint = type == ConstraintType::Hinge || usesJointProps(type);
        c->ref1 = makeAttachment(b, samePoint ? pa : pb)->id;
    }
    return scene.insert(std::move(c), a);
}

const std::vector<PremadeInfo>& premadeList() {
    static const std::vector<PremadeInfo> list = {
        {Premade::SpawnLocation,        "Spawn Location",  "Where the player appears when you press Play"},
        {Premade::Checkpoint,           "Checkpoint",      "Touch it and you respawn here (for obbies)"},
        {Premade::KillBrick,            "Kill Brick",      "Red neon block - touching it knocks the player out"},
        {Premade::Spinner,              "Spinner",         "A bar that spins around forever"},
        {Premade::JumpPad,              "Jump Pad",        "Launches the player into the air"},
        {Premade::Coin,                 "Coin",            "Spins, and counts up when collected"},
        {Premade::DisappearingPlatform, "Fading Platform", "Fades away when you stand on it"},
        {Premade::MovingPlatform,       "Moving Platform", "Slides back and forth, carrying the player"},
        {Premade::SpeedPad,             "Speed Pad",       "Makes the player run faster for 5 seconds"},
        {Premade::ClickButton,          "Click Button",    "Click it with the mouse during Play"},
        {Premade::FallingBall,          "Falling Ball",    "An unanchored ball that drops with gravity"},
        {Premade::DayNightCycle,        "Day/Night Cycle", "A script that makes time pass"},
        {Premade::Ramp,                 "Ramp",            "A slope you can walk (or roll things) up"},
        {Premade::SwingingRope,         "Swinging Rope",   "A ball hanging from a rope - push it!"},
        {Premade::WreckingBall,         "Wrecking Ball",   "A heavy ball on a rod that swings through things"},
        {Premade::Windmill,             "Windmill",        "Spinning blades driven by a motor"},
        {Premade::Seesaw,               "Seesaw",          "A plank on a hinge - jump on one end"},
        {Premade::MotorCart,            "Motor Cart",      "Hop on and drive with the arrow keys"},
        {Premade::DominoRun,            "Domino Run",      "Knock the first one over..."},
        {Premade::CratePyramid,         "Crate Pyramid",   "A stack of loose crates to knock down"},
        {Premade::Trampoline,           "Trampoline",      "Super bouncy - throws players and parts up"},
        {Premade::Zombie,               "Zombie",          "Chases the nearest player and bites (finds its way around walls)"},
        {Premade::Landmine,             "Landmine",        "Explodes when stepped on"},
        {Premade::SawBlade,             "Saw Blade",       "A spinning blade. Touch it and lose limbs"},
        {Premade::SpikeTrap,            "Spike Trap",      "Spikes shoot up every few seconds"},
        {Premade::ExplodingBarrel,      "Exploding Barrel","Click it (or bump it) to blow it up"},
        {Premade::TimeBomb,             "Time Bomb",       "Click it: 5 seconds, then a blast that leaves fires burning"},
        {Premade::Nuke,                 "Nuke",            "Click it and run: shockwave, fireball, mushroom cloud (a tsunami near water)"},
        {Premade::GearSword,            "Classic Sword",   "Gear: click to slash (25 damage a hit)"},
        {Premade::GearRocketLauncher,   "Rocket Launcher", "Gear: fires rockets that explode on impact"},
        {Premade::GearSpeedCoil,        "Speed Coil",      "Gear: hold it to run twice as fast"},
        {Premade::GearGravityCoil,      "Gravity Coil",    "Gear: hold it to jump really high"},
        {Premade::GearBomb,             "Bomb",            "Gear: drop a bomb that goes off after 3 seconds"},
        {Premade::DepthCharge,          "Depth Charge",    "Drop it in water: a column of spray and, in big water, a tsunami"},
        {Premade::WaterWheel,           "Water Wheel",     "A wheel in a flowing river: the current turns it (change the river's Flow)"},
        {Premade::LampPost,             "Lamp Post",       "A street lamp with a real light (try it at night)"},
        {Premade::DiscoFloor,           "Disco Floor",     "Tiles and a light that change colour"},
    };
    return list;
}

SceneNode* buildPremade(Scene& scene, Premade kind, const glm::vec3& at) {
    const auto Cube = PrimitiveType::Cube;
    SceneNode* n = nullptr;
    switch (kind) {
        case Premade::SpawnLocation:
            n = addPart(scene, "SpawnLocation", Cube, at + glm::vec3(0, 0.1f, 0), {3, 0.2f, 3}, {0.55f, 0.57f, 0.60f});
            break;
        case Premade::KillBrick:
            n = addPart(scene, "KillBrick", Cube, at + glm::vec3(0, 0.25f, 0), {3, 0.5f, 3}, {1.0f, 0.15f, 0.1f}, Material::Neon);
            addScript(scene, n, kKillBrick);
            break;
        case Premade::Spinner:
            n = addPart(scene, "Spinner", Cube, at + glm::vec3(0, 0.6f, 0), {8, 0.4f, 0.4f}, {0.95f, 0.55f, 0.1f});
            addScript(scene, n, kSpinner);
            break;
        case Premade::JumpPad:
            n = addPart(scene, "JumpPad", PrimitiveType::Cylinder, at + glm::vec3(0, 0.1f, 0), {2.5f, 0.2f, 2.5f}, {0.2f, 0.9f, 0.3f}, Material::Neon);
            addScript(scene, n, kJumpPad);
            break;
        case Premade::Coin:
            n = addPart(scene, "Coin", PrimitiveType::Cylinder, at + glm::vec3(0, 1.2f, 0), {0.9f, 0.12f, 0.9f}, {1.0f, 0.8f, 0.15f}, Material::Metal);
            n->transform.rotation = {90, 0, 0};
            n->canCollide = false;
            addScript(scene, n, kCoin);
            break;
        case Premade::DisappearingPlatform:
            n = addPart(scene, "FadingPlatform", Cube, at + glm::vec3(0, 2.0f, 0), {4, 0.4f, 4}, {0.3f, 0.75f, 0.95f});
            addScript(scene, n, kDisappearing);
            break;
        case Premade::MovingPlatform:
            n = addPart(scene, "MovingPlatform", Cube, at + glm::vec3(0, 1.0f, 0), {4, 0.4f, 4}, {0.6f, 0.45f, 0.95f});
            addScript(scene, n, kMoving);
            break;
        case Premade::SpeedPad:
            n = addPart(scene, "SpeedPad", Cube, at + glm::vec3(0, 0.05f, 0), {3, 0.1f, 3}, {0.1f, 0.6f, 1.0f}, Material::Neon);
            addScript(scene, n, kSpeedPad);
            break;
        case Premade::ClickButton:
            n = addPart(scene, "ClickButton", Cube, at + glm::vec3(0, 0.5f, 0), {1.5f, 1, 1.5f}, {0.9f, 0.2f, 0.5f});
            addScript(scene, n, kClickButton);
            break;
        case Premade::FallingBall:
            n = addPart(scene, "Ball", PrimitiveType::Sphere, at + glm::vec3(0, 6.0f, 0), {1.5f, 1.5f, 1.5f}, {0.95f, 0.95f, 0.95f});
            n->anchored = false;
            break;
        case Premade::Ramp:
            n = addPart(scene, "Ramp", PrimitiveType::Cube, at + glm::vec3(0, 1.5f, 0), {4, 0.5f, 9}, {0.8f, 0.55f, 0.3f}, Material::Wood);
            n->transform.rotation = {20, 0, 0};
            break;
        case Premade::SwingingRope: {
            n = scene.insert(std::make_unique<SceneNode>("SwingingRope", NodeKind::Model));
            SceneNode* beam = addPart(scene, "Beam", PrimitiveType::Cube, at + glm::vec3(0, 8, 0), {1, 1, 1}, {0.3f, 0.3f, 0.32f}, Material::Metal);
            SceneNode* ball = addPart(scene, "Ball", PrimitiveType::Sphere, at + glm::vec3(3, 5.5f, 0), {1.4f, 1.4f, 1.4f}, {1, 0.8f, 0.1f});
            ball->anchored = false;
            scene.reparent(beam, n); scene.reparent(ball, n);
            makeConstraint(scene, ConstraintType::Rope, beam, at + glm::vec3(0, 7.5f, 0), ball, ball->transform.position, {1, 0, 0}, false);
            break;
        }
        case Premade::WreckingBall: {
            n = scene.insert(std::make_unique<SceneNode>("WreckingBall", NodeKind::Model));
            glm::vec3 metal = {0.35f, 0.35f, 0.38f};
            SceneNode* l = addPart(scene, "PostL", PrimitiveType::Cube, at + glm::vec3(-4, 6, 0), {0.8f, 12, 0.8f}, {0.8f, 0.6f, 0.1f}, Material::Metal);
            SceneNode* r = addPart(scene, "PostR", PrimitiveType::Cube, at + glm::vec3(4, 6, 0), {0.8f, 12, 0.8f}, {0.8f, 0.6f, 0.1f}, Material::Metal);
            SceneNode* top = addPart(scene, "TopBeam", PrimitiveType::Cube, at + glm::vec3(0, 12.4f, 0), {9, 0.8f, 0.8f}, {0.8f, 0.6f, 0.1f}, Material::Metal);
            SceneNode* ball = addPart(scene, "Ball", PrimitiveType::Sphere, at + glm::vec3(0, 12.0f - 7.0f * 0.5f, 7.0f * 0.866f), {2.6f, 2.6f, 2.6f}, metal, Material::Metal);
            ball->anchored = false;
            ball->density = 6.0f;
            for (SceneNode* p : {l, r, top, ball}) scene.reparent(p, n);
            SceneNode* rod = makeConstraint(scene, ConstraintType::Rod, top, at + glm::vec3(0, 12.0f, 0), ball, ball->transform.position, {1, 0, 0}, false);
            rod->thickness = 0.15f;
            break;
        }
        case Premade::Windmill: {
            n = scene.insert(std::make_unique<SceneNode>("Windmill", NodeKind::Model));
            SceneNode* post = addPart(scene, "Post", PrimitiveType::Cube, at + glm::vec3(0, 4, 0), {1, 8, 1}, {0.55f, 0.4f, 0.25f}, Material::Wood);
            glm::vec3 hub = at + glm::vec3(0, 7.5f, 0.9f);
            SceneNode* b1 = addPart(scene, "Blade1", PrimitiveType::Cube, hub, {8, 0.8f, 0.2f}, {0.92f, 0.92f, 0.95f}, Material::Wood);
            SceneNode* b2 = addPart(scene, "Blade2", PrimitiveType::Cube, hub, {0.8f, 8, 0.2f}, {0.92f, 0.92f, 0.95f}, Material::Wood);
            b1->anchored = b2->anchored = false;
            for (SceneNode* p : {post, b1, b2}) scene.reparent(p, n);
            makeConstraint(scene, ConstraintType::Weld, b1, hub, b2, hub, {1, 0, 0}, false);
            SceneNode* m = makeConstraint(scene, ConstraintType::Hinge, post, hub, b1, hub, {0, 0, 1}, true);
            m->motorSpeed = 1.2f;
            break;
        }
        case Premade::WaterWheel: {
            // A river with a current (its "Flow"), and a wheel of paddles on a hinge. Nothing
            // drives it: the water pushes on the paddles that dip into it.
            n = scene.insert(std::make_unique<SceneNode>("WaterWheel", NodeKind::Model));
            SceneNode* bed = addPart(scene, "RiverBed", Cube, at + glm::vec3(0, 0.25f, 0), {10, 0.5f, 18}, {0.45f, 0.38f, 0.28f}, Material::Concrete);
            SceneNode* river = addPart(scene, "Water", Cube, at + glm::vec3(0, 1.5f, 0), {10, 2.0f, 18}, {0.13f, 0.45f, 0.62f}, Material::Glass);
            river->transparency = 0.45f;
            river->canCollide = false;
            river->castShadow = false;
            river->tags = {"Water"};
            Attribute flow; flow.name = "Flow"; flow.type = Attribute::Vector3; flow.v = glm::vec3(0, 0, 8); river->attributes.push_back(flow);
            Attribute drag; drag.name = "Drag"; drag.type = Attribute::Number; drag.n = 1.0; river->attributes.push_back(drag);
            const glm::vec3 hub = at + glm::vec3(0, 4.6f, 0);
            SceneNode* postL = addPart(scene, "PostLeft", Cube, at + glm::vec3(-1.6f, 3.0f, 0), {0.6f, 5.0f, 0.6f}, {0.4f, 0.28f, 0.18f}, Material::Wood);
            SceneNode* postR = addPart(scene, "PostRight", Cube, at + glm::vec3(1.6f, 3.0f, 0), {0.6f, 5.0f, 0.6f}, {0.4f, 0.28f, 0.18f}, Material::Wood);
            SceneNode* axle = addPart(scene, "Axle", PrimitiveType::Cylinder, hub, {1.0f, 2.6f, 1.0f}, {0.3f, 0.3f, 0.32f}, Material::Metal);
            axle->transform.rotation = {0, 0, 90};   // lying along x
            axle->anchored = false;
            for (SceneNode* p : {bed, river, postL, postR, axle}) scene.reparent(p, n);
            const int paddles = 8;
            for (int i = 0; i < paddles; ++i) {
                const float a = 6.2831853f * i / paddles;
                const glm::vec3 dir(0.0f, std::cos(a), std::sin(a));
                SceneNode* pad = addPart(scene, "Paddle", Cube, hub + dir * 2.3f, {2.2f, 2.6f, 0.25f}, {0.62f, 0.45f, 0.26f}, Material::Wood);
                pad->transform.rotation = {glm::degrees(a), 0, 0};   // standing out from the axle like a spoke
                pad->anchored = false;
                scene.reparent(pad, n);
                makeConstraint(scene, ConstraintType::Weld, axle, hub, pad, hub, {1, 0, 0}, false);
            }
            makeConstraint(scene, ConstraintType::Hinge, postL, hub, axle, hub, {1, 0, 0}, false);
            break;
        }
        case Premade::Seesaw: {
            n = scene.insert(std::make_unique<SceneNode>("Seesaw", NodeKind::Model));
            SceneNode* base = addPart(scene, "Base", PrimitiveType::Cube, at + glm::vec3(0, 0.5f, 0), {0.8f, 1, 1.4f}, {0.4f, 0.4f, 0.45f}, Material::Metal);
            SceneNode* plank = addPart(scene, "Plank", PrimitiveType::Cube, at + glm::vec3(0, 1.2f, 0), {9, 0.3f, 1.6f}, {0.75f, 0.5f, 0.3f}, Material::Wood);
            plank->anchored = false;
            scene.reparent(base, n); scene.reparent(plank, n);
            makeConstraint(scene, ConstraintType::Hinge, base, at + glm::vec3(0, 1.0f, 0), plank, at + glm::vec3(0, 1.0f, 0), {0, 0, 1}, false);
            break;
        }
        case Premade::MotorCart: {
            n = scene.insert(std::make_unique<SceneNode>("MotorCart", NodeKind::Model));
            SceneNode* body = addPart(scene, "Body", PrimitiveType::Cube, at + glm::vec3(0, 1.35f, 0), {3.2f, 0.6f, 5}, {0.8f, 0.2f, 0.15f});
            body->anchored = false;
            scene.reparent(body, n);
            for (int i = 0; i < 4; ++i) {
                glm::vec3 wp = at + glm::vec3(i % 2 ? 2.05f : -2.05f, 0.7f, i / 2 ? 1.7f : -1.7f);
                SceneNode* wheel = addPart(scene, "Wheel", PrimitiveType::Sphere, wp, {1.4f, 1.4f, 1.4f}, {0.1f, 0.1f, 0.12f});
                wheel->anchored = false;
                wheel->friction = 1.2f;
                scene.reparent(wheel, n);   // beside the body, not inside it, so it can spin
                SceneNode* motor = makeConstraint(scene, ConstraintType::Hinge, body, wp, wheel, wp, {1, 0, 0}, true);
                motor->motorSpeed = 0.0f;
                motor->visible = false;
            }
            addScript(scene, n, kCart);
            break;
        }
        case Premade::DominoRun: {
            n = scene.insert(std::make_unique<SceneNode>("DominoRun", NodeKind::Model));
            for (int i = 0; i < 14; ++i) {
                float ang = i * 0.12f;
                glm::vec3 p = at + glm::vec3(std::sin(ang) * 10.0f, 1.2f, -std::cos(ang) * 10.0f + 10.0f);
                SceneNode* d = addPart(scene, "Domino", PrimitiveType::Cube, p, {1.2f, 2.4f, 0.3f},
                                       glm::vec3(0.2f + 0.05f * (i % 4), 0.3f, 0.9f - 0.05f * (i % 3)));
                d->anchored = false;
                d->transform.rotation = {i == 0 ? -14.0f : 0.0f, -glm::degrees(ang), 0.0f};
                scene.reparent(d, n);
            }
            break;
        }
        case Premade::CratePyramid: {
            n = scene.insert(std::make_unique<SceneNode>("CratePyramid", NodeKind::Model));
            for (int row = 0; row < 4; ++row)
                for (int i = 0; i < 4 - row; ++i) {
                    glm::vec3 p = at + glm::vec3((i - (3 - row) * 0.5f) * 1.25f, 0.6f + row * 1.2f, 0);
                    SceneNode* c = addPart(scene, "Crate", PrimitiveType::Cube, p, {1.2f, 1.2f, 1.2f}, {0.62f, 0.42f, 0.22f}, Material::Wood);
                    c->anchored = false;
                    scene.reparent(c, n);
                }
            break;
        }
        case Premade::Checkpoint:
            // Built in: touching a part called "Checkpoint" makes it your respawn point. No script needed.
            n = addPart(scene, "Checkpoint", Cube, at + glm::vec3(0, 0.1f, 0), {3, 0.2f, 3}, {0.2f, 0.85f, 0.4f}, Material::Neon);
            break;
        case Premade::Zombie: {
            n = Player::buildRig(scene, "Zombie", at);
            const glm::vec3 skin(0.45f, 0.62f, 0.32f), shirt(0.36f, 0.27f, 0.2f), pants(0.18f, 0.22f, 0.4f);
            Player::applyColors(n, {skin, shirt, skin, skin, pants, pants});
            n->tags.push_back("Zombie");
            addScript(scene, n, kZombie);
            break;
        }
        case Premade::Trampoline:
            n = addPart(scene, "Trampoline", PrimitiveType::Cylinder, at + glm::vec3(0, 0.25f, 0), {4, 0.5f, 4}, {0.2f, 0.5f, 1.0f});
            n->elasticity = 1.0f;
            addScript(scene, n, kTrampoline);
            break;
        case Premade::Landmine:
            n = addPart(scene, "Landmine", PrimitiveType::Cylinder, at + glm::vec3(0, 0.05f, 0), {0.9f, 0.1f, 0.9f}, {0.3f, 0.05f, 0.05f}, Material::Neon);
            n->canCollide = false;
            addScript(scene, n, kLandmine);
            break;
        case Premade::SawBlade:
            n = addPart(scene, "SawBlade", PrimitiveType::Cylinder, at + glm::vec3(0, 1.6f, 0), {3.0f, 0.12f, 3.0f}, {0.75f, 0.77f, 0.8f}, Material::Metal);
            n->transform.rotation = {90, 0, 0};
            addScript(scene, n, kSawBlade);
            break;
        case Premade::SpikeTrap: {
            n = scene.insert(std::make_unique<SceneNode>("SpikeTrap", NodeKind::Model));
            SceneNode* base = addPart(scene, "Base", PrimitiveType::Cube, at + glm::vec3(0, 0.05f, 0), {3, 0.1f, 3}, {0.2f, 0.2f, 0.22f}, Material::Metal);
            scene.reparent(base, n);
            SceneNode* spikes = addPart(scene, "Spikes", PrimitiveType::Cube, at + glm::vec3(0, -0.45f, 0), {2.6f, 1.0f, 2.6f}, {0.7f, 0.7f, 0.75f}, Material::Metal);
            scene.reparent(spikes, n);
            addScript(scene, n, kSpikeTrap);
            break;
        }
        case Premade::TimeBomb:
            n = addPart(scene, "TimeBomb", PrimitiveType::Sphere, at + glm::vec3(0, 0.75f, 0), {1.5f, 1.5f, 1.5f}, {0.15f, 0.15f, 0.15f}, Material::Metal);
            n->anchored = false;
            addScript(scene, n, kTimeBomb);
            break;
        case Premade::Nuke:
            n = addPart(scene, "Nuke", PrimitiveType::Cylinder, at + glm::vec3(0, 1.5f, 0), {1.6f, 3.0f, 1.6f}, {0.75f, 0.7f, 0.2f}, Material::Metal);
            addScript(scene, n, kNuke);
            break;
        case Premade::DepthCharge:
            n = addPart(scene, "DepthCharge", PrimitiveType::Cylinder, at + glm::vec3(0, 0.6f, 0), {1.0f, 1.2f, 1.0f}, {0.2f, 0.25f, 0.3f}, Material::Metal);
            n->anchored = false;
            n->density = 3.0f;   // sinks
            addScript(scene, n, kDepthCharge);
            break;
        // Gear: a Tool with a Handle (what the hand holds; its long side points forward) and a script.
        case Premade::GearSword: {
            n = makeTool(scene, "Sword", "Click to slash!", at);
            toolPart(scene, n, "Handle", Cube, {0, 0, 0}, {0.12f, 0.55f, 0.12f}, {0.25f, 0.17f, 0.1f}, Material::Wood);
            toolPart(scene, n, "Guard", Cube, {0, 0.32f, 0}, {0.5f, 0.08f, 0.14f}, {0.75f, 0.6f, 0.15f}, Material::Metal);
            toolPart(scene, n, "Blade", Cube, {0, 1.16f, 0}, {0.06f, 1.6f, 0.22f}, {0.8f, 0.82f, 0.86f}, Material::Metal);
            n->gripPos = {0, -0.15f, 0};
            addScript(scene, n, kGearSword);
            break;
        }
        case Premade::GearRocketLauncher: {
            n = makeTool(scene, "Rocket", "Click to fire a rocket", at);
            toolPart(scene, n, "Handle", Cube, {0, 0, 0}, {0.14f, 0.5f, 0.16f}, {0.2f, 0.2f, 0.22f}, Material::Metal);
            toolPart(scene, n, "Tube", PrimitiveType::Cylinder, {0, 0.45f, 0.22f}, {0.34f, 1.8f, 0.34f}, {0.25f, 0.4f, 0.2f}, Material::Metal);
            addScript(scene, n, kGearRocket);
            break;
        }
        case Premade::GearSpeedCoil:
            n = makeTool(scene, "SpeedCoil", "Hold it to run faster", at);
            toolPart(scene, n, "Handle", PrimitiveType::Cylinder, {0, 0, 0}, {0.26f, 0.6f, 0.26f}, {0.1f, 0.45f, 1.0f}, Material::Neon);
            addScript(scene, n, kGearSpeedCoil);
            break;
        case Premade::GearGravityCoil:
            n = makeTool(scene, "GravityCoil", "Hold it to jump higher", at);
            toolPart(scene, n, "Handle", PrimitiveType::Cylinder, {0, 0, 0}, {0.26f, 0.6f, 0.26f}, {0.6f, 0.15f, 0.9f}, Material::Neon);
            addScript(scene, n, kGearGravityCoil);
            break;
        case Premade::GearBomb:
            n = makeTool(scene, "Bomb", "Click to drop a bomb", at);
            toolPart(scene, n, "Handle", PrimitiveType::Sphere, {0, 0, 0}, {0.5f, 0.5f, 0.5f}, {0.12f, 0.12f, 0.12f}, Material::Metal);
            toolPart(scene, n, "Fuse", PrimitiveType::Cylinder, {0, 0.32f, 0}, {0.05f, 0.18f, 0.05f}, {0.9f, 0.75f, 0.4f});
            addScript(scene, n, kGearBomb);
            break;
        case Premade::ExplodingBarrel:
            n = addPart(scene, "ExplodingBarrel", PrimitiveType::Cylinder, at + glm::vec3(0, 0.75f, 0), {1.0f, 1.5f, 1.0f}, {0.8f, 0.12f, 0.08f}, Material::Metal);
            n->anchored = false;
            addScript(scene, n, kBarrel);
            break;
        case Premade::LampPost: {
            n = addPart(scene, "LampPost", PrimitiveType::Cylinder, at + glm::vec3(0, 2.0f, 0), {0.25f, 4.0f, 0.25f}, {0.15f, 0.15f, 0.17f}, Material::Metal);
            auto bulb = std::make_unique<SceneNode>("Bulb");
            bulb->primitiveType = PrimitiveType::Sphere;
            bulb->mesh = MeshLibrary::get(PrimitiveType::Sphere);
            bulb->transform.position = {0, 0.56f, 0};       // in the pole's (scaled) space
            bulb->transform.scale    = {2.4f, 0.15f, 2.4f};
            bulb->color = {1.0f, 0.85f, 0.55f};
            bulb->material = Material::Neon;
            bulb->castShadow = false;
            SceneNode* b = scene.insert(std::move(bulb), n);
            auto light = std::make_unique<SceneNode>("PointLight", NodeKind::Light);
            light->color = {1.0f, 0.8f, 0.55f};
            light->brightness = 3.0f;
            light->range = 16.0f;
            light->transform.position = {0, -1.0f, 0};
            scene.insert(std::move(light), b);
            break;
        }
        case Premade::DiscoFloor: {
            auto model = std::make_unique<SceneNode>("DiscoFloor", NodeKind::Model);
            n = scene.insert(std::move(model));
            for (int x = 0; x < 4; ++x)
                for (int z = 0; z < 4; ++z) {
                    auto t = std::make_unique<SceneNode>("Tile");
                    t->primitiveType = PrimitiveType::Cube;
                    t->mesh = MeshLibrary::get(PrimitiveType::Cube);
                    t->transform.position = at + glm::vec3(x * 2.0f - 3.0f, 0.05f, z * 2.0f - 3.0f);
                    t->transform.scale = {1.9f, 0.1f, 1.9f};
                    t->material = Material::Neon;
                    t->color = {0.5f, 0.2f, 0.9f};
                    scene.insert(std::move(t), n);
                }
            auto light = std::make_unique<SceneNode>("PartyLight", NodeKind::Light);
            light->transform.position = at + glm::vec3(0, 5, 0);
            light->brightness = 4.0f;
            light->range = 20.0f;
            light->color = {1, 0.3f, 0.8f};
            scene.insert(std::move(light), n);
            addScript(scene, n, kDisco);
            break;
        }
        case Premade::DayNightCycle: {
            auto s = std::make_unique<SceneNode>("DayNightCycle", NodeKind::Script);
            s->source = kDayNight;
            n = scene.insert(std::move(s));
            break;
        }
    }
    return n;
}

const std::vector<Snippet>& snippetList() {
    static const std::vector<Snippet> list = {
        {"Print a message",
         "print(\"Hello!\")\n"},
        {"Wait, then do something",
         "wait(2)   -- seconds\nprint(\"2 seconds later...\")\n"},
        {"Loop forever",
         "while true do\n    -- do something here\n    wait(1)   -- always wait inside a loop!\nend\n"},
        {"Repeat 10 times",
         "for i = 1, 10 do\n    print(\"Count:\", i)\nend\n"},
        {"If / else",
         "local number = math.random(1, 10)\nif number > 5 then\n    print(\"Big number:\", number)\nelse\n    print(\"Small number:\", number)\nend\n"},
        {"Make a function",
         "local function sayHi(name)\n    print(\"Hi, \" .. name .. \"!\")\nend\n\nsayHi(\"Builder\")\n"},
        {"When this part is touched",
         "script.Parent.Touched:Connect(function(hit)\n    local humanoid = hit.Parent:FindFirstChild(\"Humanoid\")\n    if humanoid then\n        print(\"The player touched me!\")\n    end\nend)\n"},
        {"When this part is clicked",
         "script.Parent.Clicked:Connect(function()\n    print(\"Clicked!\")\nend)\n"},
        {"Every frame",
         "game:GetService(\"RunService\").Heartbeat:Connect(function(dt)\n    -- runs about 60 times a second\nend)\n"},
        {"When a key is pressed",
         "game:GetService(\"UserInputService\").InputBegan:Connect(function(input)\n    if input.KeyCode == Enum.KeyCode.E then\n        print(\"You pressed E\")\n    end\nend)\n"},
        {"Make a new part",
         "local part = Instance.new(\"Part\")\npart.Size = Vector3.new(2, 2, 2)\npart.Position = Vector3.new(0, 10, 0)\npart.Color = Color3.fromRGB(255, 100, 0)\npart.Parent = workspace   -- it falls, because new parts aren't anchored\n"},
        {"Change colour",
         "script.Parent.Color = Color3.fromRGB(0, 170, 255)\n"},
        {"Move a part",
         "local part = script.Parent\npart.Position = part.Position + Vector3.new(0, 5, 0)   -- 5 up\n"},
        {"Play a sound",
         "Sounds.Play(\"coin\")                          -- quick sound effect\n"
         "-- Built-in: jump, coin, oof, explosion, splat, click, hit, win, boing, spawn\n"
         "local music = Instance.new(\"Sound\", workspace)\n"
         "music.SoundId = \"win\"   -- or a file in the games folder, like \"music/theme.mp3\"\n"
         "music.Volume = 0.5\n"
         "music:Play()\n"},
        {"Show text on screen",
         "Gui.Label(\"Score\", \"Score: 0\")          -- a line in the corner\nGui.Message(\"Welcome to my game!\", 3)   -- big text for 3 seconds\n"},
        {"Player health and speed",
         "local character = game.Players.LocalPlayer.Character\nlocal humanoid = character:FindFirstChild(\"Humanoid\")\nhumanoid.WalkSpeed = 12\nhumanoid.JumpPower = 12\n"},
        {"When the player dies",
         "local humanoid = game.Players.LocalPlayer.Character.Humanoid\nhumanoid.Died:Connect(function()\n    print(\"Oof!\")\nend)\n"},
    };
    return list;
}
