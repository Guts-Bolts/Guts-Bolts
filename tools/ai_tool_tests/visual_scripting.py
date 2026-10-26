import time

TITLE = "Visual Scripting (blocks)"


def run(m, t):
    d = m.call("visual_script", action="blocks")
    types = [b["type"] for b in d.get("blocks", [])]
    t.check(t.ok(d) and len(types) >= 20 and "touched" in types and "ifPlayer" in types, "blocks lists the blocks", d)
    part = m.call("insert_object", kind="Part", name="BlockPart")
    n = lambda i, kind, **f: {"id": i, "type": kind, "x": 0, "y": i * 100, "f": f}
    graph = {"nodes": [n(1, "start"), n(2, "print", text="blocks-said-hi"), n(3, "transparency", target="this", value="0.8"),
                       n(4, "repeat", times="2"), n(5, "print", text="blocks-loop"), n(6, "print", text="blocks-after"),
                       n(7, "ifChance", percent="100"), n(8, "print", text="blocks-lucky"), n(9, "print", text="blocks-unlucky")],
             "links": [{"from": 1, "pin": "next", "to": 2}, {"from": 2, "pin": "next", "to": 3}, {"from": 3, "pin": "next", "to": 4},
                       {"from": 4, "pin": "do", "to": 5}, {"from": 4, "pin": "next", "to": 6}, {"from": 6, "pin": "next", "to": 7},
                       {"from": 7, "pin": "then", "to": 8}, {"from": 7, "pin": "else", "to": 9}]}
    d = m.call("visual_script", action="write", parent=part["object_id"], name="Blocky", graph=graph)
    sid = d.get("object_id")
    t.check(t.ok(d) and d.get("problems") == [] and d["syntax"]["ok"] and "task.spawn" in d.get("lua", ""), "write makes a block script", d)
    d = m.call("read_script", object=sid)
    t.check(d.get("source", "").startswith("--[==[blocks") and "for _ = 1, 2 do" in d["source"], "its code keeps the blocks, then the Lua", d)
    d = m.call("visual_script", action="read", object=sid)
    t.check(t.ok(d) and len(d["graph"]["nodes"]) == 9 and len(d["graph"]["links"]) == 8 and d["edited"] is False,
          "read gives the same blocks back", d)
    d = m.call("playtest", action="start")
    time.sleep(1.5)
    out = [l["text"] for l in m.call("get_output", run="latest", lines=400).get("lines", [])]
    t.check(any("blocks-said-hi" in t for t in out) and sum("blocks-loop" in t for t in out) == 2
          and any("blocks-after" in t for t in out) and any("blocks-lucky" in t for t in out)
          and not any("blocks-unlucky" in t for t in out), "the blocks run in order in a playtest", out)
    r = m.call("run_lua", code="print(workspace.BlockPart.Transparency)")
    t.check(t.ok(r) and r["output"] and abs(float(r["output"][-1]) - 0.8) < 1e-3, "Set see-through changed the part", r)
    m.call("playtest", action="stop")
    d = m.call("visual_script", action="write", object=sid,
               graph={"nodes": [n(1, "start"), n(2, "destroy", target="other")], "links": [{"from": 1, "to": 2}]})
    t.check(t.ok(d) and d["problems"] and "When touched" in d["problems"][0] and "Destroy" not in d["lua"],
          "\"the part that touched\" outside When touched is a problem, and is left out", d)
    d = m.call("visual_script", action="write", object=sid, graph={"nodes": [n(1, "explode")]})
    t.check(t.code(d) == "UNKNOWN_BLOCK", "an unknown block is refused", d)
    every = {"nodes": [n(1, "touched"), n(2, "ifPlayer"), n(3, "points"), n(4, "damage"), n(5, "teleport"), n(6, "ifPoints"),
                       n(7, "sound"), n(8, "color", target="other"), n(9, "ifTag"), n(10, "cooldown"), n(11, "destroy", target="other"),
                       n(20, "joins"), n(21, "makeStat"), n(22, "print", text="Hi {player}!"),
                       n(30, "clicked"), n(31, "clone"), n(40, "prompt"), n(41, "move", target="character"),
                       n(50, "every"), n(51, "ifAttr"), n(52, "property", property="CanCollide", value="false"), n(53, "wait")],
             "links": [{"from": 1, "to": 2}, {"from": 2, "pin": "then", "to": 3}, {"from": 3, "to": 4}, {"from": 4, "to": 5},
                       {"from": 5, "to": 6}, {"from": 6, "pin": "then", "to": 7}, {"from": 2, "pin": "next", "to": 8},
                       {"from": 8, "to": 9}, {"from": 9, "pin": "then", "to": 10}, {"from": 10, "to": 11},
                       {"from": 20, "to": 21}, {"from": 21, "to": 22}, {"from": 30, "to": 31}, {"from": 40, "to": 41},
                       {"from": 50, "to": 51}, {"from": 51, "pin": "then", "to": 52}, {"from": 52, "to": 53}]}
    d = m.call("visual_script", action="write", object=sid, graph=every)
    t.check(t.ok(d) and d["problems"] == [] and d["syntax"]["ok"], "every kind of block makes Lua with no syntax errors", d)
    t.check("local function stat(" in d.get("lua", "") and "local function find(" in d["lua"] and "ClickDetector" in d["lua"]
          and "ProximityPrompt" in d["lua"] and "PlayerAdded" in d["lua"], "the helpers and events are in the Lua", d)
    e = m.call("edit_script", object=sid, old='"Hi "', new='"Hey "')
    d = m.call("visual_script", action="read", object=sid)
    t.check(t.ok(e) and t.ok(d) and d["edited"] is True, "a change by hand is noticed", d)
    s = m.call("create_script", name="NotBlocks", source="print('plain')")
    d = m.call("visual_script", action="read", object=s["object_id"])
    t.check(t.code(d) == "NOT_BLOCKS", "a normal script isn't read as blocks", d)

