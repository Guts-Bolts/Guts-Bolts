import time

TITLE = "Physics record and replay"


def y_of(m, name):
    d = m.call("run_lua", code=f"print(workspace.{name}.Position.Y)")
    return float(d["output"][-1]) if d.get("output") else None


def run(m, t):
    m.call("run_lua", code="local p = Instance.new('Part'); p.Name = 'PrDrop'; p.Size = Vector3.new(2, 2, 2)\n"
                           "p.Position = Vector3.new(24, 20, -24); p.Anchored = false; p.Parent = workspace")
    d = m.call("physics_replay", action="frame")
    t.check(t.code(d) == "NO_RECORDING", "frame needs a recording first", d)
    d = m.call("physics_replay", action="record", seconds=3)
    t.check(t.ok(d) and d["frames"] == 181 and d["parts"] >= 1, "record runs 3 seconds of physics (60 steps a second)", d)
    t.check(abs(y_of(m, "PrDrop") - 20) < 1e-3, "recording doesn't move anything in the scene", None)
    d = m.call("physics_replay", action="frame", time=0)
    drop = [p for p in d.get("parts", []) if p["name"] == "PrDrop"]
    t.check(t.ok(d) and drop and abs(drop[0]["position"][1] - 20) < 1e-3, "frame 0 is the start", d)
    d = m.call("physics_replay", action="frame", time=3)
    drop = [p for p in d.get("parts", []) if p["name"] == "PrDrop"]
    t.check(t.ok(d) and drop and abs(drop[0]["position"][1] - 1) < 0.1, "at 3 seconds it has landed on the baseplate", d)
    d = m.call("physics_replay", action="frame", frame=999)
    t.check(t.code(d) == "INVALID_INPUT", "a frame past the end is refused", d)
    d = m.call("physics_replay", action="keep", time=3)
    t.check(t.ok(d) and abs(y_of(m, "PrDrop") - 1) < 0.1, "keep moves the scene's parts to that moment", d)
    m.call("run_lua", code="workspace.PrDrop.Position = Vector3.new(24, 20, -24)")

    d = m.call("physics_replay", action="save_test", name="PrDropTest", seconds=2)
    t.check(t.ok(d) and d["path"].endswith("PhysicsTests.PrDropTest"), "save_test keeps a test in ServerStorage", d)
    d = m.call("physics_replay", action="run_tests", test="PrDropTest")
    t.check(t.ok(d) and d["passed"] == 1 and d["results"][0]["worst"] < 0.01, "the test passes when nothing changed", d)
    m.call("run_lua", code="workspace.PrDrop.Position = Vector3.new(24, 30, -24)")
    d = m.call("physics_replay", action="run_tests", test="PrDropTest")
    t.check(t.ok(d) and d["passed"] == 1, "it starts the parts where the test did, so moving them doesn't matter", d)
    m.call("run_lua", code="local s = Instance.new('Part'); s.Name = 'PrShelf'; s.Size = Vector3.new(6, 1, 6)\n"
                           "s.Position = Vector3.new(24, 10, -24); s.Anchored = true; s.Parent = workspace")
    d = m.call("physics_replay", action="run_tests", test="PrDropTest")
    r = (d.get("results") or [{}])[0]
    t.check(t.ok(d) and d["failed"] == 1 and r.get("part") == "PrDrop" and "studs off" in r.get("message", ""),
            "a shelf in the way makes the test fail and says which part", d)
    d = m.call("physics_replay", action="save_test", name="PrDropTest", seconds=2)
    d2 = m.call("physics_replay", action="run_tests", test="PrDropTest")
    names = [x["name"] for x in d.get("tests", [])]
    t.check(t.ok(d2) and d2["passed"] == 1 and names.count("PrDropTest") == 1, "saving again replaces it, and it passes", d)
    d = m.call("physics_replay", action="run_tests", test="Nope")
    t.check(t.code(d) == "NOT_FOUND" and "PrDropTest" in t.choices(d), "an unknown test is refused", d)
    m.call("playtest", action="start")
    time.sleep(1.0)
    d = m.call("physics_replay", action="keep", time=1)
    t.check(t.code(d) == "PLAYING", "keep isn't allowed during a playtest", d)
    m.call("playtest", action="stop")
    m.call("run_lua", code="workspace.PrDrop:Destroy()")
    d = m.call("physics_replay", action="run_tests", test="PrDropTest")
    t.check(t.ok(d) and d["failed"] == 1 and "isn't in the game" in d["results"][0]["message"], "a missing part fails the test", d)
    m.call("run_lua", code="workspace.PrShelf:Destroy() game.ServerStorage.PhysicsTests:Destroy()")
