TITLE = 'Thunderstorms: the Storm weather and the stormy mood'


def run(m, t):
    d = m.call("set_atmosphere", weather="Storm")
    t.check(t.ok(d) and d.get("lighting", {}).get("Weather") == "Storm", "set_atmosphere takes weather Storm", d)
    d = m.call("set_atmosphere", weather="Clear")
    t.check(t.ok(d) and d.get("lighting", {}).get("Weather") == "Clear", "and back to clear", d)
    d = m.call("set_atmosphere", preset="stormy")
    li = d.get("lighting", {})
    t.check(t.ok(d) and li.get("Weather") == "Storm" and li.get("FogEnabled") is True, "the stormy mood is a thunderstorm", d)
    d = m.call("set_atmosphere", preset="day")
    t.check(t.ok(d), "back to a clear day", d)
