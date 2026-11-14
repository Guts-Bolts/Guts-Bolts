TITLE = 'Severe weather: the Supercell weather'


def run(m, t):
    d = m.call("set_atmosphere", weather="Supercell")
    t.check(t.ok(d) and d.get("lighting", {}).get("Weather") == "Supercell", "set_atmosphere takes weather Supercell", d)
    d = m.call("set_atmosphere", weather="Clear")
    t.check(t.ok(d) and d.get("lighting", {}).get("Weather") == "Clear", "and back to clear", d)
