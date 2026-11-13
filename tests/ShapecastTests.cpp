// Shapecasts: workspace:Spherecast, :Blockcast and :Shapecast find the first thing a
// sliding shape bumps into, with margins, size gradients, overrides and filters.
//
//   ShapecastTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Physics.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scripting/Shapecasts.h"

#include <imgui.h>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    err.clear();
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

void test(GameSession& session, const std::string& what, const std::string& code) {
    std::string err;
    check(run(session, code, err), what + (err.empty() ? "" : "  (" + err + ")"));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: ShapecastTests <game file>\n"; return 2; }
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
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    // A test area high above the map: a wall at x = 20..21, a thin sheet, a ball.
    test(session, "making some parts", R"(
        local f = Instance.new("Model") f.Name = "CastTest" f.Parent = workspace
        local function part(name, pos, size, shape)
            local p = Instance.new("Part") p.Name = name p.Anchored = true
            p.Size = size p.Position = pos
            if shape then p.Shape = shape end
            p.Parent = f
            return p
        end
        part("Wall", Vector3.new(20.5, 800, 0), Vector3.new(1, 20, 20))
        part("Sheet", Vector3.new(0, 800, 30.05), Vector3.new(20, 20, 0.1))     -- 0.1 studs thin
        part("Ball", Vector3.new(0, 800, -40), Vector3.new(4, 4, 4), Enum.PartType.Ball)
        part("Crate", Vector3.new(0, 830, 0), Vector3.new(2, 2, 2))
        part("Floor", Vector3.new(0, 820, 0), Vector3.new(30, 1, 30))            -- top at 820.5
    )");

    test(session, "Spherecast stops exactly where the ball touches the wall", R"(
        local hit = workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0))
        assert(hit, "no hit")
        assert(hit.Instance == workspace.CastTest.Wall, "hit " .. tostring(hit.Instance))
        assert(math.abs(hit.Distance - 19) < 0.01, "distance " .. hit.Distance)        -- 20 - radius 1
        assert(math.abs(hit.Position.X - 20) < 0.01, "position " .. tostring(hit.Position))
        assert(hit.Normal.X < -0.99, "normal " .. tostring(hit.Normal))
    )");

    test(session, "too short, or pointing away: no hit", R"(
        assert(workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(10, 0, 0)) == nil)
        assert(workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(0, 0, 0)) == nil)
    )");

    test(session, "a fast thin sheet is never jumped over", R"(
        local hit = workspace:Spherecast(Vector3.new(0, 800, 0), 0.25, Vector3.new(0, 0, 5000))
        assert(hit and hit.Instance == workspace.CastTest.Sheet, "missed the sheet")
        assert(math.abs(hit.Distance - 29.75) < 0.01, "distance " .. hit.Distance)
    )");

    test(session, "Blockcast uses the box's turn", R"(
        local straight = workspace:Blockcast(CFrame.new(0, 800, 0), Vector3.new(4, 2, 2), Vector3.new(100, 0, 0))
        assert(straight and math.abs(straight.Distance - 18) < 0.01, "straight " .. (straight and straight.Distance or -1))
        local turned = workspace:Blockcast(CFrame.new(0, 800, 0) * CFrame.Angles(0, math.rad(90), 0),
                                           Vector3.new(4, 2, 2), Vector3.new(100, 0, 0))
        assert(turned and math.abs(turned.Distance - 19) < 0.01, "turned " .. (turned and turned.Distance or -1))
    )");

    test(session, "Margin makes the shape fatter", R"(
        local params = RaycastParams.new()
        params.Margin = 0.5
        local hit = workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params)
        assert(hit and math.abs(hit.Distance - 18.5) < 0.01, "distance " .. (hit and hit.Distance or -1))
        params.Margin = -1
        assert(not pcall(function() workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params) end))
    )");

    test(session, "SizeGradient grows (and shrinks) the shape on the way", R"(
        local params = ShapecastParams.new()
        params.SizeGradient = 0.1         -- the radius grows 0.1 per stud: r = 1 + 0.1 t, touches when t + r = 20
        local hit = workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params)
        assert(hit and math.abs(hit.Distance - 19 / 1.1) < 0.01, "grow " .. (hit and hit.Distance or -1))
        params.SizeGradient = -0.04       -- r = 1 - 0.04 t: touches when t + r = 20 -> t = 19 / 0.96
        hit = workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params)
        assert(hit and math.abs(hit.Distance - 19 / 0.96) < 0.01, "shrink " .. (hit and hit.Distance or -1))
    )");

    test(session, "Shapecast uses the part's shape and leaves the part out", R"(
        local f = workspace.CastTest
        local hit = workspace:Shapecast(f.Crate, Vector3.new(0, -100, 0))
        assert(hit and hit.Instance == f.Floor, "hit " .. tostring(hit and hit.Instance))
        assert(math.abs(hit.Distance - 8.5) < 0.01, "distance " .. hit.Distance)   -- bottom 829 down to 820.5
        assert(hit.Normal.Y > 0.99, "normal")
        assert(f.Crate.Position == Vector3.new(0, 830, 0), "the part moved")
    )");

    test(session, "Shapecast CFrame and Size overrides", R"(
        local f = workspace.CastTest
        local hit = workspace:Shapecast(f.Crate, Vector3.new(100, 0, 0), nil,
            { CFrame = CFrame.new(0, 800, 0), Size = Vector3.new(6, 2, 2) })
        assert(hit and hit.Instance == f.Wall, "override hit " .. tostring(hit and hit.Instance))
        assert(math.abs(hit.Distance - 17) < 0.01, "override distance " .. hit.Distance)
    )");

    test(session, "a ball part casts round", R"(
        local f = workspace.CastTest
        local hit = workspace:Shapecast(f.Ball, Vector3.new(0, 100, 0), nil, { CFrame = CFrame.new(20.5, 800, 0) })
        -- the ball starts inside the wall, so the wall doesn't count (and nothing else is up there)
        assert(hit == nil, "a part it starts inside shouldn't count")
        hit = workspace:Shapecast(f.Ball, Vector3.new(100, 0, 0), nil, { CFrame = CFrame.new(0, 800, 0) })
        assert(hit and math.abs(hit.Distance - 18) < 0.01, "ball distance " .. (hit and hit.Distance or -1))
    )");

    test(session, "filters, CanCollide and errors work like Raycast's", R"(
        local f = workspace.CastTest
        local params = RaycastParams.new()
        params.FilterDescendantsInstances = { f.Wall }
        assert(workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params) == nil, "Exclude")
        params.FilterType = Enum.RaycastFilterType.Include
        params.FilterDescendantsInstances = { f.Sheet }
        assert(workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params) == nil, "Include")
        f.Wall.CanCollide = false
        params = RaycastParams.new()
        params.RespectCanCollide = true
        assert(workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0), params) == nil, "CanCollide")
        f.Wall.CanCollide = true
        assert(not pcall(function() workspace:Spherecast(Vector3.new(), "big", Vector3.new(1, 0, 0)) end))
        assert(not pcall(function() workspace:Blockcast(Vector3.new(), Vector3.new(1, 1, 1), Vector3.new(1, 0, 0)) end))
        assert(not pcall(function() workspace:Shapecast("crate", Vector3.new(1, 0, 0)) end))
    )");

    // Studio's Physics View: casts are kept for drawing only while it's on.
    Physics::debugOn() = true;
    test(session, "a cast while the Physics View is on", R"(
        workspace:Spherecast(Vector3.new(0, 800, 0), 1, Vector3.new(100, 0, 0))
    )");
    Physics::debugOn() = false;
    const auto& drawn = Shapecasts::recent();
    check(!drawn.empty() && drawn.back().hit && std::abs(drawn.back().position.x - 20.0f) < 0.01f,
          "the Physics View keeps the cast and where it hit");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
