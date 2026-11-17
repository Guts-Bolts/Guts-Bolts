// "Stairs Without Snags": the character's legs don't bump into anything, like a Roblox
// Humanoid. The body floats HipHeight over the ground, so stairs, curbs and the seams
// between tilted parts (turned or not) are walked over without jumping, and a ledge
// caught mid-jump is landed on. Walls, steep slopes and steps taller than the step
// height still stop you. HipHeight and MaxSlopeAngle are Humanoid settings.
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Physics.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

std::vector<SceneNode*> blocks;   // what the current test world added

SceneNode* block(Scene& scene, glm::vec3 at, glm::vec3 size, glm::vec3 rot = glm::vec3(0.0f)) {
    SceneNode* p = scene.addNode("Block", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    p->transform.position = at;
    p->transform.scale = size;
    p->transform.rotation = rot;
    p->anchored = true;
    blocks.push_back(p);
    return p;
}

struct Walker {
    glm::vec3 feet;
    glm::vec3 vel{0.0f};
    bool grounded = true;
    float yaw = 0.0f;
    int airFrames = 0;   // frames spent not on the ground
    float highest = -1e9f;
};

// Walk (or fall) for `seconds` in direction `dir` at the default WalkSpeed with gravity 60.
void walk(Physics& phys, Walker& w, glm::vec3 dir, float seconds, const Physics::Footing& foot = Physics::Footing()) {
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        w.vel.y -= 60.0f * dt;
        glm::vec3 d = dir * 6.0f * dt;
        d.y = w.vel.y * dt;
        Physics::MoveResult r = phys.moveCharacter(w.feet, d, w.grounded, w.yaw, 0, 0, foot);
        w.feet = r.position;
        w.grounded = r.grounded;
        if (r.grounded && w.vel.y < 0.0f) w.vel.y = 0.0f;
        if (!r.grounded) ++w.airFrames;
        w.highest = std::max(w.highest, w.feet.y);
    }
}

// A fresh world: a big floor at y = 0, plus whatever `build` adds.
template <class F>
void world(Scene& scene, Physics& phys, F build) {
    for (SceneNode* b : blocks) scene.removeNode(b);
    blocks.clear();
    block(scene, {0.0f, -0.5f, 0.0f}, {200.0f, 1.0f, 200.0f});
    build(scene);
    phys.reset();
    phys.gather(scene);
}
} // namespace

int main() {
    Mesh::headless = true;
    Scene scene;
    Physics phys;

    // ---- Stairs ---------------------------------------------------------------------
    // Ten steps, each 0.5 high and 1 deep, going up along +z from z = 2.
    auto stairs = [](float yawDeg) {
        return [yawDeg](Scene& s) {
            const float a = glm::radians(yawDeg);
            const glm::vec3 fwd(std::sin(a), 0.0f, std::cos(a));
            for (int i = 0; i < 10; ++i) {
                const float h = 0.5f * (i + 1);
                block(s, fwd * (2.5f + i) + glm::vec3(0.0f, h * 0.5f, 0.0f), {4.0f, h, 1.0f}, {0.0f, yawDeg, 0.0f});
            }
        };
    };
    {
        world(scene, phys, stairs(0.0f));
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 1.85f);
        std::printf("      (straight stairs: feet at %.2f high, z %.2f)\n", w.feet.y, w.feet.z);
        check(w.feet.y > 4.9f, "walks up a staircase of 0.5 steps without jumping");
        check(w.airFrames < 5, "...staying on its feet the whole way (no little hops)");

        Walker down{{0.0f, 5.0f, 11.8f}};
        walk(phys, down, {0, 0, -1}, 3.0f);
        check(down.feet.y < 0.3f && down.airFrames < 3, "walks down the same stairs, following each step");
    }
    {
        // The same staircase turned 30 degrees: turned parts used to be walls you couldn't step onto.
        world(scene, phys, stairs(30.0f));
        const float a = glm::radians(30.0f);
        Walker w{{0.0f, 0.0f, 0.0f}};
        w.yaw = 30.0f;
        walk(phys, w, {std::sin(a), 0.0f, std::cos(a)}, 1.7f);
        std::printf("      (turned stairs: feet at %.2f high)\n", w.feet.y);
        check(w.feet.y > 4.9f, "walks up a turned staircase too");
    }

    // ---- Curbs and walls --------------------------------------------------------------
    {
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 0.35f, 13.0f}, {6.0f, 0.7f, 20.0f}); });
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 1.5f);
        check(std::abs(w.feet.y - 0.7f) < 0.02f, "steps up a 0.7 curb (the old limit was 0.55)");
    }
    {
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 0.6f, 13.0f}, {6.0f, 1.2f, 20.0f}); });
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 1.5f);
        check(w.feet.y < 0.01f && w.feet.z < 3.0f, "a 1.2 tall block still stops you (jump for that)");
    }
    {
        // A tall wall: never climbed, however long you walk into it.
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 3.0f, 4.0f}, {6.0f, 6.0f, 2.0f}); });
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 2.0f);
        check(w.feet.y < 0.01f && w.feet.z < 3.0f, "walls are walls");
    }

    // ---- HipHeight decides the step ----------------------------------------------------
    {
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 0.25f, 13.0f}, {6.0f, 0.5f, 20.0f}); });
        Humanoid h;
        h.hipHeight = 0.4f;   // short legs: steps up to 0.3
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 1.5f, Physics::Footing{h.stepHeight(), h.minGroundY()});
        check(w.feet.y < 0.01f, "HipHeight 0.4 can't step up a 0.5 block");
        h.hipHeight = 1.0f;
        Walker w2{{0.0f, 0.0f, 0.0f}};
        walk(phys, w2, {0, 0, 1}, 1.5f, Physics::Footing{h.stepHeight(), h.minGroundY()});
        check(std::abs(w2.feet.y - 0.5f) < 0.02f, "...the default HipHeight 1 can");
        h.hipHeight = 2.0f;   // long legs: up to 1.5
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 0.6f, 13.0f}, {6.0f, 1.2f, 20.0f}); });
        Walker w3{{0.0f, 0.0f, 0.0f}};
        walk(phys, w3, {0, 0, 1}, 1.5f, Physics::Footing{h.stepHeight(), h.minGroundY()});
        check(std::abs(w3.feet.y - 1.2f) < 0.02f, "HipHeight 2 walks onto a 1.2 block");
    }

    // ---- Slopes ------------------------------------------------------------------------
    {
        // A slope tilted 70 degrees: too steep to stand on, so you can't step up it bit by bit.
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 0.0f, 5.0f}, {6.0f, 0.4f, 8.0f}, {-70.0f, 0.0f, 0.0f}); });
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 3.0f);
        std::printf("      (steep slope: feet at %.2f high)\n", w.feet.y);
        check(w.feet.y < 0.8f, "a 70 degree slope can't be stepped up");

        // A 20 degree ramp is fine, and MaxSlopeAngle 10 turns it into a wall.
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 5.13f, 16.0f}, {6.0f, 0.4f, 30.0f}, {-20.0f, 0.0f, 0.0f}); });
        Walker up{{0.0f, 0.0f, 0.0f}};
        walk(phys, up, {0, 0, 1}, 1.5f);
        check(up.feet.y > 2.0f, "walks up a 20 degree ramp");
        Humanoid h;
        h.maxSlopeAngle = 10.0f;
        Walker no{{0.0f, 0.0f, 0.0f}};
        walk(phys, no, {0, 0, 1}, 1.5f, Physics::Footing{h.stepHeight(), h.minGroundY()});
        std::printf("      (MaxSlopeAngle 10: feet at %.2f high)\n", no.feet.y);
        check(no.feet.y < 1.0f, "MaxSlopeAngle 10 can't walk up it");
    }

    // ---- Tilted planks with lips at the seams (like a slide's floor) ----------------------
    {
        world(scene, phys, [](Scene& s) {
            for (int i = 0; i < 8; ++i)   // each plank 4 long, tilted 8 degrees, starting a bit higher than the last one ends
                block(s, {0.0f, 0.3f + i * 0.62f, 3.0f + i * 3.95f}, {4.0f, 0.4f, 4.0f}, {-8.0f, 0.0f, 0.0f});
        });
        Walker w{{0.0f, 0.0f, 0.0f}};
        walk(phys, w, {0, 0, 1}, 6.0f);
        std::printf("      (tilted planks: got %.2f high)\n", w.highest);
        check(w.highest > 4.0f, "walks up a run of tilted planks without catching on the seams");
    }

    // ---- Catching a ledge in the air ----------------------------------------------------
    {
        world(scene, phys, [](Scene& s) { block(s, {0.0f, 1.0f, 13.0f}, {6.0f, 2.0f, 20.0f}); });
        // Falling past a ledge whose top is half a stud above our feet, pushing into it.
        Walker w{{0.0f, 1.5f, 2.6f}};
        w.grounded = false;
        w.vel.y = -2.0f;
        walk(phys, w, {0, 0, 1}, 0.6f);
        check(std::abs(w.feet.y - 2.0f) < 0.05f && w.grounded, "a ledge just above your feet mid-jump is landed on, not slid off");
    }

    std::cout << (failures ? "FAILED " : "all passed ") << failures << "\n";
    return failures ? 1 : 0;
}
