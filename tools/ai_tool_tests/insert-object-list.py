TITLE = 'Insert Object lists every kind once'


def run(m, t):
    d = m.call("get_engine_info", topic="classes")
    kinds = (d.get("classes") or []) if t.ok(d) else []
    twice = sorted({k for k in kinds if kinds.count(k) > 1})
    t.check(t.ok(d) and len(kinds) > 50, "get_engine_info lists the classes", d)
    t.check(not twice, "no kind is listed twice (Dear ImGui's conflicting ID error)", twice)
    for k in ("RemoteEvent", "RemoteFunction", "ClickDetector", "Beam"):
        t.check(k in kinds, k + " is still there", None)
