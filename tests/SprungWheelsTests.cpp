// Cars with real suspension (scene/SprungWheels.h): wheels called "Wheel" that aren't joined
// to anything hang from the chassis on springs. The Buggy (Toolbox) should rest the way it
// was built, bounce and settle when dropped, drive, steer, brake, stay parked on the
// handbrake, and be readable and changeable from Lua.
//
//   SprungWheelsTests <a game file with a baseplate>
#include "core/Log.h"
#include "editor/Premades.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Physics.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/SprungWheels.h"
#include "scene/Vehicles.h"

#include <imgui.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
std::string num(float v) { return std::to_string(v); }

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

SceneNode* find(SceneNode* n, const std::string& name) {
    if (n->name == name) return n;
    for (auto& c : n->children)
        if (SceneNode* f = find(c.get(), name)) return f;
    return nullptr;
}

glm::vec3 posOf(const SceneNode* n) { return glm::vec3(n->worldMatrix()[3]); }
glm::vec3 facing(const SceneNode* n) { return glm::normalize(-glm::vec3(n->worldMatrix()[2])); }

struct World {
    Scene& scene;
    Physics phys;
    std::vector<uint64_t> fallen;
    // `seconds` of play, the way GameSession does it: the seats, then the physics.
    void play(float seconds, const std::function<void()>& each = {}) {
        const float dt = 1.0f / 60.0f;
        for (int i = 0; i < (int)std::lround(seconds / dt); ++i) {
            scene.vehicles().update(scene, dt, true);
            phys.gather(scene);
            phys.stepParts(scene, dt, fallen);
            if (each) each();
        }
    }
};
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: SprungWheelsTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // --- The maths on its own ---
    {
        SprungWheels::Car car;
        car.wheels.resize(4);
        car.set.travel = 1.0f;
        car.set.stiffness = 1.0f;
        const float k = SprungWheels::springRate(car, 100.0f, 60.0f);
        check(std::abs(k * 0.5f - 100.0f * 60.0f / 4.0f) < 1.0f, "each spring holds up a quarter of the car halfway squashed");
        car.set.stiffness = 2.0f;
        check(SprungWheels::springRate(car, 100.0f, 60.0f) > k * 1.9f, "Stiffness 2 = springs twice as strong");
    }

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    // Somewhere of our own: a big flat floor far from the game.
    const glm::vec3 at(600.0f, 100.0f, 600.0f);
    SceneNode* floor = scene.addNode("TestFloor", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    floor->transform.position = at + glm::vec3(0, -0.5f, -100.0f);
    floor->transform.scale = {300.0f, 1.0f, 400.0f};
    floor->material = Material::Concrete;
    SceneNode* buggy = buildPremade(scene, Premade::Buggy, at);
    SceneNode* chassis = find(buggy, "Chassis");
    SceneNode* seat = find(buggy, "VehicleSeat");
    SceneNode* wheel = find(buggy, "Wheel");
    check(chassis && seat && wheel, "the Buggy has a chassis, a VehicleSeat and wheels");
    if (!chassis || !seat || !wheel) return 1;
    check(SprungWheels::hasWheels(buggy), "its unjoined \"Wheel\" parts are sprung wheels");
    const float builtY = posOf(chassis).y;
    const glm::vec3 wheelBuilt = posOf(wheel) - posOf(chassis);

    World w{scene};
    Vehicles& v = scene.vehicles();

    // --- Resting ---
    w.play(3.0f);
    const SprungWheels::Car* car = v.sprung().carOf(seat->id);
    check(car != nullptr, "the physics found the car");
    if (!car) return 1;
    check(car->wheels.size() == 4, "with four wheels (" + std::to_string(car->wheels.size()) + ")");
    check(std::abs(posOf(chassis).y - builtY) < 0.3f, "parked, it rests the way it was built (" + num(posOf(chassis).y - builtY) + " studs off)");
    check(car->onGround == 4, "all four wheels on the ground");
    check(glm::length((posOf(wheel) - posOf(chassis)) - wheelBuilt) < 0.4f, "the wheels stay where they were put, not falling off");
    bool spreadOk = true;
    for (const auto& wh : car->wheels) spreadOk = spreadOk && wh.compression > 0.2f && wh.compression < 1.2f;
    check(spreadOk, "each spring is part squashed holding the car up");

    // --- Dropped: the springs squash, bounce and settle ---
    {
        chassis->transform.position.y += 3.0f;
        chassis->velocity = glm::vec3(0.0f);
        chassis->sleepTime = 0.0f;
        float lowest = 1e9f;
        float most = 0.0f;
        w.play(4.0f, [&] {
            lowest = std::min(lowest, posOf(chassis).y);
            for (const auto& wh : car->wheels) most = std::max(most, wh.compression);
        });
        check(lowest < builtY - 0.15f, "landing squashes the springs (dipped " + num(builtY - lowest) + " studs)");
        check(most > 0.9f, "the wheels get pushed right up on landing");
        check(std::abs(posOf(chassis).y - builtY) < 0.3f, "then it settles back to its ride height");
        check(std::abs(facing(chassis).y) < 0.1f, "and stays level");
    }

    // --- Driving ---
    {
        const glm::vec3 start = posOf(chassis);
        const glm::vec3 fwd = facing(seat);
        v.scriptSet(seat->id, true, 1.0f);
        w.play(3.0f);
        const float went = glm::dot(posOf(chassis) - start, fwd);
        check(went > 20.0f, "full throttle drives it forward (" + num(went) + " studs in 3 s)");
        check(car->speed > 12.0f && car->speed < 26.0f, "near its MaxSpeed of 24 (" + num(car->speed) + ")");
        check(std::abs(posOf(chassis).y - builtY) < 0.5f, "driving along on its wheels");

        // Steering right turns it right (clockwise seen from above).
        const glm::vec3 before = facing(chassis);
        v.scriptSet(seat->id, false, 1.0f);
        float steered = 0.0f;
        w.play(1.5f, [&] { steered = std::max(steered, car->steerAngle); });
        const glm::vec3 after = facing(chassis);
        const float turn = glm::cross(before, after).y;   // (negative = clockwise)
        check(steered > 0.2f, "the front wheels turn when steering (" + num(glm::degrees(steered)) + " degrees)");
        check(turn < -0.2f, "steering right turns the car right");
        int steering = 0;
        for (const auto& wh : car->wheels) steering += wh.steers ? 1 : 0;
        check(steering == 2, "only the two front wheels steer");
        check(std::abs(facing(chassis).y) < 0.25f, "it leans in the turn but doesn't flip");

        // Letting go: nobody's driving, so the handbrake stops it.
        v.scriptSet(seat->id, true, 0.0f);
        v.scriptSet(seat->id, false, 0.0f);
        w.play(3.0f);
        check(std::abs(car->speed) < 1.0f, "let go, it comes to a stop (" + num(car->speed) + ")");
    }

    // --- Reversing and braking ---
    {
        v.scriptSet(seat->id, true, 1.0f);
        w.play(2.0f);
        const float fast = car->speed;
        v.scriptSet(seat->id, true, -1.0f);
        float stoppedAfter = -1.0f;
        float t = 0.0f;
        w.play(3.0f, [&] { t += 1.0f / 60.0f; if (stoppedAfter < 0.0f && car->speed < 0.5f) stoppedAfter = t; });
        check(stoppedAfter > 0.0f && stoppedAfter < fast / 16.0f + 0.6f, "pressing back brakes hard (stopped from " + num(fast) + " in " + num(stoppedAfter) + " s)");
        check(car->speed < -2.0f, "then reverses (" + num(car->speed) + ")");
        v.scriptSet(seat->id, true, 0.0f);
        w.play(2.0f);
    }

    // --- Spinning wheels ---
    {
        const float spin0 = car->wheels[0].spin;
        v.scriptSet(seat->id, true, 1.0f);
        w.play(0.5f);
        check(std::abs(car->wheels[0].spin - spin0) > 0.1f, "the wheels roll as it goes");
        v.scriptSet(seat->id, true, 0.0f);
        w.play(2.0f);
    }

    // --- Lua ---
    {
        const bool ok = run(session,
            "local seat = workspace:FindFirstChild('Buggy', true):FindFirstChild('VehicleSeat', true)\n"
            "assert(seat.ClassName == 'VehicleSeat')\n"
            "assert(seat.Stiffness == 1 and math.abs(seat.SpringTravel - 1.4) < 0.001, 'settings ' .. seat.Stiffness .. ' ' .. seat.SpringTravel)\n"
            "assert(seat.WheelsOnGround == 4, 'on ground ' .. seat.WheelsOnGround)\n"
            "assert(math.abs(seat.Speed) < 1, 'speed ' .. seat.Speed)\n"
            "seat.Stiffness = 2\n"
            "seat.Grip = 0.5\n"
            "assert(seat:GetAttribute('Stiffness') == 2 and seat.Grip == 0.5)\n", err);
        check(ok, "seat.Stiffness / SpringTravel / Grip / WheelsOnGround / Speed from Lua" + (ok ? std::string() : ": " + err));
        w.play(3.0f);
        check(posOf(chassis).y > builtY + 0.2f, "stiffer springs (Stiffness 2) hold it higher (" + num(posOf(chassis).y - builtY) + ")");
        const bool drive = run(session,
            "local seat = workspace:FindFirstChild('Buggy', true):FindFirstChild('VehicleSeat', true)\n"
            "seat.ThrottleFloat = 1\n", err);
        const glm::vec3 start = posOf(chassis);
        w.play(2.0f);
        check(drive && glm::length(posOf(chassis) - start) > 8.0f, "a script drives the empty car with seat.ThrottleFloat");
        run(session, "workspace:FindFirstChild('Buggy', true):FindFirstChild('VehicleSeat', true).ThrottleFloat = 0\n", err);
    }

    // --- Over bumps ---
    {
        Scene s2;
        SceneNode* f2 = s2.addNode("Floor", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
        f2->transform.position = {0, -0.5f, -60.0f};
        f2->transform.scale = {60.0f, 1.0f, 200.0f};
        f2->material = Material::Concrete;
        buildPremade(s2, Premade::BumpyRoad, {0, 0, 0});
        SceneNode* b2 = buildPremade(s2, Premade::Buggy, {0, 0.5f, 6.0f});
        SceneNode* c2 = find(b2, "Chassis");
        SceneNode* seat2 = find(b2, "VehicleSeat");
        World w2{s2};
        w2.play(1.0f);
        s2.vehicles().scriptSet(seat2->id, true, 0.6f);
        float most = 0.0f, least = 1e9f;
        w2.play(5.0f, [&] {
            if (const SprungWheels::Car* c = s2.vehicles().sprung().carOf(seat2->id))
                for (const auto& wh : c->wheels) { most = std::max(most, wh.compression); least = std::min(least, wh.compression); }
        });
        check(most - least > 0.5f, "over the speed bumps the wheels go up and down on their springs (" + num(most - least) + ")");
        check(glm::normalize(glm::vec3(c2->worldMatrix()[1])).y > 0.8f, "and it's still the right way up");
        check(posOf(c2).z < -20.0f, "it got along the road (" + num(posOf(c2).z) + ")");
    }

    session.stop();
    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
