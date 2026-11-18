// A real Studio playtest (PlaySnapshot.h), smoother and new weather (Weather.h, Storms.h)
// and better terrain (TerrainShape.cpp).
//
//   PlaytestWeatherTerrainTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Disasters.h"
#include "scene/Environment.h"
#include "scene/PlaySnapshot.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Storms.h"
#include "scene/Terrain.h"
#include "scene/Weather.h"

#include <imgui.h>
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

int countParts(Scene& scene) {
    int n = 0;
    scene.forEach([&](SceneNode* node) { if (node->isPart()) ++n; });
    return n;
}

// How bumpy the ground is: the average height difference between neighbours.
float roughness(const Terrain& t) {
    double sum = 0.0;
    int n = 0;
    for (int k = 0; k < t.points(); ++k)
        for (int i = 1; i < t.points(); ++i) { sum += std::fabs(t.height(i, k) - t.height(i - 1, k)); ++n; }
    return n ? (float)(sum / n) : 0.0f;
}

bool allFinite(const Terrain& t) {
    for (int k = 0; k < t.points(); ++k)
        for (int i = 0; i < t.points(); ++i)
            if (!std::isfinite(t.height(i, k)) || t.height(i, k) < Terrain::kMinHeight || t.height(i, k) > Terrain::kMaxHeight) return false;
    return true;
}

int countMaterial(const Terrain& t, TerrainMaterial m) {
    int n = 0;
    for (int k = 0; k < t.points(); ++k)
        for (int i = 0; i < t.points(); ++i) n += t.material(i, k) == m;
    return n;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: PlaytestWeatherTerrainTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // ---- A real playtest: everything back on Stop -------------------------------------
    {
        Scene scene;
        GameSession session(&scene);
        std::string err;
        if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
        // Some grass terrain with heights a saved file can't keep exactly.
        scene.terrain().create(32, Terrain::kDefaultCell, 0.0f, TerrainMaterial::Grass);
        for (int k = 0; k < scene.terrain().points(); ++k)
            for (int i = 0; i < scene.terrain().points(); ++i) scene.terrain().setHeight(i, k, 0.1234567f * (float)((i * 7 + k * 3) % 11));
        Terrain built;
        built.copyFrom(scene.terrain());
        const Environment lighting = scene.environment();
        const int parts = countParts(scene);
        const std::string before = Serializer::saveScene(scene);

        PlaySnapshot snap;
        snap.take(scene);
        check(snap.differences(scene).empty(), "nothing differs right after the snapshot");
        // A saved place loads back to the very same text (else Stop would wrongly warn).
        {
            Scene again;
            Serializer::loadScene(again, before);
            check(Serializer::saveScene(again) == before, "a place saves the same after loading it again");
        }

        session.setRunOnly(true);
        session.scripts().setNoLocalPlayer(true);
        session.start();
        // The playtest: it snows on the grass, a script digs and paints the terrain, a part
        // is made and another deleted, a tornado and a disaster start.
        check(run(session, "game.Lighting.Weather = 'Snow' game.Lighting.WeatherIntensity = 1 "
                           "workspace.Terrain:FillBall(Vector3.new(0, 0, 0), 12, 'Snow') "
                           "workspace.Terrain:Sculpt('Paint', Vector3.new(20, 0, 20), 10, 1, 'Snow') "
                           "local p = Instance.new('Part') p.Parent = workspace "
                           "for _, c in ipairs(workspace:GetChildren()) do if c:IsA('BasePart') and c ~= p then c:Destroy() break end end "
                           "game.Lighting:SpawnTornado(Vector3.new(0, 0, 40), 2)", err),
              "a playtest makes it snow, changes the terrain and the parts " + err);
        Disasters::spawn(scene, Disasters::defaults(Disasters::Kind::Tornado));
        for (int i = 0; i < 30; ++i) session.update(1.0f / 30.0f, 0.0f, false);
        check(scene.environment().weather == (int)Weather::Snow, "(it is snowing in the playtest)");
        check(!scene.terrain().sameAs(built), "(the playtest changed the terrain)");
        check(Disasters::any(scene), "(a disaster is going)");
        check(!snap.differences(scene).empty(), "the snapshot notices what the playtest changed: " + snap.differences(scene));

        // Stop.
        session.stop();
        check(snap.restore(scene), "Stop puts the world back");
        check(scene.terrain().sameAs(built), "the terrain is back exactly, to the last bit (not rounded like a saved file)");
        check(scene.environment().weather == lighting.weather && scene.environment().weatherAmount == lighting.weatherAmount,
              "the weather is back to what it was before Play");
        check(countParts(scene) == parts, "the deleted part is back and the new one gone");
        check(!Disasters::any(scene) && !Storms::active(scene), "no disaster or tornado is left after Stop");
        check(!scene.disasters().markersStarted, "Studio disaster spawners go off again on the next Play");
        check(snap.differences(scene).empty(), "nothing differs after Stop: " + snap.differences(scene));
        check(Serializer::saveScene(scene) == before, "the place saves exactly as it did before Play");

        // The snow lying on the ground is gone at once too (the renderer's weather is told
        // no game is running), and doesn't melt away slowly over the built place.
        WeatherSim sim;
        Scene snowy;
        snowy.environment().weather = (int)Weather::Snow;
        snowy.environment().weatherAmount = 1.0f;
        for (int i = 0; i < 400; ++i) sim.update(snowy, glm::vec3(0, 5, 0), 0.1f, true);
        check(sim.snowCover() > 0.9f, "(snow settled during the playtest)");
        snowy.environment().weather = 0;
        sim.update(snowy, glm::vec3(0, 5, 0), 1.0f / 60.0f, false);
        check(sim.snowCover() == 0.0f && sim.wetness() == 0.0f, "after Stop no snow is left lying on the grass");
        check(sim.mix()[(int)Weather::Snow] == 0.0f, "and the grey snow sky is gone at once");
        // While building, a weather shows straight away (not after 30 seconds).
        snowy.environment().weather = (int)Weather::Snow;
        sim.update(snowy, glm::vec3(0, 5, 0), 1.0f / 60.0f, false);
        check(sim.snowCover() == 1.0f, "in Studio's Build mode snow shows at once, as the Lighting says");

        // Disasters::reset keeps counting bolts (renderers remember the last one they drew).
        scene.disasters().addBolt(glm::vec3(0), 1.0f);
        const uint32_t serial = scene.disasters().boltSerial;
        Disasters::reset(scene);
        check(scene.disasters().boltSerial == serial && scene.disasters().bolts.empty(), "a reset keeps the bolt count going");
    }

    // ---- Weather: rolling in, and the new kinds ----------------------------------------
    {
        Scene scene;
        WeatherSim sim;
        const glm::vec3 eye(0, 5, 0);
        sim.update(scene, eye, 0.1f, false);   // clear
        scene.environment().weather = (int)Weather::Rain;
        scene.environment().weatherAmount = 1.0f;
        sim.update(scene, eye, 0.1f, true);
        for (int i = 0; i < 9; ++i) sim.update(scene, eye, 0.1f, true);   // one second
        const float m1 = sim.mix()[(int)Weather::Rain];
        check(m1 > 0.1f && m1 < 0.3f, "rain rolls in over a few seconds, not in one frame (" + std::to_string(m1) + " after 1 s)");
        const Environment clear = scene.environment(), full = withWeather(scene.environment()), part = sim.shown(scene.environment());
        check(part.sunIntensity < clear.sunIntensity && part.sunIntensity > full.sunIntensity, "the sun dims part-way while it rolls in");
        for (int i = 0; i < 70; ++i) sim.update(scene, eye, 0.1f, true);
        check(sim.mix()[(int)Weather::Rain] == 1.0f, "after a few seconds it is fully raining");
        check(std::fabs(sim.shown(scene.environment()).fogDensity - full.fogDensity) < 1e-5f, "fully in, it looks exactly like the weather");
        scene.environment().weather = 0;
        sim.update(scene, eye, 0.5f, true);
        check(sim.mix()[(int)Weather::Rain] > 0.5f, "clearing up takes a while too");

        // The new kinds.
        Environment e;
        e.weatherAmount = 0.7f;
        Environment rain = e; rain.weather = (int)Weather::Rain;
        Environment fog = e; fog.weather = (int)Weather::Fog;
        Environment sand = e; sand.weather = (int)Weather::Sandstorm;
        Environment bliz = e; bliz.weather = (int)Weather::Blizzard;
        check(withWeather(fog).fogDensity > withWeather(rain).fogDensity * 1.5f && withWeather(fog).fogFalloff > withWeather(rain).fogFalloff, "fog is much thicker than rain's mist");
        const Environment s = withWeather(sand);
        check(s.skyHorizon.r > s.skyHorizon.b * 1.4f && s.fogColor.r > s.fogColor.b * 1.4f, "a sandstorm turns the sky and haze orange-brown");
        Environment snow = e; snow.weather = (int)Weather::Snow;
        check(withWeather(bliz).fogDensity > withWeather(snow).fogDensity, "a blizzard is a white-out, thicker than snow");
        check(isSnowing((int)Weather::Blizzard) && !isRaining((int)Weather::Sandstorm), "a blizzard snows, a sandstorm doesn't rain");
        check(std::string(kWeatherNames[(int)Weather::Sandstorm]) == "Sandstorm" && kWeatherCount == 8, "the weather names include Fog, Blizzard and Sandstorm");

        Scene foggy; foggy.environment() = fog;
        WeatherSim fs;
        for (int i = 0; i < 20; ++i) fs.update(foggy, eye, 0.1f, true);
        check(fs.streaks().empty(), "nothing falls in fog");
        Scene dusty; dusty.environment() = sand;
        WeatherSim ds;
        for (int i = 0; i < 80; ++i) { Storms::follow(dusty, 0.1f); ds.update(dusty, eye, 0.1f, true); }
        check(!ds.streaks().empty() && ds.streaks()[0].tint.r > ds.streaks()[0].tint.b, "a sandstorm fills the air with brown dust");
        check(ds.snowCover() == 0.0f && ds.wetness() == 0.0f, "dust doesn't make things wet or snowy");
        // The gale: it pushes things, comes in gusts, and dies down when it clears.
        const glm::vec3 w = Storms::windAt(dusty, glm::vec3(0));
        check(glm::length(glm::vec2(w.x, w.z)) > 15.0f && Storms::active(dusty), "a sandstorm blows hard enough to push things");
        const glm::vec3 later = (Storms::follow(dusty, 1.3f), Storms::windAt(dusty, glm::vec3(0)));
        check(glm::length(later - w) > 0.01f, "the wind comes in gusts");
        check(glm::length(Storms::characterPush(dusty, glm::vec3(0), glm::vec3(0))) > 0.5f, "players feel a sandstorm lean on them");
        dusty.environment().weather = 0;
        for (int i = 0; i < 300; ++i) Storms::follow(dusty, 0.1f);
        check(dusty.storms().gale == 0.0f, "the wind dies down when the sandstorm ends");
        Scene snowStorm; snowStorm.environment() = bliz;
        WeatherSim bs;
        for (int i = 0; i < 300; ++i) { Storms::follow(snowStorm, 0.1f); bs.update(snowStorm, eye, 0.1f, true); }
        check(bs.snowCover() > 0.99f, "a blizzard buries everything in snow");
        bool sideways = false;
        for (const auto& st : bs.streaks()) {
            const glm::vec3 d = st.bottom - st.top;
            if (glm::length(glm::vec2(d.x, d.z)) > std::fabs(d.y)) { sideways = true; break; }
        }
        check(sideways, "blizzard flakes fly sideways in the wind");
    }

    // ---- Terrain ------------------------------------------------------------------------
    {
        for (int s = 0; s < kTerrainStyleCount; ++s) {
            Terrain t;
            t.generateStyle((TerrainStyle)s, 64, Terrain::kDefaultCell, 42u, 0.7f);
            float lo = 1e9f, hi = -1e9f;
            for (int k = 0; k < t.points(); ++k)
                for (int i = 0; i < t.points(); ++i) { lo = std::min(lo, t.height(i, k)); hi = std::max(hi, t.height(i, k)); }
            check(!t.empty() && allFinite(t) && hi - lo > 2.0f, std::string("Generate makes ") + kTerrainStyleNames[s] +
                  " (heights " + std::to_string((int)lo) + " to " + std::to_string((int)hi) + ")");
        }
        Terrain a, b;
        a.generateStyle(TerrainStyle::Mountains, 64, 4.0f, 7u, 1.0f);
        b.generateStyle(TerrainStyle::Mountains, 64, 4.0f, 7u, 1.0f);
        check(a.sameAs(b), "the same seed makes the same land");
        check(countMaterial(a, TerrainMaterial::Rock) > 0, "mountains have rock cliffs");
        {
            Terrain big;
            big.generateStyle(TerrainStyle::Mountains, 128, 4.0f, 3u, 1.0f);
            check(countMaterial(big, TerrainMaterial::Snow) > 0, "big mountains have snowy tops");
        }
        Terrain isl;
        isl.generateStyle(TerrainStyle::Islands, 96, 4.0f, 5u, 0.6f);
        float h0 = 0.0f;
        check(isl.heightAt(0, 0, h0) && h0 > 0.0f, "there is always an island in the middle of Islands");
        check(isl.height(0, 0) < 0.0f, "the edge of Islands is under the sea");
        check(countMaterial(isl, TerrainMaterial::Sand) > 0, "islands have sandy beaches");
        Terrain dunes;
        dunes.generateStyle(TerrainStyle::Dunes, 64, 4.0f, 9u, 0.8f);
        check(countMaterial(dunes, TerrainMaterial::Grass) == 0 && countMaterial(dunes, TerrainMaterial::Sand) > 0, "dunes are sand, no grass");

        // Erosion.
        Terrain hills;
        hills.generate(64, 4.0f, 11u, 1.0f);
        Terrain original;
        original.copyFrom(hills);
        const uint64_t v = hills.version();
        hills.erode(0.6f, 3u);
        check(allFinite(hills) && !hills.sameAs(original), "Erode changes the land and keeps every height sensible");
        check(hills.version() > v, "eroded land is redrawn");
        check(roughness(hills) < roughness(original) * 1.2f, "erosion doesn't tear the ground into spikes");
        Terrain e1, e2;
        e1.copyFrom(original); e2.copyFrom(original);
        e1.erode(0.4f, 5u); e2.erode(0.4f, 5u);
        check(e1.sameAs(e2), "the same rain erodes the same way");
        // The Erode brush only touches the circle.
        Terrain local;
        local.copyFrom(original);
        local.brush(Terrain::Brush::Erode, glm::vec3(30, 0, 30), 16.0f, 1.0f, TerrainMaterial::Grass);
        bool outsideSame = true, insideChanged = false;
        for (int k = 0; k < local.points(); ++k)
            for (int i = 0; i < local.points(); ++i) {
                const glm::vec3 p = local.pointPos(i, k);
                const float d = std::sqrt((p.x - 30) * (p.x - 30) + (p.z - 30) * (p.z - 30));
                const bool same = local.height(i, k) == original.height(i, k);
                if (d > 16.0f + 3 * 4.0f && !same) outsideSame = false;
                if (d < 16.0f && !same) insideChanged = true;
            }
        check(insideChanged && outsideSame, "the Erode brush changes only the ground under it");

        // Auto Paint.
        Terrain cliff;
        cliff.create(32, 4.0f, 0.0f, TerrainMaterial::Grass);
        for (int k = 0; k < cliff.points(); ++k)
            for (int i = 16; i < cliff.points(); ++i) cliff.setHeight(i, k, 30.0f);
        cliff.autoPaint();
        check(cliff.material(16, 10) == TerrainMaterial::Rock || cliff.material(15, 10) == TerrainMaterial::Rock, "Auto Paint makes cliffs rock");
        check(cliff.material(4, 10) != TerrainMaterial::Rock && cliff.material(28, 10) != TerrainMaterial::Rock, "and leaves flat ground green");

        // Exact copies vs a saved file.
        Terrain odd;
        odd.create(16, 4.0f, 0.0f, TerrainMaterial::Grass);
        odd.setHeight(3, 3, 1.0f / 3.0f);
        Terrain fromFile;
        fromFile.fromJson(odd.toJson());
        Terrain copy;
        copy.copyFrom(odd);
        check(!fromFile.sameAs(odd) && copy.sameAs(odd), "a copy keeps heights a saved file would round");
    }

    std::cout << (failures ? "\nSOME CHECKS FAILED\n" : "\nall checks passed\n");
    return failures ? 1 : 0;
}
