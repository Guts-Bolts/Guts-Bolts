TITLE = 'Your own materials (MaterialVariant)'


def run(m, t):
    v = m.call("insert_object", kind="MaterialVariant", name="TestBrick")
    t.check(t.ok(v) and v.get("path") == "MaterialService.TestBrick" and v.get("class") == "MaterialVariant",
          "a MaterialVariant goes in MaterialService", v)
    vid = v["object_id"]
    for prop, val in [("BaseMaterial", "Concrete"), ("ColorMap", "images/brick.png"), ("StudsPerTile", 6), ("Roughness", 0.2),
                      ("Glow", 1.5)]:
        d = m.call("set_property", object=vid, property=prop, value=val)
        t.check(t.ok(d), f"set {prop}", d)
    d = m.call("get_object", object=vid)
    props = d.get("properties", {})
    t.check(t.ok(d) and props.get("BaseMaterial") == "Concrete" and props.get("ColorMap") == "images/brick.png"
          and props.get("StudsPerTile") == 6 and abs(props.get("Roughness", 0) - 0.2) < 1e-6 and props.get("Glow") == 1.5,
          "get_object reads them back", props)
    d = m.call("set_property", object=vid, property="BaseMaterial", value="Cheese")
    t.check(not t.ok(d), "an unknown BaseMaterial is refused", d)
    part = m.call("insert_object", kind="Part", name="BrickWall")
    m.call("set_property", object=part["object_id"], property="Material", value="Concrete")
    d = m.call("set_property", object=part["object_id"], property="MaterialVariant", value="TestBrick")
    t.check(t.ok(d), "a part's MaterialVariant", d)
    d = m.call("run_lua", code=f"local p = __gb_byId({part['object_id'][1:]}) local ms = game:GetService('MaterialService') "
                               "local v = Instance.new('MaterialVariant') v.Name = 'FromLua' v.BaseMaterial = 'Metal' v.Metalness = 0.8 "
                               "v.Parent = ms print(p.MaterialVariant, ms.TestBrick.BaseMaterial, ms.FromLua.Metalness, ms.FromLua.ClassName)")
    out = d.get("output", [""])[-1].split()
    t.check(t.ok(d) and len(out) == 4 and out[:2] == ["TestBrick", "Concrete"] and abs(float(out[2]) - 0.8) < 1e-4
          and out[3] == "MaterialVariant", "Lua sees them", d)
    d = m.call("validate_scene")
    t.check(t.ok(d), "validate_scene with MaterialVariants", d)
