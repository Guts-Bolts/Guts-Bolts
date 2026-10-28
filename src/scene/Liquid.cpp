// Real liquid: Position Based Fluids (Macklin & Müller 2013). Each drop is a
// little blob; every step they're moved, then nudged so the liquid everywhere
// has the same density (water doesn't squash), which makes them flow, pile up,
// spread out and splash like the real thing.
#include "Liquid.h"
#include "LiquidGpu.h"
#include "../core/Settings.h"
#include "EditMesh.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"
#include "Player.h"
#include "Water.h"
#include "Npc.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <thread>

namespace {

// Every drop's sums are independent within a pass, so they're shared out over
// the computer's cores (a small pool of helper threads, made once).
class Workers {
public:
    static Workers& get() { static Workers w; return w; }

    // fn(first, last) for chunks of 0..count, on all cores; returns when all are done.
    void run(size_t count, const std::function<void(size_t, size_t)>& fn) {
        if (count < 512 || m_threads.empty()) { fn(0, count); return; }
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_job = &fn;
            m_count = count;
            m_chunks = (int)(m_threads.size() + 1) * 4;
            m_next = 0;
            ++m_generation;
        }
        m_wake.notify_all();
        work();
        std::unique_lock<std::mutex> l(m_mutex);
        m_done.wait(l, [&] { return m_busy == 0 && m_next.load() >= m_chunks; });
    }

private:
    Workers() {
        const unsigned cores = std::thread::hardware_concurrency();
#ifdef __EMSCRIPTEN__
        const unsigned helpers = 0;   // (a web page has no threads: it all runs on this one)
        (void)cores;
#else
        const unsigned helpers = cores > 1 ? std::min(cores - 1, 7u) : 0u;
#endif
        for (unsigned i = 0; i < helpers; ++i) m_threads.emplace_back([this] { loop(); });
    }
    ~Workers() {
        { std::lock_guard<std::mutex> l(m_mutex); m_quit = true; }
        m_wake.notify_all();
        for (std::thread& t : m_threads) t.join();
    }
    void work() {
        int c;
        while ((c = m_next.fetch_add(1)) < m_chunks) {
            const size_t a = m_count * (size_t)c / (size_t)m_chunks, b = m_count * (size_t)(c + 1) / (size_t)m_chunks;
            (*m_job)(a, b);
        }
    }
    void loop() {
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> l(m_mutex);
                m_wake.wait(l, [&] { return m_quit || m_generation != seen; });
                if (m_quit) return;
                seen = m_generation;
                ++m_busy;
            }
            work();
            {
                std::lock_guard<std::mutex> l(m_mutex);
                --m_busy;
            }
            m_done.notify_all();
        }
    }
    std::vector<std::thread> m_threads;
    std::mutex m_mutex;
    std::condition_variable m_wake, m_done;
    const std::function<void(size_t, size_t)>* m_job = nullptr;
    size_t m_count = 0;
    int m_chunks = 0, m_busy = 0;
    std::atomic<int> m_next{0};
    uint64_t m_generation = 0;
    bool m_quit = false;
};

void parallel(size_t count, const std::function<void(size_t, size_t)>& fn) { Workers::get().run(count, fn); }

constexpr float kH = 2.0f * Liquid::kSpacing;              // how far a drop feels its neighbours
constexpr float kPi = 3.14159265358979f;
const float kPoly6 = 315.0f / (64.0f * kPi * std::pow(kH, 9.0f));
const float kSpiky = -45.0f / (kPi * std::pow(kH, 6.0f));
constexpr int   kMaxNeigh = 40;
constexpr int   kIterations = 2;
constexpr float kMaxSpeed = 32.0f;
constexpr float kFriction = 0.15f;                         // per second, where it touches things (slides are slippery)
constexpr float kTension = 60.0f;                          // how hard SurfaceTension 1 pulls drops together (studs/s²)

float poly6(float r2) {
    const float d = kH * kH - r2;
    return d > 0.0f ? kPoly6 * d * d * d : 0.0f;
}

glm::vec3 spikyGrad(const glm::vec3& r, float len) {
    if (len <= 1e-6f || len >= kH) return glm::vec3(0.0f);
    const float d = kH - len;
    return kSpiky * d * d * (r / len);
}

// Surface tension: drops a little apart pull towards each other (zero right on
// top of each other and at the edge of reach; the density solver keeps them from squashing).
float cohesion(float len) {
    if (len <= 0.0f || len >= kH) return 0.0f;
    return std::sin(kPi * len / kH);
}

uint32_t cellHash(int x, int y, int z, uint32_t mask) {
    return ((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u) & mask;
}

glm::ivec3 cellOf(const glm::vec3& p) {
    return glm::ivec3((int)std::floor(p.x / kH), (int)std::floor(p.y / kH), (int)std::floor(p.z / kH));
}

float rnd() { return (float)std::rand() / (float)RAND_MAX; }

// The nearest point to `p` on the triangle abc (Ericson, Real-Time Collision Detection).
glm::vec3 closestOnTri(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float denom = 1.0f / (va + vb + vc);
    return a + ab * (vb * denom) + ac * (vc * denom);
}

constexpr size_t kMaxTriangles = 40000;   // mesh triangles the liquid bumps into

bool flagged(const SceneNode* n, const char* what) {
    if (std::find(n->tags.begin(), n->tags.end(), what) != n->tags.end()) return true;
    const Attribute* a = n->findAttribute(what);
    return a && a->type == Attribute::Bool && a->b;
}

float numberAttr(const SceneNode* n, const char* name, float fallback) {
    const Attribute* a = n->findAttribute(name);
    return a && a->type == Attribute::Number ? (float)a->n : fallback;
}

bool shown(const SceneNode* n) {
    for (const SceneNode* p = n; p; p = p->parent) if (!p->visible) return false;
    return true;
}

} // namespace

int Liquid::s_contexts = 0;

Liquid::Liquid() = default;

Liquid::~Liquid() {
    if (m_gpu && s_contexts <= 0) m_gpu->forget();   // the graphics context is already gone
}

void Liquid::graphicsContext(int change) { s_contexts += change; }

bool Liquid::isSource(const SceneNode* n) {
    return n->isPart() && (n->name == "FluidSource" || flagged(n, "FluidSource"));
}

void Liquid::begin(Scene& scene) {
    end();
    m_active = true;
    // The density of liquid at rest: drops packed kSpacing apart.
    m_rest = 0.0f;
    const int k = (int)std::ceil(kH / kSpacing);
    for (int x = -k; x <= k; ++x)
        for (int y = -k; y <= k; ++y)
            for (int z = -k; z <= k; ++z) {
                glm::vec3 r = glm::vec3(x, y, z) * kSpacing;
                m_rest += poly6(glm::dot(r, r));
            }
    // On the graphics card if it can (much faster: many more drops).
    if (s_contexts > 0) {
        std::string why;
        if (LiquidGpu::supported(&why)) {
            m_gpu = std::make_unique<LiquidGpu>();
            if (!m_gpu->init(why)) {
                std::fprintf(stderr, "Liquid: graphics card physics failed (%s), using the CPU\n", why.c_str());
                m_gpu.reset();
            }
        } else {
            std::fprintf(stderr, "Liquid: physics on the CPU (%s)\n", why.c_str());
        }
    }
    scan(scene);
}

void Liquid::end() {
    m_active = false;
    m_x.clear(); m_v.clear(); m_p.clear(); m_dp.clear(); m_lambda.clear(); m_age.clear(); m_near.clear(); m_draw.clear();
    m_kind.clear(); m_fluids.clear();
    m_maxAge = 120.0f;
    m_sources.clear();
    m_colliders.clear();
    if (m_gpu && s_contexts <= 0) m_gpu->forget();
    m_gpu.reset();
    m_probeAsk.clear(); m_probeSent.clear(); m_probeAnswered.clear();
    m_gpuColliderIds.clear();
    m_wetUntil.clear();
    m_time = 0.0f;
}

// Everything solid the liquid bumps into, and the taps it pours from. Done every
// frame: parts move, people walk through the stream.
void Liquid::scan(Scene& scene) {
    m_colliders.clear();
    std::vector<Source> sources;
    size_t triangles = 0;
    // The kinds of liquid first (emitters point at them).
    m_fluids.assign(1, Fluid{});
    scene.forEach([&](SceneNode* n) {
        if (n->kind != NodeKind::FluidSystem || (int)m_fluids.size() >= kMaxFluids) return;
        Fluid f;
        f.id = n->id;
        f.color = n->color;
        f.viscosity = glm::clamp(n->viscosity, 0.0f, 1.0f);
        f.tension = glm::clamp(n->surfaceTension, 0.0f, 1.0f);
        m_fluids.push_back(f);
    });
    auto fluidIndex = [&](uint64_t id) {
        for (size_t i = 1; i < m_fluids.size(); ++i) if (m_fluids[i].id == id) return (int)i;
        return 0;
    };
    scene.forEach([&](SceneNode* n) {
        // A FluidEmitter (made in Studio or with Instance.new): a box that pours drops at Velocity.
        if (n->kind == NodeKind::FluidEmitter) {
            if (!n->enabled || n->fluidRate <= 0.0f || !shown(n)) return;
            const glm::mat4 m = n->worldMatrix();
            Source s;
            s.id = n->id;
            s.pos = glm::vec3(m[3]);
            const float sp = glm::length(n->fluidVelocity);
            s.dir = sp > 1e-3f ? n->fluidVelocity / sp : glm::vec3(0.0f, -1.0f, 0.0f);
            s.speed = sp;
            // The face of the box the drops come out of, across the way they go.
            const glm::vec3 ref = std::abs(s.dir.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
            s.side = glm::normalize(glm::cross(s.dir, ref));
            s.up = glm::normalize(glm::cross(s.side, s.dir));
            const glm::vec3 size = n->transform.scale;
            s.width = std::max(0.2f, glm::dot(glm::abs(s.side), size));
            s.height = std::max(0.2f, glm::dot(glm::abs(s.up), size));
            s.rate = n->fluidRate;
            s.fluid = fluidIndex(n->fluidSystem);
            for (const Source& old : m_sources) if (old.id == s.id) s.carry = old.carry;
            sources.push_back(s);
            return;
        }
        if (!n->isPart() || !shown(n)) return;
        const glm::mat4 m = n->worldMatrix();
        if (isSource(n)) {
            Source s;
            s.id = n->id;
            s.pos = glm::vec3(m[3]);
            s.dir = -glm::normalize(glm::vec3(m[2]));      // its front (LookVector)
            s.side = glm::normalize(glm::vec3(m[0]));
            s.up = glm::normalize(glm::vec3(m[1]));
            s.rate = std::max(0.0f, numberAttr(n, "Rate", 600.0f));
            s.speed = numberAttr(n, "Speed", 8.0f);
            s.width = std::max(0.2f, numberAttr(n, "Width", glm::length(glm::vec3(m[0]))));
            s.height = std::max(0.2f, glm::length(glm::vec3(m[1])));
            s.part = true;
            for (const Source& old : m_sources) if (old.id == s.id) s.carry = old.carry;
            sources.push_back(s);
            return;
        }
        if (!n->canCollide || scene.isCharacterPart(n) || Player::isWater(n)) return;
        // A custom mesh (Modeling mode): its real triangles.
        if (n->primitiveType == PrimitiveType::Mesh && n->editMesh && !n->editMesh->faces.empty()) {
            const EditMesh& em = *n->editMesh;
            for (const auto& f : em.faces) {
                for (size_t k = 1; k + 1 < f.size(); ++k) {
                    if (triangles >= kMaxTriangles) return;
                    if (f[0] >= em.verts.size() || f[k] >= em.verts.size() || f[k + 1] >= em.verts.size()) continue;
                    const glm::vec3 a = glm::vec3(m * glm::vec4(em.verts[f[0]], 1.0f));
                    const glm::vec3 b = glm::vec3(m * glm::vec4(em.verts[f[k]], 1.0f));
                    const glm::vec3 cc = glm::vec3(m * glm::vec4(em.verts[f[k + 1]], 1.0f));
                    const glm::vec3 nrm = glm::cross(b - a, cc - a);
                    const float len = glm::length(nrm);
                    if (len < 1e-8f) continue;
                    Collider t;
                    t.id = n->id;
                    t.shape = 3;
                    t.center = a; t.axis[0] = b; t.axis[1] = cc; t.axis[2] = nrm / len;
                    t.half = glm::vec3(0.0f);
                    if (!n->anchored) t.velocity = n->velocity;
                    t.min = glm::min(a, glm::min(b, cc));
                    t.max = glm::max(a, glm::max(b, cc));
                    m_colliders.push_back(t);
                    ++triangles;
                }
            }
            return;
        }
        Collider c;
        c.id = n->id;
        c.shape = n->primitiveType == PrimitiveType::Sphere ? 1 : n->primitiveType == PrimitiveType::Cylinder ? 2 : 0;
        c.center = glm::vec3(m[3]);
        for (int i = 0; i < 3; ++i) {
            const float len = glm::length(glm::vec3(m[i]));
            c.axis[i] = len > 1e-6f ? glm::vec3(m[i]) / len : glm::vec3(i == 0, i == 1, i == 2);
            c.half[i] = std::max(0.02f, len * 0.5f);
        }
        if (n->primitiveType == PrimitiveType::Plane) c.half.y = 0.02f;
        if (!n->anchored) c.velocity = n->velocity;
        AABB b = Physics::worldBounds(n);
        c.min = b.min; c.max = b.max;
        m_colliders.push_back(c);
    });
    // People are solid too: the stream parts around them.
    auto person = [&](uint64_t id, const glm::vec3& feet, const glm::vec3& vel) {
        Collider c;
        c.id = id;
        c.center = feet + glm::vec3(0.0f, Physics::kCharHeight * 0.5f, 0.0f);
        c.axis[0] = {1, 0, 0}; c.axis[1] = {0, 1, 0}; c.axis[2] = {0, 0, 1};
        c.half = glm::vec3(Physics::kCharHalfWidth, Physics::kCharHeight * 0.5f, Physics::kCharHalfWidth);
        c.velocity = vel;
        c.min = c.center - c.half; c.max = c.center + c.half;
        m_colliders.push_back(c);
    };
    if (Player* pl = scene.player(); pl && pl->root() && !pl->isDead()) person(pl->rootId(), pl->position(), pl->velocity());
    for (const auto& n : scene.npcs().all())
        if (!n->dead)
            if (SceneNode* r = scene.findById(n->rootId)) person(n->rootId, r->transform.position, glm::vec3(0.0f));
    m_sources.swap(sources);
    buildColliderGrid();
    if (m_colliders.size() > m_touchedCap) {
        m_touchedCap = m_colliders.size() + m_colliders.size() / 2;
        m_touched.reset(new std::atomic<uint8_t>[m_touchedCap]);
    }
    for (size_t i = 0; i < m_colliders.size(); ++i) m_touched[i].store(0, std::memory_order_relaxed);
}

void Liquid::buildColliderGrid() {
    m_cgStart.clear(); m_cgItems.clear();
    if (m_colliders.empty()) { m_cgDim = glm::ivec3(0); return; }
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const Collider& c : m_colliders) { lo = glm::min(lo, c.min); hi = glm::max(hi, c.max); }
    lo -= glm::vec3(1.0f); hi += glm::vec3(1.0f);
    const glm::vec3 size = hi - lo;
    m_cgCell = std::max({6.0f, size.x / 128.0f, size.y / 128.0f, size.z / 128.0f});
    m_cgMin = glm::ivec3(glm::floor(lo / m_cgCell));
    m_cgDim = glm::ivec3(glm::floor(hi / m_cgCell)) - m_cgMin + glm::ivec3(1);
    const size_t cells = (size_t)m_cgDim.x * m_cgDim.y * m_cgDim.z;
    std::vector<int> counts(cells + 1, 0);
    auto range = [&](const Collider& c, glm::ivec3& a, glm::ivec3& b) {
        a = glm::clamp(glm::ivec3(glm::floor((c.min - glm::vec3(kRadius)) / m_cgCell)) - m_cgMin, glm::ivec3(0), m_cgDim - 1);
        b = glm::clamp(glm::ivec3(glm::floor((c.max + glm::vec3(kRadius)) / m_cgCell)) - m_cgMin, glm::ivec3(0), m_cgDim - 1);
    };
    auto idx = [&](int x, int y, int z) { return ((size_t)z * m_cgDim.y + y) * m_cgDim.x + x; };
    for (const Collider& c : m_colliders) {
        glm::ivec3 a, b;
        range(c, a, b);
        for (int z = a.z; z <= b.z; ++z) for (int y = a.y; y <= b.y; ++y) for (int x = a.x; x <= b.x; ++x) ++counts[idx(x, y, z) + 1];
    }
    for (size_t i = 1; i <= cells; ++i) counts[i] += counts[i - 1];
    m_cgStart = counts;
    m_cgItems.assign(counts[cells], 0);
    std::vector<int> fill(counts.begin(), counts.end() - 1);
    for (int i = 0; i < (int)m_colliders.size(); ++i) {
        glm::ivec3 a, b;
        range(m_colliders[i], a, b);
        for (int z = a.z; z <= b.z; ++z) for (int y = a.y; y <= b.y; ++y) for (int x = a.x; x <= b.x; ++x) m_cgItems[fill[idx(x, y, z)]++] = i;
    }
}

// Push a drop out of anything solid it's inside. Returns through `v` the drop's
// velocity with the part of it going into the surface taken away.
void Liquid::collide(glm::vec3& p, glm::vec3& v, float dt, const glm::vec3& prev) const {
    if (m_cgDim.x == 0) return;
    const glm::ivec3 cell = glm::ivec3(glm::floor(p / m_cgCell)) - m_cgMin;
    if (glm::any(glm::lessThan(cell, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(cell, m_cgDim))) return;
    const size_t ci = ((size_t)cell.z * m_cgDim.y + cell.y) * m_cgDim.x + cell.x;
    for (int k = m_cgStart[ci]; k < m_cgStart[ci + 1]; ++k) {
        const int index = m_cgItems[k];
        const Collider& c = m_colliders[index];
        if (p.x < c.min.x - kRadius || p.x > c.max.x + kRadius || p.y < c.min.y - kRadius || p.y > c.max.y + kRadius ||
            p.z < c.min.z - kRadius || p.z > c.max.z + kRadius) continue;
        if (c.shape == 3) {
            // A triangle of a mesh: stay on the side the drop came from.
            const glm::vec3 &a = c.center, &b = c.axis[0], &cc = c.axis[1], &tn = c.axis[2];
            const float sp = glm::dot(prev - a, tn), sn = glm::dot(p - a, tn);
            const float side = std::abs(sp) > 1e-4f ? (sp < 0.0f ? -1.0f : 1.0f) : (sn >= 0.0f ? 1.0f : -1.0f);
            glm::vec3 n(0.0f);
            bool hit = false;
            if (sp * sn < 0.0f) {                // went through its plane this step: where?
                const glm::vec3 x = prev + (p - prev) * (sp / (sp - sn));
                const glm::vec3 q = closestOnTri(x, a, b, cc);
                if (glm::dot(q - x, q - x) < 1e-6f) { p = q + tn * side * kRadius; n = tn * side; hit = true; }
            }
            if (!hit) {
                const glm::vec3 q = closestOnTri(p, a, b, cc);
                const glm::vec3 d = p - q;
                const float dist = glm::length(d);
                if (dist >= kRadius) continue;
                glm::vec3 dir = dist > 1e-5f ? d / dist : tn * side;
                if (glm::dot(dir, tn * side) < 0.0f) dir = tn * side;
                p = q + dir * kRadius;
                n = dir;
            }
            glm::vec3 rel = v - c.velocity;
            const float vn = glm::dot(rel, n);
            if (vn < 0.0f) rel -= vn * n;
            if (dt > 0.0f) rel -= (rel - glm::dot(rel, n) * n) * std::min(1.0f, kFriction * dt);
            v = rel + c.velocity;
            if (dt > 0.0f && m_touched) m_touched[index].store(1, std::memory_order_relaxed);
            continue;
        }
        const glm::vec3 d = p - c.center;
        glm::vec3 l(glm::dot(d, c.axis[0]), glm::dot(d, c.axis[1]), glm::dot(d, c.axis[2]));
        glm::vec3 nLocal(0.0f);
        if (c.shape == 0) {
            const glm::vec3 e = c.half + glm::vec3(kRadius);
            if (std::abs(l.x) >= e.x || std::abs(l.y) >= e.y || std::abs(l.z) >= e.z) continue;
            // Out through the side it came in by (a thin wall must never pop a drop out the far side).
            const glm::vec3 dp = prev - c.center;
            const glm::vec3 lp(glm::dot(dp, c.axis[0]), glm::dot(dp, c.axis[1]), glm::dot(dp, c.axis[2]));
            int ax = -1;
            float out = 0.0f;
            for (int i = 0; i < 3; ++i) {
                const float o = std::abs(lp[i]) / e[i];
                if (o >= 1.0f && o > out) { out = o; ax = i; }
            }
            float s;
            if (ax >= 0) {
                s = lp[ax] < 0.0f ? -1.0f : 1.0f;
            } else {   // it was already inside: the nearest face
                ax = 0;
                float best = e.x - std::abs(l.x);
                for (int i = 1; i < 3; ++i) if (e[i] - std::abs(l[i]) < best) { best = e[i] - std::abs(l[i]); ax = i; }
                s = l[ax] < 0.0f ? -1.0f : 1.0f;
            }
            l[ax] = s * e[ax];
            nLocal[ax] = s;
        } else if (c.shape == 1) {
            const glm::vec3 e = c.half + glm::vec3(kRadius);
            const glm::vec3 q = l / e;
            const float len = glm::length(q);
            if (len >= 1.0f) continue;
            const glm::vec3 dirq = len > 1e-5f ? q / len : glm::vec3(0, 1, 0);
            l = dirq * e;
            nLocal = glm::normalize(dirq / e);
        } else {
            const float rx = c.half.x + kRadius, rz = c.half.z + kRadius, hy = c.half.y + kRadius;
            const float rr = std::sqrt((l.x / rx) * (l.x / rx) + (l.z / rz) * (l.z / rz));
            if (rr >= 1.0f || std::abs(l.y) >= hy) continue;
            const float radialPen = (1.0f - rr) * std::min(rx, rz), capPen = hy - std::abs(l.y);
            if (capPen < radialPen) {
                const float s = l.y < 0.0f ? -1.0f : 1.0f;
                l.y = s * hy;
                nLocal = {0, s, 0};
            } else {
                const glm::vec2 dir = rr > 1e-5f ? glm::vec2(l.x / rx, l.z / rz) / rr : glm::vec2(1, 0);
                l.x = dir.x * rx; l.z = dir.y * rz;
                nLocal = glm::normalize(glm::vec3(dir.x / rx, 0.0f, dir.y / rz));
            }
        }
        p = c.center + c.axis[0] * l.x + c.axis[1] * l.y + c.axis[2] * l.z;
        const glm::vec3 n = glm::normalize(c.axis[0] * nLocal.x + c.axis[1] * nLocal.y + c.axis[2] * nLocal.z);
        glm::vec3 rel = v - c.velocity;
        const float vn = glm::dot(rel, n);
        if (vn < 0.0f) rel -= vn * n;                        // no going into it
        if (dt > 0.0f) rel -= (rel - glm::dot(rel, n) * n) * std::min(1.0f, kFriction * dt);   // a little drag along it
        v = rel + c.velocity;
        if (dt > 0.0f && m_touched) m_touched[index].store(1, std::memory_order_relaxed);   // it's wet now
    }
}

void Liquid::buildGrid(const std::vector<glm::vec3>& pos) {
    const size_t n = pos.size();
    // The table grows with the number of drops (about two slots each).
    uint32_t table = 1024;
    while (table < n * 2 && table < (1u << 20)) table <<= 1;
    m_mask = table - 1;
    m_cellStart.assign(table + 1, 0);
    m_cellOf.resize(n);
    for (size_t i = 0; i < n; ++i) {
        const glm::ivec3 c = cellOf(pos[i]);
        m_cellOf[i] = cellHash(c.x, c.y, c.z, m_mask);
        ++m_cellStart[m_cellOf[i] + 1];
    }
    for (uint32_t i = 1; i <= table; ++i) m_cellStart[i] += m_cellStart[i - 1];
    m_sorted.resize(n);
    m_cellCount.assign(m_cellStart.begin(), m_cellStart.end() - 1);   // (used as fill cursors)
    for (size_t i = 0; i < n; ++i) m_sorted[m_cellCount[m_cellOf[i]]++] = (int)i;
}

template <typename F>
void Liquid::forNeighbours(const glm::vec3& p, F&& f) const {
    const glm::ivec3 c = cellOf(p);
    for (int z = c.z - 1; z <= c.z + 1; ++z)
        for (int y = c.y - 1; y <= c.y + 1; ++y)
            for (int x = c.x - 1; x <= c.x + 1; ++x) {
                const uint32_t h = cellHash(x, y, z, m_mask);
                for (int k = m_cellStart[h]; k < m_cellStart[h + 1]; ++k) f(m_sorted[k]);
            }
}

// A tap pours a steady column the size of its face: a new layer of drops each
// time the water has moved one drop-spacing out of it (so they never pile up).
void Liquid::emit(float dt, size_t room, std::vector<glm::vec4>* out) {
    size_t made = 0;
    // Spray thrown in by the water (splashes, waves hitting walls).
    for (size_t i = 0; i + 1 < m_spray.size() && made < room; i += 2, ++made) {
        const glm::vec3 p(m_spray[i]), v(m_spray[i + 1]);
        if (out) { out->push_back(m_spray[i]); out->push_back(m_spray[i + 1]); continue; }
        m_x.push_back(p); m_v.push_back(v); m_p.push_back(p);
        m_lambda.push_back(0.0f); m_age.push_back(0.0f); m_near.push_back(0.0f);
        m_kind.push_back((uint8_t)m_spray[i].w);
    }
    m_spray.clear();
    for (Source& s : m_sources) {
        if (s.rate <= 0.0f || (s.part && s.speed <= 0.0f)) continue;
        if (m_viewDist > 0.0f && glm::length(s.pos - m_viewer) > m_viewDist + 10.0f) continue;   // past the render distance
        // Layers per second: the speed (a slow emitter still makes room for the next
        // layer by spacing them out), but never more drops than Rate.
        const float laySpeed = std::max(s.speed, 2.0f);
        float width = s.width, height = s.height;
        if (!s.part) {
            // A FluidEmitter makes Rate drops a second. A small opening can't push that
            // many out without squashing them together, so the stream spreads wider
            // instead (like a nozzle spraying), up to 4 times as wide.
            const float fits = std::max(1.0f, std::round(width / kSpacing)) * std::max(1.0f, std::round(height / kSpacing)) *
                               laySpeed / kSpacing;
            if (s.rate > fits) {
                const float grow = std::min(4.0f, std::sqrt(s.rate / fits));
                width *= grow;
                height *= grow;
            }
        }
        const int across = std::max(1, (int)std::round(width / kSpacing));
        const int high = std::max(1, (int)std::round(height / kSpacing));
        const float perLayer = (float)(across * high);
        const float layers = std::min(laySpeed / kSpacing, s.rate / perLayer);
        s.carry += layers * dt;
        while (s.carry >= 1.0f && made + (size_t)perLayer <= room) {
            made += (size_t)perLayer;
            s.carry -= 1.0f;
            const float ahead = s.carry / std::max(1e-3f, layers) * laySpeed;   // how far this layer has moved already
            for (int a = 0; a < across; ++a)
                for (int b = 0; b < high; ++b) {
                    glm::vec3 p = s.pos + s.side * ((a + 0.5f) / across - 0.5f) * width +
                                  s.up * ((b + 0.5f) / high - 0.5f) * height + s.dir * ahead +
                                  glm::vec3(rnd() - 0.5f, rnd() - 0.5f, rnd() - 0.5f) * 0.04f;
                    if (out) {
                        out->push_back(glm::vec4(p, (float)s.fluid));
                        out->push_back(glm::vec4(s.dir * s.speed, 0.0f));
                        continue;
                    }
                    m_x.push_back(p); m_v.push_back(s.dir * s.speed); m_p.push_back(p);
                    m_lambda.push_back(0.0f); m_age.push_back(0.0f); m_near.push_back(6.0f);
                    m_kind.push_back((uint8_t)s.fluid);
                }
        }
        if (made + (size_t)perLayer > room) s.carry = std::min(s.carry, 1.0f);   // full: don't save up
    }
}

void Liquid::step(float dt, Scene& scene) {
    const size_t n = m_x.size();
    if (!n) return;
    const glm::vec3 g(0.0f, -scene.world().gravity, 0.0f);
    using clk = std::chrono::steady_clock;
    auto tick = clk::now();
    auto lap = [&](int k) { auto t = clk::now(); m_prof[k] += std::chrono::duration<float, std::milli>(t - tick).count(); tick = t; };
    // Move, and keep out of solid things.
    parallel(n, [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) {
            m_v[i] += g * dt;
            const float sp = glm::length(m_v[i]);
            if (sp > kMaxSpeed) m_v[i] *= kMaxSpeed / sp;
            m_p[i] = m_x[i] + m_v[i] * dt;
            collide(m_p[i], m_v[i], 0.0f, m_x[i]);
        }
    });
    lap(0);
    // Who's near whom.
    buildGrid(m_p);
    m_neigh.resize(n * kMaxNeigh);
    m_neighCount.assign(n, 0);
    parallel(n, [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) {
            int cnt = 0;
            const glm::vec3 pi = m_p[i];
            int* out = &m_neigh[i * kMaxNeigh];
            forNeighbours(pi, [&](int j) {
                if ((size_t)j == i || cnt >= kMaxNeigh) return;
                const glm::vec3 r = pi - m_p[j];
                if (glm::dot(r, r) < kH * kH) out[cnt++] = j;
            });
            m_neighCount[i] = cnt;
        }
    });
    lap(1);
    // Make the density the same everywhere (liquid doesn't squash).
    const float invRest = 1.0f / m_rest;
    const float wq = poly6(0.04f * kH * kH);        // for the anti-clumping term
    m_dp.resize(n);
    for (int it = 0; it < kIterations; ++it) {
        parallel(n, [&](size_t a, size_t b) {
            for (size_t i = a; i < b; ++i) {
                const glm::vec3 pi = m_p[i];
                float rho = poly6(0.0f), sumGrad2 = 0.0f;
                glm::vec3 gradI(0.0f);
                const int* nb = &m_neigh[i * kMaxNeigh];
                for (int k = 0; k < m_neighCount[i]; ++k) {
                    const glm::vec3 r = pi - m_p[nb[k]];
                    const float len = glm::length(r);
                    rho += poly6(len * len);
                    const glm::vec3 gr = spikyGrad(r, len) * invRest;
                    sumGrad2 += glm::dot(gr, gr);
                    gradI += gr;
                }
                const float C = std::max(0.0f, rho * invRest - 1.0f);   // only push apart (no clumping at the surface)
                m_lambda[i] = -C / (sumGrad2 + glm::dot(gradI, gradI) + 0.5f);
            }
        });
        parallel(n, [&](size_t a, size_t b) {
            for (size_t i = a; i < b; ++i) {
                const glm::vec3 pi = m_p[i];
                glm::vec3 d(0.0f);
                const int* nb = &m_neigh[i * kMaxNeigh];
                for (int k = 0; k < m_neighCount[i]; ++k) {
                    const int j = nb[k];
                    const glm::vec3 r = pi - m_p[j];
                    const float len = glm::length(r);
                    float corr = poly6(len * len) / wq;
                    corr = -0.002f * corr * corr * corr * corr;
                    d += (m_lambda[i] + m_lambda[j] + corr) * spikyGrad(r, len);
                }
                m_dp[i] = d * invRest;
                // Never shove a drop more than half a spacing at once (drops poured on top of
                // each other would otherwise explode apart).
                const float len2 = glm::dot(m_dp[i], m_dp[i]);
                if (len2 > 0.0625f) m_dp[i] *= 0.25f / std::sqrt(len2);
            }
        });
        parallel(n, [&](size_t a, size_t b) {
            for (size_t i = a; i < b; ++i) {
                m_p[i] += m_dp[i];
                glm::vec3 dummy = m_v[i];
                collide(m_p[i], dummy, 0.0f, m_x[i]);
            }
        });
    }
    lap(2);
    // New velocities, a touch of viscosity (drops move with their neighbours).
    parallel(n, [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) m_dp[i] = (m_p[i] - m_x[i]) / dt;   // (m_dp holds the plain velocities now)
    });
    parallel(n, [&](size_t a, size_t b) {
        for (size_t i = a; i < b; ++i) {
            glm::vec3 acc(0.0f), pull(0.0f);
            const Fluid& f = m_fluids[m_kind[i] < m_fluids.size() ? m_kind[i] : 0];
            const int* nb = &m_neigh[i * kMaxNeigh];
            for (int k = 0; k < m_neighCount[i]; ++k) {
                const glm::vec3 r = m_p[i] - m_p[nb[k]];
                const float len = glm::length(r);
                acc += (m_dp[nb[k]] - m_dp[i]) * (poly6(len * len) * invRest);
                if (len > 1e-5f) pull -= r / len * cohesion(len);
            }
            // Viscosity (drops move with their neighbours; honey a lot, water a little) and
            // surface tension (drops pull on each other, so they bead up and hang in strands).
            m_v[i] = m_dp[i] + f.viscosity * acc + pull * (f.tension * kTension * dt);
            // Friction and "don't go into walls" (a lone drop sticks to things; a stream slides).
            collide(m_p[i], m_v[i], m_neighCount[i] < 3 ? dt * 10.0f : dt, m_x[i]);
            m_x[i] = m_p[i];
            // A stray drop sitting on its own dries up in a few seconds.
            if (m_neighCount[i] < 3 && glm::dot(m_v[i], m_v[i]) < 1.0f) m_age[i] += dt * 30.0f;
            m_near[i] = (float)m_neighCount[i];
        }
    });
    lap(3);
}

// Liquid that pours into a pool of still water becomes part of it: ripples.
void Liquid::soakIntoPools(Scene& scene) {
    WaterSystem& water = scene.water();
    size_t w = 0;
    int sprays = 0;
    for (size_t i = 0; i < m_x.size(); ++i) {
        bool keep = m_age[i] < m_maxAge && m_x[i].y > -40.0f;
        if (keep)
            for (const WaterSystem::Body& b : water.bodies()) {
                const glm::vec3& p = m_x[i];
                if (p.x < b.min.x || p.x > b.max.x || p.z < b.min.z || p.z > b.max.z || p.y < b.min.y) continue;
                const float s = water.surface(b, p.x, p.z);
                if (p.y > s - 0.15f) continue;
                const float speed = glm::length(m_v[i]);
                water.disturb(glm::vec3(p.x, s, p.z), std::min(0.02f, 0.0015f * speed), 0.7f);
                if (speed > 10.0f && sprays < 3 && rnd() < 0.08f) {
                    scene.particles().waterSpray(glm::vec3(p.x, s, p.z), 3, std::min(8.0f, speed * 0.3f), b.color, 0.2f);
                    ++sprays;
                }
                keep = false;
                break;
            }
        if (!keep) continue;
        m_x[w] = m_x[i]; m_v[w] = m_v[i]; m_p[w] = m_p[i]; m_lambda[w] = m_lambda[i]; m_age[w] = m_age[i];
        m_near[w] = m_near[i]; m_kind[w] = m_kind[i];
        ++w;
    }
    m_x.resize(w); m_v.resize(w); m_p.resize(w); m_lambda.resize(w); m_age.resize(w); m_near.resize(w); m_kind.resize(w);
}

// Floating things bob on the liquid and get carried along by it.
void Liquid::pushThings(float dt, Scene& scene) {
    scene.forEach([&](SceneNode* nd) {
        if (!nd->isPart() || nd->anchored || !nd->canCollide || scene.isCharacterPart(nd)) return;
        AABB b = Physics::worldBounds(nd);
        const glm::vec3 size = b.max - b.min;
        if (std::max({size.x, size.y, size.z}) > 8.0f) return;
        glm::vec3 vel;
        const int cnt = sample((b.min + b.max) * 0.5f, std::max({size.x, size.y, size.z}) * 0.5f + 0.6f, &vel);
        if (cnt < 3) return;
        const float amount = std::min(1.0f, cnt / 25.0f);
        nd->velocity += (vel - nd->velocity) * std::min(1.0f, 2.5f * amount * dt);
        // Buoyancy (Archimedes): lighter than water floats up, heavier sinks.
        const float lift = std::min(2.5f, Physics::kWaterDensity / Physics::densityOf(nd));
        nd->velocity.y += scene.world().gravity * lift * amount * dt;
        Physics::wake(nd);
    });
}

int Liquid::sample(const glm::vec3& p, float radius, glm::vec3* velocity, float* top) const {
    if (m_gpu) {
        // Ask the graphics card (answered next frame), and give last frame's answer for here.
        bool asked = false;
        for (const glm::vec4& q : m_probeAsk)
            if (std::abs(q.w - radius) < 0.01f && glm::length(glm::vec3(q) - p) < 0.5f) { asked = true; break; }
        if (!asked && m_probeAsk.size() < (size_t)LiquidGpu::kMaxProbes) m_probeAsk.push_back(glm::vec4(p, radius));
        const auto& res = m_gpu->results().probes;
        int best = -1;
        float bestD = 3.0f;
        for (size_t i = 0; i < m_probeAnswered.size() && i < res.size(); ++i) {
            const glm::vec4& q = m_probeAnswered[i];
            const float d = glm::length(glm::vec3(q) - p);
            if (std::abs(q.w - radius) < 0.01f && d < bestD) { bestD = d; best = (int)i; }
        }
        if (best < 0) {
            if (velocity) *velocity = glm::vec3(0.0f);
            if (top) *top = p.y;
            return 0;
        }
        if (velocity) *velocity = res[best].vel;
        if (top) *top = res[best].count ? res[best].top : p.y;
        return res[best].count;
    }
    if (m_x.empty() || m_cellStart.empty()) return 0;
    const float r2 = radius * radius;
    int cnt = 0;
    glm::vec3 sum(0.0f);
    float hi = -1e30f;
    const glm::ivec3 a = cellOf(p - glm::vec3(radius)), b = cellOf(p + glm::vec3(radius));
    for (int z = a.z; z <= b.z; ++z)
        for (int y = a.y; y <= b.y; ++y)
            for (int x = a.x; x <= b.x; ++x) {
                const uint32_t h = cellHash(x, y, z, m_mask);
                for (int k = m_cellStart[h]; k < m_cellStart[h + 1]; ++k) {
                    const int j = m_sorted[k];
                    if ((size_t)j >= m_x.size()) continue;
                    const glm::vec3 d = m_x[j] - p;
                    if (glm::dot(d, d) > r2) continue;
                    ++cnt;
                    sum += m_v[j];
                    hi = std::max(hi, m_x[j].y);
                }
            }
    if (velocity) *velocity = cnt ? sum / (float)cnt : glm::vec3(0.0f);
    if (top) *top = cnt ? hi + kRadius : p.y;
    return cnt;
}

void Liquid::update(float dt, Scene& scene) {
    if (!m_active) return;
    if (m_gpu) { updateGpu(dt, scene); return; }
    const auto t0 = std::chrono::steady_clock::now();
    for (float& p : m_prof) p = 0.0f;
    scan(scene);
    budget(scene, m_x.size(), dt);
    m_prof[4] = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (m_sources.empty() && m_x.empty() && m_spray.empty()) { m_draw.clear(); return; }
    dt = std::min(dt, 1.0f / 30.0f);
    const int steps = std::clamp((int)std::ceil(dt * 90.0f), 1, 3);
    const float h = dt / steps;
    for (int s = 0; s < steps; ++s) {
        emit(h, m_cap - std::min(m_x.size(), m_cap));
        step(h, scene);
    }
    for (float& a : m_age) a += dt;
    m_time += dt;
    for (size_t i = 0; i < m_colliders.size(); ++i)
        if (m_touched[i].load(std::memory_order_relaxed)) markWet(m_colliders[i].id);
    soakIntoPools(scene);
    buildGrid(m_x);   // for sample()
    // Put drops that are near each other next to each other in memory: the sums
    // over neighbours then read memory in order, which is a lot faster.
    {
        const size_t n = m_x.size();
        auto permute = [&](auto& arr) {
            auto copy = arr;
            for (size_t k = 0; k < n; ++k) arr[k] = copy[m_sorted[k]];
        };
        permute(m_x); permute(m_v); permute(m_p); permute(m_lambda); permute(m_age); permute(m_near); permute(m_kind);
        std::vector<uint32_t> cells(n);
        for (size_t k = 0; k < n; ++k) cells[k] = m_cellOf[m_sorted[k]];
        m_cellOf.swap(cells);
        for (size_t k = 0; k < n; ++k) m_sorted[k] = (int)k;
    }
    pushThings(dt, scene);
    // Two vec4s a drop (see drawList): where to draw it (a little smoothed) and its shape.
    m_draw.resize(m_x.size() * 2);
    parallel(m_x.size(), [&](size_t a, size_t b) {
        std::vector<glm::vec3> near;
        for (size_t i = a; i < b; ++i) {
            near.clear();
            forNeighbours(m_x[i], [&](int j) {
                if ((size_t)j == i || near.size() >= 48) return;
                const glm::vec3 r = m_x[j] - m_x[i];
                if (glm::dot(r, r) < kH * kH) near.push_back(m_x[j]);
            });
            glm::vec3 smoothed;
            const glm::vec4 shape = dropShape(m_x[i], near, smoothed);
            m_draw[i * 2] = glm::vec4(smoothed, (float)m_kind[i] * 4096.0f + std::min(m_near[i], 31.0f) * 64.0f +
                                                    std::min(glm::length(m_v[i]), 63.0f));
            m_draw[i * 2 + 1] = shape;
        }
    });
    m_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// The same frame, with the physics on the graphics card.
void Liquid::updateGpu(float dt, Scene& scene) {
    const auto t0 = std::chrono::steady_clock::now();
    for (float& p : m_prof) p = 0.0f;
    m_gpu->beginFrame();                               // (waits for last frame's work to finish)
    m_probeAnswered = m_probeSent;
    const LiquidGpu::Results& res = m_gpu->results();
    // Drops that fell into pools make ripples (and spray if they hit hard).
    WaterSystem& water = scene.water();
    int sprays = 0;
    for (const glm::vec4& sp : res.splashes) {
        const glm::vec3 p(sp);
        water.disturb(p, std::min(0.02f, 0.0015f * sp.w), 0.7f);
        if (sp.w > 10.0f && sprays < 3 && rnd() < 0.3f) {
            glm::vec3 color(0.2f, 0.45f, 0.7f);
            for (const WaterSystem::Body& b : water.bodies())
                if (p.x >= b.min.x && p.x <= b.max.x && p.z >= b.min.z && p.z <= b.max.z) color = b.color;
            scene.particles().waterSpray(p, 3, std::min(8.0f, sp.w * 0.3f), color, 0.2f);
            ++sprays;
        }
    }
    // Parts the liquid ran over last frame are wet.
    m_time += dt;
    {
        const std::vector<uint32_t> touched = m_gpu->takeTouched(m_gpuColliderIds.size());
        for (size_t i = 0; i < touched.size(); ++i)
            if (touched[i]) markWet(m_gpuColliderIds[i]);
    }
    const auto t1 = std::chrono::steady_clock::now();
    m_prof[3] = std::chrono::duration<float, std::milli>(t1 - t0).count();

    scan(scene);
    m_gpuColliderIds.resize(m_colliders.size());
    for (size_t i = 0; i < m_colliders.size(); ++i) m_gpuColliderIds[i] = m_colliders[i].id;
    std::vector<glm::vec4> packed;
    packed.reserve(m_colliders.size() * 7);
    for (const Collider& c : m_colliders) {
        packed.push_back(glm::vec4(c.center, (float)c.shape));
        packed.push_back(glm::vec4(c.axis[0], c.half.x));
        packed.push_back(glm::vec4(c.axis[1], c.half.y));
        packed.push_back(glm::vec4(c.axis[2], c.half.z));
        packed.push_back(glm::vec4(c.velocity, 0.0f));
        packed.push_back(glm::vec4(c.min, 0.0f));
        packed.push_back(glm::vec4(c.max, 0.0f));
    }
    std::vector<int> grid(m_cgStart);
    const int items = (int)grid.size();
    grid.insert(grid.end(), m_cgItems.begin(), m_cgItems.end());
    m_gpu->setColliders(packed, grid, items, m_cgMin, m_cgDim, m_cgCell);
    std::vector<LiquidGpu::Pool> pools;
    for (const WaterSystem::Body& b : water.bodies()) {
        const glm::vec3 mid = (b.min + b.max) * 0.5f;
        pools.push_back({b.min, glm::vec3(b.max.x, water.surface(b, mid.x, mid.z), b.max.z)});
    }
    m_gpu->setPools(pools);
    budget(scene, res.count, dt);
    m_gpu->setMaxAge(m_maxAge);
    {
        std::vector<glm::vec2> params;
        for (const Fluid& f : m_fluids) params.push_back({f.viscosity, f.tension});
        m_gpu->setFluids(params);
    }
    m_prof[4] = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t1).count();

    if (!m_sources.empty() || res.count > 0 || !m_spray.empty()) {
        dt = std::min(dt, 1.0f / 30.0f);
        const int steps = std::clamp((int)std::ceil(dt * 90.0f), 1, 3);
        const float h = dt / steps;
        const glm::vec3 g(0.0f, -scene.world().gravity, 0.0f);
        for (int s = 0; s < steps; ++s) {
            m_incoming.clear();
            const size_t known = m_gpu->results().count;
            emit(h, known < m_cap ? m_cap - known : 0, &m_incoming);
            m_gpu->step(h, g, m_incoming);
        }
    }
    m_gpu->endFrame(m_probeAsk);
    m_probeSent = m_probeAsk;
    m_probeAsk.clear();
    pushThings(dt, scene);
    m_draw.clear();
    m_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

size_t Liquid::count() const { return m_gpu ? m_gpu->results().count : m_x.size(); }
unsigned Liquid::gpuDrawBuffer() const { return m_gpu ? m_gpu->drawBuffer() : 0; }
unsigned Liquid::gpuCommandBuffer() const { return m_gpu ? m_gpu->commandBuffer() : 0; }
unsigned Liquid::gpuDrawCommandOffset() { return LiquidGpu::kDrawCommandOffset; }

Liquid::Stats Liquid::stats() const {
    Stats st;
    if (m_gpu) {
        const LiquidGpu::Results& r = m_gpu->results();
        st.count = r.count; st.lo = r.lo; st.hi = r.hi; st.fastest = r.fastest;
        return st;
    }
    st.count = m_x.size();
    if (m_x.empty()) return st;
    st.lo = glm::vec3(1e30f); st.hi = glm::vec3(-1e30f);
    for (size_t i = 0; i < m_x.size(); ++i) {
        st.lo = glm::min(st.lo, m_x[i]); st.hi = glm::max(st.hi, m_x[i]);
        st.fastest = std::max(st.fastest, glm::length(m_v[i]));
    }
    return st;
}

std::vector<glm::vec4> Liquid::debugDrops() {
    if (m_gpu) return m_gpu->readDrops();
    std::vector<glm::vec4> out;
    for (size_t i = 0; i < m_x.size(); ++i) out.push_back(glm::vec4(m_x[i], glm::length(m_v[i])));
    return out;
}

namespace {
// Eigen-decomposition of a symmetric 3x3 matrix (the analytic way): its smallest
// and largest eigenvalues, and the eigenvector of the smallest.
void smallestEigen(const glm::mat3& C, glm::vec3& vec, float& lmin, float& lmax) {
    const float p1 = C[0][1] * C[0][1] + C[0][2] * C[0][2] + C[1][2] * C[1][2];
    const float q = (C[0][0] + C[1][1] + C[2][2]) / 3.0f;
    const float p2 = (C[0][0] - q) * (C[0][0] - q) + (C[1][1] - q) * (C[1][1] - q) + (C[2][2] - q) * (C[2][2] - q) + 2.0f * p1;
    const float p = std::sqrt(p2 / 6.0f);
    if (p < 1e-9f) { vec = glm::vec3(0, 1, 0); lmin = lmax = q; return; }
    const glm::mat3 B = (C - q * glm::mat3(1.0f)) * (1.0f / p);
    const float r = glm::clamp(glm::determinant(B) * 0.5f, -1.0f, 1.0f);
    const float phi = std::acos(r) / 3.0f;
    lmax = q + 2.0f * p * std::cos(phi);
    lmin = q + 2.0f * p * std::cos(phi + 2.0943951f);
    const glm::mat3 A = C - lmin * glm::mat3(1.0f);
    const glm::vec3 r0(A[0][0], A[1][0], A[2][0]), r1(A[0][1], A[1][1], A[2][1]), r2(A[0][2], A[1][2], A[2][2]);
    glm::vec3 c0 = glm::cross(r0, r1), c1 = glm::cross(r0, r2), c2 = glm::cross(r1, r2);
    float d0 = glm::dot(c0, c0), d1 = glm::dot(c1, c1), d2 = glm::dot(c2, c2);
    vec = d0 >= d1 && d0 >= d2 ? c0 : d1 >= d2 ? c1 : c2;
    const float len = glm::length(vec);
    vec = len > 1e-12f ? vec / len : glm::vec3(0, 1, 0);
}
} // namespace

glm::vec4 Liquid::dropShape(const glm::vec3& center, const std::vector<glm::vec3>& near, glm::vec3& smoothed) {
    smoothed = center;
    if (near.size() < 4) return glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);   // on its own: a round droplet
    // Neighbours weighted by closeness: their middle, and how they're spread (covariance).
    float wsum = 1.0f;
    glm::vec3 mean = center;
    for (const glm::vec3& q : near) {
        const float t = glm::length(q - center) / kH;
        const float w = 1.0f - t * t * t;
        mean += q * w;
        wsum += w;
    }
    mean /= wsum;
    glm::mat3 C(0.0f);
    for (const glm::vec3& q : near) {
        const float t = glm::length(q - center) / kH;
        const float w = 1.0f - t * t * t;
        const glm::vec3 d = q - mean;
        C += w * glm::outerProduct(d, d);
    }
    C /= wsum;
    glm::vec3 axis;
    float lmin, lmax;
    smallestEigen(C, axis, lmin, lmax);
    // Round inside the liquid; flat where the neighbours lie in a sheet (a surface, a
    // film on the floor). Drawn a little towards its neighbours, which smooths the surface.
    const float flat = glm::clamp(std::sqrt(std::max(lmin, 0.0f) / std::max(lmax, 1e-8f)) * 1.6f, 0.25f, 1.0f);
    smoothed = glm::mix(center, mean, 0.85f);
    return glm::vec4(axis, flat);
}

bool Liquid::isWet(uint64_t partId) const {
    auto it = m_wetUntil.find(partId);
    return it != m_wetUntil.end() && it->second > m_time;
}

void Liquid::budget(Scene& scene, size_t count, float dt) {
    const size_t most = m_gpu ? (size_t)LiquidGpu::maxCapacity() : (size_t)kMaxDrops;
    m_cap = std::min({most, (size_t)std::max(0, scene.world().maxFluidParticles),
                      (size_t)GraphicsSettings::get().waterMaxDrops()});
    // Full (or nearly) while something is still pouring: the oldest drops go, a little
    // sooner each frame, so new liquid keeps coming and the game stays smooth.
    if (!m_sources.empty() && m_cap > 0 && count + count / 50 >= m_cap)
        m_maxAge = std::max(1.5f, m_maxAge * (1.0f - 1.5f * dt));
    else
        m_maxAge = std::min(120.0f, m_maxAge + 10.0f * dt);
}
