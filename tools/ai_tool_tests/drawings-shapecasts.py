TITLE = 'Shapecasts and drawing pictures: the docs explain them and Lua can use them'


def run(m, t):
    d = m.call("get_engine_info", topic="concepts")
    text = d.get("text", "") if t.ok(d) else ""
    for word in ("Spherecast", "Blockcast", "SizeGradient", "CreateEditableImage", "PromptPublishImageAsync"):
        t.check(word in text, "concepts mention " + word, None)

    d = m.call("run_lua", code='local img = game:GetService("AssetService"):CreateEditableImage({Size = Vector2.new(32, 32)}) '
                               'img:DrawCircle(Vector2.new(16, 16), 5, Color3.new(1, 0, 0), 0) '
                               'local c = img:GetPixel(Vector2.new(16, 16)) img:Destroy() print(c.R > 0.99, c.G < 0.01)')
    out = d.get("output", [""])[-1].split() if t.ok(d) else []
    t.check(out == ["true", "true"], "Lua draws a red dot on an EditableImage", d)

    d = m.call("run_lua", code='local r = workspace:Spherecast(Vector3.new(0, 500, 0), 1, Vector3.new(0, 1, 0)) print(r == nil)')
    out = d.get("output", [""])[-1].split() if t.ok(d) else []
    t.check(out == ["true"], "a shapecast into the empty sky finds nothing", d)
