import time

TITLE = "Procedural levels (Level Generator)"


def run(m, t):
    d = m.call("generate_level", style="Dungeon", seed=7, count=8, dry_run=True)
    tr = d.get("tried", [{}])[0]
    t.check(t.ok(d) and d["ok"] and tr.get("problems") == [] and tr["route"]["status"] == "Success" and "object_id" not in d,
            "dry_run checks a dungeon (the robot finds the goal) and makes nothing", d)
    again = m.call("generate_level", style="Dungeon", seed=7, count=8, dry_run=True)
    t.check(t.ok(again) and again["tried"][0]["route"] == tr["route"], "the same seed makes the same level", again)
    d = m.call("generate_level", style="Obby", seed=3, count=20, tries=5, dry_run=True)
    t.check(t.ok(d) and d["ok"] and d["tried"][-1]["route"]["jumps"] > 0, "an obby passes, and the robot has to jump", d)
    d = m.call("generate_level", style="Dungeon", count=30, room_min=40, room_max=60, dry_run=True)
    t.check(t.ok(d) and d["ok"] and d["tried"][0]["placed"] == 30, "30 big rooms all fit with nothing overlapping", d)
    d = m.call("generate_level", style="Maze")
    t.check(t.code(d) == "INVALID_ENUM_VALUE" and "Obby" in t.choices(d), "a kind that doesn't exist is refused", d)

    d = m.call("generate_level", style="Dungeon", seed=11, count=5, tries=10, at=[0, 200, 0])
    first = d.get("object_id")
    t.check(t.ok(d) and d["ok"] and first and d["path"].endswith("DungeonLevel") and d["parts"] > 20, "building makes a Model", d)
    r = m.call("run_lua", code="local n = {} for _, c in ipairs(workspace.DungeonLevel:GetChildren()) do n[c.Name] = true end "
                               "print(n.Goal and n.SpawnLocation and n.Floor ~= nil, workspace.DungeonLevel:GetAttribute('Seed'))")
    t.check(t.ok(r) and r["output"] and r["output"][-1].startswith("true"), "it has floors, a spawn and a goal, and remembers its seed", r)
    d = m.call("generate_level", style="Obby", seed=2, count=8, tries=10, at=[0, 200, 0], replace=first)
    gone = m.call("run_lua", code="print(workspace:FindFirstChild('DungeonLevel') == nil, workspace:FindFirstChild('ObbyLevel') ~= nil)")
    t.check(t.ok(d) and t.ok(gone) and gone["output"][-1] == "true true", "replace takes the old level away", gone)
    m.call("playtest", action="start")
    time.sleep(1.0)
    d = m.call("generate_level", style="Dungeon")
    t.check(t.code(d) == "PLAYING", "levels aren't made during a playtest", d)
    d = m.call("generate_level", style="Dungeon", dry_run=True)
    t.check(t.ok(d), "but dry_run still works then", d)
    m.call("playtest", action="stop")
