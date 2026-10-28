TITLE = 'Vehicles: the ready-made Car and a VehicleSeat'


def run(m, t):
    d = m.call("insert_object", kind="Car", name="AiCar")
    t.check(t.ok(d), "insert the ready-made Car", d)
    d = m.call("find_objects", name="VehicleSeat")
    t.check(t.ok(d) and d.get("count", 0) >= 1, "it has a VehicleSeat", d)
    d = m.call("run_lua", code=(
        "local n = 0 for _, x in ipairs(workspace:GetDescendants()) do "
        "if x:IsA('HingeConstraint') and x.Parent and x.Parent.Name == 'Chassis' then n = n + 1 end end "
        "local s = workspace:FindFirstChild('VehicleSeat', true) "
        "print(s.ClassName, s.MaxSpeed, n)"))
    out = " ".join(str(x) for x in d.get("output") or []).strip() if t.ok(d) else ""
    t.check(out.startswith("VehicleSeat 14") and out.endswith(" 4"), "scripts see a VehicleSeat with MaxSpeed 14 and 4 wheel motors", d)
    d = m.call("insert_object", kind="VehicleSeat", name="AiSeat")
    t.check(t.ok(d), "Insert Object > VehicleSeat", d)
