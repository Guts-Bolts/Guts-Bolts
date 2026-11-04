import os

TITLE = "The AI models a top hat, paints it and makes it a hat"


def run(m, t):
    part = m.call("insert_object", kind="Part", name="AiTopHat")
    t.check(t.ok(part), "a cube to start from", part)
    pid = part["object_id"]

    # Shape it: a tall cylinder with a flat brim round the bottom.
    d = m.call("edit_mesh", object=pid, steps=[
        {"op": "start", "shape": "cylinder", "size": [1.2, 1.2, 1.2]},
        {"op": "extrude", "select": "bottom", "distance": 0},
        {"op": "scale", "select": "bottom", "by": [1.8, 1, 1.8]},
        {"op": "extrude", "select": "bottom", "distance": 0.08},
    ])
    t.check(t.ok(d) and d.get("class") == "MeshPart" and len(d.get("steps", [])) == 4, "edit_mesh does all four steps", d)
    size = d.get("Size", [0, 0, 0])
    t.check(t.ok(d) and abs(size[0] - 2.16) < 0.05 and abs(size[2] - 2.16) < 0.05 and abs(size[1] - 1.28) < 0.05,
            "the part's box grows round the brim (2.16 wide, 1.28 tall)", d)
    t.check(t.ok(d) and d["steps"][1]["picked_faces"] == 1, "\"bottom\" picks the cylinder's bottom face", d)

    # A step that picks nothing fails, and changes nothing.
    before = d.get("faces")
    d = m.call("edit_mesh", object=pid, steps=[{"op": "scale", "select": {"axis": "y", "min": 50}, "by": 2}])
    t.check(not t.ok(d) and t.code(d) == "NOTHING_PICKED", "a step that picks nothing is refused", d)
    d = m.call("edit_mesh", object=pid, steps=[{"op": "shading", "shading": "flat"}])
    t.check(t.ok(d) and d.get("faces") == before, "the failed call left the mesh alone", d)
    d = m.call("edit_mesh", object=pid, steps=[{"op": "wiggle"}])
    t.check(not t.ok(d) and "extrude" in t.choices(d), "an unknown op lists the ones that work", d)
    d = m.call("edit_mesh", object=pid, steps=[{"op": "start", "shape": "teapot"}])
    t.check(not t.ok(d) and "torus" in t.choices(d), "an unknown shape lists the shapes", d)

    # Paint it: black with a red band just above the brim.
    d = m.call("texture_mesh", object=pid, color="black",
               layers=[{"select": {"axis": "y", "from": 0.15, "to": 0.3}, "color": "#cc2222"},
                       {"select": {"axis": "y", "min": 99}, "color": "white"}])
    t.check(t.ok(d) and d.get("Texture", "").endswith(".png") and os.path.isfile(d.get("path", "")), "texture_mesh saves a picture", d)
    t.check(t.ok(d) and d["layers"][0]["pixels"] > 0, "the band layer painted something", d)
    t.check(t.ok(d) and any("Layer 1" in w for w in d.get("warnings", [])), "a layer that picks nothing is warned about", d)
    if t.ok(d) and os.path.isfile(d.get("path", "")):
        with open(d["path"], "rb") as f:
            head = f.read(24)
        t.check(head[:8] == b"\x89PNG\r\n\x1a\n" and int.from_bytes(head[16:20], "big") == 512, "it's a 512 pixel PNG", d)
    d = m.call("texture_mesh", object=pid, layers=[{"pattern": "polka"}])
    t.check(not t.ok(d) and "stripes" in t.choices(d), "an unknown pattern lists the patterns", d)

    # Make it a hat on the mannequin.
    d = m.call("make_accessory", object=pid, kind="hat", name="AI Top Hat")
    t.check(t.ok(d) and d.get("kind") == "hat" and d.get("name") == "AI Top Hat", "make_accessory makes it a hat", d)
    hat = d.get("object_id")
    mannequin = d.get("mannequin_id")
    g = m.call("get_object", object=hat)
    t.check(t.ok(g) and g.get("class") == "Model", "the part went into a Model of its own", g)
    attrs = g.get("attributes", {}) if t.ok(g) else {}
    t.check("AccessoryPlacement" in attrs, "its position on the mannequin is saved", g)
    t.check(t.ok(d) and 2.4 < d["bounds_min"][1] < 2.7, "the hat sits on top of the mannequin's head", d)
    t.check(t.ok(d) and 0.3 < d.get("head_size", [0, 0, 0])[0] < 1.0, "it says how big the head is", d)
    d = m.call("make_accessory", object=hat, kind="cape")
    t.check(not t.ok(d) and "hat" in t.choices(d), "an unknown kind lists the kinds", d)
    m.call("delete_object", object=hat)
    if mannequin:
        m.call("delete_object", object=mannequin)
