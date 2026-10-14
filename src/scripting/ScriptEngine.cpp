#include "ScriptEngine.h"
#include "../core/Pad.h"
#include "../core/Account.h"
#include "../online/OnlineClient.h"
#include "../online/AssetCache.h"
#include "../scene/Serializer.h"
#include <cctype>
#include <sstream>
#include <fstream>
#include "LuaApi.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Log.h"
#include "../scene/Effects.h"
#include "../scene/Blast.h"
#include "../scene/Physics.h"
#include "../core/Audio.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Lua code that builds the friendly Roblox-style globals on top of the C API.
const char* kPrelude = R"LUA(
-- Enum.KeyCode.E -> "E", Enum.Material.Neon -> "Neon", Enum.UserInputType.Keyboard -> "Keyboard"
Enum = setmetatable({}, { __index = function(t, category)
    local e = setmetatable({}, { __index = function(_, name) return name end })
    rawset(t, category, e)
    return e
end })

task = { wait = __gb_wait, spawn = __gb_spawn, delay = __gb_delay, defer = __gb_spawn }
wait, spawn, delay = task.wait, task.spawn, task.delay

-- ColorSequence / NumberSequence: a colour or number that changes along a Trail or
-- Beam. ColorSequence.new(c), .new(c0, c1) or .new({ ColorSequenceKeypoint.new(t, c), ... })
local function keypoint(kind)
    local meta = { __type = kind, __tostring = function(k) return tostring(k.Time) .. " " .. tostring(k.Value) end }
    return { new = function(t, v, envelope)
        return setmetatable({ Time = tonumber(t) or 0, Value = v, Envelope = envelope or 0 }, meta)
    end }
end
ColorSequenceKeypoint, NumberSequenceKeypoint = keypoint("ColorSequenceKeypoint"), keypoint("NumberSequenceKeypoint")
local function sequence(kind, Key)
    local meta = { __type = kind, __tostring = function(q)
        local t = {}
        for i, k in ipairs(q.Keypoints) do t[i] = tostring(k) end
        return table.concat(t, " ")
    end }
    return { new = function(a, b)
        local kp
        if type(a) == "table" and getmetatable(a) == nil then kp = a                 -- a list of keypoints
        elseif b ~= nil then kp = { Key.new(0, a), Key.new(1, b) }                 -- from a to b
        else kp = { Key.new(0, a), Key.new(1, a) } end                              -- the same all along
        return setmetatable({ Keypoints = kp }, meta)
    end }
end
ColorSequence = sequence("ColorSequence", ColorSequenceKeypoint)
NumberSequence = sequence("NumberSequence", NumberSequenceKeypoint)

-- Players are tables with an object behind them (__node), so `player.leaderstats`
-- and `folder.Parent = player` work like Roblox.
local playerNode, setRespawn = __gb_playerNode, __gb_setRespawn
-- Game UI lives in a folder called StarterGui (game.StarterGui, player.PlayerGui).
local function uiFolder()
    local f = workspace:FindFirstChild("StarterGui")
    if not f then
        f = Instance.new("Folder")
        f.Name = "StarterGui"
        f.Parent = workspace
    end
    return f
end
__gb_uiFolder = uiFolder
local joinData = __gb_joinData
local function getJoinData(self) return joinData(rawget(self, "Name")) end
-- player.CameraMode (Enum.CameraMode.Classic / LockFirstPerson), CameraMinZoomDistance and
-- CameraMaxZoomDistance live in the engine (the camera uses them; the host sends a
-- joined player's to them).
local camOpt, setCamOpt = __gb_camOpt, __gb_setCamOpt
local cameraOptions = { CameraMode = true, CameraMinZoomDistance = true, CameraMaxZoomDistance = true }
local playerMeta = {
    __index = function(t, k)
        if k == "PlayerGui" then return uiFolder() end
        if k == "GetJoinData" then return getJoinData end
        if cameraOptions[k] then return camOpt(rawget(t, "Name"), k) end
        return rawget(t, "__node")[k]
    end,
    __newindex = function(t, k, v)
        if cameraOptions[k] then setCamOpt(rawget(t, "Name"), k, v) return end
        if k == "RespawnLocation" and rawget(t, "__local") then setRespawn(v) end
        rawset(t, k, v)
    end,
    __tostring = function(t) return rawget(t, "Name") end,
}
local function makePlayer(name, character, id)
    return setmetatable({ Name = name, DisplayName = name, UserId = id, Character = character,
                          __node = playerNode(name) }, playerMeta)
end
-- A game server machine plays nobody: no LocalPlayer, only the people who join.
local LocalPlayer, playerList = nil, {}
if not __gb_noLocalPlayer then
    LocalPlayer = makePlayer(__gb_playerName, __gb_character, __gb_localUserId or 1)
    rawset(LocalPlayer, "Backpack", __gb_backpack)
    rawset(LocalPlayer, "__local", true)
    playerList = { LocalPlayer }
end
Players = { LocalPlayer = LocalPlayer, PlayerAdded = __gb_playerAdded,
            PlayerRemoving = __gb_playerRemoving }
function Players:GetPlayers()
    local t = {}
    for i, p in ipairs(playerList) do t[i] = p end
    return t
end
function Players:GetPlayerFromCharacter(model)
    if model == nil then return nil end
    for _, p in ipairs(playerList) do if p.Character == model then return p end end
    return nil
end
function Players:FindFirstChild(name)
    for _, p in ipairs(playerList) do if p.Name == name then return p end end
    return nil
end
-- Teams, like Roblox's Teams service: Team objects (Insert Object > Team) in a
-- folder called Teams, each with a TeamColor. Players join the AutoAssignable team
-- with the fewest people; set player.Team to move them.
Teams = { ClassName = "Teams", Name = "Teams" }
function Teams:GetTeams()
    local t, f = {}, workspace:FindFirstChild("Teams")
    if f then
        for _, c in ipairs(f:GetChildren()) do
            if c:GetAttribute("RobloxClass") == "Team" then t[#t + 1] = c end
        end
    end
    return t
end
function Teams:GetPlayersOnTeam(team)
    local t = {}
    for _, p in ipairs(playerList) do if rawget(p, "Team") == team then t[#t + 1] = p end end
    return t
end
local function autoTeam(p)
    local best, fewest = nil, math.huge
    for _, team in ipairs(Teams:GetTeams()) do
        if team:GetAttribute("AutoAssignable") ~= false then
            local n = #Teams:GetPlayersOnTeam(team)
            if n < fewest then best, fewest = team, n end
        end
    end
    if best then
        rawset(p, "Team", best)
        rawset(p, "TeamColor", best:GetAttribute("TeamColor"))
        rawset(p, "Neutral", false)
    else
        rawset(p, "Neutral", true)
    end
end
if LocalPlayer then autoTeam(LocalPlayer) end

-- Used by the engine when people join / leave a multiplayer game.
function __gb_addPlayer(name, character, id, backpack)
    local p = makePlayer(name, character, id)
    rawset(p, "Backpack", backpack)
    table.insert(playerList, p)
    autoTeam(p)
    return p
end
-- RemoteEvents hand over players by UserId. A joined player's computer only knows
-- itself, so anyone else arrives as a plain { Name, UserId } table.
function __gb_playerById(id, name)
    for _, p in ipairs(playerList) do if p.UserId == id then return p end end
    if id == nil or id == 0 then return nil end
    return { Name = name or "", DisplayName = name or "", UserId = id }
end
function __gb_removePlayer(name)
    for i, p in ipairs(playerList) do
        if p.Name == name then table.remove(playerList, i) return p end
    end
    return nil
end

RunService = { Heartbeat = __gb_heartbeat, RenderStepped = __gb_heartbeat,
               Stepped = __gb_heartbeat }
UserInputService = { InputBegan = __gb_inputBegan, InputEnded = __gb_inputEnded }
local isKeyDown = __gb_isKeyDown
function UserInputService:IsKeyDown(key) return isKeyDown(key) end
-- Controllers: one at most (Gamepad1). GamepadEnabled is kept up to date by the engine.
local padState = __gb_padState
UserInputService.GamepadEnabled = false
function UserInputService:IsGamepadButtonDown(pad, key) return isKeyDown(key) end
function UserInputService:GetGamepadConnected(pad) return UserInputService.GamepadEnabled and tostring(pad) == "Gamepad1" end
function UserInputService:GetConnectedGamepads() return UserInputService.GamepadEnabled and { "Gamepad1" } or {} end
UserInputService.GetNavigationGamepads = UserInputService.GetConnectedGamepads
function UserInputService:GetGamepadState(pad)
    -- The sticks and triggers, as InputObjects (Position: x, y for sticks, z for triggers).
    local lx, ly, rx, ry, lt, rt = padState()
    local function obj(key, pos) return { KeyCode = key, UserInputType = "Gamepad1", Position = pos, UserInputState = "Change" } end
    return { obj("Thumbstick1", Vector3.new(lx, ly, 0)), obj("Thumbstick2", Vector3.new(rx, ry, 0)),
             obj("ButtonL2", Vector3.new(0, 0, lt)), obj("ButtonR2", Vector3.new(0, 0, rt)) }
end

-- An event that only passes on some firings: `test` picks them, `out` shapes the arguments.
local function filtered(sig, test, out)
    local s = {}
    function s:Connect(fn)
        return sig:Connect(function(...) if test(...) then fn(out(...)) end end)
    end
    s.connect = s.Connect
    function s:Once(fn)
        local c
        c = sig:Connect(function(...) if test(...) then c:Disconnect() fn(out(...)) end end)
        return c
    end
    function s:Wait()
        while true do
            local r = table.pack(sig:Wait())
            if test(table.unpack(r, 1, r.n)) then return out(table.unpack(r, 1, r.n)) end
        end
    end
    return s
end
function __gb_attrSignal(sig, name)
    return filtered(sig, function(n) return n == name end, function() end)
end

-- CollectionService: tags on objects (obj:AddTag("Enemy"), CollectionService:GetTagged("Enemy")).
local tagAdded, tagRemoved = __gb_tagAdded, __gb_tagRemoved
CollectionService = {}
function CollectionService:GetTagged(tag)
    local t = {}
    for _, d in ipairs(workspace:GetDescendants()) do
        if d:HasTag(tag) then table.insert(t, d) end
    end
    return t
end
function CollectionService:GetAllTags()
    local seen, t = {}, {}
    for _, d in ipairs(workspace:GetDescendants()) do
        for _, tag in ipairs(d:GetTags()) do
            if not seen[tag] then seen[tag] = true table.insert(t, tag) end
        end
    end
    return t
end
function CollectionService:HasTag(obj, tag) return obj:HasTag(tag) end
function CollectionService:AddTag(obj, tag) obj:AddTag(tag) end
function CollectionService:RemoveTag(obj, tag) obj:RemoveTag(tag) end
function CollectionService:GetTags(obj) return obj:GetTags() end
function CollectionService:GetInstanceAddedSignal(tag)
    return filtered(tagAdded, function(_, t) return t == tag end, function(o) return o end)
end
function CollectionService:GetInstanceRemovedSignal(tag)
    return filtered(tagRemoved, function(_, t) return t == tag end, function(o) return o end)
end

-- DataStoreService: save things between visits (coins, levels...). Kept on the
-- Guts&Bolts server for the game's own servers, else in this computer's account
-- folder. GetAsync / IncrementAsync wait for the answer, like Roblox.
local dsStart, dsDone, dsSet = __gb_dsStart, __gb_dsDone, __gb_dsSet
local function dsGet(store, key, kind, delta)
    local ticket = dsStart(kind or "get", store, key, delta or 0)
    while true do
        local done, value = dsDone(ticket)
        if done then return value end
        task.wait()
    end
end
DataStoreService = {}
function DataStoreService:GetDataStore(name, scope)
    local store = tostring(name) .. (scope and ("/" .. tostring(scope)) or "")
    local ds = {}
    function ds:GetAsync(key) return dsGet(store, tostring(key)) end
    function ds:SetAsync(key, value) dsSet(store, tostring(key), value) end
    function ds:RemoveAsync(key)
        local old = dsGet(store, tostring(key))
        dsSet(store, tostring(key), nil)
        return old
    end
    function ds:UpdateAsync(key, fn)
        local new = fn(dsGet(store, tostring(key)))
        if new ~= nil then dsSet(store, tostring(key), new) end
        return new
    end
    function ds:IncrementAsync(key, delta)
        return dsGet(store, tostring(key), "inc", tonumber(delta) or 1)
    end
    return ds
end
DataStoreService.GetOrderedDataStore = DataStoreService.GetDataStore

-- PathfindingService: walking routes on the navigation mesh (every floor a character
-- can stand on, baked from the parts; it re-bakes itself when anchored parts change).
-- Works like Roblox's:
--   local path = PathfindingService:CreatePath({ AgentRadius = 0.6, AgentCanJump = true,
--                                                Costs = { Water = 10, Lava = math.huge } })
--   path:ComputeAsync(npc.HumanoidRootPart.Position, target)
--   if path.Status == Enum.PathStatus.Success then
--       for _, wp in ipairs(path:GetWaypoints()) do
--           if wp.Action == Enum.PathWaypointAction.Jump then humanoid.Jump = true end
--           humanoid:MoveTo(wp.Position)
--           humanoid.MoveToFinished:Wait()
--       end
--   end
-- ...or let the engine do all of that: humanoid:PathfindTo(target) walks there by itself
-- (jumping, finding a new way when blocked or stuck, keeping up with a moving target).
-- Extras: PathfindingService:IsWalkable(pos), :FindClosestPoint(pos), :GetRandomPoint(near, radius),
-- :CanWalkStraight(a, b), :Bake(), :SetBakeSettings({...}). Part attributes
-- PathfindingLabel (a name for Costs) and PathfindingPassThrough (ignored) work like
-- Roblox's PathfindingModifier.
local navPath, navQuery = __gb_navPath, __gb_navQuery
local function luaSignal()
    local sig, list = {}, {}
    function sig:Connect(fn)
        local c = { Connected = true }
        function c:Disconnect() self.Connected = false end
        list[#list + 1] = { c, fn }
        return c
    end
    sig.connect = sig.Connect
    function sig:Wait()
        local done, args = false, nil
        local c
        c = sig:Connect(function(...) args = { ... }; done = true; c:Disconnect() end)
        while not done do task.wait() end
        return table.unpack(args)
    end
    function sig:Fire(...)
        for _, e in ipairs(list) do if e[1].Connected then task.spawn(e[2], ...) end end
    end
    return sig
end
local watched = setmetatable({}, { __mode = "k" })   -- paths that can be Blocked
local watching = false
local function startWatching()
    if watching then return end
    watching = true
    task.spawn(function()
        while true do
            task.wait(0.25)
            for path in pairs(watched) do
                if path._ver ~= navQuery("version") then
                    path._ver = navQuery("version")
                    local idx = path:CheckOcclusionAsync(1)
                    if idx > 0 and not path._blocked then path._blocked = true; path.Blocked:Fire(idx)
                    elseif idx < 0 and path._blocked then path._blocked = false; path.Unblocked:Fire(1) end
                end
            end
        end
    end)
end
PathfindingService = { ClassName = "PathfindingService", Name = "PathfindingService" }
function PathfindingService:CreatePath(params)
    local path = { ClassName = "Path", Status = "NoPath", _wps = {}, _params = params or {} }
    path.Blocked, path.Unblocked = luaSignal(), luaSignal()
    function path:ComputeAsync(from, to)
        local status, wps = navPath(from, to, self._params)
        self.Status = status
        self._wps = wps
        self._ver = navQuery("version")
        self._blocked = false
        watched[self] = true
        startWatching()
    end
    function path:GetWaypoints()
        local t = {}
        for i, w in ipairs(self._wps) do
            t[i] = { ClassName = "PathWaypoint", Position = w[1], Action = w[2], Label = w[3] }
        end
        return t
    end
    -- The first waypoint (from `start` on) whose way there is blocked now, or -1.
    function path:CheckOcclusionAsync(start)
        local w = self._wps
        for i = math.max(2, start or 1), #w do
            if w[i][2] == "Jump" then
                if not navQuery("walkable", w[i][1], self._params) then return i end
            elseif not navQuery("straight", w[i - 1][1], w[i][1], self._params) then
                return i
            end
        end
        return -1
    end
    function path:Destroy() watched[self] = nil; self._wps = {} end
    return path
end
function PathfindingService:FindPathAsync(from, to)
    local p = self:CreatePath()
    p:ComputeAsync(from, to)
    return p
end
PathfindingService.ComputeRawPathAsync = PathfindingService.FindPathAsync
PathfindingService.ComputeSmoothPathAsync = PathfindingService.FindPathAsync
function PathfindingService:IsWalkable(pos, params) return navQuery("walkable", pos, params) end
function PathfindingService:FindClosestPoint(pos, range, params) return navQuery("closest", pos, range or 10, params) end
function PathfindingService:GetRandomPoint(near, radius, params) return navQuery("random", near, radius, params) end
function PathfindingService:CanWalkStraight(a, b, params) return navQuery("straight", a, b, params) end
function PathfindingService:Bake() return navQuery("bake") end
function PathfindingService:SetBakeSettings(t) navQuery("settings", t) end
function PathfindingService:GetService() return self end

-- humanoid:PathfindTo(target [, params]): walk there by itself; waits until it gets
-- there (true) or gives up (false). target: a Vector3, a part, or a model/character.
function __gb_pathfindTo(hum, target, params)
    if not hum:PathfindStart(target, params) then return false end
    while true do
        local st = hum.PathfindStatus
        if st == "Arrived" then return true end
        if st ~= "Walking" then return false end
        task.wait(0.1)
    end
end

-- BadgeService: give players the badges you made for your game on its page
-- (Create > your game > Badges). Only works in a published game's online server.
--   BadgeService:AwardBadge(player, "gb-badge-1a2b3c")
local awardBadge, hasBadge = __gb_awardBadge, __gb_hasBadge
BadgeService = {}
local function nameOf(p)
    if type(p) == "table" then return p.Name end
    if type(p) == "number" then
        for _, pl in ipairs(Players:GetPlayers()) do if pl.UserId == p then return pl.Name end end
    end
    return tostring(p)
end
function BadgeService:AwardBadge(player, badgeId)
    awardBadge(nameOf(player), tostring(badgeId))
    return true
end
function BadgeService:UserHasBadgeAsync(player, badgeId)
    return hasBadge(nameOf(player), tostring(badgeId))
end
BadgeService.UserHasBadge = BadgeService.UserHasBadgeAsync

-- TeleportService: send players to another game (its ID is the number in the game's
-- link), with some data they bring along, like Roblox:
--   TeleportService:Teleport(1234, player, { coins = 5 })
--   TeleportService:TeleportAsync(1234, { player1, player2 }, options)   (options: TeleportOptions)
--   over there: player:GetJoinData().TeleportData, or GetLocalPlayerTeleportData() in a LocalScript
do
    local teleport = __gb_teleport
    local function stub() return { Connect = function() return { Disconnect = function() end } end, Wait = function() end } end
    TeleportService = { ClassName = "TeleportService", Name = "TeleportService",
                        TeleportInitFailed = stub(), LocalPlayerArrivedFromTeleport = stub() }
    local function send(placeId, players, data)
        if players == nil then players = Players.LocalPlayer end
        if players == nil then error("Teleport needs a player to send", 3) end
        if type(players) ~= "table" or rawget(players, "Name") then players = { players } end
        for _, p in ipairs(players) do teleport(tostring(placeId), nameOf(p), data) end
    end
    function TeleportService:Teleport(placeId, player, data) send(placeId, player, data) end
    function TeleportService:TeleportAsync(placeId, players, options)
        send(placeId, players, options and options.__data)
        return { ClassName = "TeleportAsyncResult", PrivateServerId = "", ReservedServerAccessCode = "" }
    end
    function TeleportService:TeleportPartyAsync(placeId, players, data) send(placeId, players, data) return "" end
    function TeleportService:TeleportToPlaceInstance(placeId, _, player, _, data) send(placeId, player, data) end
    function TeleportService:TeleportToPrivateServer(placeId, _, players, _, data) send(placeId, players, data) end
    function TeleportService:TeleportToSpawnByName(placeId, _, player, data) send(placeId, player, data) end
    function TeleportService:GetLocalPlayerTeleportData()
        local me = Players.LocalPlayer
        return me and joinData(rawget(me, "Name")).TeleportData
    end
    function TeleportService:SetTeleportGui() end
    function TeleportService:GetArrivingTeleportGui() return nil end
    local settings = {}
    function TeleportService:SetTeleportSetting(k, v) settings[k] = v end
    function TeleportService:GetTeleportSetting(k) return settings[k] end
end

-- ProximityPromptService: every ProximityPrompt's events in one place.
--   ProximityPromptService.PromptTriggered:Connect(function(prompt, player) ... end)
ProximityPromptService = { Name = "ProximityPromptService", ClassName = "ProximityPromptService", Enabled = true,
    MaxPromptsVisible = 4, PromptTriggered = __gb_promptTriggered, PromptTriggerEnded = __gb_promptTriggerEnded,
    PromptButtonHoldBegan = __gb_promptHoldBegan, PromptButtonHoldEnded = __gb_promptHoldEnded,
    PromptShown = __gb_promptShown, PromptHidden = __gb_promptHidden }
__gb_promptTriggered, __gb_promptTriggerEnded, __gb_promptHoldBegan = nil, nil, nil
__gb_promptHoldEnded, __gb_promptShown, __gb_promptHidden = nil, nil, nil

-- Debris: throw something away later, like Roblox's. Debris:AddItem(part, 5)
Debris = { ClassName = "Debris", Name = "Debris", MaxItems = 1000 }
function Debris:AddItem(obj, lifetime)
    task.delay(tonumber(lifetime) or 10, function()
        if obj and obj.Parent then pcall(function() obj:Destroy() end) end
    end)
end
Debris.addItem = Debris.AddItem


-- ---------------------------------------------------------------------------
-- More of Roblox's API (the "Who Let Us Cook?" update)
-- ---------------------------------------------------------------------------
local raycast, addMethod = __gb_raycast, __gb_addMethod

-- workspace:Raycast(origin, direction [, params]) -> RaycastResult or nil
--   local params = RaycastParams.new()
--   params.FilterDescendantsInstances = { character }
--   params.FilterType = Enum.RaycastFilterType.Exclude   -- or Include
--   local hit = workspace:Raycast(gun.Position, gun.CFrame.LookVector * 300, params)
--   if hit then print(hit.Instance.Name, hit.Position, hit.Normal, hit.Distance) end
RaycastParams = {}
function RaycastParams.new()
    return { ClassName = "RaycastParams", FilterDescendantsInstances = {}, FilterType = "Exclude", IgnoreWater = false }
end
local function castWith(origin, direction, list, include)
    local part, pos, normal, dist, material = raycast(origin, direction, list or {}, include)
    if not part then return nil end
    return { ClassName = "RaycastResult", Instance = part, Position = pos, Normal = normal, Distance = dist,
             Material = material or part.Material }
end
addMethod("Raycast", function(_, origin, direction, params)
    params = params or RaycastParams.new()
    local ft = tostring(params.FilterType)
    return castWith(origin, direction, params.FilterDescendantsInstances,
                    ft == "Include" or ft == "Whitelist" or ft == "Enum.RaycastFilterType.Include")
end)
-- The old way: Ray.new(origin, direction) and workspace:FindPartOnRay(ray, ignore)
Ray = {}
function Ray.new(origin, direction)
    local r = { ClassName = "Ray", Origin = origin, Direction = direction }
    r.Unit = setmetatable({}, { __index = function(_, k)
        if k == "Origin" then return origin elseif k == "Direction" then return direction.Unit end
    end })
    return r
end
local function findOnRay(ray, list, include)
    local hit = castWith(ray.Origin, ray.Direction, list, include)
    if not hit then return nil, ray.Origin + ray.Direction, Vector3.new(0, 0, 0), nil end
    return hit.Instance, hit.Position, hit.Normal, hit.Material
end
addMethod("FindPartOnRay", function(_, ray, ignore) return findOnRay(ray, ignore and { ignore } or {}, false) end)
addMethod("FindPartOnRayWithIgnoreList", function(_, ray, list) return findOnRay(ray, list, false) end)
addMethod("FindPartOnRayWithWhitelist", function(_, ray, list) return findOnRay(ray, list, true) end)
-- workspace:GetPartBoundsInRadius(position, radius): parts near a point.
addMethod("GetPartBoundsInRadius", function(self, pos, radius)
    local out = {}
    for _, d in ipairs(workspace:GetDescendants()) do
        if d:IsA("BasePart") then
            local s = d.Size
            local reach = radius + math.max(s.X, s.Y, s.Z) * 0.5
            if (d.Position - pos).Magnitude <= reach then out[#out + 1] = d end
        end
    end
    return out
end)

-- TweenService: smoothly change properties over time.
--   local info = TweenInfo.new(2, Enum.EasingStyle.Quad, Enum.EasingDirection.Out)
--   TweenService:Create(door, info, { Position = door.Position + Vector3.new(0, 10, 0) }):Play()
-- Works on numbers, Vector3, Color3, CFrame, UDim2 and Vector2 properties.
TweenInfo = {}
function TweenInfo.new(time, style, direction, repeatCount, reverses, delayTime)
    return { ClassName = "TweenInfo", Time = time or 1, EasingStyle = tostring(style or "Quad"),
             EasingDirection = tostring(direction or "Out"), RepeatCount = repeatCount or 0,
             Reverses = reverses or false, DelayTime = delayTime or 0 }
end
local function bareName(s) return (tostring(s):match("([%w]+)$")) end
local easeIn = {
    Linear = function(t) return t end,
    Quad = function(t) return t * t end,
    Cubic = function(t) return t * t * t end,
    Quart = function(t) return t ^ 4 end,
    Quint = function(t) return t ^ 5 end,
    Sine = function(t) return 1 - math.cos(t * math.pi / 2) end,
    Exponential = function(t) return t == 0 and 0 or 2 ^ (10 * (t - 1)) end,
    Circular = function(t) return 1 - math.sqrt(1 - t * t) end,
    Back = function(t) local s = 1.70158; return t * t * ((s + 1) * t - s) end,
    Elastic = function(t)
        if t == 0 or t == 1 then return t end
        return -(2 ^ (10 * (t - 1))) * math.sin((t - 1.075) * (2 * math.pi) / 0.3)
    end,
    Bounce = function(t)
        t = 1 - t
        local v
        if t < 1 / 2.75 then v = 7.5625 * t * t
        elseif t < 2 / 2.75 then t = t - 1.5 / 2.75; v = 7.5625 * t * t + 0.75
        elseif t < 2.5 / 2.75 then t = t - 2.25 / 2.75; v = 7.5625 * t * t + 0.9375
        else t = t - 2.625 / 2.75; v = 7.5625 * t * t + 0.984375 end
        return 1 - v
    end,
}
local function ease(style, dir, t)
    local f = easeIn[bareName(style)] or easeIn.Quad
    dir = bareName(dir)
    if dir == "In" then return f(t) end
    if dir == "InOut" then
        if t < 0.5 then return f(t * 2) / 2 end
        return 1 - f((1 - t) * 2) / 2
    end
    return 1 - f(1 - t)   -- Out
end
local function lerpValue(a, b, t)
    if type(a) == "number" then return a + (b - a) * t end
    if type(a) == "boolean" or type(a) == "string" then return t < 1 and a or b end
    return a:Lerp(b, t)
end
TweenService = { ClassName = "TweenService", Name = "TweenService" }
function TweenService:GetValue(alpha, style, direction) return ease(style, direction, math.clamp(alpha, 0, 1)) end
function TweenService:Create(obj, info, goals)
    info = info or TweenInfo.new()
    local tw = { ClassName = "Tween", Instance = obj, TweenInfo = info, PlaybackState = "Begin" }
    tw.Completed = luaSignal()
    local run = 0   -- which Play is running (a new Play or Cancel stops the old one)
    function tw:Play()
        run = run + 1
        local me = run
        self.PlaybackState = "Playing"
        task.spawn(function()
            if info.DelayTime > 0 then task.wait(info.DelayTime) end
            local loops = info.RepeatCount < 0 and math.huge or info.RepeatCount
            local done = 0
            while true do
                local from = {}
                for k in pairs(goals) do from[k] = obj[k] end
                for pass = 1, info.Reverses and 2 or 1 do
                    local start = time()
                    while true do
                        if run ~= me then return end
                        local a = info.Time <= 0 and 1 or math.min(1, (time() - start) / info.Time)
                        local e = ease(info.EasingStyle, info.EasingDirection, a)
                        if pass == 2 then e = 1 - e end
                        for k, goal in pairs(goals) do obj[k] = lerpValue(from[k], goal, e) end
                        if a >= 1 then break end
                        task.wait()
                    end
                end
                done = done + 1
                if done > loops then break end
                if info.Reverses then
                    -- (back where it started: go again)
                else
                    for k, v in pairs(from) do obj[k] = v end
                end
            end
            if run == me then self.PlaybackState = "Completed"; self.Completed:Fire("Completed") end
        end)
    end
    function tw:Pause() run = run + 1; self.PlaybackState = "Paused" end
    function tw:Cancel()
        run = run + 1
        self.PlaybackState = "Cancelled"
        self.Completed:Fire("Cancelled")
    end
    tw.Destroy = tw.Cancel
    return tw
end

-- HttpService: JSON in and out, and unique ids. (Games can't reach other websites.)
HttpService = { ClassName = "HttpService", Name = "HttpService", HttpEnabled = false }
local function jsonString(s)
    return '"' .. s:gsub('[%c"\\]', function(c)
        local map = { ['"'] = '\\"', ['\\'] = '\\\\', ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t' }
        return map[c] or string.format("\\u%04x", c:byte())
    end) .. '"'
end
local function encode(v, depth)
    depth = depth or 0
    if depth > 100 then error("JSONEncode: that table goes too deep (or contains itself)", 0) end
    local t = type(v)
    if v == nil then return "null"
    elseif t == "boolean" then return tostring(v)
    elseif t == "number" then
        if v ~= v or v == math.huge or v == -math.huge then return "null" end
        if math.type and math.type(v) == "integer" then return tostring(v) end
        return string.format("%.14g", v)
    elseif t == "string" then return jsonString(v)
    elseif t == "table" then
        local n = #v
        local isArray = n > 0 or next(v) == nil
        if isArray then for k in pairs(v) do if type(k) ~= "number" or k < 1 or k > n or k % 1 ~= 0 then isArray = false break end end end
        local parts = {}
        if isArray then
            for i = 1, n do parts[i] = encode(v[i], depth + 1) end
            return "[" .. table.concat(parts, ",") .. "]"
        end
        for k, val in pairs(v) do parts[#parts + 1] = jsonString(tostring(k)) .. ":" .. encode(val, depth + 1) end
        return "{" .. table.concat(parts, ",") .. "}"
    end
    error("JSONEncode can't save a " .. t, 0)
end
function HttpService:JSONEncode(v) return encode(v) end
function HttpService:JSONDecode(s)
    local pos = 1
    local function ws() pos = s:find("[^ \t\r\n]", pos) or #s + 1 end
    local value
    local function str()
        local out = {}
        pos = pos + 1
        while true do
            local c = s:sub(pos, pos)
            if c == "" then error("JSONDecode: a string never ends", 0) end
            if c == '"' then pos = pos + 1 break end
            if c == "\\" then
                local e = s:sub(pos + 1, pos + 1)
                local map = { b = "\b", f = "\f", n = "\n", r = "\r", t = "\t" }
                if e == "u" then
                    out[#out + 1] = utf8.char(tonumber(s:sub(pos + 2, pos + 5), 16) or 63)
                    pos = pos + 6
                else
                    out[#out + 1] = map[e] or e
                    pos = pos + 2
                end
            else
                out[#out + 1] = c
                pos = pos + 1
            end
        end
        return table.concat(out)
    end
    value = function()
        ws()
        local c = s:sub(pos, pos)
        if c == "{" then
            local t = {}
            pos = pos + 1; ws()
            if s:sub(pos, pos) == "}" then pos = pos + 1 return t end
            while true do
                ws()
                local k = str()
                ws()
                if s:sub(pos, pos) ~= ":" then error("JSONDecode: expected ':' at " .. pos, 0) end
                pos = pos + 1
                t[k] = value()
                ws()
                local d = s:sub(pos, pos); pos = pos + 1
                if d == "}" then return t end
                if d ~= "," then error("JSONDecode: expected ',' or '}' at " .. (pos - 1), 0) end
            end
        elseif c == "[" then
            local t = {}
            pos = pos + 1; ws()
            if s:sub(pos, pos) == "]" then pos = pos + 1 return t end
            while true do
                t[#t + 1] = value()
                ws()
                local d = s:sub(pos, pos); pos = pos + 1
                if d == "]" then return t end
                if d ~= "," then error("JSONDecode: expected ',' or ']' at " .. (pos - 1), 0) end
            end
        elseif c == '"' then return str()
        elseif s:sub(pos, pos + 3) == "true" then pos = pos + 4 return true
        elseif s:sub(pos, pos + 4) == "false" then pos = pos + 5 return false
        elseif s:sub(pos, pos + 3) == "null" then pos = pos + 4 return nil
        end
        local num = s:match("^-?%d+%.?%d*[eE]?[-+]?%d*", pos)
        if not num or num == "" then error("JSONDecode: unexpected '" .. c .. "' at " .. pos, 0) end
        pos = pos + #num
        return tonumber(num)
    end
    local v = value()
    ws()
    if pos <= #s then error("JSONDecode: extra text at " .. pos, 0) end
    return v
end
function HttpService:GenerateGUID(braces)
    local g = string.gsub("xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx", "[xy]", function(c)
        local v = c == "x" and math.random(0, 15) or math.random(8, 11)
        return string.format("%X", v)
    end)
    if braces == false then return g end
    return "{" .. g .. "}"
end
function HttpService:UrlEncode(s)
    return (tostring(s):gsub("[^%w%-_%.~]", function(c) return string.format("%%%02X", c:byte()) end))
end
function HttpService:GetAsync() error("Games can't reach other websites from Guts&Bolts.", 2) end
HttpService.PostAsync, HttpService.RequestAsync = HttpService.GetAsync, HttpService.GetAsync

-- Random.new(seed): its own stream of random numbers (the same seed, the same numbers).
Random = {}
function Random.new(seed)
    local state = math.floor(tonumber(seed) or (os.time() + math.random(1, 1000000))) % 2147483646 + 1
    local r = { ClassName = "Random" }
    local function step()
        state = (state * 48271) % 2147483647
        return (state - 1) / 2147483646
    end
    function r:NextNumber(lo, hi)
        lo, hi = lo or 0, hi or 1
        return lo + (hi - lo) * step()
    end
    function r:NextInteger(lo, hi)
        if hi < lo then lo, hi = hi, lo end
        return math.min(hi, lo + math.floor(step() * (hi - lo + 1)))
    end
    function r:NextUnitVector()
        local z = r:NextNumber(-1, 1)
        local a = r:NextNumber(0, math.pi * 2)
        local s = math.sqrt(1 - z * z)
        return Vector3.new(s * math.cos(a), s * math.sin(a), z)
    end
    function r:Shuffle(t)
        for i = #t, 2, -1 do
            local j = r:NextInteger(1, i)
            t[i], t[j] = t[j], t[i]
        end
    end
    function r:Clone()
        local c = Random.new(1)
        local saved = state
        -- (copy the state by stepping a fresh one to the same place)
        c._set(saved)
        return c
    end
    function r._set(v) state = v end
    return r
end

-- math.noise(x [, y, z]): smooth "Perlin" noise between about -1 and 1 (0 on whole numbers),
-- for hills, wobbly lights, wind...
do
    local perm = {}
    local base = Random.new(1234)
    local p = {}
    for i = 0, 255 do p[i] = i end
    for i = 255, 1, -1 do local j = base:NextInteger(0, i); p[i], p[j] = p[j], p[i] end
    for i = 0, 511 do perm[i] = p[i % 256] end
    local function fade(t) return t * t * t * (t * (t * 6 - 15) + 10) end
    local function grad(h, x, y, z)
        h = h % 16
        local u = h < 8 and x or y
        local v = h < 4 and y or ((h == 12 or h == 14) and x or z)
        return ((h % 2 == 0) and u or -u) + ((h % 4 < 2) and v or -v)
    end
    function math.noise(x, y, z)
        x, y, z = x or 0, y or 0, z or 0
        local X, Y, Z = math.floor(x) % 256, math.floor(y) % 256, math.floor(z) % 256
        x, y, z = x - math.floor(x), y - math.floor(y), z - math.floor(z)
        local u, v, w = fade(x), fade(y), fade(z)
        local A, B = perm[X] + Y, perm[X + 1] + Y
        local AA, AB, BA, BB = perm[A] + Z, perm[A + 1] + Z, perm[B] + Z, perm[B + 1] + Z
        local function lerp(t, a, b) return a + t * (b - a) end
        return lerp(w, lerp(v, lerp(u, grad(perm[AA], x, y, z), grad(perm[BA], x - 1, y, z)),
                               lerp(u, grad(perm[AB], x, y - 1, z), grad(perm[BB], x - 1, y - 1, z))),
                       lerp(v, lerp(u, grad(perm[AA + 1], x, y, z - 1), grad(perm[BA + 1], x - 1, y, z - 1)),
                               lerp(u, grad(perm[AB + 1], x, y - 1, z - 1), grad(perm[BB + 1], x - 1, y - 1, z - 1))))
    end
end

-- Explosion, like Roblox's: make one, set it up, parent it (to workspace) and BOOM.
--   local e = Instance.new("Explosion")
--   e.Position = bomb.Position
--   e.BlastRadius = 20          -- how far it reaches
--   e.BlastPressure = 500000    -- how hard it hits (500000 = normal)
--   e.Hit:Connect(function(part, distance) ... end)
--   e.Parent = workspace
-- Extras (Guts&Bolts): e.Smoke (true), e.Fire (seconds of fires left burning),
-- e.MushroomCloud (true / false; big blasts get one anyway), e.Destroy (rips anchored
-- parts loose near the middle: craters), e.Visible = false (just the push).
-- Big blasts near water throw up spray, make waves, and big enough ones a tsunami.
do
    local explode = Explode
    local function explosion(parent)
        local hit = luaSignal()
        local o = { ClassName = "Explosion", Name = "Explosion", Position = Vector3.new(0, 0, 0), BlastRadius = 4,
                    BlastPressure = 500000, DestroyJointRadiusPercent = 1, ExplosionType = "Craters", Visible = true,
                    Smoke = true, Fire = 0, Destroy = false, Hit = hit }
        local fired = false
        local function go(self)
            if fired then return end
            fired = true
            local mush = rawget(self, "MushroomCloud")
            local hits = explode(self.Position, self.BlastRadius, (self.BlastPressure or 500000) / 500000, {
                Smoke = self.Smoke, Fire = self.Fire, Destroy = self.Destroy, Visible = self.Visible,
                MushroomCloud = mush, JointBreak = self.DestroyJointRadiusPercent })
            for _, h in ipairs(hits or {}) do hit:Fire(h[1], h[2]) end
        end
        function o:IsA(c) return c == "Explosion" or c == "Instance" end
        function o:Destroy() fired = true end
        function o:GetFullName() return "Explosion" end
        local proxy = setmetatable({}, {
            __index = function(_, k)
                if k == "Parent" then return rawget(o, "_parent") end
                return o[k]
            end,
            __newindex = function(t, k, v)
                if k == "Parent" then rawset(o, "_parent", v); if v ~= nil then go(o) end
                else o[k] = v end
            end })
        if parent ~= nil then proxy.Parent = parent end
        return proxy
    end
    __gb_newExplosion = explosion
end

-- BindableEvent / BindableFunction: scripts talking to each other.
--   local ev = Instance.new("BindableEvent")      ev.Event:Connect(print)    ev:Fire("hi")
--   local fn = Instance.new("BindableFunction")   fn.OnInvoke = function(x) return x * 2 end   fn:Invoke(21)
-- (They live in the script that made them: share them through a module or _G.)
do
    local rawNew = Instance.new
    local function bindable(cls, parent)
        local o = { ClassName = cls, Name = cls, Parent = parent }
        function o:IsA(c) return c == cls or c == "Instance" end
        function o:Destroy() self.Parent = nil; self._dead = true end
        function o:GetFullName() return self.Name end
        if cls == "BindableEvent" then
            o.Event = luaSignal()
            function o:Fire(...) if not self._dead then self.Event:Fire(...) end end
        else
            function o:Invoke(...)
                if not self.OnInvoke then error("BindableFunction has no OnInvoke", 2) end
                return self.OnInvoke(...)
            end
        end
        return o
    end
    Instance.new = function(cls, parent)
        if cls == "BindableEvent" or cls == "BindableFunction" then return bindable(cls, parent) end
        if cls == "Explosion" then return __gb_newExplosion(parent) end
        if cls == "TeleportOptions" then   -- TeleportService:TeleportAsync's options
            local o = { ClassName = "TeleportOptions", Name = "TeleportOptions", ShouldReserveServer = false,
                        ServerInstanceId = "", ReservedServerAccessCode = "" }
            function o:SetTeleportData(d) self.__data = d end
            function o:GetTeleportData() return self.__data end
            return o
        end
        return rawNew(cls, parent)
    end
end
_G = _G or {}

-- MarketplaceService: game passes, made on the game's Configure page on the website.
--   if MarketplaceService:UserOwnsGamePassAsync(player.UserId, 123) then ... end
--   MarketplaceService:PromptGamePassPurchase(player, 123)   (a Buy window pops up for them)
--   MarketplaceService.PromptGamePassPurchaseFinished:Connect(function(player, passId, bought) end)
-- Developer products (bought again and again, like coins or a revive):
--   MarketplaceService:PromptProductPurchase(player, 456)
--   MarketplaceService.ProcessReceipt = function(info)   -- info.PlayerId, .ProductId, .PurchaseId, .CurrencySpent
--       (give them the thing)
--       return Enum.ProductPurchaseDecision.PurchaseGranted   -- or NotProcessedYet: asked again later
--   end
do
    local lookup, ready, owns, prompt, results = __gb_passLookup, __gb_passReady, __gb_passOwns, __gb_passPrompt, __gb_passResults
    local receiptCheck, receiptsNow, receiptDone = __gb_receiptCheck, __gb_receipts, __gb_receiptDone
    local finished, productFinished = Instance.new("BindableEvent"), Instance.new("BindableEvent")
    MarketplaceService = { Name = "MarketplaceService", ClassName = "MarketplaceService",
                           PromptGamePassPurchaseFinished = finished.Event, PromptProductPurchaseFinished = productFinished.Event }
    local function userIdOf(p) if type(p) == "table" then return p.UserId or 0 end return tonumber(p) or 0 end
    function MarketplaceService:UserOwnsGamePassAsync(user, passId)
        local id = userIdOf(user)
        if id <= 0 then return false end
        lookup(id)
        local tries = 0
        while not ready(id) and tries < 100 do task.wait(0.05); tries = tries + 1 end
        return owns(id, tostring(passId))
    end
    local listening = false
    local function listen()
        if listening then return end
        listening = true
        RunService.Heartbeat:Connect(function()
            for _, r in ipairs(results()) do
                local product = r[2]:match("^product:(.*)$")
                if product then
                    productFinished:Fire(r[1], tonumber(product) or product, r[3])   -- (Roblox passes the UserId here)
                else
                    local who
                    for _, pl in ipairs(Players:GetPlayers()) do if pl.UserId == r[1] then who = pl end end
                    if who then finished:Fire(who, tonumber(r[2]) or r[2], r[3]) end
                end
            end
        end)
    end
    function MarketplaceService:PromptGamePassPurchase(player, passId)
        prompt(userIdOf(player), tostring(passId))
        listen()
    end
    function MarketplaceService:PromptProductPurchase(player, productId)
        prompt(userIdOf(player), "product:" .. tostring(productId))
        listen()
    end
    -- Receipts: each player's waiting ones are fetched when ProcessReceipt is set (and they're
    -- in the game), handed to it one at a time, and the server is told which were given out.
    local checked = {}
    RunService.Heartbeat:Connect(function()
        local process = rawget(MarketplaceService, "ProcessReceipt")
        if type(process) ~= "function" then return end
        for _, pl in ipairs(Players:GetPlayers()) do
            if pl.UserId > 0 and not checked[pl.UserId] then checked[pl.UserId] = true; receiptCheck(pl.UserId) end
        end
        for _, r in ipairs(receiptsNow()) do
            local userId, receipt = r[1], r[2]
            task.spawn(function()
                local ok, decision = pcall(process, { PlayerId = userId, ProductId = tonumber(r[3]) or r[3], PurchaseId = receipt,
                                                      CurrencySpent = r[4], CurrencyType = "Bolts" })
                if not ok then warn("ProcessReceipt: " .. tostring(decision)) end
                local granted = ok and decision == Enum.ProductPurchaseDecision.PurchaseGranted
                receiptDone(userId, receipt, granted)
                if not granted then task.delay(30, function() checked[userId] = nil end) end   -- try again in a bit
            end)
        end
    end)
    function MarketplaceService:GetProductInfo(id) return { AssetId = tonumber(id) or id, Name = tostring(id) } end
    GamePassService = { Name = "GamePassService", ClassName = "GamePassService" }
    function GamePassService:PlayerHasPass(player, passId) return MarketplaceService:UserOwnsGamePassAsync(player, passId) end
end

local services = { Workspace = workspace, PathfindingService = PathfindingService, BadgeService = BadgeService, TeleportService = TeleportService, Debris = Debris, Teams = Teams, Players = Players, Lighting = Lighting,
                   MarketplaceService = MarketplaceService, GamePassService = GamePassService,
                   RunService = RunService, UserInputService = UserInputService, Gui = Gui,
                   CollectionService = CollectionService, DataStoreService = DataStoreService,
                   TweenService = TweenService, HttpService = HttpService,
                   ProximityPromptService = ProximityPromptService }
-- The container services are folders at the top of the game (Studio shows them in the Explorer).
local storageNames = { ReplicatedStorage = true, ServerStorage = true, ReplicatedFirst = true, ServerScriptService = true,
                       StarterPack = true, SoundService = true, Chat = true, TextChatService = true }
local storage = __gb_storage
-- Services Guts and Bolts doesn't do much with yet: they exist (so scripts written for
-- Roblox start), their harmless calls do nothing, and anything else says so clearly.
local quiet = { ContentProvider = { PreloadAsync = true }, LogService = {}, ScriptContext = {}, TestService = {},
    Stats = {}, GuiService = {}, HapticService = {}, LocalizationService = {},
    AnalyticsService = { LogCustomEvent = true, LogEconomyEvent = true, LogProgressionEvent = true },
    PolicyService = {}, SocialService = {}, GroupService = {}, AssetService = {}, TextService = {},
    MaterialService = {}, VoiceChatService = {}, ContextActionService = { UnbindAction = true },
    MemoryStoreService = {}, MessagingService = { PublishAsync = true }, InsertService = {},
    PhysicsService = { RegisterCollisionGroup = true, CollisionGroupSetCollidable = true },
    ChangeHistoryService = { SetWaypoint = true }, Selection = {},
    StarterPlayer = {}, VRService = {}, GamepadService = {}, KeyframeSequenceProvider = {}, NotificationService = {} }
local made = {}
local function quietService(name)
    if made[name] then return made[name] end
    local calls = quiet[name]
    local s = setmetatable({ Name = name, ClassName = name }, { __index = function(_, key)
        if calls[key] then return function() end end
        if type(key) == "string" and key:sub(1, 1):match("%u") then
            return function() error(name .. ":" .. key .. " isn't in Guts and Bolts yet", 2) end
        end
        return nil
    end, __tostring = function() return name end })
    made[name] = s
    return s
end
-- GuiService: the emote menu can be opened and closed from scripts (the rest is quiet).
do
    local emotesMenu = __gb_emotesMenu
    local gs = quietService("GuiService")
    rawset(gs, "SetEmotesMenuOpen", function(_, open) emotesMenu(open and true or false) end)
    rawset(gs, "GetEmotesMenuOpen", function() return emotesMenu() end)
end
local function findService(name)
    if name == "StarterGui" then return __gb_uiFolder() end
    if storageNames[name] then return storage(name) end
    if services[name] ~= nil then return services[name] end
    if quiet[name] then return quietService(name) end
    return nil
end
game = setmetatable({}, { __index = function(_, name) return findService(name) end })
function game:GetService(name)
    local s = findService(name)
    if s == nil then
        error("'" .. tostring(name) .. "' is not a service Guts and Bolts knows about", 2)
    end
    return s
end
game.FindService = function(_, name) return findService(name) end
Workspace = workspace

-- Handy extras that Roblox's Luau also has.
math.atan2 = math.atan2 or function(y, x) return math.atan(y, x) end
math.pow = math.pow or function(x, y) return x ^ y end
math.log10 = math.log10 or function(x) return math.log(x, 10) end
function math.clamp(x, lo, hi) if x < lo then return lo elseif x > hi then return hi end return x end
function math.sign(x) if x > 0 then return 1 elseif x < 0 then return -1 end return 0 end
function math.round(x) return math.floor(x + 0.5) end
function math.lerp(a, b, t) return a + (b - a) * t end
function string.split(s, sep)
    sep = sep or ","
    local out, start = {}, 1
    while true do
        local i, j = string.find(s, sep, start, true)
        if not i then table.insert(out, string.sub(s, start)) break end
        table.insert(out, string.sub(s, start, i - 1))
        start = j + 1
    end
    return out
end
function table.find(t, value)
    for i, v in ipairs(t) do if v == value then return i end end
    return nil
end

-- ---------------------------------------------------------------------------
-- RemoteEvents / RemoteFunctions (ScriptRemotes.cpp does the sending)
-- ---------------------------------------------------------------------------
do
    local mode, send, fireHere = __gb_remoteMode, __gb_remoteSend, __gb_remoteFire
    local invokeStart, invokeDone = __gb_invokeStart, __gb_invokeDone
    local takeInvokes, invokeReply, invokers = __gb_takeInvokes, __gb_invokeReply, __gb_invokers
    local function isPlayer(p) return type(p) == "table" and rawget(p, "UserId") ~= nil end
    local function onServer()
        if mode() == "Client" then error("only a Script on the server can do this (this is a LocalScript)", 3) end
    end
    local function needEvent(self, name)
        if self.ClassName ~= "RemoteEvent" then error(name .. " is for RemoteEvents", 3) end
    end

    -- A LocalScript tells the server something. The server's OnServerEvent gets (player, ...).
    addMethod("FireServer", function(self, ...)
        needEvent(self, "FireServer")
        if mode() == "Server" then error("FireServer is for LocalScripts", 2) end
        if mode() == "Client" then send(self, -1, ...)
        else fireHere(self, "Server", LocalPlayer, ...) end
    end)
    -- The server tells one player something. Their OnClientEvent gets (...).
    addMethod("FireClient", function(self, player, ...)
        needEvent(self, "FireClient")
        onServer()
        if not isPlayer(player) then error("FireClient needs a player first", 2) end
        if LocalPlayer and player.UserId == LocalPlayer.UserId then fireHere(self, "Client", ...)
        else send(self, player.UserId, ...) end
    end)
    -- The server tells everyone.
    addMethod("FireAllClients", function(self, ...)
        needEvent(self, "FireAllClients")
        onServer()
        if LocalPlayer then fireHere(self, "Client", ...) end
        send(self, 0, ...)
    end)

    -- A LocalScript asks the server and waits for the answer from OnServerInvoke.
    addMethod("InvokeServer", function(self, ...)
        if self.ClassName ~= "RemoteFunction" then error("InvokeServer is for RemoteFunctions", 2) end
        if mode() == "Server" then error("InvokeServer is for LocalScripts", 2) end
        if mode() ~= "Client" then
            -- Playing solo or hosting: the server's scripts are right here.
            local started = os.clock()
            while not invokers[self] do
                if os.clock() - started > 30 then error("nothing set OnServerInvoke for " .. self.Name, 2) end
                task.wait()
            end
            return invokers[self](LocalPlayer, ...)
        end
        local call, started = invokeStart(self, ...), os.clock()
        while true do
            local r = table.pack(invokeDone(call))
            if r.n > 0 then
                if not r[1] then error(r[2], 2) end
                return table.unpack(r, 2, r.n)
            end
            if os.clock() - started > 60 then error("the server didn't answer " .. self.Name, 2) end
            task.wait()
        end
    end)
    addMethod("InvokeClient", function(self)
        error("InvokeClient isn't supported (a player could freeze the server). Use a RemoteEvent instead.", 2)
    end)

    -- On the server, answer what players asked (each in its own thread so one slow
    -- answer doesn't hold up the rest).
    if mode() ~= "Client" then
        RunService.Heartbeat:Connect(function()
            for _, q in ipairs(takeInvokes()) do
                task.spawn(function()
                    local started = os.clock()
                    while not invokers[q.remote] do
                        if os.clock() - started > 30 then
                            invokeReply(q.from, q.call, false, "nothing set OnServerInvoke for " .. q.remote.Name)
                            return
                        end
                        task.wait()
                    end
                    local player = q.player or __gb_playerById(q.from, "")
                    invokeReply(q.from, q.call, pcall(invokers[q.remote], player, table.unpack(q.args, 1, q.args.n)))
                end)
            end
        end)
    end
end
-- require(module): runs a ModuleScript once; everyone who asks gets what it returned.
-- require(ID) (server only), like Roblox's require(1234567): gets that model from the
-- Library and requires its ModuleScript named MainModule. Works in the dev console too.
do
    local moduleLoad, moduleSave, libraryModel, wait = __gb_moduleLoad, __gb_moduleSave, __gb_libraryModel, task.wait
    function require(m)
        if type(m) == "number" or type(m) == "string" then
            while true do
                local state, v = libraryModel(tostring(m))
                if state == "ok" then m = v break end
                if state == "fail" then error(v, 2) end
                wait()
            end
        end
        while true do
            local fine, done, v = pcall(moduleLoad, m)
            if not fine then error((tostring(done):gsub("^prelude:%d+: ", "")), 2) end   -- (point at their line)
            if done then return v end
            if done == false then return moduleSave(m, pcall(v)) end
            wait()   -- another script is running it right now
        end
    end
end
-- The command bar: an expression's values get printed.
function __gb_cmdRun(fn)
    local r = table.pack(fn())
    if r.n == 0 then return end
    local out = {}
    for i = 1, r.n do out[i] = tostring(r[i]) end
    print(table.concat(out, "  "))
end
__gb_moduleLoad, __gb_moduleSave, __gb_libraryModel = nil, nil, nil
__gb_remoteMode, __gb_remoteSend, __gb_remoteFire, __gb_invokeStart, __gb_invokeDone = nil, nil, nil, nil, nil
__gb_takeInvokes, __gb_invokeReply, __gb_invokers, __gb_localUserId = nil, nil, nil, nil

__gb_wait, __gb_spawn, __gb_delay, __gb_character, __gb_playerName, __gb_backpack = nil, nil, nil, nil, nil, nil
__gb_noLocalPlayer = nil
__gb_heartbeat, __gb_inputBegan, __gb_inputEnded, __gb_isKeyDown, __gb_padState = nil, nil, nil, nil, nil
__gb_playerAdded, __gb_playerRemoving, __gb_tagAdded, __gb_tagRemoved = nil, nil, nil, nil
__gb_playerNode, __gb_setRespawn, __gb_dsStart, __gb_dsDone, __gb_dsSet, __gb_navPath, __gb_navQuery = nil, nil, nil, nil, nil, nil, nil
__gb_camOpt, __gb_setCamOpt, __gb_emotesMenu = nil, nil, nil
__gb_awardBadge, __gb_hasBadge, __gb_teleport, __gb_joinData = nil, nil, nil, nil
__gb_raycast, __gb_storage, __gb_addMethod = nil, nil, nil
__gb_passLookup, __gb_passReady, __gb_passOwns, __gb_passPrompt, __gb_passResults = nil, nil, nil, nil, nil
__gb_receiptCheck, __gb_receipts, __gb_receiptDone = nil, nil, nil
)LUA";

constexpr double kTimeoutSeconds = 5.0;

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct KeyName { const char* name; ImGuiKey key; const char* type = "Keyboard"; };

const std::vector<KeyName>& keyTable() {
    static std::vector<KeyName> keys = [] {
        std::vector<KeyName> k;
        static const char* letters[] = {"A","B","C","D","E","F","G","H","I","J","K","L","M",
                                        "N","O","P","Q","R","S","T","U","V","W","X","Y","Z"};
        for (int i = 0; i < 26; ++i) k.push_back({letters[i], (ImGuiKey)(ImGuiKey_A + i)});
        static const char* digits[] = {"Zero","One","Two","Three","Four","Five","Six","Seven",
                                       "Eight","Nine"};
        for (int i = 0; i < 10; ++i) k.push_back({digits[i], (ImGuiKey)(ImGuiKey_0 + i)});
        k.push_back({"Space",        ImGuiKey_Space});
        k.push_back({"Return",       ImGuiKey_Enter});
        k.push_back({"Tab",          ImGuiKey_Tab});
        k.push_back({"Backspace",    ImGuiKey_Backspace});
        k.push_back({"LeftShift",    ImGuiKey_LeftShift});
        k.push_back({"RightShift",   ImGuiKey_RightShift});
        k.push_back({"LeftControl",  ImGuiKey_LeftCtrl});
        k.push_back({"RightControl", ImGuiKey_RightCtrl});
        k.push_back({"LeftAlt",      ImGuiKey_LeftAlt});
        k.push_back({"Up",           ImGuiKey_UpArrow});
        k.push_back({"Down",         ImGuiKey_DownArrow});
        k.push_back({"Left",         ImGuiKey_LeftArrow});
        k.push_back({"Right",        ImGuiKey_RightArrow});
        // A controller (Enum.KeyCode.ButtonA...; Xbox names, like Roblox).
        static const KeyName pad[] = {
            {"ButtonA", ImGuiKey_GamepadFaceDown}, {"ButtonB", ImGuiKey_GamepadFaceRight},
            {"ButtonX", ImGuiKey_GamepadFaceLeft}, {"ButtonY", ImGuiKey_GamepadFaceUp},
            {"ButtonL1", ImGuiKey_GamepadL1}, {"ButtonR1", ImGuiKey_GamepadR1},
            {"ButtonL2", ImGuiKey_GamepadL2}, {"ButtonR2", ImGuiKey_GamepadR2},
            {"ButtonL3", ImGuiKey_GamepadL3}, {"ButtonR3", ImGuiKey_GamepadR3},
            {"ButtonStart", ImGuiKey_GamepadStart}, {"ButtonSelect", ImGuiKey_GamepadBack},
            {"DPadUp", ImGuiKey_GamepadDpadUp}, {"DPadDown", ImGuiKey_GamepadDpadDown},
            {"DPadLeft", ImGuiKey_GamepadDpadLeft}, {"DPadRight", ImGuiKey_GamepadDpadRight},
        };
        for (const KeyName& p : pad) k.push_back({p.name, p.key, "Gamepad1"});
        return k;
    }();
    return keys;
}

// ---------------------------------------------------------------------------
// Global functions
// ---------------------------------------------------------------------------

std::string joinArgs(lua_State* L) {
    std::string out;
    int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        size_t len;
        const char* s = luaL_tolstring(L, i, &len);
        if (i > 1) out += ' ';
        out.append(s, len);
        lua_pop(L, 1);
    }
    return out;
}

int l_print(lua_State* L) { Log::info(joinArgs(L)); return 0; }
int l_warn (lua_State* L) { Log::warn(joinArgs(L)); return 0; }

// wait(seconds) — pause this script; returns how long it actually waited.
int l_wait(lua_State* L) {
    double t = luaL_optnumber(L, 1, 0.0);
    lua_settop(L, 0);
    lua_pushnumber(L, t);
    return lua_yield(L, 1);
}

// spawn(fn, ...) — run fn right away in its own thread.
int l_spawn(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    int n = lua_gettop(L);
    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    for (int i = 1; i <= n; ++i) lua_pushvalue(L, i);
    lua_xmove(L, co, n);
    LuaApi::engine(L)->resume(co, ref, n - 1, L);
    return 0;
}

// delay(seconds, fn, ...) — run fn later.
int l_delay(lua_State* L) {
    double t = luaL_checknumber(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    int n = lua_gettop(L);
    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    for (int i = 2; i <= n; ++i) lua_pushvalue(L, i);
    lua_xmove(L, co, n - 1);
    LuaApi::engine(L)->schedule(co, ref, t, n - 2);
    return 0;
}

// require() is written in Lua (see the prelude) so a module may wait() while it starts.
// __gb_moduleLoad(module) -> true, value (already ran) | false, fn (run it, then save) |
//                            nil (another script is running it right now: try again)
int l_moduleLoad(lua_State* L) {
    SceneNode* m = LuaApi::checkNode(L, 1);
    if (!m->isScript()) return luaL_error(L, "require() needs a ModuleScript, got %s", m->name.c_str());
    auto table = [L](const char* key) {
        lua_getfield(L, LUA_REGISTRYINDEX, key);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, key);
        }
    };
    table("__gb_modules");
    lua_rawgeti(L, -1, (lua_Integer)m->id);
    if (!lua_isnil(L, -1)) { lua_pushboolean(L, 1); lua_insert(L, -2); return 2; }   // already loaded
    lua_pop(L, 2);
    table("__gb_modulesLoading");
    lua_rawgeti(L, -1, (lua_Integer)m->id);
    if (lua_toboolean(L, -1)) return 0;                                          // someone's running it
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    lua_rawseti(L, -2, (lua_Integer)m->id);
    lua_pop(L, 1);
    std::string chunk = "=" + m->fullName();
    lua_pushboolean(L, 0);
    if (luaL_loadbuffer(L, m->source.data(), m->source.size(), chunk.c_str()) != LUA_OK) {
        table("__gb_modulesLoading");
        lua_pushnil(L);
        lua_rawseti(L, -2, (lua_Integer)m->id);
        lua_pop(L, 1);
        return lua_error(L);
    }
    // Its own globals (falling back to the shared ones), with `script` = the module.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    LuaApi::pushInstance(L, m->id);
    lua_setfield(L, -2, "script");
    lua_setupvalue(L, -2, 1);
    return 2;
}

// __gb_moduleSave(module, ok, result): remember what it returned (or pass its error on).
int l_moduleSave(lua_State* L) {
    SceneNode* m = LuaApi::checkNode(L, 1);
    const bool ok = lua_toboolean(L, 2);
    lua_settop(L, 3);
    lua_getfield(L, LUA_REGISTRYINDEX, "__gb_modulesLoading");
    if (lua_istable(L, -1)) { lua_pushnil(L); lua_rawseti(L, -2, (lua_Integer)m->id); }
    lua_pop(L, 1);
    if (!ok) return lua_error(L);                       // the module's own error, as it was
    if (lua_isnil(L, 3)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
    lua_getfield(L, LUA_REGISTRYINDEX, "__gb_modules");
    lua_pushvalue(L, 3);
    lua_rawseti(L, -2, (lua_Integer)m->id);              // cache it
    lua_pop(L, 1);
    return 1;
}

// __gb_libraryModel(id) -> "wait" | "ok", MainModule | "fail", why   (require(ID), in the prelude)
int l_libraryModel(lua_State* L) {
    ScriptEngine* e = LuaApi::engine(L);
    if (e->runMode() == ScriptEngine::RunMode::Client)
        return luaL_error(L, "require(ID) only works on the server (in a Script or the server's dev console)");
    std::string id = luaL_checkstring(L, 1);
    for (const char* prefix : {"gb:", "rbxassetid://"})
        if (id.rfind(prefix, 0) == 0) id = id.substr(std::strlen(prefix));
    const bool digits = !id.empty() && id.size() <= 20 &&
                        std::all_of(id.begin(), id.end(), [](char c) { return std::isdigit((unsigned char)c); });
    if (!digits) { lua_pushstring(L, "fail"); lua_pushfstring(L, "require: \"%s\" isn't a Library ID (it's a number, like 1234)", id.c_str()); return 2; }
    uint64_t module = 0;
    std::string err;
    switch (e->libraryModule(id, module, err)) {
        case 0: lua_pushstring(L, "wait"); return 1;
        case 1: lua_pushstring(L, "ok"); LuaApi::pushInstance(L, module); return 2;
        default: lua_pushstring(L, "fail"); lua_pushstring(L, err.c_str()); return 2;
    }
}

int l_time(lua_State* L) { lua_pushnumber(L, LuaApi::engine(L)->time()); return 1; }
int l_tick(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, duration<double>(system_clock::now().time_since_epoch()).count());
    return 1;
}

int l_padState(lua_State* L) {   // the controller's sticks and triggers
    const bool on = Pad::connected() && !ImGui::GetIO().WantTextInput;
    const glm::vec2 ls = on ? Pad::leftStick() : glm::vec2(0.0f), rs = on ? Pad::rightStick() : glm::vec2(0.0f);
    lua_pushnumber(L, ls.x); lua_pushnumber(L, ls.y);
    lua_pushnumber(L, rs.x); lua_pushnumber(L, rs.y);
    lua_pushnumber(L, on ? Pad::leftTrigger() : 0.0f); lua_pushnumber(L, on ? Pad::rightTrigger() : 0.0f);
    return 6;
}

int l_isKeyDown(lua_State* L) {
    lua_pushboolean(L, LuaApi::engine(L)->isKeyDown(luaL_checkstring(L, 1)));
    return 1;
}

// --- DataStoreService: Lua values <-> JSON, kept in a file per game ----------

nlohmann::json toJson(lua_State* L, int idx, int depth = 0) {
    idx = lua_absindex(L, idx);
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN: return (bool)lua_toboolean(L, idx);
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) return (long long)lua_tointeger(L, idx);
            return lua_tonumber(L, idx);
        case LUA_TSTRING: return std::string(lua_tostring(L, idx));
        case LUA_TTABLE: {
            if (depth > 20) luaL_error(L, "That table is nested too deep to save");
            // A list (1..n) saves as an array; anything else as an object with string keys.
            lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
            nlohmann::json out = n > 0 ? nlohmann::json::array() : nlohmann::json::object();
            if (n > 0) {
                for (lua_Integer i = 1; i <= n; ++i) {
                    lua_rawgeti(L, idx, i);
                    out.push_back(toJson(L, -1, depth + 1));
                    lua_pop(L, 1);
                }
                return out;
            }
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                lua_pushvalue(L, -2);
                std::string key = luaL_tolstring(L, -1, nullptr);
                lua_pop(L, 2);   // the tostring'd key and its copy
                out[key] = toJson(L, -1, depth + 1);
                lua_pop(L, 1);
            }
            return out;
        }
        case LUA_TNIL: return nullptr;
        default: luaL_error(L, "DataStores can only save numbers, text, true/false and tables of those");
    }
    return nullptr;
}

void pushJson(lua_State* L, const nlohmann::json& j) {
    if (j.is_boolean()) lua_pushboolean(L, j.get<bool>());
    else if (j.is_number_integer()) lua_pushinteger(L, j.get<lua_Integer>());
    else if (j.is_number()) lua_pushnumber(L, j.get<double>());
    else if (j.is_string()) lua_pushstring(L, j.get<std::string>().c_str());
    else if (j.is_array()) {
        lua_createtable(L, (int)j.size(), 0);
        for (size_t i = 0; i < j.size(); ++i) { pushJson(L, j[i]); lua_rawseti(L, -2, (lua_Integer)i + 1); }
    } else if (j.is_object()) {
        lua_createtable(L, 0, (int)j.size());
        for (auto& [k, v] : j.items()) { pushJson(L, v); lua_setfield(L, -2, k.c_str()); }
    } else lua_pushnil(L);
}

// __gb_dsStart(kind, store, key, delta) -> ticket; __gb_dsDone(ticket) -> done, value
int l_dsStart(lua_State* L) {
    if (LuaApi::engine(L)->runMode() == ScriptEngine::RunMode::Client)
        return luaL_error(L, "DataStores only work in Scripts on the server (this is a LocalScript)");
    std::string kind = luaL_checkstring(L, 1), store = luaL_checkstring(L, 2), key = luaL_checkstring(L, 3);
    if (store.empty() || key.empty() || store.size() > 100 || key.size() > 100)
        return luaL_error(L, "DataStore names and keys are 1-100 letters");
    lua_pushinteger(L, LuaApi::engine(L)->dataStart(kind == "inc" ? "inc" : "get", store, key, luaL_optnumber(L, 4, 0)));
    return 1;
}

int l_dsDone(lua_State* L) {
    nlohmann::json v;
    if (!LuaApi::engine(L)->dataDone((int)luaL_checkinteger(L, 1), v)) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, 1);
    pushJson(L, v);
    return 2;
}

int l_dsSet(lua_State* L) {
    if (LuaApi::engine(L)->runMode() == ScriptEngine::RunMode::Client)
        return luaL_error(L, "DataStores only work in Scripts on the server (this is a LocalScript)");
    std::string store = luaL_checkstring(L, 1), key = luaL_checkstring(L, 2);
    if (store.empty() || key.empty() || store.size() > 100 || key.size() > 100)
        return luaL_error(L, "DataStore names and keys are 1-100 letters");
    nlohmann::json v = toJson(L, 3);
    if (v.dump().size() > 256 * 1024) return luaL_error(L, "That's too much to save in one key (256 KB at most)");
    LuaApi::engine(L)->dataSet(store, key, v);
    return 0;
}

// __gb_awardBadge(playerName, badgeId): the app sends it to the server.
int l_awardBadge(lua_State* L) {
    std::string name = luaL_checkstring(L, 1), badge = luaL_checkstring(L, 2);
    ScriptEngine* e = LuaApi::engine(L);
    if (!e->knowsBadge(name, badge)) { e->queueBadge(name, badge); e->markBadge(name, badge); }
    return 0;
}

int l_hasBadge(lua_State* L) {
    lua_pushboolean(L, LuaApi::engine(L)->knowsBadge(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
    return 1;
}

// __gb_teleport(placeId, playerName, data): TeleportService (see the prelude).
int l_teleport(lua_State* L) {
    ScriptEngine* e = LuaApi::engine(L);
    std::string place = luaL_checkstring(L, 1);
    const std::string who = luaL_checkstring(L, 2);
    if (place.rfind("gb:", 0) == 0) place = place.substr(3);
    const bool digits = !place.empty() && place.size() <= 20 &&
                        std::all_of(place.begin(), place.end(), [](char c) { return std::isdigit((unsigned char)c); });
    if (!digits) return luaL_error(L, "Teleport: \"%s\" isn't a game ID (it's the number in the game's link, like 1234)", place.c_str());
    nlohmann::json data = lua_isnoneornil(L, 3) ? nlohmann::json() : toJson(L, 3);
    if (data.dump().size() > 64 * 1024) return luaL_error(L, "Teleport data is too big (64 KB at most)");
    if (!e->teleportsWork()) {
        Log::warn("TeleportService: " + who + " would go to game " + place + " now (teleports work in the Guts&Bolts Player, not here)");
        return 0;
    }
    e->queueTeleport({who, place, std::move(data)});
    return 0;
}

// __gb_joinData(playerName) -> { TeleportData = ..., SourcePlaceId = 123 } (or an empty table)
int l_joinData(lua_State* L) {
    const nlohmann::json* j = LuaApi::engine(L)->joinData(luaL_checkstring(L, 1));
    lua_newtable(L);
    if (!j) return 1;
    if (j->contains("data") && !(*j)["data"].is_null()) { pushJson(L, (*j)["data"]); lua_setfield(L, -2, "TeleportData"); }
    const std::string from = j->value("from", std::string());
    if (!from.empty()) {
        char* end = nullptr;
        const double n = std::strtod(from.c_str(), &end);
        if (end && *end == 0 && n == std::floor(n) && n < 9e15) lua_pushinteger(L, (lua_Integer)n);
        else lua_pushstring(L, from.c_str());
        lua_setfield(L, -2, "SourcePlaceId");
    }
    return 1;
}

// Game passes (MarketplaceService in the Lua prelude).
int l_passLookup(lua_State* L) { LuaApi::engine(L)->lookUpPasses((int)luaL_checkinteger(L, 1)); return 0; }
int l_passReady(lua_State* L) { lua_pushboolean(L, LuaApi::engine(L)->passesReady((int)luaL_checkinteger(L, 1))); return 1; }
int l_passOwns(lua_State* L) {
    lua_pushboolean(L, LuaApi::engine(L)->ownsPass((int)luaL_checkinteger(L, 1), luaL_checkstring(L, 2)));
    return 1;
}
int l_passPrompt(lua_State* L) { LuaApi::engine(L)->queuePassPrompt((int)luaL_checkinteger(L, 1), luaL_checkstring(L, 2)); return 0; }
// Developer products: receiptCheck(userId), receipts() -> { {userId, receiptId, productId, price}, ... },
// receiptDone(userId, receiptId, granted).
int l_receiptCheck(lua_State* L) { LuaApi::engine(L)->checkReceipts((int)luaL_checkinteger(L, 1)); return 0; }
int l_receipts(lua_State* L) {
    auto list = LuaApi::engine(L)->takeReceipts();
    lua_createtable(L, (int)list.size(), 0);
    for (size_t i = 0; i < list.size(); ++i) {
        lua_createtable(L, 4, 0);
        lua_pushinteger(L, list[i].userId); lua_rawseti(L, -2, 1);
        lua_pushstring(L, list[i].id.c_str()); lua_rawseti(L, -2, 2);
        lua_pushstring(L, list[i].product.c_str()); lua_rawseti(L, -2, 3);
        lua_pushinteger(L, (lua_Integer)list[i].price); lua_rawseti(L, -2, 4);
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}
int l_receiptDone(lua_State* L) {
    LuaApi::engine(L)->receiptDone((int)luaL_checkinteger(L, 1), luaL_checkstring(L, 2), lua_toboolean(L, 3));
    return 0;
}
// Purchase prompts that have finished: { {userId, passId, bought}, ... }
int l_passResults(lua_State* L) {
    auto results = LuaApi::engine(L)->takePassResults();
    lua_createtable(L, (int)results.size(), 0);
    for (size_t i = 0; i < results.size(); ++i) {
        lua_createtable(L, 3, 0);
        lua_pushinteger(L, results[i].userId); lua_rawseti(L, -2, 1);
        lua_pushstring(L, results[i].pass.c_str()); lua_rawseti(L, -2, 2);
        lua_pushboolean(L, results[i].bought); lua_rawseti(L, -2, 3);
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 1;
}

int l_playerNode(lua_State* L) {
    LuaApi::pushInstance(L, LuaApi::engine(L)->playerNode(luaL_checkstring(L, 1)));
    return 1;
}

// LocalPlayer.RespawnLocation = part (nil = back to the normal spawn)
int l_setRespawn(lua_State* L) {
    Player* p = LuaApi::engine(L)->scene()->player();
    if (p) p->setCheckpoint(lua_isnil(L, 1) ? 0 : LuaApi::checkNode(L, 1)->id);
    return 0;
}

// player.CameraMode / CameraMinZoomDistance / CameraMaxZoomDistance (see the prelude).
// Our own player's live here; another player's (on the host) are sent to them.
CameraOptions* cameraOptionsOf(lua_State* L, const std::string& player, bool forWriting) {
    ScriptEngine* e = LuaApi::engine(L);
    Scene& scene = *e->scene();
    if (!e->noLocalPlayer() && player == e->playerName()) {
        ScriptCamera& c = scene.camera();
        if (!c.optionsSet && forWriting) { c.options = scene.world().playerCamera; c.optionsSet = true; }
        return c.optionsSet ? &c.options : &scene.world().playerCamera;
    }
    for (RemoteCharacter& r : scene.remotes())
        if (r.name == player) {
            if (!r.cameraSet && forWriting) { r.camera = scene.world().playerCamera; r.cameraSet = true; }
            return r.cameraSet ? &r.camera : &scene.world().playerCamera;
        }
    return forWriting ? nullptr : &scene.world().playerCamera;
}
// GuiService:SetEmotesMenuOpen(open) / GetEmotesMenuOpen() (see the prelude). With no
// argument it just says whether the emote menu is open; it can't open while a game has
// turned it off (StarterGui:SetCoreGuiEnabled) or there's no character.
int l_emotesMenu(lua_State* L) {
    ScriptEngine* e = LuaApi::engine(L);
    CoreGuiState& g = e->scene()->coreGui();
    if (lua_gettop(L) >= 1)
        g.emotesOpen = lua_toboolean(L, 1) && g.enabled("EmotesMenu") && !e->noLocalPlayer() && e->scene()->player();
    lua_pushboolean(L, g.emotesOpen);
    return 1;
}
int l_camOpt(lua_State* L) {
    const CameraOptions* o = cameraOptionsOf(L, luaL_checkstring(L, 1), false);
    const std::string k = luaL_checkstring(L, 2);
    if (k == "CameraMode") lua_pushstring(L, o->lockFirstPerson ? "LockFirstPerson" : "Classic");
    else if (k == "CameraMinZoomDistance") lua_pushnumber(L, o->minZoom);
    else lua_pushnumber(L, o->maxZoom);
    return 1;
}
int l_setCamOpt(lua_State* L) {
    const std::string k = luaL_checkstring(L, 2);
    CameraOptions want;
    if (k == "CameraMode") {
        std::string m = luaL_checkstring(L, 3);   // Enum.CameraMode.LockFirstPerson -> "LockFirstPerson"
        if (size_t dot = m.rfind('.'); dot != std::string::npos) m = m.substr(dot + 1);
        if (m != "Classic" && m != "LockFirstPerson")
            return luaL_error(L, "'%s' is not a CameraMode (use Classic or LockFirstPerson)", m.c_str());
        want.lockFirstPerson = m == "LockFirstPerson";
    }
    const float n = k == "CameraMode" ? 0.0f : std::clamp((float)luaL_checknumber(L, 3), 0.0f, CameraOptions::kLimit);
    CameraOptions* o = cameraOptionsOf(L, luaL_checkstring(L, 1), true);
    if (!o) return 0;   // (someone who isn't here any more)
    if (k == "CameraMode") o->lockFirstPerson = want.lockFirstPerson;
    else if (k == "CameraMinZoomDistance") o->minZoom = n;
    else o->maxZoom = n;
    return 0;
}

// Gui.Message(text, seconds), Gui.Label(key, text), Gui.Clear()
int gui_message(lua_State* L) {
    GuiState& g = LuaApi::engine(L)->gui();
    g.message     = luaL_optstring(L, 1, "");
    g.messageTime = (float)luaL_optnumber(L, 2, 3.0);
    return 0;
}
int gui_label(lua_State* L) {
    GuiState& g = LuaApi::engine(L)->gui();
    std::string key = luaL_checkstring(L, 1);
    if (lua_isnoneornil(L, 2)) g.labels.erase(key);
    else g.labels[key] = luaL_tolstring(L, 2, nullptr);
    return 0;
}
int gui_clear(lua_State* L) {
    GuiState& g = LuaApi::engine(L)->gui();
    g.labels.clear();
    g.message.clear();
    return 0;
}

// Explode(position, radius, power)
// __gb_navPath(from, to, params) -> status, { {position, action, label}, ... } (PathfindingService).
// __gb_raycast(origin, direction, filterList, includeOnly) -> part, position, normal, distance
// (and the terrain material, when it hits workspace.Terrain)
// (workspace:Raycast / FindPartOnRay). `direction`'s length is how far to look.
int l_raycast(lua_State* L) {
    Scene* scene = LuaApi::engine(L)->scene();
    const glm::vec3 from = LuaApi::checkVector3(L, 1), dir = LuaApi::checkVector3(L, 2);
    const float range = glm::length(dir);
    if (!scene || range < 1e-6f) return 0;
    std::vector<const SceneNode*> list;
    if (lua_istable(L, 3)) {
        for (lua_Integer i = 1;; ++i) {
            lua_rawgeti(L, 3, i);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
            if (lua_isuserdata(L, -1)) list.push_back(LuaApi::checkNode(L, -1));
            lua_pop(L, 1);
        }
    }
    const bool include = lua_toboolean(L, 4);
    auto inside = [&](const SceneNode* n) {   // n is one of the list, or inside one
        for (const SceneNode* p = n; p; p = p->parent)
            if (std::find(list.begin(), list.end(), p) != list.end()) return true;
        return false;
    };
    auto above = [&](const SceneNode* n) {    // n holds one of the list
        for (const SceneNode* l : list)
            for (const SceneNode* p = l; p; p = p->parent) if (p == n) return true;
        return false;
    };
    const glm::vec3 rd = dir / range;
    float dist = 0.0f;
    SceneNode* hit = Physics::raycastIf(*scene, from, rd, &dist, [&](const SceneNode* n) {
        if (n == scene->root()) return true;
        return include ? (inside(n) || above(n)) : !inside(n);
    });
    if (!hit || dist > range) return 0;
    const glm::vec3 at = from + rd * dist;
    if (hit->id == Scene::kTerrainId) {   // the ground: its slope there, and which ground it is
        LuaApi::pushInstance(L, hit->id);
        LuaApi::pushVector3(L, at);
        LuaApi::pushVector3(L, scene->terrain().normalAt(at.x, at.z));
        lua_pushnumber(L, dist);
        lua_pushstring(L, kTerrainMaterialNames[(int)scene->terrain().materialAt(at.x, at.z)]);
        return 5;
    }
    const OBB box = Physics::worldOBB(hit);
    glm::vec3 normal(0, 1, 0);
    if (hit->primitiveType == PrimitiveType::Sphere) {
        if (glm::length(at - box.center) > 1e-5f) normal = glm::normalize(at - box.center);
    } else {   // the face of the box it hit
        float best = -1.0f;
        for (int i = 0; i < 3; ++i) {
            const float f = glm::dot(at - box.center, box.axis[i]) / std::max(box.half[i], 1e-4f);
            if (std::abs(f) > best) { best = std::abs(f); normal = box.axis[i] * (f < 0 ? -1.0f : 1.0f); }
        }
    }
    LuaApi::pushInstance(L, hit->id);
    LuaApi::pushVector3(L, at);
    LuaApi::pushVector3(L, normal);
    lua_pushnumber(L, dist);
    return 4;
}

// __gb_addMethod(name, fn): a method every object gets (the prelude adds workspace:Raycast etc. this way).
int l_addMethod(lua_State* L) {
    luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, lua_tostring(L, 1));
    return 0;
}

// __gb_storage(name): ReplicatedStorage / ServerStorage - a hidden folder for templates
// (what Roblox imports put there too).
int l_storage(lua_State* L) {
    Scene* scene = LuaApi::engine(L)->scene();
    const std::string name = luaL_checkstring(L, 1);
    if (!scene) return 0;
    for (auto& c : scene->root()->children)
        if (c->name == name && c->kind == NodeKind::Model) { LuaApi::pushInstance(L, c->id); return 1; }
    auto folder = std::make_unique<SceneNode>(name, NodeKind::Model);
    folder->visible = false;
    LuaApi::pushInstance(L, scene->insert(std::move(folder))->id);
    return 1;
}

int l_navPath(lua_State* L) {
    glm::vec3 from = LuaApi::checkVector3(L, 1), to = LuaApi::checkVector3(L, 2);
    NavMesh::Agent agent = LuaApi::checkAgent(L, 3);
    const Physics* ph = LuaApi::engine(L)->physics();
    if (!ph) { lua_pushstring(L, "NoPath"); lua_newtable(L); return 2; }
    std::vector<NavMesh::Waypoint> wps;
    NavMesh::Status st = ph->navMesh().findPath(from, to, agent, wps);
    lua_pushstring(L, NavMesh::statusName(st));
    lua_createtable(L, (int)wps.size(), 0);
    for (size_t i = 0; i < wps.size(); ++i) {
        lua_createtable(L, 3, 0);
        LuaApi::pushVector3(L, wps[i].pos);  lua_rawseti(L, -2, 1);
        lua_pushstring(L, wps[i].action == NavMesh::Action::Jump ? "Jump" : "Walk"); lua_rawseti(L, -2, 2);
        lua_pushstring(L, wps[i].label.c_str()); lua_rawseti(L, -2, 3);
        lua_rawseti(L, -2, (int)i + 1);
    }
    return 2;
}

// __gb_navQuery(what, ...): the navmesh's other questions (see PathfindingService below).
int l_navQuery(lua_State* L) {
    const std::string what = luaL_checkstring(L, 1);
    const Physics* ph = LuaApi::engine(L)->physics();
    if (!ph) { lua_pushnil(L); return 1; }
    if (what == "bake") {
        ph->rebakeNavMesh();
        lua_pushnumber(L, ph->navMesh().bakeMs());
        lua_pushinteger(L, (lua_Integer)ph->navMesh().spanCount());
        return 2;
    }
    if (what == "settings") {   // SetBakeSettings({CellSize=, MaxSlope=, JumpHeight=, JumpGap=, MaxDrop=, StepHeight=})
        NavMesh::Settings st = ph->navMesh().settings();
        luaL_checktype(L, 2, LUA_TTABLE);
        auto num = [&](const char* k, float& out) {
            lua_getfield(L, 2, k);
            if (lua_isnumber(L, -1)) out = (float)lua_tonumber(L, -1);
            lua_pop(L, 1);
        };
        num("CellSize", st.cell); num("MaxSlope", st.maxSlope); num("JumpHeight", st.jumpHeight);
        num("JumpGap", st.jumpGap); num("MaxDrop", st.maxDrop); num("StepHeight", st.maxClimb);
        st.cell = std::clamp(st.cell, 0.2f, 4.0f);
        ph->setNavSettings(st);
        return 0;
    }
    const NavMesh& nav = ph->navMesh();
    if (what == "version") { lua_pushinteger(L, nav.version()); return 1; }
    if (what == "walkable") {
        lua_pushboolean(L, nav.walkable(LuaApi::checkVector3(L, 2), LuaApi::checkAgent(L, 3)));
        return 1;
    }
    if (what == "closest") {
        glm::vec3 out;
        if (nav.closestPoint(LuaApi::checkVector3(L, 2), LuaApi::checkAgent(L, 4), (float)luaL_optnumber(L, 3, 10.0), out))
            LuaApi::pushVector3(L, out);
        else
            lua_pushnil(L);
        return 1;
    }
    if (what == "straight") {
        glm::vec3 hit;
        bool ok = nav.straightWalk(LuaApi::checkVector3(L, 2), LuaApi::checkVector3(L, 3), LuaApi::checkAgent(L, 4), &hit);
        lua_pushboolean(L, ok);
        if (ok) LuaApi::pushVector3(L, LuaApi::checkVector3(L, 3)); else LuaApi::pushVector3(L, hit);
        return 2;
    }
    if (what == "random") {
        glm::vec3 around = lua_isnoneornil(L, 2) ? glm::vec3(0.0f) : LuaApi::checkVector3(L, 2);
        float radius = lua_isnoneornil(L, 2) ? 0.0f : (float)luaL_optnumber(L, 3, 20.0);
        static uint32_t seed = 1;
        glm::vec3 out;
        if (nav.randomPoint(around, radius, LuaApi::checkAgent(L, 4), seed++, out)) LuaApi::pushVector3(L, out);
        else lua_pushnil(L);
        return 1;
    }
    return luaL_error(L, "unknown navmesh question '%s'", what.c_str());
}

// Explode(position, radius, power, options) -> { {part, distance}, ... }
// options (all optional): Smoke = true, Fire = seconds, MushroomCloud = true / false,
// Destroy = true (rips anchored parts loose: craters), Visible = false, Hurts = false,
// JointBreak = 0..1 (how much of the radius breaks joints).
int l_explode(lua_State* L) {
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    BlastOptions o;
    o.radius = (float)luaL_optnumber(L, 2, 6.0);
    o.power = (float)luaL_optnumber(L, 3, 1.0);
    if (lua_istable(L, 4)) {
        auto flag = [&](const char* k, bool& out) { lua_getfield(L, 4, k); if (!lua_isnil(L, -1)) out = lua_toboolean(L, -1); lua_pop(L, 1); };
        auto num = [&](const char* k, float& out) { lua_getfield(L, 4, k); if (lua_isnumber(L, -1)) out = (float)lua_tonumber(L, -1); lua_pop(L, 1); };
        flag("Smoke", o.smoke); flag("Destroy", o.destroy); flag("Visible", o.visible); flag("Hurts", o.hurts);
        num("Fire", o.fire); num("JointBreak", o.jointBreak);
        lua_getfield(L, 4, "MushroomCloud");
        if (lua_isboolean(L, -1)) o.mushroom = lua_toboolean(L, -1) ? 1 : 0;
        lua_pop(L, 1);
        o.jointBreak = std::clamp(o.jointBreak, 0.0f, 1.0f);
    }
    auto hits = Effects::blast(*LuaApi::engine(L)->scene(), pos, o);
    lua_createtable(L, (int)hits.size(), 0);
    int i = 0;
    for (const auto& [part, dist] : hits) {
        lua_createtable(L, 2, 0);
        LuaApi::pushInstance(L, part->id); lua_rawseti(L, -2, 1);
        lua_pushnumber(L, dist);           lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// Effects.Blood(position, amount), Effects.Oil(...), Effects.Sparks(...), Effects.Gibs(...)
int fx_spray(lua_State* L, GoreKind kind) {
    Scene* s = LuaApi::engine(L)->scene();
    if (!s->goreEnabled()) return 0;
    int n = (int)luaL_optinteger(L, 2, 20);
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    // Optional third argument: which way (and how hard) it sprays, e.g. Vector3.new(0, 2, -8).
    glm::vec3 dir(0, 1, 0);
    float speed = 3.0f;
    if (lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
        glm::vec3 d = LuaApi::checkVector3(L, 3);
        if (glm::length(d) > 1e-3f) { speed = std::min(glm::length(d), 40.0f); dir = d / glm::length(d); }
    }
    s->particles().spray(kind, pos, dir, std::clamp(n, 1, 300), speed);
    s->pushFx(kind == GoreKind::Blood ? FxEvent::Blood : FxEvent::Oil, pos, (float)n);
    return 0;
}
int fx_blood(lua_State* L) { return fx_spray(L, GoreKind::Blood); }
int fx_oil(lua_State* L)   { return fx_spray(L, GoreKind::Oil); }
int fx_gibs(lua_State* L) {
    Scene* s = LuaApi::engine(L)->scene();
    if (!s->goreEnabled()) return 0;
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    int n = std::clamp((int)luaL_optinteger(L, 2, 8), 1, 60);
    s->particles().gibs(s->goreKind(), pos, glm::vec3(0, 2, 0), n);
    s->pushFx(FxEvent::Gibs, pos, (float)n);
    return 0;
}
int fx_sparks(lua_State* L) {
    Scene* s = LuaApi::engine(L)->scene();
    glm::vec3 pos = LuaApi::checkVector3(L, 1);
    int n = std::clamp((int)luaL_optinteger(L, 2, 20), 1, 300);
    s->particles().sparks(pos, n);
    s->pushFx(FxEvent::Sparks, pos, (float)n);
    return 0;
}

// Sounds.Play(name, position, volume) — quick one-off sound effects.
int snd_play(lua_State* L) {
    std::string name = luaL_checkstring(L, 1);
    glm::vec3* pos = LuaApi::toVector3(L, 2);
    float volume = (float)luaL_optnumber(L, 3, 0.7);
    Audio::play(name, volume, 1.0f, false, pos);
    LuaApi::engine(L)->scene()->pushFx(FxEvent::Sound, pos ? *pos : glm::vec3(0.0f), volume, name);
    return 0;
}
int snd_list(lua_State* L) {
    lua_newtable(L);
    int i = 1;
    for (const auto& n : Audio::builtinNames()) { lua_pushstring(L, n.c_str()); lua_rawseti(L, -2, i++); }
    return 1;
}

void timeoutHook(lua_State* L, lua_Debug*) { LuaApi::engine(L)->checkTimeout(L); }

} // namespace

// ===========================================================================

namespace LuaApi {
ScriptEngine* engine(lua_State* L) { return *static_cast<ScriptEngine**>(lua_getextraspace(L)); }

NavMesh::Agent checkAgent(lua_State* L, int idx) {
    NavMesh::Agent a;
    if (lua_isnoneornil(L, idx) || !lua_istable(L, idx)) return a;
    idx = lua_absindex(L, idx);
    auto num = [&](const char* k, float& out) {
        lua_getfield(L, idx, k);
        if (lua_isnumber(L, -1)) out = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    };
    num("AgentRadius", a.radius);
    num("AgentHeight", a.height);
    num("WaypointSpacing", a.spacing);
    // Copied from a Roblox game (AgentHeight 5, AgentRadius 2...)? Those are Roblox
    // studs: characters here are half that size.
    if (a.height >= 4.0f) { a.radius *= 0.5f; a.height *= 0.5f; a.spacing *= 0.5f; }
    a.radius = std::clamp(a.radius, 0.1f, 20.0f);
    a.height = std::clamp(a.height, 0.5f, 50.0f);
    if (!std::isfinite(a.spacing) || a.spacing <= 0.0f) a.spacing = 0.0f;   // math.huge: corners only
    lua_getfield(L, idx, "AgentCanJump");
    if (lua_isboolean(L, -1)) a.canJump = lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, idx, "Costs");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1)) {
                double v = lua_tonumber(L, -1);
                a.costs[lua_tostring(L, -2)] = !std::isfinite(v) || v > 1e8 ? 1e9f : (float)std::max(0.01, v);
            }
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    return a;
}
}

ScriptEngine::ScriptEngine(Scene* scene) : m_scene(scene) {}
ScriptEngine::~ScriptEngine() { stop(); }

void ScriptEngine::openLibraries() {
    lua_State* L = m_L;
    static const luaL_Reg libs[] = {
        {LUA_GNAME, luaopen_base},        {LUA_COLIBNAME, luaopen_coroutine},
        {LUA_TABLIBNAME, luaopen_table},  {LUA_STRLIBNAME, luaopen_string},
        {LUA_MATHLIBNAME, luaopen_math},  {LUA_UTF8LIBNAME, luaopen_utf8},
        {LUA_OSLIBNAME, luaopen_os},      {nullptr, nullptr}};
    for (const luaL_Reg* lib = libs; lib->func; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    // Sandbox: scripts can't touch files or run programs.
    for (const char* name : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    lua_getglobal(L, "os");
    for (const char* name : {"execute", "exit", "remove", "rename", "tmpname", "getenv", "setlocale"}) {
        lua_pushnil(L);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
}

void ScriptEngine::start(bool runScripts) {
    stop();
    m_L = luaL_newstate();
    *static_cast<ScriptEngine**>(lua_getextraspace(m_L)) = this;
    m_time = 0.0;
    m_depth = 0;
    m_gui = GuiState{};
    // Every coroutine inherits this hook, which stops runaway loops.
    lua_sethook(m_L, timeoutHook, LUA_MASKCOUNT, 20000);

    openLibraries();
    LuaApi::registerTypes(m_L);
    LuaApi::registerInstance(m_L);
    openRemotes();

    lua_State* L = m_L;
    lua_register(L, "print", l_print);
    lua_register(L, "warn",  l_warn);
    lua_register(L, "time",  l_time);
    lua_register(L, "__gb_teleport", l_teleport);
    lua_register(L, "__gb_joinData", l_joinData);
    lua_register(L, "__gb_moduleLoad", l_moduleLoad);
    lua_register(L, "__gb_moduleSave", l_moduleSave);
    lua_register(L, "__gb_libraryModel", l_libraryModel);
    lua_register(L, "tick",  l_tick);
    lua_register(L, "__gb_wait",  l_wait);
    lua_register(L, "__gb_spawn", l_spawn);
    lua_register(L, "__gb_delay", l_delay);
    lua_register(L, "__gb_isKeyDown", l_isKeyDown);
    lua_register(L, "__gb_padState", l_padState);
    lua_register(L, "__gb_playerNode", l_playerNode);
    lua_register(L, "__gb_setRespawn", l_setRespawn);
    lua_register(L, "__gb_camOpt", l_camOpt);
    lua_register(L, "__gb_emotesMenu", l_emotesMenu);
    lua_register(L, "__gb_setCamOpt", l_setCamOpt);
    lua_register(L, "__gb_dsStart", l_dsStart);
    lua_register(L, "__gb_dsDone", l_dsDone);
    lua_register(L, "__gb_dsSet", l_dsSet);
    lua_register(L, "__gb_navPath", l_navPath);
    lua_register(L, "__gb_navQuery", l_navQuery);
    lua_register(L, "__gb_raycast", l_raycast);
    lua_register(L, "__gb_storage", l_storage);
    lua_register(L, "__gb_addMethod", l_addMethod);
    lua_register(L, "__gb_awardBadge", l_awardBadge);
    lua_register(L, "__gb_hasBadge", l_hasBadge);
    lua_register(L, "__gb_passLookup", l_passLookup);
    lua_register(L, "__gb_passReady", l_passReady);
    lua_register(L, "__gb_passOwns", l_passOwns);
    lua_register(L, "__gb_passPrompt", l_passPrompt);
    lua_register(L, "__gb_passResults", l_passResults);
    lua_register(L, "__gb_receiptCheck", l_receiptCheck);
    lua_register(L, "__gb_receipts", l_receipts);
    lua_register(L, "__gb_receiptDone", l_receiptDone);

    LuaApi::pushInstance(L, m_scene->root()->id);
    lua_setglobal(L, "workspace");
    LuaApi::pushInstance(L, m_scene->player() ? m_scene->player()->rootId() : 0);
    lua_setglobal(L, "__gb_character");
    LuaApi::pushInstance(L, m_scene->player() ? m_scene->player()->backpackId() : 0);
    lua_setglobal(L, "__gb_backpack");
    lua_pushstring(L, m_playerName.c_str());
    lua_setglobal(L, "__gb_playerName");
    lua_pushboolean(L, m_noLocalPlayer);
    lua_setglobal(L, "__gb_noLocalPlayer");
    LuaApi::pushLighting(L);
    lua_setglobal(L, "Lighting");
    LuaApi::pushSignal(L, SignalKind::Heartbeat, 0);  lua_setglobal(L, "__gb_heartbeat");
    LuaApi::pushSignal(L, SignalKind::InputBegan, 0); lua_setglobal(L, "__gb_inputBegan");
    LuaApi::pushSignal(L, SignalKind::InputEnded, 0); lua_setglobal(L, "__gb_inputEnded");
    LuaApi::pushSignal(L, SignalKind::PlayerAdded, 0);    lua_setglobal(L, "__gb_playerAdded");
    LuaApi::pushSignal(L, SignalKind::PlayerRemoving, 0); lua_setglobal(L, "__gb_playerRemoving");
    LuaApi::pushSignal(L, SignalKind::TagAdded, 0);       lua_setglobal(L, "__gb_tagAdded");
    LuaApi::pushSignal(L, SignalKind::TagRemoved, 0);     lua_setglobal(L, "__gb_tagRemoved");
    LuaApi::pushSignal(L, SignalKind::PromptTriggered, 0);    lua_setglobal(L, "__gb_promptTriggered");
    LuaApi::pushSignal(L, SignalKind::PromptTriggerEnded, 0); lua_setglobal(L, "__gb_promptTriggerEnded");
    LuaApi::pushSignal(L, SignalKind::PromptHoldBegan, 0);    lua_setglobal(L, "__gb_promptHoldBegan");
    LuaApi::pushSignal(L, SignalKind::PromptHoldEnded, 0);    lua_setglobal(L, "__gb_promptHoldEnded");
    LuaApi::pushSignal(L, SignalKind::PromptShown, 0);        lua_setglobal(L, "__gb_promptShown");
    LuaApi::pushSignal(L, SignalKind::PromptHidden, 0);       lua_setglobal(L, "__gb_promptHidden");

    lua_register(L, "Explode", l_explode);
    lua_newtable(L);
    lua_pushcfunction(L, snd_play); lua_setfield(L, -2, "Play");
    lua_pushcfunction(L, snd_list); lua_setfield(L, -2, "List");
    lua_setglobal(L, "Sounds");
    lua_newtable(L);
    lua_pushcfunction(L, l_explode);  lua_setfield(L, -2, "Explosion");
    lua_pushcfunction(L, fx_blood);   lua_setfield(L, -2, "Blood");
    lua_pushcfunction(L, fx_oil);     lua_setfield(L, -2, "Oil");
    lua_pushcfunction(L, fx_gibs);    lua_setfield(L, -2, "Gibs");
    lua_pushcfunction(L, fx_sparks);  lua_setfield(L, -2, "Sparks");
    lua_setglobal(L, "Effects");

    lua_newtable(L);
    lua_pushcfunction(L, gui_message); lua_setfield(L, -2, "Message");
    lua_pushcfunction(L, gui_label);   lua_setfield(L, -2, "Label");
    lua_pushcfunction(L, gui_clear);   lua_setfield(L, -2, "Clear");
    lua_setglobal(L, "Gui");

    m_resumeStart = nowSeconds();
    ++m_depth;
    if (luaL_loadbuffer(L, kPrelude, std::strlen(kPrelude), "=prelude") != LUA_OK ||
        lua_pcall(L, 0, 0, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    --m_depth;
    // For Studio's AI tools: an object by its id number (they refer to objects that way).
    lua_register(L, "__gb_byId", [](lua_State* L) -> int {
        uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
        if (LuaApi::engine(L)->scene()->findById(id)) LuaApi::pushInstance(L, id);
        else lua_pushnil(L);
        return 1;
    });
    // Remote messages name players by UserId (see the prelude).
    lua_getglobal(L, "__gb_playerById");
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.playerById");
    // obj:GetAttributeChangedSignal(name) is built in Lua (see the prelude).
    lua_getglobal(L, "__gb_attrSignal");
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.attrSignal");
    lua_pushnil(L);
    lua_setglobal(L, "__gb_attrSignal");
    lua_getglobal(L, "__gb_cmdRun");
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.cmdRun");
    lua_pushnil(L);
    lua_setglobal(L, "__gb_cmdRun");

    if (!runScripts) return;
    // Collect first, then run: scripts may add or remove objects as they start.
    std::vector<uint64_t> scripts;
    m_scene->forEach([&](SceneNode* n) {
        if (n->isScript() && n->enabled && !n->isModule && wantsScript(n))   // modules wait for require()
            scripts.push_back(n->id);
    });
    for (uint64_t id : scripts)
        if (SceneNode* s = resolve(id)) runScript(s);
}

bool ScriptEngine::runCommand(const std::string& code, std::string& error) {
    if (!m_L) { error = "not running"; return false; }
    lua_State* L = m_L;
    // "= 5 + 5" style: try it as an expression first, so results get printed.
    std::string asExpr = "return " + code;
    bool expr = luaL_loadbuffer(L, asExpr.data(), asExpr.size(), "=CommandBar") == LUA_OK;
    if (!expr) {
        lua_pop(L, 1);
        if (luaL_loadbuffer(L, code.data(), code.size(), "=CommandBar") != LUA_OK) {
            error = lua_tostring(L, -1);
            lua_pop(L, 1);
            return false;
        }
    }
    if (expr) {   // run it through the printer
        lua_getfield(L, LUA_REGISTRYINDEX, "GB.cmdRun");
        lua_insert(L, -2);
    }
    // Commands run like a script's body, so wait() and require(ID) work in them (errors go to Output).
    const uint64_t was = m_current;
    m_current = 0;
    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, co, expr ? 2 : 1);
    resume(co, ref, expr ? 1 : 0, L);
    m_current = was;
    return true;
}

bool ScriptEngine::runChunk(const std::string& code, const std::string& chunkName, std::string& error) {
    if (!m_L) { error = "not running"; return false; }
    lua_State* L = m_L;
    int top = lua_gettop(L);
    if (luaL_loadbuffer(L, code.data(), code.size(), ("=" + chunkName).c_str()) != LUA_OK) {
        error = lua_tostring(L, -1);
        lua_settop(L, top);
        return false;
    }
    m_resumeStart = nowSeconds();
    ++m_depth;
    int status = lua_pcall(L, 0, 0, 0);
    --m_depth;
    if (status != LUA_OK) error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
    lua_settop(L, top);
    return status == LUA_OK;
}

bool ScriptEngine::callRef(int ref, std::string& error) {
    if (!m_L) { error = "not running"; return false; }
    lua_State* L = m_L;
    int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    m_resumeStart = nowSeconds();
    ++m_depth;
    int status = lua_pcall(L, 0, 0, 0);
    --m_depth;
    if (status != LUA_OK) error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "error";
    lua_settop(L, top);
    return status == LUA_OK;
}

void ScriptEngine::runScript(SceneNode* script) {
    lua_State* L = m_L;
    std::string chunk = "=" + script->fullName();
    if (luaL_loadbuffer(L, script->source.data(), script->source.size(), chunk.c_str()) != LUA_OK) {
        Log::error(lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    // Each script gets its own globals table (falling back to the shared one),
    // with `script` pointing at itself.
    lua_newtable(L);
    lua_newtable(L);
    lua_pushglobaltable(L);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    LuaApi::pushInstance(L, script->id);
    lua_setfield(L, -2, "script");
    lua_setupvalue(L, -2, 1);          // becomes the chunk's _ENV

    lua_State* co = lua_newthread(L);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_xmove(L, co, 1);
    m_started.insert(script->id);
    m_stopped.erase(script->id);
    uint64_t prev = m_current;
    m_current = script->id;
    resume(co, ref, 0);
    m_current = prev;
}

void ScriptEngine::stopScripts(SceneNode* root) {
    if (!root) return;
    std::vector<SceneNode*> stack{root};
    std::unordered_set<uint64_t> ids;
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        if (n->isScript()) ids.insert(n->id);
        for (auto& c : n->children) stack.push_back(c.get());
    }
    if (ids.empty()) return;
    m_stopped.insert(ids.begin(), ids.end());
    for (size_t i = 0; i < m_conns.size(); ++i)
        if (m_conns[i].alive && ids.count(m_conns[i].owner)) disconnect((int)i);
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (ids.count(it->owner)) {
            if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, it->ref);
            it = m_waiting.erase(it);
        } else ++it;
    }
}

void ScriptEngine::runScriptsIn(SceneNode* root) {
    if (!m_L || !root) return;
    std::vector<uint64_t> ids;
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        if (n->isScript() && n->enabled && !n->isModule && !m_started.count(n->id) && wantsScript(n))
            ids.push_back(n->id);
        for (auto& c : n->children) stack.push_back(c.get());
    }
    for (uint64_t id : ids)
        if (SceneNode* s = resolve(id)) runScript(s);
}

void ScriptEngine::fireTool(SignalKind kind, uint64_t toolId) { fire(kind, toolId, nullptr); }
void ScriptEngine::fireGui(SignalKind kind, uint64_t id) { fire(kind, id, nullptr); }
void ScriptEngine::fireFocusLost(uint64_t id, bool enter) {
    fire(SignalKind::GuiFocusLost, id, [enter](lua_State* co) { lua_pushboolean(co, enter); return 1; });
}
void ScriptEngine::firePropertyChanged(uint64_t id, const char* property) {
    std::string name = property;
    fire(SignalKind::Changed, id, [name](lua_State* co) { lua_pushstring(co, name.c_str()); return 1; });
}

void ScriptEngine::fireAnimationEvents() {
    std::vector<Anim::Animator::Event> events;
    events.swap(m_scene->animator().events);
    for (const auto& e : events) {
        if (!m_L) return;
        SignalKind k = e.kind == Anim::Animator::Event::Stopped ? SignalKind::AnimStopped
                     : e.kind == Anim::Animator::Event::Ended   ? SignalKind::AnimEnded
                     : e.kind == Anim::Animator::Event::DidLoop ? SignalKind::AnimDidLoop : SignalKind::KeyframeReached;
        std::string name = e.name;
        fire(k, (uint64_t)e.track, [k, name](lua_State* co) {
            if (k != SignalKind::KeyframeReached) return 0;
            lua_pushstring(co, name.c_str());
            return 1;
        });
    }
}

void ScriptEngine::fireValueChanged(uint64_t id) {
    fire(SignalKind::Changed, id, [this, id](lua_State* co) {
        SceneNode* n = resolve(id);
        if (!n) { lua_pushnil(co); return 1; }
        LuaApi::pushValue(co, *n);
        return 1;
    });
}

void ScriptEngine::setScriptEnabled(SceneNode* script, bool on) {
    if (!script || !script->isScript() || script->enabled == on) return;
    script->enabled = on;
    if (!on) stopScripts(script);
    else if (m_L) runScript(script);
}

void ScriptEngine::stop() {
    if (m_L) lua_close(m_L);
    m_L = nullptr;
    m_waiting.clear();
    m_conns.clear();
    m_stopped.clear();
    m_started.clear();
    m_playersRoot = 0;
    m_saveData = nlohmann::json::object();   // the next game reads its own file
    m_saveLoaded = false;
    m_dataMode = DataMode::Unknown;            // (replies for this game are ignored from now on)
    ++m_dataGen;
    m_dataQueue.clear();
    m_dataDone.clear();
    m_dataCache.clear();
    m_libraryModules.clear();
    m_current = 0;
    m_detached.clear();
    m_gui = GuiState{};
}

void ScriptEngine::reportError(lua_State* co) {
    const char* msg = lua_tostring(co, -1);
    Log::error(msg ? msg : "(script error with no message)");
    lua_pop(co, 1);
}

void ScriptEngine::checkTimeout(lua_State* L) {
    if (m_depth > 0 && nowSeconds() - m_resumeStart > kTimeoutSeconds)
        luaL_error(L, "Script ran for more than %d seconds without waiting - "
                      "did you forget a wait() inside a loop?", (int)kTimeoutSeconds);
}

void ScriptEngine::resume(lua_State* co, int ref, int nargs, lua_State* from) {
    if (!m_L) return;
    if (m_depth == 0) m_resumeStart = nowSeconds();
    ++m_depth;
    int nres = 0;
    int status = lua_resume(co, from ? from : m_L, nargs, &nres);
    --m_depth;

    if (status == LUA_YIELD) {
        // Parked by Signal:Wait()? Then the connection owns the thread now.
        for (auto& c : m_conns) {
            if (c.alive && c.waiter == co && c.threadRef == LUA_NOREF) {
                c.threadRef = ref;
                lua_pop(co, nres);
                return;
            }
        }
        double t = (nres > 0 && lua_isnumber(co, -1)) ? lua_tonumber(co, -1) : 0.0;
        lua_pop(co, nres);
        schedule(co, ref, std::max(t, 0.0), -1);
        return;
    }
    if (status != LUA_OK) reportError(co);
    luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

void ScriptEngine::schedule(lua_State* co, int ref, double delay, int startArgs) {
    if (m_stopped.count(m_current)) {   // its script was stopped while it ran
        if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
        return;
    }
    m_waiting.push_back({ref, co, m_time + delay, m_time, startArgs, m_current});
}

void ScriptEngine::update(float dt) {
    if (!m_L) return;
    m_time += dt;

    // Keyboard events.
    if (!ImGui::GetIO().WantTextInput) {
        for (const auto& k : keyTable()) {
            bool began = ImGui::IsKeyPressed(k.key, false);
            bool ended = ImGui::IsKeyReleased(k.key);
            if (!began && !ended) continue;
            const char* name = k.name;
            const char* type = k.type;
            fire(began ? SignalKind::InputBegan : SignalKind::InputEnded, 0, [name, type](lua_State* co) {
                lua_newtable(co);
                lua_pushstring(co, name); lua_setfield(co, -2, "KeyCode");
                lua_pushstring(co, type); lua_setfield(co, -2, "UserInputType");
                lua_pushboolean(co, 0);
                return 2;
            });
        }
    }

    // UserInputService.GamepadEnabled: is a controller plugged in?
    if (const bool pad = Pad::connected(); pad != m_padSeen) {
        m_padSeen = pad;
        lua_getglobal(m_L, "UserInputService");
        if (lua_istable(m_L, -1)) { lua_pushboolean(m_L, pad); lua_setfield(m_L, -2, "GamepadEnabled"); }
        lua_pop(m_L, 1);
    }

    // Wake threads whose wait() is over.
    std::vector<Waiting> ready;
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (it->wakeAt <= m_time) { ready.push_back(*it); it = m_waiting.erase(it); }
        else ++it;
    }
    for (auto& w : ready) {
        if (!m_L) return;
        if (m_stopped.count(w.owner)) { luaL_unref(m_L, LUA_REGISTRYINDEX, w.ref); continue; }
        m_current = w.owner;
        if (w.startArgs >= 0) {
            resume(w.co, w.ref, w.startArgs);
        } else {
            lua_pushnumber(w.co, m_time - w.since);
            resume(w.co, w.ref, 1);
        }
    }
    m_current = 0;

    fire(SignalKind::Heartbeat, 0, [dt](lua_State* co) { lua_pushnumber(co, dt); return 1; });

    if (m_gui.messageTime > 0.0f) {
        m_gui.messageTime -= dt;
        if (m_gui.messageTime <= 0.0f) m_gui.message.clear();
    }
}

void ScriptEngine::fire(SignalKind kind, uint64_t id, const std::function<int(lua_State*)>& pushArgs) {
    if (!m_L) return;
    size_t n = m_conns.size();   // connections made while firing wait for next time
    for (size_t i = 0; i < n; ++i) {
        if (!m_L) return;
        Connection& c = m_conns[i];
        if (!c.alive || c.kind != kind || c.id != id) continue;
        if (m_joinerEvent && c.owner)
            if (SceneNode* s = resolve(c.owner); s && s->isLocal) continue;
        uint64_t prev = m_current;
        m_current = c.owner;
        struct Restore { uint64_t& cur; uint64_t val; ~Restore() { cur = val; } } restore{m_current, prev};

        if (c.waiter) {
            // A thread parked in :Wait() — wake it with the event's arguments.
            if (c.threadRef == LUA_NOREF) continue;
            lua_State* co = c.waiter;
            int ref = c.threadRef;
            c.alive = false;
            int nargs = pushArgs ? pushArgs(co) : 0;
            resume(co, ref, nargs);
            continue;
        }

        lua_State* co = lua_newthread(m_L);
        int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
        lua_rawgeti(co, LUA_REGISTRYINDEX, c.fnRef);
        if (c.once) disconnect((int)i);
        int nargs = pushArgs ? pushArgs(co) : 0;
        resume(co, ref, nargs);
    }
}

void ScriptEngine::fireTouched(uint64_t partId, uint64_t otherId) {
    fire(SignalKind::Touched, partId, [otherId](lua_State* co) {
        LuaApi::pushInstance(co, otherId);
        return 1;
    });
}

void ScriptEngine::fireClicked(uint64_t partId) {
    fire(SignalKind::InputBegan, 0, [](lua_State* co) {
        lua_newtable(co);
        lua_pushstring(co, "Unknown");      lua_setfield(co, -2, "KeyCode");
        lua_pushstring(co, "MouseButton1"); lua_setfield(co, -2, "UserInputType");
        lua_pushboolean(co, 0);
        return 2;
    });
    if (partId) fire(SignalKind::Clicked, partId, nullptr);
}

void ScriptEngine::fireDied(uint64_t rootId) { fire(SignalKind::Died, rootId, nullptr); }

void ScriptEngine::fireMoveToFinished(uint64_t rootId, bool reached) {
    fire(SignalKind::MoveToFinished, rootId, [reached](lua_State* co) { lua_pushboolean(co, reached); return 1; });
}

void ScriptEngine::fireAttributeChanged(uint64_t id, const std::string& name) {
    fire(SignalKind::AttributeChanged, id, [name](lua_State* co) { lua_pushstring(co, name.c_str()); return 1; });
}

void ScriptEngine::fireTag(bool added, uint64_t id, const std::string& tag) {
    fire(added ? SignalKind::TagAdded : SignalKind::TagRemoved, 0, [id, tag](lua_State* co) {
        LuaApi::pushInstance(co, id);
        lua_pushstring(co, tag.c_str());
        return 2;
    });
}

// ---------------------------------------------------------------------------
// Players (for leaderstats): each one has an object outside the world.
// ---------------------------------------------------------------------------

std::filesystem::path ScriptEngine::saveFile() const {
    // One file per game: its published ID if it has one, else its title.
    const GameInfo& info = m_scene->info();
    std::string key = !info.publishedId.empty() ? info.publishedId : info.title.empty() ? "Untitled" : info.title;
    for (char& c : key)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
    return Account::folder() / "savedata" / (key + ".json");
}

const nlohmann::json& ScriptEngine::saveData() {
    if (!m_saveLoaded) {
        m_saveLoaded = true;
        std::ifstream f(saveFile());
        std::stringstream ss;
        ss << f.rdbuf();
        m_saveData = nlohmann::json::parse(ss.str(), nullptr, false);
        if (!m_saveData.is_object()) m_saveData = nlohmann::json::object();
    }
    return m_saveData;
}

void ScriptEngine::setSaveData(const std::string& store, const std::string& key, const nlohmann::json& value) {
    saveData();
    if (value.is_null()) { if (m_saveData.contains(store)) m_saveData[store].erase(key); }
    else m_saveData[store][key] = value;
    std::error_code ec;
    std::filesystem::create_directories(saveFile().parent_path(), ec);
    std::string tmp = saveFile().string() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << m_saveData.dump(1); }
    std::filesystem::rename(tmp, saveFile(), ec);
}

// --- DataStores on the Guts&Bolts server ------------------------------------

namespace {
constexpr double kDataFresh = 5.0;   // like Roblox: GetAsync can answer from what it saw in the last few seconds
}

void ScriptEngine::dataDecide() {
    if (m_dataMode != DataMode::Unknown) return;
    std::string game = m_dataGame.empty() ? m_scene->info().publishedId : m_dataGame;
    if (!m_onlineData || game.empty() || game.rfind("local:", 0) == 0 || !Online::online()) { m_dataMode = DataMode::Local; return; }
    m_dataGame = game;
    m_dataMode = DataMode::Asking;
    std::weak_ptr<bool> alive = m_alive;
    const int gen = m_dataGen;
    Online::request("data.can", {{"game", game}}, [this, alive, gen](const nlohmann::json& r) {
        if (alive.expired() || gen != m_dataGen) return;
        m_dataMode = r.value("ok", false) && r.value("save", false) ? DataMode::Online : DataMode::Local;
        if (m_dataMode == DataMode::Local) Log::info("DataStores: saving on this computer (only the game's own servers save online).");
        std::vector<DataJob> jobs = std::move(m_dataQueue);
        m_dataQueue.clear();
        for (const DataJob& j : jobs) dataRun(j);
    });
}

void ScriptEngine::dataRun(const DataJob& job) {
    const std::string ck = job.store + "\n" + job.key;
    if (m_dataMode == DataMode::Unknown || m_dataMode == DataMode::Asking) { m_dataQueue.push_back(job); return; }
    if (m_dataMode == DataMode::Local) {
        if (job.kind == "set") { setSaveData(job.store, job.key, job.value); return; }
        const nlohmann::json& all = saveData();
        nlohmann::json v = all.contains(job.store) && all[job.store].contains(job.key) ? all[job.store][job.key] : nlohmann::json();
        if (job.kind == "inc") {
            v = (v.is_number() ? v.get<double>() : 0.0) + job.delta;
            if (std::floor(v.get<double>()) == v.get<double>() && std::abs(v.get<double>()) < 9e15) v = (long long)v.get<double>();
            setSaveData(job.store, job.key, v);
        }
        m_dataDone[job.ticket] = v;
        return;
    }
    std::weak_ptr<bool> alive = m_alive;
    const int gen = m_dataGen, ticket = job.ticket;
    auto ours = [this, alive, gen] { return !alive.expired() && gen == m_dataGen; };
    nlohmann::json args = {{"game", m_dataGame}, {"store", job.store}, {"key", job.key}};
    if (job.kind == "set") {
        DataCached& c = m_dataCache[ck];
        c.value = job.value; c.at = nowSeconds(); ++c.writing;
        args["value"] = job.value;
        Online::request("data.set", args, [this, ours, ck](const nlohmann::json& r) {
            if (!ours()) return;
            if (auto it = m_dataCache.find(ck); it != m_dataCache.end() && it->second.writing > 0) --it->second.writing;
            if (!r.value("ok", false)) Log::error("DataStore SetAsync: " + r.value("error", std::string("couldn't save")));
        });
        return;
    }
    if (job.kind == "get") {
        if (auto it = m_dataCache.find(ck); it != m_dataCache.end() && (it->second.writing > 0 || nowSeconds() - it->second.at < kDataFresh)) {
            m_dataDone[ticket] = it->second.value;
            return;
        }
        Online::request("data.get", args, [this, ours, ck, ticket](const nlohmann::json& r) {
            if (!ours()) return;
            if (!r.value("ok", false)) Log::error("DataStore GetAsync: " + r.value("error", std::string("no answer")));
            nlohmann::json v = r.value("value", nlohmann::json());
            DataCached& c = m_dataCache[ck];
            if (c.writing == 0) { c.value = v; c.at = nowSeconds(); }
            else v = c.value;   // our own newer write wins
            m_dataDone[ticket] = v;
        });
        return;
    }
    // "inc": the server adds in one step (two servers adding at once both count).
    if (std::floor(job.delta) == job.delta && std::abs(job.delta) < 9e15) args["delta"] = (long long)job.delta;   // (whole numbers stay whole)
    else args["delta"] = job.delta;
    Online::request("data.increment", args, [this, ours, ck, ticket](const nlohmann::json& r) {
        if (!ours()) return;
        if (!r.value("ok", false)) Log::error("DataStore IncrementAsync: " + r.value("error", std::string("no answer")));
        nlohmann::json v = r.value("value", nlohmann::json());
        if (!v.is_null()) { DataCached& c = m_dataCache[ck]; c.value = v; c.at = nowSeconds(); }
        m_dataDone[ticket] = v;
    });
}

int ScriptEngine::libraryModule(const std::string& assetId, uint64_t& module, std::string& error) {
    auto it = m_libraryModules.find(assetId);
    if (it != m_libraryModules.end()) {
        if (it->second.state == 1 && !resolve(it->second.module)) {   // it was destroyed: get a fresh copy
            m_libraryModules.erase(it);
        } else {
            module = it->second.module;
            error = it->second.error;
            return it->second.state;
        }
    }
    if (!Online::online()) { error = "require(" + assetId + "): not connected to Guts&Bolts, so the Library can't be reached"; return 2; }
    m_libraryModules[assetId] = LibraryModule{};
    std::weak_ptr<bool> alive = m_alive;
    const int gen = m_dataGen;
    auto ours = [this, alive, gen] { return !alive.expired() && gen == m_dataGen; };
    auto fail = [this, assetId](const std::string& why) {
        LibraryModule& l = m_libraryModules[assetId];
        l.state = 2;
        l.error = "require(" + assetId + "): " + why;
    };
    // Only models (anyone's public ones, or your own): not games, plugins and the rest.
    Online::request("asset.info", {{"id", assetId}}, [this, ours, fail, assetId](const nlohmann::json& r) {
        if (!ours()) return;
        if (!r.value("ok", false)) { fail(r.value("error", std::string("couldn't look that up"))); return; }
        const nlohmann::json a = r.value("asset", nlohmann::json::object());
        if (a.value("kind", std::string()) != "model") { fail("that isn't a model from the Library"); return; }
        const std::string name = a.value("name", std::string("Model"));
        Online::download(assetId, [this, ours, fail, assetId, name](bool ok, const std::filesystem::path& f, const nlohmann::json& info) {
            if (!ours()) return;
            if (!ok) { fail(info.value("error", std::string("the download failed"))); return; }
            std::ifstream in(f, std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            nlohmann::json model = nlohmann::json::parse(ss.str(), nullptr, false);
            if (!model.is_object() || !model.contains("nodes") || !model["nodes"].is_array()) { fail("that model looks broken"); return; }
            // A fresh copy with Parent = nil (scripts can move it into the game if they like).
            auto root = std::make_unique<SceneNode>(name, NodeKind::Model);
            for (const nlohmann::json& nj : model["nodes"])
                if (auto n = Serializer::nodeFromString(nj.dump(), true)) root->addChild(std::move(n));
            SceneNode* main = nullptr;
            int modules = 0;
            SceneNode* only = nullptr;
            std::vector<SceneNode*> stack{root.get()};
            while (!stack.empty()) {
                SceneNode* n = stack.back(); stack.pop_back();
                if (n->isScript() && n->isModule) {
                    ++modules; only = n;
                    if (!main && n->name == "MainModule") main = n;
                }
                for (auto& c : n->children) stack.push_back(c.get());
            }
            if (!main && modules == 1) main = only;
            if (!main) { fail("that model has no ModuleScript named MainModule"); return; }
            LibraryModule& l = m_libraryModules[assetId];
            l.state = 1;
            l.module = main->id;
            adopt(std::move(root));
            Log::system("Loaded " + name + " (" + assetId + ") from the Library");
        }, true);
    });
    return 0;
}

int ScriptEngine::dataStart(const std::string& kind, const std::string& store, const std::string& key, double delta) {
    dataDecide();
    DataJob j;
    j.ticket = m_dataNext++;
    j.kind = kind; j.store = store; j.key = key; j.delta = delta;
    dataRun(j);
    return j.ticket;
}

bool ScriptEngine::dataDone(int ticket, nlohmann::json& value) {
    auto it = m_dataDone.find(ticket);
    if (it == m_dataDone.end()) return false;
    value = std::move(it->second);
    m_dataDone.erase(it);
    return true;
}

void ScriptEngine::dataSet(const std::string& store, const std::string& key, const nlohmann::json& value) {
    dataDecide();
    DataJob j;
    j.kind = "set"; j.store = store; j.key = key; j.value = value;
    dataRun(j);
}

uint64_t ScriptEngine::playerNode(const std::string& name) {
    if (!m_playersRoot || !resolve(m_playersRoot)) {
        auto root = std::make_unique<SceneNode>("Players", NodeKind::Model);
        m_playersRoot = adopt(std::move(root))->id;
    }
    SceneNode* root = resolve(m_playersRoot);
    if (SceneNode* p = root->findChild(name)) return p->id;
    return root->addChild(std::make_unique<SceneNode>(name, NodeKind::Model))->id;
}

std::vector<std::pair<std::string, std::string>> ScriptEngine::leaderstats(const std::string& name) {
    std::vector<std::pair<std::string, std::string>> out;
    SceneNode* root = m_playersRoot ? resolve(m_playersRoot) : nullptr;
    SceneNode* p = root ? root->findChild(name) : nullptr;
    SceneNode* ls = p ? p->findChild("leaderstats") : nullptr;
    if (!ls) return out;
    for (auto& c : ls->children)
        if (c->isValue() && out.size() < 4) out.push_back({c->name, c->valueText()});   // Roblox shows up to 4
    return out;
}

void ScriptEngine::addPlayer(const std::string& name, uint64_t rootId, int userId, uint64_t backpackId) {
    if (!m_L) return;
    lua_getglobal(m_L, "__gb_addPlayer");
    lua_pushstring(m_L, name.c_str());
    LuaApi::pushInstance(m_L, rootId);
    lua_pushinteger(m_L, userId);
    LuaApi::pushInstance(m_L, backpackId);
    if (lua_pcall(m_L, 4, 1, 0) != LUA_OK) { lua_pop(m_L, 1); return; }
    int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
    fire(SignalKind::PlayerAdded, 0, [ref](lua_State* co) { lua_rawgeti(co, LUA_REGISTRYINDEX, ref); return 1; });
    if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

void ScriptEngine::removePlayer(const std::string& name) {
    if (!m_L) return;
    lua_getglobal(m_L, "__gb_removePlayer");
    lua_pushstring(m_L, name.c_str());
    if (lua_pcall(m_L, 1, 1, 0) != LUA_OK || lua_isnil(m_L, -1)) { lua_pop(m_L, 1); return; }
    int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
    fire(SignalKind::PlayerRemoving, 0, [ref](lua_State* co) { lua_rawgeti(co, LUA_REGISTRYINDEX, ref); return 1; });
    if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

int ScriptEngine::connect(SignalKind kind, uint64_t id, int fnRef, bool once) {
    Connection c;
    c.kind = kind; c.id = id; c.fnRef = fnRef; c.once = once; c.owner = m_current;
    m_conns.push_back(c);
    return (int)m_conns.size() - 1;
}

int ScriptEngine::connectWaiter(SignalKind kind, uint64_t id, lua_State* thread) {
    Connection c;
    c.kind = kind; c.id = id; c.waiter = thread; c.threadRef = LUA_NOREF; c.owner = m_current;
    m_conns.push_back(c);
    return (int)m_conns.size() - 1;
}

void ScriptEngine::disconnect(int index) {
    if (index < 0 || index >= (int)m_conns.size()) return;
    Connection& c = m_conns[index];
    if (!c.alive) return;
    c.alive = false;
    if (m_L && c.fnRef != LUA_NOREF) luaL_unref(m_L, LUA_REGISTRYINDEX, c.fnRef);
    c.fnRef = LUA_NOREF;
}

bool ScriptEngine::connected(int index) const {
    return index >= 0 && index < (int)m_conns.size() && m_conns[index].alive;
}

bool ScriptEngine::isKeyDown(const std::string& key) const {
    if (ImGui::GetIO().WantTextInput) return false;
    for (const auto& k : keyTable())
        if (key == k.name) return ImGui::IsKeyDown(k.key);
    return false;
}

// ---------------------------------------------------------------------------
// Object ownership (for Parent = nil / Instance.new / Clone / Destroy)
// ---------------------------------------------------------------------------

namespace {
SceneNode* findIn(SceneNode* n, uint64_t id) {
    if (n->id == id) return n;
    for (auto& c : n->children)
        if (SceneNode* f = findIn(c.get(), id)) return f;
    return nullptr;
}
SceneNode* topOf(SceneNode* n) {
    while (n->parent) n = n->parent;
    return n;
}
} // namespace

SceneNode* ScriptEngine::resolve(uint64_t id) {
    if (!id) return nullptr;
    if (SceneNode* n = m_scene->findById(id)) return n;
    for (auto& d : m_detached)
        if (SceneNode* f = findIn(d.get(), id)) return f;
    return nullptr;
}

SceneNode* ScriptEngine::adopt(std::unique_ptr<SceneNode> n) {
    m_detached.push_back(std::move(n));
    return m_detached.back().get();
}

bool ScriptEngine::setParent(SceneNode* node, SceneNode* newParent, std::string& err) {
    if (m_scene->isProtected(node)) { err = "You can't change the Parent of " + node->name; return false; }
    if (newParent && (newParent == node || node->isAncestorOf(newParent))) {
        err = "Can't put an object inside itself";
        return false;
    }
    if (node->parent == newParent && newParent) return true;

    // Take ownership of the node, wherever it currently lives.
    std::unique_ptr<SceneNode> owned;
    bool inScene = topOf(node) == m_scene->root();
    if (inScene) {
        owned = m_scene->detach(node);
    } else if (node->parent) {
        owned = node->parent->detachChild(node);
    } else {
        for (auto it = m_detached.begin(); it != m_detached.end(); ++it)
            if (it->get() == node) { owned = std::move(*it); m_detached.erase(it); break; }
    }
    if (!owned) { err = "Couldn't move " + node->name; return false; }

    if (newParent) {
        SceneNode* moved = newParent->addChild(std::move(owned));
        m_scene->markDirty();
        // Like Roblox: scripts start when they arrive in the world (a clone parented in, say).
        if (!inScene && topOf(moved) == m_scene->root()) runScriptsIn(moved);
    } else {
        m_detached.push_back(std::move(owned));
    }
    return true;
}

bool ScriptEngine::destroy(SceneNode* node, std::string& err) {
    if (m_scene->isProtected(node)) { err = "You can't Destroy " + node->name; return false; }
    stopScripts(node);
    if (topOf(node) == m_scene->root()) {
        m_scene->removeNode(node);
    } else if (node->parent) {
        node->parent->removeChild(node);
    } else {
        m_detached.erase(std::remove_if(m_detached.begin(), m_detached.end(),
            [node](const auto& p) { return p.get() == node; }), m_detached.end());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Lighting.ClockTime — moves the sun and blends day / sunset / night colours.
// ---------------------------------------------------------------------------

float& ScriptEngine::clockTime() { return m_scene->environment().clockTime; }

void ScriptEngine::applyClockTime() {
    EnvironmentPresets::applyTimeOfDay(m_scene->environment(), clockTime());
}

// ---------------------------------------------------------------------------

bool ScriptEngine::checkSyntax(const std::string& source, std::string& error, int& line) {
    lua_State* L = luaL_newstate();
    bool ok = luaL_loadbuffer(L, source.data(), source.size(), "=script") == LUA_OK;
    line = 0;
    if (!ok) {
        error = lua_tostring(L, -1);
        // Messages look like "script:12: 'end' expected near <eof>".
        if (error.rfind("script:", 0) == 0) {
            line = std::atoi(error.c_str() + 7);
            size_t colon = error.find(':', 7);
            if (colon != std::string::npos) error = error.substr(colon + 2);
        }
    }
    lua_close(L);
    return ok;
}

// --- Game passes -------------------------------------------------------------------

// The game's ID on the server: the one it was started as (an online game), else the one
// saved in its file when it was published.
std::string ScriptEngine::onlineGameId() const {
    return !m_dataGame.empty() ? m_dataGame : m_scene->info().publishedId;
}

void ScriptEngine::lookUpPasses(int userId) {
    if (userId <= 0 || !m_passAsked.insert(userId).second) return;
    const std::string game = onlineGameId();
    // In scripts players are numbered 1 (us), 2, 3...: their accounts are what the server knows.
    std::string account = userId == 1 ? Account::id() : "";
    if (auto it = m_playerAccounts.find(userId); it != m_playerAccounts.end()) account = it->second;
    if (game.empty() || account.empty() || !Online::online()) { m_passReady.insert(userId); return; }   // nothing to look up
    std::weak_ptr<bool> alive = m_alive;
    Online::request("pass.owned", {{"game", game}, {"user", account}}, [this, alive, userId](const nlohmann::json& r) {
        if (alive.expired()) return;
        std::set<std::string>& mine = m_passOwned[userId];
        for (const auto& p : r.value("passes", nlohmann::json::array())) {
            mine.insert(p.value("id", std::string()));
            if (p.value("num", 0LL) > 0) mine.insert(std::to_string(p.value("num", 0LL)));
        }
        m_passReady.insert(userId);
    });
}

bool ScriptEngine::ownsPass(int userId, const std::string& pass) const {
    auto it = m_passOwned.find(userId);
    return it != m_passOwned.end() && it->second.count(pass) > 0;
}

void ScriptEngine::passPromptDone(int userId, const std::string& pass, bool bought) {
    const bool product = pass.rfind("product:", 0) == 0;
    if (bought && !product) m_passOwned[userId].insert(pass);
    m_passResults.push_back({userId, pass, bought});
    if (bought && product) checkReceipts(userId);   // the receipt is waiting on the server
}

// --- Developer products ------------------------------------------------------------

void ScriptEngine::checkReceipts(int userId) {
    const std::string game = onlineGameId();
    std::string account = userId == 1 ? Account::id() : "";
    if (auto it = m_playerAccounts.find(userId); it != m_playerAccounts.end()) account = it->second;
    if (game.empty() || account.empty() || !Online::online()) return;
    std::weak_ptr<bool> alive = m_alive;
    Online::request("product.pending", {{"game", game}, {"user", account}}, [this, alive, userId](const nlohmann::json& r) {
        if (alive.expired()) return;
        for (const auto& x : r.value("receipts", nlohmann::json::array())) {
            const std::string id = x.value("id", std::string());
            if (id.empty() || !m_receiptsBusy.insert(id).second) continue;   // (already being handed out)
            const long long num = x.value("num", 0LL);
            m_receipts.push_back({userId, id, num > 0 ? std::to_string(num) : x.value("product", std::string()), x.value("price", 0LL)});
        }
    });
}

void ScriptEngine::receiptDone(int userId, const std::string& receipt, bool granted) {
    m_receiptsBusy.erase(receipt);
    if (!granted) return;   // (it stays waiting: the game is asked again later)
    const std::string game = onlineGameId();
    std::string account = userId == 1 ? Account::id() : "";
    if (auto it = m_playerAccounts.find(userId); it != m_playerAccounts.end()) account = it->second;
    if (game.empty() || account.empty()) return;
    Online::request("product.grant", {{"game", game}, {"user", account}, {"receipt", receipt}});
}
