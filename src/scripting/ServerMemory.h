#pragma once
// Server memory warnings for scripts, like Roblox's DataModel.ServerLowMemoryWarning:
//   game.ServerLowMemoryWarning:Connect(function(usedMb, limitMb) ... end)
//       fires on the server when the game uses kWarnShare of its memory limit or more, at most
//       once every kWarnEvery seconds while it stays high. Clean up then (old bullets, debris,
//       cached maps) so the server doesn't run out and drop everyone.
//   game:GetService("Stats"):GetTotalMemoryUsageMb()    memory this game's program uses now (MB)
//   game:GetService("Stats"):GetServerMemoryLimitMb()   the limit the warning measures against (MB)
// The limit: GB_SERVER_MEMORY_MB if it's set, or half of the computer's memory (4 GB if it can't tell).
struct lua_State;
namespace ServerMemory {
constexpr double kWarnShare = 0.75;   // warn at 75% of the limit (Roblox: around 70-80%)
constexpr double kWarnEvery = 60.0;   // seconds between warnings while memory stays high
constexpr double kCheckEvery = 5.0;   // seconds between checks

double usedMb();    // this program's memory now, in MB (0 if the system can't say)
double limitMb();   // the limit above, in MB
void install(lua_State* L, bool server);   // adds the event and the Stats functions
void step(lua_State* L, double now);      // every frame: checks every kCheckEvery seconds (server only)
void pretend(double usedMb, double limitMb);   // tests: pretend numbers (0, 0 = real ones again)
}
