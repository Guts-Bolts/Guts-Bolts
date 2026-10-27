// Weather (Lighting.Weather): scripts can set it, it's saved, and rain stops on roofs.
//
//   WeatherTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Environment.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scene/Weather.h"

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
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

// The lowest point any drop reaches in the columns within `r` studs of (x, z).
float lowestDrop(const WeatherSim& w, float x, float z, float r) {
    float low = 1e9f;
    for (const auto& s : w.streaks())
        if (std::abs(s.bottom.x - x) < r && std::abs(s.bottom.z - z) < r) low = std::min(low, s.bottom.y);
    return low;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: WeatherTests <game file>\n"; return 2; }
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

    // Scripts.
    check(scene.environment().weather == 0, "games start with clear weather");
    check(run(session, "local L = game:GetService('Lighting') L.Weather = 'rain' L.WeatherIntensity = 2", err),
          "a script sets Lighting.Weather " + err);
    check(scene.environment().weather == (int)Weather::Rain, "Weather = 'rain' makes it rain");
    check(scene.environment().weatherAmount == 1.0f, "WeatherIntensity is kept between 0 and 1");
    check(run(session, "assert(game.Lighting.Weather == 'Rain')", err), "reading Lighting.Weather back " + err);
    check(run(session, "game.Lighting.Weather = 'Snow' assert(not pcall(function() game.Lighting.Weather = 'Lava' end)) "
                                       "assert(game.Lighting.Weather == 'Snow') game.Lighting.Weather = 'Rain'", err),
          "unknown weather is an error and changes nothing " + err);

    // Saved and sent to players (the same text).
    Environment copy;
    Serializer::environmentFromString(copy, Serializer::environmentToString(scene.environment()));
    check(copy.weather == (int)Weather::Rain && copy.weatherAmount == 1.0f, "weather survives saving and loading");

    // The look: grey and foggy, but the game's own Lighting isn't touched.
    const Environment shown = withWeather(scene.environment());
    check(shown.fogDensity > scene.environment().fogDensity && shown.cloudCover >= 0.9f, "rain brings clouds and fog");

    // Drops: on the baseplate out in the open, and none under a roof.
    check(run(session, 
              "local p = Instance.new('Part') p.Anchored = true p.Size = Vector3.new(12, 1, 12) "
              "p.Position = Vector3.new(0, 12, 0) p.Parent = workspace", err),
          "a roof " + err);
    WeatherSim sim;
    const glm::vec3 eye(0.0f, 6.0f, 0.0f);
    for (int i = 0; i < 120; ++i) sim.update(scene, eye, 1.0f / 30.0f);
    check(!sim.streaks().empty(), "it rains");
    check(sim.groundAt(0.0f, 0.0f) > 12.0f, "the roof is found");
    check(lowestDrop(sim, 0.0f, 0.0f, 4.0f) > 12.0f, "no rain under the roof");
    check(lowestDrop(sim, 16.0f, 16.0f, 4.0f) < 5.0f, "rain reaches the ground in the open");
    check(sim.wetness() > 0.0f && sim.snowCover() == 0.0f, "rain makes things wet, not snowy");

    run(session, "game.Lighting.Weather = 'Clear'", err);
    sim.update(scene, eye, 1.0f / 30.0f);
    check(sim.streaks().empty(), "clear weather stops the rain");

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "weather tests FAILED\n" : "weather tests passed\n");
    return failures ? 1 : 0;
}
