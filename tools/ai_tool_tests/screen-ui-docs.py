TITLE = 'The engine reference explains screen UI and the cops and robbers API'


def run(m, t):
    d = m.call("get_engine_info", topic="concepts")
    text = d.get("text", "") if t.ok(d) else ""
    for word in ("ScreenGui", "TextLabel", "player.PlayerGui", "RichText", "LoadCharacter", "team:GetPlayers()", "TouchEnded", "PlayerListStyle", "CoreGuiType.PlayerList"):
        t.check(word in text, "concepts mention " + word, None)
    c = m.call("get_engine_info", topic="capabilities")
    ui = ((c.get("manifest") or {}).get("capabilities") or {}).get("ui") if t.ok(c) else None
    t.check(isinstance(ui, str) and "TextLabel" in ui and "PlayerGui" in ui, "capabilities.ui says what UI there is", ui)
    # A script can really make text on the screen, in a player's own PlayerGui.
    r = m.call("run_lua", code='local g = Instance.new("ScreenGui") local l = Instance.new("TextLabel", g) l.RichText = true '
                               'l.Text = "<b>Cops</b> win" g.Parent = game:GetService("StarterGui") print(l.ContentText) g:Destroy()')
    t.check(t.ok(r) and "Cops win" in str(r), "run_lua makes a TextLabel with RichText", r)
