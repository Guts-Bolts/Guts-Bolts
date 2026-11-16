// Calm Waters: two bugs seen together in one game.
//  * A player standing still on a floor far from the middle of the map slid slowly
//    across it, faster the higher the frame rate (riding the floor as if it moved).
//  * Water with a big swell ("Waves" / "WaveScale") was a field of tall spikes: the
//    short chop was stretched upwards with the big waves. Now a bigger swell is the
//    same sea made bigger, so it is never steeper than a swell of 1.
//
//   StillWaterTests <a game file with a player>
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/CharacterFooting.h"
#include "scene/OceanWaves.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <glm/gtc/matrix_transform.hpp>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
void play(GameSession& session, float seconds, float fps) {
    for (int i = 0; i < (int)(seconds * fps); ++i) {
        ImGui::NewFrame();
        session.update(1.0f / fps, 0.0f, false);
        ImGui::EndFrame();
    }
}

// The steepest the swell's surface gets (height change per stud), looked at on a grid.
float steepest(const OceanWaves::Field& f, float amplitude) {
    float most = 0.0f;
    const float step = 0.25f;
    for (float z = 0.0f; z < 40.0f; z += 0.5f)
        for (float x = 0.0f; x < 40.0f; x += 0.5f) {
            const float h = f.heightAt(x, z, amplitude, 1.0f);
            const float hx = f.heightAt(x + step, z, amplitude, 1.0f), hz = f.heightAt(x, z + step, amplitude, 1.0f);
            most = std::max(most, std::max(std::abs(hx - h), std::abs(hz - h)) / step);
        }
    return most;
}
} // namespace

int main(int argc, char** argv) {
    // ---- Riding the ground --------------------------------------------------------
    {
        // A big squashed baseplate far from the middle that doesn't move: whatever
        // stood on it stays exactly where it was.
        const glm::mat4 floor = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(1500.0f, -8.0f, -2300.0f)), glm::vec3(2048.0f, 16.0f, 2048.0f));
        const glm::vec3 feet(1712.37f, 0.0f, -2481.9f);
        float turn = 1.0f;
        const glm::vec3 still = CharacterFooting::carry(floor, floor, feet, &turn);
        check(still == feet && turn == 0.0f, "a part that didn't move doesn't move you");
        // A platform that slides 0.1 along x and turns 5 degrees still carries you.
        const glm::mat4 before = glm::translate(glm::mat4(1.0f), glm::vec3(1000.0f, 5.0f, 1000.0f));
        const glm::mat4 after = glm::rotate(glm::translate(before, glm::vec3(0.1f, 0.0f, 0.0f)), glm::radians(5.0f), glm::vec3(0, 1, 0));
        const glm::vec3 rider = before * glm::vec4(2.0f, 1.0f, 0.0f, 1.0f);
        const glm::vec3 moved = CharacterFooting::carry(before, after, rider, &turn);
        const glm::vec3 want = after * glm::vec4(2.0f, 1.0f, 0.0f, 1.0f);
        check(glm::length(moved - want) < 1e-3f, "a moving platform still carries you along");
        check(std::abs(turn - 5.0f) < 0.01f, "...and a turning one turns you with it");
    }

    // ---- The swell ------------------------------------------------------------------
    {
        OceanWaves::Field f;
        f.update(4.0f);
        const float one = steepest(f, 1.0f), four = steepest(f, 4.0f), eight = steepest(f, 8.0f);
        std::printf("      (steepest: swell 1 %.2f, swell 4 %.2f, swell 8 %.2f)\n", one, four, eight);
        check(four < one * 1.25f && eight < one * 1.25f, "a big swell is no steeper than a swell of 1 (no spikes)");
        float tallest = 0.0f;
        for (float x = 0.0f; x < 400.0f; x += 1.0f) tallest = std::max(tallest, f.heightAt(x, 0.0f, 4.0f, 1.0f));
        check(tallest > 2.0f, "a big swell still has big waves (" + std::to_string(tallest) + " studs)");
        // (A swell of 1 or less isn't stretched at all: it looks just as it did.)
        check(OceanWaves::stretch(0.5f) == 1.0f && OceanWaves::stretch(1.0f) == 1.0f && OceanWaves::stretch(4.0f) == 4.0f,
              "a swell of 1 or less looks just as before");
    }

    // ---- In a game: standing still --------------------------------------------------
    if (argc < 2) { std::cerr << "usage: StillWaterTests <game file>\n"; return 2; }
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
    scene.world().spawnForceField = 0.0f;
    session.start();
    Player* p = scene.player();
    if (!p || !p->root()) { std::cerr << "no player in that game\n"; return 1; }
    session.scripts().runCommand("local f = Instance.new('Part') f.Name = 'Floor' f.Anchored = true f.Size = Vector3.new(400, 2, 400) "
                                 "f.Position = Vector3.new(900, 99, 900) f.Parent = workspace", err);
    p->root()->transform.position = glm::vec3(1000.0f, 100.2f, 1020.0f);
    play(session, 1.0f, 60.0f);
    const glm::vec3 a = p->root()->transform.position;
    play(session, 10.0f, 240.0f);   // (a fast computer: the slide used to grow with the frame rate)
    const glm::vec3 b = p->root()->transform.position;
    const float slid = glm::length(glm::vec2(b.x - a.x, b.z - a.z));
    check(slid < 0.001f, "standing still on a floor for 10 seconds at 240 FPS: no sliding (" + std::to_string(slid) + " studs)");

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "still water tests FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
