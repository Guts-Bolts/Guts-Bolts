// Roblox-style value types for scripts: Vector3, Color3, CFrame, and for game
// UI: UDim2, UDim and Vector2.
#include "LuaApi.h"
#include "../scene/SceneNode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

constexpr const char* kVec   = "Vector3";
constexpr const char* kColor = "Color3";
constexpr const char* kCF    = "CFrame";
constexpr const char* kUDim2 = "UDim2";
constexpr const char* kUDim  = "UDim";
constexpr const char* kVec2  = "Vector2";

template <typename T>
T* newUd(lua_State* L, const char* meta, const T& value) {
    T* p = static_cast<T*>(lua_newuserdatauv(L, sizeof(T), 0));
    new (p) T(value);
    luaL_setmetatable(L, meta);
    return p;
}

int readOnly(lua_State* L) {
    const char* type = "This";
    if (luaL_getmetafield(L, 1, "__name") == LUA_TSTRING) type = lua_tostring(L, -1);
    return luaL_error(L, "%s values can't be changed - make a new one with %s.new(...)",
                      type, type);
}

// ===========================================================================
// Vector3
// ===========================================================================

int v3_new(lua_State* L) {
    LuaApi::pushVector3(L, {(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0),
                            (float)luaL_optnumber(L, 3, 0)});
    return 1;
}

int v3_dot(lua_State* L) {
    lua_pushnumber(L, glm::dot(LuaApi::checkVector3(L, 1), LuaApi::checkVector3(L, 2)));
    return 1;
}
int v3_cross(lua_State* L) {
    LuaApi::pushVector3(L, glm::cross(LuaApi::checkVector3(L, 1), LuaApi::checkVector3(L, 2)));
    return 1;
}
int v3_lerp(lua_State* L) {
    glm::vec3 a = LuaApi::checkVector3(L, 1), b = LuaApi::checkVector3(L, 2);
    LuaApi::pushVector3(L, a + (b - a) * (float)luaL_checknumber(L, 3));
    return 1;
}

int v3_index(lua_State* L) {
    glm::vec3 v = LuaApi::checkVector3(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "X") || !std::strcmp(k, "x")) { lua_pushnumber(L, v.x); return 1; }
    if (!std::strcmp(k, "Y") || !std::strcmp(k, "y")) { lua_pushnumber(L, v.y); return 1; }
    if (!std::strcmp(k, "Z") || !std::strcmp(k, "z")) { lua_pushnumber(L, v.z); return 1; }
    if (!std::strcmp(k, "Magnitude")) { lua_pushnumber(L, glm::length(v)); return 1; }
    if (!std::strcmp(k, "Unit")) {
        float len = glm::length(v);
        LuaApi::pushVector3(L, len > 1e-8f ? v / len : glm::vec3(0.0f));
        return 1;
    }
    if (!std::strcmp(k, "Dot"))   { lua_pushcfunction(L, v3_dot);   return 1; }
    if (!std::strcmp(k, "Cross")) { lua_pushcfunction(L, v3_cross); return 1; }
    if (!std::strcmp(k, "Lerp"))  { lua_pushcfunction(L, v3_lerp);  return 1; }
    return luaL_error(L, "'%s' is not a valid member of Vector3", k);
}

int v3_add(lua_State* L) { LuaApi::pushVector3(L, LuaApi::checkVector3(L, 1) + LuaApi::checkVector3(L, 2)); return 1; }
int v3_sub(lua_State* L) { LuaApi::pushVector3(L, LuaApi::checkVector3(L, 1) - LuaApi::checkVector3(L, 2)); return 1; }
int v3_unm(lua_State* L) { LuaApi::pushVector3(L, -LuaApi::checkVector3(L, 1)); return 1; }

int v3_mul(lua_State* L) {
    if (lua_isnumber(L, 1)) { LuaApi::pushVector3(L, (float)lua_tonumber(L, 1) * LuaApi::checkVector3(L, 2)); return 1; }
    if (lua_isnumber(L, 2)) { LuaApi::pushVector3(L, LuaApi::checkVector3(L, 1) * (float)lua_tonumber(L, 2)); return 1; }
    LuaApi::pushVector3(L, LuaApi::checkVector3(L, 1) * LuaApi::checkVector3(L, 2));
    return 1;
}
int v3_div(lua_State* L) {
    if (lua_isnumber(L, 2)) { LuaApi::pushVector3(L, LuaApi::checkVector3(L, 1) / (float)lua_tonumber(L, 2)); return 1; }
    LuaApi::pushVector3(L, LuaApi::checkVector3(L, 1) / LuaApi::checkVector3(L, 2));
    return 1;
}
int v3_eq(lua_State* L) {
    glm::vec3* a = LuaApi::toVector3(L, 1);
    glm::vec3* b = LuaApi::toVector3(L, 2);
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}
int v3_tostring(lua_State* L) {
    glm::vec3 v = LuaApi::checkVector3(L, 1);
    lua_pushfstring(L, "%f, %f, %f", (double)v.x, (double)v.y, (double)v.z);
    return 1;
}

// ===========================================================================
// Color3
// ===========================================================================

int c3_new(lua_State* L) {
    LuaApi::pushColor3(L, {(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0),
                           (float)luaL_optnumber(L, 3, 0)});
    return 1;
}
int c3_fromRGB(lua_State* L) {
    LuaApi::pushColor3(L, glm::vec3((float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0),
                                    (float)luaL_optnumber(L, 3, 0)) / 255.0f);
    return 1;
}
int c3_fromHSV(lua_State* L) {
    float h = (float)luaL_checknumber(L, 1), s = (float)luaL_checknumber(L, 2),
          v = (float)luaL_checknumber(L, 3);
    h = h - std::floor(h);
    float c = v * s, x = c * (1 - std::abs(std::fmod(h * 6.0f, 2.0f) - 1)), m = v - c;
    glm::vec3 rgb;
    int seg = (int)(h * 6.0f) % 6;
    switch (seg) {
        case 0:  rgb = {c, x, 0}; break;
        case 1:  rgb = {x, c, 0}; break;
        case 2:  rgb = {0, c, x}; break;
        case 3:  rgb = {0, x, c}; break;
        case 4:  rgb = {x, 0, c}; break;
        default: rgb = {c, 0, x}; break;
    }
    LuaApi::pushColor3(L, rgb + m);
    return 1;
}
int c3_fromHex(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    if (*s == '#') ++s;
    unsigned int r = 0, g = 0, b = 0;
    if (std::sscanf(s, "%02x%02x%02x", &r, &g, &b) != 3)
        return luaL_error(L, "Color3.fromHex: expected something like \"#ff8800\"");
    LuaApi::pushColor3(L, glm::vec3(r, g, b) / 255.0f);
    return 1;
}
int c3_lerp(lua_State* L) {
    glm::vec3 a = LuaApi::checkColor3(L, 1), b = LuaApi::checkColor3(L, 2);
    LuaApi::pushColor3(L, a + (b - a) * (float)luaL_checknumber(L, 3));
    return 1;
}
int c3_index(lua_State* L) {
    glm::vec3 c = LuaApi::checkColor3(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "R") || !std::strcmp(k, "r")) { lua_pushnumber(L, c.r); return 1; }
    if (!std::strcmp(k, "G") || !std::strcmp(k, "g")) { lua_pushnumber(L, c.g); return 1; }
    if (!std::strcmp(k, "B") || !std::strcmp(k, "b")) { lua_pushnumber(L, c.b); return 1; }
    if (!std::strcmp(k, "Lerp")) { lua_pushcfunction(L, c3_lerp); return 1; }
    return luaL_error(L, "'%s' is not a valid member of Color3", k);
}
int c3_eq(lua_State* L) {
    auto* a = static_cast<glm::vec3*>(luaL_testudata(L, 1, kColor));
    auto* b = static_cast<glm::vec3*>(luaL_testudata(L, 2, kColor));
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}
int c3_tostring(lua_State* L) {
    glm::vec3 c = LuaApi::checkColor3(L, 1);
    lua_pushfstring(L, "%f, %f, %f", (double)c.r, (double)c.g, (double)c.b);
    return 1;
}

// ===========================================================================
// CFrame — a position plus a rotation.
// ===========================================================================

glm::vec3 cfPos(const glm::mat4& m) { return glm::vec3(m[3]); }

// A CFrame at `pos` facing `at`. Straight up or down there's no "up" to keep, so it
// borrows -Z (glm::lookAt with a parallel up gives NaNs, e.g. a mouse.Hit from a top-down camera).
glm::mat4 lookAtFrame(const glm::vec3& pos, const glm::vec3& at) {
    const glm::vec3 f = at - pos;
    if (glm::length(f) <= 1e-6f) return glm::translate(glm::mat4(1.0f), pos);
    const glm::vec3 up = std::abs(glm::normalize(f).y) > 0.9999f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
    return glm::inverse(glm::lookAt(pos, at, up));
}
int cf_new(lua_State* L) {
    glm::mat4 m(1.0f);
    int n = lua_gettop(L);
    if (n >= 3 && lua_isnumber(L, 1)) {
        m = glm::translate(m, {(float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2), (float)lua_tonumber(L, 3)});
    } else if (n >= 1) {
        glm::vec3 pos = LuaApi::checkVector3(L, 1);
        if (n >= 2) {
            m = lookAtFrame(pos, LuaApi::checkVector3(L, 2));
        } else {
            m = glm::translate(m, pos);
        }
    }
    LuaApi::pushCFrame(L, m);
    return 1;
}
int cf_angles(lua_State* L) {
    float x = (float)luaL_optnumber(L, 1, 0), y = (float)luaL_optnumber(L, 2, 0),
          z = (float)luaL_optnumber(L, 3, 0);
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), x, {1, 0, 0});
    m = glm::rotate(m, y, {0, 1, 0});
    m = glm::rotate(m, z, {0, 0, 1});
    LuaApi::pushCFrame(L, m);
    return 1;
}
int cf_lookAt(lua_State* L) {
    glm::vec3 pos = LuaApi::checkVector3(L, 1), at = LuaApi::checkVector3(L, 2);
    LuaApi::pushCFrame(L, lookAtFrame(pos, at));
    return 1;
}
int cf_inverse(lua_State* L) { LuaApi::pushCFrame(L, glm::inverse(LuaApi::checkCFrame(L, 1))); return 1; }
int cf_lerp(lua_State* L) {
    glm::mat4 a = LuaApi::checkCFrame(L, 1), b = LuaApi::checkCFrame(L, 2);
    float t = (float)luaL_checknumber(L, 3);
    glm::quat qa = glm::quat_cast(glm::mat3(a)), qb = glm::quat_cast(glm::mat3(b));
    glm::mat4 m = glm::mat4_cast(glm::slerp(qa, qb, t));
    m[3] = glm::vec4(glm::mix(cfPos(a), cfPos(b), t), 1.0f);
    LuaApi::pushCFrame(L, m);
    return 1;
}
int cf_toEuler(lua_State* L) {
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    float x, y, z;
    glm::extractEulerAngleXYZ(m, x, y, z);
    lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, z);
    return 3;
}
int cf_toWorld(lua_State* L) {
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    LuaApi::pushVector3(L, glm::vec3(m * glm::vec4(LuaApi::checkVector3(L, 2), 1.0f)));
    return 1;
}
int cf_toObject(lua_State* L) {
    glm::mat4 m = glm::inverse(LuaApi::checkCFrame(L, 1));
    LuaApi::pushVector3(L, glm::vec3(m * glm::vec4(LuaApi::checkVector3(L, 2), 1.0f)));
    return 1;
}

int cf_index(lua_State* L) {
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "Position") || !std::strcmp(k, "p")) { LuaApi::pushVector3(L, cfPos(m)); return 1; }
    if (!std::strcmp(k, "X")) { lua_pushnumber(L, m[3].x); return 1; }
    if (!std::strcmp(k, "Y")) { lua_pushnumber(L, m[3].y); return 1; }
    if (!std::strcmp(k, "Z")) { lua_pushnumber(L, m[3].z); return 1; }
    if (!std::strcmp(k, "LookVector"))  { LuaApi::pushVector3(L, -glm::normalize(glm::vec3(m[2]))); return 1; }
    if (!std::strcmp(k, "RightVector")) { LuaApi::pushVector3(L,  glm::normalize(glm::vec3(m[0]))); return 1; }
    if (!std::strcmp(k, "UpVector"))    { LuaApi::pushVector3(L,  glm::normalize(glm::vec3(m[1]))); return 1; }
    if (!std::strcmp(k, "Rotation")) { glm::mat4 r = m; r[3] = {0, 0, 0, 1}; LuaApi::pushCFrame(L, r); return 1; }
    if (!std::strcmp(k, "Inverse"))  { lua_pushcfunction(L, cf_inverse);  return 1; }
    if (!std::strcmp(k, "Lerp"))     { lua_pushcfunction(L, cf_lerp);     return 1; }
    if (!std::strcmp(k, "ToEulerAnglesXYZ")) { lua_pushcfunction(L, cf_toEuler); return 1; }
    if (!std::strcmp(k, "PointToWorldSpace"))  { lua_pushcfunction(L, cf_toWorld);  return 1; }
    if (!std::strcmp(k, "PointToObjectSpace")) { lua_pushcfunction(L, cf_toObject); return 1; }
    return luaL_error(L, "'%s' is not a valid member of CFrame", k);
}

int cf_mul(lua_State* L) {
    glm::mat4 a = LuaApi::checkCFrame(L, 1);
    if (glm::mat4* b = LuaApi::toCFrame(L, 2)) { LuaApi::pushCFrame(L, a * *b); return 1; }
    LuaApi::pushVector3(L, glm::vec3(a * glm::vec4(LuaApi::checkVector3(L, 2), 1.0f)));
    return 1;
}
int cf_add(lua_State* L) {
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    m[3] += glm::vec4(LuaApi::checkVector3(L, 2), 0.0f);
    LuaApi::pushCFrame(L, m);
    return 1;
}
int cf_sub(lua_State* L) {
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    m[3] -= glm::vec4(LuaApi::checkVector3(L, 2), 0.0f);
    LuaApi::pushCFrame(L, m);
    return 1;
}
int cf_tostring(lua_State* L) {
    glm::vec3 p = cfPos(LuaApi::checkCFrame(L, 1));
    lua_pushfstring(L, "CFrame(%f, %f, %f)", (double)p.x, (double)p.y, (double)p.z);
    return 1;
}

// ===========================================================================
// UDim2 / UDim: a fraction of the parent plus pixels (game UI sizes and positions)
// ===========================================================================

UDim2 checkU2(lua_State* L, int idx) { return LuaApi::checkUDim2(L, idx); }
glm::vec2 checkU(lua_State* L, int idx) {
    if (auto* u = static_cast<glm::vec2*>(luaL_testudata(L, idx, kUDim))) return *u;
    luaL_typeerror(L, idx, "UDim");
    return {};
}
void pushU(lua_State* L, glm::vec2 v) { newUd(L, kUDim, v); }

int u2_new(lua_State* L) {
    if (lua_gettop(L) == 2 && luaL_testudata(L, 1, kUDim)) {   // UDim2.new(UDim, UDim)
        glm::vec2 x = checkU(L, 1), y = checkU(L, 2);
        LuaApi::pushUDim2(L, {x.x, x.y, y.x, y.y});
        return 1;
    }
    LuaApi::pushUDim2(L, {(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0),
                          (float)luaL_optnumber(L, 3, 0), (float)luaL_optnumber(L, 4, 0)});
    return 1;
}
int u2_fromScale(lua_State* L) {
    LuaApi::pushUDim2(L, {(float)luaL_optnumber(L, 1, 0), 0, (float)luaL_optnumber(L, 2, 0), 0});
    return 1;
}
int u2_fromOffset(lua_State* L) {
    LuaApi::pushUDim2(L, {0, (float)luaL_optnumber(L, 1, 0), 0, (float)luaL_optnumber(L, 2, 0)});
    return 1;
}
int u2_lerp(lua_State* L) {
    UDim2 a = checkU2(L, 1), b = checkU2(L, 2);
    float t = (float)luaL_checknumber(L, 3);
    LuaApi::pushUDim2(L, {a.xs + (b.xs - a.xs) * t, a.xo + (b.xo - a.xo) * t, a.ys + (b.ys - a.ys) * t, a.yo + (b.yo - a.yo) * t});
    return 1;
}
int u2_index(lua_State* L) {
    UDim2 u = checkU2(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "X") || !std::strcmp(k, "Width"))  { pushU(L, {u.xs, u.xo}); return 1; }
    if (!std::strcmp(k, "Y") || !std::strcmp(k, "Height")) { pushU(L, {u.ys, u.yo}); return 1; }
    if (!std::strcmp(k, "Lerp")) { lua_pushcfunction(L, u2_lerp); return 1; }
    return luaL_error(L, "'%s' is not a valid member of UDim2", k);
}
int u2_add(lua_State* L) {
    UDim2 a = checkU2(L, 1), b = checkU2(L, 2);
    LuaApi::pushUDim2(L, {a.xs + b.xs, a.xo + b.xo, a.ys + b.ys, a.yo + b.yo});
    return 1;
}
int u2_sub(lua_State* L) {
    UDim2 a = checkU2(L, 1), b = checkU2(L, 2);
    LuaApi::pushUDim2(L, {a.xs - b.xs, a.xo - b.xo, a.ys - b.ys, a.yo - b.yo});
    return 1;
}
int u2_eq(lua_State* L) {
    auto* a = static_cast<UDim2*>(luaL_testudata(L, 1, kUDim2));
    auto* b = static_cast<UDim2*>(luaL_testudata(L, 2, kUDim2));
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}
int u2_tostring(lua_State* L) {
    UDim2 u = checkU2(L, 1);
    lua_pushfstring(L, "{%f, %f}, {%f, %f}", (double)u.xs, (double)u.xo, (double)u.ys, (double)u.yo);
    return 1;
}
int u_new(lua_State* L) { pushU(L, {(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0)}); return 1; }
int u_index(lua_State* L) {
    glm::vec2 u = checkU(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "Scale"))  { lua_pushnumber(L, u.x); return 1; }
    if (!std::strcmp(k, "Offset")) { lua_pushnumber(L, u.y); return 1; }
    return luaL_error(L, "'%s' is not a valid member of UDim", k);
}
int u_add(lua_State* L) { pushU(L, checkU(L, 1) + checkU(L, 2)); return 1; }
int u_sub(lua_State* L) { pushU(L, checkU(L, 1) - checkU(L, 2)); return 1; }
int u_eq(lua_State* L) {
    auto* a = static_cast<glm::vec2*>(luaL_testudata(L, 1, kUDim));
    auto* b = static_cast<glm::vec2*>(luaL_testudata(L, 2, kUDim));
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}
int u_tostring(lua_State* L) {
    glm::vec2 u = checkU(L, 1);
    lua_pushfstring(L, "%f, %f", (double)u.x, (double)u.y);
    return 1;
}

// ===========================================================================
// Vector2
// ===========================================================================

int v2_new(lua_State* L) { LuaApi::pushVector2(L, {(float)luaL_optnumber(L, 1, 0), (float)luaL_optnumber(L, 2, 0)}); return 1; }
int v2_lerp(lua_State* L) {
    glm::vec2 a = LuaApi::checkVector2(L, 1), b = LuaApi::checkVector2(L, 2);
    LuaApi::pushVector2(L, a + (b - a) * (float)luaL_checknumber(L, 3));
    return 1;
}
int v2_dot(lua_State* L) { lua_pushnumber(L, glm::dot(LuaApi::checkVector2(L, 1), LuaApi::checkVector2(L, 2))); return 1; }
int v2_index(lua_State* L) {
    glm::vec2 v = LuaApi::checkVector2(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "X") || !std::strcmp(k, "x")) { lua_pushnumber(L, v.x); return 1; }
    if (!std::strcmp(k, "Y") || !std::strcmp(k, "y")) { lua_pushnumber(L, v.y); return 1; }
    if (!std::strcmp(k, "Magnitude")) { lua_pushnumber(L, glm::length(v)); return 1; }
    if (!std::strcmp(k, "Unit")) { LuaApi::pushVector2(L, glm::length(v) > 0 ? v / glm::length(v) : v); return 1; }
    if (!std::strcmp(k, "Lerp")) { lua_pushcfunction(L, v2_lerp); return 1; }
    if (!std::strcmp(k, "Dot"))  { lua_pushcfunction(L, v2_dot); return 1; }
    return luaL_error(L, "'%s' is not a valid member of Vector2", k);
}
glm::vec2 v2Arg(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TNUMBER) { float f = (float)lua_tonumber(L, idx); return {f, f}; }
    return LuaApi::checkVector2(L, idx);
}
int v2_add(lua_State* L) { LuaApi::pushVector2(L, v2Arg(L, 1) + v2Arg(L, 2)); return 1; }
int v2_sub(lua_State* L) { LuaApi::pushVector2(L, v2Arg(L, 1) - v2Arg(L, 2)); return 1; }
int v2_mul(lua_State* L) { LuaApi::pushVector2(L, v2Arg(L, 1) * v2Arg(L, 2)); return 1; }
int v2_div(lua_State* L) { LuaApi::pushVector2(L, v2Arg(L, 1) / v2Arg(L, 2)); return 1; }
int v2_unm(lua_State* L) { LuaApi::pushVector2(L, -LuaApi::checkVector2(L, 1)); return 1; }
int v2_eq(lua_State* L) {
    auto* a = LuaApi::toVector2(L, 1);
    auto* b = LuaApi::toVector2(L, 2);
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}
int v2_tostring(lua_State* L) {
    glm::vec2 v = LuaApi::checkVector2(L, 1);
    lua_pushfstring(L, "%f, %f", (double)v.x, (double)v.y);
    return 1;
}

void makeMeta(lua_State* L, const char* name, const luaL_Reg* fns) {
    luaL_newmetatable(L, name);
    luaL_setfuncs(L, fns, 0);
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);
}

} // namespace

namespace LuaApi {

void pushVector3(lua_State* L, const glm::vec3& v) { newUd(L, kVec, v); }
glm::vec3* toVector3(lua_State* L, int idx) { return static_cast<glm::vec3*>(luaL_testudata(L, idx, kVec)); }
glm::vec3* toColor3(lua_State* L, int idx) { return static_cast<glm::vec3*>(luaL_testudata(L, idx, kColor)); }
glm::vec3 checkVector3(lua_State* L, int idx) {
    if (auto* v = toVector3(L, idx)) return *v;
    luaL_typeerror(L, idx, "Vector3");
    return {};
}

void pushColor3(lua_State* L, const glm::vec3& c) { newUd(L, kColor, glm::clamp(c, 0.0f, 1.0f)); }
glm::vec3 checkColor3(lua_State* L, int idx) {
    if (auto* c = static_cast<glm::vec3*>(luaL_testudata(L, idx, kColor))) return *c;
    luaL_typeerror(L, idx, "Color3");
    return {};
}

void pushCFrame(lua_State* L, const glm::mat4& m) { newUd(L, kCF, m); }
glm::mat4* toCFrame(lua_State* L, int idx) { return static_cast<glm::mat4*>(luaL_testudata(L, idx, kCF)); }
glm::mat4 checkCFrame(lua_State* L, int idx) {
    if (auto* m = toCFrame(L, idx)) return *m;
    luaL_typeerror(L, idx, "CFrame");
    return glm::mat4(1.0f);
}

void pushUDim2(lua_State* L, const UDim2& u) { newUd(L, kUDim2, u); }
UDim2* toUDim2(lua_State* L, int idx) { return static_cast<UDim2*>(luaL_testudata(L, idx, kUDim2)); }
UDim2 checkUDim2(lua_State* L, int idx) {
    if (auto* u = toUDim2(L, idx)) return *u;
    luaL_typeerror(L, idx, "UDim2");
    return {};
}
void pushUDim(lua_State* L, float scale, float offset) { newUd(L, kUDim, glm::vec2(scale, offset)); }
glm::vec2 checkUDim(lua_State* L, int idx) {
    if (auto* u = static_cast<glm::vec2*>(luaL_testudata(L, idx, kUDim))) return *u;
    luaL_typeerror(L, idx, "UDim");
    return {};
}
void pushVector2(lua_State* L, const glm::vec2& v) { newUd(L, kVec2, v); }
glm::vec2* toVector2(lua_State* L, int idx) { return static_cast<glm::vec2*>(luaL_testudata(L, idx, kVec2)); }
glm::vec2 checkVector2(lua_State* L, int idx) {
    if (auto* v = toVector2(L, idx)) return *v;
    luaL_typeerror(L, idx, "Vector2");
    return {};
}

void registerTypes(lua_State* L) {
    static const luaL_Reg vecMeta[] = {
        {"__index", v3_index}, {"__newindex", readOnly}, {"__add", v3_add}, {"__sub", v3_sub},
        {"__mul", v3_mul}, {"__div", v3_div}, {"__unm", v3_unm}, {"__eq", v3_eq},
        {"__tostring", v3_tostring}, {nullptr, nullptr}};
    static const luaL_Reg colMeta[] = {
        {"__index", c3_index}, {"__newindex", readOnly}, {"__eq", c3_eq},
        {"__tostring", c3_tostring}, {nullptr, nullptr}};
    static const luaL_Reg cfMeta[] = {
        {"__index", cf_index}, {"__newindex", readOnly}, {"__mul", cf_mul}, {"__add", cf_add},
        {"__sub", cf_sub}, {"__tostring", cf_tostring}, {nullptr, nullptr}};
    makeMeta(L, kVec, vecMeta);
    makeMeta(L, kColor, colMeta);
    makeMeta(L, kCF, cfMeta);
    static const luaL_Reg u2Meta[] = {
        {"__index", u2_index}, {"__newindex", readOnly}, {"__add", u2_add}, {"__sub", u2_sub}, {"__eq", u2_eq},
        {"__tostring", u2_tostring}, {nullptr, nullptr}};
    static const luaL_Reg uMeta[] = {
        {"__index", u_index}, {"__newindex", readOnly}, {"__add", u_add}, {"__sub", u_sub}, {"__eq", u_eq},
        {"__tostring", u_tostring}, {nullptr, nullptr}};
    static const luaL_Reg v2Meta[] = {
        {"__index", v2_index}, {"__newindex", readOnly}, {"__add", v2_add}, {"__sub", v2_sub}, {"__mul", v2_mul},
        {"__div", v2_div}, {"__unm", v2_unm}, {"__eq", v2_eq}, {"__tostring", v2_tostring}, {nullptr, nullptr}};
    makeMeta(L, kUDim2, u2Meta);
    makeMeta(L, kUDim, uMeta);
    makeMeta(L, kVec2, v2Meta);

    lua_newtable(L);
    lua_pushcfunction(L, u2_new);        lua_setfield(L, -2, "new");
    lua_pushcfunction(L, u2_fromScale);  lua_setfield(L, -2, "fromScale");
    lua_pushcfunction(L, u2_fromOffset); lua_setfield(L, -2, "fromOffset");
    lua_setglobal(L, "UDim2");
    lua_newtable(L);
    lua_pushcfunction(L, u_new); lua_setfield(L, -2, "new");
    lua_setglobal(L, "UDim");
    lua_newtable(L);
    lua_pushcfunction(L, v2_new); lua_setfield(L, -2, "new");
    pushVector2(L, {0, 0}); lua_setfield(L, -2, "zero");
    pushVector2(L, {1, 1}); lua_setfield(L, -2, "one");
    lua_setglobal(L, "Vector2");

    // Vector3 = { new, zero, one, xAxis, yAxis, zAxis }
    lua_newtable(L);
    lua_pushcfunction(L, v3_new); lua_setfield(L, -2, "new");
    pushVector3(L, {0, 0, 0}); lua_setfield(L, -2, "zero");
    pushVector3(L, {1, 1, 1}); lua_setfield(L, -2, "one");
    pushVector3(L, {1, 0, 0}); lua_setfield(L, -2, "xAxis");
    pushVector3(L, {0, 1, 0}); lua_setfield(L, -2, "yAxis");
    pushVector3(L, {0, 0, 1}); lua_setfield(L, -2, "zAxis");
    lua_setglobal(L, "Vector3");

    lua_newtable(L);
    lua_pushcfunction(L, c3_new);     lua_setfield(L, -2, "new");
    lua_pushcfunction(L, c3_fromRGB); lua_setfield(L, -2, "fromRGB");
    lua_pushcfunction(L, c3_fromHSV); lua_setfield(L, -2, "fromHSV");
    lua_pushcfunction(L, c3_fromHex); lua_setfield(L, -2, "fromHex");
    lua_setglobal(L, "Color3");

    lua_newtable(L);
    lua_pushcfunction(L, cf_new);    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, cf_angles); lua_setfield(L, -2, "Angles");
    lua_pushcfunction(L, cf_angles); lua_setfield(L, -2, "fromEulerAnglesXYZ");
    lua_pushcfunction(L, cf_lookAt); lua_setfield(L, -2, "lookAt");
    lua_setglobal(L, "CFrame");
}

} // namespace LuaApi
