TITLE = 'Weathering Heights: Fog, Blizzard and Sandstorm weather, terrain styles'


def run(m, t):
    for kind in ("Fog", "Blizzard", "Sandstorm"):
        d = m.call("set_atmosphere", weather=kind)
        t.check(t.ok(d) and d.get("lighting", {}).get("Weather") == kind, "set_atmosphere takes weather " + kind, d)
    d = m.call("set_atmosphere", weather="Clear")
    t.check(t.ok(d) and d.get("lighting", {}).get("Weather") == "Clear", "and back to clear", d)
