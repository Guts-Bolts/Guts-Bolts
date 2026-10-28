// Vehicles: a VehicleSeat drives the car it's in (spinning its motor wheels) or pushes a
// boat along, scripts can read and set Throttle / Steer, and it stops when let go.
//
//   VehiclesTests <a game file with a baseplate and a player>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Vehicles.h"

#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// `drive`: what the player in the seat is pressing, every frame (null = nobody pressing).
void play(GameSession& session, Scene& scene, float seconds, SceneNode* seat = nullptr, float throttle = 0.0f, float steer = 0.0f) {
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        if (seat && scene.player()) scene.vehicles().drive(seat->id, scene.player()->rootId(), throttle, steer);
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

void run(GameSession& session, const std::string& code) {
    std::string err;
    if (!session.scripts().runCommand(code, err) && !err.empty()) std::cout << "script error: " << err << "\n";
}

SceneNode* find(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == name && n->isPart()) found = n; });
    return found;
}

float numAttr(const SceneNode* n, const char* name) {
    const Attribute* a = n ? n->findAttribute(name) : nullptr;
    return a && a->type == Attribute::Number ? (float)a->n : -999.0f;
}

glm::vec3 pos(const SceneNode* n) { return glm::vec3(n->worldMatrix()[3]); }
glm::vec3 front(const SceneNode* n) { glm::vec3 f = -glm::vec3(n->worldMatrix()[2]); f.y = 0.0f; return glm::normalize(f); }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: VehiclesTests <game file>\n"; return 2; }
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

    // --- Scripts see a VehicleSeat like Roblox's ---
    run(session, R"(
        local s = Instance.new('VehicleSeat')
        s.Name = 'LooseSeat'
        s.Anchored = true
        s.Position = Vector3.new(800, 300, 800)
        s.Parent = workspace
        s:SetAttribute('Class', s.ClassName == 'VehicleSeat' and 1 or 0)
        s:SetAttribute('IsPart', s:IsA('BasePart') and 1 or 0)
        s:SetAttribute('Default', s.MaxSpeed)
        s.MaxSpeed = 20
        s:SetAttribute('Changed', s.MaxSpeed)
    )");
    SceneNode* loose = find(scene, "LooseSeat");
    check(loose && Vehicles::isVehicleSeat(loose) && Player::isSeat(loose), "Instance.new('VehicleSeat') makes a seat you drive");
    check(numAttr(loose, "Class") == 1.0f && numAttr(loose, "IsPart") == 1.0f, "its ClassName is VehicleSeat, and it's a BasePart");
    check(numAttr(loose, "Default") == Vehicles::kMaxSpeed && numAttr(loose, "Changed") == 20.0f, "MaxSpeed has a default and can be changed");

    // --- A car: a chassis with a VehicleSeat welded on, and four ball wheels on motor hinges ---
    run(session, R"(
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
    )");
    SceneNode* chassis = find(scene, "Chassis");
    SceneNode* seat = find(scene, "DriverSeat");
    if (!chassis || !seat) { std::cerr << "couldn't build the car\n"; return 1; }
    p->root()->transform.position = pos(seat) + glm::vec3(0.0f, 1.0f, 0.0f);
    play(session, scene, 1.5f);
    p->sit(seat);
    play(session, scene, 0.5f);
    check(p->seatId() == seat->id, "the player sits in the VehicleSeat");

    const glm::vec3 start = pos(chassis), startFront = front(chassis);
    play(session, scene, 3.0f, seat, 1.0f, 0.0f);
    run(session, R"(
        local seat = workspace.Car.DriverSeat
        seat:SetAttribute('Throttle', seat.Throttle)
        seat:SetAttribute('HasOccupant', seat.Occupant and 1 or 0)
    )");
    const float forward = glm::dot(pos(chassis) - start, startFront);
    check(forward > 6.0f, "W drives the car forward (" + std::to_string(forward) + " studs in 3 seconds)");
    check(std::abs(glm::dot(pos(chassis) - start, glm::vec3(startFront.z, 0, -startFront.x))) < 0.3f * forward, "and fairly straight");
    check(numAttr(seat, "Throttle") == 1.0f, "scripts see seat.Throttle = 1");
    check(numAttr(seat, "HasOccupant") == 1.0f, "and seat.Occupant is the driver");
    check(p->seatId() == seat->id, "the driver stays in the seat");

    play(session, scene, 2.5f, seat, 1.0f, 1.0f);
    const float turned = glm::degrees(std::acos(std::clamp(glm::dot(front(chassis), startFront), -1.0f, 1.0f)));
    const float side = glm::cross(startFront, front(chassis)).y;   // < 0: turned right (clockwise from above)
    check(turned > 25.0f && side < 0.0f, "D steers right (turned " + std::to_string(turned) + " degrees)");

    play(session, scene, 3.0f, seat, 0.0f, 0.0f);
    const glm::vec3 before = pos(chassis);
    play(session, scene, 1.0f, seat, 0.0f, 0.0f);
    check(glm::length(pos(chassis) - before) < 1.0f, "letting go stops the car");
    p->standUp(true);
    play(session, scene, 1.0f);
    check(scene.vehicles().driver(seat->id) == 0, "getting out: nobody's driving");

    // --- A boat: a wooden hull with a VehicleSeat, in a pond; a script drives it (nobody in it) ---
    run(session, R"(
        local bed = Instance.new('Part')
        bed.Name = 'PondBed' bed.Anchored = true bed.Size = Vector3.new(80, 1, 80) bed.Position = Vector3.new(-500, 99.5, -500) bed.Parent = workspace
        local water = Instance.new('Part')
        water.Name = 'Water' water.Anchored = true water.CanCollide = false water.Transparency = 0.5
        water.Size = Vector3.new(80, 6, 80) water.Position = Vector3.new(-500, 103, -500) water.Parent = workspace
        local boat = Instance.new('Model') boat.Name = 'Boat' boat.Parent = workspace
        local hull = Instance.new('Part')
        hull.Name = 'Hull' hull.Material = Enum.Material.Wood hull.Size = Vector3.new(3, 0.8, 6.5)
        hull.Position = Vector3.new(-500, 106.2, -500) hull.Parent = boat
        local seat = Instance.new('VehicleSeat')
        seat.Name = 'BoatSeat' seat.Material = Enum.Material.Wood seat.Size = Vector3.new(1.4, 0.2, 1.2)
        seat.Position = Vector3.new(-500, 106.7, -498.4) seat.Parent = boat
        local weld = Instance.new('WeldConstraint') weld.Part0 = hull weld.Part1 = seat weld.Parent = hull
    )");
    SceneNode* hull = find(scene, "Hull");
    SceneNode* boatSeat = find(scene, "BoatSeat");
    if (!hull || !boatSeat) { std::cerr << "couldn't build the boat\n"; return 1; }
    play(session, scene, 3.0f);
    const glm::vec3 afloat = pos(hull), boatFront = front(hull);
    check(afloat.y > 103.0f, "the wooden boat floats (y " + std::to_string(afloat.y) + ")");
    run(session, "workspace.Boat.BoatSeat.Throttle = 1");
    play(session, scene, 4.0f);
    const float sailed = glm::dot(pos(hull) - afloat, boatFront);
    check(sailed > 6.0f, "a script's Throttle = 1 pushes the boat forward (" + std::to_string(sailed) + " studs)");
    check(pos(hull).y > 103.0f, "and it stays afloat");
    run(session, "workspace.Boat.BoatSeat.Throttle = 0 workspace.Boat.BoatSeat.Steer = -1");
    const glm::vec3 boatNow = front(hull);
    play(session, scene, 2.0f);
    check(glm::cross(boatNow, front(hull)).y > 0.1f, "Steer = -1 turns it left");

    session.stop();
    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
