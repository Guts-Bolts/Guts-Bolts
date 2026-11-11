TITLE = 'Test like a phone: phone_check budgets'


def run(m, t):
    d = m.call("phone_check", device="cheap_phone")
    t.check(t.ok(d) and d.get("device") == "Cheap phone" and len(d.get("checks", [])) == 7, "phone_check cheap_phone", d)
    names = [c["what"] for c in d.get("checks", [])]
    t.check("Parts" in names and "Lights" in names and "Triangles" in names, "the checks name parts, lights, triangles", d)
    t.check(all(c["verdict"] in ("ok", "close", "over") for c in d.get("checks", [])), "every check has a verdict", d)
    lights = [c for c in d["checks"] if c["what"] == "Lights"][0]
    for i in range(lights["limit"] + 2):
        m.call("insert_object", kind="PointLight", name="PhoneLamp%d" % i)
    d = m.call("phone_check", device="cheap_phone")
    lights = [c for c in d.get("checks", []) if c["what"] == "Lights"][0]
    t.check(lights["verdict"] == "over" and lights.get("fix") and d.get("over", 0) >= 1, "too many lights is 'over', with a fix", d)
    d2 = m.call("phone_check", device="old_laptop")
    l2 = [c for c in d2.get("checks", []) if c["what"] == "Lights"][0]
    t.check(t.ok(d2) and l2["limit"] > lights["limit"], "an old laptop allows more lights", d2)
    d = m.call("phone_check", device="toaster")
    t.check(not t.ok(d), "an unknown device is refused", d)
    for i in range(lights["limit"] + 2):
        m.call("undo")
    d = m.call("find_objects", name="PhoneLamp0")
    t.check(t.ok(d) and d.get("count") == 0, "undo takes the test lamps away", d)
