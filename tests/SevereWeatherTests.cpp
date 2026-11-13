// Severe weather (Storms.h): supercells, tornadoes and downbursts, and the real cloud types.
//
//   SevereWeatherTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Environment.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Storms.h"

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

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

SceneNode* named(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (!found && n->name == name) found = n; });
    return found;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: SevereWeatherTests <game file>\n"; return 2; }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = {1280, 720};
    Mesh::headless = true;
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // ---- The wind of a tornado and of a downburst --------------------------------
    {
        Storms::State st;
        st.tornado = 3.0f;
        st.tornadoPos = {100.0f, 0.0f, 50.0f};
        const float core = Storms::coreRadius(3.0f), peak = Storms::peakWind(3.0f);
        // (70 studs up, where the funnel is 1.42 times as wide as at its foot.)
        const glm::vec3 atEdge = Storms::tornadoWind(st, st.tornadoPos + glm::vec3(core * 1.42f, 70.0f, 0.0f));
        const float turning = glm::length(glm::vec2(atEdge.x, atEdge.z));
        check(std::abs(turning - peak) < peak * 0.12f, "a tornado blows hardest at the edge of its core");
        const glm::vec3 half = Storms::tornadoWind(st, st.tornadoPos + glm::vec3(core * 0.5f, 70.0f, 0.0f));
        check(glm::length(glm::vec2(half.x, half.z)) < turning * 0.65f, "inside the core the air turns like a wheel (slower near the middle)");
        const glm::vec3 out = Storms::tornadoWind(st, st.tornadoPos + glm::vec3(core * 5.0f, 70.0f, 0.0f));
        check(glm::length(glm::vec2(out.x, out.z)) < turning * 0.4f && glm::length(out) > 1.0f, "outside it the wind drops off with distance");
        check(glm::length(Storms::tornadoWind(st, st.tornadoPos + glm::vec3(core * 20.0f, 5.0f, 0.0f))) == 0.0f, "far away there is none");
        check(Storms::tornadoWind(st, st.tornadoPos + glm::vec3(1.0f, 20.0f, 0.0f)).y > peak * 0.5f, "air rushes up the middle");
        const glm::vec3 low = Storms::tornadoWind(st, st.tornadoPos + glm::vec3(core * 2.0f, 2.0f, 0.0f));
        check(low.x < -3.0f, "near the ground it is sucked in");
        check(low.z > 0.0f, "and it turns anticlockwise seen from above");
        st.tornado = 0.0f;
        check(glm::length(Storms::tornadoWind(st, st.tornadoPos + glm::vec3(5.0f, 5.0f, 0.0f))) == 0.0f, "no tornado, no wind from it");
        check(Storms::peakWind(5.0f) > Storms::peakWind(1.0f) * 2.0f && Storms::coreRadius(5.0f) > Storms::coreRadius(1.0f) * 2.0f,
              "an EF5 is much wider and faster than an EF1");

        st.downburst = 1.0f;
        st.downburstPos = {0.0f, 0.0f, 0.0f};
        const float R = Storms::kDownburstRadius;
        check(Storms::downburstWind(st, {0.0f, 100.0f, 0.0f}).y < -40.0f, "a downburst's air drops out of the cloud");
        const glm::vec3 ring = Storms::downburstWind(st, {R, 3.0f, 0.0f});
        check(ring.x > 50.0f && std::abs(ring.y) < 5.0f, "at the ground it bursts outwards");
        check(glm::length(Storms::downburstWind(st, {0.5f, 3.0f, 0.0f})) < 5.0f, "right under the middle it is nearly still");
        const glm::vec3 far = Storms::downburstWind(st, {R * 4.0f, 3.0f, 0.0f});
        check(far.x > 3.0f && far.x < ring.x * 0.5f, "the burst fades with distance");
        const glm::vec3 other = Storms::downburstWind(st, {-R, 3.0f, 0.0f});
        check(other.x < -50.0f, "and goes out in every direction");
    }

    // ---- Cloud types ---------------------------------------------------------------
    check(cloudShape(0, (int)Weather::Clear, 0.4f) == (int)CloudType::Cumulus, "fair weather has cumulus heaps");
    check(cloudShape(0, (int)Weather::Clear, 0.95f) == (int)CloudType::Stratus, "an overcast sky is a stratus sheet");
    check(cloudShape(0, (int)Weather::Rain, 0.4f) == (int)CloudType::Stratus, "rain falls from stratus");
    check(cloudShape(0, (int)Weather::Storm, 0.4f) == (int)CloudType::Cumulonimbus, "a thunderstorm has cumulonimbus");
    check(cloudShape(0, (int)Weather::Supercell, 0.4f) == (int)CloudType::Supercell, "a supercell has its own cloud");
    check(cloudShape((int)CloudType::Cumulus, (int)Weather::Storm, 0.4f) == (int)CloudType::Cumulus, "a chosen cloud type wins over the weather");
    check(isStorm((int)Weather::Supercell) && isRaining((int)Weather::Supercell), "a supercell is a storm: it rains");

    // ---- In a running game ---------------------------------------------------------
    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();
    auto step = [&](int frames) { for (int i = 0; i < frames; ++i) session.update(1.0f / 60.0f, 0.0f, false); };

    check(run(session, "game.Lighting.Weather = 'Supercell' game.Lighting.SevereWeather = false", err), "scripts can set Lighting.Weather = \"Supercell\": " + err);
    check(scene.environment().weather == (int)Weather::Supercell && !scene.environment().severeWeather, "and switch SevereWeather off");
    check(run(session, "game.Lighting.CloudType = 'Cumulonimbus' assert(game.Lighting.CloudType == 'Cumulonimbus') game.Lighting.CloudType = 'Auto'", err),
          "Lighting.CloudType: " + err);
    check(!run(session, "game.Lighting.CloudType = 'Cheese'", err), "a made-up cloud type is an error");
    step(30);
    check(scene.environment().tornado == 0.0f && !Storms::active(scene), "with SevereWeather off no tornado forms by itself");

    // Loose parts round where the tornado will be, and one that is anchored.
    check(run(session, R"(
        local floor = Instance.new('Part')
        floor.Name = 'StormFloor'
        floor.Anchored = true
        floor.Size = Vector3.new(900, 2, 900)
        floor.Position = Vector3.new(300, 58, 300)
        floor.Parent = workspace
        local a = Instance.new('Part')
        a.Name = 'Bolted'
        a.Anchored = true
        a.Size = Vector3.new(4, 0.4, 4)
        a.Position = Vector3.new(305, 80, 305)
        a.Parent = workspace
        local f = Instance.new('Part')
        f.Name = 'FarAway'
        f.Size = Vector3.new(4, 0.4, 4)
        f.Position = Vector3.new(660, 59.3, 300)
        f.Parent = workspace
    )", err), "parts to throw about: " + err);
    step(5);
    check(run(session, "game.Lighting:SpawnTornado(Vector3.new(312, 59, 300), 4, 40)", err), "Lighting:SpawnTornado: " + err);
    check(scene.environment().tornado > 0.0f, "the tornado is in the Lighting straight away (so every player gets it)");
    {
        Environment copy;
        Serializer::environmentFromString(copy, Serializer::environmentToString(scene.environment()));
        check(copy.tornado == scene.environment().tornado && copy.tornadoPos == scene.environment().tornadoPos &&
              copy.weather == (int)Weather::Supercell && copy.severeWeather == scene.environment().severeWeather,
              "the storm is sent to players with the rest of the Lighting");
    }
    step(60 * 9);   // (it has come down and grown to full strength)
    check(scene.storms().tornado > 3.0f, "it grows to full strength in a few seconds");
    const glm::vec3 foot0 = scene.storms().tornadoPos;
    check(run(session, R"(
        local at = game.Lighting.TornadoPosition
        for i = 1, 6 do
            local p = Instance.new('Part')
            p.Name = 'Loose' .. i
            p.Size = Vector3.new(4, 0.4, 4)
            p.Position = Vector3.new(at.X + i * 5 - 15, 59.3, at.Z)
            p.Parent = workspace
        end
    )", err), "loose parts in its path: " + err);
    float highest = 0.0f, furthest = 0.0f;
    for (int i = 0; i < 60 * 6; ++i) {
        session.update(1.0f / 60.0f, 0.0f, false);
        for (int k = 1; k <= 6; ++k)
            if (SceneNode* n = named(scene, "Loose" + std::to_string(k))) {
                const glm::vec3 p(n->worldMatrix()[3]);
                highest = std::max(highest, p.y);
                furthest = std::max(furthest, glm::length(glm::vec2(p.x - (foot0.x + k * 5.0f - 15.0f), p.z - foot0.z)));
            }
    }
    check(highest > 85.0f, "a tornado lifts loose parts high into the air (" + std::to_string(highest) + ")");
    check(furthest > 15.0f, "and carries them off (" + std::to_string(furthest) + " studs)");
    if (SceneNode* a = named(scene, "Bolted")) {
        const glm::vec3 p(a->worldMatrix()[3]);
        check(std::abs(p.x - 305.0f) < 1e-3f && std::abs(p.y - 80.0f) < 1e-3f, "anchored parts stay where they are");
    } else check(false, "the anchored part is still there");
    if (SceneNode* f = named(scene, "FarAway")) check(std::abs(f->worldMatrix()[3].x - 660.0f) < 2.0f, "parts far from it are left alone");
    else check(false, "the far part is still there");
    check(run(session, "local w = game.Lighting:GetWindAt(game.Lighting.TornadoPosition + Vector3.new(" + std::to_string(Storms::coreRadius(4.0f)) +
                       ", 50, 0)) assert(w.Magnitude > 80, 'wind ' .. w.Magnitude) assert(game.Lighting.TornadoStrength > 3)", err),
          "Lighting:GetWindAt and TornadoStrength: " + err);
    {
        // It moves along, and players' computers follow it smoothly.
        const glm::vec3 before = scene.storms().tornadoPos;
        step(120);
        check(glm::length(scene.storms().tornadoPos - before) > 3.0f, "the tornado wanders along its path");
        check(glm::length(scene.storms().tornadoPos - scene.environment().tornadoPos) < 6.0f, "the smooth one stays close to the sent one");
    }
    // A character caught in it is shoved; out of it nothing happens.
    {
        const glm::vec3 foot = scene.storms().tornadoPos;
        const glm::vec3 near = Storms::characterPush(scene, foot + glm::vec3(Storms::coreRadius(4.0f), 3.0f, 0.0f), glm::vec3(0.0f));
        check(glm::length(near) > 40.0f, "a character in the tornado is thrown about");
        check(glm::length(Storms::characterPush(scene, foot + glm::vec3(900.0f, 3.0f, 0.0f), glm::vec3(0.0f))) == 0.0f, "one far away feels nothing");
    }
    check(run(session, "game.Lighting:ClearStorms()", err), "Lighting:ClearStorms: " + err);
    step(90);
    check(scene.environment().tornado == 0.0f && scene.storms().tornado == 0.0f && !Storms::active(scene), "the tornado is gone");

    // A downburst.
    check(run(session, "game.Lighting:SpawnDownburst(Vector3.new(0, 0, 0), 1, 10)", err), "Lighting:SpawnDownburst: " + err);
    step(60 * 5);
    check(scene.storms().downburst > 0.8f && scene.environment().downburst > 0.8f, "the downburst builds up");
    check(run(session, "assert(game.Lighting.DownburstStrength > 0.8) assert(game.Lighting:GetWindAt(Vector3.new(45, 3, 0)).X > 40)", err),
          "scripts can read it: " + err);
    step(60 * 7);
    check(scene.environment().downburst == 0.0f, "and blows itself out");

    // Left alone, a supercell grows its own tornadoes.
    check(run(session, "game.Lighting.SevereWeather = true", err), "SevereWeather back on: " + err);
    bool formed = false, burst = false;
    float strongest = 0.0f;
    for (int i = 0; i < 10 * 200 && !(formed && burst); ++i) {
        Storms::simulate(scene, 0.1f);
        Storms::follow(scene, 0.1f);
        formed = formed || scene.environment().tornado > 0.0f;
        burst = burst || scene.environment().downburst > 0.0f;
        strongest = std::max(strongest, scene.environment().tornado);
    }
    check(formed, "a supercell forms a tornado by itself");
    check(burst, "and storms make downbursts");
    check(strongest <= Storms::kMaxTornado, "never stronger than an EF5");
    check(glm::length(scene.environment().stormCenter) > 1.0f, "the supercell is somewhere on the map and drifts");
    check(run(session, "game.Lighting.Weather = 'Clear' game.Lighting:ClearStorms()", err), "clear weather again: " + err);

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "FAILED" : "all ok") << "\n";
    return failures ? 1 : 0;
}
