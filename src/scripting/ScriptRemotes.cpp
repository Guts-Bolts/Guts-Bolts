// RemoteEvents and RemoteFunctions: how a LocalScript (running on a player's own
// computer) and a Script (running on the server) talk to each other.
//
//   remote:FireServer(...)              player -> server   (OnServerEvent gets player, ...)
//   remote:FireClient(player, ...)      server -> a player (OnClientEvent gets ...)
//   remote:FireAllClients(...)          server -> everyone
//   remoteFunction:InvokeServer(...)    player asks, the server's OnServerInvoke answers
//
// Values travel as JSON. Objects, players, Vector3s and the like get a small tag so
// they come back as the same kind of thing on the other side.
#include "ScriptEngine.h"
#include "LuaApi.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Log.h"

#include <cmath>
#include <string>

using nlohmann::json;

namespace {

constexpr int    kMaxDepth = 20;
constexpr size_t kMaxBytes = 64 * 1024;   // per message, like Roblox's limits keep things small

} // namespace

struct RemoteLua {
    static ScriptEngine* E(lua_State* L) { return LuaApi::engine(L); }

    // ---- Lua value -> JSON ----------------------------------------------------
    static json encode(lua_State* L, int idx, int depth) {
        idx = lua_absindex(L, idx);
        if (depth > kMaxDepth) luaL_error(L, "Can't send that: tables are nested too deep");
        switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN: return lua_toboolean(L, idx) != 0;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx)) return (int64_t)lua_tointeger(L, idx);
            { double d = lua_tonumber(L, idx); return std::isfinite(d) ? json(d) : json(nullptr); }
        case LUA_TSTRING: { size_t n; const char* s = lua_tolstring(L, idx, &n); return std::string(s, n); }
        case LUA_TUSERDATA: {
            if (uint64_t id = LuaApi::toInstanceId(L, idx)) {
                ScriptEngine* e = E(L);
                if (!e->resolve(id)) return nullptr;            // destroyed: arrives as nil, like Roblox
                if (e->idToServer) id = e->idToServer(id);
                return json{{"$i", id}};
            }
            if (glm::mat4* m = LuaApi::toCFrame(L, idx)) {
                json a = json::array();
                for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) a.push_back((*m)[c][r]);
                return json{{"$cf", a}};
            }
            if (glm::vec3* v = LuaApi::toVector3(L, idx)) return json{{"$v3", {v->x, v->y, v->z}}};
            if (glm::vec3* c = LuaApi::toColor3(L, idx))  return json{{"$c3", {c->x, c->y, c->z}}};
            if (glm::vec2* v = LuaApi::toVector2(L, idx)) return json{{"$v2", {v->x, v->y}}};
            if (UDim2* u = LuaApi::toUDim2(L, idx))       return json{{"$u2", {u->xs, u->xo, u->ys, u->yo}}};
            return nullptr;
        }
        case LUA_TTABLE: {
            // A player stands for itself.
            lua_pushstring(L, "__node");
            lua_rawget(L, idx);
            bool isPlayer = LuaApi::toInstanceId(L, -1) != 0;
            lua_pop(L, 1);
            if (isPlayer) {
                lua_pushstring(L, "UserId"); lua_rawget(L, idx);
                lua_pushstring(L, "Name");   lua_rawget(L, idx);
                json p{{"$p", (int64_t)lua_tointeger(L, -2)},
                       {"n", lua_isstring(L, -1) ? lua_tostring(L, -1) : ""}};
                lua_pop(L, 2);
                return p;
            }
            // 1..n with nothing else: an array. Anything else: a list of key/value pairs.
            lua_Integer n = (lua_Integer)lua_rawlen(L, idx);
            lua_Integer count = 0;
            lua_pushnil(L);
            while (lua_next(L, idx)) { ++count; lua_pop(L, 1); }
            if (count == n) {
                json a = json::array();
                for (lua_Integer i = 1; i <= n; ++i) {
                    lua_rawgeti(L, idx, i);
                    a.push_back(encode(L, -1, depth + 1));
                    lua_pop(L, 1);
                }
                return a;
            }
            json pairs = json::array();
            lua_pushnil(L);
            while (lua_next(L, idx)) {
                pairs.push_back(json::array({encode(L, -2, depth + 1), encode(L, -1, depth + 1)}));
                lua_pop(L, 1);
            }
            return json{{"$d", pairs}};
        }
        default: return nullptr;   // functions and threads can't be sent (they arrive as nil)
        }
    }

    // Arguments first..top as a JSON array, or a Lua error if it's too much.
    static json encodeArgs(lua_State* L, int first) {
        json a = json::array();
        for (int i = first; i <= lua_gettop(L); ++i) a.push_back(encode(L, i, 0));
        if (a.dump().size() > kMaxBytes) luaL_error(L, "Can't send that: it's more than 64 KB");
        return a;
    }

    // ---- JSON -> Lua value ----------------------------------------------------
    static void pushPlayer(lua_State* L, int64_t uid, const std::string& name) {
        lua_getfield(L, LUA_REGISTRYINDEX, "GB.playerById");
        if (!lua_isfunction(L, -1)) { lua_pop(L, 1); lua_pushnil(L); return; }
        lua_pushinteger(L, uid);
        lua_pushstring(L, name.c_str());
        if (lua_pcall(L, 2, 1, 0) != LUA_OK) { lua_pop(L, 1); lua_pushnil(L); }
    }

    static float num(const json& a, size_t i) {
        return a.is_array() && i < a.size() && a[i].is_number() ? a[i].get<float>() : 0.0f;
    }

    static void decode(lua_State* L, const json& j, int depth) {
        luaL_checkstack(L, 4, "remote data");
        if (depth > kMaxDepth) { lua_pushnil(L); return; }
        if (j.is_boolean()) { lua_pushboolean(L, j.get<bool>()); return; }
        if (j.is_number_integer()) { lua_pushinteger(L, (lua_Integer)j.get<int64_t>()); return; }
        if (j.is_number()) { lua_pushnumber(L, j.get<double>()); return; }
        if (j.is_string()) { const auto& s = j.get_ref<const std::string&>(); lua_pushlstring(L, s.data(), s.size()); return; }
        if (j.is_array()) {
            lua_createtable(L, (int)j.size(), 0);
            for (size_t i = 0; i < j.size(); ++i) {
                decode(L, j[i], depth + 1);
                lua_rawseti(L, -2, (lua_Integer)i + 1);
            }
            return;
        }
        if (!j.is_object()) { lua_pushnil(L); return; }
        if (j.contains("$i")) {
            uint64_t id = j["$i"].is_number_unsigned() || j["$i"].is_number_integer() ? j["$i"].get<uint64_t>() : 0;
            ScriptEngine* e = E(L);
            if (e->idFromServer) id = e->idFromServer(id);
            if (id && e->resolve(id)) LuaApi::pushInstance(L, id);
            else lua_pushnil(L);
            return;
        }
        if (j.contains("$p")) {
            pushPlayer(L, j["$p"].is_number() ? j["$p"].get<int64_t>() : 0,
                       j.value("n", std::string()));
            return;
        }
        if (j.contains("$v3")) { const json& a = j["$v3"]; LuaApi::pushVector3(L, {num(a, 0), num(a, 1), num(a, 2)}); return; }
        if (j.contains("$c3")) { const json& a = j["$c3"]; LuaApi::pushColor3(L, {num(a, 0), num(a, 1), num(a, 2)}); return; }
        if (j.contains("$v2")) { const json& a = j["$v2"]; LuaApi::pushVector2(L, {num(a, 0), num(a, 1)}); return; }
        if (j.contains("$u2")) {
            const json& a = j["$u2"];
            UDim2 u; u.xs = num(a, 0); u.xo = num(a, 1); u.ys = num(a, 2); u.yo = num(a, 3);
            LuaApi::pushUDim2(L, u);
            return;
        }
        if (j.contains("$cf")) {
            const json& a = j["$cf"];
            glm::mat4 m(1.0f);
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) m[c][r] = num(a, (size_t)(c * 4 + r));
            LuaApi::pushCFrame(L, m);
            return;
        }
        if (j.contains("$d") && j["$d"].is_array()) {
            lua_newtable(L);
            for (const json& kv : j["$d"]) {
                if (!kv.is_array() || kv.size() != 2 || kv[0].is_null()) continue;
                decode(L, kv[0], depth + 1);
                if (lua_isnil(L, -1)) { lua_pop(L, 1); continue; }   // e.g. a key object that's gone
                decode(L, kv[1], depth + 1);
                lua_rawset(L, -3);
            }
            return;
        }
        lua_pushnil(L);
    }

    // A packed table of arguments ({n = count, ...}) kept in the registry, so firing
    // can copy it onto each listener's thread.
    static std::function<int(lua_State*)> unpacker(int ref) {
        return [ref](lua_State* co) {
            lua_rawgeti(co, LUA_REGISTRYINDEX, ref);
            lua_getfield(co, -1, "n");
            int n = (int)lua_tointeger(co, -1);
            lua_pop(co, 1);
            luaL_checkstack(co, n + 1, "remote arguments");
            for (int i = 1; i <= n; ++i) lua_rawgeti(co, -i, i);
            lua_remove(co, -(n + 1));
            return n;
        };
    }

    static void fireWithTable(ScriptEngine* e, SignalKind kind, uint64_t id, lua_State* L) {
        // The table is on top of L.
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        e->fire(kind, id, unpacker(ref));
        if (e->m_L) luaL_unref(e->m_L, LUA_REGISTRYINDEX, ref);
    }

    static SceneNode* checkRemote(lua_State* L, int idx) {
        SceneNode* n = LuaApi::checkNode(L, idx);
        if (!n->isRemote()) luaL_argerror(L, idx, "expected a RemoteEvent or RemoteFunction");
        return n;
    }

    // ---- Lua functions --------------------------------------------------------

    // __gb_remoteMode() -> "All" | "Server" | "Client"
    static int mode(lua_State* L) {
        auto m = E(L)->m_runMode;
        lua_pushstring(L, m == ScriptEngine::RunMode::Server ? "Server"
                        : m == ScriptEngine::RunMode::Client ? "Client" : "All");
        return 1;
    }

    // __gb_remoteSend(remote, to, ...): to = a UserId, 0 = every player, -1 = the server.
    static int send(lua_State* L) {
        SceneNode* r = checkRemote(L, 1);
        int to = (int)luaL_checkinteger(L, 2);
        ScriptEngine* e = E(L);
        uint64_t id = e->idToServer ? e->idToServer(r->id) : r->id;
        json args = encodeArgs(L, 3);   // (checked even when there's nobody to send to)
        if (e->m_networked) e->m_remoteOut.push_back({to, json{{"t", "remote"}, {"id", id}, {"a", std::move(args)}}});
        return 0;
    }

    // __gb_remoteFire(remote, "Server"|"Client", ...): fire on this computer (playing solo
    // or as the host). Values still go through JSON, so tables are copies like they'd be
    // over the network.
    static int fireHere(lua_State* L) {
        SceneNode* r = checkRemote(L, 1);
        SignalKind kind = std::string(luaL_checkstring(L, 2)) == "Server" ? SignalKind::RemoteServer
                                                                         : SignalKind::RemoteClient;
        int n = lua_gettop(L) - 2;
        lua_createtable(L, n, 1);
        int t = lua_gettop(L);
        for (int i = 0; i < n; ++i) {
            // The player (first argument to OnServerEvent) stays the same table.
            if (kind == SignalKind::RemoteServer && i == 0) lua_pushvalue(L, 3);
            else decode(L, encode(L, 3 + i, 0), 0);
            lua_rawseti(L, t, i + 1);
        }
        lua_pushinteger(L, n);
        lua_setfield(L, t, "n");
        fireWithTable(E(L), kind, r->id, L);
        return 0;
    }

    // __gb_invokeStart(remote, ...) -> call number. Asks the server; the answer is
    // picked up with __gb_invokeDone.
    static int invokeStart(lua_State* L) {
        SceneNode* r = checkRemote(L, 1);
        ScriptEngine* e = E(L);
        int call = e->m_nextCall++;
        uint64_t id = e->idToServer ? e->idToServer(r->id) : r->id;
        e->m_remoteOut.push_back({-1, json{{"t", "invoke"}, {"id", id}, {"call", call}, {"a", encodeArgs(L, 2)}}});
        lua_pushinteger(L, call);
        return 1;
    }

    // __gb_invokeDone(call) -> nil while waiting, else ok, results... (or false, error)
    static int invokeDone(lua_State* L) {
        ScriptEngine* e = E(L);
        auto it = e->m_invokeResults.find((int)luaL_checkinteger(L, 1));
        if (it == e->m_invokeResults.end()) return 0;
        json msg = std::move(it->second);
        e->m_invokeResults.erase(it);
        bool ok = msg.value("ok", false);
        lua_pushboolean(L, ok);
        if (!ok) { lua_pushstring(L, msg.value("err", std::string("The server's OnServerInvoke failed")).c_str()); return 2; }
        const json& res = msg.contains("r") && msg["r"].is_array() ? msg["r"] : json::array();
        luaL_checkstack(L, (int)res.size() + 2, "results");
        for (const json& v : res) decode(L, v, 0);
        return 1 + (int)res.size();
    }

    // __gb_takeInvokes() -> { {remote, player, call, args = {n=..., ...}}, ... }
    static int takeInvokes(lua_State* L) {
        ScriptEngine* e = E(L);
        auto list = std::move(e->m_invokesIn);
        e->m_invokesIn.clear();
        lua_createtable(L, (int)list.size(), 0);
        int out = lua_gettop(L);
        int k = 0;
        for (auto& in : list) {
            lua_createtable(L, 0, 4);
            LuaApi::pushInstance(L, in.remote);   lua_setfield(L, -2, "remote");
            pushPlayer(L, in.from, "");             lua_setfield(L, -2, "player");
            lua_pushinteger(L, in.from);            lua_setfield(L, -2, "from");
            lua_pushinteger(L, in.call);            lua_setfield(L, -2, "call");
            decode(L, in.args, 0);
            lua_pushinteger(L, in.args.is_array() ? (lua_Integer)in.args.size() : 0);
            lua_setfield(L, -2, "n");
            lua_setfield(L, -2, "args");
            lua_rawseti(L, out, ++k);
        }
        return 1;
    }

    // __gb_invokeReply(from, call, ok, ...)
    static int invokeReply(lua_State* L) {
        ScriptEngine* e = E(L);
        int from = (int)luaL_checkinteger(L, 1);
        int call = (int)luaL_checkinteger(L, 2);
        bool ok = lua_toboolean(L, 3) != 0;
        json msg{{"t", "invoked"}, {"call", call}, {"ok", ok}};
        if (ok) {
            // Results that can't be sent become an error for the caller instead.
            int top = lua_gettop(L);
            lua_pushcfunction(L, [](lua_State* L) -> int {
                json* out = static_cast<json*>(lua_touserdata(L, 1));
                lua_remove(L, 1);
                *out = encodeArgs(L, 1);
                return 0;
            });
            json res;
            lua_pushlightuserdata(L, &res);
            for (int i = 4; i <= top; ++i) lua_pushvalue(L, i);
            if (lua_pcall(L, top - 2, 0, 0) != LUA_OK) {
                msg["ok"] = false;
                msg["err"] = lua_tostring(L, -1) ? lua_tostring(L, -1) : "bad result";
                lua_pop(L, 1);
            } else msg["r"] = std::move(res);
        } else {
            msg["err"] = lua_isstring(L, 4) ? lua_tostring(L, 4) : "OnServerInvoke failed";
        }
        if (e->m_networked) e->m_remoteOut.push_back({from, std::move(msg)});
        return 0;
    }
};

void ScriptEngine::openRemotes() {
    lua_State* L = m_L;
    lua_register(L, "__gb_remoteMode",   RemoteLua::mode);
    lua_register(L, "__gb_remoteSend",   RemoteLua::send);
    lua_register(L, "__gb_remoteFire",   RemoteLua::fireHere);
    lua_register(L, "__gb_invokeStart",  RemoteLua::invokeStart);
    lua_register(L, "__gb_invokeDone",   RemoteLua::invokeDone);
    lua_register(L, "__gb_takeInvokes",  RemoteLua::takeInvokes);
    lua_register(L, "__gb_invokeReply",  RemoteLua::invokeReply);
    lua_pushinteger(L, m_localUserId);
    lua_setglobal(L, "__gb_localUserId");
    // remoteFunction.OnServerInvoke, keyed by the object (the prelude reads it too).
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "__gb_invokers");
    lua_setfield(L, LUA_REGISTRYINDEX, "GB.invokers");
}

bool ScriptEngine::wantsScript(const SceneNode* script) const {
    if (!script || script->isModule) return true;          // modules run wherever require() is
    if (!script->isLocal) return m_runMode != RunMode::Client;
    if (m_runMode == RunMode::Server) return false;
    // A LocalScript belongs to whoever's character or backpack it's in: not someone
    // else's (on the host, a joined player's tools carry LocalScripts that are theirs).
    for (const SceneNode* p = script->parent; p; p = p->parent)
        if (m_scene && m_scene->findRemote(p->id)) return false;
    return true;
}

void ScriptEngine::remoteIn(const json& msg, int fromUserId) {
    if (!m_L || !msg.is_object()) return;
    const std::string t = msg.value("t", std::string());
    lua_State* L = m_L;
    if (t == "invoked") {
        if (m_runMode != RunMode::Client) return;
        m_invokeResults[msg.value("call", 0)] = msg;
        return;
    }
    uint64_t id = msg.contains("id") && msg["id"].is_number() ? msg["id"].get<uint64_t>() : 0;
    if (m_runMode == RunMode::Client && idFromServer) id = idFromServer(id);
    SceneNode* r = resolve(id);
    if (!r || !r->isRemote()) return;
    const json& args = msg.contains("a") && msg["a"].is_array() ? msg["a"] : json::array();

    if (t == "invoke") {
        if (m_runMode == RunMode::Client || !r->remoteFunction) return;
        m_invokesIn.push_back({id, fromUserId, msg.value("call", 0), args});
        return;
    }
    if (t != "remote" || r->remoteFunction) return;
    bool toServer = m_runMode != RunMode::Client;
    int top = lua_gettop(L);
    int n = (int)args.size() + (toServer ? 1 : 0);
    lua_createtable(L, n, 1);
    int i = 0;
    if (toServer) { RemoteLua::pushPlayer(L, fromUserId, ""); lua_rawseti(L, -2, ++i); }
    for (const json& v : args) { RemoteLua::decode(L, v, 0); lua_rawseti(L, -2, ++i); }
    lua_pushinteger(L, n);
    lua_setfield(L, -2, "n");
    RemoteLua::fireWithTable(this, toServer ? SignalKind::RemoteServer : SignalKind::RemoteClient, id, L);
    if (m_L) lua_settop(m_L, top);
}

void ScriptEngine::firePrompt(SignalKind kind, uint64_t id, int userId) {
    if (!m_L) return;
    lua_State* L = m_L;
    // PromptShown / PromptHidden happen on the computer the card shows on: no player.
    if (kind == SignalKind::PromptShown || kind == SignalKind::PromptHidden) {
        const bool shown = kind == SignalKind::PromptShown;
        fire(kind, id, [shown](lua_State* co) {
            if (!shown) return 0;
            lua_pushstring(co, "Keyboard");
            return 1;
        });
        fire(kind, 0, [id, shown](lua_State* co) {   // ProximityPromptService.PromptShown(prompt, inputType)
            LuaApi::pushInstance(co, id);
            if (!shown) return 1;
            lua_pushstring(co, "Keyboard");
            return 2;
        });
        return;
    }
    RemoteLua::pushPlayer(L, userId ? userId : m_localUserId, "");
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    fire(kind, id, [ref](lua_State* co) { lua_rawgeti(co, LUA_REGISTRYINDEX, ref); return 1; });
    fire(kind, 0, [ref, id](lua_State* co) {   // ProximityPromptService.PromptTriggered(prompt, player)
        LuaApi::pushInstance(co, id);
        lua_rawgeti(co, LUA_REGISTRYINDEX, ref);
        return 2;
    });
    if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}

void ScriptEngine::fireClickDetector(SignalKind kind, uint64_t id, int userId) {
    if (!m_L) return;
    lua_State* L = m_L;
    RemoteLua::pushPlayer(L, userId ? userId : m_localUserId, "");
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    fire(kind, id, [ref](lua_State* co) { lua_rawgeti(co, LUA_REGISTRYINDEX, ref); return 1; });
    if (m_L) luaL_unref(m_L, LUA_REGISTRYINDEX, ref);
}
