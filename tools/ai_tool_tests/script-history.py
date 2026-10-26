import time

TITLE = 'Script history records what scripts change'


def run(m, t):
    lamp = m.call("insert_object", kind="Part", name="HistLamp")
    lid = lamp["object_id"]
    m.call("create_script", name="HistScript", source=(
        "local p = workspace:WaitForChild('HistLamp')\n"
        "p.AttributeChanged:Connect(function() p.Name = 'HistLampSeen' end)\n"
        "p.Transparency = 0.5\n"
        "p:SetAttribute('Lit', true)\n"))
    d = m.call("script_history", action="clear")
    d = m.call("script_history", action="record")
    t.check(t.ok(d) and d.get("recording") is True and d.get("total") == 0, "script_history record", d)
    m.call("playtest", action="start")
    time.sleep(1.5)
    m.call("playtest", action="stop")
    d = m.call("script_history", object=lid)
    ents = d.get("entries", [])
    sets = [e for e in ents if e["kind"] == "set" and e["what"] == "Transparency"]
    t.check(t.ok(d) and sets and float(sets[0]["before"]) == 0 and float(sets[0]["after"]) == 0.5 and sets[0]["line"] == 3
          and "HistScript" in sets[0]["script"], "Transparency 0 -> 0.5 by HistScript line 3", d)
    evs = [e for e in ents if e["kind"] == "event" and e["what"] == "AttributeChanged"]
    t.check(evs and "HistScript" in evs[0]["script"], "the AttributeChanged event that ran the script", d)
    names = [e for e in ents if e["what"] == "Name"]
    t.check(names and names[0]["after"] == "HistLampSeen" and names[0]["line"] == 2, "the change made inside the event", d)
    d = m.call("script_history", kind="event", script="HistScript")
    t.check(t.ok(d) and d["entries"] and all(e["kind"] == "event" for e in d["entries"]), "kind and script filters", d)
    m.call("script_history", action="stop")
    before = m.call("script_history").get("total")
    m.call("playtest", action="start")
    time.sleep(1.0)
    m.call("playtest", action="stop")
    d = m.call("script_history")
    t.check(t.ok(d) and d.get("recording") is False and d.get("total") == before, "nothing is added when not recording", d)
    d = m.call("script_history", action="clear")
    t.check(t.ok(d) and d.get("total") == 0, "clear", d)
