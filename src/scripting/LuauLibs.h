#pragma once
// The rest of Luau's standard library, for scripts written for Roblox: bit32, buffer,
// vector, the missing table / math / string / utf8 / task / debug functions, a few old
// globals (unpack, gcinfo, newproxy...) and small data types (Axes, Faces, Vector3int16...).
// Parallel Luau (Actors, task.desynchronize, SharedTable) works too, one script at a time.
struct lua_State;
namespace LuauLibs {
// Runs after the prelude (it builds on Vector3, task and the Instance methods).
void install(lua_State* L);
}
