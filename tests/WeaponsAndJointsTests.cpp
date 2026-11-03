// The "Lock and Load" additions: old-style joints (Weld, Motor6D and the character's R6
// joints), hinge limits, mesh stand-ins, Roblox's lower-case CFrame names, fast parts that
// no longer fly through things, and the ready-made Blaster gun.
//
//   WeaponsAndJointsTests <a game file with a baseplate>
#include "core/Log.h"
#include "editor/Premades.h"
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

// Run some Lua and give back what it printed ("ERROR ..." if it failed).
std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    const bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    std::string out = ok ? "" : "ERROR " + err;
    for (const Log::Entry& e : lines) {
        if (e.level == Log::Level::Error) out += "ERROR " + e.text;
        else if (e.text.rfind("> ", 0) != 0) out += e.text;   // (not the echo of the command)
    }
    return out;
}

void frames(GameSession& session, int n) {
    for (int i = 0; i < n; ++i) session.update(1.0f / 60.0f, 0.0f, false);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: WeaponsAndJointsTests <game file>\n"; return 2; }

    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // --- The ready-made gun: a Tool with a Handle, a Muzzle, a RemoteEvent and both halves of the script ---
    {
        Scene s;
        SceneNode* gun = buildPremade(s, Premade::GearBlaster, glm::vec3(0.0f));
        check(gun && gun->kind == NodeKind::Tool && isGearPremade(Premade::GearBlaster), "the Blaster is gear");
        int scripts = 0, locals = 0;
        if (gun)
            for (auto& c : gun->children) {
                if (c->isScript()) (c->isLocal ? locals : scripts)++;
            }
        check(gun && gun->findChild("Handle") && gun->findChild("Muzzle") && gun->findChild("Fire") &&
              gun->findChild("Fire")->isRemote(), "it has a Handle, a Muzzle and a Fire RemoteEvent");
        check(scripts == 1 && locals == 1, "a Script (damage) and a LocalScript (aiming)");
    }

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.start();
    frames(session, 5);

    // --- Roblox's older names still work ---
    std::string out = run(session, R"(
        local a = CFrame.new(1, 2, 3)
        local b = CFrame.new(1, 5, 3)
        print(a:toObjectSpace(b).Y, a:ToWorldSpace(b).Y, a.p.Y, a.lookVector.Z, Vector3.new(3, 4, 0).magnitude,
              select("#", a:GetComponents()), a:inverse().Y)
        local x, y, z = CFrame.fromOrientation(0, math.rad(90), 0):ToOrientation()
        print(math.floor(math.deg(y) + 0.5))
    )");
    check(out.find("3.0 7.0 2.0 -1.0 5.0 12 -2.0") != std::string::npos && out.find("90") != std::string::npos,
          "toObjectSpace, ToWorldSpace, .p, lookVector, magnitude, GetComponents, inverse, ToOrientation (" + out + ")");

    // --- player:WaitForChild("PlayerGui") finds it straight away (it used to wait forever) ---
    out = run(session, R"(local me = game:GetService("Players").LocalPlayer print(me:FindFirstChild("PlayerGui") ~= nil, me:WaitForChild("PlayerGui").ClassName))");
    check(out.find("true PlayerGui") != std::string::npos, "player:WaitForChild(\"PlayerGui\") (" + out + ")");

    // --- Old Roblox properties that weapon scripts set (they used to stop the script) ---
    out = run(session, R"(
        local p = Instance.new("Part") p.TopSurface = Enum.SurfaceType.Smooth p.BottomSurface = 0 p.Friction = 0.3
        local l = Instance.new("TextLabel") l.FontSize = Enum.FontSize.Size24 l.SizeConstraint = Enum.SizeConstraint.RelativeXY l.Transparency = 1
        local a = Instance.new("Attachment") a.Axis = Vector3.new(0, 0, 1)
        local s = Instance.new("Sound") s.MaxDistance = 100 s.EmitterSize = 10
        local b = Instance.new("TextButton")
        local hum = game:GetService("Players").LocalPlayer.Character.Humanoid
        print("old", l.TextSize, l.BackgroundTransparency, workspace.FilteringEnabled, workspace:PGSIsEnabled(),
              hum.RigType == Enum.HumanoidRigType.R6, b.MouseButton1Down ~= nil, b.MouseButton1Up ~= nil,
              game:GetService("Players").LocalPlayer:GetMouse().Move ~= nil,
              math.floor(math.abs(a.Orientation.Y) + 0.5))
    )");
    check(out.find("old 24.0 1.0 true true true true true true 90") != std::string::npos,
          "FontSize, part surfaces, SizeConstraint, Axis, Sound roll-off, RigType, MouseButton1Down, mouse.Move (" + out + ")");

    // --- Weld and Motor6D ---
    out = run(session, R"(
        local a = Instance.new("Part") a.Name = "WeldA" a.Anchored = true a.Position = Vector3.new(30, 5, 30) a.Parent = workspace
        local b = Instance.new("Part") b.Name = "WeldB" b.Position = Vector3.new(40, 5, 30) b.Parent = workspace
        local w = Instance.new("Weld") w.Name = "TestWeld" w.Part0 = a w.Part1 = b w.C0 = CFrame.new(0, 2, 0) w.Parent = a
        local c = Instance.new("Part") c.Name = "MotorC" c.Position = Vector3.new(30, 9, 30) c.Parent = workspace
        local m = Instance.new("Motor6D") m.Name = "TestMotor" m.Part0 = a m.Part1 = c m.C0 = CFrame.new(0, 3, 0) m.MaxVelocity = 0.1 m.Parent = a
        m.DesiredAngle = 1.5
        print(w.ClassName, w:IsA("JointInstance"), m.ClassName, m:IsA("Motor"), Instance.new("ManualWeld"):IsA("JointInstance"))
    )");
    check(out.find("Weld true Motor6D true true") != std::string::npos, "Instance.new Weld / Motor6D / ManualWeld (" + out + ")");
    frames(session, 60);
    out = run(session, R"(
        local a, b, c = workspace.WeldA, workspace.WeldB, workspace.MotorC
        print(string.format("%.2f %.2f", (b.Position - (a.Position + Vector3.new(0, 2, 0))).Magnitude, a.TestMotor.CurrentAngle))
    )");
    check(out.find("0.00 1.50") != std::string::npos, "a Weld holds Part1 at C0, a Motor6D turns to DesiredAngle (" + out + ")");

    // --- The character's joints ---
    out = run(session, R"(
        local ch = game:GetService("Players").LocalPlayer.Character
        local t = ch.Torso
        local names = {}
        for _, n in ipairs({"Right Shoulder", "Left Shoulder", "Right Hip", "Left Hip", "Neck"}) do
            local j = t:FindFirstChild(n)
            names[#names + 1] = j and j.ClassName or "none"
        end
        print(table.concat(names, ","), ch.HumanoidRootPart:FindFirstChild("RootJoint") ~= nil, t["Right Shoulder"].Part1 == ch["Right Arm"])
        workspace:SetAttribute("ArmY", ch["Right Arm"].Position.Y - t.Position.Y)
        t["Right Shoulder"].C0 = t["Right Shoulder"].C0 * CFrame.Angles(0, 0, math.rad(90))
    )");
    check(out.find("Motor6D,Motor6D,Motor6D,Motor6D,Motor6D true true") != std::string::npos, "characters have Roblox's R6 joints (" + out + ")");
    frames(session, 10);
    out = run(session, R"(
        local ch = game:GetService("Players").LocalPlayer.Character
        print("raised", ch["Right Arm"].Position.Y - ch.Torso.Position.Y > workspace:GetAttribute("ArmY") + 0.15)
        ch.Torso["Right Shoulder"].C0 = ch.Torso["Right Shoulder"].C0 * CFrame.Angles(0, 0, math.rad(-90))
    )");
    check(out.find("raised true") != std::string::npos, "turning Right Shoulder's C0 raises the arm (" + out + ")");

    // --- Hinge limits, meshes ---
    out = run(session, R"(
        local h = Instance.new("HingeConstraint") h.LimitsEnabled = true h.UpperAngle = 45 h.LowerAngle = -30
        local p = Instance.new("Part") p.Parent = workspace
        local m = Instance.new("CylinderMesh") m.Parent = p m.Scale = Vector3.new(1, 2, 1)
        print(h.LimitsEnabled, h.UpperAngle, h.LowerAngle, tostring(p.Shape), m.ClassName, m.Scale.Y)
    )");
    check(out.find("true 45.0 -30.0") != std::string::npos && out.find("Cylinder") != std::string::npos &&
          out.find("CylinderMesh 2.0") != std::string::npos, "hinge limits and CylinderMesh (" + out + ")");

    // --- A Motor6D survives saving ---
    {
        Scene copy;
        std::string loadErr;
        check(Serializer::loadScene(copy, Serializer::saveScene(scene), &loadErr), "the running game saves and loads");
        SceneNode* m = nullptr;
        copy.forEach([&](SceneNode* n) { if (n->name == "TestMotor") m = n; });
        check(m && m->legacyJoint == 2 && std::abs(m->jointC0[3].y - 3.0f) < 0.01f, "a Motor6D keeps its C0 when saved");
    }

    // --- Fast parts hit things (they used to pass right through between two frames) ---
    out = run(session, R"(
        local wall = Instance.new("Part") wall.Name = "ThinWall" wall.Anchored = true wall.Size = Vector3.new(6, 6, 0.2)
        wall.Position = Vector3.new(-30, 4, 0) wall.Parent = workspace
        workspace:SetAttribute("WallHit", false)
        wall.Touched:Connect(function(h) if h.Name == "Bullet" then workspace:SetAttribute("WallHit", true) end end)
        local b = Instance.new("Part") b.Name = "Bullet" b.Size = Vector3.new(0.2, 0.2, 0.2) b.CanCollide = false
        b.Position = Vector3.new(-30, 4, 10) b.Parent = workspace
        b.AssemblyLinearVelocity = Vector3.new(0, 0, -300)
    )");
    frames(session, 10);
    out = run(session, R"(print("wallHit", workspace:GetAttribute("WallHit")))");
    check(out.find("wallHit true") != std::string::npos, "a fast bullet fires Touched on a thin wall (" + out + ")");

    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
