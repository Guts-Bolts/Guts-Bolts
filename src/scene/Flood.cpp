// Flowing water: WaterSource parts pour water that runs downhill, spreads out,
// fills holes, piles up behind walls and pours over edges.
//
// How it works: the area around the sources is cut into a grid of little
// squares. Each square knows how high its floor is (the top of the parts under
// it) and how deep the water on it is. Every step, a square whose water surface
// is higher than a neighbour's pushes water into it, a bit faster each step,
// like water speeding up through a pipe. That one rule gives spreading,
// sloshing, filling up and overflowing. Water that runs off the edge of the
// grid, or off into nothing, is gone.
#include "Water.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kNoGround  = -1e4f;    // nothing below: water falls away
constexpr float kStep      = 1.0f / 120.0f;
constexpr float kMinDepth  = 0.03f;    // thinner than this counts as dry
constexpr float kDefaultRate = 8.0f;   // units³ of water a source pours per second
constexpr float kDefaultArea = 80.0f;  // how big the flooding area is

bool flagged(const SceneNode* n, const char* what) {
    if (std::find(n->tags.begin(), n->tags.end(), what) != n->tags.end()) return true;
    const Attribute* a = n->findAttribute(what);
    return a && a->type == Attribute::Bool && a->b;
}

float numberAttr(const SceneNode* n, const char* name, float fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Number ? (float)a->n : fallback;
}

bool isSource(const SceneNode* n) {
    return n->isPart() && (n->name == "WaterSource" || flagged(n, "WaterSource"));
}

} // namespace

void WaterSystem::scanSources(Scene& scene) {
    m_sources.clear();
    const SceneNode* first = nullptr;
    scene.forEach([&](SceneNode* n) {
        if (!isSource(n) || n->internal) return;
        for (const SceneNode* p = n; p; p = p->parent) if (!p->visible) return;
        AABB b = Physics::worldBounds(n);
        m_sources.push_back({n->id, (b.min + b.max) * 0.5f, std::max(0.25f, std::max(b.max.x - b.min.x, b.max.z - b.min.z) * 0.5f),
                             std::max(0.0f, numberAttr(n, "Rate", kDefaultRate))});
        if (!first) first = n;
    });
    if (m_sources.empty() || m_flood.n) return;

    // The first time there's a source: lay out the grid around the sources.
    glm::vec3 mid(0.0f);
    float top = -1e9f;
    for (const Source& s : m_sources) { mid += s.pos; top = std::max(top, s.pos.y); }
    mid /= (float)m_sources.size();
    const float area = std::clamp(numberAttr(first, "FloodSize", kDefaultArea), 8.0f, 200.0f);
    Flood& f = m_flood;
    f.cell = area > 100.0f ? 1.0f : 0.5f;
    f.n = (int)std::ceil(area / f.cell);
    f.origin = glm::vec2(mid.x, mid.z) - glm::vec2(f.n * f.cell * 0.5f);
    f.ceiling = top + 1.0f;
    const size_t cells = (size_t)f.n * f.n;
    f.ground.assign(cells, kNoGround);
    f.depth.assign(cells, 0.0f);
    for (auto& o : f.out) o.assign(cells, 0.0f);
    f.vel.assign(cells, glm::vec2(0.0f));
    f.color = first->color;
    f.transparency = std::clamp(numberAttr(first, "WaterTransparency", 0.3f), 0.0f, 0.95f);
    f.groundTimer = 0.0f;
    buildGround(scene);
}

// The floor under each square: the top of the highest solid, anchored part
// there (parts entirely above the sources are a roof, not a floor).
void WaterSystem::buildGround(Scene& scene) {
    Flood& f = m_flood;
    std::fill(f.ground.begin(), f.ground.end(), kNoGround);
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->internal || !n->canCollide || !n->anchored || !n->visible) return;
        if (scene.isCharacterPart(n)) return;
        AABB b = Physics::worldBounds(n);
        if (b.min.y > f.ceiling) return;
        int i0 = std::max(0, (int)std::ceil((b.min.x - f.origin.x) / f.cell - 0.5f));
        int i1 = std::min(f.n - 1, (int)std::floor((b.max.x - f.origin.x) / f.cell - 0.5f));
        int k0 = std::max(0, (int)std::ceil((b.min.z - f.origin.y) / f.cell - 0.5f));
        int k1 = std::min(f.n - 1, (int)std::floor((b.max.z - f.origin.y) / f.cell - 0.5f));
        for (int k = k0; k <= k1; ++k)
            for (int i = i0; i <= i1; ++i) {
                float& g = f.ground[f.idx(i, k)];
                g = std::max(g, b.max.y);
            }
    });
}

float WaterSystem::floodVolume() const {
    float v = 0.0f;
    for (float d : m_flood.depth) v += d;
    return v * m_flood.cell * m_flood.cell;
}

void WaterSystem::stepFlood(float dt, Scene& scene) {
    Flood& f = m_flood;
    if (!f.n) return;
    if ((f.groundTimer -= dt) <= 0.0f) { f.groundTimer = 1.0f; buildGround(scene); }   // parts can move
    const float L = f.cell, area = L * L, g = scene.world().fluidGravity();   // (real-world strength: see WorldSettings)
    const int n = f.n;
    const int steps = std::clamp((int)std::ceil(dt / kStep), 1, 8);
    const float h = dt / steps;
    const int di[4] = {1, -1, 0, 0}, dk[4] = {0, 0, 1, -1};

    for (int s = 0; s < steps; ++s) {
        // Sources pour water onto the floor under them.
        for (const Source& src : m_sources) {
            if (src.rate <= 0.0f) continue;
            int ci = (int)std::floor((src.pos.x - f.origin.x) / L), ck = (int)std::floor((src.pos.z - f.origin.y) / L);
            int r = std::max(0, (int)std::floor(src.radius / L));
            std::vector<size_t> under;
            for (int k = ck - r; k <= ck + r; ++k)
                for (int i = ci - r; i <= ci + r; ++i)
                    if (i >= 0 && k >= 0 && i < n && k < n && f.ground[f.idx(i, k)] > kNoGround * 0.5f) under.push_back(f.idx(i, k));
            for (size_t id : under) f.depth[id] += src.rate * h / (area * under.size());
        }

        // How much flows from each square to each neighbour.
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n; ++i) {
                size_t id = f.idx(i, k);
                float d = f.depth[id];
                float H = f.ground[id] + d;
                float total = 0.0f;
                for (int dir = 0; dir < 4; ++dir) {
                    int ni = i + di[dir], nk = k + dk[dir];
                    float Hn;
                    if (ni < 0 || nk < 0 || ni >= n || nk >= n) Hn = f.ground[id] - 1.0f;   // off the edge: runs away
                    else {
                        size_t nid = f.idx(ni, nk);
                        Hn = f.ground[nid] < kNoGround * 0.5f ? f.ground[id] - 1.0f : f.ground[nid] + f.depth[nid];
                    }
                    float o = std::max(0.0f, f.out[dir][id] * 0.995f + h * L * g * (H - Hn));
                    f.out[dir][id] = o;
                    total += o;
                }
                // Can't send away more water than there is.
                if (total * h > d * area) {
                    float k2 = total > 0.0f ? d * area / (total * h) : 0.0f;
                    for (auto& o : f.out) o[id] *= k2;
                }
            }
        // Move the water.
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n; ++i) {
                size_t id = f.idx(i, k);
                float in = 0.0f, outSum = 0.0f;
                for (int dir = 0; dir < 4; ++dir) {
                    outSum += f.out[dir][id];
                    int ni = i + di[dir], nk = k + dk[dir];
                    if (ni >= 0 && nk >= 0 && ni < n && nk < n) in += f.out[dir ^ 1][f.idx(ni, nk)];   // its flow back towards us
                }
                float& d = f.depth[id];
                d = std::max(0.0f, d + h * (in - outSum) / area);
                if (f.ground[id] < kNoGround * 0.5f) d = 0.0f;   // fell away
            }
    }
    // Which way the water is going (for currents), from the flow through each side.
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i) {
            size_t id = f.idx(i, k);
            float d = f.depth[id];
            if (d < kMinDepth) { f.vel[id] = glm::vec2(0.0f); continue; }
            auto net = [&](int dirPos, int ni, int nk) {   // flow across the face towards +dir
                float o = f.out[dirPos][id];
                if (ni >= 0 && nk >= 0 && ni < n && nk < n) o -= f.out[dirPos ^ 1][f.idx(ni, nk)];
                return o;
            };
            glm::vec2 v(net(0, i + 1, k) - net(1, i - 1, k), net(2, i, k + 1) - net(3, i, k - 1));
            v /= 2.0f * L * std::max(d, 0.1f);
            float len = glm::length(v);
            f.vel[id] = len > 12.0f ? v * (12.0f / len) : v;
        }

    // A stream of droplets pouring out of each source.
    m_streamTimer -= dt;
    if (m_streamTimer <= 0.0f) {
        m_streamTimer = 0.05f;
        for (const Source& src : m_sources)
            if (src.rate > 0.0f)
                scene.particles().waterSpray(src.pos, (int)std::clamp(src.rate * 0.5f, 1.0f, 8.0f), 1.5f, f.color, src.radius * 0.5f);
    }
}

bool WaterSystem::floodAt(const glm::vec3& p, float* surface, glm::vec3* flow) const {
    const Flood& f = m_flood;
    if (!f.n) return false;
    int i = (int)std::floor((p.x - f.origin.x) / f.cell), k = (int)std::floor((p.z - f.origin.y) / f.cell);
    if (i < 0 || k < 0 || i >= f.n || k >= f.n) return false;
    size_t id = f.idx(i, k);
    float d = f.depth[id], g = f.ground[id];
    if (d < kMinDepth || p.y < g - 0.1f || p.y > g + d) return false;
    if (surface) *surface = g + d;
    if (flow) *flow = glm::vec3(f.vel[id].x, 0.0f, f.vel[id].y);
    return true;
}

bool WaterSystem::maybeWet(const glm::vec3& mn, const glm::vec3& mx) const {
    for (const auto& w : m_bodies)
        if (mx.x > w.min.x && mn.x < w.max.x && mx.z > w.min.z && mn.z < w.max.z &&
            mn.y < w.max.y + w.swell + 2.5f && mx.y > w.min.y) return true;
    const Flood& f = m_flood;
    if (f.n) {
        float size = f.n * f.cell;
        if (mx.x > f.origin.x && mn.x < f.origin.x + size && mx.z > f.origin.y && mn.z < f.origin.y + size &&
            mn.y < f.ceiling + 2.0f) return true;
    }
    return false;
}
