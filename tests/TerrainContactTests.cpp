// Studio's Collisions switch and the terrain (src/scene/TerrainContact.h): how far a
// box has sunk into the ground, so a part dragged onto a hill stops on top of it.
//
//   TerrainContactTests
#include "scene/TerrainContact.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

OBB box(glm::vec3 center, glm::vec3 size, float turnYDeg = 0.0f, float tiltZDeg = 0.0f) {
    OBB b;
    b.center = center;
    b.half = size * 0.5f;
    glm::mat4 r = glm::rotate(glm::mat4(1.0f), glm::radians(turnYDeg), glm::vec3(0, 1, 0)) *
                  glm::rotate(glm::mat4(1.0f), glm::radians(tiltZDeg), glm::vec3(0, 0, 1));
    for (int a = 0; a < 3; ++a) b.axis[a] = glm::normalize(glm::vec3(r[a]));
    return b;
}

// Slide a box from `from` to `to` and stop where it first goes into the ground
// (the same halving search Studio's Collisions uses).
glm::vec3 slide(const Terrain& t, glm::vec3 from, glm::vec3 to, glm::vec3 size) {
    float lo = 0.0f, hi = 1.0f;
    for (int it = 0; it < 20; ++it) {
        float mid = (lo + hi) * 0.5f;
        if (TerrainContact::sinkDepth(t, box(glm::mix(from, to, mid), size)) > 0.002f) hi = mid; else lo = mid;
    }
    return glm::mix(from, to, lo);
}
} // namespace

int main() {
    Terrain flat;
    flat.create(32, Terrain::kDefaultCell, 2.0f, TerrainMaterial::Grass);   // ground at y = 2

    check(near(TerrainContact::sinkDepth(Terrain(), box({0, -10, 0}, glm::vec3(2))), 0.0f),
          "no terrain: nothing to sink into");
    check(near(TerrainContact::sinkDepth(flat, box({0, 10, 0}, glm::vec3(2))), 0.0f), "a block up in the air isn't in the ground");
    check(TerrainContact::sinkDepth(flat, box({0, 3, 0}, glm::vec3(2))) < 0.002f, "a block resting on the ground isn't in it");
    check(near(TerrainContact::sinkDepth(flat, box({0, 2.5f, 0}, glm::vec3(2))), 0.5f), "half a stud into the ground = 0.5");
    check(TerrainContact::sinkDepth(flat, box({0, -20, 0}, glm::vec3(2))) > 1.0f, "a block buried deep counts too");
    check(near(TerrainContact::sinkDepth(flat, box({500, 0, 0}, glm::vec3(2))), 0.0f), "off the edge of the terrain: nothing");

    // Dragged straight down from high up: it stops resting on the ground.
    glm::vec3 rest = slide(flat, {0, 10, 0}, {0, -5, 0}, glm::vec3(2));
    check(near(rest.y, 3.0f, 0.01f), "dragged down onto flat ground, it rests on top (y = " + std::to_string(rest.y) + ")");

    // A tilted block: its lowest corner touches first.
    {
        OBB tilted = box({0, 3.0f, 0}, glm::vec3(2), 0.0f, 45.0f);   // corner sticks ~0.41 below its old bottom
        check(TerrainContact::sinkDepth(flat, tilted) > 0.3f, "a tilted block's corner pokes into the ground");
    }

    // A thin peak smaller than the block, poking up into its middle.
    {
        Terrain spike;
        spike.create(32, Terrain::kDefaultCell, 0.0f, TerrainMaterial::Rock);
        const int mid = spike.cells() / 2;   // the grid point at (0, 0)
        spike.setHeight(mid, mid, 3.0f);
        // A wide flat plank whose corners are all over low ground, sitting at y = 2..2.5.
        check(TerrainContact::sinkDepth(spike, box({0, 2.25f, 0}, {7.0f, 0.5f, 7.0f})) > 0.5f,
              "a hill top poking into the middle of a plank counts");
        glm::vec3 r = slide(spike, {0, 10, 0}, {0, -2, 0}, {7.0f, 0.5f, 7.0f});
        check(near(r.y, 3.25f, 0.02f), "the plank stops on the peak (y = " + std::to_string(r.y) + ")");
    }

    // Sloped ground: generated hills; a block dragged down ends up touching, not inside.
    {
        Terrain hills;
        hills.generate(64, Terrain::kDefaultCell, 7, 0.8f);
        const glm::vec3 size(4, 2, 4);
        for (float x : {-60.0f, -13.0f, 21.0f, 77.0f}) {
            glm::vec3 r = slide(hills, {x, 600, 9}, {x, -300, 9}, size);
            float d = TerrainContact::sinkDepth(hills, box(r, size));
            float d2 = TerrainContact::sinkDepth(hills, box(r - glm::vec3(0, 0.05f, 0), size));
            check(d <= 0.002f && d2 > 0.002f, "on hills at x = " + std::to_string((int)x) + " it stops right on the surface");
        }
    }

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
