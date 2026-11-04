// Studio's AI making whole games, not one part at a time: build makes a map of many
// parts in one call, terrain shapes the ground, create_ui makes a whole screen UI,
// add_game_system adds ready-made, tested game systems (leaderboard, teams, rounds,
// collectibles, checkpoints, a shop) and set_atmosphere sets the mood (time of day,
// fog, weather). The tool descriptions are in AiTools.cpp; Editor::runAiTool sends
// these five here.
#include "Editor.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Terrain.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using nlohmann::json;

namespace {

json vec(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
std::string idOf(const SceneNode* n) { return "#" + std::to_string(n->id); }

std::string canon(const std::string& s) {
    std::string out;
    for (char c : s) if (c != '_' && c != ' ' && c != '-') out += (char)std::tolower((unsigned char)c);
    return out;
}

bool readVec3(const json& v, glm::vec3& out) {
    if (!v.is_array() || v.size() != 3) return false;
    for (int i = 0; i < 3; ++i) { if (!v[i].is_number()) return false; out[i] = v[i].get<float>(); }
    return true;
}

// A colour: [r, g, b] (0-1, or 0-255 when a number is over 1), "#rrggbb" or a plain name.
bool readColor(const json& v, glm::vec3& out) {
    if (v.is_array() && v.size() == 3 && readVec3(v, out)) {
        if (out.x > 1.0f || out.y > 1.0f || out.z > 1.0f) out /= 255.0f;
        out = glm::clamp(out, glm::vec3(0.0f), glm::vec3(1.0f));
        return true;
    }
    if (!v.is_string()) return false;
    std::string s = v.get<std::string>();
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    if (s.size() == 7 && s[0] == '#' && s.find_first_not_of("0123456789abcdef", 1) == std::string::npos) {
        const unsigned long x = std::stoul(s.substr(1), nullptr, 16);
        out = glm::vec3((x >> 16) & 255, (x >> 8) & 255, x & 255) / 255.0f;
        return true;
    }
    static const std::map<std::string, glm::vec3> names = {
        {"white", {1, 1, 1}}, {"black", {0.07f, 0.07f, 0.07f}}, {"grey", {0.5f, 0.5f, 0.5f}}, {"gray", {0.5f, 0.5f, 0.5f}},
        {"dark grey", {0.25f, 0.25f, 0.27f}}, {"dark gray", {0.25f, 0.25f, 0.27f}}, {"light grey", {0.78f, 0.78f, 0.8f}},
        {"light gray", {0.78f, 0.78f, 0.8f}}, {"red", {0.85f, 0.12f, 0.12f}}, {"dark red", {0.5f, 0.05f, 0.05f}},
        {"orange", {0.95f, 0.5f, 0.1f}}, {"yellow", {0.98f, 0.85f, 0.15f}}, {"gold", {0.85f, 0.65f, 0.15f}},
        {"green", {0.2f, 0.65f, 0.25f}}, {"dark green", {0.1f, 0.35f, 0.15f}}, {"lime", {0.55f, 0.9f, 0.2f}},
        {"blue", {0.15f, 0.35f, 0.85f}}, {"navy", {0.08f, 0.12f, 0.35f}}, {"light blue", {0.5f, 0.75f, 0.95f}},
        {"cyan", {0.2f, 0.8f, 0.85f}}, {"purple", {0.5f, 0.2f, 0.7f}}, {"pink", {0.95f, 0.5f, 0.7f}},
        {"brown", {0.45f, 0.28f, 0.15f}}, {"dark brown", {0.28f, 0.17f, 0.09f}}, {"tan", {0.82f, 0.7f, 0.52f}},
        {"beige", {0.9f, 0.85f, 0.72f}}, {"silver", {0.75f, 0.75f, 0.78f}}, {"stone", {0.55f, 0.55f, 0.52f}},
        {"sand", {0.86f, 0.78f, 0.55f}}, {"wood", {0.6f, 0.42f, 0.25f}}, {"grass", {0.3f, 0.6f, 0.25f}},
        {"water", {0.2f, 0.5f, 0.95f}}, {"lava", {1.0f, 0.35f, 0.05f}},
    };
    auto it = names.find(s);
    if (it == names.end()) return false;
    out = it->second;
    return true;
}

// A part material by name ("Wood", "wood", "Enum.Material.Wood", Roblox ones we draw with one of ours).
bool readMaterial(const json& v, Material& out) {
    if (!v.is_string()) return false;
    std::string s = v.get<std::string>();
    if (s.rfind("Enum.", 0) == 0) s = s.substr(s.rfind('.') + 1);
    if (materialFromName(s, out)) return true;
    for (int i = 0; i < kMaterialCount; ++i)
        if (canon(kMaterialNames[i]) == canon(s)) { out = (Material)i; return true; }
    for (const char* other : {"CorrodedMetal", "WoodPlanks", "Asphalt", "Basalt", "Limestone", "Pavement", "Sandstone", "Rock",
                              "Pebble", "LeafyGrass", "Salt", "Glacier"})
        if (canon(other) == canon(s)) return materialFromName(other, out);
    return false;
}

std::string luaQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') {}
        else out += c;
    }
    return out + "\"";
}
std::string num(double v) {
    std::ostringstream o;
    o << v;
    return o.str();
}
std::string luaColor(const glm::vec3& c) {
    return "Color3.new(" + num(c.x) + ", " + num(c.y) + ", " + num(c.z) + ")";
}

// The shared helper every game system script uses to find (or make) a leaderboard number.
const char* kGetStat = R"LUA(-- A leaderboard number (player.leaderstats.<name>), made if it isn't there yet.
local function getStat(player, name)
    local folder = player:FindFirstChild("leaderstats") or player:WaitForChild("leaderstats", 10)
    if not folder then
        folder = Instance.new("Folder")
        folder.Name = "leaderstats"
        folder.Parent = player
    end
    local value = folder:FindFirstChild(name)
    if not value then
        value = Instance.new("IntValue")
        value.Name = name
        value.Value = 0
        value.Parent = folder
    end
    return value
end
)LUA";

// ---------------------------------------------------------------- the systems' code
std::string leaderboardSource(const std::vector<std::string>& stats, bool save) {
    std::string list;
    for (const std::string& s : stats) list += (list.empty() ? "" : ", ") + luaQuote(s);
    return R"LUA(-- Leaderboard (made by Studio's AI, add_game_system): every player gets these numbers,
-- shown on the player list while playing. Other scripts change them, like:
--     player.leaderstats.Coins.Value = player.leaderstats.Coins.Value + 1
local Players = game:GetService("Players")

local STATS = { )LUA" + list + R"LUA( }   -- the numbers every player has
local SAVE = )LUA" + std::string(save ? "true" : "false") + R"LUA(   -- true: keep them between visits (DataStoreService)
local STORE_NAME = "Leaderboard"

local store = nil
if SAVE then store = game:GetService("DataStoreService"):GetDataStore(STORE_NAME) end

local function setUp(player)
    if player:FindFirstChild("leaderstats") then return end
    local saved = nil
    if store then
        local ok, data = pcall(function() return store:GetAsync("player_" .. player.UserId) end)
        if ok and type(data) == "table" then saved = data end
    end
    local folder = Instance.new("Folder")
    folder.Name = "leaderstats"
    for _, name in ipairs(STATS) do
        local value = Instance.new("IntValue")
        value.Name = name
        value.Value = saved and tonumber(saved[name]) or 0
        value.Parent = folder
    end
    folder.Parent = player
end

local function save(player)
    if not store then return end
    local folder = player:FindFirstChild("leaderstats")
    if not folder then return end
    local data = {}
    for _, value in ipairs(folder:GetChildren()) do data[value.Name] = value.Value end
    pcall(function() store:SetAsync("player_" .. player.UserId, data) end)
end

Players.PlayerAdded:Connect(setUp)
Players.PlayerRemoving:Connect(save)
for _, player in ipairs(Players:GetPlayers()) do setUp(player) end
if store then
    game:BindToClose(function()
        for _, player in ipairs(Players:GetPlayers()) do save(player) end
    end)
    -- Save every minute too, in case the server stops suddenly.
    while true do
        task.wait(60)
        for _, player in ipairs(Players:GetPlayers()) do save(player) end
    end
end
)LUA";
}

struct RoundSettings {
    double intermission = 15, roundTime = 60;
    int minPlayers = 1;
    std::string mode = "survival", winStat = "Wins", pointsStat = "Kills";
    std::string mapName, mapFrom;   // "" = no map
};

std::string roundsSource(const RoundSettings& r) {
    return R"LUA(-- Round system (made by Studio's AI, add_game_system): waits for players, counts down an
-- intermission, starts a round (copies the map in and moves everyone to its ArenaSpawn
-- parts), picks the winners, then sends everyone back to the lobby. Forever.
-- The status line on everyone's screen is ReplicatedStorage.RoundStatus (the RoundStatusGui shows it).
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")

local INTERMISSION = )LUA" + num(r.intermission) + R"LUA(   -- seconds between rounds
local ROUND_TIME = )LUA" + num(r.roundTime) + R"LUA(     -- seconds a round lasts
local MIN_PLAYERS = )LUA" + std::to_string(r.minPlayers) + R"LUA(     -- players needed to start a round
local MODE = )LUA" + luaQuote(r.mode) + R"LUA(   -- "survival": players still alive at the end win; "most_points": who got the most POINTS_STAT this round wins
local WIN_STAT = )LUA" + luaQuote(r.winStat) + R"LUA(   -- winners get +1 of this leaderboard number
local POINTS_STAT = )LUA" + luaQuote(r.pointsStat) + R"LUA(   -- (most_points) the number that counts
local MAP_NAME = )LUA" + luaQuote(r.mapName) + R"LUA(   -- the map Model ("" = no map: rounds happen where everyone is)
local MAP_FROM = )LUA" + luaQuote(r.mapFrom.empty() ? std::string("ServerStorage") : r.mapFrom) + R"LUA(   -- where the map is kept (ServerStorage: copied in for each round)
local SPAWN_NAME = "ArenaSpawn"   -- parts players are moved to when a round starts

)LUA" + kGetStat + R"LUA(
local status = ReplicatedStorage:FindFirstChild("RoundStatus")
if not status then
    status = Instance.new("StringValue")
    status.Name = "RoundStatus"
    status.Parent = ReplicatedStorage
end
local function say(text)
    status.Value = text
end

local function alive(player)
    local character = player.Character
    local humanoid = character and character:FindFirstChildOfClass("Humanoid")
    return humanoid ~= nil and humanoid.Health > 0
end

local function mapTemplate()
    if MAP_NAME == "" then return nil end
    local holder = MAP_FROM == "Workspace" and workspace or game:GetService(MAP_FROM)
    return holder and holder:FindFirstChild(MAP_NAME)
end

local function spawnPoints(map)
    local points = {}
    for _, d in ipairs((map or workspace):GetDescendants()) do
        if d.Name == SPAWN_NAME and d:IsA("BasePart") then table.insert(points, d) end
    end
    return points
end

local function moveTo(player, part)
    local character = player.Character
    if character and part then
        character:PivotTo(CFrame.new(part.Position + Vector3.new(0, 3, 0)))
    end
end

while true do
    while #Players:GetPlayers() < MIN_PLAYERS do
        say("Waiting for players (" .. #Players:GetPlayers() .. "/" .. MIN_PLAYERS .. ")")
        task.wait(1)
    end
    for t = INTERMISSION, 1, -1 do
        say("Intermission: " .. t)
        task.wait(1)
    end

    -- The round starts: copy the map in and move everyone into it.
    local template = mapTemplate()
    local map = nil
    if template and MAP_FROM ~= "Workspace" then
        map = template:Clone()
        map.Parent = workspace
    elseif template then
        map = template
    end
    local points = spawnPoints(map)
    local playing, startPoints, connections = {}, {}, {}
    local count = 0
    for i, player in ipairs(Players:GetPlayers()) do
        if alive(player) then
            playing[player.Name] = player
            count = count + 1
            if #points > 0 then moveTo(player, points[(i - 1) % #points + 1]) end
            startPoints[player.Name] = getStat(player, POINTS_STAT).Value
            local humanoid = player.Character:FindFirstChildOfClass("Humanoid")
            table.insert(connections, humanoid.Died:Connect(function()
                playing[player.Name] = nil
            end))
        end
    end

    for t = ROUND_TIME, 1, -1 do
        local left = 0
        for name, player in pairs(playing) do
            if player.Parent then left = left + 1 else playing[name] = nil end
        end
        if MODE == "survival" and (left == 0 or (count > 1 and left == 1)) then break end
        say("Round: " .. t .. "s left - " .. left .. " playing")
        task.wait(1)
    end
    for _, c in ipairs(connections) do c:Disconnect() end

    -- Who won?
    local winners = {}
    if MODE == "survival" then
        for _, player in pairs(playing) do
            if player.Parent and alive(player) then table.insert(winners, player) end
        end
    else
        local best = 0
        for name, start in pairs(startPoints) do
            local player = Players:FindFirstChild(name)
            if player then
                local got = getStat(player, POINTS_STAT).Value - start
                if got > best then
                    best = got
                    winners = { player }
                elseif got == best and got > 0 then
                    table.insert(winners, player)
                end
            end
        end
    end
    local names = {}
    for _, player in ipairs(winners) do
        local wins = getStat(player, WIN_STAT)
        wins.Value = wins.Value + 1
        table.insert(names, player.Name)
    end
    if #names > 0 then
        say("Winner: " .. table.concat(names, ", ") .. "!")
        print("Round over. Winner: " .. table.concat(names, ", "))
    else
        say("Nobody won this round.")
        print("Round over. Nobody won.")
    end
    task.wait(5)

    -- Back to the lobby.
    if map and map ~= template then map:Destroy() end
    for _, player in ipairs(Players:GetPlayers()) do
        player:LoadCharacter()
    end
end
)LUA";
}

const char* kRoundStatusClient = R"LUA(-- Shows the round system's status (ReplicatedStorage.RoundStatus) at the top of the screen.
local status = game:GetService("ReplicatedStorage"):WaitForChild("RoundStatus")
local label = script.Parent:WaitForChild("Status")

local function show()
    label.Text = status.Value
end
status.Changed:Connect(show)
show()
)LUA";

std::string collectiblesSource(const std::string& name, const std::string& stat, double amount, double respawn) {
    return R"LUA(-- Collectibles (made by Studio's AI, add_game_system): every part called (or tagged) NAME
-- gives the player who touches it AMOUNT of STAT, hides, and comes back after RESPAWN seconds.
local Players = game:GetService("Players")
local CollectionService = game:GetService("CollectionService")
local RunService = game:GetService("RunService")

local NAME = )LUA" + luaQuote(name) + R"LUA(   -- parts with this name or tag can be collected
local STAT = )LUA" + luaQuote(stat) + R"LUA(   -- the leaderboard number it adds to
local AMOUNT = )LUA" + num(amount) + R"LUA(       -- how much each one is worth
local RESPAWN = )LUA" + num(respawn) + R"LUA(     -- seconds until it comes back (0 = never)
local SPIN = true      -- spin them (looks like a coin)

)LUA" + kGetStat + R"LUA(
local spinning = {}

local function hook(thing)
    local part = thing
    if thing:IsA("Model") then part = thing.PrimaryPart or thing:FindFirstChildWhichIsA("BasePart") end
    if not part or not part:IsA("BasePart") or part:GetAttribute("Collectible") then return end
    part:SetAttribute("Collectible", true)
    if SPIN and part.Anchored then table.insert(spinning, part) end
    local ready = true
    local shown = part.Transparency
    part.Touched:Connect(function(hit)
        if not ready then return end
        local player = Players:GetPlayerFromCharacter(hit.Parent)
        if not player then return end
        ready = false
        local value = getStat(player, STAT)
        value.Value = value.Value + AMOUNT
        if Sounds then Sounds.Play("coin", part.Position) end
        part.Transparency = 1
        if RESPAWN <= 0 then
            part:Destroy()
            return
        end
        task.wait(RESPAWN)
        part.Transparency = shown
        ready = true
    end)
end

for _, d in ipairs(workspace:GetDescendants()) do
    if d.Name == NAME then hook(d) end
end
for _, d in ipairs(CollectionService:GetTagged(NAME)) do hook(d) end
workspace.DescendantAdded:Connect(function(d)
    if d.Name == NAME then hook(d) end
end)

if SPIN then
    RunService.Heartbeat:Connect(function(dt)
        for _, part in ipairs(spinning) do
            if part.Parent then part.Orientation = part.Orientation + Vector3.new(0, 90 * dt, 0) end
        end
    end)
end
)LUA";
}

std::string checkpointsSource(const std::string& name, const std::string& stat, bool save) {
    return R"LUA(-- Checkpoints (made by Studio's AI, add_game_system): parts called NAME are the stages of an
-- obby. Touching one moves your STAT up to its number (and the engine respawns you on the last
-- one you touched). A checkpoint's number is its "Stage" attribute; without one, they count
-- in the order they are in the game.
local Players = game:GetService("Players")
local CollectionService = game:GetService("CollectionService")

local NAME = )LUA" + luaQuote(name) + R"LUA(   -- parts with this name (or tag) are checkpoints
local STAT = )LUA" + luaQuote(stat) + R"LUA(   -- the leaderboard number that shows your stage
local SAVE = )LUA" + std::string(save ? "true" : "false") + R"LUA(   -- true: come back to your stage next visit (DataStoreService)

)LUA" + kGetStat + R"LUA(
local store = nil
if SAVE then store = game:GetService("DataStoreService"):GetDataStore("Checkpoints") end

-- Every checkpoint and its number.
local checkpoints, seen, found = {}, {}, {}
for _, d in ipairs(workspace:GetDescendants()) do
    if d.Name == NAME and d:IsA("BasePart") then table.insert(found, d) end
end
for _, d in ipairs(CollectionService:GetTagged(NAME)) do
    if d:IsA("BasePart") then table.insert(found, d) end
end
local nextNumber = 0
for _, part in ipairs(found) do
    if not seen[part] then
        seen[part] = true
        nextNumber = nextNumber + 1
        local number = tonumber(part:GetAttribute("Stage")) or nextNumber
        checkpoints[number] = part
        part.Touched:Connect(function(hit)
            local player = Players:GetPlayerFromCharacter(hit.Parent)
            if not player then return end
            local stage = getStat(player, STAT)
            if stage.Value < number then
                stage.Value = number
                if store then pcall(function() store:SetAsync("stage_" .. player.UserId, number) end) end
            end
        end)
    end
end

-- Coming back: start at the saved stage.
local function joined(player)
    if not store then return end
    local ok, saved = pcall(function() return store:GetAsync("stage_" .. player.UserId) end)
    saved = ok and tonumber(saved) or 0
    if saved <= 0 then return end
    local stage = getStat(player, STAT)
    if saved > stage.Value then stage.Value = saved end
    local part = checkpoints[stage.Value]
    local character = player.Character or player.CharacterAdded:Wait()
    task.wait(0.2)
    if part and character then character:PivotTo(CFrame.new(part.Position + Vector3.new(0, 3, 0))) end
end
Players.PlayerAdded:Connect(joined)
for _, player in ipairs(Players:GetPlayers()) do task.spawn(joined, player) end
)LUA";
}

struct ShopItem { std::string name; int price; };

std::string shopItemsLua(const std::vector<ShopItem>& items) {
    std::string s = "{\n";
    for (const ShopItem& i : items) s += "    { name = " + luaQuote(i.name) + ", price = " + std::to_string(i.price) + " },\n";
    return s + "}";
}

std::string shopServerSource(const std::string& currency, const std::vector<ShopItem>& items) {
    return R"LUA(-- Shop (made by Studio's AI, add_game_system): players buy the Tools in ServerStorage.ShopItems
-- with a leaderboard number. The ShopGui on their screen sends ReplicatedStorage.ShopBuy;
-- this script checks the price, takes the money, gives the tool and answers with ShopResult.
-- Bought tools come back every time the player respawns.
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local ServerStorage = game:GetService("ServerStorage")

local CURRENCY = )LUA" + luaQuote(currency) + R"LUA(   -- the leaderboard number used to pay
local ITEMS = )LUA" + shopItemsLua(items) + R"LUA(   -- the names of Tools in ServerStorage.ShopItems, and their prices

)LUA" + kGetStat + R"LUA(
local buy = ReplicatedStorage:WaitForChild("ShopBuy")
local result = ReplicatedStorage:WaitForChild("ShopResult")
local stock = ServerStorage:WaitForChild("ShopItems")
local owned = {}   -- [player name] = { [item name] = true }

local function backpackOf(player)
    local ok, backpack = pcall(function() return player.Backpack end)
    return ok and backpack or nil
end

local function has(player, name)
    for _, place in ipairs({ backpackOf(player) or false, player.Character or false }) do
        if place and place:FindFirstChild(name) then return true end
    end
    return false
end

local function give(player, name)
    local tool = stock:FindFirstChild(name)
    local backpack = backpackOf(player)
    if not tool or not backpack or has(player, name) then return end
    tool:Clone().Parent = backpack
end

buy.OnServerEvent:Connect(function(player, name)
    local price = nil
    for _, item in ipairs(ITEMS) do
        if item.name == name then price = item.price end
    end
    if not price then
        result:FireClient(player, false, "That isn't for sale.")
        return
    end
    owned[player.Name] = owned[player.Name] or {}
    if owned[player.Name][name] then
        result:FireClient(player, false, "You already have " .. name .. ".")
        return
    end
    local money = getStat(player, CURRENCY)
    if money.Value < price then
        result:FireClient(player, false, "You need " .. price .. " " .. CURRENCY .. " (you have " .. money.Value .. ").")
        return
    end
    money.Value = money.Value - price
    owned[player.Name][name] = true
    give(player, name)
    result:FireClient(player, true, "You bought " .. name .. "!")
    print(player.Name .. " bought " .. name .. " for " .. price .. " " .. CURRENCY)
end)

local function watch(player)
    player.CharacterAdded:Connect(function()
        task.wait(0.5)
        for name in pairs(owned[player.Name] or {}) do give(player, name) end
    end)
end
Players.PlayerAdded:Connect(watch)
for _, player in ipairs(Players:GetPlayers()) do watch(player) end
Players.PlayerRemoving:Connect(function(player) owned[player.Name] = nil end)
)LUA";
}

const char* kShopClient = R"LUA(-- The shop window (made by Studio's AI, add_game_system). The Shop button opens it; each
-- button in Window.List buys the item with the same name (the Shop script checks the price).
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local gui = script.Parent
local window = gui:WaitForChild("Window")
local list = window:WaitForChild("List")
local message = window:WaitForChild("Message")
local buy = ReplicatedStorage:WaitForChild("ShopBuy")
local result = ReplicatedStorage:WaitForChild("ShopResult")

gui:WaitForChild("OpenShop").MouseButton1Click:Connect(function()
    window.Visible = not window.Visible
end)
window:WaitForChild("Close").MouseButton1Click:Connect(function()
    window.Visible = false
end)
for _, button in ipairs(list:GetChildren()) do
    if button:IsA("TextButton") then
        button.MouseButton1Click:Connect(function()
            message.Text = "Buying..."
            buy:FireServer(button.Name)
        end)
    end
end
result.OnClientEvent:Connect(function(ok, text)
    message.Text = text
    message.TextColor3 = ok and Color3.new(0.4, 1, 0.5) or Color3.new(1, 0.5, 0.45)
end)
)LUA";

const char* kSystemTagPrefix = "GameSystem:";

} // namespace

AiToolResult Editor::runGameAiTool(const std::string& name, const json& args) {
    SceneNode* const root = m_scene->root();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto done = [&](json d) {
        d["success"] = true;
        d["operation"] = name;
        AiToolResult r;
        r.text = d.dump(1);
        r.data = std::move(d);
        return r;
    };
    auto fail = [&](const char* code, const std::string& message, const std::string& suggestion, json extra = json::object()) {
        json d = {{"success", false}, {"operation", name},
                  {"error", {{"code", code}, {"message", message}, {"recoverable", true}, {"suggested_action", suggestion}}}};
        for (auto& [k, v] : extra.items()) d["error"][k] = v;
        AiToolResult r;
        r.text = d.dump(1);
        r.error = true;
        r.data = std::move(d);
        return r;
    };
    // Another tool's answer as this tool's failure (keeps its error code and message).
    auto passOn = [&](AiToolResult r) {
        r.data["operation"] = name;
        r.text = r.data.dump(1);
        return r;
    };
    auto pathOf = [&](const SceneNode* n) {
        if (n == root) return std::string("Workspace");
        std::string p = n->name;
        const SceneNode* top = n;
        for (const SceneNode* a = n->parent; a && a != root; a = a->parent) { p = a->name + "." + p; top = a; }
        return m_scene->isServiceFolder(top) ? p : "Workspace." + p;
    };
    auto brief = [&](const SceneNode* n) {
        json b = {{"object_id", idOf(n)}, {"name", n->name}, {"path", pathOf(n)}};
        return b;
    };
    auto byRef = [&](const AiToolResult& r) -> SceneNode* {
        if (r.error || !r.data.contains("object_id")) return nullptr;
        return aiFind(r.data["object_id"].get<std::string>());
    };
    if (m_playing && name != "terrain")
        return fail("PLAYTEST_RUNNING", "Stop the playtest first: changes made during a playtest are thrown away.", "playtest stop, then try again.");

    // ------------------------------------------------------------------ build
    if (name == "build") {
        if (!args.contains("parts") || !args["parts"].is_array() || args["parts"].empty())
            return fail("INVALID_INPUT", "`parts` is missing: give a list of parts.", "parts: [{name, position, size, color, material}...]");
        glm::vec3 offset(0.0f);
        if (args.contains("at") && !readVec3(args["at"], offset))
            return fail("INVALID_INPUT", "`at` must be [x, y, z].", "Leave it out, or give three numbers.");
        struct Spec {
            std::string name;
            PrimitiveType shape = PrimitiveType::Cube;
            glm::vec3 pos{0.0f}, size{2.0f, 1.0f, 1.0f}, rot{0.0f}, color{0.64f, 0.64f, 0.66f};
            Material mat = Material::Plastic;
            float transparency = 0.0f;
            bool anchored = true, collide = true;
            std::vector<std::string> tags;
        };
        std::vector<Spec> specs;
        const json& list = args["parts"];
        for (size_t i = 0; i < list.size(); ++i) {
            const json& p = list[i];
            auto bad = [&](const std::string& msg, const std::string& fix) {
                return fail("INVALID_INPUT", "Part " + std::to_string(i) + ": " + msg, fix + " Nothing was built.", {{"part_index", i}});
            };
            if (!p.is_object()) return bad("each part must be an object like {position, size}.", "Give {name, position, size...}.");
            Spec s;
            s.name = p.value("name", std::string("Part"));
            if (p.contains("shape")) {
                const std::string sh = canon(p["shape"].is_string() ? p["shape"].get<std::string>() : std::string());
                if (sh == "block" || sh == "cube" || sh == "part" || sh == "box") s.shape = PrimitiveType::Cube;
                else if (sh == "ball" || sh == "sphere") s.shape = PrimitiveType::Sphere;
                else if (sh == "cylinder") s.shape = PrimitiveType::Cylinder;
                else return fail("INVALID_ENUM_VALUE", "Part " + std::to_string(i) + ": shape must be Block, Ball or Cylinder.",
                                 "For wedges and ramps use a Block turned with orientation. Nothing was built.",
                                 {{"part_index", i}, {"valid_choices", json::array({"Block", "Ball", "Cylinder"})}});
            }
            if (!p.contains("position") || !readVec3(p["position"], s.pos)) return bad("`position` [x, y, z] is missing or wrong.", "Give its centre as [x, y, z].");
            if (p.contains("size") && !readVec3(p["size"], s.size)) return bad("`size` must be [x, y, z].", "Give three positive numbers.");
            if (s.size.x <= 0 || s.size.y <= 0 || s.size.z <= 0) return bad("every side of `size` must be more than 0.", "Use positive numbers.");
            if (p.contains("orientation") && !readVec3(p["orientation"], s.rot)) return bad("`orientation` must be [x, y, z] degrees.", "Give three numbers.");
            if (p.contains("color") && !readColor(p["color"], s.color))
                return bad("`color` " + p["color"].dump() + " isn't a colour.", "Use [r, g, b] (0-1 or 0-255), \"#rrggbb\" or a name like \"red\".");
            if (p.contains("material") && !readMaterial(p["material"], s.mat)) {
                json choices(std::vector<std::string>(std::begin(kMaterialNames), std::end(kMaterialNames)));
                return fail("INVALID_ENUM_VALUE", "Part " + std::to_string(i) + ": " + p["material"].dump() + " isn't a material.",
                            "Use one of valid_choices. Nothing was built.", {{"part_index", i}, {"valid_choices", choices}});
            }
            if (p.contains("transparency") && p["transparency"].is_number()) s.transparency = std::clamp(p["transparency"].get<float>(), 0.0f, 1.0f);
            if (p.contains("anchored") && p["anchored"].is_boolean()) s.anchored = p["anchored"].get<bool>();
            for (const char* k : {"can_collide", "CanCollide", "cancollide"})
                if (p.contains(k) && p[k].is_boolean()) s.collide = p[k].get<bool>();
            if (p.contains("tags") && p["tags"].is_array())
                for (const json& t : p["tags"]) if (t.is_string()) s.tags.push_back(t.get<std::string>());
            int count = 1;
            glm::vec3 step(0.0f), grow(0.0f);
            if (p.contains("repeat")) {
                const json& r = p["repeat"];
                if (!r.is_object() || !r.contains("count") || !r["count"].is_number_integer())
                    return bad("`repeat` needs {count, step}.", "Like repeat: {count: 10, step: [0, 0.5, -1.5]}.");
                count = r["count"].get<int>();
                if (count < 1 || count > 100) return bad("repeat count must be 1-100.", "Use a smaller count, or another part.");
                if (!r.contains("step") || !readVec3(r["step"], step)) return bad("repeat needs step [dx, dy, dz].", "How far each copy moves.");
                if (r.contains("size_step") && !readVec3(r["size_step"], grow)) return bad("size_step must be [dx, dy, dz].", "Or leave it out.");
            }
            for (int k = 0; k < count; ++k) {
                Spec c = s;
                c.pos = s.pos + step * (float)k + offset;
                c.size = glm::max(s.size + grow * (float)k, glm::vec3(0.05f));
                if (count > 1) c.name = s.name + std::to_string(k + 1);
                specs.push_back(std::move(c));
            }
            if (specs.size() > 2000)
                return fail("TOO_MANY_PARTS", "That's more than 2000 parts (with repeats) in one call.", "Split the build into several calls. Nothing was built.");
        }

        // Where it goes: a new Model (in parent), or an existing Model (`into`).
        SceneNode* model = nullptr;
        bool made = false;
        if (!str("into").empty()) {
            model = aiFind(str("into"));
            if (!model) return fail("OBJECT_NOT_FOUND", "No object " + str("into") + " exists.", "Use the object_id an earlier build returned.");
            if (model->kind != NodeKind::Model) return fail("NOT_A_MODEL", model->name + " isn't a Model or Folder.", "Give a Model's object_id, or leave `into` out.");
        } else {
            SceneNode* parent = root;
            if (!str("parent").empty()) {
                const std::string ref = str("parent");
                if (ref == "Lighting" || ref == "Players" || ref == "game.Lighting" || ref == "game.Players")
                    return fail("NOT_A_CONTAINER", ref + " can't hold parts.", "Use Workspace, ServerStorage or ReplicatedStorage.");
                parent = aiFind(ref, true);
                if (!parent) return fail("OBJECT_NOT_FOUND", "No object " + ref + " exists.", "Use Workspace, a service, or an object_id.");
                if (parent->isScript() || m_scene->isCharacterPart(parent))
                    return fail("NOT_A_CONTAINER", parent->name + " can't hold a Model.", "Use Workspace, a service, or a Model.");
            }
            auto m = std::make_unique<SceneNode>(str("model").empty() ? std::string("Build") : str("model"), NodeKind::Model);
            model = m_scene->insert(std::move(m), parent);
            made = true;
        }
        json ids = json::array();
        glm::vec3 mn(1e30f), mx(-1e30f);
        std::vector<SceneNode*> built;
        for (const Spec& s : specs) {
            SceneNode* n = m_scene->addNode(s.name, s.shape, MeshLibrary::get(s.shape));
            n->transform.position = s.pos;
            n->transform.rotation = s.rot;
            n->transform.scale = s.size;
            n->color = s.color;
            n->material = s.mat;
            n->transparency = s.transparency;
            n->anchored = s.anchored;
            n->canCollide = s.collide;
            n->tags = s.tags;
            if (s.mat == Material::Glass && s.transparency == 0.0f) n->transparency = 0.4f;
            m_scene->reparent(n, model);
            built.push_back(n);
            if (ids.size() < 100) ids.push_back({{"object_id", idOf(n)}, {"name", n->name}});
            mn = glm::min(mn, s.pos - s.size * 0.5f);
            mx = glm::max(mx, s.pos + s.size * 0.5f);
        }
        // Checks: parts sunk deep into each other (unturned only), below the world, loose with nothing under them.
        json warnings = json::array();
        int overlaps = 0;
        for (size_t a = 0; a < specs.size() && overlaps < 10; ++a)
            for (size_t b = a + 1; b < specs.size() && overlaps < 10; ++b) {
                const Spec& A = specs[a];
                const Spec& B = specs[b];
                if (A.rot != glm::vec3(0.0f) || B.rot != glm::vec3(0.0f) || A.transparency >= 1.0f || B.transparency >= 1.0f) continue;
                const glm::vec3 lo = glm::max(A.pos - A.size * 0.5f, B.pos - B.size * 0.5f);
                const glm::vec3 hi = glm::min(A.pos + A.size * 0.5f, B.pos + B.size * 0.5f);
                const glm::vec3 d = hi - lo;
                if (d.x <= 0 || d.y <= 0 || d.z <= 0) continue;
                const float vol = d.x * d.y * d.z;
                const float small = std::min(A.size.x * A.size.y * A.size.z, B.size.x * B.size.y * B.size.z);
                if (vol > small * 0.5f) {
                    warnings.push_back("\"" + built[a]->name + "\" " + idOf(built[a]) + " and \"" + built[b]->name + "\" " +
                                       idOf(built[b]) + " are more than half inside each other (same place twice?).");
                    ++overlaps;
                }
            }
        const float fallen = m_scene->world().fallenPartsHeight;
        for (size_t i = 0; i < specs.size(); ++i) {
            if (specs[i].pos.y < fallen) {
                warnings.push_back("\"" + built[i]->name + "\" " + idOf(built[i]) + " is below FallenPartsHeight (" + num(fallen) +
                                   "): it is destroyed as soon as the game starts.");
                break;
            }
        }
        for (size_t i = 0; i < specs.size(); ++i) {
            if (specs[i].anchored) continue;
            const Spec& s = specs[i];
            const float bottom = s.pos.y - s.size.y * 0.5f;
            bool held = bottom < 0.6f;   // (on or near the baseplate)
            float ground;
            if (!held && m_scene->terrain().heightAt(s.pos.x, s.pos.z, ground)) held = bottom - ground < 0.6f;
            for (size_t j = 0; j < specs.size() && !held; ++j) {
                if (j == i) continue;
                const Spec& o = specs[j];
                const float top = o.pos.y + o.size.y * 0.5f;
                if (std::fabs(top - bottom) < 0.6f && std::fabs(o.pos.x - s.pos.x) < (o.size.x + s.size.x) * 0.5f &&
                    std::fabs(o.pos.z - s.pos.z) < (o.size.z + s.size.z) * 0.5f) held = true;
            }
            if (!held) {
                warnings.push_back("Loose part \"" + s.name + "\" " + idOf(built[i]) + " has nothing right under it in this build: it will fall when the game starts (fine if that's wanted).");
                break;
            }
        }
        if (made) m_scene->select(model);
        json d = {{"object_id", idOf(model)}, {"path", pathOf(model)}, {"count", (int)specs.size()}, {"parts", ids},
                  {"bounds", {{"min", vec(mn)}, {"max", vec(mx)}}}};
        if (specs.size() > ids.size()) d["note"] = "Only the first 100 parts are listed; get_game_tree shows the rest.";
        if (!warnings.empty()) d["warnings"] = warnings;
        return done(d);
    }

    // ---------------------------------------------------------------- terrain
    if (name == "terrain") {
        Terrain& t = m_scene->terrain();
        const std::string action = str("action");
        if (m_playing && action != "info" && action != "height_at")
            return fail("PLAYTEST_RUNNING", "Stop the playtest first: terrain changed during a playtest is thrown away.",
                        "playtest stop, or use workspace.Terrain in a script for changes while the game runs.");
        auto summary = [&]() {
            json d = {{"exists", !t.empty()}};
            if (t.empty()) return d;
            float lo = 1e30f, hi = -1e30f;
            int counts[kTerrainMaterialCount] = {};
            const int pts = t.points();
            for (int k = 0; k < pts; ++k)
                for (int i = 0; i < pts; ++i) {
                    lo = std::min(lo, t.height(i, k));
                    hi = std::max(hi, t.height(i, k));
                }
            const float half = t.halfWidth();
            for (int k = 0; k < t.cells(); ++k)
                for (int i = 0; i < t.cells(); ++i)
                    counts[(int)t.materialAt(-half + (i + 0.5f) * t.cellSize(), -half + (k + 0.5f) * t.cellSize())]++;
            json mats = json::object();
            const float total = (float)t.cells() * t.cells();
            for (int m = 0; m < kTerrainMaterialCount; ++m)
                if (counts[m]) mats[kTerrainMaterialNames[m]] = std::round(1000.0 * counts[m] / (double)total) / 10.0;
            d["size"] = half * 2.0f;
            d["cells"] = t.cells();
            d["cell_size"] = t.cellSize();
            d["covers"] = {{"min", json::array({-half, -half})}, {"max", json::array({half, half})}, {"note", "x and z"}};
            d["lowest"] = lo;
            d["highest"] = hi;
            d["materials_percent"] = mats;
            return d;
        };
        auto material = [&](TerrainMaterial& m) -> bool {
            m = TerrainMaterial::Grass;
            if (!args.contains("material")) return true;
            return args["material"].is_string() && Terrain::parseMaterial(args["material"].get<std::string>(), m);
        };
        auto badMaterial = [&]() {
            json choices(std::vector<std::string>(std::begin(kTerrainMaterialNames), std::end(kTerrainMaterialNames)));
            return fail("INVALID_ENUM_VALUE", args["material"].dump() + " isn't a terrain material.",
                        "Use one of valid_choices (water is a part: action water).", {{"valid_choices", choices}});
        };
        glm::vec3 pos(0.0f);
        const bool hasPos = args.contains("position") && readVec3(args["position"], pos);
        auto needTerrain = [&]() { return fail("NO_TERRAIN", "There's no terrain yet.", "Make some first: terrain generate or terrain flat."); };

        if (action == "info") return done(summary());
        if (action == "clear") { t.clear(); return done(summary()); }
        if (action == "generate") {
            const float size = args.contains("size") && args["size"].is_number() ? args["size"].get<float>() : 512.0f;
            const int cells = std::clamp((int)std::round(size / Terrain::kDefaultCell), Terrain::kMinCells, Terrain::kMaxCells);
            const float hills = std::clamp(args.value("hills", 0.5f), 0.0f, 1.0f);
            t.generate(cells, Terrain::kDefaultCell, (uint32_t)std::max(0, args.value("seed", 1)), hills);
            return done(summary());
        }
        if (action == "flat") {
            TerrainMaterial m;
            if (!material(m)) return badMaterial();
            const float size = args.contains("size") && args["size"].is_number() ? args["size"].get<float>() : 512.0f;
            const int cells = std::clamp((int)std::round(size / Terrain::kDefaultCell), Terrain::kMinCells, Terrain::kMaxCells);
            t.create(cells, Terrain::kDefaultCell, args.value("height", 0.0f), m);
            return done(summary());
        }
        if (action == "fill_box" || action == "dig_box") {
            glm::vec3 size;
            if (!hasPos || !args.contains("size") || !readVec3(args["size"], size))
                return fail("INVALID_INPUT", "Give position (the box's centre, [x, y, z]) and size [x, y, z].", "Like {position: [0, 5, 0], size: [40, 10, 40]}.");
            size = glm::abs(size);
            const glm::vec3 mn = pos - size * 0.5f, mx = pos + size * 0.5f;
            if (action == "fill_box") {
                TerrainMaterial m;
                if (!material(m)) return badMaterial();
                if (t.empty()) t.create(128, Terrain::kDefaultCell, mn.y, m);
                t.fillBox(mn, mx, m);
            } else {
                if (t.empty()) return needTerrain();
                t.digBox(mn, mx);
            }
            return done(summary());
        }
        if (action == "fill_ball" || action == "dig_ball") {
            const float r = args.value("radius", 0.0f);
            if (!hasPos || r <= 0.0f) return fail("INVALID_INPUT", "Give position [x, y, z] and radius (studs, more than 0).", "Like {position: [0, 0, 0], radius: 20}.");
            if (action == "fill_ball") {
                TerrainMaterial m;
                if (!material(m)) return badMaterial();
                if (t.empty()) t.create(128, Terrain::kDefaultCell, pos.y - r, m);
                t.fillBall(pos, r, m);
            } else {
                if (t.empty()) return needTerrain();
                t.digBall(pos, r);
            }
            return done(summary());
        }
        if (action == "brush") {
            if (t.empty()) return needTerrain();
            static const std::map<std::string, Terrain::Brush> brushes = {
                {"raise", Terrain::Brush::Raise}, {"lower", Terrain::Brush::Lower}, {"smooth", Terrain::Brush::Smooth},
                {"flatten", Terrain::Brush::Flatten}, {"paint", Terrain::Brush::Paint}};
            auto it = brushes.find(canon(str("brush")));
            if (it == brushes.end())
                return fail("INVALID_ENUM_VALUE", "brush must be Raise, Lower, Smooth, Flatten or Paint.", "Pick one of valid_choices.",
                            {{"valid_choices", json::array({"Raise", "Lower", "Smooth", "Flatten", "Paint"})}});
            TerrainMaterial m;
            if (!material(m)) return badMaterial();
            const float radius = std::max(1.0f, args.value("radius", 12.0f));
            const bool level = it->second == Terrain::Brush::Smooth || it->second == Terrain::Brush::Flatten;
            const float strength = args.value("strength", level ? 1.0f : 4.0f);
            std::vector<glm::vec3> line;
            if (args.contains("points") && args["points"].is_array()) {
                for (const json& p : args["points"]) {
                    glm::vec3 v;
                    if (readVec3(p, v)) line.push_back(v);
                    else if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number()) {
                        v = glm::vec3(p[0].get<float>(), 0.0f, p[1].get<float>());
                        float h;
                        if (t.heightAt(v.x, v.z, h)) v.y = h;
                        line.push_back(v);
                    } else return fail("INVALID_INPUT", "Each point must be [x, y, z] or [x, z].", "Fix points.");
                }
            } else if (hasPos) line.push_back(pos);
            if (line.empty()) return fail("INVALID_INPUT", "Give position [x, y, z] or points [[x, y, z]...].", "Where the brush goes.");
            int dabs = 0;
            auto dab = [&](const glm::vec3& c) { t.brush(it->second, c, radius, strength, m); ++dabs; };
            dab(line[0]);
            for (size_t i = 1; i < line.size(); ++i) {   // a dab every third of a radius along the line
                const float len = glm::length(line[i] - line[i - 1]);
                const int steps = std::max(1, (int)std::ceil(len / (radius / 3.0f)));
                for (int s = 1; s <= steps && dabs < 5000; ++s) dab(glm::mix(line[i - 1], line[i], (float)s / steps));
            }
            json d = summary();
            d["dabs"] = dabs;
            return done(d);
        }
        if (action == "height_at") {
            if (!args.contains("points") || !args["points"].is_array() || args["points"].empty())
                return fail("INVALID_INPUT", "Give points: [[x, z], ...] (or [x, y, z]; y is ignored).", "Up to 200 points.");
            if (args["points"].size() > 200) return fail("INVALID_INPUT", "Up to 200 points at a time.", "Split them up.");
            json heights = json::array();
            for (const json& p : args["points"]) {
                if (!p.is_array() || (p.size() != 2 && p.size() != 3) || !p[0].is_number() || !p.back().is_number())
                    return fail("INVALID_INPUT", "Each point must be [x, z] or [x, y, z].", "Fix points.");
                const float x = p[0].get<float>(), z = p.back().get<float>();
                float h;
                json one = {{"x", x}, {"z", z}};
                if (!t.empty() && t.heightAt(x, z, h)) {
                    one["height"] = h;
                    one["material"] = kTerrainMaterialNames[(int)t.materialAt(x, z)];
                } else one["height"] = nullptr;
                heights.push_back(one);
            }
            json d = summary();
            d["heights"] = heights;
            if (t.empty()) d["note"] = "There's no terrain: the ground is whatever parts are there (the baseplate's top is about 0).";
            return done(d);
        }
        if (action == "water") {
            float lo = 0.0f;
            glm::vec2 size(t.empty() ? 128.0f : t.halfWidth() * 2.0f), at(0.0f);
            if (!t.empty()) {
                lo = 1e30f;
                for (int k = 0; k < t.points(); ++k)
                    for (int i = 0; i < t.points(); ++i) lo = std::min(lo, t.height(i, k));
            }
            const float level = args.value("level", lo + 3.0f);
            if (args.contains("size") && args["size"].is_array() && args["size"].size() == 2 && args["size"][0].is_number() && args["size"][1].is_number())
                size = glm::vec2(args["size"][0].get<float>(), args["size"][1].get<float>());
            if (args.contains("position") && args["position"].is_array() && args["position"].size() >= 2 && args["position"][0].is_number())
                at = glm::vec2(args["position"][0].get<float>(), args["position"].back().get<float>());
            const float depth = std::max(2.0f, level - lo + 2.0f);
            AiToolResult w = runAiTool("insert_object", {{"kind", "Water"}, {"name", "Water"}});
            SceneNode* n = byRef(w);
            if (!n) return passOn(w);
            n->transform.position = glm::vec3(at.x, level - depth * 0.5f, at.y);
            n->transform.scale = glm::vec3(std::max(4.0f, size.x), depth, std::max(4.0f, size.y));
            json d = summary();
            d["object_id"] = idOf(n);
            d["water"] = {{"top", level}, {"size", vec(n->transform.scale)}, {"position", vec(n->transform.position)}};
            return done(d);
        }
        return fail("INVALID_ENUM_VALUE", "action must be one of valid_choices.", "Pick an action.",
                    {{"valid_choices", json::array({"info", "generate", "flat", "fill_box", "dig_box", "fill_ball", "dig_ball",
                                                    "brush", "height_at", "water", "clear"})}});
    }

    // --------------------------------------------------------------- create_ui
    // Each object: insert_object, then set_property for each prop (the same checks and names).
    json made = json::array(), problems = json::array();
    std::function<SceneNode*(const json&, const std::string&)> makeUi = [&](const json& spec, const std::string& parent) -> SceneNode* {
        if (!spec.is_object() || !spec.contains("class") || !spec["class"].is_string()) {
            problems.push_back({{"error", "Each UI object needs a class (like Frame or TextLabel)."}, {"got", spec.dump().substr(0, 200)}});
            return nullptr;
        }
        const std::string uiName = spec.value("name", spec["class"].get<std::string>());
        json insertArgs = {{"kind", spec["class"]}, {"name", uiName}};
        if (!parent.empty()) insertArgs["parent"] = parent;
        AiToolResult r = runAiTool("insert_object", insertArgs);
        SceneNode* n = byRef(r);
        if (!n) {
            json p = {{"name", uiName}, {"error", r.data.contains("error") ? r.data["error"].value("message", std::string("couldn't make it")) : std::string("couldn't make it")}};
            if (r.data.contains("error") && r.data["error"].contains("did_you_mean")) p["did_you_mean"] = r.data["error"]["did_you_mean"];
            problems.push_back(p);
            return nullptr;
        }
        made.push_back(brief(n));
        made.back()["class"] = r.data.value("class", std::string());
        if (spec.contains("props") && spec["props"].is_object())
            for (auto& [prop, value] : spec["props"].items()) {
                AiToolResult s = runAiTool("set_property", {{"object", idOf(n)}, {"property", prop}, {"value", value}});
                if (s.error) {
                    json p = {{"name", uiName}, {"object_id", idOf(n)}, {"property", prop},
                              {"error", s.data["error"].value("message", std::string("couldn't set it"))}};
                    if (s.data["error"].contains("valid_choices") && s.data["error"]["valid_choices"].size() <= 40)
                        p["valid_choices"] = s.data["error"]["valid_choices"];
                    problems.push_back(p);
                }
            }
        if (spec.contains("children") && spec["children"].is_array())
            for (const json& c : spec["children"]) makeUi(c, idOf(n));
        return n;
    };
    if (name == "create_ui") {
        if (!args.contains("ui") || (!args["ui"].is_object() && !args["ui"].is_array()))
            return fail("INVALID_INPUT", "`ui` is missing: give {class, name, props, children}.", "Start with a ScreenGui: {class: \"ScreenGui\", children: [...]}.");
        const std::string parent = str("parent").empty() ? std::string("StarterGui") : str("parent");
        const json tops = args["ui"].is_array() ? args["ui"] : json::array({args["ui"]});
        SceneNode* first = nullptr;
        for (const json& top : tops) {
            SceneNode* n = makeUi(top, parent);
            if (!first) first = n;
        }
        if (!first) {
            json d = {{"success", false}, {"operation", name},
                      {"error", {{"code", "COULD_NOT_INSERT"}, {"message", "Nothing could be made."}, {"recoverable", true},
                                 {"suggested_action", "Read problems, fix the classes and try again."}}},
                      {"problems", problems}};
            AiToolResult r;
            r.text = d.dump(1);
            r.error = true;
            r.data = d;
            return r;
        }
        json d = {{"object_id", idOf(first)}, {"made", made}, {"problems", problems}};
        if (!problems.empty())
            d["warnings"] = json::array({std::to_string(problems.size()) + " thing(s) couldn't be set: see problems (the rest was made)."});
        return done(d);
    }

    // ------------------------------------------------------------ set_atmosphere
    if (name == "set_atmosphere") {
        struct Mood { double clock, bright; glm::vec3 ambient, sun; bool fog; double fogDensity; glm::vec3 fogColor; const char* weather; double amount; };
        static const std::map<std::string, Mood> moods = {
            {"day",     {14.0, 2.0, {0.5f, 0.5f, 0.5f}, {1.0f, 0.97f, 0.9f}, false, 0.0, {0.75f, 0.8f, 0.88f}, "Clear", 0.0}},
            {"morning", {7.5, 1.6, {0.45f, 0.45f, 0.5f}, {1.0f, 0.88f, 0.72f}, true, 0.004, {0.85f, 0.85f, 0.9f}, "Clear", 0.0}},
            {"sunset",  {18.0, 1.4, {0.45f, 0.36f, 0.36f}, {1.0f, 0.62f, 0.35f}, true, 0.004, {0.95f, 0.65f, 0.5f}, "Clear", 0.0}},
            {"night",   {0.0, 0.4, {0.16f, 0.17f, 0.25f}, {0.6f, 0.65f, 0.85f}, false, 0.0, {0.1f, 0.1f, 0.18f}, "Clear", 0.0}},
            {"foggy",   {11.0, 1.0, {0.5f, 0.5f, 0.52f}, {0.9f, 0.9f, 0.9f}, true, 0.03, {0.72f, 0.74f, 0.76f}, "Clear", 0.0}},
            {"spooky",  {23.0, 0.3, {0.18f, 0.12f, 0.22f}, {0.55f, 0.45f, 0.75f}, true, 0.025, {0.12f, 0.08f, 0.16f}, "Clear", 0.0}},
            {"rainy",   {13.0, 0.8, {0.38f, 0.4f, 0.45f}, {0.7f, 0.72f, 0.78f}, true, 0.012, {0.5f, 0.53f, 0.58f}, "Rain", 0.8}},
            {"snowy",   {12.0, 1.2, {0.55f, 0.56f, 0.62f}, {0.9f, 0.92f, 1.0f}, true, 0.01, {0.85f, 0.88f, 0.93f}, "Snow", 0.7}},
            {"space",   {0.0, 1.5, {0.1f, 0.1f, 0.14f}, {1.0f, 1.0f, 1.0f}, false, 0.0, {0.0f, 0.0f, 0.02f}, "Clear", 0.0}},
        };
        std::string lua = "local L = game:GetService(\"Lighting\")\n";
        const size_t header = lua.size();
        const std::string preset = str("preset");
        if (!preset.empty()) {
            auto it = moods.find(canon(preset));
            if (it == moods.end()) {
                json choices = json::array();
                for (auto& [k, v] : moods) choices.push_back(k);
                return fail("INVALID_ENUM_VALUE", "\"" + preset + "\" isn't a preset.", "Use one of valid_choices.", {{"valid_choices", choices}});
            }
            const Mood& m = it->second;
            lua += "L.ClockTime = " + num(m.clock) + "\nL.Brightness = " + num(m.bright) + "\nL.Ambient = " + luaColor(m.ambient) +
                   "\nL.SunColor = " + luaColor(m.sun) + "\nL.FogEnabled = " + (m.fog ? "true" : "false") +
                   "\nL.FogDensity = " + num(m.fogDensity) + "\nL.FogColor = " + luaColor(m.fogColor) +
                   "\nL.Weather = \"" + m.weather + "\"\nL.WeatherIntensity = " + num(m.amount) + "\n";
        }
        auto colorArg = [&](const char* k, const char* prop) -> bool {
            if (!args.contains(k)) return true;
            glm::vec3 c;
            if (!readColor(args[k], c)) return false;
            lua += std::string("L.") + prop + " = " + luaColor(c) + "\n";
            return true;
        };
        if (args.contains("clock_time")) lua += "L.ClockTime = " + num(std::fmod(std::max(0.0, args.value("clock_time", 14.0)), 24.0)) + "\n";
        if (args.contains("brightness")) lua += "L.Brightness = " + num(std::max(0.0, args.value("brightness", 2.0))) + "\n";
        if (args.contains("fog")) lua += std::string("L.FogEnabled = ") + (args.value("fog", false) ? "true" : "false") + "\n";
        if (args.contains("fog_density")) lua += "L.FogDensity = " + num(std::clamp(args.value("fog_density", 0.01), 0.0, 1.0)) + "\n";
        if (args.contains("weather")) {
            lua += "L.Weather = " + luaQuote(str("weather")) + "\n";
            if (!args.contains("weather_intensity") && canon(str("weather")) != "clear") lua += "if L.WeatherIntensity < 0.1 then L.WeatherIntensity = 0.7 end\n";
        }
        if (args.contains("weather_intensity")) lua += "L.WeatherIntensity = " + num(std::clamp(args.value("weather_intensity", 0.5), 0.0, 1.0)) + "\n";
        for (auto [k, prop] : {std::pair<const char*, const char*>{"ambient", "Ambient"}, {"sun_color", "SunColor"}, {"fog_color", "FogColor"}})
            if (!colorArg(k, prop)) return fail("INVALID_INPUT", std::string("`") + k + "` isn't a colour.", "Use [r, g, b] 0-1, \"#rrggbb\" or a name.");
        if (lua.size() == header)
            return fail("INVALID_INPUT", "Nothing to change: give a preset or some values.", "Like {preset: \"sunset\"} or {clock_time: 20}.");
        lua += "print(\"\\1ATMO\" .. L.ClockTime .. \"|\" .. L.Brightness .. \"|\" .. tostring(L.FogEnabled) .. \"|\" .. L.FogDensity .. \"|\" .. "
               "tostring(L.Weather) .. \"|\" .. L.WeatherIntensity)\n";
        std::vector<Log::Entry> got;
        Log::setCapture(&got);
        runCommand(lua);
        Log::setCapture(nullptr);
        json now;
        for (const Log::Entry& e : got) {
            if (e.level == Log::Level::Error)
                return fail("LUA_ERROR", "Couldn't change Lighting: " + e.text, "Check the values (weather: Clear, Rain or Snow).");
            if (e.text.rfind("\1ATMO", 0) == 0) {
                std::vector<std::string> f;
                std::stringstream ss(e.text.substr(5));
                for (std::string x; std::getline(ss, x, '|');) f.push_back(x);
                if (f.size() == 6)
                    now = {{"ClockTime", std::atof(f[0].c_str())}, {"Brightness", std::atof(f[1].c_str())}, {"FogEnabled", f[2] == "true"},
                           {"FogDensity", std::atof(f[3].c_str())}, {"Weather", f[4]}, {"WeatherIntensity", std::atof(f[5].c_str())}};
            }
        }
        json d = {{"lighting", now}};
        if (!preset.empty()) d["preset"] = canon(preset);
        return done(d);
    }

    // ----------------------------------------------------------- add_game_system
    if (name == "add_game_system") {
        const std::string system = str("system");
        static const std::vector<std::string> systems = {"leaderboard", "teams", "rounds", "collectibles", "checkpoints", "shop"};
        if (std::find(systems.begin(), systems.end(), system) == systems.end())
            return fail("INVALID_ENUM_VALUE", "\"" + system + "\" isn't a game system.", "Use one of valid_choices.", {{"valid_choices", systems}});
        json notes = json::array(), replaced = json::array(), settings = json::object();
        made = json::array();
        problems = json::array();

        // Take away what an earlier call made for this system (tagged GameSystem:<system>).
        auto removeSystem = [&](const std::string& sys) {
            const std::string tag = kSystemTagPrefix + sys;
            std::vector<SceneNode*> old;
            m_scene->forEach([&](SceneNode* n) {
                if (std::find(n->tags.begin(), n->tags.end(), tag) != n->tags.end()) old.push_back(n);
            });
            std::set<SceneNode*> set(old.begin(), old.end());
            for (SceneNode* n : old) {
                bool inside = false;
                for (SceneNode* p = n->parent; p; p = p->parent) if (set.count(p)) inside = true;
                if (inside) continue;
                replaced.push_back(n->name);
            }
            for (SceneNode* n : old) {
                bool inside = false;
                for (SceneNode* p = n->parent; p; p = p->parent) if (set.count(p)) inside = true;
                if (!inside) m_scene->removeNode(n);
            }
        };
        auto mark = [&](SceneNode* n, const std::string& sys) {
            if (n) n->tags.push_back(kSystemTagPrefix + sys);
        };
        // A script for this system (made with create_script, so its syntax is checked).
        auto addScript = [&](const std::string& sys, const std::string& parent, const std::string& scriptName,
                             const char* type, const std::string& source) -> SceneNode* {
            AiToolResult r = runAiTool("create_script", {{"parent", parent}, {"name", scriptName}, {"type", type}, {"source", source}});
            SceneNode* n = byRef(r);
            if (!n) { problems.push_back({{"name", scriptName}, {"error", r.text}}); return nullptr; }
            mark(n, sys);
            json b = brief(n);
            b["class"] = type;
            made.push_back(b);
            if (r.data.contains("syntax") && !r.data["syntax"].value("ok", true))
                problems.push_back({{"name", scriptName}, {"error", "syntax: " + r.data["syntax"].value("error", std::string())}});
            return n;
        };
        auto addObject = [&](const std::string& sys, const std::string& kind, const std::string& parent, const std::string& objName) -> SceneNode* {
            AiToolResult r = runAiTool("insert_object", {{"kind", kind}, {"parent", parent}, {"name", objName}});
            SceneNode* n = byRef(r);
            if (!n) { problems.push_back({{"name", objName}, {"error", r.data.contains("error") ? r.data["error"].value("message", std::string()) : r.text}}); return nullptr; }
            mark(n, sys);
            json b = brief(n);
            b["class"] = kind;
            made.push_back(b);
            return n;
        };
        auto findTagged = [&](const std::string& sys) {
            SceneNode* found = nullptr;
            m_scene->forEach([&](SceneNode* n) {
                if (!found && std::find(n->tags.begin(), n->tags.end(), kSystemTagPrefix + sys) != n->tags.end()) found = n;
            });
            return found;
        };
        // Systems that use a leaderboard number add the leaderboard if the game has none.
        auto ensureLeaderboard = [&](const std::vector<std::string>& stats) {
            if (findTagged("leaderboard")) return;
            // Another script that makes leaderstats (not one of these systems, which only use it)?
            bool other = false;
            m_scene->forEach([&](SceneNode* n) {
                if (!n->isScript() || n->source.find("Instance.new(\"Folder\")") == std::string::npos ||
                    n->source.find("leaderstats") == std::string::npos) return;
                const bool ours = std::any_of(n->tags.begin(), n->tags.end(), [](const std::string& t) { return t.rfind(kSystemTagPrefix, 0) == 0; });
                if (!ours) other = true;
            });
            if (other) { notes.push_back("The game already has a script that makes leaderstats, so no Leaderboard was added."); return; }
            addScript("leaderboard", "ServerScriptService", "Leaderboard", "Script", leaderboardSource(stats, false));
            notes.push_back("Added a Leaderboard (" + json(stats).dump() + ") because this system uses its numbers.");
        };
        auto strList = [&](const char* k, std::vector<std::string> fallback) {
            if (!args.contains(k) || !args[k].is_array()) return fallback;
            std::vector<std::string> out;
            for (const json& s : args[k])
                if (s.is_string() && !s.get<std::string>().empty() && out.size() < 10) out.push_back(s.get<std::string>());
            return out.empty() ? fallback : out;
        };

        if (system == "leaderboard") {
            const std::vector<std::string> stats = strList("stats", {"Coins"});
            for (const std::string& s : stats)
                if (s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_ ") != std::string::npos)
                    return fail("INVALID_INPUT", "\"" + s + "\" can't be a leaderboard name (letters, numbers, spaces and _ only).", "Use names like Coins, Wins, Kills.");
            removeSystem("leaderboard");
            const bool save = args.value("save", false);
            addScript("leaderboard", "ServerScriptService", "Leaderboard", "Script", leaderboardSource(stats, save));
            settings = {{"stats", stats}, {"save", save}};
            notes.push_back("Other scripts change a number like: player.leaderstats." + stats[0] + ".Value = player.leaderstats." + stats[0] + ".Value + 1");
            if (save) notes.push_back("Saving works in published games (and is kept on this computer while testing).");
        } else if (system == "teams") {
            struct TeamSpec { std::string name; glm::vec3 color; };
            std::vector<TeamSpec> teams;
            if (args.contains("teams") && args["teams"].is_array()) {
                for (const json& t : args["teams"]) {
                    TeamSpec s;
                    if (t.is_string()) s.name = t.get<std::string>();
                    else if (t.is_object()) s.name = t.value("name", std::string());
                    if (s.name.empty()) return fail("INVALID_INPUT", "Each team needs a name.", "teams: [{name: \"Red\", color: \"red\"}, ...]");
                    glm::vec3 c;
                    if (t.is_object() && t.contains("color")) {
                        if (!readColor(t["color"], c)) return fail("INVALID_INPUT", "Team " + s.name + ": " + t["color"].dump() + " isn't a colour.", "Use a name like \"red\", [r, g, b] or \"#rrggbb\".");
                    } else if (!readColor(json(canon(s.name)), c)) {
                        static const glm::vec3 defaults[] = {{0.85f, 0.15f, 0.15f}, {0.15f, 0.35f, 0.9f}, {0.2f, 0.7f, 0.25f}, {0.95f, 0.8f, 0.15f},
                                                             {0.6f, 0.25f, 0.8f}, {0.95f, 0.5f, 0.1f}, {0.2f, 0.8f, 0.85f}, {0.95f, 0.5f, 0.7f}};
                        c = defaults[teams.size() % 8];
                    }
                    s.color = c;
                    teams.push_back(s);
                }
            } else teams = {{"Red", {0.85f, 0.15f, 0.15f}}, {"Blue", {0.15f, 0.35f, 0.9f}}};
            if (teams.size() < 2 || teams.size() > 8) return fail("INVALID_INPUT", "Give 2 to 8 teams.", "teams: [{name, color}, ...]");
            for (size_t i = 0; i < teams.size(); ++i)
                for (size_t j = i + 1; j < teams.size(); ++j)
                    if (glm::length(teams[i].color - teams[j].color) < 0.03f)
                        return fail("INVALID_INPUT", teams[i].name + " and " + teams[j].name + " have the same colour.",
                                    "Every team needs its own colour: players spawn on SpawnLocations of their team's colour.");
            removeSystem("teams");
            SceneNode* folder = m_scene->serviceFolder("Teams", true);
            glm::vec3 at(0.0f, 0.0f, 0.0f);
            if (args.contains("at")) readVec3(args["at"], at);
            const float spread = std::max(6.0f, args.value("spread", 30.0f));
            const bool spawns = args.value("spawns", true);
            json list = json::array();
            for (size_t i = 0; i < teams.size(); ++i) {
                auto team = std::make_unique<SceneNode>(teams[i].name, NodeKind::Model);
                Attribute cls; cls.name = "RobloxClass"; cls.type = Attribute::String; cls.s = "Team";
                Attribute col; col.name = "TeamColor"; col.type = Attribute::Color3; col.v = teams[i].color;
                Attribute aa; aa.name = "AutoAssignable"; aa.type = Attribute::Bool; aa.b = true;
                team->attributes = {cls, col, aa};
                team->tags.push_back(std::string(kSystemTagPrefix) + "teams");
                SceneNode* n = m_scene->insert(std::move(team), folder);
                json b = brief(n);
                b["class"] = "Team";
                made.push_back(b);
                json one = {{"name", teams[i].name}, {"color", vec(teams[i].color)}, {"object_id", idOf(n)}};
                if (spawns) {
                    SceneNode* s = addObject("teams", "SpawnLocation", "Workspace", teams[i].name + "Spawn");
                    if (s) {
                        const float angle = 6.2831853f * (float)i / (float)teams.size();
                        const float radius = teams.size() == 2 ? spread * 0.5f : spread * 0.5f / std::sin(3.14159265f / (float)teams.size());
                        float ground = at.y;
                        glm::vec3 p = at + glm::vec3(std::sin(angle) * radius, 0.0f, std::cos(angle) * radius);
                        if (m_scene->terrain().heightAt(p.x, p.z, ground)) p.y = std::max(p.y, ground);
                        s->transform.position = p + glm::vec3(0.0f, 0.1f, 0.0f);
                        s->transform.scale = {6.0f, 0.4f, 6.0f};
                        s->color = teams[i].color;
                        s->teamColor = teams[i].color;
                        s->spawnNeutral = false;
                        one["spawn_id"] = idOf(s);
                        one["spawn_position"] = vec(s->transform.position);
                    }
                }
                list.push_back(one);
            }
            // The neutral spawn (the default one) would take players from every team: say so.
            int neutral = 0;
            m_scene->forEach([&](SceneNode* n) { if (n->isSpawnLocation() && n->spawnNeutral) ++neutral; });
            if (spawns && neutral > 0)
                notes.push_back(std::to_string(neutral) + " neutral SpawnLocation(s) are still in the game: anyone can spawn there. Delete them or "
                                "set Neutral false if every player should start at their team's spawn.");
            notes.push_back("Players are put on the team with the fewest people when they join. Scripts read player.Team.Name.");
            settings = {{"teams", list}, {"spawns", spawns}};
        } else if (system == "rounds") {
            RoundSettings r;
            r.intermission = std::clamp(args.value("intermission", 15.0), 1.0, 600.0);
            r.roundTime = std::clamp(args.value("round_time", 60.0), 5.0, 3600.0);
            r.minPlayers = std::clamp(args.value("min_players", 1), 1, 100);
            r.mode = str("mode").empty() ? "survival" : str("mode");
            if (r.mode != "survival" && r.mode != "most_points")
                return fail("INVALID_ENUM_VALUE", "mode must be survival or most_points.", "Pick one.", {{"valid_choices", json::array({"survival", "most_points"})}});
            r.winStat = "Wins";
            r.pointsStat = str("stat").empty() ? (r.mode == "most_points" ? std::string("Kills") : std::string("Wins")) : str("stat");
            if (r.mode == "survival" && !str("stat").empty()) r.winStat = str("stat");
            if (!str("map").empty()) {
                SceneNode* map = aiFind(str("map"));
                if (!map) return fail("OBJECT_NOT_FOUND", "No object " + str("map") + " exists.", "Build the map first (build {parent: \"ServerStorage\"}) and give its object_id.");
                if (map->kind != NodeKind::Model) return fail("NOT_A_MODEL", map->name + " isn't a Model.", "The map must be a Model (build makes one).");
                if (map->parent != root && !(m_scene->isServiceFolder(map->parent) && map->parent->parent == root))
                    return fail("INVALID_INPUT", "The map must sit right in Workspace, ServerStorage or ReplicatedStorage.", "Move it with set_property Parent.");
                r.mapName = map->name;
                r.mapFrom = map->parent == root ? "Workspace" : map->parent->name;
                bool spawnsInMap = false;
                std::function<void(SceneNode*)> look = [&](SceneNode* n) {
                    if (n->name == "ArenaSpawn" && n->isPart()) spawnsInMap = true;
                    for (auto& c : n->children) look(c.get());
                };
                look(map);
                if (!spawnsInMap)
                    notes.push_back("The map has no parts called ArenaSpawn, so players aren't moved when a round starts. Add some "
                                    "(build {into: \"" + idOf(map) + "\", parts: [{name: \"ArenaSpawn\", transparency: 1, can_collide: false, ...}]}).");
                if (r.mapFrom == "Workspace")
                    notes.push_back("The map is in Workspace, so it's always there. Put it in ServerStorage to copy a fresh one in for each round.");
            } else notes.push_back("No map: rounds happen wherever the players are. Give map (a Model in ServerStorage) to copy an arena in each round.");
            removeSystem("rounds");
            SceneNode* status = addObject("rounds", "StringValue", "ReplicatedStorage", "RoundStatus");
            if (status) runAiTool("set_property", {{"object", idOf(status)}, {"property", "Value"}, {"value", "Waiting for players"}});
            addScript("rounds", "ServerScriptService", "RoundSystem", "Script", roundsSource(r));
            json ui = {{"class", "ScreenGui"}, {"name", "RoundStatusGui"}, {"props", {{"ResetOnSpawn", false}}},
                       {"children", json::array({{{"class", "TextLabel"}, {"name", "Status"},
                           {"props", {{"Size", json::array({0.5, 0, 0, 40})}, {"Position", json::array({0.25, 0, 0, 8})},
                                      {"BackgroundColor3", json::array({0.08, 0.08, 0.1})}, {"BackgroundTransparency", 0.35},
                                      {"TextColor3", json::array({1, 1, 1})}, {"TextScaled", true}, {"Text", "Waiting for players"}}},
                           {"children", json::array({{{"class", "UICorner"}}})}}})}};
            SceneNode* gui = makeUi(ui, "StarterGui");
            mark(gui, "rounds");
            if (gui) addScript("rounds", idOf(gui), "ShowStatus", "LocalScript", kRoundStatusClient);
            ensureLeaderboard(r.mode == "most_points" ? std::vector<std::string>{r.pointsStat, r.winStat} : std::vector<std::string>{r.winStat});
            settings = {{"intermission", r.intermission}, {"round_time", r.roundTime}, {"min_players", r.minPlayers}, {"mode", r.mode},
                        {"win_stat", r.winStat}, {"points_stat", r.pointsStat}, {"map", r.mapName}, {"map_from", r.mapFrom}};
            if (r.mode == "most_points")
                notes.push_back("most_points counts how much " + r.pointsStat + " each player gets during the round: your own scripts add to it "
                                "(e.g. +1 Kills when a player knocks someone out).");
        } else if (system == "collectibles") {
            const std::string what = str("name").empty() ? "Coin" : str("name");
            const std::string stat = str("stat").empty() ? "Coins" : str("stat");
            const double amount = args.value("amount", 1.0), respawn = std::max(0.0, args.value("respawn", 10.0));
            removeSystem("collectibles");
            // A ready-made Coin's own script destroys it and keeps its own count: switch those off (this system does both jobs).
            int found = 0, switched = 0;
            m_scene->forEach([&](SceneNode* n) {
                if (n->name != what && std::find(n->tags.begin(), n->tags.end(), what) == n->tags.end()) return;
                ++found;
                for (auto& c : n->children)
                    if (c->isScript() && c->enabled && c->source.rfind("-- Coin:", 0) == 0) { c->enabled = false; ++switched; }
            });
            addScript("collectibles", "ServerScriptService", "Collectibles", "Script", collectiblesSource(what, stat, amount, respawn));
            ensureLeaderboard({stat});
            if (switched) notes.push_back("Switched off the old counting script in " + std::to_string(switched) + " ready-made coin(s); the Collectibles script counts them now.");
            if (!found) notes.push_back("There's nothing called " + what + " yet: add some (insert_object \"Coin\", or build parts named " + what + ").");
            settings = {{"name", what}, {"stat", stat}, {"amount", amount}, {"respawn", respawn}, {"found", found}};
        } else if (system == "checkpoints") {
            const std::string what = str("name").empty() ? "Checkpoint" : str("name");
            const std::string stat = str("stat").empty() ? "Stage" : str("stat");
            const bool save = args.value("save", false);
            removeSystem("checkpoints");
            int found = 0;
            m_scene->forEach([&](SceneNode* n) { if (n->name == what && n->isPart()) ++found; });
            addScript("checkpoints", "ServerScriptService", "Checkpoints", "Script", checkpointsSource(what, stat, save));
            ensureLeaderboard({stat});
            if (!found) notes.push_back("There are no parts called " + what + " yet: add some (insert_object \"Checkpoint\").");
            if (what != "Checkpoint") notes.push_back("Only parts called Checkpoint set where you respawn by themselves; others only count stages.");
            notes.push_back("To choose the order, give each checkpoint a Stage attribute (1, 2, 3...).");
            settings = {{"name", what}, {"stat", stat}, {"save", save}, {"found", found}};
        } else if (system == "shop") {
            const std::string currency = str("currency").empty() ? "Coins" : str("currency");
            if (!args.contains("items") || !args["items"].is_array() || args["items"].empty() || args["items"].size() > 20)
                return fail("INVALID_INPUT", "Give 1-20 items: [{name, price, item}].",
                            "item is a Tool (\"#42\") or a ready-made gear name like \"Classic Sword\".");
            struct Want { std::string name; int price; std::string item; SceneNode* tool = nullptr; };
            std::vector<Want> wants;
            for (const json& it : args["items"]) {
                if (!it.is_object()) return fail("INVALID_INPUT", "Each item must be {name, price, item}.", "Fix items.");
                Want w;
                w.item = it.value("item", std::string());
                w.name = it.value("name", w.item);
                w.price = it.contains("price") && it["price"].is_number() ? (int)it["price"].get<double>() : -1;
                if (w.name.empty() || w.price < 0)
                    return fail("INVALID_INPUT", "Each item needs a name and a price (0 or more).", "Like {name: \"Sword\", price: 10, item: \"Classic Sword\"}.");
                if (w.item.empty()) w.item = w.name;
                if (w.item[0] == '#' || w.item.find('.') != std::string::npos) {
                    w.tool = aiFind(w.item);
                    if (!w.tool) return fail("OBJECT_NOT_FOUND", "No object " + w.item + " exists.", "Give a Tool's object_id, or a ready-made gear name.");
                    if (!w.tool->isTool()) return fail("NOT_A_TOOL", w.tool->name + " isn't a Tool: only Tools can be sold.", "insert_object Tool first.");
                }
                wants.push_back(w);
            }
            // Ready-made gear: check every name before changing anything.
            const std::vector<std::string> kinds = insertKinds();
            for (Want& w : wants) {
                if (w.tool) continue;
                std::string fixed;
                for (const std::string& k : kinds) if (canon(k) == canon(w.item)) fixed = k;
                if (fixed.empty())
                    return fail("UNKNOWN_CLASS", "\"" + w.item + "\" isn't a Tool in the game or a ready-made gear.",
                                "Give a Tool's object_id, or one of: Classic Sword, Rocket Launcher, Speed Coil, Gravity Coil, Bomb, Blaster.");
                w.item = fixed;
            }
            removeSystem("shop");
            SceneNode* stock = addObject("shop", "Folder", "ServerStorage", "ShopItems");
            if (!stock) return fail("COULD_NOT_INSERT", "Couldn't make ServerStorage.ShopItems.", "Check problems.", {{"problems", problems}});
            std::vector<ShopItem> items;
            for (Want& w : wants) {
                SceneNode* tool = w.tool;
                if (tool) {
                    if (tool->parent != stock) {
                        if (tool->parent && tool->parent->name == "StarterPack") notes.push_back(tool->name + " was in StarterPack (everyone had it free): it's moved to the shop.");
                        m_scene->reparent(tool, stock);
                    }
                } else {
                    AiToolResult r = runAiTool("insert_object", {{"kind", w.item}, {"parent", idOf(stock)}});
                    tool = byRef(r);
                    if (!tool) { problems.push_back({{"name", w.name}, {"error", r.text}}); continue; }
                    if (!tool->isTool()) {
                        problems.push_back({{"name", w.name}, {"error", w.item + " isn't a Tool, so it can't be sold."}});
                        m_scene->removeNode(tool);
                        continue;
                    }
                    if (tool->parent != stock) m_scene->reparent(tool, stock);   // (ready-made gear lands in the Workspace)
                }
                tool->name = w.name;
                tool->starterTool = false;   // (ready-made gear is "In StarterPack": everyone would get it free)
                items.push_back({w.name, w.price});
            }
            if (items.empty()) return fail("COULD_NOT_INSERT", "None of the items could be added.", "Check problems.", {{"problems", problems}});
            addObject("shop", "RemoteEvent", "ReplicatedStorage", "ShopBuy");
            addObject("shop", "RemoteEvent", "ReplicatedStorage", "ShopResult");
            addScript("shop", "ServerScriptService", "Shop", "Script", shopServerSource(currency, items));
            // Sized in parts of the screen (scale), so it fits a phone as well as a big monitor.
            const double rows = (double)items.size();
            json buttons = json::array();
            for (size_t i = 0; i < items.size(); ++i)
                buttons.push_back({{"class", "TextButton"}, {"name", items[i].name},
                                   {"props", {{"Size", json::array({1, 0, 1.0 / rows, -6})}, {"Position", json::array({0, 0, (double)i / rows, 0})},
                                              {"Text", items[i].name + "  -  " + std::to_string(items[i].price) + " " + currency},
                                              {"BackgroundColor3", json::array({0.22, 0.45, 0.85})}, {"TextColor3", json::array({1, 1, 1})},
                                              {"TextScaled", true}}},
                                   {"children", json::array({{{"class", "UICorner"}}})}});
            const std::string title = str("title").empty() ? std::string("Shop") : str("title");
            const double listHeight = std::min(0.62, 0.14 * rows);
            json ui = {{"class", "ScreenGui"}, {"name", "ShopGui"}, {"props", {{"ResetOnSpawn", false}}},
                {"children", json::array({
                    {{"class", "TextButton"}, {"name", "OpenShop"},
                     {"props", {{"Size", json::array({0, 110, 0, 44})}, {"Position", json::array({0, 12, 0.5, -22})}, {"Text", "Shop"},
                                {"BackgroundColor3", json::array({0.95, 0.65, 0.15})}, {"TextColor3", json::array({0.1, 0.1, 0.1})}, {"TextScaled", true}}},
                     {"children", json::array({{{"class", "UICorner"}}})}},
                    {{"class", "Frame"}, {"name", "Window"},
                     {"props", {{"Size", json::array({0.42, 0, listHeight + 0.3, 0})}, {"AnchorPoint", json::array({0.5, 0.5})},
                                {"Position", json::array({0.5, 0, 0.5, 0})},
                                {"BackgroundColor3", json::array({0.1, 0.1, 0.13})}, {"BackgroundTransparency", 0.1}, {"Visible", false}}},
                     {"children", json::array({
                         {{"class", "UICorner"}},
                         {{"class", "TextLabel"}, {"name", "Title"},
                          {"props", {{"Size", json::array({1, -70, 0.12, 0})}, {"Position", json::array({0, 12, 0.03, 0})}, {"Text", title},
                                     {"BackgroundTransparency", 1}, {"TextColor3", json::array({1, 1, 1})}, {"TextScaled", true},
                                     {"TextXAlignment", "Left"}}}},
                         {{"class", "TextButton"}, {"name", "Close"},
                          {"props", {{"Size", json::array({0, 36, 0, 36})}, {"Position", json::array({1, -44, 0, 8})}, {"Text", "X"},
                                     {"BackgroundColor3", json::array({0.75, 0.2, 0.2})}, {"TextColor3", json::array({1, 1, 1})}, {"TextScaled", true}}}},
                         {{"class", "Frame"}, {"name", "List"},
                          {"props", {{"Size", json::array({1, -24, listHeight / (listHeight + 0.3), 0})}, {"Position", json::array({0, 12, 0.17, 0})},
                                     {"BackgroundTransparency", 1}}},
                          {"children", buttons}},
                         {{"class", "TextLabel"}, {"name", "Message"},
                          {"props", {{"Size", json::array({1, -24, 0.1, 0})}, {"Position", json::array({0, 12, 0.86, 0})}, {"Text", "Buy with " + currency},
                                     {"BackgroundTransparency", 1}, {"TextColor3", json::array({1, 1, 1})}, {"TextScaled", true}}}},
                     })}},
                })}};
            SceneNode* gui = makeUi(ui, "StarterGui");
            mark(gui, "shop");
            if (gui) addScript("shop", idOf(gui), "ShopClient", "LocalScript", kShopClient);
            ensureLeaderboard({currency});
            json sold = json::array();
            for (const ShopItem& i : items) sold.push_back({{"name", i.name}, {"price", i.price}});
            settings = {{"currency", currency}, {"items", sold}};
            notes.push_back("Players need " + currency + " to buy things: add a way to earn them (add_game_system collectibles, or your own script).");
        }
        if (made.empty()) return fail("COULD_NOT_INSERT", "Nothing could be made.", "Check problems.", {{"problems", problems}});
        json d = {{"system", system}, {"made", made}, {"replaced", replaced}, {"settings", settings}, {"notes", notes}};
        if (!problems.empty()) {
            d["problems"] = problems;
            d["warnings"] = json::array({"Some parts couldn't be made: see problems."});
        }
        d["next"] = "playtest start -> get_errors {run: \"latest\"} -> get_output -> playtest stop. read_script shows the code and its settings at the top.";
        return done(d);
    }

    return fail("UNKNOWN_TOOL", "There's no tool called " + name + ".", "Use one of the tools from tools/list.");
}
