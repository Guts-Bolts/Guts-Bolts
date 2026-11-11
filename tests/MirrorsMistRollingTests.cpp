// "Mirrors, Mist and Rolling Barrels": shiny parts mirror the world (screen-space
// reflections, High and up), height fog, cylinders that are really round (they roll),
// and air that slows light parts and lets the wind blow them about.
//
//   MirrorsMistRollingTests <a game file with a baseplate>
#include "core/Log.h"
#include "core/Scalability.h"
#include "core/Settings.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Environment.h"
#include "scene/Physics.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <glm/glm.hpp>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

SceneNode* addPart(Scene& scene, const std::string& name, PrimitiveType type, glm::vec3 at, glm::vec3 size,
                   glm::vec3 rot = glm::vec3(0.0f), bool anchored = false) {
    SceneNode* p = scene.addNode(name, type, MeshLibrary::get(type));
    p->transform.position = at;
    p->transform.scale = size;
    p->transform.rotation = rot;
    p->anchored = anchored;
    return p;
}

void simulate(Scene& scene, float seconds) {
    Physics phys;
    std::vector<uint64_t> fallen;
    const int steps = (int)(seconds * 60.0f);
    for (int i = 0; i < steps; ++i) { phys.gather(scene); phys.stepParts(scene, 1.0f / 60.0f, fallen); }
}

// The fog sum the lit shader does (Shaders.h fogAmount), in C++.
float fogAmount(const Environment& e, glm::vec3 eye, glm::vec3 p) {
    float k = e.fogDensity * glm::length(p - eye);
    if (e.fogFalloff > 0.0f) {
        const float t = e.fogFalloff * (p.y - eye.y);
        const float along = std::abs(t) > 0.01f ? (1.0f - std::exp(-t)) / t : 1.0f - t * 0.5f;
        k *= std::exp(std::min(-e.fogFalloff * (eye.y - e.fogHeight), 8.0f)) * along;
    }
    return 1.0f - std::exp(-std::max(k, 0.0f));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: MirrorsMistRollingTests <game file>\n"; return 2; }
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

    // --- shiny reflections follow the Reflections quality group ---
    {
        GraphicsSettings s;
        s.applyPreset(GraphicsSettings::Low);
        check(s.shinyReflections() == 0, "Low: shiny parts only reflect the sky");
        s.applyPreset(GraphicsSettings::Medium);
        check(s.shinyReflections() == 0, "Medium: still only the sky (and water reflections)");
        s.applyPreset(GraphicsSettings::High);
        check(s.shinyReflections() == 1, "High: shiny parts mirror the world");
        s.applyPreset(GraphicsSettings::Ultra);
        check(s.shinyReflections() == 2, "Epic: longer reflection rays");
        s.applyPreset(GraphicsSettings::Cinematic);
        check(s.shinyReflections() == 3, "Cinematic: the longest");
        s.applyPreset(GraphicsSettings::VeryLow);
        check(s.shinyReflections() == 0, "Very Low: off");
        GraphicsSettings phone;
        phone.setDevice("Apple A15 GPU", Scalability::Phone);
        phone.applyPreset(GraphicsSettings::Ultra);
        check(phone.shinyReflections() == 0, "phones stay on sky reflections until tested on a real one");
    }

    // --- height fog ---
    {
        Environment e;
        e.fogDensity = 0.02f;
        const glm::vec3 eye(0, 5, 0);
        const float flatBefore = fogAmount(e, eye, {100, 5, 0});
        check(std::abs(flatBefore - (1.0f - std::exp(-0.02f * 100.0f))) < 1e-4f, "falloff 0 is the old fog, exactly");
        e.fogFalloff = 0.05f;
        e.fogHeight = 0.0f;
        const float low = fogAmount(e, eye, {100, 0, 0}), high = fogAmount(e, eye, {100, 80, 0});
        check(low > high * 2.0f, "height fog: thick looking down a valley, thin looking up at a tower");
        check(fogAmount(e, {0, 200, 0}, {100, 200, 0}) < 0.01f, "and nearly clear high above the mist");
        Environment back;
        e.fogHeight = 12.5f;
        Serializer::environmentFromString(back, Serializer::environmentToString(e));
        check(back.fogFalloff == 0.05f && back.fogHeight == 12.5f, "height fog is saved with the lighting");
        Environment rain;
        rain.weather = 1;
        rain.weatherAmount = 1.0f;
        check(withWeather(rain).fogFalloff > 0.0f, "rain leaves a low mist on the ground");
        check(EnvironmentPresets::horror().fogFalloff > 0.0f, "the horror look has mist round your ankles");
        check(run(session, "game.Lighting.FogFalloff = 0.07; game.Lighting.FogHeight = 3; "
                           "assert(math.abs(game.Lighting.FogFalloff - 0.07) < 1e-5 and game.Lighting.FogHeight == 3)", err) &&
              std::abs(scene.environment().fogFalloff - 0.07f) < 1e-5f && scene.environment().fogHeight == 3.0f,
              "scripts can set Lighting.FogFalloff and FogHeight (" + err + ")");
    }

    // --- round cylinders ---
    {
        scene.world().airDensity = 0.0f;   // (just the shapes here)
        // A slope going down towards +X, tilted 15 degrees.
        SceneNode* slope = addPart(scene, "Slope", PrimitiveType::Cube, {1000, 0, 0}, {80, 1, 30}, {0, 0, -15}, true);
        // A barrel lying on its side (its middle line along Z), and a box the same size beside it.
        SceneNode* barrel = addPart(scene, "Barrel", PrimitiveType::Cylinder, {990, 4.2f, -6}, {2, 4, 2}, {90, 0, 0});
        SceneNode* crate = addPart(scene, "Crate", PrimitiveType::Cube, {990, 4.2f, 6}, {2, 4, 2}, {90, 0, 0});
        slope->material = Material::Concrete;   // (grippy, so a box can't slide down it)
        crate->material = Material::Concrete;
        simulate(scene, 0.25f);   // (both settle onto the slope first)
        const glm::vec3 b0 = barrel->transform.position, c0 = crate->transform.position;
        simulate(scene, 1.25f);
        const float rolled = barrel->transform.position.x - b0.x, slid = crate->transform.position.x - c0.x;
        check(rolled > 6.0f, "a barrel on its side rolls down a slope (" + std::to_string(rolled) + " studs)");
        check(slid < 1.5f, "a concrete box the same size stays put (" + std::to_string(slid) + " studs)");
        check(std::abs(barrel->transform.position.z - b0.z) < 1.0f, "and the barrel rolls straight, not sideways");
        check(glm::length(barrel->angularVelocity) > 3.0f, "it's really spinning as it goes");
        scene.removeNode(barrel);
        scene.removeNode(crate);
        scene.removeNode(slope);

        // On flat ground: lying down it rests on its round side, standing up on its end.
        SceneNode* floor = addPart(scene, "Floor", PrimitiveType::Cube, {1200, -0.5f, 0}, {60, 1, 60}, {0, 0, 0}, true);
        SceneNode* log = addPart(scene, "Log", PrimitiveType::Cylinder, {1200, 1.3f, 0}, {2, 6, 2}, {0, 0, 90});
        SceneNode* drum = addPart(scene, "Drum", PrimitiveType::Cylinder, {1210, 2.2f, 0}, {3, 4, 3});
        SceneNode* ball = addPart(scene, "Ball", PrimitiveType::Sphere, {1210, 6.0f, 0}, {1, 1, 1});
        simulate(scene, 2.0f);
        check(std::abs(log->transform.position.y - 1.0f) < 0.1f, "a log rests on its round side, as high as its radius (" +
                                                                    std::to_string(log->transform.position.y) + ")");
        check(std::abs(log->transform.position.x - 1200.0f) < 0.5f && glm::length(log->velocity) < 0.5f, "and lies still on flat ground");
        check(std::abs(drum->transform.position.y - 2.0f) < 0.1f, "a drum stands on its end (" + std::to_string(drum->transform.position.y) + ")");
        check(std::abs(ball->transform.position.y - 4.5f) < 0.15f, "a ball can sit on top of the drum (" + std::to_string(ball->transform.position.y) + ")");
        // A ball rolling into a standing post bounces off its round side.
        SceneNode* post = addPart(scene, "Post", PrimitiveType::Cylinder, {1220, 3, 0}, {2, 6, 2}, {0, 0, 0}, true);
        SceneNode* roller = addPart(scene, "Roller", PrimitiveType::Sphere, {1214, 0.5f, 0.3f}, {1, 1, 1});
        roller->velocity = {15, 0, 0};
        simulate(scene, 1.0f);
        check(roller->transform.position.x < 1219.0f + 0.1f, "a rolling ball can't go through a round post");
        for (SceneNode* n : {floor, log, drum, ball, post, roller}) scene.removeNode(n);
    }

    // --- air and wind ---
    {
        auto drop = [&](float air, glm::vec3 wind, glm::vec3& sheetEnd, glm::vec3& brickEnd) {
            scene.world().airDensity = air;
            scene.world().globalWind = wind;
            SceneNode* sheet = addPart(scene, "Sheet", PrimitiveType::Cube, {1400, 120, 0}, {6, 0.2f, 6});
            sheet->density = 0.01f;   // (foam: about as light as parts can be)
            SceneNode* brick = addPart(scene, "Brick", PrimitiveType::Cube, {1420, 120, 0}, {2, 1, 2});
            simulate(scene, 1.0f);
            sheetEnd = sheet->transform.position;
            brickEnd = brick->transform.position;
            scene.removeNode(sheet);
            scene.removeNode(brick);
        };
        glm::vec3 sheetNoAir, brickNoAir, sheetAir, brickAir, sheetWind, brickWind;
        drop(0.0f, glm::vec3(0.0f), sheetNoAir, brickNoAir);
        check(std::abs(sheetNoAir.y - brickNoAir.y) < 0.2f, "with no air, a foam sheet and a brick fall together");
        drop(0.0012f, glm::vec3(0.0f), sheetAir, brickAir);
        check(sheetAir.y > brickAir.y + 20.0f, "with real air, the light sheet floats down slowly (" + std::to_string(sheetAir.y) +
                                                   " vs " + std::to_string(brickAir.y) + ")");
        check(std::abs(brickAir.y - brickNoAir.y) < 2.0f, "but a brick hardly notices the air");
        drop(0.0012f, glm::vec3(30, 0, 0), sheetWind, brickWind);
        check(sheetWind.x - 1400.0f > 3.0f, "the wind blows the sheet along (" + std::to_string(sheetWind.x - 1400.0f) + " studs)");
        check(std::abs(brickWind.x - 1420.0f) < 0.5f, "and barely moves the brick");
        check(run(session, "workspace.AirDensity = 0.005; assert(math.abs(workspace.AirDensity - 0.005) < 1e-6)", err) &&
              std::abs(scene.world().airDensity - 0.005f) < 1e-6f, "scripts can set workspace.AirDensity (" + err + ")");
        check(WorldSettings{}.airDensity > 0.0f, "air is on in new games (real-world thickness)");
        scene.world().airDensity = 0.0012f;
        scene.world().globalWind = glm::vec3(0.0f);
    }

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
