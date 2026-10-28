#pragma once
#include <string>
#include <vector>

// Roblox scripts are written in Luau, a Lua dialect with a few extras. This
// rewrites the common ones into plain Lua 5.4 so imported scripts run:
//   x += 1            ->  x = x + (1)        (also -= *= /= //= %= ^= ..=)
//   continue          ->  goto to the end of the loop
//   local n: number   ->  local n            (type annotations and casts)
//   type Foo = ...    ->  (removed)
//   `Hi {name}!`      ->  ("Hi " .. tostring(name) .. "!")
// `notes` (optional) collects what was changed or couldn't be converted.
// Every line stays on the same line number, so errors point at the right place.
struct lua_State;
namespace Luau {
std::string toLua(const std::string& source, std::vector<std::string>* notes = nullptr);
// Compiles a script (like luaL_loadbufferx, same results): plain Lua exactly as written;
// if that doesn't compile, its Luau rewrite (so += and continue work in any script,
// not just imported ones). On an error the message is on the stack, as usual.
// Text only unless `mode` says otherwise (never "b" for code from games or apps: bytecode isn't checked).
int load(lua_State* L, const std::string& source, const char* chunkName, const char* mode = nullptr);
}
