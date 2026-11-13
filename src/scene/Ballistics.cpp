#include "Ballistics.h"
#include "Gore.h"
#include "Particles.h"
#include "Physics.h"
#include "Realism.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Terrain.h"
#include "../core/Audio.h"
#include <algorithm>
#include <cmath>

namespace Ballistics {

namespace {
float rand01(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / float(0xffffff); }
glm::vec3 randDir(uint32_t& s) {
    glm::vec3 v(rand01(s) * 2.0f - 1.0f, rand01(s) * 2.0f - 1.0f, rand01(s) * 2.0f - 1.0f);
    return glm::length(v) > 1e-4f ? glm::normalize(v) : glm::vec3(0, 1, 0);
}
} // namespace

Result trace(const Shot& shot, const CastFn& cast, const ThicknessFn& thickness) {
    Result r;
    glm::vec3 pos = shot.origin, vel = shot.velocity;
    const float speed0 = std::max(1.0f, glm::length(shot.velocity));
    float travelled = 0.0f, power = 1.0f;
    int bounces = 0;
    uint32_t seed = shot.seed ? shot.seed : 1u;
    r.path.push_back(pos);
    // Small steps (a 240th of a second) so the drop and the wind bend the path smoothly.
    const float dt = 1.0f / 240.0f;
    for (int stepNo = 0; stepNo < 4000; ++stepNo) {
        const glm::vec3 next = pos + vel * dt + 0.5f * shot.gravity * dt * dt;
        glm::vec3 seg = next - pos;
        const float len = glm::length(seg);
        if (len < 1e-5f) break;
        seg /= len;
        Hit h;
        if (cast(pos, seg, len, h)) {
            const float before = glm::length(vel);
            h.velocity = vel;
            r.path.push_back(h.point);
            travelled += glm::length(h.point - pos);
            r.time += dt * glm::length(h.point - pos) / len;
            const float cosIn = -glm::dot(seg, h.normal);   // 1 = straight in, 0 = skimming along
            const bool bounce = shot.ricochet && !h.flesh && bounces < shot.maxBounces && cosIn < 0.26f &&
                                (h.terrain ? false : Realism::ricochets(h.material));
            if (bounce) {
                // Off it goes, losing a good part of its speed, scattered a little.
                ++bounces;
                h.what = Hit::Ricochet;
                h.damage = shot.damage * power;
                glm::vec3 out = vel - 2.0f * glm::dot(vel, h.normal) * h.normal;
                out = glm::normalize(glm::normalize(out) + randDir(seed) * 0.08f + h.normal * 0.03f);
                vel = out * before * 0.6f;
                power *= 0.5f;
                pos = h.point + h.normal * 0.02f;
                r.hits.push_back(h);
                continue;
            }
            float depth = h.flesh ? kFleshDepth : h.terrain ? 0.0f : Realism::pierceDepth(h.material);
            depth *= before / speed0;   // a slower bullet goes through less
            const float thick = shot.pierce && depth > 0.0f ? thickness(h, seg) : 1e9f;
            h.damage = shot.damage * power;
            if (thick < depth) {
                h.what = Hit::Pierced;
                r.hits.push_back(h);
                // Out the other side, slower for what it went through.
                const float lose = 0.25f + 0.6f * (thick / depth);
                vel *= std::max(0.05f, 1.0f - lose);
                power *= std::max(0.1f, 1.0f - lose * (h.flesh ? 1.2f : 0.8f));
                pos = h.point + seg * (thick + 0.03f);
                r.path.push_back(pos);
                if (glm::length(vel) < speed0 * 0.08f) { r.end = pos; return r; }
                continue;
            }
            h.what = Hit::Stopped;
            r.hits.push_back(h);
            r.end = h.point;
            return r;
        }
        pos = next;
        travelled += len;
        r.time += dt;
        // The air: gravity pulls it down, the wind (and the drag of the air) tugs it along.
        vel += shot.gravity * dt + (shot.wind - vel) * (shot.drag * dt) * (1.0f / std::max(1.0f, speed0 / 900.0f));
        if (stepNo % 12 == 0) r.path.push_back(pos);
        if (travelled >= shot.range || glm::length(vel) < 5.0f) break;
    }
    r.path.push_back(pos);
    r.end = pos;
    return r;
}

namespace {

SceneNode* characterOf(Scene& scene, SceneNode* part) {
    for (SceneNode* p = part ? part->parent : nullptr; p; p = p->parent)
        if (scene.humanoidOf(p->id)) return p;
    return nullptr;
}

glm::vec3 faceNormal(const SceneNode* hit, const glm::vec3& at) {
    const OBB box = Physics::worldOBB(hit);
    if (hit->primitiveType == PrimitiveType::Sphere)
        return glm::length(at - box.center) > 1e-5f ? glm::normalize(at - box.center) : glm::vec3(0, 1, 0);
    glm::vec3 normal(0, 1, 0);
    float best = -1.0f;
    for (int i = 0; i < 3; ++i) {
        const float f = glm::dot(at - box.center, box.axis[i]) / std::max(box.half[i], 1e-4f);
        if (std::abs(f) > best) { best = std::abs(f); normal = box.axis[i] * (f < 0 ? -1.0f : 1.0f); }
    }
    return normal;
}

// The closest the line a..b comes to p.
glm::vec3 closestOn(const glm::vec3& a, const glm::vec3& b, const glm::vec3& p) {
    const glm::vec3 d = b - a;
    const float l2 = glm::dot(d, d);
    if (l2 < 1e-8f) return a;
    return a + d * std::clamp(glm::dot(p - a, d) / l2, 0.0f, 1.0f);
}

} // namespace

Result fire(Scene& scene, const Shot& shot, const std::vector<const SceneNode*>& ignore, const glm::vec3* listener) {
    auto skipped = [&](const SceneNode* n) {
        for (const SceneNode* p = n; p; p = p->parent)
            if (std::find(ignore.begin(), ignore.end(), p) != ignore.end()) return true;
        return false;
    };
    auto cast = [&](const glm::vec3& from, const glm::vec3& dir, float length, Hit& out) {
        float dist = 0.0f;
        SceneNode* hit = Physics::raycastIf(scene, from, dir, &dist, [&](const SceneNode* n) {
            if (n == scene.root()) return true;
            if (n->isPart() && !n->canCollide && n->transparency >= 0.99f) return false;   // invisible triggers
            return !skipped(n);
        });
        if (!hit || dist > length) return false;
        out.point = from + dir * dist;
        out.part = hit->id;
        if (hit->id == Scene::kTerrainId) {
            out.terrain = true;
            out.normal = scene.terrain().normalAt(out.point.x, out.point.z);
            out.material = Material::Concrete;
        } else {
            out.normal = faceNormal(hit, out.point);
            out.material = hit->material;
            out.flesh = characterOf(scene, hit) != nullptr;
        }
        return true;
    };
    auto thickness = [&](const Hit& h, const glm::vec3& dir) {
        if (h.terrain) return 1e9f;
        SceneNode* part = scene.findById(h.part);
        if (!part) return 1e9f;
        glm::vec3 out;
        if (!Gore::exitPoint(*part, h.point, dir, out)) return 0.05f;   // only grazed it
        return glm::length(out - h.point);
    };
    Result r = trace(shot, cast, thickness);

    // What you hear and see.
    ParticleSystem& fx = scene.particles();
    uint32_t seed = shot.seed * 7919u + 13u;
    for (const Hit& h : r.hits) {
        const float pitch = 0.9f + 0.2f * rand01(seed);
        if (h.flesh) {
            Audio::play("hit_flesh", 0.7f, pitch, false, &h.point);
            continue;   // (the wound itself comes with the harm: TakeDamage)
        }
        const Realism::Feel feel = h.terrain ? Realism::feelOf(scene.terrain().materialAt(h.point.x, h.point.z))
                                             : Realism::feelOf(h.material);
        Audio::play(Realism::hitSound(feel), 0.75f, pitch, false, &h.point);
        if (h.what == Hit::Ricochet) {
            fx.sparks(h.point, 10);
            Audio::play("whiz", 0.55f, 0.9f + 0.3f * rand01(seed), false, &h.point);
        } else if (feel == Realism::Feel::Metal || feel == Realism::Feel::Stone) {
            fx.sparks(h.point, feel == Realism::Feel::Metal ? 6 : 3);
        }
        // A bullet hole (only in things that stay put, or it would hang in the air).
        const SceneNode* part = h.terrain ? nullptr : scene.findById(h.part);
        if (h.terrain || (part && part->anchored)) {
            const bool glass = feel == Realism::Feel::Glass;
            const glm::vec3 col = glass ? glm::vec3(0.3f, 0.33f, 0.37f) : glm::vec3(0.06f, 0.05f, 0.045f);
            const float size = glass ? 0.55f : 0.16f;
            fx.mark(h.point, h.normal, h.velocity, glm::vec3(size, 0.02f, size), col, glass ? 3 : 0,
                    1.0f, 60.0f);
        }
    }
    // A near miss: the crack of it going past your head.
    if (listener && glm::length(*listener - shot.origin) > 6.0f) {
        float best = 1e9f;
        glm::vec3 where(0.0f);
        for (size_t i = 1; i < r.path.size(); ++i) {
            const glm::vec3 c = closestOn(r.path[i - 1], r.path[i], *listener);
            const float d = glm::length(c - *listener);
            if (d < best) { best = d; where = c; }
        }
        bool hitYou = false;
        for (const Hit& h : r.hits) if (glm::length(h.point - *listener) < 2.5f) hitYou = true;
        if (best < 6.0f && !hitYou)
            Audio::play("whiz", std::clamp(1.0f - best / 6.0f, 0.2f, 0.9f), 0.9f + 0.25f * rand01(seed), false, &where);
    }
    return r;
}

} // namespace Ballistics
