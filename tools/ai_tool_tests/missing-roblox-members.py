TITLE = 'The Roblox members scripts use most work in run_lua (Model:MoveTo, Lighting.TimeOfDay, BindToRenderStep)'


def last(d, t):
    return d.get("output", [""])[-1].split() if t.ok(d) else []


def run(m, t):
    d = m.call("run_lua", code='local m = Instance.new("Model") local p = Instance.new("Part") p.Anchored = true p.Parent = m m.Parent = workspace '
                               'm:MoveTo(Vector3.new(900, 40, 900)) local cf, size = m:GetBoundingBox() print(cf.Position, size.Y) m:Destroy()')
    t.check(" ".join(last(d, t)) == "900, 40, 900 1", "Model:MoveTo and GetBoundingBox", d)

    d = m.call("run_lua", code='local c = game.Lighting.ClockTime game.Lighting.ClockTime = 14.5 print(game.Lighting.TimeOfDay) game.Lighting.ClockTime = c')
    t.check(last(d, t) == ["14:30:00"], "Lighting.TimeOfDay", d)

    d = m.call("run_lua", code='local R = game:GetService("RunService") R:BindToRenderStep("AiCheck", 1, function() end) R:UnbindFromRenderStep("AiCheck") print("ok")')
    t.check(last(d, t) == ["ok"], "BindToRenderStep and UnbindFromRenderStep", d)
