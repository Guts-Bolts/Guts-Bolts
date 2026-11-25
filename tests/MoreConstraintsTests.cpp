// "Strings Attached": the rest of Roblox's constraints working for real (RigidConstraint,
// PlaneConstraint, UniversalConstraint, TorsionSpringConstraint, NoCollisionConstraint,
// LineForce), the RopeConstraint winch, CurrentDistance, and assembly members
// (AssemblyRootPart, AssemblyMass, IsGrounded, GetClosestPointOnSurface...).
//
//   MoreConstraintsTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    const bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    std::string out = ok ? "" : "ERROR " + err;
    for (const Log::Entry& e : lines) {
        if (e.level == Log::Level::Error) out += "ERROR " + e.text;
        else if (e.text.rfind("> ", 0) != 0) out += e.text;
    }
    return out;
}

void frames(GameSession& session, int n) {
    for (int i = 0; i < n; ++i) session.update(1.0f / 60.0f, 0.0f, false);
}
bool has(const std::string& out, const std::string& what) { return out.find(what) != std::string::npos; }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: MoreConstraintsTests <game file>\n"; return 2; }

    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.start();
    frames(session, 5);

    // --- They're real classes now, with Roblox's properties ---
    std::string out = run(session, R"(
        local r = Instance.new("RigidConstraint")
        local u = Instance.new("UniversalConstraint")
        local t = Instance.new("TorsionSpringConstraint")
        local n = Instance.new("NoCollisionConstraint")
        local l = Instance.new("LineForce")
        local p = Instance.new("PlaneConstraint")
        r.DestructionEnabled = true r.DestructionForce = 500
        u.LimitsEnabled = true u.MaxAngle = 30
        t.MaxAngle = 60
        l.Magnitude = 250 l.InverseSquareLaw = true l.ReactionForceEnabled = true
        print("classes", r.ClassName, p.ClassName, n.ClassName, l.ClassName)
        print("isa", r:IsA("Constraint"), l:IsA("Constraint"), n:IsA("Constraint"))
        print("props", r.DestructionEnabled, r.DestructionForce, u.LimitsEnabled, u.MaxAngle, t.Stiffness, t.MaxAngle,
              l.Magnitude, l.InverseSquareLaw, l.ReactionForceEnabled, n.Enabled)
    )");
    check(has(out, "classes RigidConstraint PlaneConstraint NoCollisionConstraint LineForce"), "Instance.new makes them (" + out + ")");
    check(has(out, "isa true true false"), "NoCollisionConstraint isn't a Constraint, like Roblox (" + out + ")");
    check(has(out, "props true 500 true 30 100 60 250 true true true"), "their properties read back (" + out + ")");

    // --- Set up one little rig of each (far apart so they don't touch) ---
    out = run(session, R"(
        local function part(name, pos, anchored, size)
            local p = Instance.new("Part") p.Name = name p.Anchored = anchored p.Size = size or Vector3.new(1, 1, 1)
            p.Position = pos p.Parent = workspace
            return p
        end
        local function att(p, offset, cf)
            local a = Instance.new("Attachment") a.Parent = p
            if cf then a.CFrame = cf end
            if offset then a.Position = offset end
            return a
        end
        -- NoCollision: a block dropped on a floor it's told to pass through, and one that isn't.
        local floor = part("NcFloor", Vector3.new(200, 40, 0), true, Vector3.new(20, 1, 20))
        local ghost = part("NcGhost", Vector3.new(198, 44, 0), false)
        part("NcSolid", Vector3.new(202, 44, 0), false)
        local nc = Instance.new("NoCollisionConstraint") nc.Part0 = floor nc.Part1 = ghost nc.Parent = floor
        -- Rigid: a block held in mid-air by an anchored one.
        local ra = part("RgA", Vector3.new(230, 60, 0), true)
        local rb = part("RgB", Vector3.new(232, 60, 0), false)
        local rc = Instance.new("RigidConstraint") rc.Attachment0 = att(ra, Vector3.new(1, 0, 0)) rc.Attachment1 = att(rb, Vector3.new(-1, 0, 0)) rc.Parent = rb
        -- Plane: Attachment0's X points up, so the plane is flat and the block can't fall through it.
        local pa = part("PlA", Vector3.new(260, 60, 0), true)
        local pb = part("PlB", Vector3.new(263, 60, 0), false)
        local pc = Instance.new("PlaneConstraint") pc.Attachment0 = att(pa, nil, CFrame.Angles(0, 0, math.rad(90)))
        pc.Attachment1 = att(pb) pc.Parent = pb
        -- Universal: a block hanging off an anchored one by a bendy joint.
        local ua = part("UnA", Vector3.new(290, 60, 0), true)
        local ub = part("UnB", Vector3.new(290, 58, 0), false)
        local uc = Instance.new("UniversalConstraint") uc.Attachment0 = att(ua, Vector3.new(0, -1, 0))
        uc.Attachment1 = att(ub, Vector3.new(0, 1, 0), CFrame.Angles(math.rad(90), 0, 0)) uc.Parent = ub
        -- Rope with a winch: 12 studs of rope reeled in to 3.
        local wa = part("WnA", Vector3.new(320, 70, 0), true)
        local wb = part("WnB", Vector3.new(320, 58, 0), false)
        local rope = Instance.new("RopeConstraint") rope.Name = "Winch" rope.Attachment0 = att(wa) rope.Attachment1 = att(wb)
        rope.Length = 12 rope.WinchEnabled = true rope.WinchTarget = 3 rope.WinchSpeed = 20 rope.WinchForce = 100000 rope.Parent = wb
        -- Assemblies: an anchored block welded to two loose ones.
        local g = part("AsRoot", Vector3.new(350, 60, 0), true, Vector3.new(2, 2, 2))
        local g2 = part("AsTwo", Vector3.new(352, 60, 0), false)
        local g3 = part("AsThree", Vector3.new(354, 60, 0), false)
        local w1 = Instance.new("WeldConstraint") w1.Part0 = g w1.Part1 = g2 w1.Parent = g
        local w2 = Instance.new("WeldConstraint") w2.Part0 = g2 w2.Part1 = g3 w2.Parent = g2
        local lone = part("AsLone", Vector3.new(360, 60, 0), true, Vector3.new(4, 2, 2))
        print("built")
    )");
    check(has(out, "built"), "the test rigs are built (" + out + ")");
    frames(session, 120);
    out = run(session, R"(
        local w = workspace
        local ghost = w:FindFirstChild("NcGhost")
        print("nc", ghost == nil or ghost.Position.Y < 39, w.NcSolid.Position.Y > 40)
        print("rigid", (w.RgB.Position - Vector3.new(232, 60, 0)).Magnitude < 0.5, w.RgB.Position.Y)
        print("plane", math.abs(w.PlB.Position.Y - 60) < 0.3, w.PlB.Position.Y)
        print("universal", (w.UnB.Position - w.UnA.Position).Magnitude < 2.5, w.UnB.Position.Y)
        local rope = w.WnB.Winch
        print("winch", math.abs(rope.Length - 3) < 0.05, math.abs(rope.CurrentDistance - 3) < 0.5, rope.CurrentDistance)
        print("assembly", w.AsThree.AssemblyRootPart == w.AsRoot, w.AsThree:GetRootPart() == w.AsRoot, w.AsThree:IsGrounded(),
              w.AsLone:IsGrounded(), math.abs(w.AsTwo.AssemblyMass - (w.AsRoot.Mass + w.AsTwo.Mass + w.AsThree.Mass)) < 1e-3)
        local c = w.AsLone:GetClosestPointOnSurface(Vector3.new(370, 60, 0))
        local inside = w.AsLone:GetClosestPointOnSurface(Vector3.new(360.5, 60, 0))
        print("closest", c.X, c.Y, inside.X)
        print("ncList", #w.NcFloor:GetNoCollisionConstraints(), #w.NcSolid:GetNoCollisionConstraints())
    )");
    check(has(out, "nc true true"), "NoCollisionConstraint lets one block fall through the floor, not the other (" + out + ")");
    check(has(out, "rigid true"), "RigidConstraint holds a block in mid-air, attachments together (" + out + ")");
    check(has(out, "plane true"), "PlaneConstraint keeps a block on its plane (" + out + ")");
    check(has(out, "universal true"), "UniversalConstraint keeps the hanging block on (" + out + ")");
    check(has(out, "winch true true"), "the rope winch reels it in to WinchTarget (" + out + ")");
    check(has(out, "assembly true true true true true"), "AssemblyRootPart, GetRootPart, IsGrounded, AssemblyMass (" + out + ")");
    check(has(out, "closest 362 60 360.5"), "GetClosestPointOnSurface (" + out + ")");
    check(has(out, "ncList 1 0"), "GetNoCollisionConstraints (" + out + ")");

    // --- No gravity: a line force pulls, a torsion spring turns back ---
    out = run(session, R"(
        workspace.Gravity = 0
        local function part(name, pos, anchored)
            local p = Instance.new("Part") p.Name = name p.Anchored = anchored p.Size = Vector3.new(1, 1, 1)
            p.Position = pos p.Parent = workspace
            return p
        end
        local la = part("LfA", Vector3.new(400, 80, 0), true)
        local lb = part("LfB", Vector3.new(410, 80, 0), false)
        local lf = Instance.new("LineForce") lf.Attachment0 = Instance.new("Attachment", lb) lf.Attachment1 = Instance.new("Attachment", la)
        lf.Magnitude = 20 lf.ApplyAtCenterOfMass = true lf.Parent = lb
        local ta = part("TsA", Vector3.new(450, 80, 0), true)
        local tb = part("TsB", Vector3.new(450, 80, 0), false)
        tb.CanCollide = false
        tb.CFrame = CFrame.new(450, 80, 0) * CFrame.Angles(math.rad(60), 0, 0)
        local ts = Instance.new("TorsionSpringConstraint") ts.Name = "Spring"
        ts.Attachment0 = Instance.new("Attachment", ta) ts.Attachment1 = Instance.new("Attachment", tb)
        ts.Stiffness = 200 ts.Damping = 20 ts.Parent = tb
        print("start", lb.Position.X)
    )");
    frames(session, 90);
    out = run(session, R"(
        local w = workspace
        print("line", w.LfB.Position.X < 409, w.LfB.Position.X)
        print("torsion", w.TsB.Spring.CurrentAngle < 30, w.TsB.Spring.CurrentAngle)
        workspace.Gravity = 196.2
    )");
    check(has(out, "line true"), "LineForce pulls Attachment0 towards Attachment1 (" + out + ")");
    check(has(out, "torsion true"), "TorsionSpringConstraint turns the block back into line (" + out + ")");

    // --- They survive saving ---
    {
        Scene copy;
        std::string loadErr;
        check(Serializer::loadScene(copy, Serializer::saveScene(scene), &loadErr), "the running game saves and loads");
        SceneNode* rope = nullptr, *line = nullptr, *ghost = nullptr;
        copy.forEach([&](SceneNode* n) {
            if (n->name == "Winch") rope = n;
            if (n->isConstraint() && n->constraintType == ConstraintType::LineForce) line = n;
            if (n->isConstraint() && n->constraintType == ConstraintType::NoCollision) ghost = n;
        });
        check(rope && rope->joint.winchEnabled && std::abs(rope->joint.winchTarget - 3.0f) < 1e-3f, "the winch is saved");
        check(line && std::abs(line->joint.lineMagnitude - 20.0f) < 1e-3f && line->joint.applyAtCenterOfMass, "LineForce is saved");
        check(ghost && ghost->ref0, "NoCollisionConstraint keeps its Part0");
    }

    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
