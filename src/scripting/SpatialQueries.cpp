// "What's in this space?" for scripts, like Roblox's spatial queries:
//
//   local params = OverlapParams.new()
//   params.FilterDescendantsInstances = { zone }        -- leave the zone itself out
//   params.FilterType = Enum.RaycastFilterType.Exclude  -- or Include: only look at these
//   params.MaxParts = 10                                -- 0 = no limit
//   for _, part in workspace:GetPartsInPart(zone, params) do ... end
//   workspace:GetPartBoundsInBox(CFrame.new(0, 5, 0), Vector3.new(10, 10, 10), params)
//   workspace:GetPartBoundsInRadius(Vector3.new(0, 5, 0), 8, params)
//
// GetPartsInPart uses the real shapes (a turned box is a turned box, a ball is round).
// The two Bounds ones are quicker and rougher, like Roblox's: each part counts as the
// box around it, so a turned part or a ball reaches a little further than it looks.
// CollisionGroup and RespectCanCollide work like RaycastParams' do.
#include "SpatialQueries.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Log.h"
#include "../scene/CollisionGroups.h"
#include "../scene/Physics.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

// What we're looking inside: a turned box, or a ball.
struct Space {
    bool      ball = false;
    OBB       box;              // (a ball: center is its middle)
    float     radius = 0.0f;
    bool      exact = true;     // false: each part is the box around it (the Bounds queries)
};

// The point of `b` nearest to `p`.
glm::vec3 nearestOn(const OBB& b, const glm::vec3& p) {
    const glm::vec3 d = p - b.center;
    glm::vec3 q = b.center;
    for (int i = 0; i < 3; ++i) q += b.axis[i] * glm::clamp(glm::dot(d, b.axis[i]), -b.half[i], b.half[i]);
    return q;
}

bool ballTouches(const glm::vec3& c, float r, const OBB& b) {
    const glm::vec3 q = nearestOn(b, c);
    return glm::dot(q - c, q - c) < r * r;
}

// Does the part `n` reach into the space?
bool touches(const Space& s, const SceneNode* n) {
    OBB part = Physics::worldOBB(n);
    const bool partIsBall = s.exact && n->primitiveType == PrimitiveType::Sphere;
    if (!s.exact) part = OBB::fromAABB(Physics::bounds(part));
    const float partRadius = std::min({part.half.x, part.half.y, part.half.z});
    if (s.ball) {
        if (partIsBall) return glm::length(part.center - s.box.center) < s.radius + partRadius;
        return ballTouches(s.box.center, s.radius, part);
    }
    if (partIsBall) return ballTouches(part.center, partRadius, s.box);
    glm::vec3 normal;
    float depth;
    return Physics::obbOverlap(s.box, part, normal, depth);
}

// A table of objects at `idx` (or nil) -> the nodes in it.
std::vector<const SceneNode*> nodeList(lua_State* L, int idx) {
    std::vector<const SceneNode*> list;
    if (!lua_istable(L, idx)) return list;
    for (lua_Integer i = 1;; ++i) {
        lua_rawgeti(L, idx, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
        if (lua_isuserdata(L, -1)) list.push_back(LuaApi::checkNode(L, -1));
        lua_pop(L, 1);
    }
    return list;
}

// __gb_overlap(shape, a, b, filterList, include, group, respectCanCollide, maxParts)
//   shape "part":   a = the part (its own shape; the part itself is left out)
//   shape "box":    a = CFrame, b = Vector3 size   (parts' bounds)
//   shape "radius": a = Vector3 middle, b = radius (parts' bounds)
// -> a list of parts.
int l_overlap(lua_State* L) {
    Scene* scene = LuaApi::engine(L)->scene();
    const std::string shape = luaL_checkstring(L, 1);
    Space s;
    const SceneNode* self = nullptr;
    if (shape == "part") {
        self = LuaApi::checkNode(L, 2);
        if (!self->isPart()) luaL_error(L, "GetPartsInPart needs a part (got %s)", self->name.c_str());
        s.box = Physics::worldOBB(self);
        if (self->primitiveType == PrimitiveType::Sphere) {
            s.ball = true;
            s.radius = std::min({s.box.half.x, s.box.half.y, s.box.half.z});
        }
    } else if (shape == "box") {
        const glm::mat4 cf = LuaApi::checkCFrame(L, 2);
        const glm::vec3 size = LuaApi::checkVector3(L, 3);
        s.box.center = glm::vec3(cf[3]);
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 col(cf[i]);
            const float len = glm::length(col);
            s.box.axis[i] = len > 1e-8f ? col / len : glm::vec3(i == 0, i == 1, i == 2);
            s.box.half[i] = std::abs(size[i]) * 0.5f;
        }
        s.exact = false;
    } else if (shape == "radius") {
        s.ball = true;
        s.box.center = LuaApi::checkVector3(L, 2);
        s.radius = std::max(0.0f, (float)luaL_checknumber(L, 3));
        s.exact = false;
    } else {
        return luaL_error(L, "unknown overlap shape '%s'", shape.c_str());
    }
    lua_newtable(L);
    if (!scene) return 1;

    const std::vector<const SceneNode*> list = nodeList(L, 4);
    const bool include = lua_toboolean(L, 5);
    const std::string group = lua_isstring(L, 6) ? lua_tostring(L, 6) : CollisionGroups::kDefault;
    const bool respectCanCollide = lua_toboolean(L, 7);
    const lua_Integer maxParts = lua_isnumber(L, 8) ? std::max<lua_Integer>(0, lua_tointeger(L, 8)) : 0;
    const CollisionGroups& groups = scene->world().collisionGroups;
    const bool useGroups = !groups.onlyDefault() || !groups.collides(0, 0);
    auto listed = [&](const SceneNode* n) {   // n is one of the list, or inside one
        for (const SceneNode* p = n; p; p = p->parent)
            if (std::find(list.begin(), list.end(), p) != list.end()) return true;
        return false;
    };

    // Everything in the world that can be seen (not ServerStorage's hidden folders,
    // not the parts that only make up a ViewportFrame's picture).
    lua_Integer found = 0;
    std::vector<const SceneNode*> stack{scene->root()};
    while (!stack.empty()) {
        const SceneNode* n = stack.back();
        stack.pop_back();
        if (!n->visible || n->isViewportFrame()) continue;
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->internal || n == self) continue;
        if (respectCanCollide && !n->canCollide) continue;
        if (useGroups && !groups.collides(group, n->collisionGroup)) continue;
        if (include != listed(n)) continue;   // (Exclude with nothing listed: everything)
        if (!touches(s, n)) continue;
        LuaApi::pushInstance(L, n->id);
        lua_rawseti(L, -2, ++found);
        if (maxParts > 0 && found >= maxParts) break;
    }
    return 1;
}

// The Lua side: OverlapParams and the three methods. Gets (overlap, methods).
const char* kLua = R"LUA(
local overlap, methods = ...

OverlapParams = {}
function OverlapParams.new()
    return { ClassName = "OverlapParams", FilterDescendantsInstances = {}, FilterType = "Exclude",
             MaxParts = 0, CollisionGroup = "Default", RespectCanCollide = false, BruteForceAllSlow = false }
end

local function run(what, shape, a, b, params)
    params = params or OverlapParams.new()
    if type(params) ~= "table" then error(what .. ": the last value should be OverlapParams.new()", 3) end
    local group = params.CollisionGroup
    if group ~= nil and type(group) ~= "string" then error("OverlapParams.CollisionGroup must be a string", 3) end
    local ft = tostring(params.FilterType)
    local include = ft == "Include" or ft == "Whitelist" or ft == "Enum.RaycastFilterType.Include"
    return overlap(shape, a, b, params.FilterDescendantsInstances, include, group or "Default",
                   params.RespectCanCollide == true, tonumber(params.MaxParts) or 0)
end

-- workspace:GetPartsInPart(part [, params]): every part that really overlaps this one.
methods.GetPartsInPart = function(_, part, params)
    return run("GetPartsInPart", "part", part, nil, params)
end
-- workspace:GetPartBoundsInBox(cframe, size [, params]): parts whose box reaches into this box.
methods.GetPartBoundsInBox = function(_, cframe, size, params)
    return run("GetPartBoundsInBox", "box", cframe, size, params)
end
-- workspace:GetPartBoundsInRadius(position, radius [, params]): parts whose box reaches into this ball.
methods.GetPartBoundsInRadius = function(_, position, radius, params)
    if type(radius) ~= "number" then error("GetPartBoundsInRadius needs a radius", 2) end
    return run("GetPartBoundsInRadius", "radius", position, radius, params)
end
)LUA";

} // namespace

namespace SpatialQueries {

void install(lua_State* L) {
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=spatial") != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_overlap);
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

} // namespace SpatialQueries
