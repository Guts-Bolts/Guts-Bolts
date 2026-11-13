// Lua bindings for the realism features (Ballistics.h): workspace:FireBullet's engine side.
// The prelude (ScriptEngine.cpp) wraps __gb_fireBullet and hands out the harm with
// Humanoid:TakeDamage, so a bullet wound works exactly like any other.
#include "LuaRealism.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Audio.h"
#include "../core/Log.h"
#include "../scene/Ballistics.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include <algorithm>
#include <cstring>
#include <string>

namespace {

// __gb_fireBullet(origin, velocity, ignoreList, damage, range, pierce, ricochet, gravity?)
//   -> { Hits = {...}, EndPosition, Path = {...}, Time }
int l_fireBullet(lua_State* L) {
    Scene* scene = LuaApi::engine(L)->scene();
    if (!scene) return 0;
    Ballistics::Shot shot;
    shot.origin = LuaApi::checkVector3(L, 1);
    shot.velocity = LuaApi::checkVector3(L, 2);
    std::vector<const SceneNode*> ignore;
    if (lua_istable(L, 3)) {
        for (lua_Integer i = 1;; ++i) {
            lua_rawgeti(L, 3, i);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
            if (lua_isuserdata(L, -1)) ignore.push_back(LuaApi::checkNode(L, -1));
            lua_pop(L, 1);
        }
    }
    shot.damage = std::max(0.0f, (float)luaL_optnumber(L, 4, 25.0));
    shot.range = std::clamp((float)luaL_optnumber(L, 5, 1500.0), 1.0f, 20000.0f);
    shot.pierce = lua_isnoneornil(L, 6) ? true : lua_toboolean(L, 6);
    shot.ricochet = lua_isnoneornil(L, 7) ? true : lua_toboolean(L, 7);
    const float g = lua_isnumber(L, 8) ? (float)lua_tonumber(L, 8) : scene->world().gravity;
    shot.gravity = glm::vec3(0.0f, -std::max(0.0f, g), 0.0f);
    shot.wind = scene->world().globalWind;
    static uint32_t s_seed = 0x9e3779b9u;
    s_seed = s_seed * 1664525u + 1013904223u;
    shot.seed = s_seed | 1u;
    const glm::vec3 ears = Audio::listenerPosition();
    const Ballistics::Result r = Ballistics::fire(*scene, shot, ignore, &ears);

    lua_newtable(L);
    lua_newtable(L);
    int n = 0;
    for (const Ballistics::Hit& h : r.hits) {
        lua_newtable(L);
        LuaApi::pushInstance(L, h.part);                      lua_setfield(L, -2, "Instance");
        LuaApi::pushVector3(L, h.point);                      lua_setfield(L, -2, "Position");
        LuaApi::pushVector3(L, h.normal);                     lua_setfield(L, -2, "Normal");
        LuaApi::pushVector3(L, h.velocity);                   lua_setfield(L, -2, "Velocity");
        lua_pushstring(L, h.terrain ? "Terrain" : kMaterialNames[(int)h.material]); lua_setfield(L, -2, "Material");
        lua_pushboolean(L, h.what == Ballistics::Hit::Pierced);  lua_setfield(L, -2, "Pierced");
        lua_pushboolean(L, h.what == Ballistics::Hit::Ricochet); lua_setfield(L, -2, "Ricocheted");
        lua_pushnumber(L, h.flesh ? h.damage : 0.0f);         lua_setfield(L, -2, "Damage");
        lua_pushboolean(L, h.flesh);                          lua_setfield(L, -2, "Flesh");
        lua_rawseti(L, -2, ++n);
    }
    lua_setfield(L, -2, "Hits");
    LuaApi::pushVector3(L, r.end);
    lua_setfield(L, -2, "EndPosition");
    lua_newtable(L);
    for (size_t i = 0; i < r.path.size(); ++i) { LuaApi::pushVector3(L, r.path[i]); lua_rawseti(L, -2, (int)i + 1); }
    lua_setfield(L, -2, "Path");
    lua_pushnumber(L, r.time);
    lua_setfield(L, -2, "Time");
    return 1;
}

const char* kLua = R"LUA(
local fireBullet, methods = ...

-- workspace:FireBullet(origin, velocity [, params]) -> result
--   A real bullet: it drops, drifts in the wind (workspace.GlobalWind), goes through
--   thin wood, glass and bodies, stops in metal and concrete, and glances off hard
--   things. Characters it hits take the harm (Humanoid:TakeDamage, so wounds and all).
--   params (all can be left out):
--     Damage = 25, Range = 1500, Ignore = { yourCharacter },
--     Pierce = true, Ricochet = true, Gravity = workspace.Gravity
--   result.Hits = { { Instance, Position, Normal, Material, Pierced, Ricocheted, Damage }, ... }
--   result.EndPosition, result.Path (the points it flew through), result.Time (seconds)
methods.FireBullet = function(_, origin, velocity, params)
    if typeof(origin) ~= "Vector3" or typeof(velocity) ~= "Vector3" then
        error("FireBullet needs an origin and a velocity (both Vector3s)", 2)
    end
    params = params or {}
    if type(params) ~= "table" then error("FireBullet: the last value should be a table of settings", 2) end
    local r = fireBullet(origin, velocity, params.Ignore or params.FilterDescendantsInstances or {},
                         tonumber(params.Damage) or 25, tonumber(params.Range) or 1500,
                         params.Pierce ~= false, params.Ricochet ~= false, tonumber(params.Gravity))
    for _, hit in ipairs(r.Hits) do
        if hit.Damage > 0 and hit.Instance then
            local model = hit.Instance:FindFirstAncestorOfClass("Model")
            local hum = model and model:FindFirstChildOfClass("Humanoid")
            if hum then hum:TakeDamage(hit.Damage, hit, hit.Velocity) end
        end
        hit.Flesh = nil
    end
    return r
end
)LUA";

} // namespace

namespace LuaRealism {
void install(lua_State* L) {
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=realism") != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_fireBullet);
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}
} // namespace LuaRealism
