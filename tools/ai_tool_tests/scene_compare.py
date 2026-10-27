TITLE = "Scene compare and merge"


def lua(m, code):
    d = m.call("run_lua", code=code)
    return (d.get("output") or [""])[-1]


def find(d, name):
    return [c for c in d.get("changes", []) if c["name"] == name]


def run(m, t):
    lua(m, """
for _, n in ipairs({'CmpKeep', 'CmpMove', 'CmpColor', 'CmpGone'}) do
  local p = Instance.new('Part'); p.Name = n; p.Anchored = true; p.Position = Vector3.new(40, 1, 40 + #n); p.Parent = workspace
end""")
    d = m.call("compare_scenes", action="save_copy", name="CmpTestBase")
    t.check(t.ok(d) and d["path"].endswith("CmpTestBase.gbscene"), "save_copy keeps a copy of the game", d)
    d = m.call("compare_scenes", action="compare", other="CmpTestBase")
    t.check(t.ok(d) and d["changes"] == [], "a game and its fresh copy have no differences", d)

    lua(m, """
workspace.CmpMove.Position = workspace.CmpMove.Position + Vector3.new(0, 5, 0)
workspace.CmpColor.Color = Color3.new(1, 0, 0)
workspace.CmpGone:Destroy()
local p = Instance.new('Part'); p.Name = 'CmpNew'; p.Anchored = true; p.Parent = workspace""")
    d = m.call("compare_scenes", action="compare", other="CmpTestBase")
    kinds = {c["name"]: c["kind"] for c in d.get("changes", [])}
    t.check(t.ok(d) and kinds.get("CmpMove") == "Moved" and kinds.get("CmpColor") == "Changed"
            and kinds.get("CmpGone") == "Added" and kinds.get("CmpNew") == "Removed" and "CmpKeep" not in kinds,
            "it finds the moved, changed, deleted and new objects", d)
    color = find(d, "CmpColor")
    t.check(color and any(f["name"] == "Color" for f in color[0]["fields"]), "a change lists the property, before and after", color)
    pick = [c["index"] for c in d["changes"] if c["name"] in ("CmpGone", "CmpColor")]
    d = m.call("compare_scenes", action="merge", changes=pick)
    out = lua(m, "local c = workspace.CmpColor.Color print(workspace:FindFirstChild('CmpGone') ~= nil, c.G > 0.5, "
                 "workspace:FindFirstChild('CmpNew') ~= nil, math.floor(workspace.CmpMove.Position.Y))")
    t.check(t.ok(d) and d["done"] == 2 and out == "true true true 6",
            "merge brings back only the ticked ones (the deleted part and the colour), the rest stays", (d, out))
    d = m.call("compare_scenes", action="compare", other="CmpTestBase")
    t.check(t.ok(d) and not find(d, "CmpGone") and find(d, "CmpMove"), "the part brought back is the same object again", d)

    # Two people: both start from CmpTestStart; "they" change CmpKeep's size and CmpMove's colour.
    m.call("compare_scenes", action="save_copy", name="CmpTestStart")
    lua(m, "workspace.CmpKeep.Size = Vector3.new(6, 1, 6) workspace.CmpMove.Color = Color3.new(0, 0, 1)")
    m.call("compare_scenes", action="save_copy", name="CmpTestTheirs")
    m.call("compare_scenes", action="compare", other="CmpTestStart")
    m.call("compare_scenes", action="merge")   # (back to where both started)
    t.check(float(lua(m, "print(workspace.CmpKeep.Size.X)")) == 1, "merging everything makes it like the other version", None)
    lua(m, "workspace.CmpMove.Transparency = 0.5")   # "you" changed CmpMove too
    d = m.call("compare_scenes", action="compare", other="CmpTestTheirs", base="CmpTestStart")
    keep, move = find(d, "CmpKeep"), find(d, "CmpMove")
    t.check(t.ok(d) and d["three_way"] and len(d["changes"]) == 2 and keep and not keep[0]["conflict"]
            and move and move[0]["conflict"] and "too" in move[0]["why"],
            "with the starting version, only their changes are listed and the one you both changed is a conflict", d)
    d = m.call("compare_scenes", action="merge")
    out = lua(m, "print(workspace.CmpKeep.Size.X, workspace.CmpMove.Transparency, workspace.CmpMove.Color.B > 0.9)")
    t.check(t.ok(d) and d["done"] == 1 and out.split() == ["6.0", "0.5", "false"], "merge leaves conflicts alone unless they're picked", (d, out))

    d = m.call("compare_scenes", action="compare", other="NoSuchGameHere")
    t.check(t.code(d) == "CANT_COMPARE", "a game that doesn't exist is refused", d)
    d = m.call("compare_scenes", action="merge")
    t.check(t.code(d) == "NOTHING_TO_MERGE", "merge needs a compare first", d)
    lua(m, "for _, n in ipairs({'CmpKeep', 'CmpMove', 'CmpColor', 'CmpGone', 'CmpNew'}) do local p = workspace:FindFirstChild(n) if p then p:Destroy() end end")
