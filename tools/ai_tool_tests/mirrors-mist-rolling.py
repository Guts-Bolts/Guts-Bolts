TITLE = 'Height fog, air density and round cylinders'


def run(m, t):
    d = m.call("run_lua", code="game.Lighting.FogFalloff = 0.05; game.Lighting.FogHeight = 4; workspace.AirDensity = 0.002; "
                               "print(game.Lighting.FogFalloff, game.Lighting.FogHeight, workspace.AirDensity)")
    out = d.get("output", [""])[-1].split()
    t.check(t.ok(d) and len(out) == 3 and abs(float(out[0]) - 0.05) < 1e-5 and float(out[1]) == 4
            and abs(float(out[2]) - 0.002) < 1e-6, "Lua sets height fog and the air's thickness", d)
    m.call("run_lua", code="game.Lighting.FogFalloff = 0; game.Lighting.FogHeight = 0; workspace.AirDensity = 0.0012")
    d = m.call("run_lua", code="local p = Instance.new('Part'); p.Shape = Enum.PartType.Cylinder; p.Name = 'RollTestBarrel'; "
                               "p.Parent = workspace; print(p.Shape); p:Destroy()")
    t.check(t.ok(d) and d.get("output", [""])[-1] == "Cylinder", "a script can make a cylinder part", d)
