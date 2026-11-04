TITLE = "create_script and edit_script point out likely mistakes"


def run(m, t):
    d = m.call("create_script", name="AiTypoScript", source="pirnt('hi')\nlocal n = math.flor(2.5)\n")
    mistakes = d.get("mistakes") or []
    t.check(t.ok(d) and len(mistakes) == 2 and mistakes[0]["line"] == 1 and mistakes[0].get("fix") == "print"
            and mistakes[1].get("fix") == "floor" and d.get("warnings"),
            "a misspelt name and math.flor come back as mistakes with fixes", d)
    e = m.call("edit_script", object=d.get("object_id"), source="print('hi')\nlocal n = math.floor(2.5)\n")
    t.check(t.ok(e) and not e.get("mistakes") and not e.get("warnings"), "fixed: no mistakes left", e)
    e = m.call("edit_script", object=d.get("object_id"), old="print('hi')", new="workspace.Baseplate.Destroy()")
    t.check(t.ok(e) and any(x.get("fix") == ":Destroy" for x in e.get("mistakes") or []),
            "a dot where a colon goes is a mistake too", e)
