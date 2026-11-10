#include "FireSpread.h"
#include "Physics.h"
#include "Player.h"
#include "Realism.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Shatter.h"
#include "Environment.h"
#include "../core/Audio.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace FireSpread {

float catchRate(float flammability, bool raining) {
    if (flammability <= 0.0f) return 0.0f;
    // Dry wood next to a fire catches in about 4 s on average; wet weather triples that.
    return flammability * 0.25f * (raining ? 0.33f : 1.0f);
}

float charAfter(float seconds, float flammability) {
    if (flammability <= 0.0f) return 0.0f;
    // Wood is burnt through in about 30 s; cloth quicker, plastic slower.
    return std::clamp(seconds * flammability / 30.0f, 0.0f, 1.0f);
}

namespace {
SceneNode* fireIn(SceneNode* part) {
    for (auto& c : part->children)
        if (c->isParticles() && c->enabled && c->particle.type == ParticleType::Fire) return c.get();
    return nullptr;
}
float rand01(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / float(0xffffff); }
} // namespace

void System::clear() {
    for (auto& [id, h] : m_sounds) Audio::stop(h);
    m_sounds.clear();
    m_burn.clear();
    m_timer = 0.0f;
}

void System::update(Scene& scene, float dt, bool host, const glm::vec3& ears) {
    m_timer -= dt;
    if (m_timer > 0.0f) return;
    const float step = 0.25f;
    m_timer = step;

    // What's on fire, and what could catch.
    struct Spot { SceneNode* part; AABB box; float flam; };
    std::vector<Spot> burning, fuel;
    const bool spread = host && scene.world().fireSpread;
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->id == Scene::kTerrainId) return;
        if (fireIn(n)) burning.push_back({n, Physics::worldBounds(n), Realism::flammability(n->material)});
        else if (spread && Realism::flammability(n->material) > 0.0f && !scene.isCharacterPart(n))
            fuel.push_back({n, Physics::worldBounds(n), Realism::flammability(n->material)});
    });

    // The crackle: the nearest few fires.
    std::sort(burning.begin(), burning.end(), [&](const Spot& a, const Spot& b) {
        return glm::length((a.box.min + a.box.max) * 0.5f - ears) < glm::length((b.box.min + b.box.max) * 0.5f - ears);
    });
    std::unordered_map<uint64_t, int> keep;
    for (size_t i = 0; i < burning.size() && i < 6; ++i) {
        SceneNode* p = burning[i].part;
        const glm::vec3 at = (burning[i].box.min + burning[i].box.max) * 0.5f;
        const float vol = std::clamp(0.2f + fireIn(p)->particle.fxSize * 0.05f, 0.2f, 0.7f);
        auto it = m_sounds.find(p->id);
        int h = it != m_sounds.end() ? it->second : 0;
        if (!h || !Audio::isPlaying(h)) h = Audio::play("fire_loop", vol, 0.9f + 0.2f * rand01(m_seed), true, &at);
        else Audio::setPosition(h, at);
        if (h) keep[p->id] = h;
        if (it != m_sounds.end()) m_sounds.erase(it);
    }
    for (auto& [id, h] : m_sounds) Audio::stop(h);
    m_sounds.swap(keep);

    if (!spread) { m_burn.clear(); return; }
    const Environment& env = scene.environment();
    const bool raining = env.weather == (int)Weather::Rain;
    std::vector<uint64_t> burntOut, putOut;
    for (const Spot& s : burning) {
        SceneNode* p = s.part;
        SceneNode* fire = fireIn(p);
        const bool ours = fire && fire->name == kFireName;
        // In water: out it goes (only the fires it started; a game's own torches stay lit).
        const glm::vec3 mid = (s.box.min + s.box.max) * 0.5f;
        if (ours && Player::waterAt(scene, mid)) { putOut.push_back(p->id); continue; }
        // Burning: charring black, then falling to bits.
        if (s.flam > 0.0f) {
            auto [it, fresh] = m_burn.try_emplace(p->id);
            if (fresh) it->second.color0 = p->color;
            it->second.time += step;
            const float c = charAfter(it->second.time, s.flam);
            p->color = glm::mix(it->second.color0, glm::vec3(0.07f, 0.06f, 0.055f), c);
            if (c >= 1.0f) burntOut.push_back(p->id);
        }
        // Setting what it touches alight.
        const AABB reach = s.box.inflated(0.6f);
        for (const Spot& f : fuel) {
            if (!reach.overlaps(f.box)) continue;
            if (rand01(m_seed) > catchRate(f.flam, raining) * step) continue;
            if (fireIn(f.part)) continue;
            auto made = std::make_unique<SceneNode>(kFireName, NodeKind::Particles);
            made->particle = particleDefaults(ParticleType::Fire);
            const glm::vec3 size = f.box.max - f.box.min;
            made->particle.fxSize = std::clamp(std::max(size.x, size.z) * 0.8f, 2.0f, 10.0f);
            f.part->addChild(std::move(made));
        }
    }
    for (uint64_t id : putOut)
        if (SceneNode* p = scene.findById(id)) {
            const glm::vec3 at(p->worldMatrix()[3]);
            for (auto& c : p->children)
                if (c->isParticles() && c->name == kFireName) { scene.removeNode(c.get()); break; }
            Audio::play("splash", 0.4f, 1.3f, false, &at);   // (a hiss)
        }
    for (uint64_t id : burntOut) {
        m_burn.erase(id);
        if (SceneNode* p = scene.findById(id))
            Shatter::breakPart(scene, p, glm::vec3(p->worldMatrix()[3]), 0.25f);
    }
    // Forget parts that stopped burning (or are gone).
    for (auto it = m_burn.begin(); it != m_burn.end();) {
        SceneNode* p = scene.findById(it->first);
        if (!p || !fireIn(p)) it = m_burn.erase(it);
        else ++it;
    }
}

} // namespace FireSpread
