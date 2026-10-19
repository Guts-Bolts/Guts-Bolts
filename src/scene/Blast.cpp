#include "Blast.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Player.h"
#include "Physics.h"
#include "Water.h"
#include "../core/Audio.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace {
std::mt19937& rng() { static std::mt19937 r(1234567u); return r; }
float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng()); }
glm::vec3 rndDir() {
    for (;;) {
        glm::vec3 v(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
        float l = glm::length(v);
        if (l > 0.05f && l <= 1.0f) return v / l;
    }
}
glm::vec3 rndFlat() { float a = rnd(0.0f, 6.2831853f); return {std::cos(a), 0.0f, std::sin(a)}; }
const glm::vec3 kSmoke(0.16f, 0.15f, 0.14f), kDust(0.48f, 0.42f, 0.35f), kSpray(0.86f, 0.9f, 0.94f);
bool bigEnoughForMushroom(const BlastOptions& o) { return o.mushroom == 1 || (o.mushroom < 0 && o.radius * std::sqrt(std::max(o.power, 0.1f)) >= 45.0f); }
} // namespace

std::string BlastOptions::encode() const {
    nlohmann::json j = {{"r", radius}, {"p", power}, {"s", smoke}, {"f", fire}, {"m", mushroom}, {"d", destroy}, {"v", visible}};
    return j.dump();
}

BlastOptions BlastOptions::decode(const std::string& text, float r) {
    BlastOptions o;
    o.radius = r;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object()) return o;
    o.radius = std::clamp(j.value("r", r), 0.5f, 2000.0f);
    o.power = std::clamp(j.value("p", 1.0f), 0.0f, 100.0f);
    o.smoke = j.value("s", true);
    o.fire = std::clamp(j.value("f", 0.0f), 0.0f, 120.0f);
    o.mushroom = j.value("m", -1);
    o.destroy = j.value("d", false);
    o.visible = j.value("v", true);
    return o;
}

void BlastSystem::clear() {
    m_puffs.clear(); m_shocks.clear(); m_live.clear(); m_fires.clear(); m_clouds.clear(); m_rumbles.clear();
}

void BlastSystem::add(const Puff& p) {
    if ((int)m_puffs.size() >= kMaxPuffs) {
        // Full: replace the puff closest to fading away.
        size_t worst = 0;
        float least = 1e9f;
        for (size_t i = 0; i < m_puffs.size(); ++i) {
            const float left = m_puffs[i].life / std::max(m_puffs[i].maxLife, 1e-3f);
            if (left < least) { least = left; worst = i; }
        }
        m_puffs[worst] = p;
        return;
    }
    m_puffs.push_back(p);
}

// The fireball: hot puffs that swell, glow and cool into smoke.
void BlastSystem::fireball(const glm::vec3& c, const BlastOptions& o) {
    const float R = o.radius;
    const int n = (int)std::clamp(R * 2.5f, 10.0f, 90.0f);
    for (int i = 0; i < n; ++i) {
        Puff p;
        p.pos = c + rndDir() * R * rnd(0.0f, 0.25f);
        p.vel = rndDir() * R * rnd(0.6f, 1.6f) + glm::vec3(0, R * rnd(0.2f, 0.6f), 0);
        p.radius = R * rnd(0.12f, 0.22f);
        p.grow = R * rnd(0.25f, 0.45f);
        p.color = glm::mix(glm::vec3(1.0f, 0.55f, 0.15f), glm::vec3(1.0f, 0.85f, 0.45f), rnd(0, 1));
        p.glow = rnd(3.0f, 6.0f);
        p.alpha = 0.9f;
        p.life = p.maxLife = rnd(0.5f, 1.1f) * std::max(1.0f, std::sqrt(R / 8.0f));
        p.drag = 3.5f;
        p.lift = 2.0f;
        p.seed = rnd(0, 100);
        p.coolTo = o.smoke ? kSmoke * rnd(0.8f, 1.3f) : glm::vec3(-1.0f);
        add(p);
    }
    // A blinding flash at the middle.
    Puff f;
    f.pos = c;
    f.radius = R * 0.35f;
    f.grow = R * 1.5f;
    f.color = glm::vec3(1.0f, 0.95f, 0.8f);
    f.glow = 12.0f;
    f.alpha = 0.8f;
    f.life = f.maxLife = 0.18f + std::min(0.5f, R * 0.004f);
    f.drag = 0.0f;
    f.seed = rnd(0, 100);
    add(f);
    if (!o.smoke) return;
    // Smoke left hanging, drifting and spreading.
    const int s = (int)std::clamp(R * 1.6f, 8.0f, 70.0f);
    for (int i = 0; i < s; ++i) {
        Puff p;
        p.pos = c + rndDir() * R * rnd(0.1f, 0.45f);
        p.vel = rndDir() * R * rnd(0.2f, 0.6f) + glm::vec3(0.4f, R * rnd(0.05f, 0.2f), 0);
        p.radius = R * rnd(0.15f, 0.3f);
        p.grow = R * rnd(0.04f, 0.1f);
        p.color = kSmoke * rnd(0.7f, 1.4f);
        p.alpha = rnd(0.45f, 0.7f);
        p.life = p.maxLife = rnd(5.0f, 10.0f) * std::max(1.0f, std::sqrt(R / 10.0f));
        p.drag = 1.2f;
        p.lift = 0.6f;
        p.seed = rnd(0, 100);
        add(p);
    }
}

// Blasts in or next to water.
void BlastSystem::water(Scene& scene, const glm::vec3& c, const BlastOptions& o) {
    WaterSystem& ws = scene.water();
    if (!ws.active()) return;
    const float R = o.radius;
    // Is there water here, or just below (a blast on the surface, or a bomb dropped in)?
    float surface = 0.0f;
    glm::vec3 probe = c;
    bool found = ws.at(probe, &surface);
    if (!found) { probe = c - glm::vec3(0, R * 0.5f, 0); found = ws.at(probe, &surface); }
    if (!found) return;
    const float depth = surface - c.y;                       // > 0: under water
    if (depth < -R * 0.5f) return;                           // too far above it
    const float k = std::clamp(1.0f - std::abs(depth) / std::max(R * 0.9f, 1.0f), 0.15f, 1.0f);
    const glm::vec3 at(c.x, surface, c.z);
    // A crater in the surface, and rings of waves running out of it.
    ws.disturb(at, std::min(2.0f, 0.04f * R * k + 0.3f), R * 0.5f);
    ws.splash(scene, at, 20.0f + R, std::max(1.0f, R * 0.4f));
    scene.particles().waterSpray(at, (int)std::clamp(R * 25.0f * k, 40.0f, 1200.0f), 6.0f + R * 0.6f * k,
                                 glm::vec3(0.85f, 0.92f, 1.0f), R * 0.3f);
    // A column (and dome) of white spray thrown up, falling back as rain.
    const int n = (int)std::clamp(R * 2.0f * k, 6.0f, 120.0f);
    for (int i = 0; i < n; ++i) {
        Puff p;
        glm::vec3 d = rndFlat() * rnd(0.0f, R * 0.3f);
        p.pos = at + d;
        p.vel = glm::vec3(d.x * 0.6f, R * rnd(0.6f, 1.4f) * k + 4.0f, d.z * 0.6f);
        p.radius = R * rnd(0.08f, 0.16f) + 0.4f;
        p.grow = R * 0.06f;
        p.color = kSpray * rnd(0.92f, 1.05f);
        p.alpha = rnd(0.5f, 0.75f);
        p.life = p.maxLife = rnd(2.5f, 5.0f) * std::max(1.0f, std::sqrt(R / 12.0f));
        p.drag = 0.5f;
        p.lift = -scene.world().gravity * 0.35f;   // it falls back down
        p.seed = rnd(0, 100);
        add(p);
    }
    // Big blasts in big water: a tsunami.
    if (R * k >= 10.0f)
        ws.addSurge(at, 0.06f * R * k + 0.6f, std::clamp(10.0f + R * 0.5f, 12.0f, 90.0f), std::max(2.5f, R * 0.12f));
}

std::vector<BlastSystem::Hit> BlastSystem::start(Scene& scene, const glm::vec3& pos, const BlastOptions& in, bool authority) {
    BlastOptions o = in;
    o.radius = std::clamp(o.radius, 0.5f, 2000.0f);
    o.power = std::clamp(o.power, 0.0f, 100.0f);
    const float R = o.radius;
    std::vector<Hit> hits;

    // Sound: bigger = louder and deeper.
    Audio::play("explosion", std::min(1.0f, 0.5f + R * 0.03f), std::max(0.45f, 1.1f - std::sqrt(R) * 0.06f), false, &pos);

    // What the shockwave will reach (for scripts: Explosion.Hit gives each part once).
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isViewportFrame()) continue;   // (its parts are only a picture)
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n == scene.root()) continue;
        const float d = glm::length(glm::vec3(n->worldMatrix()[3]) - pos);
        if (d <= R) hits.push_back({n, d});
    }

    // The ground under it (for the dust ring and the fires).
    float dist = 0.0f;
    float groundY = -1e9f;
    if (Physics::raycast(scene, pos + glm::vec3(0, 0.05f, 0), {0, -1, 0}, &dist) && dist < R * 0.8f + 2.0f) groundY = pos.y + 0.05f - dist;

    if (o.visible) {
        fireball(pos, o);
        Shock s;
        s.center = pos;
        s.maxRadius = R * 1.6f;
        s.speed = std::clamp(R * 6.0f, 40.0f, 600.0f);
        s.groundY = groundY;
        m_shocks.push_back(s);
        // Lingering fires on the ground around it.
        if (o.fire > 0.0f && groundY > -1e8f) {
            const int n = (int)std::clamp(R * 0.6f, 2.0f, 30.0f);
            for (int i = 0; i < n; ++i) {
                glm::vec3 p = pos + rndFlat() * rnd(0.0f, R * 0.55f) + glm::vec3(0, 2.0f, 0);
                float dd = 0.0f;
                if (Physics::raycast(scene, p, {0, -1, 0}, &dd) && dd < R) {
                    p.y -= dd;
                    m_fires.push_back({p, o.fire * rnd(0.6f, 1.0f), rnd(0.6f, 1.4f) * std::clamp(R * 0.05f, 0.6f, 3.0f)});
                }
            }
        }
        // Really big: a mushroom cloud.
        if (bigEnoughForMushroom(o)) {
            Cloud cl;
            cl.ground = groundY > -1e8f ? glm::vec3(pos.x, groundY, pos.z) : pos;
            cl.height = R * 2.2f;
            cl.cap = R * 0.75f;
            cl.life = std::clamp(R * 0.45f, 25.0f, 60.0f);
            cl.capCenter = cl.ground;
            m_clouds.push_back(cl);
        }
    }
    water(scene, pos, o);
    m_rumbles.push_back({pos, 0.0f, std::clamp(R * 6.0f, 40.0f, 600.0f), o.power, R, 0.0f});

    Live b;
    b.center = pos;
    b.opts = o;
    b.speed = std::clamp(R * 6.0f, 40.0f, 600.0f);
    b.authority = authority;
    m_live.push_back(std::move(b));
    return hits;
}

// The shockwave reached from `from` to `to` this frame: push what's in that shell.
void BlastSystem::push(Scene& scene, Live& b, float from, float to, const Physics&) {
    const float R = b.opts.radius, P = b.opts.power;
    std::vector<SceneNode*> stack{scene.root()}, joints;
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isViewportFrame()) continue;   // (its parts are only a picture)
        for (auto& c : n->children) stack.push_back(c.get());
        if (n->isConstraint()) { joints.push_back(n); continue; }
        if (!n->isPart() || scene.isCharacterPart(n) || b.done.count(n->id)) continue;
        const glm::vec3 p(n->worldMatrix()[3]);
        const float d = glm::length(p - b.center);
        if (d > to || d > R) continue;
        b.done.insert(n->id);
        const float k = std::pow(1.0f - d / R, 1.5f);
        const glm::vec3 dir = d > 1e-3f ? (p - b.center) / d : glm::vec3(0, 1, 0);
        // Craters: big "destroy" blasts tear anchored things loose near the middle.
        if (n->anchored) {
            if (!b.opts.destroy || d > R * 0.4f) continue;
            n->anchored = false;
        }
        // Light things fly further than heavy ones.
        const glm::vec3 s = n->transform.scale;
        const float volume = std::max(0.05f, s.x * s.y * s.z);
        const float heft = std::sqrt(std::max(1.0f, volume * std::max(0.1f, n->density >= 0 ? n->density : 0.7f) / 4.0f));
        n->velocity += (dir + glm::vec3(0, 0.45f, 0)) * 48.0f * P * k / heft;
        n->angularVelocity += glm::cross(glm::vec3(0, 1, 0), dir) * 10.0f * P * k / heft + rndDir() * 3.0f * k;
        Physics::wake(n);
    }
    // Joints in the blast come apart (welds, hinges, ropes...).
    std::vector<SceneNode*> broken;
    for (SceneNode* j : joints) {
        for (uint64_t id : {j->ref0, j->ref1}) {
            SceneNode* a = id ? scene.findById(id) : nullptr;
            if (!a) continue;
            // (attachments: their part)
            const glm::vec3 p(a->worldMatrix()[3]);
            const float d = glm::length(p - b.center);
            if (d >= from && d <= to && d <= R * b.opts.jointBreak) { broken.push_back(j); break; }
        }
    }
    for (SceneNode* j : broken) scene.removeNode(j);

    if (!b.opts.hurts) return;
    // People: hurt and thrown when the front reaches them.
    Player* player = scene.player();
    if (player && player->root() && !player->isDead() && !b.playerHit) {
        const glm::vec3 body = player->position() + glm::vec3(0, 1.3f, 0);
        const float d = glm::length(body - b.center);
        if (d <= to) {
            b.playerHit = true;
            if (d < R) {
                const float k = 1.0f - d / R;
                const glm::vec3 dir = d > 1e-3f ? (body - b.center) / d : glm::vec3(0, 1, 0);
                player->hurt(160.0f * P * k, std::min(1.0f, 0.4f + k * P), (dir + glm::vec3(0, 0.8f, 0)) * 22.0f * P * k);
            }
        }
    }
    for (RemoteCharacter& rc : scene.remotes()) {   // (multiplayer host: their computers do the ragdoll)
        SceneNode* root = scene.findById(rc.rootId);
        if (!root || !rc.alive || root->hasForceField() || b.done.count(rc.rootId)) continue;
        const glm::vec3 body = root->transform.position + glm::vec3(0, 1.3f, 0);
        const float d = glm::length(body - b.center);
        if (d > to) continue;
        b.done.insert(rc.rootId);
        if (d >= R) continue;
        const float k = 1.0f - d / R;
        const glm::vec3 dir = d > 1e-3f ? (body - b.center) / d : glm::vec3(0, 1, 0);
        const glm::vec3 impulse = (dir + glm::vec3(0, 0.8f, 0)) * 22.0f * P * k;
        rc.humanoid.health = std::max(0.0f, rc.humanoid.health - 160.0f * P * k);
        rc.humanoidDirty = true;
        if (rc.humanoid.health <= 0.0f) rc.kills.push_back({std::min(1.0f, 0.4f + k * P), impulse});
        else                            rc.kills.push_back({-1.0f, impulse});
    }
}

void BlastSystem::update(float dt, Scene& scene, const Physics& physics) {
    if (dt <= 0.0f) return;
    // Shockwaves going out.
    for (size_t i = 0; i < m_live.size();) {
        Live& b = m_live[i];
        const float from = b.front;
        b.front += b.speed * dt;
        if (b.authority) push(scene, b, from, b.front, physics);
        if (b.front > b.opts.radius * 1.6f + 5.0f) { m_live.erase(m_live.begin() + (long)i); continue; }
        ++i;
    }
    for (size_t i = 0; i < m_shocks.size();) {
        Shock& s = m_shocks[i];
        s.age += dt;
        s.radius += s.speed * dt;
        // Dust kicked up along the ground where the ring passes.
        if (s.groundY > -1e8f && s.radius < s.maxRadius && (int)m_puffs.size() < kMaxPuffs - 50) {
            const int n = (int)std::clamp(s.radius * 0.25f, 1.0f, 8.0f);
            for (int k = 0; k < n; ++k) {
                Puff p;
                const glm::vec3 d = rndFlat();
                p.pos = glm::vec3(s.center.x, s.groundY + 0.3f, s.center.z) + d * s.radius;
                p.vel = d * s.speed * 0.15f + glm::vec3(0, rnd(0.5f, 2.0f), 0);
                p.radius = std::max(0.4f, s.maxRadius * rnd(0.03f, 0.06f));
                p.grow = p.radius * 0.6f;
                p.color = kDust * rnd(0.8f, 1.15f);
                p.alpha = rnd(0.25f, 0.45f);
                p.life = p.maxLife = rnd(2.0f, 4.5f);
                p.drag = 1.5f;
                p.lift = 0.3f;
                p.seed = rnd(0, 100);
                add(p);
            }
        }
        if (s.radius > s.maxRadius) { m_shocks.erase(m_shocks.begin() + (long)i); continue; }
        ++i;
    }
    for (size_t i = 0; i < m_rumbles.size();) {
        Rumble& r = m_rumbles[i];
        r.front += r.speed * dt;
        r.age += dt;
        if (r.age > 6.0f) { m_rumbles.erase(m_rumbles.begin() + (long)i); continue; }
        ++i;
    }

    // Fires burning on the ground: flames and smoke, and they hurt to stand in.
    Player* player = scene.player();
    for (size_t i = 0; i < m_fires.size();) {
        Fire& f = m_fires[i];
        f.life -= dt;
        f.timer -= dt;
        if (f.timer <= 0.0f && (int)m_puffs.size() < kMaxPuffs - 20) {
            f.timer = 0.07f;
            Puff p;
            p.pos = f.pos + rndFlat() * rnd(0.0f, f.size * 0.6f);
            p.vel = glm::vec3(rnd(-0.3f, 0.3f), rnd(1.5f, 3.0f) * f.size, rnd(-0.3f, 0.3f));
            p.radius = f.size * rnd(0.35f, 0.6f);
            p.grow = -f.size * 0.25f;
            p.color = glm::mix(glm::vec3(1.0f, 0.4f, 0.08f), glm::vec3(1.0f, 0.75f, 0.25f), rnd(0, 1));
            p.glow = rnd(2.0f, 4.0f);
            p.alpha = 0.85f;
            p.life = p.maxLife = rnd(0.35f, 0.7f);
            p.drag = 0.5f;
            p.seed = rnd(0, 100);
            add(p);
            if (rnd(0, 1) < 0.35f) {
                Puff s = p;
                s.pos.y += f.size;
                s.radius = f.size * rnd(0.5f, 0.9f);
                s.grow = f.size * 0.5f;
                s.color = kSmoke * rnd(0.8f, 1.2f);
                s.glow = 0.0f;
                s.alpha = rnd(0.25f, 0.45f);
                s.life = s.maxLife = rnd(2.5f, 5.0f);
                s.lift = 0.8f;
                add(s);
            }
        }
        if (player && player->root() && !player->isDead()) {
            const glm::vec3 feet = player->position();
            if (glm::length(glm::vec2(feet.x - f.pos.x, feet.z - f.pos.z)) < f.size && std::abs(feet.y - f.pos.y) < 2.0f)
                player->hurt(18.0f * dt, 0.0f, glm::vec3(0.0f));
        }
        if (f.life <= 0.0f) { m_fires.erase(m_fires.begin() + (long)i); continue; }
        ++i;
    }

    // Mushroom clouds: the cap rises and spreads; puffs roll around its ring.
    for (size_t ci = 0; ci < m_clouds.size(); ++ci) {
        Cloud& c = m_clouds[ci];
        c.age += dt;
        const float rise = 1.0f - std::exp(-c.age / 7.0f);
        c.capCenter = c.ground + glm::vec3(0, c.height * rise, 0);
        const float spread = 0.55f + 0.45f * rise;
        if (c.age < c.life * 0.6f) {
            c.timer -= dt;
            while (c.timer <= 0.0f && (int)m_puffs.size() < kMaxPuffs - 10) {
                c.timer += 0.025f;
                Puff p;
                p.cloud = (int)ci;
                p.theta = rnd(0.0f, 6.2831853f);
                p.phi = rnd(0.0f, 6.2831853f);
                p.ring = c.cap * spread;
                p.tube = c.cap * 0.45f * spread * rnd(0.6f, 1.0f);
                p.radius = c.cap * rnd(0.22f, 0.36f) * spread;
                p.grow = c.cap * 0.01f;
                const float hot = std::max(0.0f, 1.0f - c.age / 10.0f);
                p.color = glm::mix(glm::vec3(0.36f, 0.28f, 0.22f), glm::vec3(0.30f, 0.27f, 0.25f), rnd(0, 1));
                p.glow = hot * rnd(0.5f, 1.6f);   // lit up orange from inside at first
                p.coolTo = glm::vec3(-1.0f);
                p.alpha = rnd(0.55f, 0.8f);
                p.life = p.maxLife = std::min(c.life - c.age, rnd(8.0f, 16.0f));
                p.seed = rnd(0, 100);
                add(p);
            }
            // The stem: smoke and dust climbing from the ground to the cap.
            c.stemTimer -= dt;
            while (c.stemTimer <= 0.0f && (int)m_puffs.size() < kMaxPuffs - 10) {
                c.stemTimer += 0.03f;
                Puff p;
                const float t = rnd(0.0f, 1.0f);
                p.pos = glm::mix(c.ground, c.capCenter, t) + rndFlat() * c.cap * 0.18f * (1.0f - t * 0.5f);
                p.vel = glm::vec3(0, c.height * 0.08f, 0);
                p.radius = c.cap * rnd(0.12f, 0.2f);
                p.grow = c.cap * 0.03f;
                p.color = glm::mix(kDust, kSmoke, t) * rnd(0.85f, 1.15f);
                p.alpha = rnd(0.4f, 0.65f);
                p.life = p.maxLife = rnd(3.0f, 6.0f);
                p.drag = 0.6f;
                p.seed = rnd(0, 100);
                add(p);
            }
        }
    }

    // Puffs: drift, swell, cool, fade.
    const glm::vec3 wind(0.6f, 0.0f, 0.2f);
    for (size_t i = 0; i < m_puffs.size();) {
        Puff& p = m_puffs[i];
        p.life -= dt;
        if (p.life <= 0.0f || p.radius <= 0.02f) { p = m_puffs.back(); m_puffs.pop_back(); continue; }
        if (p.cloud >= 0 && p.cloud < (int)m_clouds.size()) {
            const Cloud& c = m_clouds[(size_t)p.cloud];
            // Roll over the top: up the middle, out over the cap, down the outside.
            p.phi += dt * 0.35f;
            const glm::vec3 out(std::cos(p.theta), 0.0f, std::sin(p.theta));
            p.pos = c.capCenter + out * (p.ring + p.tube * std::cos(p.phi)) + glm::vec3(0, p.tube * std::sin(p.phi) * 0.7f, 0) +
                    wind * c.age * 0.3f;
        } else {
            p.vel *= std::max(0.0f, 1.0f - p.drag * dt);
            p.vel.y += p.lift * dt;
            p.pos += (p.vel + wind) * dt;
        }
        p.radius += p.grow * dt;
        if (p.glow > 0.0f) {
            p.glow *= std::max(0.0f, 1.0f - 2.2f * dt);   // cools down
            if (p.coolTo.x >= 0.0f) p.color = glm::mix(p.color, p.coolTo, std::min(1.0f, 2.5f * dt));
        }
        ++i;
    }
    // Clouds that are gone (and no puffs need them any more).
    if (!m_clouds.empty()) {
        bool allDone = true;
        for (const Cloud& c : m_clouds) if (c.age < c.life) allDone = false;
        if (allDone) {
            for (Puff& p : m_puffs) p.cloud = -1;
            m_clouds.clear();
        }
    }
}

float BlastSystem::shake(const glm::vec3& at) const {
    float s = 0.0f;
    for (const Rumble& r : m_rumbles) {
        const float d = glm::length(at - r.center);
        const float reach = r.radius * 4.0f + 10.0f;
        if (d > reach) continue;
        // Strongest as the front passes us, then dies away.
        const float behind = r.front - d;
        if (behind < 0.0f) continue;
        const float since = behind / std::max(r.speed, 1.0f);
        const float k = (1.0f - d / reach) * std::min(2.0f, 0.5f + r.power * 0.5f) * std::sqrt(std::min(r.radius, 200.0f) / 10.0f);
        s += k * std::exp(-since * 2.5f);
    }
    return std::min(s, 3.0f);
}
