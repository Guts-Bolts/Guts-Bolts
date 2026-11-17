TITLE = 'Network simulator: multiplayer test players over a slow connection (network_sim)'

import time


def run(m, t):
    d = m.call("network_sim", action="set", preset="3G")
    t.check(t.ok(d) and d.get("conditions", {}).get("name") == "3G" and d["conditions"].get("latency_ms") == 250,
            "the 3G preset is picked", d)
    t.check(not d.get("test_running") and "next one" in d.get("note", ""), "with no test running it waits for the next one", d)
    d = m.call("network_sim", action="set", preset="Dial-up")
    t.check(not t.ok(d), "an unknown connection is refused", d)
    d = m.call("network_sim", action="set", latency_ms=99999, jitter_ms=20, loss_percent=1, kbps=500)
    t.check(t.ok(d) and d["conditions"].get("name") == "Custom" and d["conditions"].get("latency_ms") == 3000,
            "your own numbers are kept in range", d)

    m.call("network_sim", action="set", preset="3G")
    d = m.call("multiplayer_test", action="start", players=2)
    t.check(t.ok(d), "a multiplayer test starts", d)
    if not t.ok(d):
        return
    try:
        slow = None
        deadline = time.time() + 60
        while time.time() < deadline:
            s = m.call("network_sim", action="status")
            fresh = [p for p in s.get("players", []) if p.get("fresh") and p.get("worldPerSec", 0) > 0]
            if len(fresh) == 2:
                slow = s
                break
            time.sleep(1)
        t.check(slow is not None, "both players report their numbers", s)
        if slow is None:
            return
        p = slow["players"][0]
        t.check(p.get("conditions") == "3G", "the players use the 3G conditions", p)
        t.check(p.get("ageMs", 0) >= 80, "on 3G the world arrives late (about half the lag)", p)
        t.check(p.get("kbIn", 0) > 0 and "misses" in p and "ownerChanges" in p and "othersBehindMs" in p,
                "data, prediction misses, ownership and other players' delay are counted", p)

        # Switch mid-test: no restart.
        m.call("network_sim", action="set", preset="Off")
        fast = None
        deadline = time.time() + 20
        while time.time() < deadline:
            time.sleep(1.5)
            s = m.call("network_sim", action="status")
            ps = [q for q in s.get("players", []) if q.get("fresh") and q.get("conditions") == "Off"]
            if len(ps) == 2 and all(q.get("ageMs", 999) < 60 for q in ps):
                fast = s
                break
        t.check(fast is not None, "switching to Off mid-test makes the world arrive on time", s)
        t.check(isinstance(s.get("advice"), list), "there's advice in plain words", s)
    finally:
        m.call("multiplayer_test", action="stop")
        m.call("network_sim", action="set", preset="Off")
