import os
import tempfile

TITLE = "Export As writes 3D model and Roblox files"


def lua(m, code):
    d = m.call("run_lua", code=code)
    return (d.get("output") or [""])[-1]


def run(m, t):
    lua(m, """
local car = Instance.new('Model'); car.Name = 'ExportCar'; car.Parent = workspace
local body = Instance.new('Part'); body.Name = 'ExportBody'; body.Size = Vector3.new(4, 1, 2)
body.Position = Vector3.new(60, 0.5, 60); body.Color = Color3.new(1, 0, 0); body.Anchored = true; body.Parent = car
local wheel = Instance.new('Part'); wheel.Name = 'ExportWheel'; wheel.Shape = Enum.PartType.Ball; wheel.Size = Vector3.new(2, 2, 2)
wheel.Position = Vector3.new(63, 1, 60); wheel.Anchored = true; wheel.Parent = car
local s = Instance.new('Script'); s.Parent = car""")
    folder = tempfile.mkdtemp(prefix="gb-export-")
    for fmt in ("obj", "fbx", "glb", "gltf", "mesh", "stl", "ply", "dae", "rbxmx"):
        d = m.call("export_model", format=fmt, objects=["Workspace.ExportCar"], folder=folder)
        path = d.get("path", "")
        wrote = t.ok(d) and path.endswith("ExportCar." + fmt) and os.path.isfile(path) and d.get("bytes", 0) > 100
        if fmt != "rbxmx":
            wrote = wrote and d.get("parts") == 2
        t.check(wrote, "export_model writes a ." + fmt + " of the car's 2 parts", d)
    t.check(os.path.isfile(os.path.join(folder, "ExportCar.mtl")), "an .obj comes with its .mtl (the colours)")
    with open(os.path.join(folder, "ExportCar.mesh"), "rb") as f:
        t.check(f.read(13) == b"version 2.00\n", "a .mesh is a Roblox mesh file")

    d = m.call("export_model", format="rbxlx", name="ExportWholeGame", folder=folder)
    t.check(t.ok(d) and os.path.isfile(d.get("path", "")), "rbxlx exports the whole game as a Roblox place", d)
    d = m.call("export_model", format="blend", objects=["Workspace.ExportCar"])
    t.check(t.code(d) == "INVALID_INPUT" and "fbx" in t.choices(d), "an unknown type lists the ones it can write", d)
    d = m.call("export_model", format="obj", objects=["Workspace.ExportCar.Script"], folder=folder)
    t.check(t.code(d) == "EXPORT_FAILED", "a script alone has nothing to export", d)
    lua(m, "workspace.ExportCar:Destroy()")
