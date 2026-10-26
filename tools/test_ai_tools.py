#!/usr/bin/env python3
"""Tests Studio's AI tools (the MCP server) the way an AI app uses them.

Starts Studio with its MCP server switched on, talks JSON-RPC to it over HTTP
and checks: inserting into services, the game tree showing every service,
property names matching Lua (and the old spellings still working), Script.Source
going through edit_script, Output lines marked with their play session, bad names
listing the valid choices, and publish_game refusing when not signed in.

    python3 tools/test_ai_tools.py [--exe build/GutsAndBolts] [--port 44799]

On Linux without a screen it runs Studio under xvfb-run. Studio is pointed at a
server that doesn't exist (GB_SERVER) and a throwaway account folder, so nothing
is published and your own account isn't touched.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class Mcp:
    def __init__(self, port):
        self.url = f"http://127.0.0.1:{port}/mcp"
        self.next_id = 0

    def rpc(self, method, params=None, timeout=60):
        self.next_id += 1
        body = json.dumps({"jsonrpc": "2.0", "id": self.next_id, "method": method, "params": params or {}}).encode()
        req = urllib.request.Request(self.url, data=body, headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=timeout) as r:
            reply = json.loads(r.read())
        if "error" in reply:
            raise RuntimeError(f"{method}: {reply['error']}")
        return reply["result"]

    def call(self, tool, **args):
        res = self.rpc("tools/call", {"name": tool, "arguments": args})
        data = res.get("structuredContent") or json.loads(res["content"][0]["text"])
        return data


failures = []
checks = 0


def check(cond, what, detail=None):
    global checks
    checks += 1
    if cond:
        print(f"  ok   {what}")
    else:
        failures.append(what)
        print(f"  FAIL {what}")
        if detail is not None:
            print("       " + json.dumps(detail)[:1500])


def ok(d):
    return d.get("success") is True


def code(d):
    return (d.get("error") or {}).get("code")


def choices(d):
    return (d.get("error") or {}).get("valid_choices") or []


def run_tests(m):
    tools = {t["name"] for t in m.rpc("tools/list")["tools"]}
    check("publish_game" in tools, "tools/list has publish_game")
    check("analyze_script_dependencies" in tools, "tools/list has analyze_script_dependencies")
    check("analyze_scene_performance" in tools, "tools/list has analyze_scene_performance")
    perf = m.call("analyze_scene_performance")
    metrics = perf.get("metrics", {})
    check(ok(perf) and isinstance(metrics.get("part_count"), int) and
          isinstance(perf.get("findings"), list) and "Static heuristic" in perf.get("note", ""),
          "scene performance audit returns static metrics and clearly labels limitations", perf)

    dep_module = m.call("create_script", parent="ServerScriptService", name="AiDependencyModule",
                        type="ModuleScript", source="return { answer = 42 }")
    dep_user = m.call("create_script", parent="ServerScriptService", name="AiDependencyUser",
                      source='local mod = require("AiDependencyModule")')
    dep_report = m.call("analyze_script_dependencies")
    edges = dep_report.get("dependencies", [])
    check(ok(dep_module) and ok(dep_user) and ok(dep_report) and
          any(e.get("from", "").endswith("AiDependencyUser") and
              e.get("to", "").endswith("AiDependencyModule") for e in edges),
          "dependency analysis finds a literal require(ModuleScript) edge", dep_report)


    print("1. Inserting into services")
    targets = [("Tool", "StarterPack"), ("Script", "ServerScriptService"), ("ScreenGui", "StarterGui"),
               ("Part", "ReplicatedStorage"), ("Part", "ServerStorage"), ("Folder", "game.ReplicatedFirst"),
               ("LocalScript", "StarterPlayer.StarterPlayerScripts"), ("Sound", "SoundService"),
               ("Folder", "Chat"), ("Folder", "TextChatService"), ("RemoteEvent", "ReplicatedStorage")]
    made = {}
    for kind, parent in targets:
        d = m.call("insert_object", kind=kind, parent=parent, name=f"Ai{kind}")
        svc = parent.replace("game.", "").split(".")[0]
        check(ok(d) and d.get("path", "").startswith(svc + ".") and not d.get("warnings"),
              f"insert {kind} into {parent} -> {d.get('path')}", d)
        made[(kind, parent)] = d.get("object_id")
    d = m.call("insert_object", kind="Team", parent="Teams")
    check(ok(d) and d.get("class") == "Team" and d.get("path", "").startswith("Teams."), f"insert Team -> {d.get('path')}", d)
    d = m.call("insert_object", kind="Script", parent="StarterPlayer.StarterCharacterScripts", name="AiCharScript")
    check(ok(d) and d.get("class") == "Script", "insert Script into StarterPlayer.StarterCharacterScripts (the character)", d)
    d = m.call("create_script", parent="ServerScriptService", name="AiRules", source="print('rules')")
    check(ok(d) and d.get("path") == "ServerScriptService.AiRules", "create_script in ServerScriptService", d)
    d = m.call("get_object", object="StarterPack.AiTool")
    check(ok(d) and d.get("class") == "Tool", "find StarterPack.AiTool by path", d)
    d = m.call("insert_object", kind="Part", parent="Lighting")
    check(not ok(d) and code(d) == "NOT_A_CONTAINER" and "StarterPack" in choices(d), "Lighting refuses objects, lists containers", d)
    d = m.call("run_lua", code="print(game:GetService('StarterPack'):FindFirstChild('AiTool') ~= nil, "
                               "game.StarterPack.ClassName, game.StarterPlayer.StarterPlayerScripts.AiLocalScript.ClassName)")
    check(ok(d) and d.get("output") and d["output"][-1] == "true StarterPack LocalScript",
          "Lua sees the inserted objects where the tools put them", d)

    print("2. The game tree shows every service")
    d = m.call("get_game_tree", depth=3)
    tree = d.get("tree", "")
    names = [s["name"] for s in d.get("services", [])]
    for svc in ["Workspace", "Players", "Lighting", "ReplicatedFirst", "ReplicatedStorage", "ServerScriptService",
                "ServerStorage", "StarterGui", "StarterPack", "StarterPlayer", "Teams", "SoundService", "Chat", "TextChatService"]:
        check(svc in names and f"\n{svc} (" in "\n" + tree, f"tree lists {svc}", d if svc not in names else None)
    check("AiTool (Tool)" in tree and "StarterPlayerScripts" in tree and "StarterCharacterScripts" in tree,
          "tree shows what's inside services", tree)
    ws = tree.split("\nPlayers (")[0]
    check("StarterPack (" not in ws, "service folders aren't listed inside the Workspace", ws)
    sp = tree.split("\nStarterPlayer (")[1].split("\nTeams (")[0] if "\nStarterPlayer (" in tree else ""
    check("AiCharScript (Script)" in sp and "AiLocalScript (LocalScript)" in sp,
          "StarterPlayer shows the character (with its script) and StarterPlayerScripts", sp)
    d = m.call("read_script", object="StarterPlayer.StarterCharacterScripts.AiCharScript")
    check(ok(d), "path through StarterCharacterScripts finds the character's script", d)
    d = m.call("get_game_tree", root="StarterPack")
    check(ok(d) and "AiTool" in d.get("tree", ""), "tree with root StarterPack", d)

    print("3. Property names agree with Lua")
    part = m.call("insert_object", kind="Part", name="AiPart")
    pid = part["object_id"]
    st = part.get("resulting_state", {})
    check(all(k in st for k in ["Position", "Size", "CanCollide", "Anchored", "Color", "Material"]),
          "resulting_state uses Lua names", st)
    d = m.call("get_object", object=pid)
    props = d.get("properties", {})
    check(all(k in props for k in ["Position", "Size", "CanCollide", "Anchored", "Transparency", "Material", "Name", "Parent"]),
          "get_object properties use Lua names", sorted(props)[:40])
    d = m.call("set_property", object=pid, property="can_collide", value=False)
    check(ok(d) and "CanCollide" in d.get("changed_properties", {}) and d["changed_properties"]["CanCollide"]["now"] is False
          and d.get("warnings"), "old spelling can_collide works and is corrected to CanCollide", d)
    d = m.call("run_lua", code=f"print(__gb_byId({pid[1:]}).CanCollide)")
    check(ok(d) and d["output"][-1] == "false", "Lua reads back CanCollide = false", d)
    d = m.call("set_property", object=pid, property="Material", value="Enum.Material.Neon")
    check(ok(d) and d["changed_properties"]["Material"]["now"] == "Neon", "Enum.Material.Neon accepted", d)
    d = m.call("set_property", object=pid, property="Parent", value="ReplicatedStorage")
    check(ok(d) and d["resulting_state"]["path"] == "ReplicatedStorage.AiPart", "Parent = a service", d)
    d = m.call("create_physical_object", Size=[3, 1, 2], CanCollide=True, Material="Wood", name="AiCrate")
    check(ok(d) and d["resulting_state"]["Size"] == [3, 1, 2] and d["resulting_state"]["Material"] == "Wood",
          "create_physical_object takes Lua names (Size, Material)", d)
    d = m.call("create_physical_object", size=[2, 2, 2], can_collide=True, name="AiCrate2")
    check(ok(d) and d["resulting_state"]["CanCollide"] is True, "create_physical_object still takes size / can_collide", d)
    d = m.call("set_property", object="Lighting", property="ClockTime", value=6)
    check(ok(d) and abs(d["changed_properties"]["ClockTime"]["now"] - 6) < 0.01, "set_property on Lighting", d)
    d = m.call("get_object", object="Lighting")
    check(ok(d) and "FogEnabled" in d.get("properties", {}), "get_object Lighting", d)
    gui = m.call("insert_object", kind="Frame", name="AiFrame")
    d = m.call("set_property", object=gui["object_id"], property="Size", value=[[0.5, 0], [0, 40]])
    check(ok(d) and d["changed_properties"]["Size"]["now"] == [[0.5, 0], [0, 40]], "UDim2 round-trips in get_object's format", d)

    print("4. Script.Source goes through edit_script")
    s = m.call("create_script", name="AiSrc", source="print('one')")
    d = m.call("set_property", object=s["object_id"], property="Source", value="print('two')")
    check(ok(d) and d.get("routed_to") == "edit_script" and "edit_script" in d.get("note", ""), "Source routed to edit_script", d)
    d = m.call("read_script", object=s["object_id"])
    check(d.get("source") == "print('two')", "the script has the new code", d)

    print("5. Output lines are marked with their play session")
    m.call("run_lua", code="error('old-error-before-play')")
    d = m.call("playtest", action="start")
    check(ok(d) and d.get("playing"), "playtest start", d)
    time.sleep(1.0)
    m.call("run_lua", code="print('hello-from-this-run') warn('warn-this-run')")
    d = m.call("get_output", run="latest", lines=400)
    run = d.get("latest_run")
    texts = [l["text"] for l in d.get("lines", [])]
    check(ok(d) and run and run == d.get("current_run") and any("hello-from-this-run" in t for t in texts)
          and all(l["run"] == run for l in d["lines"]), f"get_output run latest = run {run} only", d)
    check(not any("old-error-before-play" in t for t in texts), "the old error isn't in the latest run", texts)
    m.call("playtest", action="stop")
    d = m.call("get_errors", run="latest")
    check(ok(d) and not any("old-error-before-play" in e["text"] for e in d["errors"])
          and any("warn-this-run" in w["text"] for w in d["warnings"]), "get_errors run latest leaves old errors out", d)
    d = m.call("get_errors")
    old = [e for e in d.get("errors", []) if "old-error-before-play" in e["text"]]
    check(ok(d) and old and old[0]["run"] == 0, "all runs: the old error is there, marked run 0 (editing)", d)
    d = m.call("get_errors", run="banana")
    check(not ok(d) and code(d) == "INVALID_INPUT", "bad run value is refused", d)

    print("6. Bad names list the valid choices")
    d = m.call("insert_object", kind="Partt")
    check(code(d) == "UNKNOWN_CLASS" and "Part" in choices(d) and "Part" in (d["error"].get("did_you_mean") or []),
          "unknown class lists the classes (did you mean Part)", d)
    d = m.call("insert_object", kind="pointlight", parent="ReplicatedStorage")
    check(ok(d) and d.get("class") == "PointLight", "class name in another case is accepted", d)
    d = m.call("set_property", object=pid, property="Colr", value=[1, 0, 0])
    check(code(d) == "UNKNOWN_PROPERTY" and "Color" in choices(d) and "CanCollide" in choices(d),
          "unknown property lists the properties", d)
    d = m.call("set_property", object=pid, property="Material", value="Lava")
    check(code(d) == "INVALID_ENUM_VALUE" and "Neon" in choices(d), "bad enum value lists the values", d)
    d = m.call("insert_object", kind="Part", parent="StarterPak")
    check(code(d) == "SERVICE_NOT_FOUND" and "StarterPack" in choices(d), "unknown service lists the services", d)
    d = m.call("get_game_tree", root="Workspaze.Thing")
    check(code(d) == "SERVICE_NOT_FOUND", "unknown service in a path", d)

    print("7. publish_game refuses when not signed in")
    d = m.call("publish_game", confirm=True)
    check(not ok(d) and code(d) == "NOT_SIGNED_IN", "publish refused: not signed in", d)
    d = m.call("publish_game", action="status")
    check(ok(d) and d.get("state") == "none", "nothing was published", d)
    d = m.call("validate_scene")
    check(ok(d), "validate_scene still works", d)

    print("8. Find and Change (many objects at once)")
    ids = []
    for i, mass_tag in enumerate([None, "Lightweight", None]):
        d = m.call("insert_object", kind="Part", name=f"BulkCrate{i}")
        ids.append(d["object_id"])
        m.call("set_property", object=d["object_id"], property="Anchored", value=False)
        if mass_tag:
            m.call("run_lua", code=f"__gb_byId({d['object_id'][1:]}):AddTag('{mass_tag}')")
    d = m.call("find_and_change", kind="BasePart", name_contains="bulkcrate")
    check(ok(d) and d.get("count") == 3, "finds by kind and name (any case)", d)
    d = m.call("find_and_change", name_contains="BulkCrate", not_tag="Lightweight",
               change={"property": "Density", "op": "Multiply by", "value": "1.5"})
    rows = d.get("results", [])
    check(ok(d) and d.get("count") == 2 and all(r.get("becomes") and not r.get("error") for r in rows) and not d.get("applied"),
          "preview skips the tagged one and shows new values", d)
    before = m.call("get_object", object=ids[0])["properties"]["Density"]
    check(abs(before - float(rows[0]["now"])) < 1e-3 if rows else False, "preview changed nothing", before)
    d = m.call("find_and_change", name_contains="BulkCrate", not_tag="Lightweight",
               change={"property": "Density", "op": "Multiply by", "value": "1.5"}, apply=True)
    check(ok(d) and d.get("changed") == 2, "apply changes both", d)
    after = m.call("get_object", object=ids[0])["properties"]["Density"]
    tagged = m.call("get_object", object=ids[1])["properties"]["Density"]
    check(abs(after - before * 1.5) < 1e-3 and abs(tagged - before) < 1e-3, "Density x1.5, the tagged one untouched", [before, after, tagged])
    d = m.call("find_and_change", name_contains="BulkCrate", where={"property": "Density", "op": ">", "value": str(before + 0.01)})
    check(ok(d) and d.get("count") == 2, "where Density > old value finds the two changed ones", d)
    d = m.call("undo")
    back = m.call("get_object", object=ids[2])["properties"]["Density"]
    check(ok(d) and abs(back - before) < 1e-3, "one undo puts every change back", [back, before])
    d = m.call("find_and_change", name_contains="BulkCrate", change={"property": "Color", "op": "Set to", "value": "255, 0, 0"}, apply=True)
    col = m.call("get_object", object=ids[2])["properties"]["Color"]
    check(ok(d) and d.get("changed") == 3 and abs(col[0] - 1) < 1e-3 and col[1] < 1e-3, "set a colour on all of them", [d, col])
    d = m.call("find_and_change", name_contains="BulkCrate", change={"op": "Add tag", "value": "Crate"}, apply=True)
    d2 = m.call("find_and_change", has_tag="Crate")
    check(ok(d) and d2.get("count") == 3, "add a tag to all of them", d2)
    d = m.call("find_and_change", name_contains="BulkCrate", change={"property": "Nope", "op": "Set to", "value": "1"})
    check(ok(d) and all("has no Nope" in r.get("error", "") for r in d.get("results", [])), "a missing property is reported per object", d)
    d = m.call("find_and_change", change={"property": "Density", "op": "Squish", "value": "1"})
    check(code(d) == "INVALID_INPUT" and "Multiply by" in choices(d), "bad op lists the choices", d)

    print("9. Physics workbench: joints that snap, force zones")
    setup = """
local function part(name, pos, size, anchored)
  local p = Instance.new('Part'); p.Name = name; p.Position = pos; p.Size = size; p.Anchored = anchored; p.Parent = workspace
  return p
end
local post = part('PwPost', Vector3.new(40, 20, 40), Vector3.new(2, 2, 2), true)
local weak = part('PwWeak', Vector3.new(40, 18.5, 40), Vector3.new(1, 1, 1), false)
weak.Density = 50
local w1 = Instance.new('WeldConstraint'); w1.Name = 'PwWeakWeld'; w1.Part0 = post; w1.Part1 = weak; w1.BreakForce = 100; w1.Parent = post
local post2 = part('PwPost2', Vector3.new(46, 20, 40), Vector3.new(2, 2, 2), true)
local strong = part('PwStrong', Vector3.new(46, 18.5, 40), Vector3.new(1, 1, 1), false)
local w2 = Instance.new('WeldConstraint'); w2.Name = 'PwStrongWeld'; w2.Part0 = post2; w2.Part1 = strong; w2.BreakForce = 1e7; w2.Parent = post2
local zone = part('PwZone', Vector3.new(-40, 6, -40), Vector3.new(8, 12, 8), true)
zone.CanCollide = false; zone.Transparency = 0.8
zone:AddTag('ForceZone'); zone:SetAttribute('Mode', 'Wind'); zone:SetAttribute('Strength', 400)
local box = part('PwFloater', Vector3.new(-40, 1, -40), Vector3.new(1, 1, 1), false)
print(w1.BreakForce, w1.Broken)
"""
    d = m.call("run_lua", code=setup)
    check(ok(d) and d.get("output") and d["output"][-1] in ("100 false", "100.0 false"), "BreakForce / Broken from Lua", d)
    d = m.call("get_object", object="Workspace.PwPost.PwWeakWeld")
    check(ok(d) and d.get("properties", {}).get("BreakForce") == 100, "get_object shows BreakForce", d)
    d = m.call("playtest", action="simulate")
    check(ok(d), "simulate", d)
    time.sleep(6)
    d = m.call("run_lua", code="local w = workspace.PwPost.PwWeakWeld; local s = workspace.PwPost2.PwStrongWeld\n"
                               "local weak = workspace:FindFirstChild('PwWeak')  -- (gone = fell out of the world)\n"
                               "print(w.Broken, w.Enabled, s.Broken, s.Enabled, weak and math.floor(weak.Position.Y) or -999,"
                               " math.floor(workspace.PwStrong.Position.Y), math.floor(workspace.PwFloater.Position.Y))")
    out = (d.get("output") or [""])[-1].split()
    check(ok(d) and out[:4] == ["true", "false", "false", "true"], "the weak weld snapped, the strong one held", d)
    check(len(out) == 7 and float(out[4]) < 15 and float(out[5]) >= 17, "the snapped part fell, the held one didn't", out)
    check(len(out) == 7 and float(out[6]) > 3, "the wind zone lifted the loose box", out)
    m.call("playtest", action="stop")
    d = m.call("run_lua", code="print(workspace.PwPost.PwWeakWeld.Enabled, workspace.PwPost.PwWeakWeld.BreakForce)")
    check(ok(d) and d["output"][-1] in ("true 100", "true 100.0"), "after Stop the weld is back (and still breakable)", d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "GutsAndBolts"))
    ap.add_argument("--port", type=int, default=44799)
    ap.add_argument("--keep-log", action="store_true")
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    if not os.path.exists(exe):
        sys.exit(f"No Studio at {exe}: build it first (cmake --build build).")

    # Studio reads assistant.json from its own folder: switch MCP on for this run only.
    cfg = os.path.join(os.path.dirname(exe), "assistant.json")
    backup = None
    if os.path.exists(cfg):
        backup = cfg + ".test-backup"
        shutil.copyfile(cfg, backup)
    with open(cfg, "w") as f:
        json.dump({"mcp": True, "mcpPort": a.port}, f)

    tmp = tempfile.mkdtemp(prefix="gb-ai-tools-")
    env = dict(os.environ, GB_SERVER="127.0.0.1:9", GB_ACCOUNT_DIR=os.path.join(tmp, "account"))
    cmd = [exe]
    if sys.platform.startswith("linux") and not os.environ.get("DISPLAY") and shutil.which("xvfb-run"):
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24"] + cmd
    log_path = os.path.join(tmp, "studio.log")
    log = open(log_path, "w")
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env, cwd=os.path.dirname(exe),
                            start_new_session=True)
    m = Mcp(a.port)
    try:
        deadline = time.time() + 90
        while True:
            try:
                info = m.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                            "clientInfo": {"name": "test_ai_tools", "version": "1"}}, timeout=5)
                break
            except Exception:
                if proc.poll() is not None or time.time() > deadline:
                    log.flush()
                    print(open(log_path).read()[-4000:])
                    sys.exit("Studio's MCP server didn't come up.")
                time.sleep(0.5)
        print(f"Connected to {info['serverInfo']['name']} {info['serverInfo']['version']}")
        run_tests(m)
    finally:
        try:
            os.killpg(proc.pid, 15)
            proc.wait(timeout=15)
        except Exception:
            try:
                os.killpg(proc.pid, 9)
            except Exception:
                pass
        if backup:
            shutil.move(backup, cfg)
        else:
            os.remove(cfg)
        log.close()
        if a.keep_log or failures:
            print(f"Studio's log: {log_path}")
        else:
            shutil.rmtree(tmp, ignore_errors=True)
    print(f"\n{checks - len(failures)}/{checks} checks passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
