#include "BounceLight.h"
#include "Environment.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"

#include <algorithm>
#include <cmath>

namespace BounceLight {

namespace {

glm::vec3 lin(const glm::vec3& c) { return glm::pow(glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)), glm::vec3(2.2f)); }

// Which way the side of `part` that `hit` is on faces (its box's nearest side).
glm::vec3 faceNormal(const SceneNode& part, const glm::vec3& hit) {
    const glm::mat4 M = part.worldMatrix();
    const glm::mat3 L(M);
    if (std::abs(glm::determinant(L)) < 1e-9f) return {0, 1, 0};
    const glm::vec3 local = glm::vec3(glm::inverse(M) * glm::vec4(hit, 1.0f));
    int axis = 0;
    for (int i = 1; i < 3; ++i) if (std::abs(local[i]) > std::abs(local[axis])) axis = i;
    glm::vec3 n(0.0f);
    n[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
    const glm::vec3 w = glm::transpose(glm::inverse(L)) * n;
    return glm::length(w) > 1e-6f ? glm::normalize(w) : glm::vec3(0, 1, 0);
}

bool cast(Scene& scene, const glm::vec3& from, const glm::vec3& dir, float most,
          glm::vec3& hit, glm::vec3& normal, glm::vec3& surface) {
    float dist = 0.0f;
    SceneNode* n = Physics::raycastIf(scene, from, dir, &dist, [](const SceneNode* s) {
        return !s->isPart() || s->transparency < 0.8f;   // light goes through see-through things
    });
    if (!n || dist > most) return false;
    hit = from + dir * dist;
    if (n == scene.terrainNode()) {
        normal = {0, 1, 0};
        surface = {0.36f, 0.42f, 0.26f};   // (grass-ish: terrain has many colours)
    } else {
        normal = faceNormal(*n, hit);
        surface = n->color;
    }
    return true;
}

} // namespace

std::vector<Point> gather(Scene& scene, const Environment& env, const glm::vec3& around,
                          const std::vector<Lamp>& lamps) {
    std::vector<Point> out;
    const float k = std::clamp(env.bounceLight, 0.0f, 2.0f);
    if (k <= 0.0f) return out;

    // Sunlight landing round the camera.
    const glm::vec3 sunDir = glm::normalize(env.sunDirection());   // (points at the sun)
    if (env.sunElevation > 2.0f && env.sunIntensity > 0.0f) {
        const glm::vec3 sun = lin(env.sunColor) * env.sunIntensity;
        const float half = (kSunRays - 1) * 0.5f;
        for (int x = 0; x < kSunRays; ++x)
            for (int z = 0; z < kSunRays; ++z) {
                const glm::vec3 at = around + glm::vec3((x - half) * kSpacing, 0.0f, (z - half) * kSpacing);
                glm::vec3 hit, n, surface;
                if (!cast(scene, at + sunDir * 120.0f, -sunDir, 400.0f, hit, n, surface)) continue;
                const float facing = std::max(0.0f, glm::dot(n, sunDir));
                if (facing <= 0.05f) continue;
                const glm::vec3 c = sun * lin(surface) * facing * 0.3f * k;
                out.push_back({hit + n * 0.3f, c, n, kSpacing * 1.4f});
            }
    }
    // Each lamp lights the floor under it, and that floor glows back a little.
    for (size_t i = 0; i < lamps.size() && (int)i < kMostLamps; ++i) {
        const Lamp& l = lamps[i];
        glm::vec3 hit, n, surface;
        if (!cast(scene, l.pos, {0, -1, 0}, l.range, hit, n, surface)) continue;
        const float d = glm::length(hit - l.pos);
        const float fall = std::pow(std::clamp(1.0f - std::pow(d / l.range, 4.0f), 0.0f, 1.0f), 2.0f) / (d * d + 1.0f);
        const glm::vec3 c = l.color * lin(surface) * fall * 1.2f * k;
        out.push_back({hit + n * 0.3f, c, n, std::max(3.0f, l.range * 0.6f)});
    }
    // Only the brightest few (each one costs as much as a lamp to draw).
    std::sort(out.begin(), out.end(), [](const Point& a, const Point& b) {
        return glm::dot(a.color, glm::vec3(1.0f)) > glm::dot(b.color, glm::vec3(1.0f));
    });
    out.erase(std::remove_if(out.begin(), out.end(), [](const Point& p) { return glm::dot(p.color, glm::vec3(1.0f)) < 0.002f; }), out.end());
    if ((int)out.size() > kMost) out.resize((size_t)kMost);
    return out;
}

} // namespace BounceLight
