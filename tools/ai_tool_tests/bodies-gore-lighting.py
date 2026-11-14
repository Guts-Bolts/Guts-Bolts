TITLE = 'Lamp shadows, bouncy light and the new gore settings'


def run(m, t):
    lamp = m.call("insert_object", kind="PointLight", name="ShadowTestLamp")
    t.check(t.ok(lamp), "a lamp for the shadows", lamp)
    lid = lamp["object_id"]
    d = m.call("set_property", object=lid, property="Shadows", value=True)
    t.check(t.ok(d), "turn its Shadows on", d)
    d = m.call("get_object", object=lid)
    t.check(t.ok(d) and d.get("properties", {}).get("Shadows") is True, "get_object reads Shadows back", d.get("properties"))
    d = m.call("run_lua", code="game.Lighting.BounceLight = 1; workspace.LiquidBlood = true; workspace.SoftTissue = true; "
                               "print(game.Lighting.BounceLight, workspace.LiquidBlood, workspace.SoftTissue)")
    out = d.get("output", [""])[-1].split()
    t.check(t.ok(d) and len(out) == 3 and float(out[0]) == 1 and out[1:] == ["true", "true"], "Lua sets Bouncy Light, Liquid Blood and Soft Tissue", d)
    m.call("run_lua", code="game.Lighting.BounceLight = 0; workspace.LiquidBlood = false")
    m.call("delete_object", object=lid)
