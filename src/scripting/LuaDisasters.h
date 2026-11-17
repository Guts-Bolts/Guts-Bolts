#pragma once
// The Disasters service (Disasters.h) for scripts: game:GetService("Disasters").
struct lua_State;
namespace LuaDisasters {
void install(lua_State* L, bool server);
}
