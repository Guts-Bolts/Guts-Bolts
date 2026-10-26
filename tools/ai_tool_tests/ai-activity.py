TITLE = 'AI Activity: change report, log, asking first'


def run(m, t):
    d = m.call("ai_activity", action="keep")
    t.check(t.ok(d), "ai_activity keep", d)
    d = m.call("ai_activity")
    t.check(t.ok(d) and d.get("pending") is False and d["added"] == [], "nothing to report after keep", d)
    box = m.call("insert_object", kind="Part", name="AiReportBox")
    bid = box["object_id"]
    d = m.call("ai_activity")
    t.check(t.ok(d) and d.get("pending") and [a["object_id"] for a in d["added"]] == [bid], "the new part is in the report", d)
    m.call("ai_activity", action="keep")
    m.call("set_property", object=bid, property="Color", value=[1, 0, 0])
    d = m.call("ai_activity")
    ch = [c for c in d.get("changed", []) if c["object_id"] == bid]
    t.check(t.ok(d) and ch and "Color" in ch[0]["properties"] and not d["added"], "a changed property is in the report", d)
    s = m.call("create_script", name="AiReportScript", source="print('a')\nprint('b')")
    m.call("ai_activity", action="keep")
    m.call("edit_script", object=s["object_id"], source="print('a')\nprint('c')\nprint('d')")
    d = m.call("ai_activity")
    ch = [c for c in d.get("changed", []) if c["object_id"] == s["object_id"]]
    t.check(ch and ch[0].get("lines_added") == 2 and ch[0].get("lines_removed") == 1, "script changes count lines", d)
    d = m.call("delete_object", object=bid)
    t.check(not t.ok(d) and t.code(d) == "NEEDS_PERSON_OK", "delete waits for the person's OK", d)
    d = m.call("get_object", object=bid)
    t.check(t.ok(d), "...and the part is still there", d)
    d = m.call("ai_activity", action="log", limit=10)
    tools = [(e["tool"], e["result"]) for e in d.get("entries", [])]
    t.check(t.ok(d) and len(tools) > 1 and tools[0][0] == "get_object" and tools[1] == ("delete_object", "NEEDS_PERSON_OK") and ("edit_script", "") in tools,
          "the log has every call, newest first", d)
    ins = [e for e in d["entries"] if e["tool"] == "edit_script"]
    t.check(ins and ins[0]["changed"] == 1 and ins[0]["source"] == "AI app", "the log says what each call changed", d)
    m.call("ai_activity", action="keep")
    m.call("insert_object", kind="Part", name="AiUndoMe")
    d = m.call("ai_activity", action="undo_all")
    t.check(t.ok(d) and d.get("undone"), "undo_all", d)
    d = m.call("find_objects", name="AiUndoMe")
    t.check(t.ok(d) and d.get("count") == 0, "undo_all took the new part away", d)
    m.call("undo")
    d = m.call("find_objects", name="AiUndoMe")
    t.check(t.ok(d) and d.get("count") == 1, "undo brings it back", d)
    m.call("ai_activity", action="keep")
    d = m.call("ai_activity", action="undo_all")
    t.check(not t.ok(d) and t.code(d) == "NOTHING_TO_UNDO", "nothing left to undo", d)
