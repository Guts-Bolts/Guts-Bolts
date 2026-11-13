TITLE = 'Realism settings, real sounds and workspace:FireBullet'


def run(m, t):
    d = m.call("run_lua", code="workspace.Stamina = true; workspace.FireSpread = true; game.Lighting.MotionBlur = 0.5; "
                               "print(workspace.Stamina, workspace.FireSpread, workspace.Injuries, workspace.Acoustics, "
                               "game.Lighting.MotionBlur, game.Lighting.AutoExposure)")
    out = d.get("output", [""])[-1].split()
    t.check(t.ok(d) and out[:4] == ["true", "true", "true", "true"] and float(out[4]) == 0.5 and out[5] == "true",
            "Lua turns on Stamina, Fire Spread and Motion Blur", d)
    m.call("run_lua", code="workspace.Stamina = false; workspace.FireSpread = false; game.Lighting.MotionBlur = 0")
    d = m.call("run_lua", code="local s = Instance.new('Sound'); s.SoundId = 'hit_metal'; s.Parent = workspace; "
                               "print(s.SoundId); s:Destroy()")
    t.check(t.ok(d) and d.get("output", [""])[-1] == "hit_metal", "a Sound can use a recorded sound by name", d)
    d = m.call("run_lua", code="local p = Instance.new('Part'); p.Name = 'BulletTestPlank'; p.Anchored = true; "
                               "p.Material = Enum.Material.Wood; p.Size = Vector3.new(0.5, 6, 6); p.Position = Vector3.new(10, 500, 0); "
                               "p.Parent = workspace; "
                               "local r = workspace:FireBullet(Vector3.new(0, 500, 0), Vector3.new(900, 0, 0)); "
                               "print(#r.Hits > 0 and r.Hits[1].Instance.Name, r.Hits[1] and r.Hits[1].Pierced); p:Destroy()")
    out = d.get("output", [""])[-1].split()
    t.check(t.ok(d) and out == ["BulletTestPlank", "true"], "workspace:FireBullet goes through a wooden plank", d)
