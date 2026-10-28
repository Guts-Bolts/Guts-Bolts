// Spatial queries: workspace:GetPartsInPart, :GetPartBoundsInBox, :GetPartBoundsInRadius
// and OverlapParams find the right parts.
//
//   SpatialQueriesTests <a game file with a baseplate>
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

// Run some Lua; false if it didn't compile or hit an error while running (errors only get logged).
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
    if (argc < 2) { std::cerr << "usage: SpatialQueriesTests <game file>\n"; return 2; }
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

    // A test area high above the map, so the game's own parts never get in the way.
    test(session, "making some parts", R"(
        local f = Instance.new("Model") f.Name = "QueryTest" f.Parent = workspace
        local function part(name, pos, size, shape)
            local p = Instance.new("Part") p.Name = name p.Anchored = true
            p.Size = size p.Position = pos
            if shape then p.Shape = shape end
            p.Parent = f
            return p
        end
        part("Zone", Vector3.new(0, 500, 0), Vector3.new(10, 10, 10))
        part("Inside", Vector3.new(2, 500, 0), Vector3.new(2, 2, 2))
        part("Edge", Vector3.new(5.5, 500, 0), Vector3.new(2, 2, 2))      -- pokes in by half a stud
        part("Far", Vector3.new(30, 500, 0), Vector3.new(2, 2, 2))
        local corner = part("Corner", Vector3.new(6.2, 506.2, 0), Vector3.new(2, 2, 2))
        corner.CFrame = CFrame.new(6.2, 506.2, 0) * CFrame.Angles(0, 0, math.rad(45))
        part("Ball", Vector3.new(0, 520, 0), Vector3.new(4, 4, 4), Enum.PartType.Ball)
        part("NearBall", Vector3.new(2.2, 522.2, 0), Vector3.new(1, 1, 1))
    )");

    test(session, "GetPartsInPart finds the parts inside and leaves the part itself out", R"(
        local f = workspace.QueryTest
        local names = {}
        for _, p in ipairs(workspace:GetPartsInPart(f.Zone)) do names[p.Name] = true end
        assert(names.Inside and names.Edge, "missed a part inside the zone")
        assert(not names.Zone, "the zone found itself")
        assert(not names.Far, "found a part far away")
    )");

    test(session, "GetPartsInPart uses the real shape of a turned part", R"(
        -- The turned Corner's bounding box reaches into the zone, but the part doesn't.
        local f = workspace.QueryTest
        for _, p in ipairs(workspace:GetPartsInPart(f.Zone)) do assert(p.Name ~= "Corner", "found Corner") end
        local inBox = {}
        for _, p in ipairs(workspace:GetPartBoundsInBox(f.Zone.CFrame, f.Zone.Size)) do inBox[p.Name] = true end
        assert(inBox.Corner, "GetPartBoundsInBox should count Corner's bounding box")
    )");

    test(session, "a ball is round for GetPartsInPart", R"(
        local f = workspace.QueryTest
        for _, p in ipairs(workspace:GetPartsInPart(f.Ball)) do assert(p.Name ~= "NearBall", "the ball's corner counted") end
        local found = false
        for _, p in ipairs(workspace:GetPartsInPart(f.NearBall)) do if p == f.Ball then found = true end end
        assert(not found, "NearBall should miss the ball")
    )");

    test(session, "GetPartBoundsInRadius finds near parts only", R"(
        local f = workspace.QueryTest
        local names = {}
        for _, p in ipairs(workspace:GetPartBoundsInRadius(Vector3.new(30, 500, 0), 3)) do names[p.Name] = true end
        assert(names.Far and not names.Inside, "wrong parts near Far")
    )");

    test(session, "OverlapParams Include, Exclude and MaxParts", R"(
        local f = workspace.QueryTest
        local params = OverlapParams.new()
        params.FilterType = Enum.RaycastFilterType.Include
        params.FilterDescendantsInstances = { f.Inside }
        local got = workspace:GetPartsInPart(f.Zone, params)
        assert(#got == 1 and got[1] == f.Inside, "Include should only find Inside")
        params = OverlapParams.new()
        params.FilterDescendantsInstances = { f.Inside }
        for _, p in ipairs(workspace:GetPartsInPart(f.Zone, params)) do assert(p ~= f.Inside, "Exclude didn't") end
        params = OverlapParams.new()
        params.MaxParts = 1
        assert(#workspace:GetPartBoundsInBox(CFrame.new(0, 500, 0), Vector3.new(40, 40, 40), params) == 1, "MaxParts")
    )");

    test(session, "RespectCanCollide and CollisionGroup", R"(
        local f = workspace.QueryTest
        f.Inside.CanCollide = false
        local params = OverlapParams.new()
        params.RespectCanCollide = true
        for _, p in ipairs(workspace:GetPartsInPart(f.Zone, params)) do assert(p ~= f.Inside, "a CanCollide-off part counted") end
        f.Inside.CanCollide = true
        local PhysicsService = game:GetService("PhysicsService")
        PhysicsService:RegisterCollisionGroup("Ghosts")
        PhysicsService:CollisionGroupSetCollidable("Ghosts", "Default", false)
        params = OverlapParams.new()
        params.CollisionGroup = "Ghosts"
        assert(#workspace:GetPartsInPart(f.Zone, params) == 0, "Ghosts pass through Default parts")
    )");

    test(session, "bad values are errors", R"(
        assert(not pcall(function() workspace:GetPartBoundsInBox(Vector3.new(), Vector3.new(1, 1, 1)) end))
        assert(not pcall(function() workspace:GetPartBoundsInRadius(Vector3.new(), "big") end))
        assert(not pcall(function() workspace:GetPartsInPart(workspace.QueryTest.Zone, "params") end))
    )");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
