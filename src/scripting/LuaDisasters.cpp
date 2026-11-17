// The Disasters service for scripts: lightning, tornadoes, tsunamis and earthquakes a game
// starts where it wants, tuned how it wants (Disasters.h runs them).
//
//   local Disasters = game:GetService("Disasters")
//   Disasters:Lightning(player)                                   -- strikes them (100 damage)
//   Disasters:Lightning(Vector3.new(0, 0, 0), {Kill = true})
//   local t = Disasters:Tornado(Vector3.new(0, 0, 50), {Strength = 3, ExplodeOnImpact = true})
//   t.Thrown:Connect(function(character, player) print(player.Name .. " went flying") end)
//   t:Follow(player)  t:MoveTo(pos)  t.Velocity = Vector3.new(5, 0, 0)  t:Stop()
//   Disasters:Tsunami(Vector3.new(0, 0, -150), Vector3.new(0, 0, 1), {Height = 25, Speed = 35})
//   Disasters:Earthquake(7.5, {Duration = 25, Ragdoll = true})
//
// The C functions below do the work; the Lua part (kLua) makes the handles and events.
#include "LuaDisasters.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Log.h"
#include "../scene/Disasters.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace LuaDisasters {
namespace {

using Disasters::Disaster;

Scene* sceneOf(lua_State* L) { return LuaApi::engine(L)->scene(); }

// The character (its model) an object belongs to: the model itself, or a part of it.
uint64_t characterOf(lua_State* L, int idx) {
    const uint64_t id = LuaApi::toInstanceId(L, idx);
    Scene* scene = sceneOf(L);
    if (!id || !scene) return 0;
    for (SceneNode* n = scene->findById(id); n; n = n->parent)
        if (scene->humanoidOf(n->id)) return n->id;
    return 0;
}

glm::vec3 positionOf(lua_State* L, int idx, bool& ok) {
    ok = true;
    if (glm::vec3* v = LuaApi::toVector3(L, idx)) return *v;
    Scene* scene = sceneOf(L);
    if (const uint64_t id = LuaApi::toInstanceId(L, idx); id && scene)
        if (SceneNode* n = scene->findById(id)) {
            if (SceneNode* hrp = n->findChild("HumanoidRootPart")) return glm::vec3(hrp->worldMatrix()[3]);
            return glm::vec3(n->worldMatrix()[3]);
        }
    ok = false;
    return glm::vec3(0.0f);
}

struct NumField { const char* name; float Disaster::*field; float lo, hi; };
const NumField kNums[] = {
    {"Strength", &Disaster::strength, 0.0f, 9.5f},   {"Magnitude", &Disaster::strength, 1.0f, 9.5f},
    {"Radius", &Disaster::radius, 0.0f, 2000.0f},    {"Size", &Disaster::radius, 0.0f, 2000.0f},
    {"Width", &Disaster::radius, 0.0f, 4000.0f},     // (a tsunami: its whole width; halved below)
    {"Height", &Disaster::height, 0.0f, 400.0f},     {"Speed", &Disaster::speed, 0.0f, 400.0f},
    {"Distance", &Disaster::distance, 0.0f, 20000.0f}, {"Duration", &Disaster::duration, 0.0f, 3600.0f},
    {"Damage", &Disaster::damage, 0.0f, 1e6f},       {"Shake", &Disaster::shake, 0.0f, 10.0f},
    {"RotationSpeed", &Disaster::spin, 0.0f, 10.0f}, {"PullForce", &Disaster::pull, 0.0f, 10.0f},
    {"LiftForce", &Disaster::lift, 0.0f, 10.0f},     {"ThrowForce", &Disaster::throwForce, 0.0f, 10.0f},
    {"ThrowAfter", &Disaster::throwAfter, 0.0f, 60.0f}, {"FollowSpeed", &Disaster::followSpeed, 0.0f, 200.0f},
};
struct BoolField { const char* name; bool Disaster::*field; };
const BoolField kBools[] = {
    {"Kill", &Disaster::kill}, {"Ragdoll", &Disaster::ragdoll}, {"ExplodeOnImpact", &Disaster::explode},
    {"LoseFooting", &Disaster::loseFooting}, {"Destroy", &Disaster::destroy},
};

// Set one setting from the value at `idx`. False if there's no such setting.
bool setField(lua_State* L, Disaster& d, const char* key, int idx) {
    for (const NumField& f : kNums)
        if (std::strcmp(f.name, key) == 0) {
            float v = (float)luaL_checknumber(L, idx);
            if (std::strcmp(key, "Width") == 0) v *= 0.5f;
            if (std::strcmp(key, "Strength") == 0 && d.kind != Disasters::Kind::Earthquake) v = std::min(v, 5.0f);
            d.*(f.field) = std::clamp(v, f.lo, f.hi);
            return true;
        }
    for (const BoolField& f : kBools)
        if (std::strcmp(f.name, key) == 0) { d.*(f.field) = lua_toboolean(L, idx) != 0; return true; }
    if (std::strcmp(key, "Position") == 0) {
        bool ok;
        d.pos = positionOf(L, idx, ok);
        if (!ok) luaL_error(L, "Position must be a Vector3 (or a part)");
        return true;
    }
    if (std::strcmp(key, "Velocity") == 0) { d.velocity = LuaApi::checkVector3(L, idx); d.velocity.y = 0.0f; return true; }
    if (std::strcmp(key, "Direction") == 0) {
        glm::vec3 v = LuaApi::checkVector3(L, idx);
        v.y = 0.0f;
        if (glm::length(v) > 1e-4f) d.dir = glm::normalize(v);
        return true;
    }
    if (std::strcmp(key, "Target") == 0 || std::strcmp(key, "Follow") == 0) {
        d.follow = lua_isnil(L, idx) ? 0 : characterOf(L, idx);
        if (!d.follow && !lua_isnil(L, idx)) {   // a spot or a part: go there instead
            bool ok;
            const glm::vec3 p = positionOf(L, idx, ok);
            if (ok) d.pos = p;
        }
        return true;
    }
    return false;
}

// __spawn(kind, options) -> id
int l_spawn(lua_State* L) {
    Scene* scene = sceneOf(L);
    Disasters::Kind kind;
    const char* name = luaL_checkstring(L, 1);
    if (!Disasters::kindFromName(name, kind))
        return luaL_error(L, "'%s' isn't a disaster (Lightning, Tornado, Tsunami or Earthquake)", name);
    Disaster d = Disasters::defaults(kind);
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                const char* key = lua_tostring(L, -2);
                if (!setField(L, d, key, lua_gettop(L)))
                    return luaL_error(L, "'%s' isn't a setting of a %s", key, Disasters::kKindNames[(int)kind]);
            }
            lua_pop(L, 1);
        }
    }
    if (kind == Disasters::Kind::Lightning && d.follow)
        if (SceneNode* n = scene->findById(d.follow)) {   // (where they are right now; the host checks again as it strikes)
            if (SceneNode* hrp = n->findChild("HumanoidRootPart")) d.pos = glm::vec3(hrp->worldMatrix()[3]) - glm::vec3(0.0f, 2.5f, 0.0f);
        }
    lua_pushinteger(L, (lua_Integer)Disasters::spawn(*scene, d));
    return 1;
}

// __get(id, key) -> value (nil once it's over)
int l_get(lua_State* L) {
    Scene* scene = sceneOf(L);
    const uint32_t id = (uint32_t)luaL_checkinteger(L, 1);
    const char* key = luaL_checkstring(L, 2);
    const Disaster* d = Disasters::find(*scene, id);
    if (std::strcmp(key, "Active") == 0) { lua_pushboolean(L, d && !d->stopping); return 1; }
    if (!d) { lua_pushnil(L); return 1; }
    for (const NumField& f : kNums)
        if (std::strcmp(f.name, key) == 0) {
            float v = d->*(f.field);
            if (std::strcmp(key, "Width") == 0) v *= 2.0f;
            lua_pushnumber(L, v);
            return 1;
        }
    for (const BoolField& f : kBools)
        if (std::strcmp(f.name, key) == 0) { lua_pushboolean(L, d->*(f.field)); return 1; }
    if (std::strcmp(key, "Position") == 0)  { LuaApi::pushVector3(L, d->pos); return 1; }
    if (std::strcmp(key, "Velocity") == 0)  { LuaApi::pushVector3(L, d->kind == Disasters::Kind::Tsunami ? d->dir * d->speed : d->velocity); return 1; }
    if (std::strcmp(key, "Direction") == 0) { LuaApi::pushVector3(L, d->dir); return 1; }
    if (std::strcmp(key, "Age") == 0)       { lua_pushnumber(L, d->age); return 1; }
    if (std::strcmp(key, "TimeLeft") == 0)  { lua_pushnumber(L, d->duration > 0.0f ? std::max(0.0f, d->duration - d->age) : -1.0f); return 1; }
    if (std::strcmp(key, "Kind") == 0)      { lua_pushstring(L, Disasters::kKindNames[(int)d->kind]); return 1; }
    if (std::strcmp(key, "Intensity") == 0) {   // how much of it there is right now (earthquakes: how hard the ground shakes)
        lua_pushnumber(L, d->kind == Disasters::Kind::Earthquake ? Disasters::quakeLevel(*d) : d->fade);
        return 1;
    }
    if (std::strcmp(key, "Target") == 0 || std::strcmp(key, "Follow") == 0) { LuaApi::pushInstance(L, d->follow); return 1; }
    lua_pushnil(L);
    return 1;
}

// __set(id, key, value)
int l_set(lua_State* L) {
    Scene* scene = sceneOf(L);
    Disaster* d = Disasters::find(*scene, (uint32_t)luaL_checkinteger(L, 1));
    const char* key = luaL_checkstring(L, 2);
    if (!d) return 0;   // (it's over: nothing to change)
    if (!setField(L, *d, key, 3)) return luaL_error(L, "'%s' can't be set on a disaster", key);
    scene->disasters().publish = 0.0f;
    return 0;
}

// __stop(id, now?)
int l_stop(lua_State* L) {
    Disasters::stop(*sceneOf(L), (uint32_t)luaL_checkinteger(L, 1), lua_toboolean(L, 2) != 0);
    return 0;
}
int l_stopAll(lua_State* L) { Disasters::stopAll(*sceneOf(L)); return 0; }

// __events() -> { {id, what, character, position}, ... }   (and forgets them)
int l_events(lua_State* L) {
    Scene* scene = sceneOf(L);
    std::vector<Disasters::Event> events;
    events.swap(scene->disasters().events);
    static const char* const kWhat[] = {"Hit", "Thrown", "Exploded", "Killed", "Ended"};
    lua_createtable(L, (int)events.size(), 0);
    int i = 0;
    for (const Disasters::Event& e : events) {
        lua_createtable(L, 4, 0);
        lua_pushinteger(L, e.disaster); lua_rawseti(L, -2, 1);
        lua_pushstring(L, kWhat[(int)e.what]); lua_rawseti(L, -2, 2);
        if (e.character && scene->findById(e.character)) LuaApi::pushInstance(L, e.character); else lua_pushboolean(L, 0);
        lua_rawseti(L, -2, 3);
        LuaApi::pushVector3(L, e.pos); lua_rawseti(L, -2, 4);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// __list() -> { {id, kind}, ... }
int l_list(lua_State* L) {
    Scene* scene = sceneOf(L);
    lua_newtable(L);
    int i = 0;
    for (const Disaster& d : scene->disasters().list) {
        if (d.stopping) continue;
        lua_createtable(L, 2, 0);
        lua_pushinteger(L, d.id); lua_rawseti(L, -2, 1);
        lua_pushstring(L, Disasters::kKindNames[(int)d.kind]); lua_rawseti(L, -2, 2);
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// __fromPart(part) -> id (0 = it isn't a disaster spawner)
int l_fromPart(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)Disasters::spawnFromPart(*sceneOf(L), LuaApi::checkNode(L, 1)->id));
    return 1;
}

// __shakeAt(position) -> how hard the ground shakes there from earthquakes (0..1)
int l_shakeAt(lua_State* L) {
    lua_pushnumber(L, Disasters::quakeAt(*sceneOf(L), LuaApi::checkVector3(L, 1)));
    return 1;
}

const char* kLua = R"LUA(
local spawnRaw, getRaw, setRaw, stopRaw, stopAllRaw, eventsRaw, listRaw, fromPartRaw, shakeAtRaw, server = ...

local function newSignal()
    local sig, list = {}, {}
    function sig:Connect(fn)
        local c = { Connected = true }
        function c:Disconnect() self.Connected = false end
        list[#list + 1] = { c, fn }
        return c
    end
    sig.connect = sig.Connect
    function sig:Once(fn)
        local c
        c = sig:Connect(function(...) c:Disconnect() fn(...) end)
        return c
    end
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

local Disasters = { ClassName = "Disasters", Name = "Disasters" }
Disasters.Hit, Disasters.Thrown, Disasters.Exploded, Disasters.Killed = newSignal(), newSignal(), newSignal(), newSignal()
Disasters.Started, Disasters.Ended = newSignal(), newSignal()
local handles = {}

local function onServer(what)
    if not server then error("Disasters:" .. what .. " only works in a server Script (the server decides what happens, everyone sees it)", 3) end
end
-- A player stands for their character.
local function isPlayer(t)
    if type(t) == "table" and rawget(t, "UserId") ~= nil then return true end   -- (players are tables here)
    return typeof(t) == "Instance" and type(t) ~= "table" and t:IsA("Player")
end
local function who(t)
    if isPlayer(t) then
        if not t.Character then error("that player has no character right now", 3) end
        return t.Character
    end
    return t
end

local function handleFor(id, kind)
    if handles[id] then return handles[id] end
    local h = { Id = id, Kind = kind }
    h.Hit, h.Thrown, h.Exploded, h.Killed, h.Ended = newSignal(), newSignal(), newSignal(), newSignal(), newSignal()
    function h:Stop(now) stopRaw(id, now) end
    h.Destroy = h.Stop
    function h:MoveTo(pos) setRaw(id, "Position", who(pos)) end
    function h:Follow(target) setRaw(id, "Follow", target and who(target) or nil) end
    function h:IsActive() return getRaw(id, "Active") end
    function h:Set(options) for k, v in pairs(options) do setRaw(id, k, who(v)) end end
    setmetatable(h, {
        __index = function(_, key) return getRaw(id, key) end,
        __newindex = function(_, key, value) setRaw(id, key, who(value)) end,
        __tostring = function() return kind .. " " .. id end,
    })
    handles[id] = h
    return h
end

local function start(kind, options)
    onServer(kind)
    local opts = {}
    for k, v in pairs(options or {}) do
        if type(k) ~= "string" then error("disaster settings are names, like {Strength = 3}", 3) end
        opts[k] = who(v)
    end
    local id = spawnRaw(kind, opts)
    local h = handleFor(id, kind)
    Disasters.Started:Fire(h)
    return h
end

-- Disasters:Lightning(target, options?): target = a player, a character, a part or a Vector3.
function Disasters:Lightning(target, options)
    local o = options or {}
    if target ~= nil then o = table.clone and table.clone(o) or o; o.Target = target end
    return start("Lightning", o)
end
function Disasters:Tornado(position, options)
    local o = table.clone and table.clone(options or {}) or (options or {})
    if position ~= nil then o.Position = position end
    return start("Tornado", o)
end
function Disasters:Tsunami(position, direction, options)
    local o = table.clone and table.clone(options or {}) or (options or {})
    if position ~= nil then o.Position = position end
    if direction ~= nil then o.Direction = direction end
    return start("Tsunami", o)
end
function Disasters:Earthquake(magnitude, options)
    local o = table.clone and table.clone(options or {}) or (options or {})
    if magnitude ~= nil then o.Magnitude = magnitude end
    return start("Earthquake", o)
end
function Disasters:Spawn(kind, options) return start(tostring(kind), options) end
-- A Studio disaster spawner part (Insert Object > Disasters): start it now, as its attributes say.
function Disasters:SpawnFromPart(part)
    onServer("SpawnFromPart")
    local id = fromPartRaw(part)
    if id == 0 then error(part:GetFullName() .. " isn't a disaster spawner (it needs a Disaster attribute)", 2) end
    local h = handleFor(id, getRaw(id, "Kind"))
    Disasters.Started:Fire(h)
    return h
end
function Disasters:GetActive()
    local out = {}
    for _, e in ipairs(listRaw()) do out[#out + 1] = handleFor(e[1], e[2]) end
    return out
end
function Disasters:StopAll() onServer("StopAll") stopAllRaw() end
function Disasters:GetShakeAt(position) return shakeAtRaw(position) end
function Disasters:IsA(c) return c == "Disasters" or c == "Instance" end

-- Hand out what happened to the scripts' events.
task.spawn(function()
    if not server then return end
    local Players = game:GetService("Players")
    RunService.Heartbeat:Connect(function()
        for _, e in ipairs(eventsRaw()) do
            local id, what, character, pos = e[1], e[2], e[3], e[4]
            local h = handles[id] or handleFor(id, getRaw(id, "Kind") or "Disaster")
            if what == "Ended" then
                h.Ended:Fire()
                Disasters.Ended:Fire(h)
                handles[id] = nil
            else
                local player = character and Players:GetPlayerFromCharacter(character) or nil
                h[what]:Fire(character or nil, player, pos)
                Disasters[what]:Fire(h, character or nil, player, pos)
            end
        end
    end)
end)

return Disasters
)LUA";

} // namespace

void install(lua_State* L, bool server) {
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=disasters") != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_spawn);
    lua_pushcfunction(L, l_get);
    lua_pushcfunction(L, l_set);
    lua_pushcfunction(L, l_stop);
    lua_pushcfunction(L, l_stopAll);
    lua_pushcfunction(L, l_events);
    lua_pushcfunction(L, l_list);
    lua_pushcfunction(L, l_fromPart);
    lua_pushcfunction(L, l_shakeAt);
    lua_pushboolean(L, server);
    if (lua_pcall(L, 10, 1, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    // game:GetService("Disasters") finds it through the prelude's __gb_moreServices.
    lua_getglobal(L, "__gb_moreServices");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -2);
        lua_setfield(L, -2, "Disasters");
    }
    lua_pop(L, 1);
    lua_setglobal(L, "Disasters");   // (also just `Disasters`, like workspace)
}

} // namespace LuaDisasters
