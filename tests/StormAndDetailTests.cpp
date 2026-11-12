// Detail levels for meshes (MeshLod.h) and thunderstorms (Lighting.Weather = "Storm").
//
//   StormAndDetailTests <a game file with a baseplate>
#include "core/Audio.h"
#include "core/Log.h"
#include "core/Settings.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLod.h"
#include "scene/Environment.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scene/Weather.h"

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

// A ball 1 stud across with smooth shading: stacks x slices quads.
void ball(int stacks, int slices, std::vector<Vertex>& v, std::vector<uint32_t>& idx) {
    for (int i = 0; i <= stacks; ++i)
        for (int j = 0; j <= slices; ++j) {
            const float a = 3.14159265f * (float)i / (float)stacks, b = 6.2831853f * (float)j / (float)slices;
            const glm::vec3 n(std::sin(a) * std::cos(b), std::cos(a), std::sin(a) * std::sin(b));
            v.push_back({n * 0.5f, n, {(float)j / slices, (float)i / stacks}});
        }
    for (int i = 0; i < stacks; ++i)
        for (int j = 0; j < slices; ++j) {
            const uint32_t a = (uint32_t)(i * (slices + 1) + j), b = a + (uint32_t)slices + 1;
            idx.insert(idx.end(), {a, b, a + 1, b, b + 1, a + 1});
        }
}

// A box with flat sides, each cut into n x n squares (hard edges between the sides).
void box(int n, std::vector<Vertex>& v, std::vector<uint32_t>& idx) {
    for (int side = 0; side < 6; ++side) {
        const int ax = side / 2;
        glm::vec3 normal(0.0f);
        normal[ax] = side % 2 ? -1.0f : 1.0f;
        const uint32_t base = (uint32_t)v.size();
        for (int i = 0; i <= n; ++i)
            for (int j = 0; j <= n; ++j) {
                glm::vec3 p(0.0f);
                p[ax] = normal[ax] * 0.5f;
                p[(ax + 1) % 3] = (float)i / n - 0.5f;
                p[(ax + 2) % 3] = (float)j / n - 0.5f;
                v.push_back({p, normal, {(float)i / n, (float)j / n}});
            }
        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                const uint32_t a = base + (uint32_t)(i * (n + 1) + j), b = a + (uint32_t)n + 1;
                idx.insert(idx.end(), {a, b, a + 1, b, b + 1, a + 1});
            }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: StormAndDetailTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // ---------------------------------------------------------------- detail levels
    {
        std::vector<Vertex> v;
        std::vector<uint32_t> idx;
        ball(64, 128, v, idx);   // 16,384 triangles
        const std::vector<MeshLod::Level> levels = MeshLod::build(v, idx);
        check(levels.size() == (size_t)MeshLod::kLevels, "a detailed ball gets every simpler copy");
        size_t before = idx.size();
        bool smaller = true, inBox = true, valid = true, round = true;
        for (const MeshLod::Level& l : levels) {
            smaller = smaller && l.idx.size() * 10 <= before * 7 && !l.idx.empty();
            before = l.idx.size();
            for (uint32_t i : l.idx) valid = valid && i < l.verts.size();
            for (const Vertex& p : l.verts) {
                inBox = inBox && glm::all(glm::lessThanEqual(glm::abs(p.position), glm::vec3(0.5001f)));
                // Still a ball: every point close to the surface, facing outwards.
                const float r = glm::length(p.position);
                round = round && r > 0.38f && glm::dot(glm::normalize(p.position), p.normal) > 0.8f;
            }
        }
        check(smaller, "each copy has at most 70% of the triangles of the one before");
        check(valid, "every triangle of a copy uses its own points");
        check(inBox, "a copy never grows outside the mesh's box");
        check(round, "a simpler ball is still round, with normals pointing out");
        check(!levels.empty() && levels.back().idx.size() < idx.size() / 20, "the simplest copy is under a twentieth of the triangles");

        // The same level always gives the same answer (players must see the same thing).
        const MeshLod::Level again = MeshLod::cluster(v, idx, MeshLod::kCells[1]);
        check(levels.size() > 1 && again.idx == levels[1].idx, "making a copy twice gives the same copy");

        // Hard edges stay: a box's simpler copy still has points facing all 6 ways at the corners.
        std::vector<Vertex> bv;
        std::vector<uint32_t> bi;
        box(40, bv, bi);   // 19,200 triangles
        const MeshLod::Level coarse = MeshLod::cluster(bv, bi, 8);
        int axisAligned = 0;
        for (const Vertex& p : coarse.verts)
            if (std::max({std::abs(p.normal.x), std::abs(p.normal.y), std::abs(p.normal.z)}) > 0.99f) ++axisAligned;
        check(!coarse.idx.empty() && axisAligned == (int)coarse.verts.size(), "a box keeps its flat sides and sharp edges");
        check(coarse.idx.size() < bi.size() / 10, "and far fewer triangles");

        // Small meshes are left alone.
        std::vector<Vertex> sv;
        std::vector<uint32_t> si;
        ball(8, 12, sv, si);   // 192 triangles
        check(MeshLod::build(sv, si).empty(), "a small mesh gets no copies");
        check(MeshLod::build({}, {}).empty() && MeshLod::cluster({}, {}, 8).idx.empty(), "an empty mesh is fine");

        // Picking: near = full, far = simpler, Low sooner, Epic later, Cinematic never.
        std::vector<float> px;
        for (const MeshLod::Level& l : levels) px.push_back(l.pixels);
        const float perStud = 540.0f / std::tan(glm::radians(30.0f));   // a 1080p screen, 60 degrees
        auto at = [&](float distance, float bias) { return MeshLod::pick(MeshLod::pixelsAcross(2.0f, distance, perStud), bias, px); };
        check(at(10.0f, 1.0f) == 0, "a mesh close by is drawn in full");
        check(at(50.0f, 1.0f) == 1 && at(100.0f, 1.0f) == 2 && at(400.0f, 1.0f) == 3, "further away, simpler copies");
        bool steps = true;
        int last = 0;
        for (float d = 1.0f; d < 2000.0f; d *= 1.1f) { const int l = at(d, 1.0f); steps = steps && l >= last; last = l; }
        check(steps, "it never gets more detailed as it moves away");
        check(at(40.0f, 1.6f) >= at(40.0f, 1.0f) && at(40.0f, 1.0f) >= at(40.0f, 0.7f) && at(30.0f, 1.6f) > at(30.0f, 0.7f),
              "Low graphics swaps sooner than Epic");
        check(at(5000.0f, 0.0f) == 0, "Cinematic always draws the full mesh");

        GraphicsSettings& gs = GraphicsSettings::get();
        const int keep = gs.sg[Scalability::ViewDistance];
        float lastBias = 1e9f;
        bool falling = true;
        for (int l = 0; l < Scalability::kLevels; ++l) {
            gs.sg[Scalability::ViewDistance] = l;
            const float b = gs.meshDetailBias();
            falling = falling && b <= lastBias;
            lastBias = b;
        }
        gs.sg[Scalability::ViewDistance] = keep;
        check(falling, "every graphics level up keeps detail for longer");
    }

    // ---------------------------------------------------------------- thunderstorms
    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    check(run(session, "game.Lighting.Weather = 'Storm' assert(game.Lighting.Weather == 'Storm')", err),
          "a script sets Lighting.Weather = 'Storm' " + err);
    check(scene.environment().weather == (int)Weather::Storm, "it is a storm");
    check(isRaining((int)Weather::Storm) && isRaining((int)Weather::Rain) && !isRaining((int)Weather::Snow) && !isRaining(0),
          "a storm counts as rain (wet things, fires go out)");
    Environment copy;
    Serializer::environmentFromString(copy, Serializer::environmentToString(scene.environment()));
    check(copy.weather == (int)Weather::Storm, "a storm survives saving and loading");

    Environment rain = scene.environment();
    rain.weather = (int)Weather::Rain;
    rain.weatherAmount = 1.0f;
    const Environment stormy = withWeather(scene.environment()), rainy = withWeather(rain);
    check(glm::length(stormy.skyHorizon) < glm::length(rainy.skyHorizon) && stormy.sunIntensity < rainy.sunIntensity,
          "a storm's sky is darker than the hardest rain");
    check(scene.environment().sunIntensity > stormy.sunIntensity, "the game's own Lighting isn't touched");

    WeatherSim sim;
    const glm::vec3 eye(0.0f, 6.0f, 0.0f);
    sim.update(scene, eye, 1.0f / 30.0f);
    check(sim.bolt().empty() && sim.flash() == 0.0f, "no lightning before the first strike");
    check(!sim.streaks().empty(), "it pours");
    {   // Splashes where the rain lands, each gone in under a third of a second.
        WeatherSim wet;
        for (int i = 0; i < 90; ++i) wet.update(scene, eye, 1.0f / 30.0f);
        bool onGround = !wet.splashes().empty(), young = true;
        for (const WeatherSim::Splash& sp : wet.splashes()) {
            onGround = onGround && std::abs(sp.pos.y - wet.groundAt(sp.pos.x, sp.pos.z)) < 1e-3f;
            young = young && sp.age < WeatherSim::kSplashSeconds;
        }
        check(onGround, "drops splash where they land");
        check(young && wet.splashes().size() <= WeatherSim::kMaxSplashes, "splashes fade fast and are limited");
    }
    sim.nextStrike = 0.0f;
    sim.update(scene, eye, 1.0f / 60.0f);
    check(sim.bolt().size() > 64, "a bolt strikes: a trunk and branches");
    {
        int trunk = 0, branches = 0;
        int loose = 0;
        bool thinner = true;
        for (const WeatherSim::BoltPiece& p : sim.bolt()) {
            if (p.bright >= 0.99f) ++trunk; else ++branches;
            thinner = thinner && (p.bright >= 0.99f || p.width < 1.0f);
            // Every piece starts where some other piece ends (or at the very top): no loose bits.
            bool found = false;
            for (const WeatherSim::BoltPiece& q : sim.bolt())
                if (&q != &p && (glm::length(q.b - p.a) < 1e-3f || glm::length(q.a - p.a) < 1e-3f)) { found = true; break; }
            if (!found) ++loose;
        }
        check(trunk == 64 && branches > 0 && branches < 1500, "a jagged trunk of 64 pieces, with branches");
        check(thinner, "branches are dimmer and thinner than the trunk");
        check(loose == 1, "the bolt is one connected tree (only its top hangs from nothing)");
    }
    check(sim.flash() > 0.7f && sim.boltGlow() == sim.flash(), "the world lights up");
    const float away = glm::length(glm::vec2(sim.strikeAt().x - eye.x, sim.strikeAt().z - eye.z));
    check(away > WeatherSim::kNearStrike - 25.0f && away < WeatherSim::kFarStrike + 25.0f, "it hits somewhere around the camera, not on it");
    float topY = -1e9f;
    for (const WeatherSim::BoltPiece& p : sim.bolt()) topY = std::max(topY, p.a.y);
    check(topY > eye.y + 150.0f && sim.strikeAt().y < eye.y, "from the clouds down to the ground");
    check(sim.nextStrike > 1.0f && sim.nextStrike < 40.0f, "the next strike is a few seconds away");

    float volume = 0.0f;
    glm::vec3 from(0.0f);
    check(!sim.takeThunder(volume, from), "the thunder isn't here yet (sound is slower than light)");
    float waited = 0.0f, brightest = 0.0f;
    bool heard = false, gone = false;
    const float plannedNext = sim.nextStrike;
    for (int i = 0; i < 120 && !heard; ++i) {   // (2 seconds: before the next strike)
        sim.update(scene, eye, 1.0f / 60.0f);
        waited += 1.0f / 60.0f;
        if (waited > 0.03f && waited < 0.5f) brightest = std::max(brightest, sim.flash());
        if (waited > 1.3f) gone = gone || sim.bolt().empty();
        heard = sim.takeThunder(volume, from);
    }
    check(heard && std::abs(waited - away / WeatherSim::kThunderSpeed) < 0.25f, "thunder arrives later the further the bolt was");
    check(volume >= 0.25f && volume <= 1.0f && from == sim.strikeAt(), "with a loudness and a place");
    check(!sim.takeThunder(volume, from), "each strike thunders once");
    for (int i = 0; i < 60; ++i) sim.update(scene, eye, 1.0f / 60.0f);
    check(brightest > 0.2f, "it flickers again after the first flash");
    check(plannedNext < 3.2f || (sim.bolt().empty() && sim.flash() == 0.0f), "the bolt and the flash are gone within a second or so");
    (void)gone;
    const auto& names = Audio::builtinNames();
    check(std::find(names.begin(), names.end(), "thunder") != names.end(), "there is a thunder sound");

    // No storm, no lightning.
    run(session, "game.Lighting.Weather = 'Rain'", err);
    WeatherSim calm;
    calm.nextStrike = 0.0f;
    for (int i = 0; i < 30; ++i) calm.update(scene, eye, 1.0f / 30.0f);
    check(calm.bolt().empty() && calm.flash() == 0.0f, "plain rain has no lightning");

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "storm and detail tests FAILED\n" : "storm and detail tests passed\n");
    return failures ? 1 : 0;
}
