TITLE = 'Instance.new makes the common Roblox classes (ObjectValue, MeshPart, Atmosphere...) in run_lua'


def last(d, t):
    return d.get("output", [""])[-1].split() if t.ok(d) else []


def run(m, t):
    d = m.call("run_lua", code='local o = Instance.new("ObjectValue") o.Value = workspace print(o.ClassName, o.Value == workspace)')
    t.check(last(d, t) == ["ObjectValue", "true"], "ObjectValue keeps an object", d)

    d = m.call("run_lua", code='local p = Instance.new("MeshPart") print(p.ClassName, p:IsA("BasePart")) p:Destroy()')
    t.check(last(d, t) == ["MeshPart", "true"], "MeshPart is a part", d)

    d = m.call("run_lua", code='local c = Instance.new("Configuration") print(c.ClassName, c:IsA("Model"))')
    t.check(last(d, t) == ["Configuration", "false"], "Configuration is its own class", d)

    d = m.call("run_lua", code='local s = Instance.new("SelectionBox") s.LineThickness = 0.3 print(s.LineThickness)')
    t.check(last(d, t) == ["0.3"], "a class we don't draw yet still keeps its properties", d)
