import time

TITLE = 'Physics workbench: joints that snap, force zones'


def run(m, t):
    setup = """
local function part(name, pos, size, anchored)
  local p = Instance.new('Part'); p.Name = name; p.Position = pos; p.Size = size; p.Anchored = anchored; p.Parent = workspace
  return p
end
local post = part('PwPost', Vector3.new(40, 20, 40), Vector3.new(2, 2, 2), true)
local weak = part('PwWeak', Vector3.new(40, 18.5, 40), Vector3.new(1, 1, 1), false)
weak.Density = 50
local w1 = Instance.new('WeldConstraint'); w1.Name = 'PwWeakWeld'; w1.Part0 = post; w1.Part1 = weak; w1.BreakForce = 100; w1.Parent = post
local post2 = part('PwPost2', Vector3.new(46, 20, 40), Vector3.new(2, 2, 2), true)
local strong = part('PwStrong', Vector3.new(46, 18.5, 40), Vector3.new(1, 1, 1), false)
local w2 = Instance.new('WeldConstraint'); w2.Name = 'PwStrongWeld'; w2.Part0 = post2; w2.Part1 = strong; w2.BreakForce = 1e7; w2.Parent = post2
local zone = part('PwZone', Vector3.new(-40, 6, -40), Vector3.new(8, 12, 8), true)
zone.CanCollide = false; zone.Transparency = 0.8
zone:AddTag('ForceZone'); zone:SetAttribute('Mode', 'Wind'); zone:SetAttribute('Strength', 400)
local box = part('PwFloater', Vector3.new(-40, 1, -40), Vector3.new(1, 1, 1), false)
print(w1.BreakForce, w1.Broken)
"""
    d = m.call("run_lua", code=setup)
    t.check(t.ok(d) and d.get("output") and d["output"][-1] in ("100 false", "100.0 false"), "BreakForce / Broken from Lua", d)
    d = m.call("get_object", object="Workspace.PwPost.PwWeakWeld")
    t.check(t.ok(d) and d.get("properties", {}).get("BreakForce") == 100, "get_object shows BreakForce", d)
    d = m.call("playtest", action="simulate")
    t.check(t.ok(d), "simulate", d)
    time.sleep(6)
    d = m.call("run_lua", code="local w = workspace.PwPost.PwWeakWeld; local s = workspace.PwPost2.PwStrongWeld\n"
                               "local weak = workspace:FindFirstChild('PwWeak')  -- (gone = fell out of the world)\n"
                               "print(w.Broken, w.Enabled, s.Broken, s.Enabled, weak and math.floor(weak.Position.Y) or -999,"
                               " math.floor(workspace.PwStrong.Position.Y), math.floor(workspace.PwFloater.Position.Y))")
    out = (d.get("output") or [""])[-1].split()
    t.check(t.ok(d) and out[:4] == ["true", "false", "false", "true"], "the weak weld snapped, the strong one held", d)
    t.check(len(out) == 7 and float(out[4]) < 15 and float(out[5]) >= 17, "the snapped part fell, the held one didn't", out)
    t.check(len(out) == 7 and float(out[6]) > 3, "the wind zone lifted the loose box", out)
    m.call("playtest", action="stop")
    d = m.call("run_lua", code="print(workspace.PwPost.PwWeakWeld.Enabled, workspace.PwPost.PwWeakWeld.BreakForce)")
    t.check(t.ok(d) and d["output"][-1] in ("true 100", "true 100.0"), "after Stop the weld is back (and still breakable)", d)
