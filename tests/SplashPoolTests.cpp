// Splash Pool: two bugs seen together in Mega Water Slide, played on a server.
//  * Teleporting: the host's anti-cheat (MoveGuard) allows about WalkSpeed x 1.5 + 6
//    studs a second (15 for the default WalkSpeed of 6). The slide takes you down at
//    20 to 30, so a joined player was "moving too fast" and got pulled back to their
//    last good spot again and again. Now water slides, streams and pools may go as
//    fast as the water carries you.
//  * Sliding about: liquid that splashed onto the 240-stud baseplate marked the whole
//    baseplate wet, so the whole map was as slippery as the slide for 20 seconds; you
//    slid around, even standing still. Now a big flat floor is only slippery where
//    the water is.
//
//   SplashPoolTests <Mega Water Slide game file>
#include "game/GameSession.h"
#include "net/MoveGuard.h"
#include "renderer/Mesh.h"
#include "scene/CharacterFooting.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Water.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
void frame(GameSession& session, float dt) {
    ImGui::NewFrame();
    session.update(dt, 0.0f, false);
    ImGui::EndFrame();
}
// Someone going `speed` studs a second in a straight line for `seconds`, reported 20
// times a second: how many times the anti-cheat pulls them back.
int pullbacks(float speed, float seconds, bool slick, float slickFor = 1e9f) {
    MoveGuard guard;
    int pulled = 0;
    for (int i = 0; i <= (int)(seconds * 20); ++i) {
        const double t = i / 20.0;
        const glm::vec3 at((float)t * speed, 0.0f, 0.0f);
        if (guard.check(1, at, t, 6.0f, 20.8f, 60.0f, false, slick && t < slickFor) != MoveGuard::Verdict::Ok) ++pulled;
    }
    return pulled;
}
} // namespace

int main(int argc, char** argv) {
    // ---- Which wet ground is slippery --------------------------------------------
    check(CharacterFooting::wetUnderfoot({5.4f, 0.6f, 7.7f}, 0.97f, 0), "a wet slide piece is slippery all the way down");
    check(CharacterFooting::wetUnderfoot({8.0f, 1.0f, 8.0f}, 1.0f, 0), "a small wet floor is slippery all over");
    check(!CharacterFooting::wetUnderfoot({240.0f, 1.0f, 240.0f}, 1.0f, 0), "a big baseplate a splash landed on somewhere isn't slippery everywhere");
    check(CharacterFooting::wetUnderfoot({240.0f, 1.0f, 240.0f}, 1.0f, 6), "...but it is where the water actually is");

    // ---- The anti-cheat and water -------------------------------------------------
    check(pullbacks(25.0f, 4.0f, false) > 0, "25 studs/s on dry ground is still too fast (zoom hacks are still caught)");
    check(pullbacks(25.0f, 4.0f, true) == 0, "25 studs/s down a water slide is fine");
    check(pullbacks(44.0f, 4.0f, true) == 0, "the fastest a slide takes you, plus walking, is fine");
    check(pullbacks(25.0f, 5.0f, true, 1.0f) > 0, "...but not for long after leaving the water");
    check(pullbacks(200.0f, 2.0f, true) > 0, "teleporting through a pool is still caught");

    // ---- In the game ----------------------------------------------------------------
    if (argc < 2) { std::cerr << "usage: SplashPoolTests <game file>\n"; return 2; }
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

    // Stand still on the grass, away from everything, on a fast computer (240 FPS),
    // while the slide and the fountain splash water about.
    {
        const float fps = 240.0f;
        p->root()->transform.position = glm::vec3(80.0f, 0.2f, 80.0f);
        for (int i = 0; i < (int)(fps * 2); ++i) frame(session, 1.0f / fps);
        const glm::vec3 a = p->position();
        for (int i = 0; i < (int)(fps * 30); ++i) frame(session, 1.0f / fps);
        const glm::vec3 b = p->position();
        const float slid = glm::length(glm::vec2(b.x - a.x, b.z - a.z));
        check(p->groundId() && scene.water().liquid().isWet(p->groundId()), "(the liquid has splashed onto the baseplate)");
        check(slid < 0.01f, "standing still on the grass for 30 seconds: no sliding (" + std::to_string(slid) + " studs)");
        float tallest = 0.0f;
        for (const WaterSystem::Body& w : scene.water().bodies())
            for (float h : w.h) tallest = std::max(tallest, std::abs(h));
        check(tallest < 0.5f, "the pools' ripples stay calm (" + std::to_string(tallest) + " studs)");
    }

    // Ride the slide holding forward, reporting where you are 20 times a second like a
    // joined player does, through the host's anti-cheat.
    {
        const float fps = 60.0f;
        p->root()->transform.position = glm::vec3(8.3f, 80.2f, 1.2f);
        MoveGuard before, after;   // (before: not knowing about water)
        after.slickHere = [&](const glm::vec3& at) { return Player::slickAt(scene, at); };   // (as the host sets it up)
        int pulledBefore = 0, pulledAfter = 0;
        float fastest = 0.0f;
        glm::vec3 last = p->position();
        double t = 0.0;
        int below = 0;   // (frames since reaching the bottom: stop a little after)
        for (int i = 0; i < (int)(fps * 90) && below < (int)(fps * 3); ++i) {
            if (p->position().y < 6.0f) ++below;
            const glm::vec3 v = p->velocity();
            const float l = glm::length(glm::vec2(v.x, v.z));
            p->scriptMove(l > 0.5f ? glm::vec3(v.x / l, 0.0f, v.z / l) : glm::vec3(0.6f, 0.0f, 0.8f));
            frame(session, 1.0f / fps);
            t += 1.0 / fps;
            if (i % 3 == 0) {
                const glm::vec3 at = p->position();
                const float ws = p->humanoid().walkSpeed, js = p->humanoid().launchSpeed(scene.world().gravity), g = scene.world().gravity;
                if (before.check(1, at, t, ws, js, g, false) != MoveGuard::Verdict::Ok) ++pulledBefore;
                if (after.check(1, at, t, ws, js, g, false) != MoveGuard::Verdict::Ok) {
                    ++pulledAfter;
                    std::printf("      pulled back at %.1fs (%.1f %.1f %.1f)\n", t, at.x, at.y, at.z);
                }
            }
            if (i % (int)fps == 0) {
                const glm::vec3 at = p->position();
                fastest = std::max(fastest, glm::length(glm::vec2(at.x - last.x, at.z - last.z)));
                last = at;
            }
        }
        p->scriptMove(glm::vec3(0.0f));
        const glm::vec3 end = p->position();
        std::printf("      (fastest %.1f studs/s; WalkSpeed %.0f; ended at %.1f %.1f %.1f)\n", fastest, p->humanoid().walkSpeed, end.x, end.y, end.z);
        check(end.y < 6.0f, "rode the slide all the way down");
        check(pulledBefore > 0, "(the old anti-cheat pulled the rider back " + std::to_string(pulledBefore) + " times)");
        check(pulledAfter == 0, "riding the slide: never pulled back (teleported) by the anti-cheat");
    }

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "splash pool tests FAILED\n" : "all passed\n");
    return failures ? 1 : 0;
}
