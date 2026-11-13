// Shapecasts: "if I slid this shape along here, what would it bump into first?"
//
//   local hit = workspace:Spherecast(gun.Position, 0.5, gun.CFrame.LookVector * 200, params)
//   local hit = workspace:Blockcast(car.CFrame, Vector3.new(6, 3, 10), Vector3.new(0, 0, -40), params)
//   local hit = workspace:Shapecast(crate, Vector3.new(0, -50, 0), params)
//   if hit then print(hit.Instance, hit.Position, hit.Normal, hit.Distance) end
//
// params is RaycastParams.new() (or ShapecastParams.new(), the same thing) with two extras:
//   params.Margin = 0.2         -- the shape is this much fatter all round, with round edges
//   params.SizeGradient = 0.05  -- it grows this much (each side) for every stud it moves;
//                               -- below 0 it shrinks (never past nothing)
// Shapecast also takes overrides as a 4th value, to cast a part's shape from somewhere else
// or at another size without moving it:
//   workspace:Shapecast(crate, dir, params, { CFrame = CFrame.new(0, 50, 0), Size = Vector3.new(2, 2, 2) })
//
// How it works: "conservative advancement". We measure the real gap between the shape and
// each part nearby, move the shape exactly that far (nothing can be closer), and repeat until
// the gap is tiny. So a thin wall is never jumped over, and a hit is accurate to 1/1000 stud.
// Like Roblox: parts the shape already overlaps at the start don't count. Parts are what the
// physics uses: balls are round, everything else is its box.
// With Studio's Physics View on, each cast is drawn for a few seconds (start, end, where it hit).
#include "Shapecasts.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Log.h"
#include "../scene/CollisionGroups.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Terrain.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace Shapecasts {
namespace {

constexpr float kTouch = 1e-3f;   // closer than this counts as touching

glm::vec3 nearestOn(const OBB& b, const glm::vec3& p) {
    const glm::vec3 d = p - b.center;
    glm::vec3 q = b.center;
    for (int i = 0; i < 3; ++i) q += b.axis[i] * glm::clamp(glm::dot(d, b.axis[i]), -b.half[i], b.half[i]);
    return q;
}

bool insideBox(const OBB& b, const glm::vec3& p) {
    const glm::vec3 d = p - b.center;
    for (int i = 0; i < 3; ++i)
        if (std::abs(glm::dot(d, b.axis[i])) > b.half[i]) return false;
    return true;
}

// A part as the physics sees it: a ball (Shape = Ball) or its box.
Shape partShape(const SceneNode* n) {
    Shape s;
    s.box = Physics::worldOBB(n);
    if (n->primitiveType == PrimitiveType::Sphere) {
        s.ball = true;
        s.radius = std::min({s.box.half.x, s.box.half.y, s.box.half.z});
    }
    return s;
}

AABB boundsOf(const Shape& s) {
    const float m = s.margin;
    if (s.ball) {
        const glm::vec3 r(s.radius + m);
        return AABB{s.box.center - r, s.box.center + r};
    }
    AABB b = Physics::bounds(s.box);
    b.min -= glm::vec3(m);
    b.max += glm::vec3(m);
    return b;
}

bool boxesMeet(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// The gap between two shapes (no margins), and the nearest points. Below 0: overlapping.
float rawGap(const Shape& a, const Shape& b, glm::vec3& pa, glm::vec3& pb) {
    if (a.ball && b.ball) {
        const glm::vec3 d = b.box.center - a.box.center;
        const float len = glm::length(d);
        const glm::vec3 u = len > 1e-6f ? d / len : glm::vec3(0, 1, 0);
        pa = a.box.center + u * a.radius;
        pb = b.box.center - u * b.radius;
        return len - a.radius - b.radius;
    }
    if (a.ball || b.ball) {   // a ball and a box
        const Shape& ball = a.ball ? a : b;
        const Shape& box = a.ball ? b : a;
        const glm::vec3 c = ball.box.center;
        const glm::vec3 q = nearestOn(box.box, c);
        float g;
        glm::vec3 onBall;
        if (insideBox(box.box, c)) {   // the ball's middle is inside the box
            g = -ball.radius;
            onBall = c;
        } else {
            const glm::vec3 d = q - c;
            const float len = glm::length(d);
            onBall = c + (len > 1e-6f ? d / len : glm::vec3(0, -1, 0)) * ball.radius;
            g = len - ball.radius;
        }
        (a.ball ? pa : pb) = onBall;
        (a.ball ? pb : pa) = q;
        return g;
    }
    // Two boxes. Overlapping: the physics' own test says how deep.
    glm::vec3 n;
    float depth;
    if (Physics::obbOverlap(a.box, b.box, n, depth)) {
        pa = a.box.center;
        pb = b.box.center;
        return -std::max(depth, kTouch);
    }
    // Apart: step back and forth between the nearest points of the two (this always
    // closes in on the real nearest pair for two solid convex shapes).
    glm::vec3 p = a.box.center, q = nearestOn(b.box, p);
    for (int i = 0; i < 64; ++i) {
        const glm::vec3 p2 = nearestOn(a.box, q);
        const glm::vec3 q2 = nearestOn(b.box, p2);
        const bool still = glm::dot(p2 - p, p2 - p) < 1e-12f && glm::dot(q2 - q, q2 - q) < 1e-12f;
        p = p2;
        q = q2;
        if (still) break;
    }
    pa = p;
    pb = q;
    return glm::length(q - p);
}

// The terrain: is any of the shape's bottom under the ground? (A height map, so the
// lowest points are what matter: the ball's bottom, or the box's corners.)
bool underGround(const Terrain& t, const Shape& s, glm::vec3& where) {
    std::vector<glm::vec3> pts;
    if (s.ball) {
        const float r = s.radius + s.margin;
        pts.push_back(s.box.center - glm::vec3(0, r, 0));
        for (int i = 0; i < 4; ++i) {   // around its lower side too, for slopes
            const float a = i * 1.5707963f;
            pts.push_back(s.box.center + glm::vec3(std::cos(a) * r * 0.7f, -r * 0.7f, std::sin(a) * r * 0.7f));
        }
    } else {
        for (int i = 0; i < 8; ++i)
            pts.push_back(s.box.center + s.box.axis[0] * ((i & 1) ? s.box.half.x : -s.box.half.x) +
                          s.box.axis[1] * ((i & 2) ? s.box.half.y : -s.box.half.y) +
                          s.box.axis[2] * ((i & 4) ? s.box.half.z : -s.box.half.z) - glm::vec3(0, s.margin, 0));
        pts.push_back(s.box.center);
    }
    for (const glm::vec3& p : pts) {
        float h;
        if (t.heightAt(p.x, p.z, h) && p.y <= h) { where = glm::vec3(p.x, h, p.z); return true; }
    }
    return false;
}

std::vector<Drawn>& drawList() {
    static std::vector<Drawn> list;
    return list;
}

void remember(const Shape& from, const Shape& to, const Hit* hit) {
    if (!Physics::debugOn()) return;
    auto& list = drawList();
    Drawn d;
    d.from = from;
    d.to = to;
    d.hit = hit != nullptr;
    if (hit) { d.position = hit->position; d.normal = hit->normal; }
    d.when = now();
    list.push_back(d);
    if (list.size() > 64) list.erase(list.begin(), list.begin() + (list.size() - 64));
}

} // namespace

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

const std::vector<Drawn>& recent() { return drawList(); }

Shape at(const Shape& s, const glm::vec3& dir, float t) {
    Shape o = s;
    o.box.center += dir * t;
    const float g = s.grow * t;
    o.box.half = glm::max(s.box.half + glm::vec3(g), glm::vec3(0.0f));
    o.radius = std::max(0.0f, s.radius + g);
    return o;
}

float gap(const Shape& s, const SceneNode* part, glm::vec3* onShape, glm::vec3* onPart) {
    glm::vec3 pa, pb;
    const float g = rawGap(s, partShape(part), pa, pb) - s.margin;
    if (onShape) *onShape = pa;
    if (onPart) *onPart = pb;
    return g;
}

bool cast(Scene& scene, const Shape& s, const glm::vec3& dir, float range,
          const std::function<bool(const SceneNode*)>& counts, Hit& hit) {
    if (!(range > 0.0f) || !std::isfinite(range)) { remember(s, s, nullptr); return false; }
    range = std::min(range, 1e5f);
    // How fast the gap can close: the move itself, plus the growing (a box's corner
    // moves out by up to sqrt(3) x the growth).
    const float rate = 1.0f + std::abs(s.grow) * (s.ball ? 1.0f : 1.7321f);
    const Shape end = at(s, dir, range);
    AABB swept = boundsOf(s);
    const AABB endBox = boundsOf(end);
    swept.min = glm::min(swept.min, endBox.min);
    swept.max = glm::max(swept.max, endBox.max);
    if (s.grow > 0.0f) {   // (it can be biggest somewhere in the middle only if it shrinks, so this covers it)
        swept.min -= glm::vec3(s.grow * range);
        swept.max += glm::vec3(s.grow * range);
    }

    float best = range + 1.0f;
    bool found = false;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (!n->shown() || !counts(n)) continue;
        if (n->isViewportFrame()) continue;   // (its parts are only a picture)
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->internal) continue;
        if (!boxesMeet(swept, Physics::worldBounds(n))) continue;
        float t = 0.0f;
        glm::vec3 pa, pb;
        float g = gap(s, n, &pa, &pb);
        if (g <= kTouch) continue;   // already overlapping at the start: left out
        for (int i = 0; i < 400 && t <= best && t <= range; ++i) {
            t += g / rate;
            if (t > range || t >= best) break;
            g = gap(at(s, dir, t), n, &pa, &pb);
            if (g <= kTouch) {
                best = t;
                found = true;
                hit.part = n;
                hit.distance = t;
                hit.position = pb;
                const glm::vec3 nrm = pa - pb;
                hit.normal = glm::dot(nrm, nrm) > 1e-10f ? glm::normalize(nrm) : -dir;
                break;
            }
        }
    }

    // The ground.
    const Terrain& terrain = scene.terrain();
    if (!terrain.empty() && counts(scene.terrainNode())) {
        glm::vec3 where;
        if (!underGround(terrain, s, where)) {
            const float limit = std::min(best, range);
            const float step = std::max(0.25f, limit / 4000.0f);
            float lo = 0.0f, hi = -1.0f;
            for (float t = step; ; t += step) {
                const float tt = std::min(t, limit);
                if (underGround(terrain, at(s, dir, tt), where)) { hi = tt; break; }
                lo = tt;
                if (tt >= limit) break;
            }
            if (hi > 0.0f) {
                for (int i = 0; i < 24; ++i) {   // narrow it down
                    const float mid = (lo + hi) * 0.5f;
                    if (underGround(terrain, at(s, dir, mid), where)) hi = mid; else lo = mid;
                }
                underGround(terrain, at(s, dir, hi), where);
                if (hi < best) {
                    best = hi;
                    found = true;
                    hit.part = scene.terrainNode();
                    hit.distance = hi;
                    hit.position = where;
                    hit.normal = terrain.normalAt(where.x, where.z);
                }
            }
        }
    }
    remember(s, at(s, dir, found ? hit.distance : range), found ? &hit : nullptr);
    return found;
}

namespace {

// __gb_shapecast(kind, a, b, direction, filterList, include, group, respectCanCollide, margin, grow, self)
//   kind "ball": a = Vector3 middle, b = radius
//   kind "box":  a = CFrame, b = Vector3 size
//   self: a part to leave out (Shapecast's own part), or nil
// -> part, position, normal, distance [, terrain material]   (nothing when it hits nothing)
int l_shapecast(lua_State* L) {
    Scene* scene = LuaApi::engine(L)->scene();
    const std::string kind = luaL_checkstring(L, 1);
    Shape s;
    if (kind == "ball") {
        s.ball = true;
        s.box.center = LuaApi::checkVector3(L, 2);
        s.radius = std::max(0.0f, (float)luaL_checknumber(L, 3));
    } else if (kind == "box") {
        const glm::mat4 cf = LuaApi::checkCFrame(L, 2);
        const glm::vec3 size = LuaApi::checkVector3(L, 3);
        s.box.center = glm::vec3(cf[3]);
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 col(cf[i]);
            const float len = glm::length(col);
            s.box.axis[i] = len > 1e-8f ? col / len : glm::vec3(i == 0, i == 1, i == 2);
            s.box.half[i] = std::abs(size[i]) * 0.5f;
        }
    } else {
        return luaL_error(L, "unknown shapecast shape '%s'", kind.c_str());
    }
    const glm::vec3 dir = LuaApi::checkVector3(L, 4);
    const float range = glm::length(dir);
    if (!scene || !(range > 1e-6f)) return 0;
    std::vector<const SceneNode*> list;
    if (lua_istable(L, 5)) {
        for (lua_Integer i = 1;; ++i) {
            lua_rawgeti(L, 5, i);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
            if (lua_isuserdata(L, -1)) list.push_back(LuaApi::checkNode(L, -1));
            lua_pop(L, 1);
        }
    }
    const bool include = lua_toboolean(L, 6);
    const std::string group = lua_isstring(L, 7) ? lua_tostring(L, 7) : CollisionGroups::kDefault;
    const bool respectCanCollide = lua_toboolean(L, 8);
    s.margin = std::max(0.0f, (float)luaL_optnumber(L, 9, 0.0));
    s.grow = (float)luaL_optnumber(L, 10, 0.0);
    if (!std::isfinite(s.margin) || !std::isfinite(s.grow)) return luaL_error(L, "Margin and SizeGradient must be numbers");
    const SceneNode* self = lua_isuserdata(L, 11) ? LuaApi::checkNode(L, 11) : nullptr;

    const CollisionGroups& groups = scene->world().collisionGroups;
    const bool useGroups = !groups.onlyDefault() || !groups.collides(0, 0);
    auto inside = [&](const SceneNode* n) {
        for (const SceneNode* p = n; p; p = p->parent)
            if (std::find(list.begin(), list.end(), p) != list.end()) return true;
        return false;
    };
    auto above = [&](const SceneNode* n) {
        for (const SceneNode* l : list)
            for (const SceneNode* p = l; p; p = p->parent) if (p == n) return true;
        return false;
    };
    Hit hit;
    const bool found = cast(*scene, s, dir / range, range, [&](const SceneNode* n) {
        if (n == scene->root()) return true;
        if (n == self) return false;
        if (n->id == Scene::kTerrainId) {
            if (useGroups && !groups.collides(group, CollisionGroups::kDefault)) return false;
        } else if (n->isPart()) {
            if (respectCanCollide && !n->canCollide) return false;
            if (useGroups && !groups.collides(group, n->collisionGroup)) return false;
        }
        return include ? (inside(n) || above(n)) : !inside(n);
    }, hit);
    if (!found) return 0;
    LuaApi::pushInstance(L, hit.part->id);
    LuaApi::pushVector3(L, hit.position);
    LuaApi::pushVector3(L, hit.normal);
    lua_pushnumber(L, hit.distance);
    if (hit.part->id == Scene::kTerrainId) {
        lua_pushstring(L, kTerrainMaterialNames[(int)scene->terrain().materialAt(hit.position.x, hit.position.z)]);
        return 5;
    }
    return 4;
}

// The Lua side. Gets (shapecast, methods).
const char* kLua = R"LUA(
local shapecast, methods = ...

-- ShapecastParams.new(): RaycastParams with the two shapecast extras filled in.
ShapecastParams = {}
function ShapecastParams.new()
    local p = RaycastParams.new()
    p.ClassName = "RaycastParams"
    p.Margin, p.SizeGradient = 0, 0
    return p
end

local function run(what, kind, a, b, direction, params, self)
    params = params or RaycastParams.new()
    if type(params) ~= "table" then error(what .. ": the params should be RaycastParams.new()", 3) end
    if typeof(direction) ~= "Vector3" then error(what .. ": the direction should be a Vector3", 3) end
    local group = params.CollisionGroup
    if group ~= nil and type(group) ~= "string" then error("RaycastParams.CollisionGroup must be a string", 3) end
    local margin, grow = params.Margin or 0, params.SizeGradient or 0
    if type(margin) ~= "number" or margin < 0 then error("RaycastParams.Margin must be a number, 0 or more", 3) end
    if type(grow) ~= "number" then error("RaycastParams.SizeGradient must be a number", 3) end
    local ft = tostring(params.FilterType)
    local include = ft == "Include" or ft == "Whitelist" or ft == "Enum.RaycastFilterType.Include"
    local part, pos, normal, dist, material = shapecast(kind, a, b, direction, params.FilterDescendantsInstances or {},
        include, group or "Default", params.RespectCanCollide == true, margin, grow, self)
    if not part then return nil end
    return { ClassName = "RaycastResult", Instance = part, Position = pos, Normal = normal, Distance = dist,
             Material = material or part.Material }
end

-- workspace:Spherecast(position, radius, direction [, params])
methods.Spherecast = function(_, position, radius, direction, params)
    if typeof(position) ~= "Vector3" then error("Spherecast: the position should be a Vector3", 2) end
    if type(radius) ~= "number" or radius < 0 then error("Spherecast needs a radius (a number, 0 or more)", 2) end
    return run("Spherecast", "ball", position, radius, direction, params)
end
-- workspace:Blockcast(cframe, size, direction [, params])
methods.Blockcast = function(_, cframe, size, direction, params)
    if typeof(cframe) ~= "CFrame" then error("Blockcast: the first value should be a CFrame", 2) end
    if typeof(size) ~= "Vector3" then error("Blockcast: the size should be a Vector3", 2) end
    return run("Blockcast", "box", cframe, size, direction, params)
end
-- workspace:Shapecast(part, direction [, params [, { CFrame = ..., Size = ... }]])
-- The part's own shape (a ball if it's a Ball), from where it is or from the overrides.
-- The part itself never counts as a hit.
methods.Shapecast = function(_, part, direction, params, overrides)
    if typeof(part) ~= "Instance" or not part:IsA("BasePart") then error("Shapecast needs a part", 2) end
    if overrides ~= nil and type(overrides) ~= "table" then error("Shapecast: overrides should be { CFrame = ..., Size = ... }", 2) end
    local cf, size = part.CFrame, part.Size
    if overrides then
        if overrides.CFrame ~= nil then
            if typeof(overrides.CFrame) ~= "CFrame" then error("Shapecast: overrides.CFrame should be a CFrame", 2) end
            cf = overrides.CFrame
        end
        if overrides.Size ~= nil then
            if typeof(overrides.Size) ~= "Vector3" then error("Shapecast: overrides.Size should be a Vector3", 2) end
            size = overrides.Size
        end
    end
    local shape = tostring(part.Shape)
    if shape == "Ball" or shape == "Enum.PartType.Ball" then
        return run("Shapecast", "ball", cf.Position, math.min(size.X, size.Y, size.Z) / 2, direction, params, part)
    end
    return run("Shapecast", "box", cf, size, direction, params, part)
end
)LUA";

} // namespace

void install(lua_State* L) {
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=shapecasts") != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_shapecast);
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_pcall(L, 2, 0, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

} // namespace Shapecasts
