TITLE = 'Jiggle, LookAt and IK bones'


def run(m, t):
    part = m.call("insert_object", kind="Part", name="RigTestTail")
    t.check(t.ok(part), "a part for the bones", part)
    pid = part["object_id"]
    bone = m.call("insert_object", kind="Bone", parent=pid, name="TailBone")
    t.check(t.ok(bone) and bone.get("class") == "Bone", "a Bone goes inside the part", bone)
    bid = bone["object_id"]
    for prop, val in [("BoneType", "Jiggle"), ("Stiffness", 0.7), ("Damping", 0.4), ("Gravity", 5)]:
        d = m.call("set_property", object=bid, property=prop, value=val)
        t.check(t.ok(d), f"set {prop}", d)
    d = m.call("get_object", object=bid)
    props = d.get("properties", {})
    t.check(t.ok(d) and props.get("BoneType") == "Jiggle" and abs(props.get("Stiffness", 0) - 0.7) < 1e-6
          and props.get("Gravity") == 5, "get_object reads a jiggle bone back", props)
    d = m.call("set_property", object=bid, property="BoneType", value="Wobbly")
    t.check(not t.ok(d), "an unknown BoneType is refused", d)
    goal = m.call("insert_object", kind="Part", name="RigTestGoal")
    m.call("set_property", object=bid, property="BoneType", value="IK")
    d = m.call("set_property", object=bid, property="Target", value=goal["object_id"])
    t.check(t.ok(d), "an IK bone's Target", d)
    d = m.call("run_lua", code=f"local b = __gb_byId({bid[1:]}) print(b.BoneType, b.Target.Name, b.ChainLength)")
    out = d.get("output", [""])[-1].split()
    t.check(t.ok(d) and out == ["IK", "RigTestGoal", "2"], "Lua sees the bone's type and target", d)
    d = m.call("validate_scene")
    t.check(t.ok(d), "validate_scene with bones", d)
    m.call("delete_object", object=goal["object_id"])
    m.call("delete_object", object=pid)
