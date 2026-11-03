TITLE = 'Guns, Welds and Motor6Ds: the docs explain them and the Blaster is a working gun'


def run(m, t):
    d = m.call("get_engine_info", topic="concepts")
    text = d.get("text", "") if t.ok(d) else ""
    for word in ("GUNS", "Muzzle", "FireServer", "OnServerEvent", "Motor6D", "Right Shoulder", "LimitsEnabled", "CylinderMesh"):
        t.check(word in text, "concepts mention " + word, None)

    # The ready-made gun goes in with its parts, its RemoteEvent and both scripts.
    d = m.call("insert_object", kind="Blaster", parent="StarterPack", name="AiBlaster")
    t.check(t.ok(d), "insert the ready-made Blaster", d)
    r = m.call("run_lua", code='local b = game:GetService("StarterPack"):FindFirstChild("AiBlaster") or workspace:FindFirstChild("AiBlaster", true) or game:GetService("StarterPack"):FindFirstChild("Blaster") or workspace:FindFirstChild("Blaster", true) local s = {} '
                               'for _, c in ipairs(b:GetChildren()) do s[#s + 1] = c.ClassName .. ":" .. c.Name end '
                               'table.sort(s) print(table.concat(s, ","))')
    out = str(r)
    for piece in ("Part:Handle", "Part:Muzzle", "RemoteEvent:Fire", "Script:Script", "LocalScript:LocalScript"):
        t.check(piece in out, "the Blaster has " + piece, r)

    # Old-style joints are real objects with C0 / C1.
    r = m.call("run_lua", code='local w = Instance.new("Motor6D") w.C0 = CFrame.new(0, 1, 0) w.DesiredAngle = 0.5 '
                               'print(w.ClassName, w:IsA("JointInstance"), w.C0.Y, w.DesiredAngle) '
                               'local m = Instance.new("CylinderMesh") print(m.ClassName, m.Scale)')
    t.check(t.ok(r) and "Motor6D true 1" in str(r) and "0.5" in str(r), "run_lua makes a Motor6D with C0 and DesiredAngle", r)
    t.check("CylinderMesh 1" in str(r), "run_lua makes a CylinderMesh with a Scale", r)
