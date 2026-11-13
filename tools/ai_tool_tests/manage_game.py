TITLE = "manage_game: looks after the published game, and changes wait for the person"

# Studio runs offline in these tests (no server), so this checks the tool's shape and its
# refusals. The requests it sends are the website's own (game.settings, pass.create...).


def run(m, t):
    tools = m.rpc("tools/list").get("tools", [])
    tool = next((x for x in tools if x.get("name") == "manage_game"), None)
    t.check(tool is not None, "manage_game is listed", [x.get("name") for x in tools][:80])
    if tool:
        desc = tool.get("description", "")
        t.check("press" in desc and "Allow" in desc, "its description says changes wait for the person's Allow", desc[:300])
        actions = tool.get("inputSchema", {}).get("properties", {}).get("action", {}).get("enum", [])
        t.check({"stats", "make_pass", "settings", "result"} <= set(actions), "it has read, change and result actions", actions)
    d = m.call("manage_game", action="explode")
    t.check(t.code(d) == "INVALID_INPUT", "an unknown action is refused", d)
    d = m.call("manage_game", action="result", job="999")
    t.check(t.code(d) == "NOT_FOUND", "an unknown job is refused", d)
    d = m.call("manage_game", action="make_pass", fields={"name": "VIP", "price": 100})
    t.check(t.code(d) == "NOT_SIGNED_IN", "nothing happens when Studio isn't signed in", d)
