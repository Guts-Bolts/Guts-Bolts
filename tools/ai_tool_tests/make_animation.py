TITLE = 'AI animation from words: make_animation'


def run(m, t):
    rig = m.call("insert_object", kind="Rig", name="AnimWordsRig")
    t.check(t.ok(rig), "a rig to animate", rig)
    rid = rig.get("object_id")
    d = m.call("make_animation", rig=rid, prompt="wave twice with the left arm, then bow")
    t.check(t.ok(d) and d.get("name") == "Wave, Bow" and [s["motion"] for s in d.get("steps", [])] == ["wave", "bow"],
            "two steps, named after them", d)
    t.check(d.get("length", 0) > 1.5 and d.get("keyframes", 0) > 5 and not d.get("loop"), "a few seconds long, not looping", d)
    t.check(d["steps"][0]["times"] == 2 and d["steps"][1]["start"] >= d["steps"][0]["end"] - 0.01, "twice, and bow comes after", d)
    o = m.call("get_object", object=d["object_id"])
    t.check(t.ok(o) and o.get("class") == "Animation", "an Animation object in the rig", o)
    d = m.call("make_animation", rig=rid, prompt="run in a loop", name="Run Cycle")
    t.check(t.ok(d) and d.get("loop") is True and d.get("name") == "Run Cycle", "run in a loop loops", d)
    d = m.call("make_animation", rig=rid, prompt="do a backflip into the sea")
    t.check(not t.ok(d) and t.code(d) == "NO_MOTION" and len(d.get("motions", [])) > 20, "no known motion: says which ones work", d)
    d = m.call("make_animation", rig=rid, prompt="jump then frobnicate wildly")
    t.check(t.ok(d) and d.get("not_understood") == ["frobnicate wildly"], "words it didn't get are listed", d)
    d = m.call("make_animation", rig=rid, keyframes=[{"t": 0, "poses": {"Head": {"rot": [0, 0, 0]}}},
                                                     {"t": 0.5, "poses": {"Head": {"rot": [0, 45, 0]}, "Tail": {"rot": [10, 0, 0]}}}])
    t.check(t.ok(d) and d.get("keyframes") == 2 and d.get("missing_parts") == ["Tail"], "own keyframes, unknown parts left out", d)
    d = m.call("make_animation", rig=rid)
    t.check(not t.ok(d), "prompt or keyframes is needed", d)
