#include "GameTemplates.h"
#include "LevelGen.h"
#include "Premades.h"
#include "../renderer/MeshLibrary.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <cmath>
#include <memory>
#include <string>

namespace GameTemplates {
namespace {

constexpr PrimitiveType Cube = PrimitiveType::Cube;

SceneNode* part(Scene& scene, SceneNode* parent, const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 size,
                glm::vec3 color, Material mat = Material::Plastic) {
    SceneNode* n = scene.addNode(name, shape, MeshLibrary::get(shape));
    n->transform.position = pos;
    n->transform.scale = size;
    n->color = color;
    n->material = mat;
    if (parent) scene.reparent(n, parent);
    return n;
}
SceneNode* model(Scene& scene, const char* name, SceneNode* parent = nullptr) {
    return scene.insert(std::make_unique<SceneNode>(name, NodeKind::Model), parent);
}
void script(Scene& scene, SceneNode* parent, const char* name, const char* source) {
    auto s = std::make_unique<SceneNode>(name, NodeKind::Script);
    s->source = source;
    scene.insert(std::move(s), parent);
}
void clearStart(Scene& scene) {   // the default scene's sample cube and welcome script go
    for (const char* name : {"Cube", "WelcomeScript"})
        if (SceneNode* n = scene.root()->findChild(name)) scene.removeNode(n);
}
void title(Scene& scene, const char* t, const char* d) {
    scene.info().title = t;
    scene.info().description = d;
}

// Everyone gets a leaderstats folder with these numbers (0 to start).
std::string leaderstatsScript(const char* stats) {
    return std::string(R"(-- Gives every player the numbers on the leaderboard (top right while playing).
local Players = game:GetService("Players")
local STATS = { )") + stats + R"( }

local function setUp(player)
    if player:FindFirstChild("leaderstats") then return end
    local folder = Instance.new("Folder")
    folder.Name = "leaderstats"
    for _, name in ipairs(STATS) do
        local value = Instance.new("IntValue")
        value.Name = name
        value.Value = 0
        value.Parent = folder
    end
    folder.Parent = player
end

Players.PlayerAdded:Connect(setUp)
for _, player in ipairs(Players:GetPlayers()) do setUp(player) end
)";
}

// ------------------------------------------------------------------ Obby
void obby(Scene& scene) {
    LevelGen::Settings s;
    s.style = LevelGen::Style::Obby;
    s.count = 14;
    s.coins = true;
    LevelGen::Level level;
    for (uint32_t seed = 7; seed < 40; ++seed) {   // the first one that passes its own checks
        s.seed = seed;
        level = LevelGen::make(s);
        if (LevelGen::check(level)) break;
    }
    // The course starts just past the spawn.
    const glm::vec3 at = glm::vec3(0.0f, 0.0f, -6.0f) - level.spawn;
    LevelGen::build(scene, level, at, "Obby");
    if (SceneNode* spawn = scene.root()->findChild("SpawnLocation")) {
        spawn->transform.position = at + level.spawn + glm::vec3(0.0f, 0.1f, 4.0f);
        spawn->transform.scale = {6.0f, 0.4f, 6.0f};
    }
    script(scene, nullptr, "ObbyStages", leaderstatsScript(R"("Stage")").c_str());
    script(scene, nullptr, "StageCounter", R"(-- Every Checkpoint you touch moves your Stage up on the leaderboard.
local Players = game:GetService("Players")
local stage = 0
for _, d in ipairs(workspace:GetDescendants()) do
    if d.Name == "Checkpoint" then
        stage = stage + 1
        local number = stage
        d.Touched:Connect(function(hit)
            local player = Players:GetPlayerFromCharacter(hit.Parent)
            local stats = player and player:FindFirstChild("leaderstats")
            if stats and stats.Stage.Value < number then stats.Stage.Value = number end
        end)
    end
end
)");
    title(scene, "My Obby", "Jump from platform to platform. Checkpoints save your spot, and red bricks send you back!");
}

// ------------------------------------------------------------------ Tycoon
const char* kTycoon = R"(-- Tycoon: touch the green gate to claim it. The droppers drop ore onto the belt;
-- the collector at the end turns ore into Cash. Step on the buttons to buy more droppers.
local Players = game:GetService("Players")
local tycoon = script.Parent
local owner = nil

local function cashOf(player)
    local stats = player and player:FindFirstChild("leaderstats")
    return stats and stats:FindFirstChild("Cash")
end

tycoon.ClaimGate.Touched:Connect(function(hit)
    if owner then return end
    local player = Players:GetPlayerFromCharacter(hit.Parent)
    if not player then return end
    owner = player
    tycoon.ClaimGate.Transparency = 0.8
    tycoon.ClaimGate.CanCollide = false
    tycoon.Owner.Value = player.Name
    print(player.Name .. " owns the tycoon now!")
end)

-- Ore rolls down the belt (the belt pushes it along) into the collector.
tycoon.Collector.Touched:Connect(function(hit)
    local value = hit:GetAttribute("OreValue")
    if not value then return end
    hit:Destroy()
    local cash = cashOf(owner)
    if cash then cash.Value = cash.Value + value end
end)

local function drop(dropper)
    local ore = Instance.new("Part")
    ore.Name = "Ore"
    ore.Size = Vector3.new(0.7, 0.7, 0.7)
    ore.Position = dropper.Position - Vector3.new(0, 1.2, 0)
    ore.Color = dropper:GetAttribute("OreColor") or Color3.new(0.6, 0.6, 0.6)
    ore.Anchored = false
    ore:SetAttribute("OreValue", dropper:GetAttribute("OreValue") or 1)
    ore.Parent = workspace
    game:GetService("Debris"):AddItem(ore, 30)
end

-- Every dropper that's been bought drops one ore every 2 seconds (while someone owns the tycoon).
task.spawn(function()
    while true do
        task.wait(2)
        if owner then
            for _, d in ipairs(tycoon.Droppers:GetChildren()) do
                if d:GetAttribute("Bought") then drop(d) end
            end
        end
    end
end)

-- Buy buttons: step on one with enough Cash to switch on its dropper.
for _, button in ipairs(tycoon.Buttons:GetChildren()) do
    button.Touched:Connect(function(hit)
        local player = Players:GetPlayerFromCharacter(hit.Parent)
        if not player or player ~= owner or button:GetAttribute("Sold") then return end
        local cash, price = cashOf(player), button:GetAttribute("Price") or 0
        if not cash or cash.Value < price then return end
        cash.Value = cash.Value - price
        button:SetAttribute("Sold", true)
        button.Transparency = 1
        button.CanCollide = false
        local dropper = tycoon.Droppers:FindFirstChild(button:GetAttribute("Dropper"))
        if dropper then
            dropper:SetAttribute("Bought", true)
            dropper.Transparency = 0
        end
    end)
end
)";

void attr(SceneNode* n, const char* name, double v) { Attribute a; a.name = name; a.type = Attribute::Number; a.n = v; n->attributes.push_back(a); }
void attr(SceneNode* n, const char* name, bool v) { Attribute a; a.name = name; a.type = Attribute::Bool; a.b = v; n->attributes.push_back(a); }
void attr(SceneNode* n, const char* name, const char* v) { Attribute a; a.name = name; a.type = Attribute::String; a.s = v; n->attributes.push_back(a); }
void attrColor(SceneNode* n, const char* name, glm::vec3 c) { Attribute a; a.name = name; a.type = Attribute::Color3; a.v = c; n->attributes.push_back(a); }

void tycoon(Scene& scene) {
    SceneNode* t = model(scene, "Tycoon");
    const glm::vec3 o(0.0f, 0.0f, -14.0f);
    part(scene, t, "Plot", Cube, o + glm::vec3(0, 0.1f, 0), {24, 0.2f, 20}, {0.55f, 0.55f, 0.6f}, Material::Concrete);
    SceneNode* gate = part(scene, t, "ClaimGate", Cube, o + glm::vec3(0, 2.0f, 10.0f), {6, 4, 0.4f}, {0.2f, 0.85f, 0.3f}, Material::Neon);
    gate->transparency = 0.3f;
    // Who owns it (a StringValue, so other scripts and signs can read it).
    {
        auto v = std::make_unique<SceneNode>("Owner", NodeKind::Value);
        v->value.type = Attribute::String;
        scene.insert(std::move(v), t);
    }
    // The belt: a long strip that pushes what lands on it towards the collector.
    part(scene, t, "Conveyor", Cube, o + glm::vec3(0, 0.6f, -2.0f), {16, 0.6f, 3}, {0.15f, 0.15f, 0.17f}, Material::Metal);
    // (an invisible wind zone just above the belt does the pushing: see Premades' Wind Zone)
    SceneNode* push = part(scene, t, "BeltPush", Cube, o + glm::vec3(0, 1.6f, -2.0f), {16, 1.4f, 3}, {0.3f, 0.6f, 1.0f});
    push->transparency = 1.0f;
    push->canCollide = false;
    push->tags.push_back("ForceZone");
    attr(push, "Mode", "Wind");
    push->attributes.push_back([] { Attribute a; a.name = "Direction"; a.type = Attribute::Vector3; a.v = {1, 0, 0}; return a; }());
    attr(push, "Strength", 130.0);   // (strong enough to beat the belt's friction under Roblox-strength gravity)
    part(scene, t, "BeltRailBack", Cube, o + glm::vec3(0, 1.1f, -3.6f), {16, 0.6f, 0.2f}, {0.85f, 0.7f, 0.1f}, Material::Metal);
    part(scene, t, "BeltRailFront", Cube, o + glm::vec3(0, 1.1f, -0.4f), {16, 0.6f, 0.2f}, {0.85f, 0.7f, 0.1f}, Material::Metal);
    SceneNode* collector = part(scene, t, "Collector", Cube, o + glm::vec3(9.0f, 0.9f, -2.0f), {2, 1.2f, 3.4f}, {0.1f, 0.6f, 1.0f}, Material::Neon);
    (void)collector;
    // Droppers above the belt: the first one is free, the others are bought with Cash.
    SceneNode* droppers = model(scene, "Droppers", t);
    SceneNode* buttons = model(scene, "Buttons", t);
    struct D { const char* name; float x; double value; double price; glm::vec3 color; };
    const D ds[] = {{"Dropper1", -6.0f, 1, 0, {0.6f, 0.6f, 0.6f}}, {"Dropper2", -2.0f, 3, 25, {0.85f, 0.55f, 0.2f}},
                    {"Dropper3", 2.0f, 8, 100, {0.95f, 0.85f, 0.2f}}};
    for (int i = 0; i < 3; ++i) {
        const D& d = ds[i];
        SceneNode* dr = part(scene, droppers, d.name, Cube, o + glm::vec3(d.x, 4.0f, -2.0f), {1.6f, 1.6f, 1.6f}, {0.3f, 0.3f, 0.35f}, Material::Metal);
        attr(dr, "OreValue", d.value);
        attrColor(dr, "OreColor", d.color);
        attr(dr, "Bought", i == 0);
        if (i > 0) {
            dr->transparency = 0.7f;
            SceneNode* b = part(scene, buttons, (std::string("Buy") + d.name).c_str(), Cube, o + glm::vec3(d.x, 0.3f, 4.0f), {2.5f, 0.2f, 2.5f},
                                {0.2f, 0.85f, 0.3f}, Material::Neon);
            attr(b, "Price", d.price);
            attr(b, "Dropper", d.name);
        }
    }
    script(scene, t, "TycoonScript", kTycoon);
    script(scene, nullptr, "Cash", leaderstatsScript(R"("Cash")").c_str());
    title(scene, "My Tycoon", "Claim the tycoon, collect Cash from your droppers and buy more of them!");
}

// ------------------------------------------------------------------ Sword fight
void swordFight(Scene& scene) {
    SceneNode* arena = model(scene, "Arena");
    part(scene, arena, "Floor", Cube, {0, 0.05f, 0}, {40, 0.1f, 40}, {0.45f, 0.4f, 0.35f}, Material::Brick);
    for (int i = 0; i < 4; ++i) {   // walls around it
        const float a = i * 1.5707963f;
        const glm::vec3 c(std::sin(a) * 20.5f, 3.0f, std::cos(a) * 20.5f);
        part(scene, arena, "Wall", Cube, c, i % 2 ? glm::vec3(1, 6, 42) : glm::vec3(42, 6, 1), {0.5f, 0.48f, 0.45f}, Material::Brick);
    }
    // Cover to fight around, and spawns in the corners.
    part(scene, arena, "Pillar", Cube, {-6, 2.5f, -6}, {2, 5, 2}, {0.6f, 0.58f, 0.55f}, Material::Concrete);
    part(scene, arena, "Pillar", Cube, {6, 2.5f, 6}, {2, 5, 2}, {0.6f, 0.58f, 0.55f}, Material::Concrete);
    part(scene, arena, "Platform", Cube, {0, 2.0f, 0}, {6, 0.5f, 6}, {0.35f, 0.35f, 0.4f}, Material::Metal);
    if (SceneNode* spawn = scene.root()->findChild("SpawnLocation")) scene.removeNode(spawn);
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 c((i & 1) ? 15.0f : -15.0f, 0.2f, (i & 2) ? 15.0f : -15.0f);
        part(scene, arena, "SpawnLocation", Cube, c, {4, 0.2f, 4}, {0.55f, 0.57f, 0.6f});
    }
    buildPremade(scene, Premade::GearSword, {0, 3, 0});   // everyone gets a sword (it's in the StarterPack)
    script(scene, nullptr, "Scores", leaderstatsScript(R"("Wipeouts")").c_str());
    script(scene, nullptr, "CountWipeouts", R"(-- Each time someone is knocked out, their Wipeouts go up by one.
local Players = game:GetService("Players")
local function watch(player)
    player.CharacterAdded:Connect(function(character)
        local humanoid = character:WaitForChild("Humanoid")
        humanoid.Died:Connect(function()
            local stats = player:FindFirstChild("leaderstats")
            if stats then stats.Wipeouts.Value = stats.Wipeouts.Value + 1 end
        end)
    end)
end
Players.PlayerAdded:Connect(watch)
for _, player in ipairs(Players:GetPlayers()) do watch(player) end
)");
    if (SceneNode* base = scene.root()->findChild("Baseplate")) base->color = {0.3f, 0.3f, 0.32f};
    title(scene, "Sword Fight", "Everyone gets a classic sword. Last one standing wins!");
}

// ------------------------------------------------------------------ Racing
void racing(Scene& scene) {
    if (SceneNode* base = scene.root()->findChild("Baseplate")) base->transform.scale = {220, 1, 220};
    SceneNode* track = model(scene, "Track");
    // An oval of road pieces.
    const int pieces = 32;
    const float rx = 70.0f, rz = 40.0f;
    for (int i = 0; i < pieces; ++i) {
        const float a0 = 6.2831853f * i / pieces, a1 = 6.2831853f * (i + 1) / pieces;
        const glm::vec3 p0(std::cos(a0) * rx, 0.05f, std::sin(a0) * rz), p1(std::cos(a1) * rx, 0.05f, std::sin(a1) * rz);
        const glm::vec3 mid = (p0 + p1) * 0.5f, d = p1 - p0;
        SceneNode* road = part(scene, track, "Road", Cube, mid, {14.0f, 0.1f, glm::length(d) + 1.5f}, {0.2f, 0.2f, 0.22f}, Material::Concrete);
        road->transform.rotation.y = glm::degrees(std::atan2(d.x, d.z));
    }
    // The start / finish line (you drive through it) and a halfway gate (so laps must go all the way round).
    SceneNode* finish = part(scene, track, "FinishLine", Cube, {rx, 3.0f, 0}, {14, 6, 1}, {1, 1, 1}, Material::Neon);
    finish->transparency = 0.6f;
    finish->canCollide = false;
    SceneNode* half = part(scene, track, "Halfway", Cube, {-rx, 3.0f, 0}, {14, 6, 1}, {1, 0.8f, 0.2f}, Material::Neon);
    half->transparency = 0.85f;
    half->canCollide = false;
    if (SceneNode* spawn = scene.root()->findChild("SpawnLocation")) spawn->transform.position = {rx + 12.0f, 0.1f, -8.0f};
    for (int i = 0; i < 3; ++i) buildPremade(scene, Premade::Car, {rx - 4.0f + i * 4.0f, 0.0f, -8.0f});
    script(scene, nullptr, "Laps", leaderstatsScript(R"("Laps", "BestSeconds")").c_str());
    script(scene, nullptr, "LapTimer", R"(-- Drive through the halfway gate, then the finish line: that's a lap. Your fastest
-- lap (in seconds) shows on the leaderboard as BestSeconds.
local Players = game:GetService("Players")
local track = workspace.Track
local started, halfway = {}, {}

local function whoTouched(hit)
    local model = hit.Parent
    while model and model ~= workspace do
        local player = Players:GetPlayerFromCharacter(model)
        if player then return player end
        model = model.Parent
    end
    return nil
end

track.Halfway.Touched:Connect(function(hit)
    local player = whoTouched(hit)
    if player and started[player] then halfway[player] = true end
end)

track.FinishLine.Touched:Connect(function(hit)
    local player = whoTouched(hit)
    if not player then return end
    local now = os.clock()
    if started[player] and halfway[player] then
        local seconds = math.floor(now - started[player])
        local stats = player:FindFirstChild("leaderstats")
        if stats then
            stats.Laps.Value = stats.Laps.Value + 1
            if stats.BestSeconds.Value == 0 or seconds < stats.BestSeconds.Value then stats.BestSeconds.Value = seconds end
        end
    end
    if not started[player] or halfway[player] then
        started[player] = now
        halfway[player] = false
    end
end)
)");
    title(scene, "Racing", "Hop in a car and race around the track. Your best lap time shows on the leaderboard.");
}

} // namespace

const std::vector<Info>& list() {
    static const std::vector<Info> all = {
        {Kind::Obby, "Obby", "An obstacle course: jump across platforms, with checkpoints and a Stage counter"},
        {Kind::Tycoon, "Tycoon", "Claim a plot, collect Cash from droppers on a belt and buy more droppers"},
        {Kind::SwordFight, "Sword Fight", "An arena where everyone gets a classic sword, with Wipeouts on the leaderboard"},
        {Kind::Racing, "Racing", "An oval track with cars and a lap timer that keeps your best lap"},
    };
    return all;
}

void build(Scene& scene, Kind kind) {
    clearStart(scene);
    switch (kind) {
        case Kind::Obby: obby(scene); break;
        case Kind::Tycoon: tycoon(scene); break;
        case Kind::SwordFight: swordFight(scene); break;
        case Kind::Racing: racing(scene); break;
    }
}

} // namespace GameTemplates
