// Network ownership: a joined player driving a car moves that car on their own computer
// (no waiting for the host), and the host leaves the car alone while they do.
// (GameSession::updateOwnership, Physics::mayMove, Vehicles::movingParts; NetGame sends it.)
//
//   OwnershipTests <a game file with a player>
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Vehicles.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Run the game for a while. `seat`: the player presses W in it every frame.
// `joinerPart`: a joined player says every frame that they're moving this part.
void play(GameSession& session, Scene& scene, float seconds, SceneNode* seat = nullptr, uint64_t joinerPart = 0) {
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        if (seat && scene.player()) scene.vehicles().drive(seat->id, scene.player()->rootId(), 1.0f, 0.0f);
        if (joinerPart) session.joinerMoves(joinerPart);
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

SceneNode* find(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == name && n->isPart()) found = n; });
    return found;
}
glm::vec3 pos(const SceneNode* n) { return glm::vec3(n->worldMatrix()[3]); }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: OwnershipTests <game file>\n"; return 2; }
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
    Player* p = scene.player();
    if (!p || !p->root()) { std::cerr << "no player in that game\n"; return 1; }

    // A floor, a car (chassis + welded VehicleSeat + four ball wheels on motor hinges)
    // and two loose crates up in the air.
    std::string code = R"(
        local floor = Instance.new('Part')
        floor.Name = 'Floor' floor.Anchored = true floor.Size = Vector3.new(300, 2, 300)
        floor.Position = Vector3.new(500, 99, 500) floor.Parent = workspace
        local car = Instance.new('Model') car.Name = 'Car' car.Parent = workspace
        local body = Instance.new('Part')
        body.Name = 'Chassis' body.Size = Vector3.new(3.4, 0.6, 6) body.Position = Vector3.new(500, 101.3, 500) body.Parent = car
        local seat = Instance.new('VehicleSeat')
        seat.Name = 'DriverSeat' seat.Size = Vector3.new(1.6, 0.2, 1.4) seat.Position = Vector3.new(500, 101.7, 500.8) seat.Parent = car
        local weld = Instance.new('WeldConstraint') weld.Part0 = body weld.Part1 = seat weld.Parent = body
        for i = 0, 3 do
            local x = (i % 2 == 1) and 2.2 or -2.2
            local z = (i >= 2) and 2.1 or -2.1
            local wheel = Instance.new('Part')
            wheel.Name = 'Wheel' wheel.Shape = Enum.PartType.Ball wheel.Size = Vector3.new(1.6, 1.6, 1.6)
            wheel.Position = Vector3.new(500 + x, 100.8, 500 + z) wheel.Parent = car
            local a0 = Instance.new('Attachment') a0.Parent = body a0.WorldPosition = wheel.Position
            local a1 = Instance.new('Attachment') a1.Parent = wheel a1.WorldPosition = wheel.Position
            local hinge = Instance.new('HingeConstraint')
            hinge.Attachment0 = a0 hinge.Attachment1 = a1
            hinge.ActuatorType = Enum.ActuatorType.Motor hinge.MotorMaxTorque = 3000 hinge.AngularVelocity = 0
            hinge.Parent = body
        end
        for i, name in ipairs({'CrateA', 'CrateB'}) do
            local c = Instance.new('Part') c.Name = name c.Size = Vector3.new(2, 2, 2)
            c.Position = Vector3.new(450 + i * 10, 140, 450) c.Parent = workspace
        end
    )";
    if (!session.scripts().runCommand(code, err)) { std::cerr << "script error: " << err << "\n"; return 1; }
    SceneNode* chassis = find(scene, "Chassis");
    SceneNode* seat = find(scene, "DriverSeat");
    SceneNode* crateA = find(scene, "CrateA");
    SceneNode* crateB = find(scene, "CrateB");
    if (!chassis || !seat || !crateA || !crateB) { std::cerr << "couldn't build the car\n"; return 1; }

    // --- What a driver owns: the car's own loose parts, nothing else ---
    const std::vector<SceneNode*> parts = Vehicles::movingParts(seat);
    auto has = [&](const SceneNode* n) { return std::find(parts.begin(), parts.end(), n) != parts.end(); };
    int wheels = 0;
    for (SceneNode* n : parts) wheels += n->name == "Wheel";
    check(has(chassis) && wheels == 4, "the car's parts are the chassis and its four wheels");
    check(!has(crateA) && !has(crateB), "a crate nearby isn't part of the car");

    // --- Hosting: a part a joined player is moving holds still in our physics ---
    play(session, scene, 1.0f, nullptr, crateA->id);
    check(pos(crateA).y > 139.9f, "the host leaves a joiner's part alone while they move it");
    check(pos(crateB).y < 135.0f, "(other loose parts still fall)");
    play(session, scene, 1.0f);
    check(pos(crateA).y < 138.0f, "when they stop telling us, it falls again (half a second later)");

    // --- Joined: driving our own car, we move it ourselves ---
    const float crateY = 145.0f;
    crateB->transform.position = glm::vec3(470, crateY, 450);
    crateB->velocity = glm::vec3(0.0f);
    p->root()->transform.position = pos(seat) + glm::vec3(0.0f, 1.0f, 0.0f);
    play(session, scene, 1.0f);
    p->sit(seat);
    session.setRole(GameSession::Role::Client);
    const float crateBefore = pos(crateB).y;
    play(session, scene, 0.2f, seat);
    check(session.ownedParts().count(chassis->id) == 1, "a joined player driving owns their car's parts");
    const glm::vec3 start = pos(chassis);
    play(session, scene, 2.0f, seat);
    check(glm::length(pos(chassis) - start) > 4.0f,
          "and moves it on their own computer (" + std::to_string(glm::length(pos(chassis) - start)) + " studs in 2 seconds)");
    check(std::fabs(pos(crateB).y - crateBefore) < 0.01f, "but other loose parts are still the host's to move");
    p->standUp(true);
    play(session, scene, 0.2f);
    check(session.ownedParts().empty(), "getting out: we own nothing");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
