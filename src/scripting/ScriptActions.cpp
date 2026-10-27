// ContextActionService: key bindings for LocalScripts, like Roblox's.
//
//   ContextActionService:BindAction("Sprint", function(name, state, input)
//       if state == Enum.UserInputState.Begin then ... end
//       return Enum.ContextActionResult.Sink     -- or Pass: let others have it too
//   end, true, Enum.KeyCode.LeftShift, Enum.KeyCode.ButtonL3)
//
// Every key, controller button, mouse press and finger in the game view comes through
// routeInput() first. The actions bound to it are asked newest / highest priority first;
// the first one that doesn't return Pass sinks it: the actions below it, the game's own
// controls (walking, jumping, tools: GameSession asks inputSunk) don't get it, and
// UserInputService's events say gameProcessedEvent = true. An action asked for its touch
// button (createTouchButton) gets a round button on touch screens (TouchControls draws
// them next to the jump button). The Lua side is in the prelude (ScriptEngine.cpp).
#include "ScriptEngine.h"
#include "LuaApi.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../core/Log.h"

#include <imgui.h>
#include <algorithm>

bool ScriptEngine::s_testHooks = false;

namespace {
constexpr int kDefaultPriority = 2000;   // Enum.ContextActionPriority.Default (= Medium)

bool knownInputType(const std::string& s) {
    return s == "Keyboard" || s == "Gamepad1" || s == "MouseButton1" || s == "Touch";
}
} // namespace

struct ActionLua {
    static ScriptEngine* E(lua_State* L) { return LuaApi::engine(L); }

    // __gb_casBind(name, fn, createTouchButton, priority, {inputs})
    static int bind(lua_State* L) {
        ScriptEngine* e = E(L);
        std::string name = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TFUNCTION);
        const bool touch = lua_toboolean(L, 3);
        const int priority = (int)luaL_optinteger(L, 4, kDefaultPriority);
        luaL_checktype(L, 5, LUA_TTABLE);
        if (e->m_runMode == ScriptEngine::RunMode::Server)
            return luaL_error(L, "ContextActionService is for LocalScripts (this is a game server)");
        if (SceneNode* s = e->resolve(e->m_current); s && s->isScript() && !s->isLocal && !s->isModule)
            return luaL_error(L, "ContextActionService:BindAction only works in a LocalScript");

        ScriptEngine::ActionBinding a;
        a.name = name;
        a.touchButton = touch;
        a.priority = priority;
        a.owner = e->m_current;
        a.button.name = name;
        a.button.title = "";
        const lua_Integer n = luaL_len(L, 5);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L, 5, i);
            if (lua_type(L, -1) != LUA_TSTRING)
                return luaL_error(L, "BindAction: input %d isn't an Enum.KeyCode or Enum.UserInputType", (int)i);
            std::string in = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (!LuaApi::keyFromName(in) && !knownInputType(in))
                Log::warn("ContextActionService: " + in + " can't be bound in Guts&Bolts yet (\"" + name + "\" won't hear it)");
            a.inputs.push_back(in);
        }
        e->unbindAction(name, false);   // binding a name again replaces it
        lua_pushvalue(L, 2);
        a.fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
        a.seq = ++e->m_actionSeq;
        e->m_actions.push_back(a);

        // Inputs held right now by actions this one outranks: they get Cancel and lose them.
        std::vector<std::pair<uint64_t, std::pair<std::string, std::string>>> cancel;
        for (auto& [key, held] : e->m_holders) {
            const bool matches = std::find(a.inputs.begin(), a.inputs.end(), held.keyCode) != a.inputs.end() ||
                                 std::find(a.inputs.begin(), a.inputs.end(), held.inputType) != a.inputs.end();
            if (!matches) continue;
            for (auto it = held.actions.begin(); it != held.actions.end();) {
                const ScriptEngine::ActionBinding* other = e->actionBySeq(*it);
                if (other && other->priority <= priority) {
                    cancel.push_back({*it, {held.keyCode, held.inputType}});
                    it = held.actions.erase(it);
                } else ++it;
            }
        }
        for (auto& [seq, in] : cancel) e->callAction(seq, "Cancel", in.first, in.second, glm::vec2(0.0f));
        return 0;
    }

    static int unbind(lua_State* L) { E(L)->unbindAction(luaL_checkstring(L, 1), false); return 0; }

    static int unbindAll(lua_State* L) {
        ScriptEngine* e = E(L);
        std::vector<std::string> names;
        for (const auto& a : e->m_actions) names.push_back(a.name);
        for (const auto& n : names) e->unbindAction(n, false);
        return 0;
    }

    // What GetBoundActionInfo gives back (Roblox's keys).
    static void pushInfo(lua_State* L, ScriptEngine* e, const ScriptEngine::ActionBinding& a) {
        lua_newtable(L);
        lua_newtable(L);
        for (size_t i = 0; i < a.inputs.size(); ++i) { lua_pushstring(L, a.inputs[i].c_str()); lua_rawseti(L, -2, (lua_Integer)i + 1); }
        lua_setfield(L, -2, "inputTypes");
        lua_pushinteger(L, a.priority);              lua_setfield(L, -2, "priorityLevel");
        lua_pushboolean(L, a.touchButton);           lua_setfield(L, -2, "createTouchButton");
        // stackOrder: 0 = the oldest binding (a newer one at the same priority wins).
        int order = 0;
        for (const auto& b : e->m_actions) if (b.seq < a.seq) ++order;
        lua_pushinteger(L, order);                   lua_setfield(L, -2, "stackOrder");
        if (a.touchButton) {
            lua_pushstring(L, a.button.title.c_str());       lua_setfield(L, -2, "title");
            lua_pushstring(L, a.button.description.c_str()); lua_setfield(L, -2, "description");
            lua_pushstring(L, a.button.image.c_str());       lua_setfield(L, -2, "image");
        }
    }

    static int info(lua_State* L) {
        ScriptEngine* e = E(L);
        if (const ScriptEngine::ActionBinding* a = e->findAction(luaL_checkstring(L, 1))) pushInfo(L, e, *a);
        else lua_newtable(L);
        return 1;
    }

    static int all(lua_State* L) {
        ScriptEngine* e = E(L);
        lua_newtable(L);
        for (const auto& a : e->m_actions) { pushInfo(L, e, a); lua_setfield(L, -2, a.name.c_str()); }
        return 1;
    }

    // __gb_casButton(name, property, value): the touch button's Title / Image / Description / Position.
    static int setButton(lua_State* L) {
        ScriptEngine* e = E(L);
        ScriptEngine::ActionBinding* a = e->findAction(luaL_checkstring(L, 1));
        const std::string what = luaL_checkstring(L, 2);
        if (!a) return 0;   // (like Roblox: nothing happens for an action that isn't bound)
        ScriptEngine::ActionButton& b = a->button;
        if (what == "Title")            b.title = luaL_tolstring(L, 3, nullptr);
        else if (what == "Description") b.description = luaL_tolstring(L, 3, nullptr);
        else if (what == "Image") {
            if (lua_type(L, 3) == LUA_TNUMBER) b.image = "rbxassetid://" + std::to_string((long long)lua_tointeger(L, 3));
            else b.image = luaL_checkstring(L, 3);
        } else if (what == "Position") {
            if (lua_isnil(L, 3)) { b.hasPosition = false; return 0; }
            const UDim2 u = LuaApi::checkUDim2(L, 3);
            b.hasPosition = true;
            b.xScale = u.xs; b.xOffset = u.xo; b.yScale = u.ys; b.yOffset = u.yo;
        } else return luaL_error(L, "the action button has no %s", what.c_str());
        return 0;
    }

    // __gb_casButtonGet(name, property) -> value (nil if there's no such touch button)
    static int getButton(lua_State* L) {
        ScriptEngine* e = E(L);
        const ScriptEngine::ActionBinding* a = e->findAction(luaL_checkstring(L, 1));
        const std::string what = luaL_checkstring(L, 2);
        if (!a || !a->touchButton) { lua_pushnil(L); return 1; }
        const ScriptEngine::ActionButton& b = a->button;
        if (what == "exists")           lua_pushboolean(L, 1);
        else if (what == "Title")       lua_pushstring(L, b.title.c_str());
        else if (what == "Description") lua_pushstring(L, b.description.c_str());
        else if (what == "Image")       lua_pushstring(L, b.image.c_str());
        else if (what == "Pressed")     lua_pushboolean(L, b.pressed);
        else if (what == "Position") {
            if (!b.hasPosition) lua_pushnil(L);
            else { UDim2 u; u.xs = b.xScale; u.xo = b.xOffset; u.ys = b.yScale; u.yo = b.yOffset; LuaApi::pushUDim2(L, u); }
        } else lua_pushnil(L);
        return 1;
    }

    // Tests only (--test-command): __gb_testInput("key", "Space", true) presses a key (it
    // arrives next frame, like a real one), ("click") clicks the game view, ("button",
    // name, down) presses an action's touch button, ("touch", "", down) a finger.
    static int testInput(lua_State* L) {
        ScriptEngine* e = E(L);
        ScriptEngine::TestInput t;
        t.kind = luaL_checkstring(L, 1);
        t.name = luaL_optstring(L, 2, "");
        t.down = lua_isnone(L, 3) ? true : lua_toboolean(L, 3);
        if (t.kind == "key" && !LuaApi::keyFromName(t.name)) return luaL_error(L, "no key called %s", t.name.c_str());
        e->m_testInputs.push_back(t);
        return 0;
    }
};

void ScriptEngine::openActions() {
    lua_State* L = m_L;
    lua_register(L, "__gb_casBind", ActionLua::bind);
    lua_register(L, "__gb_casUnbind", ActionLua::unbind);
    lua_register(L, "__gb_casUnbindAll", ActionLua::unbindAll);
    lua_register(L, "__gb_casInfo", ActionLua::info);
    lua_register(L, "__gb_casAll", ActionLua::all);
    lua_register(L, "__gb_casButton", ActionLua::setButton);
    lua_register(L, "__gb_casButtonGet", ActionLua::getButton);
    // (__gb_testInput itself is registered in ScriptEngine.cpp, which hands these kinds to
    // actionTestInput below: it also does ClickDetector hovers and clicks on a part.)
}

// __gb_testInput("key" / "button" / "touch" / "click" with no part, ...): see ActionLua::testInput.
int actionTestInput(lua_State* L) { return ActionLua::testInput(L); }

ScriptEngine::ActionBinding* ScriptEngine::findAction(const std::string& name) {
    for (auto& a : m_actions) if (a.name == name) return &a;
    return nullptr;
}

ScriptEngine::ActionBinding* ScriptEngine::actionBySeq(uint64_t seq) {
    for (auto& a : m_actions) if (a.seq == seq) return &a;
    return nullptr;
}

void ScriptEngine::unbindAction(const std::string& name, bool cancel) {
    auto it = std::find_if(m_actions.begin(), m_actions.end(), [&](const ActionBinding& a) { return a.name == name; });
    if (it == m_actions.end()) return;
    const uint64_t seq = it->seq;
    if (cancel) {
        for (auto& [key, held] : m_holders)
            if (std::find(held.actions.begin(), held.actions.end(), seq) != held.actions.end())
                callAction(seq, "Cancel", held.keyCode, held.inputType, glm::vec2(0.0f));
        it = std::find_if(m_actions.begin(), m_actions.end(), [&](const ActionBinding& a) { return a.seq == seq; });
        if (it == m_actions.end()) return;
    }
    if (m_L && it->fnRef != LUA_NOREF) luaL_unref(m_L, LUA_REGISTRYINDEX, it->fnRef);
    m_actions.erase(it);
    for (auto& [key, held] : m_holders)
        held.actions.erase(std::remove(held.actions.begin(), held.actions.end(), seq), held.actions.end());
    // (A key it sank stays sunk until it's let go: the character doesn't suddenly jump.)
}

void ScriptEngine::forgetActions(const std::unordered_set<uint64_t>& owners) {
    std::vector<std::string> names;
    for (const auto& a : m_actions) if (a.owner && owners.count(a.owner)) names.push_back(a.name);
    for (const auto& n : names) unbindAction(n, false);
}

std::string ScriptEngine::callAction(uint64_t seq, const char* state, const std::string& keyCode,
                                     const std::string& inputType, glm::vec2 position) {
    const ActionBinding* a = actionBySeq(seq);
    if (!a || !m_L) return "Pass";
    const std::string name = a->name;
    const uint64_t owner = a->owner;
    lua_State* co = lua_newthread(m_L);
    const int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);
    lua_rawgeti(co, LUA_REGISTRYINDEX, a->fnRef);
    lua_pushstring(co, name.c_str());
    lua_pushstring(co, state);
    lua_newtable(co);   // the InputObject
    lua_pushstring(co, keyCode.c_str());   lua_setfield(co, -2, "KeyCode");
    lua_pushstring(co, inputType.c_str()); lua_setfield(co, -2, "UserInputType");
    lua_pushstring(co, state);             lua_setfield(co, -2, "UserInputState");
    LuaApi::pushVector3(co, glm::vec3(position, 0.0f)); lua_setfield(co, -2, "Position");
    LuaApi::pushVector3(co, glm::vec3(0.0f));            lua_setfield(co, -2, "Delta");
    const uint64_t prev = m_current;
    m_current = owner;
    std::string result;
    resume(co, ref, 3, nullptr, &result);   // nil (or waiting, or an error) sinks it, like Roblox
    m_current = prev;
    return result;
}

bool ScriptEngine::routeInput(const std::string& key, const std::string& keyCode, const std::string& inputType,
                              const char* state, glm::vec2 position) {
    if (!m_L) return false;
    const std::string st = state;
    if (st == "End") {
        // The actions that got its Begin hear it ended (newest / highest first).
        const bool sunk = m_sunk.erase(key) > 0;
        auto it = m_holders.find(key);
        if (it == m_holders.end()) return sunk;
        const std::vector<uint64_t> got = it->second.actions;
        m_holders.erase(it);
        for (uint64_t seq : got) callAction(seq, "End", keyCode, inputType, position);
        return sunk;
    }
    // Begin / Change: down the bindings until one sinks it.
    std::vector<std::pair<int, uint64_t>> chain;
    for (const auto& a : m_actions)
        if (std::find(a.inputs.begin(), a.inputs.end(), keyCode) != a.inputs.end() ||
            std::find(a.inputs.begin(), a.inputs.end(), inputType) != a.inputs.end())
            chain.push_back({a.priority, a.seq});
    std::sort(chain.begin(), chain.end(), [](const auto& x, const auto& y) { return x > y; });
    bool sunk = false;
    std::vector<uint64_t> got;
    for (const auto& [priority, seq] : chain) {
        if (!actionBySeq(seq)) continue;   // an earlier one unbound it
        got.push_back(seq);
        if (callAction(seq, state, keyCode, inputType, position) != "Pass") { sunk = true; break; }
    }
    if (st == "Begin") {
        if (!got.empty()) m_holders[key] = Held{keyCode, inputType, got};
        if (sunk) m_sunk.insert(key);
    }
    return sunk || (st == "Change" && m_sunk.count(key));
}

bool ScriptEngine::input(const std::string& key, const std::string& keyCode, const std::string& inputType,
                         const char* state, glm::vec2 position) {
    const bool sunk = routeInput(key, keyCode, inputType, state, position);
    const std::string st = state;
    if (st != "Begin" && st != "End") return sunk;
    fire(st == "Begin" ? SignalKind::InputBegan : SignalKind::InputEnded, 0, [&](lua_State* co) {
        lua_newtable(co);
        lua_pushstring(co, keyCode.c_str());   lua_setfield(co, -2, "KeyCode");
        lua_pushstring(co, inputType.c_str()); lua_setfield(co, -2, "UserInputType");
        lua_pushstring(co, st.c_str());        lua_setfield(co, -2, "UserInputState");
        LuaApi::pushVector3(co, glm::vec3(position, 0.0f)); lua_setfield(co, -2, "Position");
        lua_pushboolean(co, sunk);   // gameProcessedEvent
        return 2;
    });
    return sunk;
}

bool ScriptEngine::touchInput(int finger, const char* state, glm::vec2 position) {
    return input("Touch#" + std::to_string(finger), "Unknown", "Touch", state, position);
}

bool ScriptEngine::pointerDown(bool down, glm::vec2 position) {
    if (down == m_pointerHeld) return down && m_sunk.count("MouseButton1");
    m_pointerHeld = down;
    return input("MouseButton1", "Unknown", "MouseButton1", down ? "Begin" : "End", position);
}

bool ScriptEngine::inputSunk(int imguiKey) const {
    const char* name = LuaApi::keyCodeName(imguiKey);
    return name && m_sunk.count(name) > 0;
}

std::vector<ScriptEngine::ActionButton> ScriptEngine::actionButtons() const {
    std::vector<ActionButton> out;
    for (const auto& a : m_actions) if (a.touchButton) out.push_back(a.button);   // (bound order)
    return out;
}

void ScriptEngine::actionButtonInput(const std::string& name, bool down, glm::vec2 position) {
    ActionBinding* a = findAction(name);
    if (!a || !a->touchButton || a->button.pressed == down) return;
    a->button.pressed = down;
    callAction(a->seq, down ? "Begin" : "End", "Unknown", "Touch", position);
}

void ScriptEngine::updateActions() {
    // Tests' pretend input.
    std::vector<TestInput> tests;
    tests.swap(m_testInputs);
    for (const TestInput& t : tests) {
        if (t.kind == "key") {
            if (int k = LuaApi::keyFromName(t.name)) ImGui::GetIO().AddKeyEvent((ImGuiKey)k, t.down);
        } else if (t.kind == "click") {
            if (t.down) m_testClick = true;   // GameSession clicks the game view with it
        } else if (t.kind == "button") {
            actionButtonInput(t.name, t.down, glm::vec2(0.0f));
        } else if (t.kind == "touch") {
            touchInput(99, t.down ? "Begin" : "End", glm::vec2(0.0f));
        }
    }
    // Keys let go while nobody was listening (typing in a TextBox...) still end.
    std::vector<std::string> stale;
    for (const auto& [key, held] : m_holders) {
        const int k = LuaApi::keyFromName(key);
        if (k && !ImGui::IsKeyDown((ImGuiKey)k)) stale.push_back(key);
    }
    for (const std::string& key : m_sunk) {
        const int k = LuaApi::keyFromName(key);
        if (k && !ImGui::IsKeyDown((ImGuiKey)k) && !m_holders.count(key)) stale.push_back(key);
    }
    for (const std::string& key : stale) {
        const char* type = "Keyboard";
        LuaApi::keyFromName(key, &type);
        input(key, key, type, "End", glm::vec2(0.0f));
    }
    // The mouse button let go.
    if (m_pointerHeld && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        pointerDown(false, LuaApi::mouseOnGame());
    }
}
