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
        if (it == m_items.end()) it = m_items.begin();
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
        } else {
            p.kind   = Particle::Bolt;
            p.color  = glm::vec3(rnd(0.55f, 0.75f));
            p.size   = glm::vec3(0.08f, rnd(0.18f, 0.3f), 0.08f);
            p.glossy = true;
        }
        p.pos  = pos + rndDir() * 0.15f;
        p.vel  = vel + rndDir() * rnd(2.0f, 6.0f) + glm::vec3(0, rnd(2.0f, 5.0f), 0);
        p.spin = rndDir() * rnd(200.0f, 700.0f);
        p.life = p.maxLife = rnd(8.0f, 14.0f);
        add(p);
    }
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
            // Liquid spreads out into a pool over a second or two...
            if (p.spread > p.size.x) {
                float ratio = p.size.z / std::max(p.size.x, 1e-4f);
                p.size.x += (p.spread - p.size.x) * std::min(1.0f, dt * 1.8f);
                p.size.z = p.size.x * ratio;
            }
            // ...and on a wall it runs down, leaving a trail, thinning as it goes.
            if (p.slide > 0.0f) {
                p.slide -= dt;
                float d = 0.35f * dt * std::min(1.0f, p.slide * 1.5f);
                p.pos.y -= d;
                p.trail += d;
                p.size.x *= 1.0f - 0.12f * dt;
                if (p.trail > 0.14f) {
                    p.trail = 0.0f;
                    Particle mark = p;
                    mark.slide = 0.0f;
                    mark.spread = 0.0f;
                    mark.size.x *= 0.55f;
                    mark.size.z *= 0.8f;
                    born.push_back(mark);
                }
                if (!physics.solidAt(p.pos - p.normal * 0.06f) || physics.solidAt(p.pos + glm::vec3(0, -0.04f, 0) + p.normal * 0.03f))
                    p.slide = 0.0f;   // ran off the wall, or reached the floor
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
                        if (k == i || q.kind != Particle::Splat || q.slide > 0.0f || q.normal.y < 0.9f) continue;
                        if (std::abs(q.pos.y - at.y) > 0.08f || q.life < 1.0f) continue;
                        glm::vec2 d(q.pos.x - at.x, q.pos.z - at.z);
                        float reach = std::max(q.size.x, q.spread) * 0.5f + r * 0.5f;
                        if (glm::dot(d, d) < reach * reach) pool = &q;
                    }
                    if (pool) {
                        float a = std::max(pool->spread, pool->size.x);
                        pool->spread = std::min(2.6f, std::sqrt(a * a + r * r * 0.5f));
                        pool->color = glm::mix(pool->color, p.color, 0.15f);
                        pool->life = std::max(pool->life, m_bloodStay * 0.8f);
                        p.life = 0.0f;
                    } else {
                        p.pos = at;
                        float s = r * 0.35f;
                        p.size = glm::vec3(s, 0.02f, s * rnd(0.7f, 1.3f));
                        p.spread = r;
                        p.rot = glm::vec3(0, rnd(0, 360), 0);
                        p.normal = glm::vec3(0, 1, 0);
                        p.kind = Particle::Splat;
                    }
                } else {
                    // Splat on a wall: it sticks, then runs down.
                    glm::vec3 v = p.vel;
                    v.y = 0.0f;
                    glm::vec3 n = glm::length(v) > 1e-3f ? -glm::normalize(v) : glm::vec3(0, 0, 1);
                    // Snap to the wall's facing (walls are mostly straight up and down).
                    n = std::abs(n.x) > std::abs(n.z) ? glm::vec3(n.x > 0 ? 1.0f : -1.0f, 0, 0) : glm::vec3(0, 0, n.z > 0 ? 1.0f : -1.0f);
                    p.normal = n;
                    p.pos += n * 0.02f;
                    for (int step = 0; step < 6 && physics.solidAt(p.pos); ++step) p.pos += n * 0.03f;   // out onto the surface
                    float s = p.size.x * rnd(2.0f, 3.0f);
                    p.size = glm::vec3(s, 0.02f, s * rnd(0.8f, 1.4f));
                    p.rot = std::abs(n.x) > 0.5f ? glm::vec3(0, 0, 90) : glm::vec3(90, 0, 0);
                    p.slide = rnd(0.6f, 2.2f);
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
            if (over > 0 && p.kind == Particle::Splat && p.life > 1.0f) { p.life = 1.0f; --over; }
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
