#include "Acoustics.h"
#include "Gore.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../core/Audio.h"
#include <algorithm>
#include <cmath>

namespace Acoustics {

float occlusion(int walls, float thickness) {
    if (walls <= 0) return 0.0f;
    // One thin wall: about half muffled; two walls or a thick one: nearly all.
    return std::clamp(0.35f * (float)walls + 0.12f * thickness, 0.0f, 1.0f);
}

Room roomFrom(int hits, int rays, float averageDistance) {
    Room r;
    if (rays <= 0 || hits <= 0) return r;
    const float enclosed = (float)hits / (float)rays;       // 1 = walls all round (and a roof)
    // Bigger spaces ring on longer: a cupboard 0.3 s, a hall 2-3 s, a huge cave up to 5 s.
    r.decay = std::clamp(0.25f + averageDistance / 12.0f, 0.25f, 5.0f);
    // Mostly open (a few walls nearby, no roof): hardly any echo.
    r.wet = enclosed < 0.35f ? 0.0f : std::clamp((enclosed - 0.35f) / 0.65f, 0.0f, 1.0f) * 0.85f;
    return r;
}

const std::vector<glm::vec3>& roomRays() {
    static const std::vector<glm::vec3> rays = [] {
        // A Fibonacci ball: 18 directions spread evenly (up included, for the roof).
        std::vector<glm::vec3> v;
        const int n = 18;
        const float golden = 2.39996323f;
        for (int i = 0; i < n; ++i) {
            const float y = 1.0f - (i + 0.5f) * 2.0f / n;
            const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
            v.emplace_back(std::cos(golden * i) * r, y, std::sin(golden * i) * r);
        }
        return v;
    }();
    return rays;
}

void Listener::reset() {
    m_occTimer = m_roomTimer = 0.0f;
    m_hasLast = false;
    m_room = Room{};
    Audio::setRoom(0.0f, 0.0f);
    Audio::setListenerVelocity(glm::vec3(0.0f));
}

void Listener::update(Scene& scene, float dt, const glm::vec3& ears) {
    if (!scene.world().acoustics) {
        if (m_room.wet > 0.0f || m_hasLast) reset();
        return;
    }
    // You moving: for the Doppler bend.
    if (m_hasLast && dt > 0.0f) {
        glm::vec3 v = (ears - m_lastEars) / dt;
        if (glm::length(v) > 200.0f) v = glm::vec3(0.0f);   // (a teleport, not a speed)
        Audio::setListenerVelocity(v);
    }
    m_lastEars = ears;
    m_hasLast = true;

    // What counts as a wall: solid, seen parts that aren't anybody's body.
    auto wall = [&](const SceneNode* n) {
        if (n == scene.root()) return true;
        if (n->id == Scene::kTerrainId) return true;
        if (!n->isPart()) return true;
        return n->canCollide && n->transparency < 0.9f && !scene.isCharacterPart(n);
    };

    // The room: every half a second.
    m_roomTimer -= dt;
    if (m_roomTimer <= 0.0f) {
        m_roomTimer = 0.5f;
        const float reach = 80.0f;
        int hits = 0;
        float total = 0.0f;
        for (const glm::vec3& d : roomRays()) {
            float dist = 0.0f;
            SceneNode* hit = Physics::raycastIf(scene, ears, d, &dist, wall);
            if (hit && dist < reach) { ++hits; total += dist; }
        }
        m_room = roomFrom(hits, (int)roomRays().size(), hits ? total / hits : 0.0f);
        Audio::setRoom(m_room.decay, m_room.wet);
    }

    // Walls in the way, and the sounds' own speed: ten times a second.
    m_occTimer -= dt;
    const bool walls = m_occTimer <= 0.0f;
    if (walls) m_occTimer = 0.1f;
    int checked = 0;
    scene.forEach([&](SceneNode* n) {
        if (!n->isSound() || !n->audioHandle || !n->parent || !n->parent->isPart()) return;
        SceneNode* holder = n->parent;
        Audio::setVelocity(n->audioHandle, holder->anchored ? glm::vec3(0.0f) : holder->velocity);
        if (!walls || ++checked > 48) return;
        const glm::vec3 at(holder->worldMatrix()[3]);
        glm::vec3 from = ears;
        int count = 0;
        float thick = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const glm::vec3 to = at - from;
            const float len = glm::length(to);
            if (len < 0.2f) break;
            const glm::vec3 dir = to / len;
            float dist = 0.0f;
            SceneNode* hit = Physics::raycastIf(scene, from, dir, &dist, [&](const SceneNode* p) {
                if (p == holder || holder->isAncestorOf(p) || p->isAncestorOf(holder)) return false;   // its own part
                return wall(p);
            });
            if (!hit || dist >= len - 0.1f) break;
            ++count;
            const glm::vec3 entry = from + dir * dist;
            if (hit->id == Scene::kTerrainId) { thick += 4.0f; break; }   // a hill in the way
            glm::vec3 out;
            const float t = Gore::exitPoint(*hit, entry, dir, out) ? glm::length(out - entry) : 0.5f;
            thick += t;
            from = entry + dir * (t + 0.05f);
        }
        Audio::setOcclusion(n->audioHandle, occlusion(count, thick));
    });
}

} // namespace Acoustics
