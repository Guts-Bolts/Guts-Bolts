-- Checks BallSocketConstraint, PrismaticConstraint and CylindricalConstraint with real
-- physics in a play test. Run it in Studio's play test (the Command Bar runs it inside the game):
--
--   xvfb-run -a build/GutsAndBolts --play --test-command "$(cat tools/test_joints.lua)" \
--       --screenshot /tmp/joints.png --frames 1500
--
-- Every check prints "PASS ..." or "FAIL ..." to the Output, then "JOINTS TEST DONE".
task.spawn(function()
    local fails = 0
    local function check(ok, what, extra)
        if ok then print("PASS " .. what)
        else fails = fails + 1 warn("FAIL " .. what .. (extra and ("  (" .. tostring(extra) .. ")") or "")) end
    end
    local function frames(n) for _ = 1, n or 3 do RunService.Heartbeat:Wait() end end
    local function near(a, b, eps) return math.abs(a - b) <= (eps or 1e-3) end

    -- Out of the way of the character (it starts near the middle of the world).
    local X0 = 300
    local function block(name, pos, size, anchored)
        local p = Instance.new("Part")
        p.Name = name
        p.Size = size or Vector3.new(1, 1, 1)
        p.Position = pos
        p.Anchored = anchored
        p.Parent = workspace
        return p
    end
    local function attach(part, worldPos, orientation)
        local a = Instance.new("Attachment")
        a.Parent = part
        if orientation then a.Orientation = orientation end
        a.WorldPosition = worldPos
        return a
    end

    -- ---- Ball socket: a pendulum ---------------------------------------------------
    local pivotPos = Vector3.new(X0, 40, 0)
    local pivot = block("Pivot", pivotPos, Vector3.new(1, 1, 1), true)
    local bob = block("Bob", pivotPos + Vector3.new(6, 0, 0), Vector3.new(1, 1, 1), false)
    local ball = Instance.new("BallSocketConstraint")
    ball.Attachment0 = attach(pivot, pivotPos)
    ball.Attachment1 = attach(bob, pivotPos)   -- (the bob's attachment is 6 studs from its middle)
    ball.Parent = pivot
    check(ball.ClassName == "BallSocketConstraint" and ball:IsA("Constraint"), "BallSocketConstraint is a Constraint")
    check(ball.LimitsEnabled == false and ball.UpperAngle == 45 and ball.TwistLowerAngle == -45 and
          ball.TwistUpperAngle == 45 and ball.Restitution == 0, "ball socket defaults")

    local minX, worst, lowest = math.huge, 0, math.huge
    for _ = 1, 150 do
        frames(1)
        local d = (bob.Position - pivotPos).Magnitude
        worst = math.max(worst, math.abs(d - 6))
        minX = math.min(minX, bob.Position.X)
        lowest = math.min(lowest, bob.Position.Y)
    end
    check(lowest < pivotPos.Y - 4, "the pendulum swings down", lowest)
    check(minX < X0 - 3, "and up the other side", minX - X0)
    check(worst < 0.15, "the ball socket keeps it at its length", worst)

    -- A cone limit: a second pendulum whose arm may only lean 30 degrees from straight down.
    local pivot2Pos = Vector3.new(X0, 40, 20)
    local pivot2 = block("Pivot2", pivot2Pos, Vector3.new(1, 1, 1), true)
    local bob2 = block("Bob2", pivot2Pos + Vector3.new(0, -4, 0), Vector3.new(1, 1, 1), false)
    local cone = Instance.new("BallSocketConstraint")
    -- Both attachments' X axes point down the arm (Orientation 0,0,-90 turns X to -Y).
    cone.Attachment0 = attach(pivot2, pivot2Pos, Vector3.new(0, 0, -90))
    cone.Attachment1 = attach(bob2, pivot2Pos, Vector3.new(0, 0, -90))
    cone.LimitsEnabled = true
    cone.UpperAngle = 30
    cone.Parent = pivot2
    bob2.AssemblyLinearVelocity = Vector3.new(30, 0, 0)   -- a hard shove sideways
    local most = 0
    for _ = 1, 120 do
        frames(1)
        local arm = (bob2.Position - pivot2Pos).Unit
        most = math.max(most, math.deg(math.acos(math.clamp(-arm.Y, -1, 1))))
    end
    check(most < 33, "UpperAngle stops the arm leaning past 30 degrees", most)
    check(most > 26, "but it leans as far as it may", most)

    -- Twist limits: spin the bob around its arm; it may only twist 20 degrees each way.
    local pivot3Pos = Vector3.new(X0, 40, 40)
    local pivot3 = block("Pivot3", pivot3Pos, Vector3.new(1, 1, 1), true)
    local bob3 = block("Bob3", pivot3Pos + Vector3.new(0, -4, 0), Vector3.new(2, 1, 1), false)
    local twist = Instance.new("BallSocketConstraint")
    twist.Attachment0 = attach(pivot3, pivot3Pos, Vector3.new(0, 0, -90))
    twist.Attachment1 = attach(bob3, pivot3Pos, Vector3.new(0, 0, -90))
    twist.TwistLimitsEnabled = true
    twist.TwistLowerAngle = -20
    twist.TwistUpperAngle = 20
    twist.Parent = pivot3
    bob3.AssemblyAngularVelocity = Vector3.new(0, 12, 0)
    local mostTwist = 0
    for _ = 1, 90 do
        frames(1)
        local yaw = bob3.Orientation.Y
        mostTwist = math.max(mostTwist, math.abs(yaw))
    end
    check(mostTwist < 23 and mostTwist > 15, "TwistLowerAngle / TwistUpperAngle stop it twisting past 20 degrees", mostTwist)

    -- ---- Prismatic: a slider -------------------------------------------------------
    -- Up and down a post: gravity pulls it to LowerLimit.
    local postPos = Vector3.new(X0 + 20, 30, 0)
    local post = block("Post", postPos, Vector3.new(1, 1, 1), true)
    local car = block("Car", postPos, Vector3.new(2, 1, 2), false)
    local up = Vector3.new(0, 0, 90)   -- attachment X pointing up
    local slide = Instance.new("PrismaticConstraint")
    slide.Attachment0 = attach(post, postPos, up)
    slide.Attachment1 = attach(car, postPos, up)
    check(slide.ClassName == "PrismaticConstraint", "PrismaticConstraint.new")
    check(slide.ActuatorType == Enum.ActuatorType.None and slide.LowerLimit == 0 and slide.UpperLimit == 5 and
          slide.ServoMaxForce == 0 and slide.MotorMaxForce == 0, "prismatic defaults")
    slide.LimitsEnabled = true
    slide.LowerLimit = -3
    slide.UpperLimit = 2
    slide.Parent = post
    frames(120)
    check(near(slide.CurrentPosition, -3, 0.15), "it slides down to LowerLimit and stops", slide.CurrentPosition)
    check(near(car.Position.Y, postPos.Y - 3, 0.15), "the part is there too", car.Position.Y)
    check(near(car.Position.X, postPos.X, 0.05) and near(car.Position.Z, postPos.Z, 0.05), "it stays on the axis", car.Position)
    check((car.Orientation - Vector3.new(0, 0, 0)).Magnitude < 1, "and doesn't turn", car.Orientation)

    -- Restitution: another slider dropped onto its LowerLimit bounces back up.
    local post2Pos = Vector3.new(X0 + 20, 30, 20)
    local post2 = block("Post2", post2Pos, Vector3.new(1, 1, 1), true)
    local car2 = block("Car2", post2Pos, Vector3.new(2, 1, 2), false)
    local bouncy = Instance.new("PrismaticConstraint")
    bouncy.Attachment0 = attach(post2, post2Pos, up)
    bouncy.Attachment1 = attach(car2, post2Pos, up)
    bouncy.LimitsEnabled = true
    bouncy.LowerLimit = -4
    bouncy.UpperLimit = 4
    bouncy.Restitution = 0.7
    bouncy.Parent = post2
    local hit, highest = false, -math.huge
    for _ = 1, 120 do
        frames(1)
        local pos = bouncy.CurrentPosition
        if pos < -3.8 then hit = true elseif hit then highest = math.max(highest, pos) end
    end
    check(hit and highest > -3.2, "Restitution bounces it off the limit", highest)

    -- A servo lifts it to TargetPosition and holds it there against gravity.
    slide.ActuatorType = Enum.ActuatorType.Servo
    check(slide.ActuatorType == Enum.ActuatorType.Servo, "ActuatorType reads back")
    slide.ServoMaxForce = 20000
    slide.Speed = 6
    slide.TargetPosition = 1
    frames(150)
    check(near(slide.CurrentPosition, 1, 0.1), "the servo reaches TargetPosition", slide.CurrentPosition)
    check(near(car.Position.Y, postPos.Y + 1, 0.1), "and holds the part there", car.Position.Y)

    -- A motor drives it up into UpperLimit.
    slide.ActuatorType = "Motor"
    slide.MotorMaxForce = 20000
    slide.Velocity = 4
    frames(120)
    check(near(slide.CurrentPosition, 2, 0.15), "the motor pushes it up to UpperLimit, and no further", slide.CurrentPosition)

    -- Sideways along a rail: gravity and shoves across the rail don't move it off.
    local railPos = Vector3.new(X0 + 40, 30, 0)
    local rail = block("Rail", railPos, Vector3.new(1, 1, 1), true)
    local cart = block("Cart", railPos, Vector3.new(1, 1, 1), false)
    local side = Instance.new("PrismaticConstraint")
    side.Attachment0 = attach(rail, railPos)   -- X along the world's X
    side.Attachment1 = attach(cart, railPos)
    side.ActuatorType = Enum.ActuatorType.Motor
    side.MotorMaxForce = 5000
    side.Velocity = 4
    side.Parent = rail
    frames(30)
    cart:ApplyImpulse(Vector3.new(0, 0, 30))
    frames(60)
    check(near(cart.AssemblyLinearVelocity.X, 4, 0.3), "a prismatic motor runs at Velocity", cart.AssemblyLinearVelocity)
    check(cart.Position.X > railPos.X + 2, "and moves the part along the axis", cart.Position.X - railPos.X)
    check(near(cart.Position.Y, railPos.Y, 0.05) and near(cart.Position.Z, railPos.Z, 0.05), "it stays on the rail", cart.Position)
    check(near(side.CurrentPosition, cart.Position.X - railPos.X, 0.05), "CurrentPosition is how far along it is", side.CurrentPosition)
    check(not pcall(function() side.ActuatorType = "Sideways" end), "an unknown ActuatorType is an error")

    -- ---- Cylindrical: slides and spins -----------------------------------------------
    local axlePos = Vector3.new(X0 + 60, 30, 0)
    local axle = block("Axle", axlePos, Vector3.new(1, 1, 1), true)
    local drill = block("Drill", axlePos, Vector3.new(1, 2, 2), false)
    local cyl = Instance.new("CylindricalConstraint")
    cyl.Attachment0 = attach(axle, axlePos)
    cyl.Attachment1 = attach(drill, axlePos)
    check(cyl.ClassName == "CylindricalConstraint" and cyl.AngularActuatorType == Enum.ActuatorType.None and
          cyl.InclinationAngle == 0, "CylindricalConstraint defaults")
    cyl.ActuatorType = Enum.ActuatorType.Motor
    cyl.MotorMaxForce = 5000
    cyl.Velocity = 2
    cyl.AngularActuatorType = Enum.ActuatorType.Motor
    cyl.MotorMaxTorque = 5000
    cyl.AngularVelocity = 3
    cyl.Parent = axle
    local turned, lastAngle = 0, 0
    for _ = 1, 60 do
        frames(1)
        local a = cyl.CurrentAngle
        local step = a - lastAngle
        if step < -180 then step = step + 360 elseif step > 180 then step = step - 360 end
        turned = turned + step
        lastAngle = a
    end
    check(cyl.CurrentPosition > 1, "a cylindrical joint slides", cyl.CurrentPosition)
    check(turned > 60, "and spins", turned)
    check(near(drill.Position.Y, axlePos.Y, 0.05) and near(drill.Position.Z, axlePos.Z, 0.05), "it stays on its axis", drill.Position)
    local spin = drill.AssemblyAngularVelocity
    check(spin and near(spin.X, 3, 0.3) and math.abs(spin.Y) < 0.2 and math.abs(spin.Z) < 0.2,
          "it only spins around the axis, at AngularVelocity", spin)

    -- Servos: slide back to 0 and turn to 90 degrees.
    cyl.ActuatorType = Enum.ActuatorType.Servo
    cyl.ServoMaxForce = 5000
    cyl.Speed = 4
    cyl.TargetPosition = 0
    cyl.AngularActuatorType = Enum.ActuatorType.Servo
    cyl.ServoMaxTorque = 5000
    cyl.AngularSpeed = 4
    cyl.TargetAngle = 90
    frames(180)
    check(near(cyl.CurrentPosition, 0, 0.1), "the slide servo brings it back", cyl.CurrentPosition)
    check(near(cyl.CurrentAngle, 90, 3), "the spin servo turns it to TargetAngle", cyl.CurrentAngle)

    -- Copies keep their settings (the same path saving and multiplayer use).
    local copy = cyl:Clone()
    check(copy.ClassName == "CylindricalConstraint" and copy.TargetAngle == 90 and copy.ServoMaxTorque == 5000 and
          copy.AngularActuatorType == Enum.ActuatorType.Servo and copy.Speed == 4, "Clone keeps the joint's settings")
    copy:Destroy()

    print(fails == 0 and "JOINTS TEST DONE: all passed" or ("JOINTS TEST DONE: " .. fails .. " failed"))
    local cam = workspace.CurrentCamera
    cam.CameraType = Enum.CameraType.Scriptable
    cam.CFrame = CFrame.new(Vector3.new(X0 + 40, 34, 26), Vector3.new(X0 + 40, 30, 0))
end)
