#pragma once
// Experiments (A/B tests) and real analytics for games (Experiments.cpp):
//
//   local version = game:GetService("ExperimentService"):GetVariantAsync(player, "Shop")   -- "Old" / "New"
//   game:GetService("AnalyticsService"):LogCustomEvent(player, "BoughtSword", 1)
//
// The creator makes the experiment on the game's Configure page; the website keeps count
// (worker/experiments.js) and shows each version side by side.
#include <string>
#include <cstdint>

struct lua_State;

namespace Experiments {

// The version a player gets: FNV-1a of "<experiment id>:<user number>" (0-based), the same sum
// as worker/experiments.js variantOf and src/server.
int variantOf(const std::string& experimentId, long long userId, int count);

// Adds AnalyticsService (for real now) and ExperimentService. Runs after the prelude.
void install(lua_State* L);

} // namespace Experiments
