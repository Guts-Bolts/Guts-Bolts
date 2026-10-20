#include "Particles.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../core/Audio.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>

namespace {
std::mt19937& rng() { static std::mt19937 r{987u}; return r; }
float rnd(float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng()); }
glm::vec3 rndDir() {
    glm::vec3 d(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1));
    float l = glm::length(d);
    return l > 1e-4f ? d / l : glm::vec3(0, 1, 0);
}

const glm::vec3 kOil      = {0.06f, 0.06f, 0.07f};
} // namespace

void ParticleSystem::add(const Particle& p) {
    if ((int)m_items.size() >= kMaxParticles) {
        // Drop the oldest non-splat to make room.
        auto it = std::find_if(m_items.begin(), m_items.end(),
                               [](const Particle& q) { return q.kind != Particle::Splat; });
        if (it == m_items.end())   // (never a stain on a body: those stay until the character respawns)
            it = std::find_if(m_items.begin(), m_items.end(), [](const Particle& q) { return !q.onPart; });
        if (it == m_items.end()) return;
        if (it->kind == Particle::Splat) --m_splats;
        m_items.erase(it);
    }
    m_items.push_back(p);
}

void ParticleSystem::spray(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, int count, float speed) {
    count = (int)std::round(count * m_bloodAmount);
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.kind   = Particle::Drop;
        p.pos    = pos + rndDir() * 0.05f;
        p.vel    = (dir + rndDir() * 0.6f) * speed * rnd(0.4f, 1.2f) + glm::vec3(0, rnd(0.5f, 2.0f), 0);
        p.color  = kind == GoreKind::Blood ? m_bloodColor * rnd(0.62f, 1.0f) : kOil;
        float s  = rnd(0.04f, 0.11f);
        p.size   = glm::vec3(s);
        p.life   = p.maxLife = rnd(1.5f, 3.0f);
        p.wet    = true;
        add(p);
    }
}

void ParticleSystem::waterSpray(const glm::vec3& pos, int count, float speed, const glm::vec3& color, float radius) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.kind  = Particle::Spray;
        glm::vec3 d = rndDir();
        d.y = std::abs(d.y) * 1.5f + 0.8f;   // mostly up, fanning out
        glm::vec2 out = glm::normalize(glm::vec2(d.x, d.z) + glm::vec2(1e-4f));
        p.pos   = pos + glm::vec3(out.x, 0.05f, out.y) * radius * rnd(0.9f, 1.2f);
        d.x = out.x * std::abs(d.x) + d.x * 0.3f; d.z = out.y * std::abs(d.z) + d.z * 0.3f;   // thrown outwards
        p.vel   = glm::normalize(d) * speed * rnd(0.5f, 1.1f);
        p.color = glm::mix(color, glm::vec3(0.92f, 0.96f, 1.0f), rnd(0.5f, 0.9f));
        p.size  = glm::vec3(rnd(0.1f, 0.24f));
        p.life  = p.maxLife = rnd(0.6f, 1.2f);
        add(p);
    }
}

void ParticleSystem::gibs(GoreKind kind, const glm::vec3& pos, const glm::vec3& vel, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        if (kind == GoreKind::Blood) {
            p.kind  = Particle::Chunk;
            p.color = glm::mix(glm::mix(glm::vec3(0.55f, 0.05f, 0.06f), glm::vec3(0.75f, 0.35f, 0.35f), rnd(0, 0.5f)),
                               m_bloodColor, 0.35f);   // stained with the game's blood colour
            p.wet   = true;
            p.size  = glm::vec3(rnd(0.12f, 0.25f), rnd(0.08f, 0.2f), rnd(0.1f, 0.22f));
            p.bleed = rnd(1.0f, 2.5f);
            if (rnd(0, 1) < 0.15f) {   // a bit of bone
                p.gib = 1;
                p.color = glm::vec3(0.86f, 0.82f, 0.72f);
                p.wet = false;
                p.size = glm::vec3(rnd(0.05f, 0.08f), rnd(0.18f, 0.32f), rnd(0.05f, 0.08f));
                p.bleed = 0.0f;
            }
        } else {
            p.kind   = Particle::Bolt;
            p.color  = glm::vec3(rnd(0.55f, 0.75f));
            p.size   = glm::vec3(0.08f, rnd(0.18f, 0.3f), 0.08f);
            p.glossy = true;
        }
        p.pos  = pos + rndDir() * 0.15f;
        p.vel  = vel + rndDir() * rnd(2.0f, 6.0f) + glm::vec3(0, rnd(2.0f, 5.0f), 0);
        p.spin = rndDir() * rnd(200.0f, 700.0f);
        p.life = p.maxLife = rnd(12.0f, 20.0f);
        p.gibSeed = rnd(0, 1);
        add(p);
    }
}

void ParticleSystem::burst(GoreKind kind, const SceneNode* part, const glm::vec3& vel, float violence) {
    if (!part) return;
    const glm::mat4 M = part->worldMatrix();
    const glm::vec3 c(M[3]);
    const glm::vec3 scale = part->transform.scale;
    const float volume = scale.x * scale.y * scale.z;
    const int pieces = std::clamp((int)(volume * 3.0f * (0.6f + violence)), 4, 18);
    const float speed = 4.0f + violence * 10.0f;
    for (int i = 0; i < pieces; ++i) {
        Particle p;
        glm::vec3 local(rnd(-0.4f, 0.4f), rnd(-0.4f, 0.4f), rnd(-0.4f, 0.4f));
        p.pos  = glm::vec3(M * glm::vec4(local, 1.0f));
        glm::vec3 out = p.pos - c;
        out = glm::length(out) > 1e-3f ? glm::normalize(out) : rndDir();
        p.vel  = vel + (out + rndDir() * 0.5f) * speed * rnd(0.4f, 1.0f) + glm::vec3(0, rnd(1.0f, 4.0f), 0);
        p.spin = rndDir() * rnd(300.0f, 900.0f);
        p.rot  = glm::vec3(rnd(0, 360), rnd(0, 360), rnd(0, 360));
        p.life = p.maxLife = rnd(14.0f, 22.0f);
        p.gibSeed = rnd(0, 1);
        float s = std::clamp(std::cbrt(volume / (float)pieces) * rnd(0.6f, 1.0f), 0.15f, 0.7f);
        p.size = glm::vec3(s * rnd(0.8f, 1.3f), s * rnd(0.6f, 1.0f), s * rnd(0.8f, 1.2f));
        if (kind == GoreKind::Blood) {
            p.kind  = Particle::Chunk;
            p.color = glm::mix(glm::vec3(0.6f, 0.08f, 0.08f), m_bloodColor, 0.4f) * rnd(0.85f, 1.1f);
            p.skin  = part->color;   // the outside of the body part
            p.wet   = true;
            p.bleed = rnd(2.0f, 4.0f);
        } else {
            p.kind   = Particle::Bolt;
            p.color  = part->color;
            p.glossy = true;
            p.size  *= 0.8f;
        }
        add(p);
    }
    // Bits of bone (or bolts and scraps), and the blood.
    gibs(kind, c, vel * 0.5f, 2 + (int)(violence * 3.0f));
    spray(kind, c, glm::vec3(0, 1, 0), 30 + (int)(violence * 40.0f), 4.0f + violence * 6.0f);
    if (kind == GoreKind::Oil) sparks(c, 20);
}

void ParticleSystem::stepGib(Particle& p, float dt, float gravity, const Physics& physics, std::vector<Particle>& born) {
    // (Moved and spun by the caller already.)
    const float r = std::max(0.05f, (p.size.x + p.size.y + p.size.z) / 6.0f);
    const bool meat = p.kind == Particle::Chunk && p.gib == 0;
    // Fresh meat drips a trail of blood as it flies and slides.
    p.bleed -= dt;
    if (meat && p.bleed > 0.0f) {
        p.trail -= dt;
        if (p.trail <= 0.0f) {
            p.trail = rnd(0.05f, 0.12f);
            Particle d;
            d.kind  = Particle::Drop;
            d.pos   = p.pos + rndDir() * r * 0.5f;
            d.vel   = p.vel * 0.6f + rndDir() * 0.4f;
            d.color = glm::mix(p.color, m_bloodColor, 0.7f);
            d.size  = glm::vec3(rnd(0.035f, 0.06f));
            d.life  = d.maxLife = 2.5f;
            d.wet   = true;
            born.push_back(d);
        }
    }
    glm::vec3 at = p.pos, n;
    if (!physics.resolveSphere(at, r, &n)) return;
    p.pos = at;
    const float vn = glm::dot(p.vel, n);
    if (vn < 0.0f) {
        const float bounce = meat ? 0.15f : (p.kind == Particle::Bolt ? 0.45f : 0.3f);
        p.vel -= n * vn * (1.0f + bounce);
        // A hard landing: meat goes splat (and leaves a mark).
        if (meat && vn < -4.0f) {
            Particle d;
            d.kind  = Particle::Drop;
            d.pos   = p.pos;
            d.vel   = -n * 2.0f + rndDir() * 0.5f;
            d.color = glm::mix(p.color, m_bloodColor, 0.8f);
            d.size  = glm::vec3(std::clamp(r * 0.7f, 0.05f, 0.14f));
            d.life  = d.maxLife = 1.0f;
            d.wet   = true;
            born.push_back(d);
            if (m_splatSound <= 0.0f) {
                Audio::play("splat", std::clamp(-vn * 0.05f, 0.1f, 0.5f), 0.7f + 0.3f * rnd(0, 1), false, &p.pos);
                m_splatSound = 0.07f;
            }
        } else if (!meat && vn < -3.0f && m_splatSound <= 0.0f) {
            Audio::play("hit", std::clamp(-vn * 0.03f, 0.05f, 0.3f), 1.2f + 0.5f * rnd(0, 1), false, &p.pos);
            m_splatSound = 0.05f;
        }
    }
    // Sliding along: friction (meat drags, bone and metal skid), and it rolls the way it slides.
    glm::vec3 vt = p.vel - n * glm::dot(p.vel, n);
    const float grip = meat ? 6.0f : 3.0f;
    vt *= std::max(0.0f, 1.0f - grip * dt);
    p.vel = vt + n * std::max(0.0f, glm::dot(p.vel, n));
    float slide = glm::length(vt);
    if (slide > 0.05f) p.spin = glm::degrees(glm::cross(n, vt) / r) * (meat ? 0.3f : 0.8f);
    else p.spin *= std::max(0.0f, 1.0f - 8.0f * dt);
    // Settled on the ground: lie still.
    if (n.y > 0.6f && glm::length(p.vel) < 0.35f) { p.vel = glm::vec3(0.0f); p.spin = glm::vec3(0.0f); }
    (void)gravity;
}

void ParticleSystem::sparks(const glm::vec3& pos, int count) {
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.kind  = Particle::Spark;
        p.pos   = pos;
        p.vel   = rndDir() * rnd(3.0f, 9.0f) + glm::vec3(0, 2, 0);
        p.color = glm::mix(glm::vec3(1.0f, 0.55f, 0.1f), glm::vec3(1.0f, 0.9f, 0.4f), rnd(0, 1));
        p.size  = glm::vec3(0.04f);
        p.life  = p.maxLife = rnd(0.3f, 0.8f);
        add(p);
    }
}

// --- Realistic blood ----------------------------------------------------------------

namespace {
// The flat side a splat lies on: `x0` and `z0` run along it when it isn't turned.
void surfaceAxes(const glm::vec3& n, glm::vec3& x0, glm::vec3& z0) {
    x0 = std::abs(n.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(glm::vec3(0, 1, 0), n));
    z0 = glm::cross(x0, n);
}
// How far to turn a splat (degrees about its surface) so its long side runs along `v`.
float turnAlong(const glm::vec3& n, const glm::vec3& v) {
    glm::vec3 x0, z0;
    surfaceAxes(n, x0, z0);
    return glm::degrees(std::atan2(glm::dot(v, x0), glm::dot(v, z0)));
}
} // namespace

void ParticleSystem::spurt(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, const glm::vec3& inherit, float strength) {
    strength = std::clamp(strength, 0.0f, 1.5f);
    int count = (int)std::round((5 + strength * 9) * m_bloodAmount);
    glm::vec3 d = glm::length(dir) > 1e-4f ? glm::normalize(dir) : glm::vec3(0, 1, 0);
    for (int i = 0; i < count; ++i) {
        Particle p;
        p.kind  = Particle::Drop;
        // One after another along the jet, so it reads as a stream, not a puff.
        float along = (float)i / std::max(1, count - 1);
        float speed = (1.2f + strength * 5.5f) * (0.75f + 0.35f * along) * rnd(0.92f, 1.08f);
        p.pos   = pos + d * (0.02f + along * 0.05f) + rndDir() * 0.015f;
        p.vel   = glm::normalize(d + rndDir() * (0.06f + 0.1f * (1.0f - strength))) * speed + inherit;
        p.color = kind == GoreKind::Blood ? m_bloodColor * rnd(0.85f, 1.05f) : kOil;
        p.size  = glm::vec3(rnd(0.035f, 0.06f) * (0.8f + 0.4f * strength));
        p.life  = p.maxLife = rnd(2.0f, 3.0f);
        p.wet   = true;
        add(p);
    }
}

void ParticleSystem::ooze(GoreKind kind, const glm::vec3& pos, const glm::vec3& inherit) {
    if (rnd(0, 1) > m_bloodAmount) return;
    Particle p;
    p.kind  = Particle::Drop;
    p.pos   = pos + rndDir() * 0.03f;
    p.vel   = inherit * 0.9f + rndDir() * 0.15f + glm::vec3(0, -0.2f, 0);
    p.color = kind == GoreKind::Blood ? m_bloodColor * rnd(0.8f, 1.0f) : kOil;
    p.size  = glm::vec3(rnd(0.05f, 0.085f));
    p.life  = p.maxLife = rnd(2.0f, 3.0f);
    p.wet   = true;
    add(p);
}

void ParticleSystem::smear(GoreKind kind, const glm::vec3& pos, const glm::vec3& vel, const glm::vec3& normal, float width, float wet) {
    if (wet <= 0.02f) return;
    glm::vec3 n = glm::length(normal) > 1e-4f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    glm::vec3 along = vel - n * glm::dot(vel, n);
    if (glm::length(along) < 1e-3f) return;
    Particle p;
    p.kind   = Particle::Splat;
    p.look   = 1;
    p.normal = n;
    p.pos    = pos + n * 0.012f;
    p.rot    = glm::vec3(0, turnAlong(n, along), 0);
    p.size   = glm::vec3(width * rnd(0.85f, 1.1f), 0.02f, width * rnd(1.6f, 2.2f));
    p.color  = kind == GoreKind::Blood ? m_bloodColor * rnd(0.75f, 0.95f) : kOil;
    p.alpha  = std::clamp(0.35f + wet * 0.65f, 0.0f, 1.0f);
    p.seed   = rnd(0, 1);
    p.wet    = true;
    p.life   = p.maxLife = m_bloodStay * rnd(0.8f, 1.2f);
    add(p);
}

float ParticleSystem::wetBloodAt(const glm::vec3& pos, float radius, float take) {
    const float fresh = dryTime() * 0.6f;   // blood older than this has gone tacky: it doesn't stick to you
    float best = 0.0f;
    for (Particle& q : m_items) {
        if (q.kind != Particle::Splat || q.onPart || q.normal.y < 0.9f || q.look == 2) continue;
        if (q.age > fresh || q.life < 1.0f) continue;
        if (std::abs(q.pos.y - pos.y) > 0.35f) continue;
        glm::vec2 d(q.pos.x - pos.x, q.pos.z - pos.z);
        float reach = std::max(q.size.x, q.size.z) * 0.5f + radius;
        if (glm::dot(d, d) > reach * reach) continue;
        float amount = (1.0f - q.age / fresh) * std::clamp(q.size.x / 0.6f, 0.15f, 1.0f) * q.alpha;
        best = std::max(best, amount);
        if (take > 0.0f && q.look == 0) {   // a little of the pool goes with you
            q.size.x *= 1.0f - take * 0.3f;
            q.size.z *= 1.0f - take * 0.3f;
            q.spread *= 1.0f - take * 0.3f;
        }
    }
    return best;
}

void ParticleSystem::footprint(GoreKind kind, const glm::vec3& pos, const glm::vec3& dir, float wet) {
    if (wet <= 0.04f) return;
    glm::vec3 d(dir.x, 0.0f, dir.z);
    if (glm::length(d) < 1e-4f) return;
    Particle p;
    p.kind   = Particle::Splat;
    p.look   = 2;
    p.normal = glm::vec3(0, 1, 0);
    p.pos    = pos + glm::vec3(0, 0.012f, 0);
    p.rot    = glm::vec3(0, turnAlong(p.normal, d) + rnd(-6.0f, 6.0f), 0);
    p.size   = glm::vec3(0.36f, 0.02f, 0.78f);
    p.color  = kind == GoreKind::Blood ? m_bloodColor * rnd(0.7f, 0.85f) : kOil;
    p.alpha  = std::clamp(wet * 1.1f, 0.0f, 0.95f);
    p.seed   = rnd(0, 1);
    p.wet    = true;
    p.life   = p.maxLife = m_bloodStay * rnd(0.8f, 1.2f);
    add(p);
}

void ParticleSystem::walk(BloodyFeet& feet, GoreKind kind, const glm::vec3& at, bool onGround) {
    if (!feet.started) { feet.last = at; feet.started = true; return; }
    glm::vec3 step = at - feet.last;
    step.y = 0.0f;
    float moved = glm::length(step);
    if (moved > 6.0f) { feet.last = at; return; }   // teleported
    feet.last = at;
    if (!onGround) return;
    if (m_splats > 0) {
        float soak = wetBloodAt(at, 0.4f, moved > 0.01f ? 0.02f : 0.0f);
        if (soak > 0.15f) feet.wet = std::max(feet.wet, std::min(1.0f, soak * 1.3f));
    }
    if (feet.wet <= 0.04f || moved < 1e-4f) return;
    feet.walked += moved;
    if (feet.walked < 1.05f) return;
    feet.walked = 0.0f;
    glm::vec3 fwd = step / moved;
    glm::vec3 side = glm::cross(fwd, glm::vec3(0, 1, 0)) * (feet.left ? -0.24f : 0.24f);
    footprint(kind, at + side, fwd, feet.wet);
    feet.left = !feet.left;
    feet.wet *= 0.84f;          // each step leaves less on the floor
    if (feet.wet < 0.05f) feet.wet = 0.0f;
}

void ParticleSystem::stain(GoreKind kind, const SceneNode* part, const glm::vec3& at, float size) {
    if (!part) return;
    glm::mat4 M = part->worldMatrix();
    glm::vec3 l = glm::vec3(glm::inverse(M) * glm::vec4(at, 1.0f));
    // Onto the side of the part nearest the point.
    int axis = 0;
    for (int i = 1; i < 3; ++i) if (std::abs(l[i]) > std::abs(l[axis])) axis = i;
    float side = l[axis] >= 0.0f ? 1.0f : -1.0f;
    glm::vec3 lp = glm::clamp(l, glm::vec3(-0.38f), glm::vec3(0.38f));
    lp[axis] = 0.5f * side;
    glm::vec3 ln(0.0f);
    ln[axis] = side;
    glm::vec3 lt(0.0f);
    lt[(axis + 1) % 3] = 1.0f;
    // A stain already there just gets bigger.
    for (Particle& q : m_items) {
        if (q.onPart != part->id || glm::dot(q.localNormal, ln) < 0.9f) continue;
        if (glm::length(q.localPos - lp) < 0.25f) { q.spread = std::min(1.1f, q.spread + size * 0.5f); q.age *= 0.3f; return; }
    }
    Particle p;
    p.kind   = Particle::Splat;
    p.look   = 0;
    p.onPart = part->id;
    p.localPos = lp;
    p.localNormal = ln;
    p.localTangent = lt;
    p.size   = glm::vec3(size * 0.35f, 0.02f, size * 0.35f * rnd(0.8f, 1.3f));
    p.spread = size;
    p.rot    = glm::vec3(0, rnd(0, 360), 0);
    p.color  = kind == GoreKind::Blood ? m_bloodColor * rnd(0.7f, 0.9f) : kOil;
    p.seed   = rnd(0, 1);
    p.wet    = true;
    p.life   = p.maxLife = 1e6f;   // stays until the character respawns (wipe)
    add(p);
}

void ParticleSystem::follow(Scene& scene) {
    for (Particle& p : m_items) {
        if (!p.onPart) continue;
        SceneNode* n = scene.findById(p.onPart);
        if (!n) { p.life = 0.0f; continue; }
        glm::mat4 M = n->worldMatrix();
        glm::mat3 R(M);
        glm::mat3 N = glm::transpose(glm::inverse(R));
        p.normal  = glm::normalize(N * p.localNormal);
        p.tangent = glm::normalize(R * p.localTangent);
        p.pos     = glm::vec3(M * glm::vec4(p.localPos, 1.0f)) + p.normal * 0.012f;
    }
}

void ParticleSystem::wipe(Scene& scene, const SceneNode* root) {
    if (!root) return;
    for (Particle& p : m_items) {
        if (!p.onPart) continue;
        SceneNode* n = scene.findById(p.onPart);
        if (!n || root == n || root->isAncestorOf(n)) p.life = 0.0f;
    }
    auto end = std::remove_if(m_items.begin(), m_items.end(), [](const Particle& p) { return p.life <= 0.0f; });
    m_items.erase(end, m_items.end());
}

void ParticleSystem::explosion(const glm::vec3& pos, float radius) {
    int n = (int)std::clamp(radius * 14.0f, 20.0f, 120.0f);
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.kind  = Particle::Fire;
        p.pos   = pos + rndDir() * radius * 0.2f;
        p.vel   = rndDir() * radius * rnd(1.5f, 4.0f) + glm::vec3(0, rnd(1, 4), 0);
        p.color = glm::mix(glm::vec3(1.0f, 0.35f, 0.05f), glm::vec3(1.0f, 0.85f, 0.3f), rnd(0, 1));
        p.size  = glm::vec3(rnd(0.12f, 0.3f) * std::sqrt(radius));
        p.spin  = rndDir() * 200.0f;
        p.life  = p.maxLife = rnd(0.25f, 0.55f);
        add(p);
    }
    for (int i = 0; i < n / 2; ++i) {
        Particle p;
        p.kind  = Particle::Smoke;
        p.pos   = pos + rndDir() * radius * 0.3f;
        p.vel   = rndDir() * radius * rnd(0.5f, 1.5f) + glm::vec3(0, rnd(2, 5), 0);
        p.color = glm::vec3(rnd(0.12f, 0.25f));
        p.size  = glm::vec3(rnd(0.2f, 0.45f) * std::sqrt(radius));
        p.spin  = rndDir() * 60.0f;
        p.life  = p.maxLife = rnd(1.5f, 3.0f);
        add(p);
    }
    sparks(pos, n / 2);
}

void ParticleSystem::update(float dt, float gravity, const Physics& physics) {
    m_splatSound -= dt;
    std::vector<Particle> born;   // made during the loop (added after, so the list doesn't move under us)
    for (size_t i = 0; i < m_items.size(); ++i) {
        Particle& p = m_items[i];
        p.life -= dt;
        if (p.kind == Particle::Splat) {
            p.age += dt;
            // Liquid spreads out into a pool over a second or two (thick blood, so it slows down)...
            if (p.spread > p.size.x) {
                float ratio = p.size.z / std::max(p.size.x, 1e-4f);
                p.size.x += (p.spread - p.size.x) * std::min(1.0f, dt * (p.onPart ? 0.8f : 1.4f));
                p.size.z = p.size.x * ratio;
            }
            if (p.onPart) continue;   // a stain on a body part: follow() moves it
            // A fresh pool that reaches the edge of what it's on runs over it and drips off.
            if (p.look == 0 && p.normal.y > 0.9f && p.drips > 0.0f && p.age < dryTime() * 0.4f) {
                p.dripTimer -= dt;
                if (p.dripTimer <= 0.0f) {
                    p.dripTimer = rnd(0.08f, 0.2f);
                    float a = rnd(0.0f, 6.2831853f);
                    glm::vec3 out(std::cos(a), 0.0f, std::sin(a));
                    glm::vec3 rim = p.pos + out * (std::max(p.size.x, p.size.z) * 0.48f);
                    if (!physics.solidAt(rim - glm::vec3(0, 0.08f, 0)) && !physics.solidAt(rim + glm::vec3(0, 0.03f, 0))) {
                        Particle d;
                        d.kind  = Particle::Drop;
                        d.pos   = rim - glm::vec3(0, 0.05f, 0);
                        d.vel   = out * rnd(0.1f, 0.4f);
                        d.color = p.color;
                        d.size  = glm::vec3(rnd(0.045f, 0.075f));
                        d.life  = d.maxLife = 3.0f;
                        d.wet   = true;
                        born.push_back(d);
                        p.drips -= 1.0f;
                        // The pool can't keep spreading out over nothing: it pulls back from that edge.
                        p.pos -= out * 0.015f;
                        p.spread = std::min(p.spread, std::max(p.size.x, p.size.z));
                    }
                }
            }
            // ...and on a wall it runs down, leaving a trail, thinning as it goes.
            if (p.slide > 0.0f) {
                p.slide -= dt;
                float d = 0.35f * dt * std::min(1.0f, p.slide * 1.5f);
                p.pos.y -= d;
                p.trail += d;
                p.size.x *= 1.0f - 0.12f * dt;
                if (p.trail > 0.2f) {
                    p.trail = 0.0f;
                    Particle mark = p;
                    mark.slide = 0.0f;
                    mark.spread = 0.0f;
                    mark.size.x *= 0.55f;
                    mark.size.z *= 0.8f;
                    mark.seed = rnd(0, 1);
                    mark.drips = 0.0f;
                    born.push_back(mark);
                }
                bool offWall = !physics.solidAt(p.pos - p.normal * 0.06f);
                bool onFloor = physics.solidAt(p.pos + glm::vec3(0, -0.04f, 0) + p.normal * 0.03f);
                if (offWall || onFloor) {
                    p.slide = 0.0f;   // ran off the wall, or reached the floor
                    // The bead at the bottom of the run drops off (and lands as a little pool).
                    Particle d;
                    d.kind  = Particle::Drop;
                    d.pos   = p.pos + p.normal * 0.06f;
                    d.vel   = p.normal * 0.2f;
                    d.color = p.color;
                    d.size  = glm::vec3(std::clamp(p.size.x * 0.5f, 0.04f, 0.08f));
                    d.life  = d.maxLife = 3.0f;
                    d.wet   = true;
                    d.drips = -1.0f;   // (a bead: if it hits the wall again it stays put, it doesn't run again)
                    if (p.drips >= 0.0f) born.push_back(d);
                }
            }
            continue;
        }

        switch (p.kind) {
            case Particle::Fire:
                p.vel *= std::max(0.0f, 1.0f - 4.0f * dt);
                p.size *= 1.0f + 0.8f * dt;
                break;
            case Particle::Smoke:
                p.vel *= std::max(0.0f, 1.0f - 1.5f * dt);
                p.vel.y += 1.5f * dt;
                p.size *= 1.0f + 0.35f * dt;
                break;
            default:
                p.vel.y -= gravity * dt;
                break;
        }
        p.pos += p.vel * dt;
        p.rot += p.spin * dt;
        if (p.kind == Particle::Spray) { if (physics.solidAt(p.pos)) p.life = 0.0f; continue; }   // lands: gone
        if (p.kind == Particle::Fire || p.kind == Particle::Smoke || p.kind == Particle::Spark) continue;
        if (p.kind == Particle::Chunk || p.kind == Particle::Bolt) { stepGib(p, dt, gravity, physics, born); if (p.pos.y < -200.0f) p.life = 0.0f; continue; }

        // Hitting something solid.
        glm::vec3 h = p.size * 0.5f;
        AABB box{p.pos - h, p.pos + h};
        float push = physics.pushUp(box);
        bool hitWall = physics.solidAt(p.pos);
        if (push > 0.0f || hitWall) {
            if (p.kind == Particle::Drop) {
                if (push > 0.0f) {
                    // Splash onto the floor. Landing in a pool makes the pool bigger instead.
                    glm::vec3 at(p.pos.x, p.pos.y + push - h.y + 0.012f, p.pos.z);
                    float r = p.size.x * rnd(4.0f, 6.0f) * (0.8f + 0.02f * glm::length(p.vel));
                    Particle* pool = nullptr;
                    for (size_t k = 0; k < m_items.size() && !pool; ++k) {
                        Particle& q = m_items[k];
                        if (k == i || q.kind != Particle::Splat || q.slide > 0.0f || q.normal.y < 0.9f || q.onPart || q.look != 0) continue;
                        if (std::abs(q.pos.y - at.y) > 0.08f || q.life < 1.0f) continue;
                        glm::vec2 d(q.pos.x - at.x, q.pos.z - at.z);
                        float reach = std::max(q.size.x, q.spread) * 0.5f + r * 0.5f;
                        if (glm::dot(d, d) < reach * reach) pool = &q;
                    }
                    glm::vec3 hv(p.vel.x, 0.0f, p.vel.z);
                    float hs = glm::length(hv);
                    if (pool) {
                        float a = std::max(pool->spread, pool->size.x);
                        pool->spread = std::min(2.6f, std::sqrt(a * a + r * r * 0.5f));
                        pool->color = glm::mix(pool->color, p.color, 0.15f);
                        pool->life = std::max(pool->life, m_bloodStay * 0.8f);
                        pool->age *= 0.6f;          // fresh blood on top: wet again
                        pool->drips += 0.6f;
                        p.life = 0.0f;
                    } else {
                        // A drop coming in at an angle lands as a teardrop, stretched the way it was going,
                        // and a fast one throws little droplets on ahead.
                        float stretch = 1.0f + std::min(1.6f, hs * 0.14f);
                        p.pos = at + (hs > 0.5f ? hv / hs * r * 0.12f * (stretch - 1.0f) : glm::vec3(0.0f));
                        float s = r * 0.35f;
                        p.size = glm::vec3(s, 0.02f, s * rnd(0.8f, 1.2f) * stretch);
                        p.spread = r;
                        p.rot = glm::vec3(0, hs > 0.5f ? glm::degrees(std::atan2(hv.x, hv.z)) : rnd(0, 360), 0);
                        p.normal = glm::vec3(0, 1, 0);
                        p.seed = rnd(0, 1);
                        p.drips = 2.0f + r * 6.0f;
                        p.kind = Particle::Splat;
                        int extra = hs > 4.0f ? (int)rnd(1.0f, std::min(5.0f, hs * 0.5f)) : 0;
                        for (int e = 0; e < extra; ++e) {
                            glm::vec3 dir = hv / hs;
                            glm::vec3 side(-dir.z, 0.0f, dir.x);
                            Particle sat = p;
                            sat.pos = at + dir * r * rnd(0.5f, 1.5f) + side * r * rnd(-0.5f, 0.5f);
                            float ss = rnd(0.025f, 0.06f);
                            sat.size = glm::vec3(ss, 0.02f, ss * rnd(1.0f, 1.8f));
                            sat.spread = 0.0f;
                            sat.drips = 0.0f;
                            sat.seed = rnd(0, 1);
                            sat.vel = sat.spin = glm::vec3(0.0f);
                            sat.life = sat.maxLife = m_bloodStay * rnd(0.8f, 1.2f);
                            born.push_back(sat);
                        }
                    }
                } else {
                    // Splat on a wall: it sticks, then runs down.
                    glm::vec3 v = p.vel;
                    v.y = 0.0f;
                    glm::vec3 n = glm::length(v) > 1e-3f ? -glm::normalize(v) : glm::vec3(0, 0, 1);
                    // Which way the wall really faces (it may be turned); else snap to the nearest side.
                    glm::vec3 probe = p.pos - p.vel * dt, wn;
                    if (physics.resolveSphere(probe, 0.12f, &wn) && std::abs(wn.y) < 0.7f) n = glm::normalize(glm::vec3(wn.x, 0.0f, wn.z));
                    else n = std::abs(n.x) > std::abs(n.z) ? glm::vec3(n.x > 0 ? 1.0f : -1.0f, 0, 0) : glm::vec3(0, 0, n.z > 0 ? 1.0f : -1.0f);
                    p.normal = n;
                    p.pos += n * 0.02f;
                    for (int step = 0; step < 6 && physics.solidAt(p.pos); ++step) p.pos += n * 0.03f;   // out onto the surface
                    float s = p.size.x * rnd(2.0f, 3.0f);
                    p.size = glm::vec3(s, 0.02f, s * rnd(0.8f, 1.4f));
                    p.rot = glm::vec3(0, rnd(-15.0f, 15.0f), 0);   // (along the wall: the run goes straight down)
                    p.seed = rnd(0, 1);
                    p.slide = p.drips < 0.0f ? 0.0f : rnd(0.6f, 2.2f);
                    p.kind = Particle::Splat;
                }
                if (p.kind == Particle::Splat) {
                    p.vel = p.spin = glm::vec3(0.0f);
                    p.life = p.maxLife = m_bloodStay * rnd(0.8f, 1.2f);
                    ++m_splats;
                }
                if (m_splatSound <= 0.0f) {    // a few squelches, not hundreds
                    Audio::play("splat", 0.25f, 0.8f + 0.4f * (rnd(0, 1)), false, &p.pos);
                    m_splatSound = 0.07f;
                }
            } else {
                // Chunks and bolts bounce, then settle.
                if (push > 0.0f) p.pos.y += push;
                p.vel.y = std::abs(p.vel.y) * 0.3f;
                p.vel.x *= 0.6f; p.vel.z *= 0.6f;
                p.spin *= 0.6f;
                if (glm::length(p.vel) < 0.4f) { p.vel = glm::vec3(0.0f); p.spin = glm::vec3(0.0f); }
            }
        }
        if (p.pos.y < -200.0f) p.life = 0.0f;
    }
    for (Particle& b : born) add(b);

    // Too many puddles: fade the oldest.
    // (Only count the ones not already fading, or it would fade them all, a batch each frame.)
    int fading = 0;
    for (auto& p : m_items) if (p.kind == Particle::Splat && p.life <= 1.0f) ++fading;
    if (m_splats - fading > kMaxSplats) {
        int over = m_splats - fading - kMaxSplats;
        for (auto& p : m_items)
            if (over > 0 && p.kind == Particle::Splat && !p.onPart && p.life > 1.0f) { p.life = 1.0f; --over; }
    }

    m_splats = 0;
    auto end = std::remove_if(m_items.begin(), m_items.end(), [](const Particle& p) { return p.life <= 0.0f; });
    m_items.erase(end, m_items.end());
    for (auto& p : m_items) if (p.kind == Particle::Splat) ++m_splats;
}

// --- ParticleEmitter, Fire, Smoke, Sparkles -----------------------------------------

namespace {
// The emitter's place and turn, and where particles come out.
struct EmitFrame {
    glm::mat4 world{1.0f};      // with the part's size: a point in -0.5..0.5 is inside the part
    glm::mat3 turn{1.0f};       // just the turn
    glm::vec3 origin{0.0f};
    bool      volume = false;   // in a part (out of anywhere in it), else from a point (an Attachment)
    bool      live = false;     // in a part or an Attachment (anywhere else, nothing comes out)
};
glm::vec3 faceAxis(Face f) {
    switch (f) {
        case Face::Right:  return {1, 0, 0};
        case Face::Top:    return {0, 1, 0};
        case Face::Back:   return {0, 0, 1};
        case Face::Left:   return {-1, 0, 0};
        case Face::Bottom: return {0, -1, 0};
        default:           return {0, 0, -1};   // Front
    }
}
float inRange(glm::vec2 r) { return r.x + (r.y - r.x) * rnd(0.0f, 1.0f); }
int lookOf(const std::string& tex) {
    for (int i = 0; i < kParticleLookCount; ++i) if (tex == kParticleLooks[i]) return i;
    return -1;
}
} // namespace

std::string particleTexture(const std::string& texture) {
    std::string low = texture;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    if (low.find("rbxasset") == std::string::npos && low.find("roblox.com") == std::string::npos) return texture;
    if (low.find("fire") != std::string::npos || low.find("flame") != std::string::npos) return "fire";
    if (low.find("smoke") != std::string::npos || low.find("cloud") != std::string::npos) return "smoke";
    if (low.find("sparkle") != std::string::npos || low.find("star") != std::string::npos) return "sparkle";
    return "";
}

int ParticleSystem::spriteCount(uint64_t owner) const {
    int n = 0;
    for (const Sprite& s : m_sprites) n += s.owner == owner;
    return n;
}

void ParticleSystem::runEmitters(Scene& scene, float dt) {
    dt = std::clamp(dt, 0.0f, 0.1f);
    // Every emitter, and where it is this frame.
    std::unordered_map<uint64_t, std::pair<SceneNode*, EmitFrame>> emitters;
    scene.forEach([&](SceneNode* n) {
        if (!n->isParticles()) return;
        EmitFrame f;
        const SceneNode* in = n->parent;
        f.volume = in && in->isPart();
        f.live = f.volume || (in && in->isAttachment());
        f.world = n->worldMatrix();
        for (int c = 0; c < 3; ++c) {
            const float l = glm::length(glm::vec3(f.world[c]));
            f.turn[c] = l > 1e-6f ? glm::vec3(f.world[c]) / l : glm::vec3(c == 0, c == 1, c == 2);
        }
        f.origin = glm::vec3(f.world[3]);
        emitters[n->id] = {n, f};
    });
    if (emitters.empty() && m_sprites.empty()) { m_spawnDebt.clear(); return; }

    // :Clear(): its particles go.
    for (auto& [id, e] : emitters) {
        ParticleProps& p = e.first->particle;
        if (!p.pendingClear) continue;
        const uint64_t owner = id;
        m_sprites.erase(std::remove_if(m_sprites.begin(), m_sprites.end(), [owner](const Sprite& s) { return s.owner == owner; }),
                        m_sprites.end());
        p.pendingClear = false;
    }
    for (auto it = m_spawnDebt.begin(); it != m_spawnDebt.end();)
        it = emitters.count(it->first) ? std::next(it) : m_spawnDebt.erase(it);

    std::unordered_map<uint64_t, int> counts;
    for (const Sprite& s : m_sprites) ++counts[s.owner];

    // New particles: Rate (while Enabled) and Emit(n).
    for (auto& [id, e] : emitters) {
        SceneNode* n = e.first;
        ParticleProps& p = n->particle;
        const EmitFrame& f = e.second;
        const int burst = std::clamp(p.pendingEmit, 0, kMaxSpritesPerEmitter);
        p.pendingEmit = 0;
        if (!f.live) { m_spawnDebt.erase(id); continue; }
        float rate = 0.0f;
        switch (p.type) {
            case ParticleType::ParticleEmitter: rate = p.rate; break;
            case ParticleType::Fire:            rate = std::clamp(18.0f + p.fxSize * 4.0f, 20.0f, 80.0f); break;
            case ParticleType::Smoke:           rate = std::clamp(6.0f + p.fxSize * 1.5f, 6.0f, 30.0f); break;
            case ParticleType::Sparkles:        rate = 22.0f; break;
        }
        int count = burst;
        if (n->enabled && rate > 0.0f) {
            float& debt = m_spawnDebt[id];
            debt = std::min(debt + std::min(rate, 100000.0f) * dt, 400.0f);   // (at most a few hundred a frame)
            count += (int)debt;
            debt -= (float)(int)debt;
        }
        int& have = counts[id];
        count = std::min(count, kMaxSpritesPerEmitter - have);
        for (int i = 0; i < count; ++i) {
            if ((int)m_sprites.size() >= kMaxSprites) break;   // full: no more until some have faded
            Sprite s;
            s.owner = id;
            s.seed = rnd(0.0f, 1.0f);
            const glm::vec3 inside = f.volume ? glm::vec3(f.world * glm::vec4(rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f), rnd(-0.5f, 0.5f), 1.0f))
                                              : f.origin;
            switch (p.type) {
                case ParticleType::ParticleEmitter: {
                    s.pos = inside;
                    glm::vec3 dir = faceAxis(p.direction);
                    // SpreadAngle: turned up to X degrees one way and up to Y the other.
                    glm::vec3 u = std::abs(dir.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                    const glm::vec3 v = glm::cross(dir, u);
                    u = glm::cross(v, dir);
                    const float ax = glm::radians(rnd(-1.0f, 1.0f) * p.spread.x), ay = glm::radians(rnd(-1.0f, 1.0f) * p.spread.y);
                    dir = dir * std::cos(ax) + v * std::sin(ax);
                    dir = dir * std::cos(ay) + u * std::sin(ay);
                    s.vel = f.turn * glm::normalize(dir) * inRange(p.speed);
                    s.life = std::max(0.01f, inRange(p.lifetime));
                    s.rot = inRange(p.rotation);
                    s.spin = inRange(p.rotSpeed);
                    break;
                }
                case ParticleType::Fire: {
                    const float r = p.fxSize * 0.18f;
                    s.pos = f.origin + glm::vec3(rnd(-r, r), rnd(-0.2f, 0.1f) * p.fxSize, rnd(-r, r));
                    s.vel = glm::vec3(rnd(-0.4f, 0.4f), p.rise * rnd(0.45f, 0.75f), rnd(-0.4f, 0.4f));
                    s.life = rnd(0.55f, 0.95f);
                    s.scale = rnd(0.75f, 1.15f);
                    s.rot = rnd(-25.0f, 25.0f);
                    s.spin = rnd(-40.0f, 40.0f);
                    break;
                }
                case ParticleType::Smoke: {
                    const float r = p.fxSize * 0.2f;
                    s.pos = f.origin + glm::vec3(rnd(-r, r), 0.0f, rnd(-r, r));
                    s.vel = glm::vec3(rnd(-0.3f, 0.3f), p.rise * rnd(0.8f, 1.2f), rnd(-0.3f, 0.3f));
                    s.life = rnd(3.0f, 4.5f);
                    s.scale = rnd(0.8f, 1.2f);
                    s.rot = rnd(0.0f, 360.0f);
                    s.spin = rnd(-30.0f, 30.0f);
                    break;
                }
                case ParticleType::Sparkles: {
                    s.pos = inside;
                    s.vel = rndDir() * rnd(0.8f, 2.5f) + glm::vec3(0.0f, 0.8f, 0.0f);
                    s.life = rnd(0.8f, 1.5f);
                    s.scale = rnd(0.7f, 1.2f);
                    s.rot = rnd(0.0f, 90.0f);
                    s.spin = rnd(-180.0f, 180.0f);
                    break;
                }
            }
            if (p.type == ParticleType::ParticleEmitter && p.locked) s.local = glm::transpose(f.turn) * (s.pos - f.origin);
            m_sprites.push_back(s);
            ++have;
        }
    }

    // Move them, and work out how each looks now.
    m_spriteTex.clear();
    std::unordered_map<std::string, int> texIndex;
    for (Sprite& s : m_sprites) {
        auto it = emitters.find(s.owner);
        s.age += dt;
        if (it == emitters.end() || s.age >= s.life) { s.life = -1.0f; continue; }   // done, or its emitter is gone
        const ParticleProps& p = it->second.first->particle;
        const EmitFrame& f = it->second.second;
        const float t = std::clamp(s.age / s.life, 0.0f, 1.0f);
        s.rot += s.spin * dt;
        s.tex = -1;
        switch (p.type) {
            case ParticleType::ParticleEmitter: {
                s.vel += p.acceleration * dt;
                if (p.drag != 0.0f) s.vel *= std::pow(2.0f, -p.drag * dt);   // loses half its speed every 1/Drag seconds
                if (p.locked) {
                    s.local += glm::transpose(f.turn) * s.vel * dt;
                    s.pos = f.origin + f.turn * s.local;
                } else {
                    s.pos += s.vel * dt;
                }
                s.color = sampleSequence(p.color, t);
                s.alpha = 1.0f - std::clamp(sampleSequence(p.transparency, t), 0.0f, 1.0f);
                s.size  = std::max(0.0f, sampleSequence(p.size, t));
                s.glow  = p.lightEmission;
                s.look  = lookOf(p.texture);
                if (s.look < 0) {
                    s.look = 0;
                    auto [ti, added] = texIndex.try_emplace(p.texture, (int)m_spriteTex.size());
                    if (added) m_spriteTex.push_back(p.texture);
                    s.tex = ti->second;
                }
                break;
            }
            case ParticleType::Fire:
                s.vel.x *= 1.0f - std::min(1.0f, 2.0f * dt);
                s.vel.z *= 1.0f - std::min(1.0f, 2.0f * dt);
                s.pos += s.vel * dt;
                s.color = glm::mix(p.color1, p.color2, std::min(1.0f, t * 1.3f));
                s.alpha = std::min(1.0f, t * 8.0f) * std::pow(1.0f - t, 1.3f) * 0.8f;
                s.size  = p.fxSize * 0.45f * s.scale * (1.0f - 0.6f * t);
                s.glow  = 1.0f;
                s.look  = 2;
                break;
            case ParticleType::Smoke:
                s.vel.x *= 1.0f - std::min(1.0f, 0.8f * dt);
                s.vel.z *= 1.0f - std::min(1.0f, 0.8f * dt);
                s.pos += s.vel * dt;
                s.color = p.color1;
                s.alpha = std::clamp(p.opacity, 0.0f, 1.0f) * std::min(1.0f, t * 5.0f) * (1.0f - t);
                s.size  = p.fxSize * s.scale * (1.0f + 1.8f * t);
                s.glow  = 0.0f;
                s.look  = 1;
                break;
            case ParticleType::Sparkles:
                s.vel *= 1.0f - std::min(1.0f, 1.2f * dt);
                s.pos += s.vel * dt;
                s.color = glm::mix(p.color1, glm::vec3(1.0f), 0.25f * s.seed);
                s.alpha = std::sin(t * 3.14159f);
                s.size  = 0.55f * s.scale * (0.75f + 0.25f * std::sin(s.age * 18.0f + s.seed * 6.0f));
                s.glow  = 1.0f;
                s.look  = 3;
                break;
        }
    }
    m_sprites.erase(std::remove_if(m_sprites.begin(), m_sprites.end(), [](const Sprite& s) { return s.life < 0.0f; }),
                    m_sprites.end());
}
