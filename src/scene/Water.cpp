#include "Water.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"
#include "Player.h"
#include "Gerstner.h"
#include "../core/Audio.h"

#include <algorithm>
#include <cstring>
#include <cmath>

namespace {

constexpr float kWaveSpeed = 6.0f;    // how fast ripples spread (units / second)
constexpr float kCalm      = 0.55f;   // how quickly waves die down
constexpr int   kMaxPoints = 96;      // grid points along the longest side

float rnd01() { static uint32_t s = 0x9e3779b9u; s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / 16777216.0f; }

const Attribute* attr(const SceneNode* n, const char* name, Attribute::Type t) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == t ? a : nullptr;
}

} // namespace

bool WaterSystem::tilted(const SceneNode* water) {
    const glm::vec3 up = glm::normalize(glm::vec3(water->worldMatrix()[1]));
    return up.y < 0.999f;
}

float WaterSystem::tiltedSurface(const SceneNode* water, float x, float z) {
    const glm::mat4 m = water->worldMatrix();
    const glm::vec3 p0 = glm::vec3(m * glm::vec4(0.0f, 0.5f, 0.0f, 1.0f));   // middle of the top face
    const glm::vec3 n = glm::normalize(glm::transpose(glm::inverse(glm::mat3(m))) * glm::vec3(0.0f, 1.0f, 0.0f));
    if (std::abs(n.y) < 0.05f) return p0.y;   // on its side: no sensible "top"
    return p0.y - (n.x * (x - p0.x) + n.z * (z - p0.z)) / n.y;
}

bool WaterSystem::insideTilted(const SceneNode* water, const glm::vec3& p) {
    const glm::vec3 l = glm::vec3(glm::inverse(water->worldMatrix()) * glm::vec4(p, 1.0f));
    return std::abs(l.x) <= 0.5f && std::abs(l.y) <= 0.5f && std::abs(l.z) <= 0.5f;
}

void WaterSystem::begin(Scene& scene) {
    end();
    m_active = true;
    scan(scene);
    scanSources(scene);
    m_liquid.begin(scene);
}

// Find the water parts. Also picks up water scripts make, move (rising tides!) or remove.
void WaterSystem::scan(Scene& scene) {
    std::vector<Body> keep;
    scene.forEach([&](SceneNode* n) {
        if (!Player::isWater(n) || n->internal || tilted(n)) return;   // slides aren't level: no wave grid
        for (const SceneNode* p = n; p; p = p->parent) if (!p->visible) return;
        AABB box = Physics::worldBounds(n);
        glm::vec3 size = box.max - box.min;
        if (size.x < 0.2f || size.z < 0.2f) return;
        Body b;
        if (const Body* old = find(n->id)) b = *old;
        b.id = n->id;
        glm::vec3 oldSize = b.max - b.min;
        b.min = box.min;
        b.max = box.max;
        if (b.h.empty() || glm::length(oldSize - size) > 1e-3f) {   // new, or resized: a fresh, calm grid
            b.cell = std::max(0.3f, std::max(size.x, size.z) / (kMaxPoints - 1));
            b.nx = std::max(2, (int)std::ceil(size.x / b.cell) + 1);
            b.nz = std::max(2, (int)std::ceil(size.z / b.cell) + 1);
            b.h.assign((size_t)b.nx * b.nz, 0.0f);
            b.v.assign(b.h.size(), 0.0f);
        }
        b.swell = 0.0f;
        b.flow = glm::vec3(0.0f);
        if (const Attribute* a = attr(n, "WaveScale", Attribute::Number)) b.swell = std::max(0.0f, (float)a->n) * 0.5f;
        if (const Attribute* a = attr(n, "Waves", Attribute::Number)) b.swell = std::max(0.0f, (float)a->n);
        b.clarity = std::clamp(0.2f + n->transparency, 0.0f, 1.0f);
        if (const Attribute* a = attr(n, "Clarity", Attribute::Number)) b.clarity = std::clamp((float)a->n, 0.0f, 1.0f);
        if (const Attribute* a = attr(n, "Flow", Attribute::Vector3)) b.flow = a->v;
        b.drag = 1.0f;
        if (const Attribute* a = attr(n, "Drag", Attribute::Number)) b.drag = std::clamp((float)a->n, 0.0f, 20.0f);
        b.color = n->color;
        buildFlowMap(b, scene);
        b.transparency = n->transparency;
        keep.push_back(std::move(b));
    });
    m_bodies.swap(keep);
}

void WaterSystem::end() {
    m_bodies.clear();
    m_surges.clear();
    m_liquid.end();
    m_flood = Flood{};
    m_wetSpots.clear();
    m_sources.clear();
    m_lastWet.clear();
    m_charPrev.clear();
    m_charWet.clear();
    m_active = false;
    m_time = 0.0f;
}

const WaterSystem::Body* WaterSystem::find(uint64_t id) const {
    for (const Body& b : m_bodies) if (b.id == id) return &b;
    return nullptr;
}

const WaterSystem::Body* WaterSystem::bodyAt(const glm::vec3& p, float pad) const {
    for (const Body& b : m_bodies)
        if (p.x >= b.min.x - pad && p.x <= b.max.x + pad && p.z >= b.min.z - pad && p.z <= b.max.z + pad &&
            p.y >= b.min.y - 0.01f && p.y <= b.max.y + b.swell + 2.5f + surgeHeight())
            return &b;
    return nullptr;
}

WaterSystem::Body* WaterSystem::bodyAt(const glm::vec3& p, float pad) {
    return const_cast<Body*>(static_cast<const WaterSystem*>(this)->bodyAt(p, pad));
}

float WaterSystem::heightAt(const Body& b, float x, float z) const {
    float fx = std::clamp((x - b.min.x) / b.cell, 0.0f, (float)(b.nx - 1));
    float fz = std::clamp((z - b.min.z) / b.cell, 0.0f, (float)(b.nz - 1));
    int x0 = std::min((int)fx, b.nx - 2), z0 = std::min((int)fz, b.nz - 2);
    float tx = fx - x0, tz = fz - z0;
    auto H = [&](int i, int k) { return b.h[(size_t)k * b.nx + i]; };
    const float grid = (H(x0, z0) * (1 - tx) + H(x0 + 1, z0) * tx) * (1 - tz) + (H(x0, z0 + 1) * (1 - tx) + H(x0 + 1, z0 + 1) * tx) * tz;
    return m_surges.empty() ? grid : grid + surgeAt(b, x, z);
}

// The flow map: start with the Flow everywhere, then make it go around solid things
// (the pressure solve every fluid simulator uses: water can't pile up or vanish, so
// it has to go around, and squeezes faster through the gaps).
void WaterSystem::buildFlowMap(Body& b, Scene& scene) {
    const glm::vec2 base(b.flow.x, b.flow.z);
    if (glm::length(base) < 1e-3f) {
        if (!b.flowMap.empty()) { b.flowMap.clear(); b.flowKey = 0; ++b.flowVersion; }
        return;
    }
    // Solid things poking through the surface.
    std::vector<AABB> solids;
    uint64_t key = 1469598103934665603ull;
    auto mix = [&](float v) { uint32_t u; std::memcpy(&u, &v, 4); key = (key ^ u) * 1099511628211ull; };
    mix(base.x); mix(base.y); mix((float)b.nx); mix((float)b.nz); mix(b.min.x); mix(b.min.z);
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || !n->canCollide || !n->anchored || Player::isWater(n) || scene.isCharacterPart(n)) return;
        const AABB a = Physics::worldBounds(n);
        if (a.max.x < b.min.x || a.min.x > b.max.x || a.max.z < b.min.z || a.min.z > b.max.z) return;
        if (a.max.y < b.max.y - 0.3f || a.min.y > b.max.y + 0.3f) return;   // under the surface, or above it
        solids.push_back(a);
        mix(a.min.x); mix(a.min.z); mix(a.max.x); mix(a.max.z);
    });
    if (key == b.flowKey && !b.flowMap.empty()) return;
    b.flowKey = key;
    ++b.flowVersion;
    const int nx = b.nx, nz = b.nz;
    const size_t n = (size_t)nx * nz;
    std::vector<uint8_t> solid(n, 0);
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
            const float x = b.min.x + i * b.cell, z = b.min.z + k * b.cell;
            for (const AABB& a : solids)
                if (x >= a.min.x && x <= a.max.x && z >= a.min.z && z <= a.max.z) { solid[(size_t)k * nx + i] = 1; break; }
        }
    std::vector<glm::vec2> u(n);
    for (size_t id = 0; id < n; ++id) u[id] = solid[id] ? glm::vec2(0.0f) : base;
    auto vel = [&](int i, int k) {   // outside the grid: the current comes in / goes out as it is
        if (i < 0 || k < 0 || i >= nx || k >= nz) return base;
        return u[(size_t)k * nx + i];
    };
    std::vector<float> div(n, 0.0f), p(n, 0.0f), q(n, 0.0f);
    const float h = b.cell;
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i)
            if (!solid[(size_t)k * nx + i])
                div[(size_t)k * nx + i] = (vel(i + 1, k).x - vel(i - 1, k).x + vel(i, k + 1).y - vel(i, k - 1).y) / (2.0f * h);
    // Pressure: open edges (the river carries on past the part), solid walls push back.
    for (int it = 0; it < 80; ++it) {
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i) {
                const size_t id = (size_t)k * nx + i;
                if (solid[id]) { q[id] = 0.0f; continue; }
                float sum = 0.0f;
                int count = 0;
                const int ni[4] = {i + 1, i - 1, i, i}, nk[4] = {k, k, k + 1, k - 1};
                for (int d = 0; d < 4; ++d) {
                    if (ni[d] < 0 || nk[d] < 0 || ni[d] >= nx || nk[d] >= nz) { ++count; continue; }   // open edge: p = 0
                    const size_t j = (size_t)nk[d] * nx + ni[d];
                    if (solid[j]) continue;                                                              // wall
                    sum += p[j];
                    ++count;
                }
                q[id] = count ? (sum - h * h * div[id]) / count : 0.0f;
            }
        p.swap(q);
    }
    auto P = [&](int i, int k, float self) {
        if (i < 0 || k < 0 || i >= nx || k >= nz) return 0.0f;
        const size_t j = (size_t)k * nx + i;
        return solid[j] ? self : p[j];
    };
    b.flowMap.assign(n, glm::vec2(0.0f));
    const float cap = glm::length(base) * 3.0f;
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
            const size_t id = (size_t)k * nx + i;
            if (solid[id]) continue;
            const float self = p[id];
            glm::vec2 v = u[id] - glm::vec2(P(i + 1, k, self) - P(i - 1, k, self), P(i, k + 1, self) - P(i, k - 1, self)) / (2.0f * h);
            const float s = glm::length(v);
            if (s > cap) v *= cap / s;
            b.flowMap[id] = v;
        }
}

glm::vec3 WaterSystem::flowAt(const Body& b, float x, float z) const {
    if (b.flowMap.empty()) return b.flow;
    float fx = std::clamp((x - b.min.x) / b.cell, 0.0f, (float)(b.nx - 1));
    float fz = std::clamp((z - b.min.z) / b.cell, 0.0f, (float)(b.nz - 1));
    int x0 = std::min((int)fx, b.nx - 2), z0 = std::min((int)fz, b.nz - 2);
    float tx = fx - x0, tz = fz - z0;
    auto F = [&](int i, int k) { return b.flowMap[(size_t)k * b.nx + i]; };
    const glm::vec2 v = (F(x0, z0) * (1 - tx) + F(x0 + 1, z0) * tx) * (1 - tz) + (F(x0, z0 + 1) * (1 - tx) + F(x0 + 1, z0 + 1) * tx) * tz;
    return glm::vec3(v.x, b.flow.y, v.y);
}

void WaterSystem::addWetSpot(const glm::vec3& p, float radius) {
    if (!m_active) return;
    for (WetSpot& s : m_wetSpots)   // soaking a patch that's already wet: freshen it up
        if (glm::length(s.pos - p) < s.radius * 0.5f) { s.born = m_time; s.radius = std::max(s.radius, radius); return; }
    if (m_wetSpots.size() >= 64) m_wetSpots.erase(m_wetSpots.begin());
    m_wetSpots.push_back({p, radius, m_time});
}

float WaterSystem::wetness(const WetSpot& s) const {
    const float age = m_time - s.born;
    return 1.0f - std::clamp((age - 12.0f) / 25.0f, 0.0f, 1.0f);   // soaked for a while, dry after ~40 s
}

float WaterSystem::churn(const Body& b, float x, float z) const {
    const int i = std::clamp((int)std::lround((x - b.min.x) / b.cell), 0, b.nx - 1);
    const int k = std::clamp((int)std::lround((z - b.min.z) / b.cell), 0, b.nz - 1);
    return std::abs(b.v[(size_t)k * b.nx + i]);
}

float WaterSystem::swellAt(const Body& b, float x, float z) const {
    if (b.swell <= 0.0f) return 0.0f;
    // Gerstner waves (the same ones the water is drawn with).
    return Gerstner::heightAt(x, z, m_time, b.swell, edgeFade(b, x, z));
}

float WaterSystem::surfaceOf(const SceneNode* water, float x, float z) const {
    const Body* b = water ? find(water->id) : nullptr;
    if (!b) return water ? Physics::worldBounds(water).max.y : 0.0f;
    return b->max.y + heightAt(*b, x, z) + swellAt(*b, x, z);
}

float WaterSystem::dragAt(const glm::vec3& p) const {
    const Body* b = bodyAt(p);
    return b ? b->drag : 1.0f;
}

bool WaterSystem::at(const glm::vec3& p, float* surface, glm::vec3* flow) const {
    const Body* b = bodyAt(p);
    if (!b) return floodAt(p, surface, flow);
    float s = b->max.y + heightAt(*b, p.x, p.z) + swellAt(*b, p.x, p.z);
    if (p.y > s) return floodAt(p, surface, flow);
    if (surface) *surface = s;
    if (flow) *flow = flowAt(*b, p.x, p.z);
    return true;
}

// --- Tsunamis ---------------------------------------------------------------------

void WaterSystem::addSurge(const glm::vec3& at, float height, float speed, float width) {
    const Body* b = bodyAt(at, width);
    if (!b) return;
    Surge s;
    s.body = b->id;
    s.center = at;
    s.height = std::clamp(height, 0.2f, 40.0f);
    s.speed = std::clamp(speed, 4.0f, 200.0f);
    s.width = std::clamp(width, 1.0f, 60.0f);
    s.start = s.width * 1.5f;
    s.radius = s.start;
    // It runs until it's past the far side of the water (and a bit up the shore).
    const glm::vec2 c(at.x, at.z);
    float far = 0.0f;
    for (glm::vec2 corner : {glm::vec2(b->min.x, b->min.z), glm::vec2(b->max.x, b->min.z), glm::vec2(b->min.x, b->max.z), glm::vec2(b->max.x, b->max.z)})
        far = std::max(far, glm::length(corner - c));
    s.maxRadius = far + s.width * 4.0f + s.height * 8.0f;
    m_surges.push_back(std::move(s));
}

float WaterSystem::surgeHeight() const {
    float h = 0.0f;
    for (const Surge& s : m_surges) h = std::max(h, s.height);
    return h;
}

// The wave's shape: a tall crest with a trough just behind it, getting lower as it
// spreads out (its energy is shared around a bigger and bigger ring).
float WaterSystem::surgeAt(const Body& b, float x, float z) const {
    float h = 0.0f;
    for (const Surge& s : m_surges) {
        if (s.body != b.id) continue;
        const float d = glm::length(glm::vec2(x - s.center.x, z - s.center.z));
        const float u = (d - s.radius) / s.width;
        if (u > 3.0f || u < -5.0f) continue;
        const float amp = s.height * std::sqrt(s.start / std::max(s.radius, s.start));
        h += amp * (std::exp(-u * u) - 0.4f * std::exp(-(u + 1.8f) * (u + 1.8f)));
    }
    return h * edgeFade(b, x, z);
}

void WaterSystem::stepSurges(float dt, Scene& scene) {
    for (size_t i = 0; i < m_surges.size();) {
        Surge& s = m_surges[i];
        s.radius += s.speed * dt;
        const Body* b = find(s.body);
        if (!b || s.radius > s.maxRadius) { m_surges.erase(m_surges.begin() + (long)i); continue; }
        const float amp = s.height * std::sqrt(s.start / std::max(s.radius, s.start));
        // Is `p` in the wave's path: in the water, or on the shore it washes up onto?
        const float runUp = amp * 6.0f + s.width;   // how far past the water's edge it reaches
        auto inPath = [&](const glm::vec3& p, glm::vec3& out, float& k) {
            if (p.x < b->min.x - runUp || p.x > b->max.x + runUp || p.z < b->min.z - runUp || p.z > b->max.z + runUp) return false;
            if (p.y > b->max.y + amp * 1.6f + 1.0f || p.y < b->min.y - 1.0f) return false;
            const glm::vec2 d(p.x - s.center.x, p.z - s.center.z);
            const float dist = glm::length(d);
            const float u = (dist - s.radius) / s.width;
            if (u > 1.2f || u < -1.5f) return false;
            out = dist > 1e-3f ? glm::vec3(d.x / dist, 0.0f, d.y / dist) : glm::vec3(1, 0, 0);
            k = std::exp(-u * u);
            return true;
        };
        // People get picked up and carried along (once each).
        if (Player* pl = scene.player(); pl && pl->root() && !pl->isDead()) {
            glm::vec3 dir; float k;
            if (inPath(pl->position(), dir, k) && std::find(s.swept.begin(), s.swept.end(), pl->rootId()) == s.swept.end() && amp > 0.4f) {
                s.swept.push_back(pl->rootId());
                pl->launch(dir * s.speed * 0.55f * k + glm::vec3(0.0f, std::min(30.0f, amp * 5.0f) * k, 0.0f));
            }
        }
        // Loose parts (boats, crates, debris) ride the front.
        s.pushTimer -= dt;
        if (s.pushTimer <= 0.0f) {
            s.pushTimer = 0.05f;
            std::vector<SceneNode*> stack{scene.root()};
            while (!stack.empty()) {
                SceneNode* n = stack.back();
                stack.pop_back();
                if (n->isViewportFrame()) continue;   // (its parts are only a picture)
                for (auto& c : n->children) stack.push_back(c.get());
                if (!n->isPart() || n->anchored || scene.isCharacterPart(n)) continue;
                glm::vec3 dir; float k;
                if (!inPath(glm::vec3(n->worldMatrix()[3]), dir, k)) continue;
                const float want = s.speed * 0.6f * k;
                const float along = glm::dot(n->velocity, dir);
                if (along < want) n->velocity += dir * std::min(want - along, want * 0.5f) + glm::vec3(0.0f, amp * 1.5f * k * 0.05f, 0.0f);
                Physics::wake(n);
            }
        }
        // White water thrown off the crest.
        if (amp > 0.8f) {
            const int n = (int)std::clamp(amp * 2.0f, 1.0f, 8.0f);
            for (int j = 0; j < n; ++j) {
                const float a = rnd01() * 6.2831853f;
                const glm::vec3 p(s.center.x + std::cos(a) * s.radius, 0.0f, s.center.z + std::sin(a) * s.radius);
                if (p.x < b->min.x || p.x > b->max.x || p.z < b->min.z || p.z > b->max.z) continue;
                const glm::vec3 top(p.x, b->max.y + heightAt(*b, p.x, p.z) + 0.2f, p.z);
                m_liquid.spray(top, glm::vec3(std::cos(a), 0.0f, std::sin(a)) * s.speed * 0.25f + glm::vec3(0.0f, amp * 1.5f, 0.0f));
            }
        }
        ++i;
    }
}

void WaterSystem::disturb(const glm::vec3& p, float amount, float radius) {
    Body* b = bodyAt(p, radius);
    if (!b) return;
    radius = std::max(radius, b->cell * 1.5f);
    int i0 = std::max(0, (int)std::floor((p.x - radius - b->min.x) / b->cell));
    int i1 = std::min(b->nx - 1, (int)std::ceil((p.x + radius - b->min.x) / b->cell));
    int k0 = std::max(0, (int)std::floor((p.z - radius - b->min.z) / b->cell));
    int k1 = std::min(b->nz - 1, (int)std::ceil((p.z + radius - b->min.z) / b->cell));
    for (int k = k0; k <= k1; ++k)
        for (int i = i0; i <= i1; ++i) {
            float dx = b->min.x + i * b->cell - p.x, dz = b->min.z + k * b->cell - p.z;
            float q = 1.0f - (dx * dx + dz * dz) / (radius * radius);
            if (q <= 0.0f) continue;
            float& h = b->h[(size_t)k * b->nx + i];
            h = std::clamp(h - amount * q * q, -2.0f, 2.0f);
        }
}

void WaterSystem::splash(Scene& scene, const glm::vec3& p, float speed, float size) {
    const Body* b = bodyAt(p, 0.5f);
    if (!b && !floodAt(p - glm::vec3(0.0f, 0.05f, 0.0f), nullptr, nullptr)) return;
    size = std::clamp(size, 0.2f, 4.0f);
    if (b) disturb(p, std::min(0.6f, speed * 0.02f * size), 0.5f + size * 0.7f);
    addWetSpot(p, std::clamp(1.5f + size * 1.5f + speed * 0.08f, 1.5f, 9.0f));   // the pool's edge and anything nearby get splashed
    int count = (int)std::clamp(speed * size * 4.0f, 8.0f, 90.0f);
    scene.particles().waterSpray(p, count, std::clamp(speed * 0.45f, 2.0f, 10.0f), b ? b->color : m_flood.color, size * 0.6f);
    // A big splash also throws real liquid up (it falls back and soaks in).
    const int drops = (int)std::clamp((speed - 6.0f) * size * 1.5f, 0.0f, 40.0f);
    for (int i = 0; i < drops; ++i) {
        const float a = rnd01() * 6.2831853f, r = rnd01();
        const glm::vec3 out(std::cos(a) * r, 0.0f, std::sin(a) * r);
        m_liquid.spray(p + out * size * 0.6f + glm::vec3(0.0f, 0.2f, 0.0f),
                       out * std::clamp(speed * 0.12f, 1.0f, 5.0f) +
                       glm::vec3(0.0f, std::clamp(speed * 0.3f, 4.0f, 14.0f) * (0.6f + 0.4f * rnd01()), 0.0f));
    }
    if (m_splashSound <= 0.0f) {
        Audio::play("splash", std::clamp(speed * size / 12.0f, 0.15f, 1.0f), std::clamp(1.3f - size * 0.15f, 0.7f, 1.3f), false, &p);
        m_splashSound = 0.08f;
    }
}

void WaterSystem::touching(Scene& scene, uint64_t id, const glm::vec3& p, float downSpeed, float size) {
    // Only the first touch splashes (it counts as "still in the water" for a moment after).
    auto it = m_lastWet.find(id);
    const bool wasWet = it != m_lastWet.end() && m_time - it->second < 0.4f;
    m_lastWet[id] = m_time;
    if (wasWet || downSpeed < 2.5f) return;
    if (const Body* b = bodyAt(p)) {
        splash(scene, glm::vec3(p.x, b->max.y + heightAt(*b, p.x, p.z), p.z), downSpeed, size);
        return;
    }
    // Flowing water: find its surface under the thing.
    float s;
    for (float drop : {0.0f, 1.0f, 2.0f, 3.0f})
        if (floodAt(p - glm::vec3(0.0f, drop, 0.0f), &s, nullptr)) { splash(scene, glm::vec3(p.x, s, p.z), downSpeed, size); return; }
}

void WaterSystem::update(float dt, Scene& scene) {
    if (!m_active) return;
    m_time += dt;
    m_splashSound -= dt;
    m_wetSpots.erase(std::remove_if(m_wetSpots.begin(), m_wetSpots.end(),
                                    [&](const WetSpot& s) { return wetness(s) <= 0.0f; }), m_wetSpots.end());
    if ((m_scanTime -= dt) <= 0.0f) { m_scanTime = 0.25f; scan(scene); scanSources(scene); }
    stepFlood(dt, scene);
    stepSurges(dt, scene);
    m_liquid.update(dt, scene);

    // People (the player and NPCs) splash when they jump in, and leave a wake.
    std::vector<std::pair<uint64_t, glm::vec3>> people;
    if (Player* pl = scene.player(); pl && pl->root() && !pl->isDead()) people.push_back({pl->rootId(), pl->position()});
    for (const auto& n : scene.npcs().all())
        if (!n->dead)
            if (SceneNode* r = scene.findById(n->rootId)) people.push_back({n->rootId, r->transform.position});
    for (auto& [id, feet] : people) {
        auto prev = m_charPrev.find(id);
        glm::vec3 vel = prev != m_charPrev.end() && dt > 0.0f ? (feet - prev->second) / dt : glm::vec3(0.0f);
        m_charPrev[id] = feet;
        float surface;
        bool wet = at(feet + glm::vec3(0.0f, 0.05f, 0.0f), &surface);
        bool& was = m_charWet[id];
        if (wet && !was && vel.y < -3.0f) splash(scene, glm::vec3(feet.x, surface, feet.z), -vel.y, 1.2f);
        was = wet;
        // Only a body crossing the surface makes waves (not one deep underwater).
        if (wet && surface - feet.y < 3.0f) {
            float speed = glm::length(glm::vec2(vel.x, vel.z));
            if (speed > 0.5f) disturb(glm::vec3(feet.x, surface, feet.z), std::min(0.05f, speed * dt * 0.25f), 0.9f);
        }
    }

    // The waves themselves.
    for (Body& b : m_bodies) {
        if (m_viewDist > 0.0f) {   // out past the render distance: the waves wait
            const glm::vec3 nearest = glm::clamp(m_viewer, b.min, b.max);
            if (glm::length(nearest - m_viewer) > m_viewDist) continue;
        }
        const int steps = std::max(1, (int)std::ceil(dt * kWaveSpeed / (0.5f * b.cell)));
        const float hs = dt / steps;
        const float c2 = kWaveSpeed * kWaveSpeed / (b.cell * b.cell);
        const int nx = b.nx, nz = b.nz;
        for (int s = 0; s < steps; ++s) {
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i) {
                    size_t id = (size_t)k * nx + i;
                    // Sides of the pool reflect waves back (a missing neighbour counts as this point).
                    float c = b.h[id];
                    float l = i > 0 ? b.h[id - 1] : c, r = i < nx - 1 ? b.h[id + 1] : c;
                    float d = k > 0 ? b.h[id - nx] : c, u = k < nz - 1 ? b.h[id + nx] : c;
                    b.v[id] += c2 * (l + r + d + u - 4.0f * c) * hs;
                    b.v[id] *= std::max(0.0f, 1.0f - kCalm * hs);
                }
            for (size_t id = 0; id < b.h.size(); ++id)
                b.h[id] = std::clamp(b.h[id] + b.v[id] * hs, -2.0f, 2.0f) * (1.0f - 0.02f * hs);
        }
        // White water: where the surface shoots up fast (a wave slapping a wall, the
        // middle of a splash) it throws real liquid drops into the air.
        int thrown = 0;
        for (int k = 1; k + 1 < b.nz && thrown < 12; k += 2)
            for (int i = 1; i + 1 < b.nx && thrown < 12; i += 2) {
                const size_t id = (size_t)k * b.nx + i;
                const float rise = b.v[id];
                if (rise < 1.4f || b.h[id] < 0.12f || rnd01() > 0.25f) continue;
                const glm::vec3 p(b.min.x + i * b.cell, b.max.y + b.h[id], b.min.z + k * b.cell);
                const glm::vec3 side((rnd01() - 0.5f) * 2.0f, 0.0f, (rnd01() - 0.5f) * 2.0f);
                m_liquid.spray(p, side + glm::vec3(0.0f, std::min(rise * 2.2f, 9.0f), 0.0f));
                ++thrown;
            }
    }

    // Forget things that left the water long ago.
    for (auto i = m_lastWet.begin(); i != m_lastWet.end();)
        i = m_time - i->second > 5.0f ? m_lastWet.erase(i) : std::next(i);
}
