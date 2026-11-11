#include "Tracks.h"
#include "Particles.h"
#include "Physics.h"
#include "Realism.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>

namespace Tracks {

float slip(const glm::vec3& v, const glm::vec3& w, const glm::vec3& toGround) {
    const glm::vec3 contact = v + glm::cross(w, toGround);
    return glm::length(glm::vec2(contact.x, contact.z));
}

void System::update(Scene& scene, float dt) {
    (void)dt;
    // Find the wheels now and then: the smaller part of each motor hinge.
    m_listTimer -= dt;
    if (m_listTimer <= 0.0f) {
        m_listTimer = 1.0f;
        m_list.clear();
        scene.forEach([&](SceneNode* n) {
            if (!n->isConstraint() || !n->enabled || n->constraintType != ConstraintType::Hinge || n->motorTorque <= 0.0f) return;
            SceneNode* a0 = scene.findById(n->ref0);
            SceneNode* a1 = scene.findById(n->ref1);
            if (!a0 || !a1 || !a0->parent || !a1->parent) return;
            auto vol = [](const SceneNode* p) { const glm::vec3 s = p->transform.scale; return s.x * s.y * s.z; };
            SceneNode* wheel = vol(a0->parent) < vol(a1->parent) ? a0->parent : a1->parent;
            if (wheel->isPart() && !wheel->anchored) m_list.push_back(wheel->id);
        });
    }
    ParticleSystem& fx = scene.particles();
    for (uint64_t id : m_list) {
        SceneNode* wheel = scene.findById(id);
        if (!wheel) continue;
        const glm::vec3 s = wheel->transform.scale;
        const float radius = 0.5f * std::max(s.x, std::max(s.y, s.z));
        const float width = std::max(0.25f, std::min(s.x, std::min(s.y, s.z)));
        const glm::vec3 c(wheel->worldMatrix()[3]);
        float dist = 0.0f;
        SceneNode* model = wheel->parent;
        SceneNode* ground = Physics::raycastIf(scene, c, glm::vec3(0, -1, 0), &dist, [&](const SceneNode* n) {
            if (n == scene.root()) return true;
            for (const SceneNode* p = n; p; p = p->parent) if (p == model) return false;   // (its own car)
            return n->id == Scene::kTerrainId || (n->isPart() && n->canCollide);
        });
        Wheel& w = m_wheels[id];
        if (!ground || dist > radius + 0.35f) { w.has = false; continue; }   // in the air
        const glm::vec3 at = c - glm::vec3(0.0f, dist, 0.0f);
        if (!w.has) { w.last = at; w.has = true; continue; }
        glm::vec3 moved = at - w.last;
        moved.y = 0.0f;
        const float len = glm::length(moved);
        if (len > 8.0f) { w.last = at; continue; }   // (teleported)
        if (len < 0.5f) continue;
        // Soft ground keeps a track; sliding leaves rubber on anything.
        const bool terrain = ground->id == Scene::kTerrainId;
        bool soft = false;
        glm::vec3 col(0.0f), normal(0, 1, 0);
        if (terrain) {
            const TerrainMaterial tm = scene.terrain().materialAt(at.x, at.z);
            soft = Realism::takesPrints(tm);
            col = Realism::printColor(tm);
            normal = scene.terrain().normalAt(at.x, at.z);
        } else if (ground->anchored) {
            soft = Realism::takesPrints(ground->material);
            col = Realism::printColor(ground->material, ground->color);
        }
        const float sliding = slip(wheel->velocity, wheel->angularVelocity, at - c);
        const bool skid = sliding > kSkidSlip && (terrain || ground->anchored);
        const glm::vec3 mid = (at + w.last) * 0.5f;
        w.last = at;
        if (skid) {
            fx.mark(mid, normal, moved, glm::vec3(width * 0.9f, 0.02f, len + 0.15f), glm::vec3(0.03f), 1,
                    std::clamp(0.35f + sliding / 40.0f, 0.35f, 0.8f), 45.0f);
            ++m_made;
        } else if (soft) {
            fx.mark(mid, normal, moved, glm::vec3(width, 0.02f, len + 0.1f), col, 1, 0.7f, 60.0f);
            ++m_made;
        }
    }
}

} // namespace Tracks
