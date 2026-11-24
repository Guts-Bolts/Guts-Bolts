// The parts of Luau's standard library that plain Lua 5.4 doesn't have, so scripts written
// for Roblox run as they are. Our scripts are Lua 5.4 underneath (Luau.h turns Luau's syntax
// into Lua), and these are the library calls that used to stop a script with "attempt to
// index a nil value":
//
//   bit32.band(a, b), bit32.extract(n, 3, 4)...      32-bit maths on whole numbers
//   local b = buffer.create(16) buffer.writeu32(b, 0, 7)   a block of raw bytes
//   vector.create(1, 2, 3), vector.dot(a, b)...      (a vector is a Vector3 here)
//   table.clone / create / freeze / isfrozen / maxn / foreach / foreachi / getn
//   math.sign / map / isnan / isinf / isfinite, math.phi, math.sqrt2, math.tau
//   utf8.graphemes, utf8.nfcnormalize, utf8.nfdnormalize
//   task.cancel(thread), task.spawn(...) returning the thread
//   debug.traceback, debug.info, debug.profilebegin / profileend, memory categories
//   coroutine.finally
//   unpack, gcinfo, newproxy, elapsedTime, version, printidentity, ypcall, settings, stats
//   Axes, Faces, Vector2int16, Vector3int16, Region3int16, FloatCurveKey, RotationCurveKey,
//   ValueCurveKey and SharedTable
//
// Parallel Luau: Instance.new("Actor"), script:GetActor(), actor:SendMessage /
// BindToMessage / BindToMessageParallel, signal:ConnectParallel, task.desynchronize /
// task.synchronize and SharedTable all work. The code runs one piece at a time (never
// truly at the same time), which gives the same results, just without the speed-up.
#include "LuauLibs.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Log.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace LuauLibs {
namespace {

// --- bit32 -------------------------------------------------------------------------

uint32_t u32(lua_State* L, int idx) {
    const double d = luaL_checknumber(L, idx);
    if (!std::isfinite(d)) return 0;
    // Like Luau: any number is taken modulo 2^32 (so -1 is 0xFFFFFFFF).
    const double m = std::fmod(std::trunc(d), 4294967296.0);
    return (uint32_t)(int64_t)(m < 0 ? m + 4294967296.0 : m);
}
int push(lua_State* L, uint32_t v) { lua_pushinteger(L, (lua_Integer)v); return 1; }

int b_band(lua_State* L) { uint32_t r = 0xFFFFFFFFu; for (int i = 1, n = lua_gettop(L); i <= n; ++i) r &= u32(L, i); return push(L, r); }
int b_bor(lua_State* L)  { uint32_t r = 0; for (int i = 1, n = lua_gettop(L); i <= n; ++i) r |= u32(L, i); return push(L, r); }
int b_bxor(lua_State* L) { uint32_t r = 0; for (int i = 1, n = lua_gettop(L); i <= n; ++i) r ^= u32(L, i); return push(L, r); }
int b_btest(lua_State* L) { uint32_t r = 0xFFFFFFFFu; for (int i = 1, n = lua_gettop(L); i <= n; ++i) r &= u32(L, i); lua_pushboolean(L, r != 0); return 1; }
int b_bnot(lua_State* L) { return push(L, ~u32(L, 1)); }
int shiftOf(lua_State* L, int idx) { return (int)luaL_checkinteger(L, idx); }
int b_lshift(lua_State* L) {
    const uint32_t x = u32(L, 1); const int s = shiftOf(L, 2);
    if (s <= -32 || s >= 32) return push(L, 0);
    return push(L, s >= 0 ? x << s : x >> -s);
}
int b_rshift(lua_State* L) {
    const uint32_t x = u32(L, 1); const int s = shiftOf(L, 2);
    if (s <= -32 || s >= 32) return push(L, 0);
    return push(L, s >= 0 ? x >> s : x << -s);
}
int b_arshift(lua_State* L) {
    const int32_t x = (int32_t)u32(L, 1); const int s = shiftOf(L, 2);
    if (s < 0) return push(L, s <= -32 ? 0 : (uint32_t)x << -s);
    return push(L, (uint32_t)(s >= 32 ? (x < 0 ? -1 : 0) : (x >> s)));
}
uint32_t rotl(uint32_t x, int s) { s &= 31; return s ? (x << s) | (x >> (32 - s)) : x; }
int b_lrotate(lua_State* L) { return push(L, rotl(u32(L, 1), shiftOf(L, 2))); }
int b_rrotate(lua_State* L) { return push(L, rotl(u32(L, 1), -shiftOf(L, 2))); }
void fieldArgs(lua_State* L, int fi, int wi, int& f, int& w) {
    f = (int)luaL_checkinteger(L, fi);
    w = (int)luaL_optinteger(L, wi, 1);
    luaL_argcheck(L, f >= 0, fi, "field cannot be negative");
    luaL_argcheck(L, w > 0, wi, "width must be positive");
    luaL_argcheck(L, f + w <= 32, wi, "trying to access non-existent bits");
}
uint32_t mask(int w) { return w >= 32 ? 0xFFFFFFFFu : ((1u << w) - 1u); }
int b_extract(lua_State* L) { int f, w; fieldArgs(L, 2, 3, f, w); return push(L, (u32(L, 1) >> f) & mask(w)); }
int b_replace(lua_State* L) {
    int f, w; fieldArgs(L, 3, 4, f, w);
    const uint32_t m = mask(w);
    return push(L, (u32(L, 1) & ~(m << f)) | ((u32(L, 2) & m) << f));
}
int b_countlz(lua_State* L) { uint32_t x = u32(L, 1); int n = 0; while (n < 32 && !(x & 0x80000000u)) { x <<= 1; ++n; } lua_pushinteger(L, n); return 1; }
int b_countrz(lua_State* L) { uint32_t x = u32(L, 1); int n = 0; while (n < 32 && !(x & 1u)) { x >>= 1; ++n; } lua_pushinteger(L, n); return 1; }
int b_byteswap(lua_State* L) {
    const uint32_t x = u32(L, 1);
    return push(L, (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24));
}

// --- buffer: a fixed-size block of bytes (little-endian numbers, like Luau) ------------

constexpr const char* kBuffer = "buffer";   // (also what typeof() says)
constexpr size_t kMostBytes = 1024u * 1024u * 1024u;   // 1 GB, Luau's limit

struct Buf { size_t size; unsigned char data[1]; };

Buf* checkBuf(lua_State* L, int idx) { return static_cast<Buf*>(luaL_checkudata(L, idx, kBuffer)); }
Buf* newBuf(lua_State* L, size_t size) {
    if (size > kMostBytes) luaL_error(L, "buffer size is too large (most is 1 GB)");
    auto* b = static_cast<Buf*>(lua_newuserdatauv(L, sizeof(Buf) + size, 0));
    b->size = size;
    std::memset(b->data, 0, size + 1);
    luaL_setmetatable(L, kBuffer);
    return b;
}
size_t offsetArg(lua_State* L, Buf* b, int idx, size_t bytes) {
    const lua_Integer o = luaL_checkinteger(L, idx);
    if (o < 0 || (size_t)o > b->size || bytes > b->size - (size_t)o) luaL_error(L, "buffer access out of bounds");
    return (size_t)o;
}
int buf_create(lua_State* L) {
    const lua_Integer n = luaL_checkinteger(L, 1);
    luaL_argcheck(L, n >= 0, 1, "size can't be negative");
    newBuf(L, (size_t)n);
    return 1;
}
int buf_fromstring(lua_State* L) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, 1, &n);
    Buf* b = newBuf(L, n);
    std::memcpy(b->data, s, n);
    return 1;
}
int buf_tostring(lua_State* L) { Buf* b = checkBuf(L, 1); lua_pushlstring(L, (const char*)b->data, b->size); return 1; }
int buf_len(lua_State* L) { lua_pushinteger(L, (lua_Integer)checkBuf(L, 1)->size); return 1; }

template <typename T> int readT(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    const size_t o = offsetArg(L, b, 2, sizeof(T));
    T v; std::memcpy(&v, b->data + o, sizeof(T));
    if constexpr (std::is_floating_point_v<T>) lua_pushnumber(L, (double)v);
    else lua_pushinteger(L, (lua_Integer)v);
    return 1;
}
template <typename T> int writeT(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    const size_t o = offsetArg(L, b, 2, sizeof(T));
    const double d = luaL_checknumber(L, 3);
    T v;
    if constexpr (std::is_floating_point_v<T>) v = (T)d;
    else {   // whole numbers wrap around, like Luau
        const double m = std::isfinite(d) ? std::fmod(std::trunc(d), 4294967296.0) : 0.0;
        v = (T)(uint32_t)(int64_t)(m < 0 ? m + 4294967296.0 : m);
    }
    std::memcpy(b->data + o, &v, sizeof(T));
    return 0;
}
int buf_readvec(lua_State* L, int count) {
    Buf* b = checkBuf(L, 1);
    const size_t o = offsetArg(L, b, 2, sizeof(float) * count);
    float v[3] = {0, 0, 0};
    std::memcpy(v, b->data + o, sizeof(float) * count);
    LuaApi::pushVector3(L, glm::vec3(v[0], v[1], v[2]));
    return 1;
}
int buf_writevec(lua_State* L, int count) {
    Buf* b = checkBuf(L, 1);
    const size_t o = offsetArg(L, b, 2, sizeof(float) * count);
    const glm::vec3 p = LuaApi::checkVector3(L, 3);
    const float v[3] = {p.x, p.y, p.z};
    std::memcpy(b->data + o, v, sizeof(float) * count);
    return 0;
}
int buf_readstring(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    const lua_Integer n = luaL_checkinteger(L, 3);
    luaL_argcheck(L, n >= 0, 3, "count can't be negative");
    const size_t o = offsetArg(L, b, 2, (size_t)n);
    lua_pushlstring(L, (const char*)b->data + o, (size_t)n);
    return 1;
}
int buf_writestring(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    size_t n = 0;
    const char* s = luaL_checklstring(L, 3, &n);
    if (!lua_isnoneornil(L, 4)) {
        const lua_Integer c = luaL_checkinteger(L, 4);
        luaL_argcheck(L, c >= 0 && (size_t)c <= n, 4, "count is bigger than the string");
        n = (size_t)c;
    }
    const size_t o = offsetArg(L, b, 2, n);
    std::memcpy(b->data + o, s, n);
    return 0;
}
int buf_copy(lua_State* L) {
    Buf* to = checkBuf(L, 1);
    Buf* from = checkBuf(L, 3);
    const lua_Integer so = luaL_optinteger(L, 4, 0);
    if (so < 0 || (size_t)so > from->size) return luaL_error(L, "buffer access out of bounds");
    const lua_Integer c = luaL_optinteger(L, 5, (lua_Integer)(from->size - (size_t)so));
    if (c < 0 || (size_t)c > from->size - (size_t)so) return luaL_error(L, "buffer access out of bounds");
    const size_t o = offsetArg(L, to, 2, (size_t)c);
    std::memmove(to->data + o, from->data + so, (size_t)c);
    return 0;
}
int buf_fill(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    const lua_Integer start = luaL_checkinteger(L, 2);
    if (start < 0 || (size_t)start > b->size) return luaL_error(L, "buffer access out of bounds");
    const int value = (int)(u32(L, 3) & 0xFFu);
    const lua_Integer c = luaL_optinteger(L, 4, (lua_Integer)(b->size - (size_t)start));
    const size_t o = offsetArg(L, b, 2, (size_t)std::max<lua_Integer>(0, c));
    if (c > 0) std::memset(b->data + o, value, (size_t)c);
    return 0;
}
void bitArgs(lua_State* L, Buf* b, lua_Integer& off, int& count) {
    off = luaL_checkinteger(L, 2);
    count = (int)luaL_checkinteger(L, 3);
    luaL_argcheck(L, count >= 0 && count <= 32, 3, "bit count must be 0 to 32");
    if (off < 0 || (uint64_t)off + (uint64_t)count > (uint64_t)b->size * 8u) luaL_error(L, "buffer access out of bounds");
}
int buf_readbits(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    lua_Integer off; int count;
    bitArgs(L, b, off, count);
    uint64_t v = 0;
    for (int i = 0; i < count; ++i) {
        const uint64_t bit = (uint64_t)off + i;
        if (b->data[bit >> 3] & (1u << (bit & 7))) v |= (1ull << i);
    }
    lua_pushinteger(L, (lua_Integer)v);
    return 1;
}
int buf_writebits(lua_State* L) {
    Buf* b = checkBuf(L, 1);
    lua_Integer off; int count;
    bitArgs(L, b, off, count);
    const uint32_t v = u32(L, 4);
    for (int i = 0; i < count; ++i) {
        const uint64_t bit = (uint64_t)off + i;
        unsigned char& byte = b->data[bit >> 3];
        if (v & (1u << i)) byte |= (unsigned char)(1u << (bit & 7));
        else byte &= (unsigned char)~(1u << (bit & 7));
    }
    return 0;
}
int buf_tostr(lua_State* L) { lua_pushfstring(L, "buffer: %p", lua_topointer(L, 1)); return 1; }

// --- debug (only the safe, read-only parts) -------------------------------------------

int dbg_traceback(lua_State* L) {
    lua_State* co = L;
    int arg = 1;
    if (lua_isthread(L, 1)) { co = lua_tothread(L, 1); arg = 2; }
    const char* msg = lua_tostring(L, arg);
    if (!msg && !lua_isnoneornil(L, arg)) { lua_pushvalue(L, arg); return 1; }   // (not a message: hand it back, like Lua)
    const int level = (int)luaL_optinteger(L, arg + 1, co == L ? 1 : 0);
    luaL_traceback(L, co, msg, level);
    return 1;
}
// debug.info(level or function [, thread first], "slnaf"): source, line, name, arity, function.
int dbg_info(lua_State* L) {
    lua_State* co = L;
    int arg = 1;
    if (lua_isthread(L, 1)) { co = lua_tothread(L, 1); arg = 2; }
    const char* opts = luaL_checkstring(L, arg + 1);
    lua_Debug ar;
    bool haveFn = false;
    if (lua_isfunction(L, arg)) {
        lua_pushvalue(L, arg);
        if (!lua_getinfo(L, ">Slnu", &ar)) return 0;
        haveFn = true;
    } else {
        const int level = (int)luaL_checkinteger(L, arg);
        luaL_argcheck(L, level >= 0, arg, "level can't be negative");
        if (!lua_getstack(co, level, &ar)) return 0;   // (no such level: nothing, like Luau)
        lua_getinfo(co, "Slnuf", &ar);
        lua_xmove(co, L, 1);
        haveFn = true;
    }
    const int fnIdx = lua_gettop(L);
    int count = 0;
    for (const char* o = opts; *o; ++o) {
        switch (*o) {
            case 's': lua_pushstring(L, ar.short_src); ++count; break;
            case 'l': lua_pushinteger(L, ar.currentline >= 0 ? ar.currentline : ar.linedefined); ++count; break;
            case 'n': if (ar.name) lua_pushstring(L, ar.name); else lua_pushnil(L); ++count; break;
            case 'a': lua_pushinteger(L, ar.nparams); lua_pushboolean(L, ar.isvararg); count += 2; break;
            case 'f': if (haveFn) lua_pushvalue(L, fnIdx); else lua_pushnil(L); ++count; break;
            default: return luaL_argerror(L, arg + 1, "options can only be s, l, n, a and f");
        }
    }
    return count;
}

// newproxy(addMetatable): an empty userdata (old Lua 5.1 scripts use it for unique keys).
int g_newproxy(lua_State* L) {
    lua_newuserdatauv(L, 0, 0);
    if (lua_toboolean(L, 1)) {
        lua_newtable(L);
        lua_setmetatable(L, -2);
    }
    return 1;
}
// gcinfo(): how much memory Lua is using, in kilobytes.
int g_gcinfo(lua_State* L) { lua_pushinteger(L, lua_gc(L, LUA_GCCOUNT, 0)); return 1; }

double startedAt = 0.0;
double clockNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
int g_elapsedTime(lua_State* L) { lua_pushnumber(L, clockNow() - startedAt); return 1; }

// task.cancel(thread): stop a thread that is waiting (or hasn't started) so it never goes on.
int t_cancel(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTHREAD);
    lua_State* co = lua_tothread(L, 1);
    if (co == L) return luaL_error(L, "task.cancel: a thread can't cancel itself");
    LuaApi::engine(L)->cancelThread(co);
    if (lua_status(co) == LUA_YIELD || lua_status(co) == LUA_OK) lua_closethread(co, L);
    return 0;
}

// The Lua half: everything that's simplest written in Lua.
const char* kLibs = R"LUA(
local bit32, buffer, instanceMethods = ...
local function __gb_addMethod(name, fn) instanceMethods[name] = fn end

-- Old Lua 5.1 globals that Luau still has.
unpack = unpack or table.unpack
ypcall = ypcall or pcall
function printidentity(s) print((s or "Current identity is") .. " 2") end
function version() return "0.1.0.0" end

-- table -----------------------------------------------------------------------------
local frozen = setmetatable({}, { __mode = "k" })
function table.clone(t)
    if type(t) ~= "table" then error("invalid argument #1 to 'clone' (table expected, got " .. type(t) .. ")", 2) end
    local c = {}
    for k, v in next, (frozen[t] or t) do c[k] = v end
    local mt = getmetatable(t)
    if frozen[t] then mt = rawget(mt, "__gbOldMeta") end
    if type(mt) == "table" then setmetatable(c, mt) end
    return c
end
function table.create(n, value)
    if type(n) ~= "number" or n < 0 then error("invalid argument #1 to 'create' (a size of 0 or more expected)", 2) end
    local t = {}
    if value ~= nil then for i = 1, math.floor(n) do t[i] = value end end
    return t
end
-- A frozen table's contents move into a hidden table; the (now empty) table reads from it
-- and refuses changes. # and ipairs / pairs / next(t) through it still work.
function table.freeze(t)
    if type(t) ~= "table" then error("invalid argument #1 to 'freeze' (table expected, got " .. type(t) .. ")", 2) end
    if frozen[t] then return t end
    local old = getmetatable(t)
    if old ~= nil and type(old) ~= "table" then error("table.freeze: this table's metatable is locked", 2) end
    local data = {}
    for k, v in next, t do data[k] = v end
    for k in next, data do rawset(t, k, nil) end
    local oldIndex = old and rawget(old, "__index")
    local mt = {}
    if old then for k, v in next, old do mt[k] = v end end
    mt.__gbOldMeta = old
    mt.__index = function(_, k)
        local v = data[k]
        if v ~= nil or oldIndex == nil then return v end
        if type(oldIndex) == "function" then return oldIndex(t, k) end
        return oldIndex[k]
    end
    mt.__newindex = function() error("attempt to modify a readonly table", 2) end
    mt.__len = mt.__len or function() return #data end
    mt.__pairs = function() return next, data, nil end
    mt.__metatable = old and rawget(old, "__metatable")
    frozen[t] = data
    setmetatable(t, mt)
    return t
end
function table.isfrozen(t)
    if type(t) ~= "table" then error("invalid argument #1 to 'isfrozen' (table expected, got " .. type(t) .. ")", 2) end
    return frozen[t] ~= nil
end
-- next(t) on a frozen table walks its hidden contents.
local rawNext = next
function next(t, k)
    local data = type(t) == "table" and frozen[t]
    return rawNext(data or t, k)
end
local rawPairs = pairs
function pairs(t)
    local data = type(t) == "table" and frozen[t]
    if data then return rawNext, data, nil end
    return rawPairs(t)
end
local rawInsert, rawRemove, rawSort, rawClear = table.insert, table.remove, table.sort, table.clear
local function writable(t, what)
    if type(t) == "table" and frozen[t] then error("attempt to modify a readonly table", 3) end
end
function table.insert(t, ...) writable(t) return rawInsert(t, ...) end
function table.remove(t, ...) writable(t) return rawRemove(t, ...) end
function table.sort(t, ...) writable(t) return rawSort(t, ...) end
if rawClear then function table.clear(t) writable(t) return rawClear(t) end end
function table.maxn(t)
    local most = 0
    for k in pairs(t) do if type(k) == "number" and k > most then most = k end end
    return most
end
function table.getn(t) return #t end
function table.foreach(t, fn)
    for k, v in pairs(t) do local r = fn(k, v) if r ~= nil then return r end end
end
function table.foreachi(t, fn)
    for i = 1, #t do local r = fn(i, t[i]) if r ~= nil then return r end end
end

-- math ------------------------------------------------------------------------------
math.phi = (1 + math.sqrt(5)) / 2
math.sqrt2 = math.sqrt(2)
math.tau = math.pi * 2
math.nan = 0 / 0
math.pow = math.pow or function(a, b) return a ^ b end
math.ldexp = math.ldexp or function(m, e) return m * 2.0 ^ e end
math.frexp = math.frexp or function(x)
    if x == 0 or x ~= x or x == math.huge or x == -math.huge then return x, 0 end
    local e = math.floor(math.log(math.abs(x), 2)) + 1
    local m = x / 2.0 ^ e
    if math.abs(m) >= 1 then m, e = m / 2, e + 1 elseif math.abs(m) < 0.5 then m, e = m * 2, e - 1 end
    return m, e
end
math.cosh = math.cosh or function(x) return (math.exp(x) + math.exp(-x)) / 2 end
math.sinh = math.sinh or function(x) return (math.exp(x) - math.exp(-x)) / 2 end
math.tanh = math.tanh or function(x)
    if x > 20 then return 1 elseif x < -20 then return -1 end
    local a, b = math.exp(x), math.exp(-x)
    return (a - b) / (a + b)
end
math.log10 = math.log10 or function(x) return math.log(x, 10) end
math.atan2 = math.atan2 or function(y, x) return math.atan(y, x) end
function math.sign(x) return x > 0 and 1 or x < 0 and -1 or 0 end
function math.map(x, inMin, inMax, outMin, outMax)
    return outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin)
end
math.lerp = math.lerp or function(a, b, t) return a + (b - a) * t end
function math.isnan(x) return x ~= x end
function math.isinf(x) return x == math.huge or x == -math.huge end
function math.isfinite(x) return x == x and x ~= math.huge and x ~= -math.huge end

-- string / utf8 ---------------------------------------------------------------------
string.split = string.split or function(s, sep)
    sep = sep or ","
    local out, start = {}, 1
    if sep == "" then for i = 1, #s do out[i] = s:sub(i, i) end return out end
    while true do
        local i, j = string.find(s, sep, start, true)
        if not i then out[#out + 1] = s:sub(start) break end
        out[#out + 1] = s:sub(start, i - 1)
        start = j + 1
    end
    return out
end
-- Graphemes: a letter plus the accents and joiners stuck to it count as one.
local function joins(cp)
    return (cp >= 0x300 and cp <= 0x36F) or (cp >= 0x1AB0 and cp <= 0x1AFF) or (cp >= 0x1DC0 and cp <= 0x1DFF)
        or (cp >= 0x20D0 and cp <= 0x20FF) or (cp >= 0xFE00 and cp <= 0xFE0F) or (cp >= 0xFE20 and cp <= 0xFE2F)
        or cp == 0x200D or (cp >= 0x1F3FB and cp <= 0x1F3FF) or (cp >= 0xE0020 and cp <= 0xE007F)
end
function utf8.graphemes(s, i, j)
    i = i or 1
    j = j or #s
    local list, prevJoiner = {}, false
    for pos, cp in utf8.codes(s) do
        if pos >= i and pos <= j then
            local last = list[#list]
            if last and (joins(cp) or prevJoiner or (cp == 0x0A and s:byte(pos - 1) == 0x0D)) then
                last[2] = pos + #utf8.char(cp) - 1
            else
                list[#list + 1] = { pos, pos + #utf8.char(cp) - 1 }
            end
            prevJoiner = cp == 0x200D
        end
    end
    local n = 0
    return function()
        n = n + 1
        local g = list[n]
        if g then return g[1], g[2] end
    end
end
-- (We don't carry Unicode's composition tables; text comes back as it went in.)
function utf8.nfcnormalize(s) if type(s) ~= "string" then error("string expected", 2) end return s end
function utf8.nfdnormalize(s) if type(s) ~= "string" then error("string expected", 2) end return s end

-- vector: Luau's built-in 3D vector. Here it is a Vector3, so both work together.
vector = {
    zero = Vector3.new(0, 0, 0),
    one = Vector3.new(1, 1, 1),
    create = function(x, y, z) return Vector3.new(x or 0, y or 0, z or 0) end,
    magnitude = function(v) return v.Magnitude end,
    normalize = function(v) local m = v.Magnitude return m > 0 and v / m or v end,
    cross = function(a, b) return a:Cross(b) end,
    dot = function(a, b) return a:Dot(b) end,
    floor = function(v) return Vector3.new(math.floor(v.X), math.floor(v.Y), math.floor(v.Z)) end,
    ceil = function(v) return Vector3.new(math.ceil(v.X), math.ceil(v.Y), math.ceil(v.Z)) end,
    abs = function(v) return Vector3.new(math.abs(v.X), math.abs(v.Y), math.abs(v.Z)) end,
    sign = function(v) return Vector3.new(math.sign(v.X), math.sign(v.Y), math.sign(v.Z)) end,
    clamp = function(v, lo, hi)
        return Vector3.new(math.clamp(v.X, lo.X, hi.X), math.clamp(v.Y, lo.Y, hi.Y), math.clamp(v.Z, lo.Z, hi.Z))
    end,
    max = function(v, ...)
        local x, y, z = v.X, v.Y, v.Z
        for _, o in ipairs({ ... }) do x, y, z = math.max(x, o.X), math.max(y, o.Y), math.max(z, o.Z) end
        return Vector3.new(x, y, z)
    end,
    min = function(v, ...)
        local x, y, z = v.X, v.Y, v.Z
        for _, o in ipairs({ ... }) do x, y, z = math.min(x, o.X), math.min(y, o.Y), math.min(z, o.Z) end
        return Vector3.new(x, y, z)
    end,
    lerp = function(a, b, t) return a + (b - a) * t end,
    angle = function(a, b, axis)
        local c = a:Cross(b)
        local ang = math.atan(c.Magnitude, a:Dot(b))
        if axis and c:Dot(axis) < 0 then ang = -ang end
        return ang
    end,
}

-- coroutine.finally(co, fn): run fn when the thread finishes, fails or is closed.
do
    local after = setmetatable({}, { __mode = "k" })
    local resume, close, wrap = coroutine.resume, coroutine.close, coroutine.wrap
    local function finish(co, ok, err)
        local list = after[co]
        if list and coroutine.status(co) == "dead" then
            after[co] = nil
            for _, fn in ipairs(list) do
                local fine, e = pcall(fn, ok, err)
                if not fine then warn("coroutine.finally: " .. tostring(e)) end
            end
        end
    end
    function coroutine.finally(co, fn)
        if type(co) ~= "thread" then error("invalid argument #1 to 'finally' (thread expected)", 2) end
        if type(fn) ~= "function" then error("invalid argument #2 to 'finally' (function expected)", 2) end
        after[co] = after[co] or {}
        table.insert(after[co], fn)
    end
    function coroutine.resume(co, ...)
        local r = table.pack(resume(co, ...))
        if after[co] then finish(co, r[1], not r[1] and r[2] or nil) end
        return table.unpack(r, 1, r.n)
    end
    function coroutine.close(co)
        local ok, err = close(co)
        if after[co] then finish(co, ok, err) end
        return ok, err
    end
end

-- debug: profiling marks and memory categories (the live profiler times whole scripts).
do
    local category = "Default"
    debug.profilebegin = function(label) end
    debug.profileend = function() end
    debug.setmemorycategory = function(name) local old = category category = tostring(name) return old end
    debug.resetmemorycategory = function() category = "Default" end
    debug.getmemorycategory = function() return category end
    debug.dumpcodesize = function() end
end

-- settings() / UserSettings() / stats(): small stand-ins old scripts poke at.
do
    local function bag(name)
        return setmetatable({ Name = name, ClassName = name }, { __index = function(t, k)
            local sub = bag(k)
            rawset(t, k, sub)
            return sub
        end, __tostring = function() return name end })
    end
    local s = bag("GlobalSettings")
    function s:GetService(name) return self[name] end
    s.Rendering.QualityLevel = "Automatic"
    s.Physics.AllowSleep = true
    function settings() return s end
    local u = bag("UserSettings")
    function u:GetService(name) return self[name] end
    function u:IsUserFeatureEnabled() return false end
    u.UserGameSettings.SavedQualityLevel = "Automatic"
    u.UserGameSettings.MasterVolume = 1
    u.UserGameSettings.MouseSensitivity = 1
    u.UserGameSettings.RotationType = "MovementRelative"
    function UserSettings() return u end
    function stats() return game:GetService("Stats") end
end

-- Small data types ----------------------------------------------------------------
local function typed(name, fields)
    local mt = { __type = name, __name = name }
    mt.__index = fields or {}
    return mt
end
local function enumName(v)
    if type(v) == "string" then return v end
    if type(v) == "table" or type(v) == "userdata" then
        local ok, n = pcall(function() return v.Name end)
        if ok and type(n) == "string" then return n end
    end
    return tostring(v)
end

-- Axes.new(Enum.Axis.X, Enum.NormalId.Top...) and Faces.new(Enum.NormalId.Top...)
do
    local faceAxis = { Right = "X", Left = "X", Top = "Y", Bottom = "Y", Front = "Z", Back = "Z" }
    local axesMeta = typed("Axes")
    axesMeta.__tostring = function(a)
        local s = {}
        for _, k in ipairs({ "X", "Y", "Z" }) do if a[k] then s[#s + 1] = k end end
        return table.concat(s, ", ")
    end
    axesMeta.__eq = function(a, b) return a.X == b.X and a.Y == b.Y and a.Z == b.Z end
    Axes = { new = function(...)
        local a = { X = false, Y = false, Z = false }
        for _, v in ipairs({ ... }) do
            local n = enumName(v)
            n = faceAxis[n] or n
            if a[n] == nil then error("Axes.new: " .. tostring(n) .. " isn't an axis or a face", 2) end
            a[n] = true
        end
        a.x, a.y, a.z = a.X, a.Y, a.Z
        a.Right, a.Left = a.X, a.X
        a.Top, a.Bottom = a.Y, a.Y
        a.Front, a.Back = a.Z, a.Z
        return setmetatable(a, axesMeta)
    end }
    local order = { "Right", "Top", "Back", "Left", "Bottom", "Front" }
    local facesMeta = typed("Faces")
    facesMeta.__tostring = function(f)
        local s = {}
        for _, k in ipairs(order) do if f[k] then s[#s + 1] = k end end
        return table.concat(s, ", ")
    end
    facesMeta.__eq = function(a, b)
        for _, k in ipairs(order) do if a[k] ~= b[k] then return false end end
        return true
    end
    Faces = { new = function(...)
        local f = {}
        for _, k in ipairs(order) do f[k] = false end
        for _, v in ipairs({ ... }) do
            local n = enumName(v)
            if f[n] == nil then error("Faces.new: " .. tostring(n) .. " isn't a face", 2) end
            f[n] = true
        end
        return setmetatable(f, facesMeta)
    end }
end

-- Vector2int16 / Vector3int16 / Region3int16: whole-number vectors (-32768 to 32767).
do
    local function i16(v)
        v = math.floor((tonumber(v) or 0) + 0) % 65536
        if v >= 32768 then v = v - 65536 end
        return v
    end
    local function make(name, keys)
        local mt = typed(name)
        local cls = {}
        local function new(...)
            local args, o = { ... }, {}
            for i, k in ipairs(keys) do o[k] = i16(args[i]) end
            return setmetatable(o, mt)
        end
        cls.new = new
        local function each(a, b, fn)
            local args = {}
            for i, k in ipairs(keys) do
                local bv = type(b) == "number" and b or b[k]
                args[i] = fn(a[k], bv)
            end
            return new(table.unpack(args))
        end
        mt.__add = function(a, b) return each(a, b, function(x, y) return x + y end) end
        mt.__sub = function(a, b) return each(a, b, function(x, y) return x - y end) end
        mt.__mul = function(a, b)
            if type(a) == "number" then a, b = b, a end
            return each(a, b, function(x, y) return x * y end)
        end
        mt.__div = function(a, b) return each(a, b, function(x, y) return x / y end) end
        mt.__unm = function(a) return each(a, -1, function(x, y) return x * y end) end
        mt.__eq = function(a, b)
            for _, k in ipairs(keys) do if a[k] ~= b[k] then return false end end
            return true
        end
        mt.__tostring = function(a)
            local s = {}
            for i, k in ipairs(keys) do s[i] = tostring(a[k]) end
            return table.concat(s, ", ")
        end
        return cls
    end
    Vector2int16 = make("Vector2int16", { "X", "Y" })
    Vector3int16 = make("Vector3int16", { "X", "Y", "Z" })
    local regionMeta = typed("Region3int16")
    regionMeta.__tostring = function(r) return tostring(r.Min) .. "; " .. tostring(r.Max) end
    Region3int16 = { new = function(lo, hi)
        return setmetatable({ Min = lo or Vector3int16.new(), Max = hi or Vector3int16.new() }, regionMeta)
    end }
end

-- Keys for FloatCurve / RotationCurve / ValueCurve (Enum.KeyInterpolationMode: Constant,
-- Linear, Cubic).
do
    local function keyType(name, valueOk)
        local mt = typed(name)
        mt.__tostring = function(k) return name .. "(" .. tostring(k.Time) .. ", " .. tostring(k.Value) .. ")" end
        return { new = function(time, value, mode)
            if type(time) ~= "number" then error(name .. ".new: the time should be a number", 2) end
            if valueOk and not valueOk(value) then error(name .. ".new: that value doesn't fit", 2) end
            return setmetatable({ Time = time, Value = value, Interpolation = mode or "Cubic",
                                  LeftTangent = nil, RightTangent = nil }, mt)
        end }
    end
    FloatCurveKey = keyType("FloatCurveKey", function(v) return type(v) == "number" end)
    RotationCurveKey = keyType("RotationCurveKey", function(v) return typeof(v) == "CFrame" end)
    ValueCurveKey = keyType("ValueCurveKey")
end

-- SharedTable: a table Actors share (here everything runs one at a time, so it is just a
-- table with Roblox's helper functions).
do
    local mt = typed("SharedTable")
    local data = setmetatable({}, { __mode = "k" })
    mt.__index = function(t, k) return data[t][k] end
    mt.__newindex = function(t, k, v)
        if type(k) ~= "string" and type(k) ~= "number" then error("SharedTable keys must be strings or numbers", 2) end
        if type(v) == "table" and getmetatable(v) ~= mt then v = SharedTable.new(v) end
        if type(v) == "function" or type(v) == "thread" then error("SharedTable can't hold a " .. type(v), 2) end
        data[t][k] = v
    end
    mt.__len = function(t) return #data[t] end
    mt.__iter = function(t) return next, data[t] end
    mt.__pairs = function(t) return next, data[t], nil end
    local frozenShared = setmetatable({}, { __mode = "k" })
    SharedTable = {}
    function SharedTable.new(init)
        local t = setmetatable({}, mt)
        data[t] = {}
        if type(init) == "table" then for k, v in pairs(init) do t[k] = v end end
        return t
    end
    function SharedTable.clear(t) if frozenShared[t] then error("SharedTable is frozen", 2) end data[t] = {} end
    function SharedTable.clone(t, deep)
        local c = SharedTable.new()
        for k, v in pairs(data[t]) do
            if deep and type(v) == "table" and getmetatable(v) == mt then v = SharedTable.clone(v, true) end
            data[c][k] = v
        end
        return c
    end
    function SharedTable.cloneAndFreeze(t, deep) local c = SharedTable.clone(t, deep) frozenShared[c] = true return c end
    function SharedTable.isFrozen(t) return frozenShared[t] == true end
    function SharedTable.size(t) local n = 0 for _ in pairs(data[t]) do n = n + 1 end return n end
    function SharedTable.increment(t, k, by)
        if frozenShared[t] then error("SharedTable is frozen", 2) end
        local old = data[t][k] or 0
        data[t][k] = old + by
        return old
    end
    function SharedTable.update(t, k, fn)
        if frozenShared[t] then error("SharedTable is frozen", 2) end
        data[t][k] = fn(data[t][k])
    end
    local function guard(t, k, v)
        if frozenShared[t] then error("SharedTable is frozen", 2) end
    end
    local set = mt.__newindex
    mt.__newindex = function(t, k, v) guard(t) set(t, k, v) end
end

-- More globals --------------------------------------------------------------------
math.e = math.exp(1)
shared = shared or {}
-- getfenv / setfenv (Lua 5.1): every script here shares one set of globals, so getfenv
-- gives that table; setfenv can't swap it and says so once.
function getfenv(f) return _G end
do
    local told = false
    function setfenv(f, env)
        if not told then told = true warn("setfenv isn't supported here (scripts share their globals); the call did nothing") end
        return f
    end
end
-- loadstring(source, name): compile some code (plain Lua text, like load()).
function loadstring(src, name)
    if type(src) ~= "string" then error("loadstring: the code should be a string", 2) end
    return load(src, "=" .. tostring(name or "loadstring"), "t")
end

-- Enum items are their names here ("Plastic"), so item.Name works on any of them, and
-- item:IsA("Material") checks the list it came from.
do
    local smeta = getmetatable("")
    local strlib = string
    smeta.__index = function(s, k)
        local f = strlib[k]
        if f ~= nil then return f end
        if k == "Name" then return s end
        if k == "IsA" then return function(item, enumName)
            local ok, items = pcall(function() return Enum[enumName]:GetEnumItems() end)
            if not ok or type(items) ~= "table" or #items == 0 then return false end
            for _, v in ipairs(items) do if v == item then return true end end
            return false
        end end
        return nil
    end
end

-- Instance.fromExisting(obj): a copy of obj without its children.
do
    local inst = Instance
    if inst and inst.fromExisting == nil then
        inst.fromExisting = function(obj)
            local c = obj:Clone()
            if c then c:ClearAllChildren() end
            return c
        end
    end
end

-- Content: an asset's address as a value (Content.fromUri / fromAssetId / fromObject).
if Content then
    Content.none = Content.none or ""
    Content.fromAssetId = Content.fromAssetId or function(id) return "rbxassetid://" .. tostring(math.floor(tonumber(id) or 0)) end
end

-- params:AddToFilter(objectOrList), on RaycastParams and OverlapParams.
local function addToFilter(params, what)
    local list = params.FilterDescendantsInstances or {}
    if type(what) == "table" and getmetatable(what) == nil then
        for _, o in ipairs(what) do table.insert(list, o) end
    else
        table.insert(list, what)
    end
    params.FilterDescendantsInstances = list
end
for _, cls in ipairs({ "RaycastParams", "OverlapParams" }) do
    local t = _G[cls]
    if type(t) == "table" and type(t.new) == "function" then
        local made = t.new
        t.new = function(...)
            local p = made(...)
            if type(p) == "table" then
                p.AddToFilter = addToFilter
                if cls == "RaycastParams" then p.BruteForceAllSlow = p.BruteForceAllSlow or false end
                if cls == "OverlapParams" then p.Tolerance = p.Tolerance or 0 end
            end
            return p
        end
    end
end
-- ray:ClosestPoint(point) / ray:Distance(point)
if type(Ray) == "table" and type(Ray.new) == "function" then
    local made = Ray.new
    Ray.new = function(origin, direction)
        local r = made(origin, direction)
        function r:ClosestPoint(p)
            local d = self.Direction
            local len2 = d:Dot(d)
            if len2 <= 0 then return self.Origin end
            local t = math.max(0, (p - self.Origin):Dot(d) / len2)
            return self.Origin + d * t
        end
        function r:Distance(p) return (p - self:ClosestPoint(p)).Magnitude end
        return r
    end
end

-- Rect: a 2D box (Min, Max, Width, Height).
do
    local mt = typed("Rect")
    mt.__index = function(r, k)
        if k == "Width" then return r.Max.X - r.Min.X end
        if k == "Height" then return r.Max.Y - r.Min.Y end
    end
    mt.__tostring = function(r) return tostring(r.Min) .. ", " .. tostring(r.Max) end
    mt.__eq = function(a, b) return a.Min == b.Min and a.Max == b.Max end
    Rect = { new = function(a, b, c, d)
        if type(a) == "number" then a, b = Vector2.new(a, b), Vector2.new(c, d) end
        return setmetatable({ Min = a or Vector2.new(), Max = b or Vector2.new() }, mt)
    end }
end

-- Region3: a box in the world, by its corners (CFrame is its middle, Size its size).
do
    local mt = typed("Region3")
    local methods = {}
    mt.__index = function(r, k)
        if k == "CFrame" then return CFrame.new((rawget(r, "__min") + rawget(r, "__max")) / 2) end
        if k == "Size" then return rawget(r, "__max") - rawget(r, "__min") end
        return methods[k]
    end
    mt.__tostring = function(r) return tostring(r.CFrame) .. "; " .. tostring(r.Size) end
    local function make(lo, hi) return setmetatable({ __min = lo, __max = hi }, mt) end
    -- region:ExpandToGrid(resolution): its corners pushed out to the nearest grid lines.
    function methods.ExpandToGrid(r, res)
        local lo, hi = rawget(r, "__min"), rawget(r, "__max")
        local function down(v) return math.floor(v / res) * res end
        local function up(v) return math.ceil(v / res) * res end
        return make(Vector3.new(down(lo.X), down(lo.Y), down(lo.Z)), Vector3.new(up(hi.X), up(hi.Y), up(hi.Z)))
    end
    Region3 = { new = function(lo, hi)
        lo, hi = lo or Vector3.new(), hi or Vector3.new()
        return make(lo:Min(hi), lo:Max(hi))
    end }
end

-- Font: a text face (Family, Weight, Style). Text shows in our own font either way.
do
    local mt = typed("Font")
    mt.__index = function(f, k)
        if k == "Bold" then return tostring(rawget(f, "Weight")):find("Bold") ~= nil or tostring(rawget(f, "Weight")):find("Heavy") ~= nil end
    end
    mt.__tostring = function(f) return "Font { Family = " .. f.Family .. ", Weight = " .. tostring(f.Weight) .. ", Style = " .. tostring(f.Style) .. " }" end
    mt.__eq = function(a, b) return a.Family == b.Family and tostring(a.Weight) == tostring(b.Weight) and tostring(a.Style) == tostring(b.Style) end
    local function new(family, weight, style)
        return setmetatable({ Family = tostring(family or "rbxasset://fonts/families/SourceSansPro.json"),
                              Weight = weight or "Regular", Style = style or "Normal" }, mt)
    end
    local families = { SourceSans = "SourceSansPro", SourceSansBold = "SourceSansPro", Arial = "Arial", ArialBold = "Arial",
                       GothamBold = "GothamSSm", Gotham = "GothamSSm", GothamBlack = "GothamSSm", Code = "RobotoMono",
                       Cartoon = "ComicNeueAngular", Highway = "HighwayGothic", SciFi = "Michroma", Fantasy = "Merriweather",
                       Antique = "SpecialElite", Legacy = "LegacyArial", Bodoni = "Balthazar", Garamond = "Guru" }
    Font = {
        new = new,
        fromName = function(name, weight, style) return new("rbxasset://fonts/families/" .. tostring(name) .. ".json", weight, style) end,
        fromId = function(id, weight, style) return new("rbxassetid://" .. tostring(id), weight, style) end,
        fromEnum = function(e)
            local n = tostring(e):gsub("^Enum%.Font%.", "")
            local weight = (n:find("Black") and "Heavy") or (n:find("Bold") and "Bold") or (n:find("Light") and "Light") or "Regular"
            return new("rbxasset://fonts/families/" .. (families[n] or n) .. ".json", weight, n:find("Italic") and "Italic" or "Normal")
        end,
    }
end

-- PathWaypoint.new(position, action = Walk, label = "")
do
    local mt = typed("PathWaypoint")
    mt.__tostring = function(w) return tostring(w.Position) .. " " .. tostring(w.Action) end
    PathWaypoint = { new = function(pos, action, label)
        return setmetatable({ Position = pos or Vector3.new(), Action = action or "Walk", Label = label or "" }, mt)
    end }
end

-- DateTime: the rest (from a date, from ISO text, and formatting like Roblox's
-- FormatUniversalTime("LLL", "en-us")).
if type(DateTime) == "table" then
    local monthNames = { "January", "February", "March", "April", "May", "June", "July", "August",
                         "September", "October", "November", "December" }
    local dayNames = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }
    local function utcSeconds(y, mo, d, h, mi, s)
        -- Days since 1970 by the civil calendar (no time zone involved).
        y = y - (mo <= 2 and 1 or 0)
        local era = (y >= 0 and y or y - 399) // 400
        local yoe = y - era * 400
        local doy = (153 * (mo + (mo > 2 and -3 or 9)) + 2) // 5 + d - 1
        local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
        local days = era * 146097 + doe - 719468
        return days * 86400 + h * 3600 + mi * 60 + s
    end
    DateTime.fromUniversalTime = function(y, mo, d, h, mi, s, ms)
        local secs = utcSeconds(y or 1970, mo or 1, d or 1, h or 0, mi or 0, s or 0)
        return DateTime.fromUnixTimestampMillis(secs * 1000 + (ms or 0))
    end
    DateTime.fromLocalTime = function(y, mo, d, h, mi, s, ms)
        local secs = os.time({ year = y or 1970, month = mo or 1, day = d or 1, hour = h or 0, min = mi or 0, sec = s or 0 })
        return DateTime.fromUnixTimestampMillis(secs * 1000 + (ms or 0))
    end
    DateTime.fromIsoDate = function(text)
        if type(text) ~= "string" then return nil end
        local y, mo, d, rest = text:match("^(%d%d%d%d)%-(%d%d)%-(%d%d)(.*)$")
        if not y then return nil end
        local h, mi, s, frac, zone = rest:match("^[T ](%d%d):(%d%d):?(%d*)%.?(%d*)(.*)$")
        local ms = frac and #frac > 0 and math.floor(tonumber("0." .. frac) * 1000 + 0.5) or 0
        local secs = utcSeconds(tonumber(y), tonumber(mo), tonumber(d), tonumber(h) or 0, tonumber(mi) or 0, tonumber(s) or 0)
        local sign, zh, zm = (zone or ""):match("^([%+%-])(%d%d):?(%d%d)$")
        if sign then secs = secs - (sign == "+" and 1 or -1) * (tonumber(zh) * 3600 + tonumber(zm) * 60) end
        return DateTime.fromUnixTimestampMillis(secs * 1000 + ms)
    end
    local function pad(n, w) return string.format("%0" .. w .. "d", n) end
    local function format(dt, fmt, utc)
        local t = os.date(utc and "!*t" or "*t", dt.UnixTimestamp)
        local ms = dt.UnixTimestampMillis % 1000
        local h12 = (t.hour % 12 == 0) and 12 or t.hour % 12
        local tokens = {
            LLLL = function() return dayNames[t.wday] .. ", " .. monthNames[t.month] .. " " .. t.day .. ", " .. t.year .. " " .. h12 .. ":" .. pad(t.min, 2) .. " " .. (t.hour < 12 and "AM" or "PM") end,
            LLL = function() return monthNames[t.month] .. " " .. t.day .. ", " .. t.year .. " " .. h12 .. ":" .. pad(t.min, 2) .. " " .. (t.hour < 12 and "AM" or "PM") end,
            LL = function() return monthNames[t.month] .. " " .. t.day .. ", " .. t.year end,
            LTS = function() return h12 .. ":" .. pad(t.min, 2) .. ":" .. pad(t.sec, 2) .. " " .. (t.hour < 12 and "AM" or "PM") end,
            LT = function() return h12 .. ":" .. pad(t.min, 2) .. " " .. (t.hour < 12 and "AM" or "PM") end,
            L = function() return pad(t.month, 2) .. "/" .. pad(t.day, 2) .. "/" .. t.year end,
            YYYY = function() return tostring(t.year) end, YY = function() return pad(t.year % 100, 2) end,
            MMMM = function() return monthNames[t.month] end, MMM = function() return monthNames[t.month]:sub(1, 3) end,
            MM = function() return pad(t.month, 2) end, M = function() return tostring(t.month) end,
            dddd = function() return dayNames[t.wday] end, ddd = function() return dayNames[t.wday]:sub(1, 3) end,
            DD = function() return pad(t.day, 2) end, D = function() return tostring(t.day) end,
            HH = function() return pad(t.hour, 2) end, H = function() return tostring(t.hour) end,
            hh = function() return pad(h12, 2) end, h = function() return tostring(h12) end,
            mm = function() return pad(t.min, 2) end, m = function() return tostring(t.min) end,
            ss = function() return pad(t.sec, 2) end, s = function() return tostring(t.sec) end,
            SSS = function() return pad(ms, 3) end, A = function() return t.hour < 12 and "AM" or "PM" end,
            a = function() return t.hour < 12 and "am" or "pm" end,
        }
        local order = { "LLLL", "LLL", "LTS", "YYYY", "MMMM", "dddd", "SSS", "LL", "LT", "MMM", "ddd", "YY", "MM", "DD",
                        "HH", "hh", "mm", "ss", "L", "M", "D", "H", "h", "m", "s", "A", "a" }
        local out, i = {}, 1
        while i <= #fmt do
            local c = fmt:sub(i, i)
            if c == "[" then   -- [text] is kept as it is
                local j = fmt:find("]", i, true) or #fmt + 1
                out[#out + 1] = fmt:sub(i + 1, j - 1)
                i = j + 1
            else
                local matched = false
                for _, tok in ipairs(order) do
                    if fmt:sub(i, i + #tok - 1) == tok then
                        out[#out + 1] = tokens[tok]()
                        i = i + #tok
                        matched = true
                        break
                    end
                end
                if not matched then out[#out + 1] = c i = i + 1 end
            end
        end
        return table.concat(out)
    end
    local probe = DateTime.now()
    local meta = getmetatable(probe)
    if type(meta) == "table" then
        meta.FormatUniversalTime = function(self, fmt, locale) return format(self, tostring(fmt), true) end
        meta.FormatLocalTime = function(self, fmt, locale) return format(self, tostring(fmt), false) end
    end
end

-- Parallel Luau. task.desynchronize / synchronize are fine to call anywhere; signals get
-- ConnectParallel (the same as Connect here); Actors pass messages to their scripts.
do
    task.desynchronize = function() end
    task.synchronize = function() end
    local handlers = {}   -- actor -> topic -> { fn }  (the same Lua object stands for the same Actor)
    local function actorOf(obj)
        local o = obj
        while o ~= nil do
            local ok, cls = pcall(function() return o.ClassName end)
            if ok and cls == "Actor" then return o end
            local fine, parent = pcall(function() return o.Parent end)
            o = fine and parent or nil
        end
        return nil
    end
    __gb_addMethod("GetActor", function(self) return actorOf(self) end)
    local function bind(self, topic, fn)
        if type(topic) ~= "string" then error("BindToMessage: the topic should be a string", 3) end
        if type(fn) ~= "function" then error("BindToMessage: the second argument should be a function", 3) end
        local byActor = handlers[self] or {}
        handlers[self] = byActor
        byActor[topic] = byActor[topic] or {}
        local list = byActor[topic]
        local entry = { fn = fn }
        table.insert(list, entry)
        return { Connected = true, Disconnect = function(c)
            c.Connected = false
            for i, e in ipairs(list) do if e == entry then table.remove(list, i) break end end
        end }
    end
    __gb_addMethod("BindToMessage", function(self, topic, fn) return bind(self, topic, fn) end)
    __gb_addMethod("BindToMessageParallel", function(self, topic, fn) return bind(self, topic, fn) end)
    __gb_addMethod("SendMessage", function(self, topic, ...)
        local byActor = handlers[self]
        local list = byActor and byActor[topic]
        if not list then return end
        local args = table.pack(...)
        for _, e in ipairs(list) do task.defer(e.fn, table.unpack(args, 1, args.n)) end
    end)
end

)LUA";

} // namespace

void install(lua_State* L) {
    startedAt = clockNow();

    static const luaL_Reg bitFns[] = {
        {"band", b_band}, {"bor", b_bor}, {"bxor", b_bxor}, {"btest", b_btest}, {"bnot", b_bnot},
        {"lshift", b_lshift}, {"rshift", b_rshift}, {"arshift", b_arshift}, {"lrotate", b_lrotate},
        {"rrotate", b_rrotate}, {"extract", b_extract}, {"replace", b_replace}, {"countlz", b_countlz},
        {"countrz", b_countrz}, {"byteswap", b_byteswap}, {nullptr, nullptr}};
    luaL_newlib(L, bitFns);
    lua_setglobal(L, "bit32");

    if (luaL_newmetatable(L, kBuffer)) {
        lua_pushcfunction(L, buf_tostr); lua_setfield(L, -2, "__tostring");
        lua_pushstring(L, kBuffer); lua_setfield(L, -2, "__type");
    }
    lua_pop(L, 1);
    static const luaL_Reg bufFns[] = {
        {"create", buf_create}, {"fromstring", buf_fromstring}, {"tostring", buf_tostring}, {"len", buf_len},
        {"readi8", readT<int8_t>}, {"readu8", readT<uint8_t>}, {"readi16", readT<int16_t>}, {"readu16", readT<uint16_t>},
        {"readi32", readT<int32_t>}, {"readu32", readT<uint32_t>}, {"readf32", readT<float>}, {"readf64", readT<double>},
        {"writei8", writeT<int8_t>}, {"writeu8", writeT<uint8_t>}, {"writei16", writeT<int16_t>}, {"writeu16", writeT<uint16_t>},
        {"writei32", writeT<int32_t>}, {"writeu32", writeT<uint32_t>}, {"writef32", writeT<float>}, {"writef64", writeT<double>},
        {"readvectorf32x2", [](lua_State* L) { return buf_readvec(L, 2); }},
        {"readvectorf32x3", [](lua_State* L) { return buf_readvec(L, 3); }},
        {"writevectorf32x2", [](lua_State* L) { return buf_writevec(L, 2); }},
        {"writevectorf32x3", [](lua_State* L) { return buf_writevec(L, 3); }},
        {"readstring", buf_readstring}, {"writestring", buf_writestring}, {"copy", buf_copy}, {"fill", buf_fill},
        {"readbits", buf_readbits}, {"writebits", buf_writebits}, {nullptr, nullptr}};
    luaL_newlib(L, bufFns);
    lua_setglobal(L, "buffer");

    // debug: a table of our own (the real debug library can break out of the sandbox).
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setglobal(L, "debug");
    }
    lua_pushcfunction(L, dbg_traceback); lua_setfield(L, -2, "traceback");
    lua_pushcfunction(L, dbg_info);      lua_setfield(L, -2, "info");
    lua_pop(L, 1);

    lua_register(L, "newproxy", g_newproxy);
    lua_register(L, "gcinfo", g_gcinfo);
    lua_register(L, "elapsedTime", g_elapsedTime);
    lua_getglobal(L, "task");
    if (lua_istable(L, -1)) { lua_pushcfunction(L, t_cancel); lua_setfield(L, -2, "cancel"); }
    lua_pop(L, 1);

    if (luaL_loadbuffer(L, kLibs, std::strlen(kLibs), "=LuauLibs") != LUA_OK) {
        Log::error(std::string("Luau library setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_getglobal(L, "bit32");
    lua_getglobal(L, "buffer");
    lua_getfield(L, LUA_REGISTRYINDEX, "GB.InstanceMethods");
    if (lua_pcall(L, 3, 0, 0) != LUA_OK) {
        Log::error(std::string("Luau library setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

} // namespace LuauLibs
