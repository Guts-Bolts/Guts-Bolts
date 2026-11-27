TITLE = 'Graphics overhaul: Lighting.ToneMapper and Lighting.PhysicalSky'


def run(m, t):
    d = m.call("run_lua", code="print(game.Lighting.ToneMapper, game.Lighting.PhysicalSky)")
    t.check(t.ok(d) and d.get("output", [""])[-1].split() == ["AgX", "true"], "new places use AgX and real air", d)
    d = m.call("run_lua", code="game.Lighting.ToneMapper = 'aces'; game.Lighting.PhysicalSky = false; "
                               "print(game.Lighting.ToneMapper, game.Lighting.PhysicalSky)")
    t.check(t.ok(d) and d.get("output", [""])[-1].split() == ["ACES", "false"], "Lua switches back to the old look", d)
    d = m.call("run_lua", code="local ok = pcall(function() game.Lighting.ToneMapper = 'Sepia' end); print(ok)")
    t.check(t.ok(d) and d.get("output", [""])[-1] == "false", "an unknown tone mapper is an error", d)
    m.call("run_lua", code="game.Lighting.ToneMapper = 'AgX'; game.Lighting.PhysicalSky = true")
