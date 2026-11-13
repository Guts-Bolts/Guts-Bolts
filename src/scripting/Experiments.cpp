// Experiments and analytics for games: the game side of worker/experiments.js.
//
//   ExperimentService:GetVariantAsync(player, "Shop")   -> "Old" / "New" (or nil: no such running experiment)
//   ExperimentService:GetExperimentsAsync()             -> { "Shop", ... } the running ones
//   ExperimentService:SetTestVariant("Shop", "New")     -- while testing: everyone gets this version
//                                                          (only in this run; never saved or sent)
//   AnalyticsService:LogCustomEvent(player, "BoughtSword" [, value])
//   AnalyticsService:LogEconomyEvent(player, Enum.AnalyticsEconomyFlowType.Sink, "Coins", 50, ...)
//   AnalyticsService:LogFunnelStepEvent(player, "Tutorial", sessionId, 3 [, "Jumped"])
//   AnalyticsService:LogOnboardingFunnelStepEvent(player, 3 [, "Jumped"])
//
// Only the game's server sends anything (a Script, or Studio's test run), and only while one of
// the game's experiments is running, so games without experiments send nothing at all. Each
// player's join and minutes played are counted by themselves. Events go in batches (every 15
// seconds, or 200 at a time).
#include "Experiments.h"
#include "LuaApi.h"
#include "ScriptEngine.h"
#include "../core/Log.h"
#include "../online/OnlineClient.h"

#include <nlohmann/json.hpp>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace Experiments {

int variantOf(const std::string& experimentId, long long userId, int count) {
    if (count <= 0) return 0;
    uint32_t h = 0x811c9dc5u;
    const std::string s = experimentId + ":" + std::to_string(userId);
    for (unsigned char c : s) { h ^= c; h *= 0x01000193u; }
    return (int)(h % (uint32_t)count);
}

namespace {

// What one running game knows: the experiments (once the server answered) and the events
// waiting to be sent.
struct State {
    bool asked = false, loaded = false;
    std::string game;
    nlohmann::json list = nlohmann::json::array();
    std::vector<nlohmann::json> queue;
};
std::map<lua_State*, std::shared_ptr<State>>& states() {
    static std::map<lua_State*, std::shared_ptr<State>> m;
    return m;
}
std::shared_ptr<State> stateOf(lua_State* L) {
    auto& m = states();
    auto it = m.find(L);
    if (it == m.end()) it = m.emplace(L, std::make_shared<State>()).first;
    return it->second;
}

// fetch(): ask the server once which experiments are running (a game that isn't published, or
// isn't online, has none).
int l_fetch(lua_State* L) {
    auto st = stateOf(L);
    if (st->asked) return 0;
    st->asked = true;
    st->game = LuaApi::engine(L)->onlineGameId();
    if (st->game.empty() || st->game.rfind("local:", 0) == 0 || !Online::online()) { st->loaded = true; return 0; }
    std::weak_ptr<State> weak = st;
    Online::request("experiments.forGame", {{"game", st->game}}, [weak](const nlohmann::json& r) {
        auto s = weak.lock();
        if (!s) return;
        if (r.value("ok", false) && r.contains("experiments") && r["experiments"].is_array()) s->list = r["experiments"];
        s->loaded = true;
    });
    return 0;
}

// list() -> nil while waiting, else { {id = , name = , variants = {...}}, ... }
int l_list(lua_State* L) {
    auto st = stateOf(L);
    if (!st->loaded) return 0;
    lua_newtable(L);
    int i = 0;
    for (const auto& e : st->list) {
        if (!e.is_object()) continue;
        lua_newtable(L);
        lua_pushstring(L, e.value("id", std::string()).c_str());
        lua_setfield(L, -2, "id");
        lua_pushstring(L, e.value("name", std::string()).c_str());
        lua_setfield(L, -2, "name");
        lua_newtable(L);
        int k = 0;
        if (e.contains("variants") && e["variants"].is_array())
            for (const auto& v : e["variants"])
                if (v.is_string()) { lua_pushstring(L, v.get<std::string>().c_str()); lua_rawseti(L, -2, ++k); }
        lua_setfield(L, -2, "variants");
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// variant(id, userId, count) -> 1-based
int l_variant(lua_State* L) {
    const std::string id = luaL_checkstring(L, 1);
    const long long user = (long long)luaL_checknumber(L, 2);
    const int count = (int)luaL_checkinteger(L, 3);
    lua_pushinteger(L, variantOf(id, user, count) + 1);
    return 1;
}

void send(lua_State* L, State& st) {
    if (st.queue.empty()) return;
    if (st.game.empty() || !Online::online()) { st.queue.clear(); return; }
    nlohmann::json events = nlohmann::json::array();
    for (auto& e : st.queue) events.push_back(std::move(e));
    st.queue.clear();
    Online::request("analytics.log", {{"game", st.game}, {"events", std::move(events)}}, [](const nlohmann::json& r) {
        if (!r.value("ok", false)) Log::warn("AnalyticsService: " + r.value("error", std::string("couldn't send the game's events")));
    });
    (void)L;
}

// log(userId, name, value): one event (sent with the next batch).
int l_log(lua_State* L) {
    auto st = stateOf(L);
    const long long user = (long long)luaL_checknumber(L, 1);
    const std::string name = luaL_checkstring(L, 2);
    const double value = luaL_optnumber(L, 3, 1.0);
    if (user <= 0 || name.empty() || st->list.empty()) return 0;   // nothing running: nothing to count
    st->queue.push_back({{"player", user}, {"name", name.substr(0, 60)}, {"value", value}});
    if (st->queue.size() >= 200) send(L, *st);
    return 0;
}

int l_flush(lua_State* L) {
    send(L, *stateOf(L));
    return 0;
}

// queued() -> how many events wait to be sent (tests)
int l_queued(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)stateOf(L)->queue.size());
    return 1;
}

const char* kLua = R"LUA(
local fetch, list, variantOf, logEvent, flush, queued = ...
local function isServer() return RunService == nil or RunService:IsServer() end

local forced = {}
local function userIdOf(player)
    if type(player) == "number" then return player end
    if typeof(player) == "Instance" and player:IsA("Player") then return player.UserId end
    return nil
end
local function running()
    fetch()
    local l, waited = list(), 0
    while l == nil and waited < 10 do
        task.wait(0.1)
        waited = waited + 0.1
        l = list()
    end
    return l or {}
end

local ExperimentService = { Name = "ExperimentService", ClassName = "ExperimentService" }
-- Which version of the experiment `name` this player gets (always the same one for them).
function ExperimentService:GetVariantAsync(player, name)
    if type(name) ~= "string" then error("GetVariantAsync(player, experimentName): the name should be text", 2) end
    if forced[name] then return forced[name] end
    local uid = userIdOf(player)
    if uid == nil then error("GetVariantAsync needs a player", 2) end
    for _, e in ipairs(running()) do
        if e.name == name and #e.variants > 0 then return e.variants[variantOf(e.id, uid, #e.variants)] end
    end
    return nil
end
function ExperimentService:GetExperimentsAsync()
    local names = {}
    for _, e in ipairs(running()) do names[#names + 1] = e.name end
    return names
end
-- Testing: everyone gets this version of `name` in this run (nil puts it back).
function ExperimentService:SetTestVariant(name, variant)
    if type(name) ~= "string" then error("SetTestVariant(experimentName, version)", 2) end
    forced[name] = variant
end
function ExperimentService:IsA(c) return c == "ExperimentService" or c == "Instance" end
setmetatable(ExperimentService, { __tostring = function() return "ExperimentService" end })

local AnalyticsService = { Name = "AnalyticsService", ClassName = "AnalyticsService" }
local function log(what, player, name, value)
    if not isServer() then return end   -- (the game's server counts; players' computers don't)
    local uid = userIdOf(player)
    if uid == nil then error(what .. " needs a player first", 3) end
    if type(name) ~= "string" or name == "" then error(what .. ": the event needs a name", 3) end
    if value ~= nil and type(value) ~= "number" then error(what .. ": the value should be a number", 3) end
    logEvent(uid, name, value or 1)
end
function AnalyticsService:LogCustomEvent(player, eventName, value)
    log("LogCustomEvent", player, eventName, value)
end
function AnalyticsService:LogEconomyEvent(player, flowType, currencyType, amount)
    local flow = tostring(flowType)
    local word = flow:find("Sink") and "spent:" or flow:find("Source") and "earned:" or nil
    if not word then error("LogEconomyEvent: the flow type should be Enum.AnalyticsEconomyFlowType.Sink or .Source", 2) end
    if type(currencyType) ~= "string" then error("LogEconomyEvent: say which currency, like \"Coins\"", 2) end
    log("LogEconomyEvent", player, word .. currencyType, tonumber(amount) or 0)
end
function AnalyticsService:LogFunnelStepEvent(player, funnelName, _, step)
    if type(funnelName) ~= "string" then error("LogFunnelStepEvent: the funnel needs a name", 2) end
    if type(step) ~= "number" then error("LogFunnelStepEvent: the step should be a number", 2) end
    log("LogFunnelStepEvent", player, "funnel:" .. funnelName .. ":" .. math.floor(step), 1)
end
function AnalyticsService:LogOnboardingFunnelStepEvent(player, step)
    if type(step) ~= "number" then error("LogOnboardingFunnelStepEvent: the step should be a number", 2) end
    log("LogOnboardingFunnelStepEvent", player, "funnel:Onboarding:" .. math.floor(step), 1)
end
-- Roblox's progression events: kept quiet (nothing in experiments counts them yet).
for _, n in ipairs({ "LogProgressionEvent", "LogProgressionStartEvent", "LogProgressionCompleteEvent", "LogProgressionFailEvent" }) do
    AnalyticsService[n] = function() end
end
function AnalyticsService:IsA(c) return c == "AnalyticsService" or c == "Instance" end
setmetatable(AnalyticsService, { __tostring = function() return "AnalyticsService" end })

-- Joins, minutes played, and sending in batches (only on the server, only with experiments running).
local joinedAt = {}
local function joined(p)
    joinedAt[p] = os.clock()
    logEvent(p.UserId, "__join", 1)
end
task.spawn(function()
    if not isServer() then return end
    if #running() == 0 then return end
    for _, p in ipairs(Players:GetPlayers()) do joined(p) end
    Players.PlayerAdded:Connect(joined)
    Players.PlayerRemoving:Connect(function(p)
        if joinedAt[p] then logEvent(p.UserId, "__minutes", (os.clock() - joinedAt[p]) / 60) end
        joinedAt[p] = nil
        flush()
    end)
    local since = 0
    RunService.Heartbeat:Connect(function(dt)
        since = since + dt
        if since >= 15 then since = 0 flush() end
    end)
end)

return ExperimentService, AnalyticsService, queued
)LUA";

} // namespace

void install(lua_State* L) {
    states().erase(L);   // (a fresh start for this game run)
    if (luaL_loadbuffer(L, kLua, std::strlen(kLua), "=experiments") != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    lua_pushcfunction(L, l_fetch);
    lua_pushcfunction(L, l_list);
    lua_pushcfunction(L, l_variant);
    lua_pushcfunction(L, l_log);
    lua_pushcfunction(L, l_flush);
    lua_pushcfunction(L, l_queued);
    if (lua_pcall(L, 6, 3, 0) != LUA_OK) {
        Log::error(std::string("Internal script setup failed: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    // game:GetService("ExperimentService") / ("AnalyticsService") find them through the prelude's
    // __gb_moreServices table.
    lua_getglobal(L, "__gb_moreServices");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -4);
        lua_setfield(L, -2, "ExperimentService");
        lua_pushvalue(L, -3);
        lua_setfield(L, -2, "AnalyticsService");
    }
    lua_pop(L, 1);
    lua_setglobal(L, "__gb_analyticsQueued");   // (tests)
    lua_pop(L, 2);
}

} // namespace Experiments
