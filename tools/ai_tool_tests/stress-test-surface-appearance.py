TITLE = 'Stress test (memory + physics on a weaker device) and SurfaceAppearance from pictures'


def run(m, t):
    d = m.call("stress_test", device="cheap_phone", crates=200, seconds=1)
    t.check(t.ok(d) and d.get("device", "").startswith("Cheap phone"), "stress_test runs for a cheap phone", d)
    mem = d.get("memory", {}) if t.ok(d) else {}
    phys = d.get("physics", {}) if t.ok(d) else {}
    t.check(mem.get("estimate_mb", 0) >= 200 and mem.get("budget_mb", 0) > 0 and mem.get("verdict") in ("ok", "close", "over"),
            "memory has an estimate, a budget and a verdict", mem)
    t.check(len(mem.get("rows", [])) == 6, "memory is split into 6 rows", mem)
    t.check(phys.get("loose_parts", 0) >= 200 and phys.get("with_crates_ms", -1) >= 0 and phys.get("verdict") in ("ok", "close", "over"),
            "the crates are in the physics and it was timed", phys)
    t.check(phys.get("on_device_ms", 0) >= phys.get("with_crates_ms", 0), "the device is guessed slower than this computer", phys)
    t.check(d.get("queries", {}).get("raycasts") == 500 and "network" in d and isinstance(d.get("warnings"), list),
            "raycasts, network and warnings are there", d)

    f = m.call("find_objects", name="StressCrate")
    t.check(t.ok(f) and f.get("count") == 0, "the crates were only in a copy (the game didn't change)", f)

    light = m.call("stress_test", device="mid_phone", background="light", crates=0)
    heavy = m.call("stress_test", device="mid_phone", background="heavy", crates=0)
    t.check(t.ok(light) and t.ok(heavy) and heavy["memory"]["budget_mb"] < light["memory"]["budget_mb"],
            "apps in the background leave less memory", (light.get("memory"), heavy.get("memory")))
    d = m.call("stress_test", device="toaster")
    t.check(not t.ok(d), "an unknown device is refused", d)
    d = m.call("stress_test", background="loads")
    t.check(not t.ok(d), "an unknown background is refused", d)

    d = m.call("get_engine_info", topic="concepts")
    text = d.get("text", "") if t.ok(d) else ""
    t.check("CreateSurfaceAppearanceAsync" in text and "SurfaceAppearance" in text, "concepts explain SurfaceAppearance", None)

    d = m.call("run_lua", code=(
        'local AS = game:GetService("AssetService") '
        'local img = AS:CreateEditableImage({Size = Vector2.new(16, 16)}) '
        'local sa = AS:CreateSurfaceAppearanceAsync({ColorMap = img, NormalMap = 1234, RoughnessMap = "rbxassetid://99"}) '
        'local p = Instance.new("Part") p.Name = "SurfaceTestPart" p.Parent = workspace sa.Parent = p '
        'print(sa.ClassName, sa.ColorMap == img.ContentId, sa.NormalMap, sa.RoughnessMap, sa.MetalnessMap == "")'))
    out = d.get("output", [""])[-1].split() if t.ok(d) else []
    t.check(out == ["SurfaceAppearance", "true", "gb:1234", "gb:99", "true"],
            "CreateSurfaceAppearanceAsync mixes an EditableImage and picture IDs", d)
    d = m.call("run_lua", code='local ok, err = pcall(function() return game:GetService("AssetService"):CreateSurfaceAppearanceAsync({Shine = 1}) end) print(ok)')
    out = d.get("output", [""])[-1].split() if t.ok(d) else []
    t.check(out == ["false"], "an unknown picture name is refused", d)

    f = m.call("find_objects", name="SurfaceAppearance")
    t.check(t.ok(f) and f.get("count", 0) >= 1, "the SurfaceAppearance is in the game", f)
    d = m.call("run_lua", code='workspace.SurfaceTestPart:Destroy()')
    t.check(t.ok(d), "clean up", d)
