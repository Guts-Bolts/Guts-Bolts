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

// vector:Abs() / Ceil() / Floor() / Sign(), :Max(...) / :Min(...), :FuzzyEq(other, epsilon),
// :Angle(other, axis): the angle between them in radians (signed around `axis` if given).
template <typename V> V perComponent(const V& v, float (*fn)(float)) {
    V out = v;
    for (int i = 0; i < V::length(); ++i) out[i] = fn(v[i]);
    return out;
}
float signOf(float x) { return x > 0.0f ? 1.0f : x < 0.0f ? -1.0f : 0.0f; }
float absOf(float x) { return std::fabs(x); }
float ceilOf(float x) { return std::ceil(x); }
float floorOf(float x) { return std::floor(x); }
int v3_abs(lua_State* L)   { LuaApi::pushVector3(L, perComponent(LuaApi::checkVector3(L, 1), absOf)); return 1; }
int v3_ceil(lua_State* L)  { LuaApi::pushVector3(L, perComponent(LuaApi::checkVector3(L, 1), ceilOf)); return 1; }
int v3_floor(lua_State* L) { LuaApi::pushVector3(L, perComponent(LuaApi::checkVector3(L, 1), floorOf)); return 1; }
int v3_sign(lua_State* L)  { LuaApi::pushVector3(L, perComponent(LuaApi::checkVector3(L, 1), signOf)); return 1; }
int v3_max(lua_State* L) {
    glm::vec3 v = LuaApi::checkVector3(L, 1);
    for (int i = 2, n = lua_gettop(L); i <= n; ++i) v = glm::max(v, LuaApi::checkVector3(L, i));
    LuaApi::pushVector3(L, v);
    return 1;
}
int v3_min(lua_State* L) {
    glm::vec3 v = LuaApi::checkVector3(L, 1);
    for (int i = 2, n = lua_gettop(L); i <= n; ++i) v = glm::min(v, LuaApi::checkVector3(L, i));
    LuaApi::pushVector3(L, v);
    return 1;
}
int v3_fuzzyEq(lua_State* L) {
    const glm::vec3 a = LuaApi::checkVector3(L, 1), b = LuaApi::checkVector3(L, 2);
    const float eps = (float)luaL_optnumber(L, 3, 1e-5);
    bool same = true;
    for (int i = 0; i < 3; ++i)
        if (std::fabs(a[i] - b[i]) > eps * std::max(1.0f, std::max(std::fabs(a[i]), std::fabs(b[i])))) same = false;
    lua_pushboolean(L, same);
    return 1;
}
int v3_angle(lua_State* L) {
    const glm::vec3 a = LuaApi::checkVector3(L, 1), b = LuaApi::checkVector3(L, 2);
    const glm::vec3 c = glm::cross(a, b);
    float ang = std::atan2(glm::length(c), glm::dot(a, b));
    if (glm::vec3* axis = LuaApi::toVector3(L, 3); axis && glm::dot(c, *axis) < 0.0f) ang = -ang;
    lua_pushnumber(L, ang);
    return 1;
}
int v3_idiv(lua_State* L) {
    const glm::vec3 a = LuaApi::checkVector3(L, 1);
    const glm::vec3 b = lua_isnumber(L, 2) ? glm::vec3((float)lua_tonumber(L, 2)) : LuaApi::checkVector3(L, 2);
    LuaApi::pushVector3(L, glm::floor(a / b));
    return 1;
}
// Vector3.FromNormalId(Enum.NormalId.Top) / Vector3.FromAxis(Enum.Axis.X)
std::string enumItemName(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TSTRING) return lua_tostring(L, idx);
    if (lua_type(L, idx) == LUA_TTABLE || lua_type(L, idx) == LUA_TUSERDATA) {
        lua_getfield(L, idx, "Name");
        std::string s = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        if (!s.empty()) return s;
    }
    std::string s = luaL_tolstring(L, idx, nullptr);
    lua_pop(L, 1);
    const size_t dot = s.rfind('.');
    return dot == std::string::npos ? s : s.substr(dot + 1);
}
int v3_fromNormalId(lua_State* L) {
    const std::string n = enumItemName(L, 1);
    static const std::pair<const char*, glm::vec3> faces[] = {
        {"Right", {1, 0, 0}}, {"Left", {-1, 0, 0}}, {"Top", {0, 1, 0}}, {"Bottom", {0, -1, 0}},
        {"Back", {0, 0, 1}}, {"Front", {0, 0, -1}}};
    for (const auto& [name, v] : faces) if (n == name) { LuaApi::pushVector3(L, v); return 1; }
    return luaL_error(L, "Vector3.FromNormalId: expected an Enum.NormalId (Top, Bottom, Left, Right, Front, Back)");
}
int v3_fromAxis(lua_State* L) {
    const std::string n = enumItemName(L, 1);
    if (n == "X") LuaApi::pushVector3(L, {1, 0, 0});
    else if (n == "Y") LuaApi::pushVector3(L, {0, 1, 0});
    else if (n == "Z") LuaApi::pushVector3(L, {0, 0, 1});
    else return luaL_error(L, "Vector3.FromAxis: expected an Enum.Axis (X, Y or Z)");
    return 1;
}

// Roblox still accepts the old lower-case names (v.magnitude, cf.lookVector, cf:toObjectSpace...),
// and lots of older scripts use them, so the first letter can be either case.
std::string upperFirst(const char* k) {
    std::string s = k;
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = char(s[0] - 'a' + 'A');
    return s;
}

int v3_index(lua_State* L) {
    glm::vec3 v = LuaApi::checkVector3(L, 1);
    const std::string key = upperFirst(luaL_checkstring(L, 2));
    const char* k = key.c_str();
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
    static const luaL_Reg more[] = {{"Abs", v3_abs}, {"Ceil", v3_ceil}, {"Floor", v3_floor}, {"Sign", v3_sign},
                                    {"Max", v3_max}, {"Min", v3_min}, {"FuzzyEq", v3_fuzzyEq}, {"Angle", v3_angle}};
    for (const luaL_Reg& r : more) if (!std::strcmp(k, r.name)) { lua_pushcfunction(L, r.func); return 1; }
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
// color:ToHSV() -> h, s, v (0 to 1), color:ToHex() -> "FF8800" (also Color3.toHSV(color)).
int c3_toHSV(lua_State* L) {
    const glm::vec3 c = LuaApi::checkColor3(L, 1);
    const float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b}), d = mx - mn;
    float h = 0.0f;
    if (d > 0.0f) {
        if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.0f);
        else if (mx == c.g) h = (c.b - c.r) / d + 2.0f;
        else h = (c.r - c.g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    lua_pushnumber(L, h);
    lua_pushnumber(L, mx > 0.0f ? d / mx : 0.0f);
    lua_pushnumber(L, mx);
    return 3;
}
int c3_toHex(lua_State* L) {
    const glm::vec3 c = glm::clamp(LuaApi::checkColor3(L, 1), 0.0f, 1.0f);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X%02X%02X", (int)std::lround(c.r * 255.0f), (int)std::lround(c.g * 255.0f),
                  (int)std::lround(c.b * 255.0f));
    lua_pushstring(L, buf);
    return 1;
}
int c3_index(lua_State* L) {
    glm::vec3 c = LuaApi::checkColor3(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "R") || !std::strcmp(k, "r")) { lua_pushnumber(L, c.r); return 1; }
    if (!std::strcmp(k, "G") || !std::strcmp(k, "g")) { lua_pushnumber(L, c.g); return 1; }
    if (!std::strcmp(k, "B") || !std::strcmp(k, "b")) { lua_pushnumber(L, c.b); return 1; }
    if (!std::strcmp(k, "Lerp")) { lua_pushcfunction(L, c3_lerp); return 1; }
    if (!std::strcmp(k, "ToHSV")) { lua_pushcfunction(L, c3_toHSV); return 1; }
    if (!std::strcmp(k, "ToHex")) { lua_pushcfunction(L, c3_toHex); return 1; }
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
    if (n >= 12 && lua_isnumber(L, 1)) {   // CFrame.new(x, y, z, R00, R01, R02, R10, R11, R12, R20, R21, R22)
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) m[c][r] = (float)luaL_checknumber(L, 4 + r * 3 + c);
        m[3] = glm::vec4((float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2), (float)lua_tonumber(L, 3), 1.0f);
    } else if (n >= 7 && lua_isnumber(L, 1)) {   // CFrame.new(x, y, z, qX, qY, qZ, qW)
        glm::quat q((float)luaL_checknumber(L, 7), (float)luaL_checknumber(L, 4), (float)luaL_checknumber(L, 5),
                    (float)luaL_checknumber(L, 6));
        if (glm::length(q) > 1e-8f) m = glm::mat4_cast(glm::normalize(q));
        m[3] = glm::vec4((float)lua_tonumber(L, 1), (float)lua_tonumber(L, 2), (float)lua_tonumber(L, 3), 1.0f);
    } else if (n >= 3 && lua_isnumber(L, 1)) {
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
int cf_fromOrientation(lua_State* L) {   // turned Y, then X, then Z (like Part.Orientation), in radians
    float x = (float)luaL_optnumber(L, 1, 0), y = (float)luaL_optnumber(L, 2, 0),
          z = (float)luaL_optnumber(L, 3, 0);
    glm::mat4 m = glm::rotate(glm::mat4(1.0f), y, {0, 1, 0});
    m = glm::rotate(m, x, {1, 0, 0});
    m = glm::rotate(m, z, {0, 0, 1});
    LuaApi::pushCFrame(L, m);
    return 1;
}
int cf_fromAxisAngle(lua_State* L) {
    glm::vec3 axis = LuaApi::checkVector3(L, 1);
    float a = (float)luaL_checknumber(L, 2);
    LuaApi::pushCFrame(L, glm::length(axis) > 1e-6f ? glm::rotate(glm::mat4(1.0f), a, glm::normalize(axis)) : glm::mat4(1.0f));
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

int cf_toObjectSpace(lua_State* L) {   // a:ToObjectSpace(b) = a:Inverse() * b
    LuaApi::pushCFrame(L, glm::inverse(LuaApi::checkCFrame(L, 1)) * LuaApi::checkCFrame(L, 2));
    return 1;
}
int cf_toWorldSpace(lua_State* L) {    // a:ToWorldSpace(b) = a * b
    LuaApi::pushCFrame(L, LuaApi::checkCFrame(L, 1) * LuaApi::checkCFrame(L, 2));
    return 1;
}
int cf_vecToWorld(lua_State* L) {
    LuaApi::pushVector3(L, glm::vec3(LuaApi::checkCFrame(L, 1) * glm::vec4(LuaApi::checkVector3(L, 2), 0.0f)));
    return 1;
}
int cf_vecToObject(lua_State* L) {
    LuaApi::pushVector3(L, glm::vec3(glm::inverse(LuaApi::checkCFrame(L, 1)) * glm::vec4(LuaApi::checkVector3(L, 2), 0.0f)));
    return 1;
}
int cf_toOrientation(lua_State* L) {   // the angles of Part.Orientation (turned Y, then X, then Z), in radians
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    float x, y, z;
    glm::extractEulerAngleYXZ(m, y, x, z);
    lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, z);
    return 3;
}
int cf_components(lua_State* L) {      // x, y, z, then the rotation matrix row by row
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    lua_pushnumber(L, m[3].x); lua_pushnumber(L, m[3].y); lua_pushnumber(L, m[3].z);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) lua_pushnumber(L, m[c][r]);
    return 12;
}

// Turns only (no position), with the columns made square again.
glm::mat4 orthonormal(const glm::mat4& m) {
    glm::vec3 x = glm::vec3(m[0]), y = glm::vec3(m[1]);
    if (glm::length(x) < 1e-8f) x = {1, 0, 0};
    x = glm::normalize(x);
    y = y - x * glm::dot(x, y);
    if (glm::length(y) < 1e-8f) y = std::fabs(x.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    y = glm::normalize(y - x * glm::dot(x, y));
    const glm::vec3 z = glm::cross(x, y);
    glm::mat4 out(1.0f);
    out[0] = glm::vec4(x, 0); out[1] = glm::vec4(y, 0); out[2] = glm::vec4(z, 0); out[3] = m[3];
    return out;
}
int cf_orthonormalize(lua_State* L) { LuaApi::pushCFrame(L, orthonormal(LuaApi::checkCFrame(L, 1))); return 1; }
int cf_toAxisAngle(lua_State* L) {
    const glm::quat q = glm::normalize(glm::quat_cast(glm::mat3(orthonormal(LuaApi::checkCFrame(L, 1)))));
    const float angle = 2.0f * std::acos(std::clamp(q.w, -1.0f, 1.0f));
    const float s = std::sqrt(std::max(0.0f, 1.0f - q.w * q.w));
    LuaApi::pushVector3(L, s < 1e-6f ? glm::vec3(1, 0, 0) : glm::vec3(q.x, q.y, q.z) / s);
    lua_pushnumber(L, angle);
    return 2;
}
float turnBetween(const glm::mat4& a, const glm::mat4& b) {
    const glm::quat qa = glm::normalize(glm::quat_cast(glm::mat3(a))), qb = glm::normalize(glm::quat_cast(glm::mat3(b)));
    return 2.0f * std::acos(std::clamp(std::fabs(glm::dot(qa, qb)), 0.0f, 1.0f));
}
int cf_angleBetween(lua_State* L) { lua_pushnumber(L, turnBetween(LuaApi::checkCFrame(L, 1), LuaApi::checkCFrame(L, 2))); return 1; }
int cf_fuzzyEq(lua_State* L) {
    const glm::mat4 a = LuaApi::checkCFrame(L, 1), b = LuaApi::checkCFrame(L, 2);
    const float eps = (float)luaL_optnumber(L, 3, 1e-5);
    const bool same = glm::length(glm::vec3(a[3]) - glm::vec3(b[3])) <= eps * std::max(1.0f, glm::length(glm::vec3(a[3]))) &&
                      turnBetween(a, b) <= std::max(eps, 1e-3f);
    lua_pushboolean(L, same);
    return 1;
}
// The turn as three angles in a given order (Enum.RotationOrder: XYZ, XZY, YZX, YXZ, ZXY, ZYX).
int cf_toEulerOrder(lua_State* L) {
    const glm::mat4 m = LuaApi::checkCFrame(L, 1);
    const std::string order = lua_isnoneornil(L, 2) ? "XYZ" : enumItemName(L, 2);
    float a = 0, b = 0, c = 0, x = 0, y = 0, z = 0;
    if (order == "XYZ")      { glm::extractEulerAngleXYZ(m, a, b, c); x = a; y = b; z = c; }
    else if (order == "XZY") { glm::extractEulerAngleXZY(m, a, b, c); x = a; z = b; y = c; }
    else if (order == "YXZ") { glm::extractEulerAngleYXZ(m, a, b, c); y = a; x = b; z = c; }
    else if (order == "YZX") { glm::extractEulerAngleYZX(m, a, b, c); y = a; z = b; x = c; }
    else if (order == "ZXY") { glm::extractEulerAngleZXY(m, a, b, c); z = a; x = b; y = c; }
    else if (order == "ZYX") { glm::extractEulerAngleZYX(m, a, b, c); z = a; y = b; x = c; }
    else return luaL_error(L, "ToEulerAngles: the order should be an Enum.RotationOrder (XYZ, YXZ...)");
    lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, z);
    return 3;
}
// CFrame.fromEulerAngles(rx, ry, rz, order = XYZ): the first letter is the outermost turn.
int cf_fromEuler(lua_State* L) {
    const float x = (float)luaL_optnumber(L, 1, 0), y = (float)luaL_optnumber(L, 2, 0), z = (float)luaL_optnumber(L, 3, 0);
    const std::string order = lua_isnoneornil(L, 4) ? "XYZ" : enumItemName(L, 4);
    if (order.size() != 3) return luaL_error(L, "fromEulerAngles: the order should be an Enum.RotationOrder (XYZ, YXZ...)");
    glm::mat4 m(1.0f);
    for (char axis : order) {
        if (axis == 'X') m = glm::rotate(m, x, {1, 0, 0});
        else if (axis == 'Y') m = glm::rotate(m, y, {0, 1, 0});
        else if (axis == 'Z') m = glm::rotate(m, z, {0, 0, 1});
        else return luaL_error(L, "fromEulerAngles: the order should be an Enum.RotationOrder (XYZ, YXZ...)");
    }
    LuaApi::pushCFrame(L, m);
    return 1;
}
// CFrame.fromMatrix(pos, rightVector, upVector, backVector = right x up)
int cf_fromMatrix(lua_State* L) {
    const glm::vec3 p = LuaApi::checkVector3(L, 1), vx = LuaApi::checkVector3(L, 2), vy = LuaApi::checkVector3(L, 3);
    const glm::vec3 vz = lua_isnoneornil(L, 4) ? glm::cross(glm::normalize(vx), glm::normalize(vy)) : LuaApi::checkVector3(L, 4);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(vx, 0); m[1] = glm::vec4(vy, 0); m[2] = glm::vec4(vz, 0); m[3] = glm::vec4(p, 1);
    LuaApi::pushCFrame(L, m);
    return 1;
}
// CFrame.lookAlong(pos, direction, up = Vector3.yAxis)
int cf_lookAlong(lua_State* L) {
    const glm::vec3 p = LuaApi::checkVector3(L, 1), d = LuaApi::checkVector3(L, 2);
    glm::vec3 up = lua_isnoneornil(L, 3) ? glm::vec3(0, 1, 0) : LuaApi::checkVector3(L, 3);
    if (glm::length(d) < 1e-8f) { LuaApi::pushCFrame(L, glm::translate(glm::mat4(1.0f), p)); return 1; }
    const glm::vec3 f = glm::normalize(d);
    if (glm::length(glm::cross(f, up)) < 1e-6f) up = std::fabs(f.z) < 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(f, up));
    const glm::vec3 realUp = glm::cross(right, f);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(right, 0); m[1] = glm::vec4(realUp, 0); m[2] = glm::vec4(-f, 0); m[3] = glm::vec4(p, 1);
    LuaApi::pushCFrame(L, m);
    return 1;
}
// CFrame.fromRotationBetweenVectors(from, to): the smallest turn taking one onto the other.
int cf_fromBetween(lua_State* L) {
    const glm::vec3 a = LuaApi::checkVector3(L, 1), b = LuaApi::checkVector3(L, 2);
    if (glm::length(a) < 1e-8f || glm::length(b) < 1e-8f) { LuaApi::pushCFrame(L, glm::mat4(1.0f)); return 1; }
    const glm::vec3 u = glm::normalize(a), v = glm::normalize(b);
    const float d = glm::dot(u, v);
    glm::quat q;
    if (d < -0.999999f) {   // opposite: half a turn about any axis at right angles
        glm::vec3 axis = glm::cross(glm::vec3(1, 0, 0), u);
        if (glm::length(axis) < 1e-6f) axis = glm::cross(glm::vec3(0, 1, 0), u);
        q = glm::angleAxis(glm::pi<float>(), glm::normalize(axis));
    } else {
        const glm::vec3 c = glm::cross(u, v);
        q = glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
    }
    LuaApi::pushCFrame(L, glm::mat4_cast(q));
    return 1;
}
int cf_eq(lua_State* L) {
    glm::mat4* a = LuaApi::toCFrame(L, 1);
    glm::mat4* b = LuaApi::toCFrame(L, 2);
    lua_pushboolean(L, a && b && *a == *b);
    return 1;
}

int cf_index(lua_State* L) {
    glm::mat4 m = LuaApi::checkCFrame(L, 1);
    const std::string key = upperFirst(luaL_checkstring(L, 2));
    const char* k = key.c_str();
    if (!std::strcmp(k, "Position") || !std::strcmp(k, "P")) {   // (cf.p, the old name)
        LuaApi::pushVector3(L, cfPos(m)); return 1; }
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
    if (!std::strcmp(k, "ToOrientation") || !std::strcmp(k, "ToEulerAnglesYXZ")) { lua_pushcfunction(L, cf_toOrientation); return 1; }
    if (!std::strcmp(k, "ToObjectSpace"))  { lua_pushcfunction(L, cf_toObjectSpace); return 1; }
    if (!std::strcmp(k, "ToWorldSpace"))   { lua_pushcfunction(L, cf_toWorldSpace);  return 1; }
    if (!std::strcmp(k, "VectorToWorldSpace"))  { lua_pushcfunction(L, cf_vecToWorld);  return 1; }
    if (!std::strcmp(k, "VectorToObjectSpace")) { lua_pushcfunction(L, cf_vecToObject); return 1; }
    if (!std::strcmp(k, "GetComponents") || !std::strcmp(k, "Components")) { lua_pushcfunction(L, cf_components); return 1; }
    if (!std::strcmp(k, "PointToWorldSpace"))  { lua_pushcfunction(L, cf_toWorld);  return 1; }
    if (!std::strcmp(k, "PointToObjectSpace")) { lua_pushcfunction(L, cf_toObject); return 1; }
    if (!std::strcmp(k, "XVector")) { LuaApi::pushVector3(L, glm::vec3(m[0])); return 1; }   // (the matrix's columns)
    if (!std::strcmp(k, "YVector")) { LuaApi::pushVector3(L, glm::vec3(m[1])); return 1; }
    if (!std::strcmp(k, "ZVector")) { LuaApi::pushVector3(L, glm::vec3(m[2])); return 1; }
    if (!std::strcmp(k, "Orthonormalize")) { lua_pushcfunction(L, cf_orthonormalize); return 1; }
    if (!std::strcmp(k, "ToAxisAngle"))    { lua_pushcfunction(L, cf_toAxisAngle); return 1; }
    if (!std::strcmp(k, "ToEulerAngles"))  { lua_pushcfunction(L, cf_toEulerOrder); return 1; }
    if (!std::strcmp(k, "AngleBetween"))   { lua_pushcfunction(L, cf_angleBetween); return 1; }
    if (!std::strcmp(k, "FuzzyEq"))        { lua_pushcfunction(L, cf_fuzzyEq); return 1; }
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
int v2_cross(lua_State* L) {   // the 2D "cross": how much b turns left of a (a number)
    const glm::vec2 a = LuaApi::checkVector2(L, 1), b = LuaApi::checkVector2(L, 2);
    lua_pushnumber(L, a.x * b.y - a.y * b.x);
    return 1;
}
int v2_abs(lua_State* L)   { LuaApi::pushVector2(L, perComponent(LuaApi::checkVector2(L, 1), absOf)); return 1; }
int v2_ceil(lua_State* L)  { LuaApi::pushVector2(L, perComponent(LuaApi::checkVector2(L, 1), ceilOf)); return 1; }
int v2_floor(lua_State* L) { LuaApi::pushVector2(L, perComponent(LuaApi::checkVector2(L, 1), floorOf)); return 1; }
int v2_sign(lua_State* L)  { LuaApi::pushVector2(L, perComponent(LuaApi::checkVector2(L, 1), signOf)); return 1; }
int v2_max(lua_State* L) {
    glm::vec2 v = LuaApi::checkVector2(L, 1);
    for (int i = 2, n = lua_gettop(L); i <= n; ++i) v = glm::max(v, LuaApi::checkVector2(L, i));
    LuaApi::pushVector2(L, v);
    return 1;
}
int v2_min(lua_State* L) {
    glm::vec2 v = LuaApi::checkVector2(L, 1);
    for (int i = 2, n = lua_gettop(L); i <= n; ++i) v = glm::min(v, LuaApi::checkVector2(L, i));
    LuaApi::pushVector2(L, v);
    return 1;
}
int v2_angle(lua_State* L) {   // (signed when the 3rd argument is true)
    const glm::vec2 a = LuaApi::checkVector2(L, 1), b = LuaApi::checkVector2(L, 2);
    float ang = std::atan2(a.x * b.y - a.y * b.x, glm::dot(a, b));
    if (!lua_toboolean(L, 3)) ang = std::fabs(ang);
    lua_pushnumber(L, ang);
    return 1;
}
int v2_fuzzyEq(lua_State* L) {
    const glm::vec2 a = LuaApi::checkVector2(L, 1), b = LuaApi::checkVector2(L, 2);
    const float eps = (float)luaL_optnumber(L, 3, 1e-5);
    lua_pushboolean(L, std::fabs(a.x - b.x) <= eps * std::max(1.0f, std::fabs(a.x)) &&
                       std::fabs(a.y - b.y) <= eps * std::max(1.0f, std::fabs(a.y)));
    return 1;
}
int v2_index(lua_State* L) {
    glm::vec2 v = LuaApi::checkVector2(L, 1);
    const char* k = luaL_checkstring(L, 2);
    if (!std::strcmp(k, "X") || !std::strcmp(k, "x")) { lua_pushnumber(L, v.x); return 1; }
    if (!std::strcmp(k, "Y") || !std::strcmp(k, "y")) { lua_pushnumber(L, v.y); return 1; }
    if (!std::strcmp(k, "Magnitude")) { lua_pushnumber(L, glm::length(v)); return 1; }
    if (!std::strcmp(k, "Unit")) { LuaApi::pushVector2(L, glm::length(v) > 0 ? v / glm::length(v) : v); return 1; }
    if (!std::strcmp(k, "Lerp")) { lua_pushcfunction(L, v2_lerp); return 1; }
    if (!std::strcmp(k, "Dot"))  { lua_pushcfunction(L, v2_dot); return 1; }
    static const luaL_Reg more[] = {{"Cross", v2_cross}, {"Abs", v2_abs}, {"Ceil", v2_ceil}, {"Floor", v2_floor},
                                    {"Sign", v2_sign}, {"Max", v2_max}, {"Min", v2_min}, {"Angle", v2_angle},
                                    {"FuzzyEq", v2_fuzzyEq}};
    for (const luaL_Reg& r : more) if (!std::strcmp(k, r.name)) { lua_pushcfunction(L, r.func); return 1; }
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
int v2_idiv(lua_State* L) { LuaApi::pushVector2(L, glm::floor(v2Arg(L, 1) / v2Arg(L, 2))); return 1; }
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
glm::vec3* toColor3(lua_State* L, int idx) {
    if (auto* c = static_cast<glm::vec3*>(luaL_testudata(L, idx, kColor))) return c;
    // A BrickColor (a table, see the prelude) works wherever a Color3 does: its colour.
    if (lua_type(L, idx) == LUA_TTABLE) {
        lua_getfield(L, idx, "__brickColor");
        auto* c = static_cast<glm::vec3*>(luaL_testudata(L, -1, kColor));
        lua_pop(L, 1);   // (the table still holds it)
        return c;
    }
    return nullptr;
}
glm::vec3 checkVector3(lua_State* L, int idx) {
    if (auto* v = toVector3(L, idx)) return *v;
    luaL_typeerror(L, idx, "Vector3");
    return {};
}

void pushColor3(lua_State* L, const glm::vec3& c) { newUd(L, kColor, glm::clamp(c, 0.0f, 1.0f)); }
glm::vec3 checkColor3(lua_State* L, int idx) {
    if (auto* c = toColor3(L, idx)) return *c;
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
        {"__mul", v3_mul}, {"__div", v3_div}, {"__idiv", v3_idiv}, {"__unm", v3_unm}, {"__eq", v3_eq},
        {"__tostring", v3_tostring}, {nullptr, nullptr}};
    static const luaL_Reg colMeta[] = {
        {"__index", c3_index}, {"__newindex", readOnly}, {"__eq", c3_eq},
        {"__tostring", c3_tostring}, {nullptr, nullptr}};
    static const luaL_Reg cfMeta[] = {
        {"__index", cf_index}, {"__newindex", readOnly}, {"__mul", cf_mul}, {"__add", cf_add},
        {"__sub", cf_sub}, {"__eq", cf_eq}, {"__tostring", cf_tostring}, {nullptr, nullptr}};
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
        {"__div", v2_div}, {"__idiv", v2_idiv}, {"__unm", v2_unm}, {"__eq", v2_eq}, {"__tostring", v2_tostring}, {nullptr, nullptr}};
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
    pushVector2(L, {1, 0}); lua_setfield(L, -2, "xAxis");
    pushVector2(L, {0, 1}); lua_setfield(L, -2, "yAxis");
    lua_setglobal(L, "Vector2");

    // Vector3 = { new, zero, one, xAxis, yAxis, zAxis }
    lua_newtable(L);
    lua_pushcfunction(L, v3_new); lua_setfield(L, -2, "new");
    pushVector3(L, {0, 0, 0}); lua_setfield(L, -2, "zero");
    pushVector3(L, {1, 1, 1}); lua_setfield(L, -2, "one");
    pushVector3(L, {1, 0, 0}); lua_setfield(L, -2, "xAxis");
    pushVector3(L, {0, 1, 0}); lua_setfield(L, -2, "yAxis");
    pushVector3(L, {0, 0, 1}); lua_setfield(L, -2, "zAxis");
    lua_pushcfunction(L, v3_fromNormalId); lua_setfield(L, -2, "FromNormalId");
    lua_pushcfunction(L, v3_fromAxis);     lua_setfield(L, -2, "FromAxis");
    lua_setglobal(L, "Vector3");

    lua_newtable(L);
    lua_pushcfunction(L, c3_new);     lua_setfield(L, -2, "new");
    lua_pushcfunction(L, c3_fromRGB); lua_setfield(L, -2, "fromRGB");
    lua_pushcfunction(L, c3_fromHSV); lua_setfield(L, -2, "fromHSV");
    lua_pushcfunction(L, c3_fromHex); lua_setfield(L, -2, "fromHex");
    lua_pushcfunction(L, c3_toHSV);   lua_setfield(L, -2, "toHSV");
    lua_setglobal(L, "Color3");

    lua_newtable(L);
    lua_pushcfunction(L, cf_new);    lua_setfield(L, -2, "new");
    lua_pushcfunction(L, cf_angles); lua_setfield(L, -2, "Angles");
    lua_pushcfunction(L, cf_angles); lua_setfield(L, -2, "fromEulerAnglesXYZ");
    lua_pushcfunction(L, cf_lookAt); lua_setfield(L, -2, "lookAt");
    lua_pushcfunction(L, cf_fromOrientation); lua_setfield(L, -2, "fromOrientation");
    lua_pushcfunction(L, cf_fromOrientation); lua_setfield(L, -2, "fromEulerAnglesYXZ");
    lua_pushcfunction(L, cf_fromAxisAngle);   lua_setfield(L, -2, "fromAxisAngle");
    lua_pushcfunction(L, cf_fromEuler);       lua_setfield(L, -2, "fromEulerAngles");
    lua_pushcfunction(L, cf_fromMatrix);      lua_setfield(L, -2, "fromMatrix");
    lua_pushcfunction(L, cf_lookAlong);       lua_setfield(L, -2, "lookAlong");
    lua_pushcfunction(L, cf_fromBetween);     lua_setfield(L, -2, "fromRotationBetweenVectors");
    pushCFrame(L, glm::mat4(1.0f));           lua_setfield(L, -2, "identity");
    lua_setglobal(L, "CFrame");
}

} // namespace LuaApi
