import time

TITLE = 'The Lua Debugger pauses at breakpoints and steps'


def run(m, t):
    tools = {x["name"] for x in m.rpc("tools/list")["tools"]}
    t.check("lua_debugger" in tools, "tools/list has lua_debugger")
    dbg = m.call("create_script", parent="ServerScriptService", name="AiDebugMe", source="\n".join([
        "local total = 0",          # 1
        "local function add(n)",    # 2
        "    total = total + n",    # 3
        "    return total",         # 4
        "end",                      # 5
        "for i = 1, 3 do",          # 6
        "    add(i)",               # 7
        "    print('dbg-loop', i)", # 8
        "end",                      # 9
        "print('dbg-done', total)", # 10
    ]))
    d = m.call("lua_debugger", action="add_breakpoint", object=dbg["object_id"], line=7)
    t.check(t.ok(d) and d["breakpoints"] and d["breakpoints"][0]["line"] == 7, "add a breakpoint before playing", d)

    def wait_paused(before):
        deadline = time.time() + 20
        while time.time() < deadline:
            st = m.call("lua_debugger", action="status")
            if st.get("paused") and st.get("pauses", 0) > before:
                return st
            time.sleep(0.2)
        return st

    m.call("playtest", action="start")
    d = wait_paused(0)
    t.check(d.get("paused") and d["at"]["line"] == 7 and d["at"]["object_id"] == dbg["object_id"]
          and "i = 1" in d.get("locals", []), "paused at line 7 with i = 1", d)
    d = m.call("lua_debugger", action="continue")
    d = wait_paused(1)
    t.check(d.get("paused") and d["at"]["line"] == 7 and "i = 2" in d.get("locals", []),
          "continue: it pauses at the same breakpoint the next time round the loop", d)
    d = m.call("lua_debugger", action="step_into")
    d = wait_paused(2)
    t.check(d.get("paused") and d["at"]["line"] == 3 and d["at"]["reason"] == "step" and "n = 2" in d.get("locals", [])
          and len(d.get("call_stack", [])) >= 2, "step_into goes inside add()", d)
    d = m.call("lua_debugger", action="step_out")
    d = wait_paused(3)
    t.check(d.get("paused") and d["at"]["line"] == 8, "step_out comes back to the loop (line 8)", d)
    d = m.call("lua_debugger", action="step_over")
    d = wait_paused(4)
    t.check(d.get("paused") and d["at"]["line"] in (6, 7), "step_over goes to the next line of the loop", d)
    m.call("lua_debugger", action="remove_breakpoint", object=dbg["object_id"], line=7)
    d = m.call("lua_debugger", action="continue")
    t.check(t.ok(d) and not d.get("paused"), "continue with no breakpoints left runs to the end", d)
    time.sleep(1.0)
    d = m.call("get_output", run="latest", lines=400)
    texts = [l["text"] for l in d.get("lines", [])]
    t.check(any("dbg-done" in t and "6" in t for t in texts), "the script finished normally (total 6)", texts[-20:])
    d = m.call("lua_debugger", action="continue")
    t.check(t.code(d) == "NOT_PAUSED", "continue when nothing is paused is refused", d)
    m.call("playtest", action="stop")
    d = m.call("lua_debugger", action="status")
    t.check(t.ok(d) and not d.get("paused") and not d.get("playing"), "stopping the playtest leaves nothing paused", d)
