// Rigid-body physics for unanchored parts, plus constraints (ropes, rods,
// springs, welds, hinges with motors, ball sockets, sliders and slide-and-spin joints).
//
// How it works, in short:
//  * every unanchored part becomes a "body" with mass, velocity and spin;
//    anchored parts are bodies that never move
//  * boxes and balls that overlap produce contact points
//  * a "sequential impulse" solver nudges velocities, over and over, until
//    contacts stop pushing into each other and constraints are satisfied
//  * movers (BodyVelocity, AlignPosition...) push whole welded assemblies
//  * finally positions and rotations move by the velocities
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Water.h"
#include "Movers.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <unordered_map>

namespace {

struct Body {
    SceneNode* node = nullptr;
    bool       dynamic = false;
    bool       sphere = false;
    bool       solid = true;
    int        group = 0;       // collision group slot (the terrain is Default's)
    glm::vec3  pos{0.0f};
    glm::quat  rot{1, 0, 0, 0};
    glm::vec3  half{0.5f};
    glm::vec3  scale{1.0f};
    float      radius = 0.5f;
    glm::vec3  v{0.0f}, w{0.0f};
    float      invMass = 0.0f;
    glm::vec3  invInertiaLocal{0.0f};
    float      friction = 0.5f, elasticity = 0.3f;
    float      frictionWeight = 1.0f, elasticityWeight = 1.0f;   // CustomPhysicalProperties' weights
    bool       awake = true;
    glm::vec3  aabbMin{0.0f}, aabbMax{0.0f};

    glm::mat3 R() const { return glm::mat3_cast(rot); }
    glm::mat3 invInertia() const {
        glm::mat3 r = R();
        return r * glm::mat3(glm::vec3(invInertiaLocal.x, 0, 0), glm::vec3(0, invInertiaLocal.y, 0),
                             glm::vec3(0, 0, invInertiaLocal.z)) * glm::transpose(r);
    }
    OBB obb() const {
        OBB o;
        glm::mat3 r = R();
        o.center = pos;
        o.axis[0] = r[0]; o.axis[1] = r[1]; o.axis[2] = r[2];
        o.half = half;
        return o;
    }
    glm::vec3 velocityAt(const glm::vec3& p) const { return v + glm::cross(w, p - pos); }
};

struct Contact {
    int a, b;               // a is pushed along +n
    glm::vec3 p, n;
    float depth;
    glm::vec3 ra, rb, t1, t2;
    float kN, kT1, kT2, bias, jn = 0, jt1 = 0, jt2 = 0, mu;
};

void applyImpulse(Body& b, const glm::vec3& r, const glm::vec3& j) {
    if (!b.dynamic) return;
    b.v += j * b.invMass;
    b.w += b.invInertia() * glm::cross(r, j);
}

float effMass(const Body& a, const Body& b, const glm::vec3& ra, const glm::vec3& rb, const glm::vec3& d) {
    float k = a.invMass + b.invMass;
    if (a.dynamic) k += glm::dot(d, glm::cross(a.invInertia() * glm::cross(ra, d), ra));
    if (b.dynamic) k += glm::dot(d, glm::cross(b.invInertia() * glm::cross(rb, d), rb));
    return k > 1e-9f ? k : 1e-9f;
}

// Contacts are found afresh every step; this names a pair of touching parts, so
// how hard their contacts pushed last step can be found again.
uint64_t pairKey(const Body& A, const Body& B) {
    const uint64_t a = A.node ? A.node->id : 0, b = B.node ? B.node->id : 0;
    uint64_t k = a * 0x9E3779B97F4A7C15ull;
    return k ^ (b + 0x632BE59BD9B4E019ull + (k << 6) + (k >> 2));
}

// Where a ray from `from` along `dir` first enters a box grown by `grow` on every
// side (false if it starts inside, misses, or gets there after `maxT`).
bool rayIntoBox(const glm::vec3& from, const glm::vec3& dir, float maxT, const OBB& o, float grow,
                float& t, glm::vec3& normal) {
    const glm::vec3 rel = from - o.center;
    float enter = -1e30f, leave = 1e30f;
    int enterAxis = -1;
    float enterSign = 1.0f;
    for (int i = 0; i < 3; ++i) {
        const float p = glm::dot(rel, o.axis[i]), d = glm::dot(dir, o.axis[i]), h = o.half[i] + grow;
        if (std::abs(d) < 1e-8f) {
            if (std::abs(p) > h) return false;
            continue;
        }
        float t0 = (-h - p) / d, t1 = (h - p) / d;
        float sign = -1.0f;                         // (entering the -side face)
        if (t0 > t1) { std::swap(t0, t1); sign = 1.0f; }
        if (t0 > enter) { enter = t0; enterAxis = i; enterSign = sign; }
        leave = std::min(leave, t1);
        if (enter > leave) return false;
    }
    if (enterAxis < 0 || enter < 0.0f || enter > maxT) return false;
    t = enter;
    normal = o.axis[enterAxis] * enterSign;
    return true;
}

// --- contact generation -------------------------------------------------------

void boxCorners(const OBB& o, glm::vec3 out[8]) {
    for (int i = 0; i < 8; ++i)
        out[i] = o.center + o.axis[0] * o.half.x * ((i & 1) ? 1.f : -1.f) +
                 o.axis[1] * o.half.y * ((i & 2) ? 1.f : -1.f) + o.axis[2] * o.half.z * ((i & 4) ? 1.f : -1.f);
}

bool inside(const OBB& o, const glm::vec3& p, float tol) {
    glm::vec3 d = p - o.center;
    for (int i = 0; i < 3; ++i)
        if (std::abs(glm::dot(d, o.axis[i])) > o.half[i] + tol) return false;
    return true;
}

glm::vec3 support(const OBB& o, const glm::vec3& dir) {
    glm::vec3 p = o.center;
    for (int i = 0; i < 3; ++i) p += o.axis[i] * o.half[i] * (glm::dot(dir, o.axis[i]) >= 0 ? 1.f : -1.f);
    return p;
}

// Face against face (the usual case: a box lying on another, or on the floor): the
// touching patch is the other box's face cut down to fit inside this one's face,
// so the contacts are the patch's corners. Corners sitting right on an edge don't
// flicker in and out from step to step, which is what made stacks shuffle.
bool faceContacts(int ia, int ib, const OBB& a, const OBB& b, const glm::vec3& n, float depth,
                  std::vector<Contact>& out) {
    // The reference face is the one `n` comes straight out of: B's face towards A,
    // else A's face towards B.
    const OBB* ref = nullptr; const OBB* inc = nullptr;
    glm::vec3 rn; int axis = -1;
    for (int i = 0; i < 3 && axis < 0; ++i)
        if (std::abs(glm::dot(n, b.axis[i])) > 0.9999f) { ref = &b; inc = &a; rn = n; axis = i; }
    for (int i = 0; i < 3 && axis < 0; ++i)
        if (std::abs(glm::dot(n, a.axis[i])) > 0.9999f) { ref = &a; inc = &b; rn = -n; axis = i; }
    if (axis < 0) return false;                     // (edge against edge)
    const int u = (axis + 1) % 3, v = (axis + 2) % 3;
    const glm::vec3 refCenter = ref->center + rn * ref->half[axis];

    // The incident face: the other box's face that points most against `rn`.
    int k = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(glm::dot(inc->axis[i], rn)) > std::abs(glm::dot(inc->axis[k], rn))) k = i;
    const float s = glm::dot(inc->axis[k], rn) > 0.0f ? -1.0f : 1.0f;
    const int ku = (k + 1) % 3, kv = (k + 2) % 3;
    const glm::vec3 fc = inc->center + inc->axis[k] * (inc->half[k] * s);
    const glm::vec3 eu = inc->axis[ku] * inc->half[ku], ev = inc->axis[kv] * inc->half[kv];
    std::vector<glm::vec3> poly{fc + eu + ev, fc - eu + ev, fc - eu - ev, fc + eu - ev}, next;

    // Cut it by the four sides of the reference face.
    for (int side = 0; side < 4; ++side) {
        const glm::vec3 dir = ref->axis[side < 2 ? u : v] * (side % 2 ? -1.0f : 1.0f);
        const float lim = ref->half[side < 2 ? u : v] + glm::dot(ref->center, dir);
        next.clear();
        for (size_t i = 0; i < poly.size(); ++i) {
            const glm::vec3 &p = poly[i], &q = poly[(i + 1) % poly.size()];
            const float dp = glm::dot(p, dir) - lim, dq = glm::dot(q, dir) - lim;
            if (dp <= 0.0f) next.push_back(p);
            if ((dp < 0.0f) != (dq < 0.0f) && std::abs(dp - dq) > 1e-9f) next.push_back(p + (q - p) * (dp / (dp - dq)));
        }
        poly.swap(next);
        if (poly.empty()) return false;
    }
    const size_t before = out.size();
    for (size_t i = 0; i < poly.size(); ++i) {
        const float sep = glm::dot(poly[i] - refCenter, rn);
        if (sep > 0.0f) continue;                   // (that bit of the face isn't touching)
        bool twice = false;                         // (a corner the cut landed right on)
        for (size_t j = before; j < out.size(); ++j)
            if (glm::length(out[j].p - (poly[i] - rn * (sep * 0.5f))) < 0.01f) twice = true;
        if (twice) continue;
        // On the reference face, halfway between the two surfaces.
        out.push_back({ia, ib, poly[i] - rn * (sep * 0.5f), n, std::min(-sep, depth)});
    }
    return out.size() > before;
}

void boxBox(int ia, int ib, const Body& A, const Body& B, std::vector<Contact>& out) {
    OBB a = A.obb(), b = B.obb();
    glm::vec3 n; float depth;
    if (!Physics::obbOverlap(a, b, n, depth)) return;
    if (faceContacts(ia, ib, a, b, n, depth, out)) return;
    glm::vec3 ca[8], cb[8];
    boxCorners(a, ca);
    boxCorners(b, cb);
    size_t before = out.size();
    // Each corner is as deep as it really is (not all as deep as the deepest), so a
    // box tipped onto one corner isn't shoved off it, and a stack doesn't jiggle.
    const float topB = glm::dot(support(b, n), n), bottomA = glm::dot(support(a, -n), n);
    for (const glm::vec3& p : ca)
        if (inside(b, p, 0.02f)) out.push_back({ia, ib, p, n, std::clamp(topB - glm::dot(p, n), 0.0f, depth)});
    for (const glm::vec3& p : cb)
        if (inside(a, p, 0.02f)) out.push_back({ia, ib, p, n, std::clamp(glm::dot(p, n) - bottomA, 0.0f, depth)});
    if (out.size() == before) {                     // edge against edge
        glm::vec3 p = (support(a, -n) + support(b, n)) * 0.5f;
        out.push_back({ia, ib, p, n, depth});
    }
}

void sphereBox(int is, int ib, const Body& S, const Body& B, std::vector<Contact>& out, bool sphereIsA) {
    OBB o = B.obb();
    glm::vec3 rel = S.pos - o.center;
    glm::vec3 local(glm::dot(rel, o.axis[0]), glm::dot(rel, o.axis[1]), glm::dot(rel, o.axis[2]));
    glm::vec3 cl = glm::clamp(local, -o.half, o.half);
    glm::vec3 closest = o.center + o.axis[0] * cl.x + o.axis[1] * cl.y + o.axis[2] * cl.z;
    glm::vec3 d = S.pos - closest;
    float dist = glm::length(d);
    glm::vec3 n; float depth;
    if (dist > 1e-5f) {
        if (dist >= S.radius) return;
        n = d / dist;
        depth = S.radius - dist;
    } else {
        glm::vec3 room = o.half - glm::abs(local);
        int ax = room.x < room.y ? (room.x < room.z ? 0 : 2) : (room.y < room.z ? 1 : 2);
        n = o.axis[ax] * (local[ax] >= 0 ? 1.f : -1.f);
        depth = room[ax] + S.radius;
    }
    if (sphereIsA) out.push_back({is, ib, closest, n, depth});
    else           out.push_back({ib, is, closest, -n, depth});
}

void sphereSphere(int ia, int ib, const Body& A, const Body& B, std::vector<Contact>& out) {
    glm::vec3 d = A.pos - B.pos;
    float dist = glm::length(d);
    if (dist >= A.radius + B.radius) return;
    glm::vec3 n = dist > 1e-5f ? d / dist : glm::vec3(0, 1, 0);
    out.push_back({ia, ib, B.pos + n * B.radius, n, A.radius + B.radius - dist});
}

// --- building bodies from the scene ---------------------------------------------

void localBoundsFor(PrimitiveType t, glm::vec3& mn, glm::vec3& mx) {
    if (t == PrimitiveType::Plane) { mn = {-0.5f, -0.02f, -0.5f}; mx = {0.5f, 0.02f, 0.5f}; }
    else                           { mn = glm::vec3(-0.5f); mx = glm::vec3(0.5f); }
}

Body makeBody(SceneNode* n, bool dynamic) {
    Body b;
    b.node = n;
    b.dynamic = dynamic;
    b.solid = n->canCollide;
    glm::mat4 m = n->worldMatrix();
    glm::vec3 mn, mx;
    localBoundsFor(n->primitiveType, mn, mx);
    glm::mat3 r;
    for (int i = 0; i < 3; ++i) {
        glm::vec3 c(m[i]);
        float len = glm::length(c);
        b.scale[i] = len;
        r[i] = len > 1e-8f ? c / len : glm::vec3(i == 0, i == 1, i == 2);
    }
    b.pos = glm::vec3(m[3]);
    b.rot = glm::normalize(glm::quat_cast(r));
    b.half = b.scale * (mx - mn) * 0.5f;
    b.sphere = n->primitiveType == PrimitiveType::Sphere;
    b.radius = b.sphere ? (b.half.x + b.half.y + b.half.z) / 3.0f : 0.0f;

    b.friction   = Physics::frictionOf(n);
    b.elasticity = Physics::elasticityOf(n);
    b.frictionWeight   = std::max(0.0f, n->frictionWeight);
    b.elasticityWeight = std::max(0.0f, n->elasticityWeight);
    if (dynamic) {
        float density = Physics::densityOf(n);
        glm::vec3 e = b.half * 2.0f;
        float mass, ix, iy, iz;
        if (b.sphere) {
            mass = density * 4.18879f * b.radius * b.radius * b.radius;
            ix = iy = iz = 0.4f * mass * b.radius * b.radius;
        } else {
            mass = density * std::max(1e-4f, e.x * e.y * e.z);
            ix = mass / 12.0f * (e.y * e.y + e.z * e.z);
            iy = mass / 12.0f * (e.x * e.x + e.z * e.z);
            iz = mass / 12.0f * (e.x * e.x + e.y * e.y);
        }
        b.invMass = 1.0f / mass;
        b.invInertiaLocal = {1.0f / std::max(ix, 1e-6f), 1.0f / std::max(iy, 1e-6f), 1.0f / std::max(iz, 1e-6f)};
        b.v = n->velocity;
        b.w = n->angularVelocity;
        b.awake = n->sleepTime < 0.6f;
    }
    // World-space bounding box for the broad phase.
    OBB o = b.obb();
    glm::vec3 ext(0.0f);
    for (int i = 0; i < 3; ++i) ext += glm::abs(o.axis[i]) * o.half[i];
    if (b.sphere) ext = glm::vec3(b.radius);
    b.aabbMin = b.pos - ext;
    b.aabbMax = b.pos + ext;
    return b;
}

void writeBack(Body& b) {
    SceneNode* n = b.node;
    n->velocity = b.v;
    n->angularVelocity = b.w;
    glm::mat4 world = glm::translate(glm::mat4(1.0f), b.pos) * glm::mat4_cast(b.rot);
    glm::mat4 local = n->parent ? glm::inverse(n->parent->worldMatrix()) * world : world;
    glm::mat3 r(local);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    float z, y, x;
    glm::extractEulerAngleZYX(glm::mat4(r), z, y, x);
    n->transform.position = glm::vec3(local[3]);
    n->transform.rotation = glm::degrees(glm::vec3(x, y, z));
}

bool hasDynamicAncestorPart(const SceneNode* n) {
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->isPart() && !p->anchored) return true;
    return false;
}

// --- water ------------------------------------------------------------------------

// How heavy water is compared to Plastic (1.0). Wood, ice, plastic and neon
// float; glass, concrete and metal sink.
constexpr float kWaterDensity = Physics::kWaterDensity;
constexpr float kWaterDrag    = 0.6f;   // how much water slows anything moving through it (all round)
constexpr float kWaterPush    = 1.2f;   // how hard it pushes on a flat side moving through it (or a current on it)

// Floating: the body is checked at 27 points spread through it. Each point
// under the surface is pushed up by the weight of the water it pushes aside
// (Archimedes!) and slowed by the water. Because the pushes happen at the
// points, a lopsided object tips over until it floats the right way up, and
// waves rock boats.
//
// Water also pushes on the sides of things, the way it does on a paddle: a flat
// side moving through it (or facing a current) gets a big push, an edge slicing
// through gets very little. That's what turns a water wheel in a river and lets
// a paddle push a boat along. A water part's "Drag" makes it thicker or thinner.
void floatIn(Body& b, WaterSystem& water, float g, float h, Scene& scene) {
    if (!water.maybeWet(b.aabbMin, b.aabbMax)) return;

    constexpr int N = 3;
    glm::vec3 pts[N * N * N];
    int count = 0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j)
            for (int k = 0; k < N; ++k) {
                glm::vec3 f = (glm::vec3((float)i, (float)j, (float)k) + 0.5f) / (float)N * 2.0f - 1.0f;
                if (b.sphere) {
                    if (glm::length(f) > 1.0f) continue;
                    pts[count++] = f * b.radius;
                } else {
                    pts[count++] = f * b.half;
                }
            }
    const float volume = b.sphere ? 4.18879f * b.radius * b.radius * b.radius : 8.0f * b.half.x * b.half.y * b.half.z;
    const float mass = 1.0f / b.invMass;
    const float layer = std::max(0.05f, (b.aabbMax.y - b.aabbMin.y) / N);   // how thick each point's slice is
    const glm::mat3 R = b.R();
    // The area of each pair of sides (facing x, y, z in the body's own space).
    const glm::vec3 area = b.sphere ? glm::vec3(3.14159f * b.radius * b.radius)
                                    : glm::vec3(4.0f * b.half.y * b.half.z, 4.0f * b.half.x * b.half.z, 4.0f * b.half.x * b.half.y);
    const float thick = water.dragAt(b.pos);
    float submerged = 0.0f;
    for (int n = 0; n < count; ++n) {
        glm::vec3 p = b.pos + R * pts[n];
        float surface;
        glm::vec3 flow;
        if (!water.at(p - glm::vec3(0.0f, layer * 0.5f, 0.0f), &surface, &flow)) continue;
        float frac = std::clamp((surface - p.y) / layer + 0.5f, 0.0f, 1.0f);
        if (frac <= 0.0f) continue;
        float share = frac / count;
        float waterMass = kWaterDensity * volume * share;
        glm::vec3 imp(0.0f, waterMass * g * h, 0.0f);
        glm::vec3 vrel = b.velocityAt(p) - flow;
        const float cap = 0.5f * mass / count;   // never more than it takes to stop this point (no wobbling)
        imp -= vrel * std::min(kWaterDrag * thick * h * waterMass, cap);
        // The push on its sides, in the body's own directions: big for a flat side, small for an edge.
        const glm::vec3 local = glm::transpose(R) * vrel;
        glm::vec3 side(0.0f);
        for (int i = 0; i < 3; ++i) {
            const float k = kWaterPush * thick * kWaterDensity * area[i] * frac / count * (0.5f + std::abs(local[i])) * h;
            side[i] = -local[i] * std::min(k, cap);
        }
        imp += R * side;
        applyImpulse(b, p - b.pos, imp);
        submerged += share;
    }
    if (submerged <= 0.0f) return;
    b.awake = true;
    b.w *= std::max(0.0f, 1.0f - 1.2f * h * submerged);
    glm::vec3 ext = b.aabbMax - b.aabbMin;
    water.touching(scene, b.node->id, b.pos, -b.v.y, std::cbrt(ext.x * ext.y * ext.z));
    // Bobbing at the surface, or moving along it, makes ripples.
    if (submerged < 0.95f) {
        float push = -b.v.y * h * 0.25f + glm::length(glm::vec2(b.v.x, b.v.z)) * h * 0.05f;
        if (std::abs(push) > 1e-4f) water.disturb(b.pos, std::clamp(push, -0.05f, 0.05f), std::max(ext.x, ext.z) * 0.6f);
    }
}

// --- joints ------------------------------------------------------------------------

struct Joint {
    SceneNode*     node;
    ConstraintType type;
    int            a = -1, b = -1;          // bodies
    glm::vec3      la{0.0f}, lb{0.0f};      // anchors in each body's own space
    glm::vec3      axisA{1, 0, 0}, axisB{1, 0, 0};   // hinge axes in body space
    glm::quat      rel{1, 0, 0, 0};         // weld / prismatic: B's rotation relative to A
    glm::quat      qa{1, 0, 0, 0}, qb{1, 0, 0, 0};   // each attachment's turn in its body's own space
    float          length = 0.0f;
    float          accMotor = 0.0f;
    glm::vec3      imp{0.0f};               // impulse it has given this substep (for BreakForce)
    float          peak = 0.0f;             // the hardest it was pulled this step (force)
    // Ball socket / prismatic / cylindrical: the impulse each limit or actuator row
    // has added up this substep (so limits only push, and actuators stay under
    // their force), and how fast a limit row bounces back (Restitution).
    enum Row { LinLow, LinHigh, LinMotor, AngLow, AngHigh, AngMotor, Cone, kRows };
    float          acc[kRows] = {};
    float          bounce[kRows] = {};
};

// --- rows of the ball-socket / prismatic / cylindrical joints ------------------------
// Each row nudges the two bodies' velocities so that one relative speed reaches
// `target`; the impulse it has added up (`acc`) stays between lo and hi.

constexpr float kFree = 1e30f;

void linearRow(Body& A, Body& B, const glm::vec3& ra, const glm::vec3& rb, const glm::vec3& dir,
               float target, float& acc, float lo, float hi) {
    const float cdot = glm::dot((B.v + glm::cross(B.w, rb)) - (A.v + glm::cross(A.w, ra)), dir);
    float lambda = (target - cdot) / effMass(A, B, ra, rb, dir);
    const float old = acc;
    acc = std::clamp(old + lambda, lo, hi);
    lambda = acc - old;
    applyImpulse(A, ra, -dir * lambda);
    applyImpulse(B, rb, dir * lambda);
}

void angularRow(Body& A, Body& B, const glm::vec3& u, float target, float& acc, float lo, float hi) {
    const glm::mat3 IA = A.dynamic ? A.invInertia() : glm::mat3(0.0f);
    const glm::mat3 IB = B.dynamic ? B.invInertia() : glm::mat3(0.0f);
    const float k = glm::dot(u, IA * u) + glm::dot(u, IB * u);
    if (k < 1e-9f) return;
    float lambda = (target - glm::dot(B.w - A.w, u)) / k;
    const float old = acc;
    acc = std::clamp(old + lambda, lo, hi);
    lambda = acc - old;
    if (A.dynamic) A.w -= IA * u * lambda;
    if (B.dynamic) B.w += IB * u * lambda;
}

// A turning row for a ball socket's limits. The bodies turn about the joint's point
// (which the point rows hold still), not about their own middles: a pendulum's bob
// is much harder to swing round the pivot than to spin in place, and solving it as
// a spin would barely slow the swing down.
glm::mat3 invInertiaAbout(const Body& b, const glm::vec3& r) {
    if (!b.dynamic) return glm::mat3(0.0f);
    const glm::mat3 R = b.R();
    const glm::vec3 il = 1.0f / glm::max(b.invInertiaLocal, glm::vec3(1e-12f));
    glm::mat3 I = R * glm::mat3(glm::vec3(il.x, 0, 0), glm::vec3(0, il.y, 0), glm::vec3(0, 0, il.z)) * glm::transpose(R);
    I += (1.0f / b.invMass) * (glm::dot(r, r) * glm::mat3(1.0f) - glm::outerProduct(r, r));
    return glm::inverse(I);
}

void pivotRow(Body& A, Body& B, const glm::vec3& ra, const glm::vec3& rb, const glm::vec3& u,
              float target, float& acc, float lo, float hi) {
    const glm::mat3 KA = invInertiaAbout(A, ra), KB = invInertiaAbout(B, rb);
    const float k = glm::dot(u, KA * u) + glm::dot(u, KB * u);
    if (k < 1e-9f) return;
    float lambda = (target - glm::dot(B.w - A.w, u)) / k;
    const float old = acc;
    acc = std::clamp(old + lambda, lo, hi);
    lambda = acc - old;
    if (A.dynamic) { const glm::vec3 dw = -(KA * u) * lambda; A.w += dw; A.v -= glm::cross(dw, ra); }
    if (B.dynamic) { const glm::vec3 dw = (KB * u) * lambda;  B.w += dw; B.v -= glm::cross(dw, rb); }
}

// The speed a one-sided limit row aims for, given how far inside the limit it is
// (gap > 0: inside; < 0: past it). Inside, it may close the gap in one step and no
// faster; past it, it's pushed back. `bounce` (from Restitution) kicks it back out.
float limitTarget(float gap, float h, float bounce) {
    const float t = gap >= 0.0f ? -gap / h : -0.2f / h * gap;
    return gap < 0.05f && bounce > 0.0f ? std::max(t, bounce) : t;
}

float wrapAngle(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// How far B's attachment has turned around the cylindrical joint's spin axis (radians).
glm::vec3 spinAxisLocal(const JointProps& p) {
    const float i = glm::radians(p.inclinationAngle);
    return {std::cos(i), std::sin(i), 0.0f};
}
float spinAngle(const glm::quat& fa, const glm::quat& fb, const JointProps& p) {
    const float i = glm::radians(p.inclinationAngle);
    const glm::vec3 ref(-std::sin(i), std::cos(i), 0.0f);   // (at right angles to the spin axis)
    const glm::vec3 axis = fa * spinAxisLocal(p), ra = fa * ref, rb = fb * ref;
    return std::atan2(glm::dot(glm::cross(ra, rb), axis), glm::dot(ra, rb));
}

// One solver pass over a ball-socket, prismatic or cylindrical joint.
void solveJointProps(Joint& j, Body& A, Body& B, float h, bool firstPass) {
    const JointProps& p = j.node->joint;
    const float beta = 0.2f / h;
    const glm::vec3 ra = A.rot * j.la, rb = B.rot * j.lb;
    const glm::vec3 pa = A.pos + ra, pb = B.pos + rb;
    const glm::quat fa = A.rot * j.qa, fb = B.rot * j.qb;   // the attachments' frames
    const glm::vec3 x = fa * glm::vec3(1, 0, 0);
    float dummy = 0.0f;
    // A limit row's bounce speed is worked out once per substep, from how fast it hit.
    auto bounceFor = [&](Joint::Row r, float approach) {
        if (firstPass) j.bounce[r] = approach < -0.5f ? -p.restitution * approach : 0.0f;
        return j.bounce[r];
    };

    if (j.type == ConstraintType::BallSocket) {
        // The two points stay together...
        for (int ax = 0; ax < 3; ++ax) {
            glm::vec3 e(0.0f);
            e[ax] = 1.0f;
            dummy = 0.0f;
            linearRow(A, B, ra, rb, e, -beta * glm::dot(pb - pa, e), dummy, -kFree, kFree);
        }
        // ...Attachment1's axis leans no further than UpperAngle from Attachment0's...
        const glm::vec3 xb = fb * glm::vec3(1, 0, 0);
        if (p.limitsEnabled) {
            const glm::vec3 c = glm::cross(x, xb);
            const float s = glm::length(c);
            if (s > 1e-5f) {
                const glm::vec3 u = -c / s;   // turning B this way leans it back in
                const float angle = std::atan2(s, glm::dot(x, xb));
                const float gap = glm::radians(std::clamp(p.upperAngle, 0.0f, 180.0f)) - angle;
                const float b = bounceFor(Joint::Cone, glm::dot(B.w - A.w, u));
                pivotRow(A, B, ra, rb, u, limitTarget(gap, h, b), j.acc[Joint::Cone], 0.0f, kFree);
            }
        }
        // ...and twists only between TwistLowerAngle and TwistUpperAngle.
        if (p.twistLimitsEnabled) {
            glm::quat q = glm::inverse(fa) * fb;
            if (q.w < 0.0f) q = -q;
            const float twist = 2.0f * std::atan2(q.x, q.w);
            glm::vec3 axis = x + xb;
            axis = glm::length(axis) > 1e-4f ? glm::normalize(axis) : xb;
            const float lo = glm::radians(p.twistLowerAngle), hi = glm::radians(p.twistUpperAngle);
            const float w = glm::dot(B.w - A.w, axis);
            pivotRow(A, B, ra, rb, axis, limitTarget(twist - lo, h, bounceFor(Joint::AngLow, w)), j.acc[Joint::AngLow], 0.0f, kFree);
            pivotRow(A, B, ra, rb, -axis, limitTarget(hi - twist, h, bounceFor(Joint::AngHigh, -w)), j.acc[Joint::AngHigh], 0.0f, kFree);
        }
        return;
    }

    // Prismatic / cylindrical: B's point stays on the line through A's point along
    // the axis (the rows act at B's point, so A's lever arm reaches over to it)...
    const glm::vec3 raB = pb - A.pos;
    const glm::vec3 d = pb - pa;
    const glm::vec3 y = fa * glm::vec3(0, 1, 0), z = fa * glm::vec3(0, 0, 1);
    for (const glm::vec3& e : {y, z}) {
        dummy = 0.0f;
        linearRow(A, B, raB, rb, e, -beta * glm::dot(d, e), dummy, -kFree, kFree);
    }
    // ...it slides only between LowerLimit and UpperLimit...
    const float s = glm::dot(d, x);
    const float slideV = glm::dot(B.velocityAt(pb) - A.velocityAt(pb), x);
    if (p.limitsEnabled) {
        const float lo = std::min(p.lowerLimit, p.upperLimit), hi = std::max(p.lowerLimit, p.upperLimit);
        linearRow(A, B, raB, rb, x, limitTarget(s - lo, h, bounceFor(Joint::LinLow, slideV)), j.acc[Joint::LinLow], 0.0f, kFree);
        linearRow(A, B, raB, rb, -x, limitTarget(hi - s, h, bounceFor(Joint::LinHigh, -slideV)), j.acc[Joint::LinHigh], 0.0f, kFree);
    }
    // ...pushed along by its motor or servo...
    if (p.actuator == Actuator::Motor && p.motorMaxForce > 0.0f) {
        const float most = p.motorMaxForce * h;
        linearRow(A, B, raB, rb, x, p.velocity, j.acc[Joint::LinMotor], -most, most);
    } else if (p.actuator == Actuator::Servo && p.servoMaxForce > 0.0f) {
        const float most = p.servoMaxForce * h;
        const float want = std::clamp((p.targetPosition - s) * 0.5f / h, -std::abs(p.speed), std::abs(p.speed));
        linearRow(A, B, raB, rb, x, want, j.acc[Joint::LinMotor], -most, most);
    }

    // ...and turning: a prismatic joint doesn't turn at all; a cylindrical one only
    // spins around its spin axis.
    if (j.type == ConstraintType::Prismatic) {
        glm::quat err = B.rot * glm::inverse(A.rot * j.rel);
        if (err.w < 0) err = -err;
        const glm::vec3 e = 2.0f * glm::vec3(err.x, err.y, err.z);
        for (int ax = 0; ax < 3; ++ax) {
            glm::vec3 u(0.0f);
            u[ax] = 1.0f;
            dummy = 0.0f;
            angularRow(A, B, u, -beta * glm::dot(e, u), dummy, -kFree, kFree);
        }
        return;
    }
    const glm::vec3 axA = fa * spinAxisLocal(p), axB = fb * spinAxisLocal(p);
    const glm::vec3 e = glm::cross(axA, axB);
    const glm::vec3 p1 = std::abs(axA.x) < 0.9f ? glm::normalize(glm::cross(axA, glm::vec3(1, 0, 0)))
                                                : glm::normalize(glm::cross(axA, glm::vec3(0, 1, 0)));
    const glm::vec3 p2 = glm::cross(axA, p1);
    for (const glm::vec3& u : {p1, p2}) {
        dummy = 0.0f;
        angularRow(A, B, u, -beta * glm::dot(e, u), dummy, -kFree, kFree);
    }
    const float angle = spinAngle(fa, fb, p);
    const float spinV = glm::dot(B.w - A.w, axA);
    if (p.angularLimitsEnabled) {
        const float lo = glm::radians(std::min(p.lowerAngle, p.upperAngle)), hi = glm::radians(std::max(p.lowerAngle, p.upperAngle));
        angularRow(A, B, axA, limitTarget(angle - lo, h, bounceFor(Joint::AngLow, spinV)), j.acc[Joint::AngLow], 0.0f, kFree);
        angularRow(A, B, -axA, limitTarget(hi - angle, h, bounceFor(Joint::AngHigh, -spinV)), j.acc[Joint::AngHigh], 0.0f, kFree);
    }
    if (p.angularActuator == Actuator::Motor && p.motorMaxTorque > 0.0f) {
        const float most = p.motorMaxTorque * h;
        angularRow(A, B, axA, p.angularVelocity, j.acc[Joint::AngMotor], -most, most);
    } else if (p.angularActuator == Actuator::Servo && p.servoMaxTorque > 0.0f) {
        const float most = p.servoMaxTorque * h;
        const float err = wrapAngle(glm::radians(p.targetAngle) - angle);
        const float want = std::clamp(err * 0.5f / h, -std::abs(p.angularSpeed), std::abs(p.angularSpeed));
        angularRow(A, B, axA, want, j.acc[Joint::AngMotor], -most, most);
    }
}

// A mover (BodyVelocity, AlignOrientation...) and the body it pushes.
struct ActiveMover {
    SceneNode* node;
    int        body;
    SceneNode* att0 = nullptr;   // constraint movers: Attachment0 / Attachment1
    SceneNode* att1 = nullptr;
};

// Parts welded together move as one: an "assembly". Movers push the whole thing.
struct Assembly {
    std::vector<int> bodies;
    float     mass = 0.0f;
    float     inertia = 0.0f;   // (one number: how hard it is to spin, roughly, any way round)
    glm::vec3 center{0.0f};
};

glm::vec3 assemblyVelocity(const std::vector<Body>& bodies, const Assembly& a) {
    glm::vec3 p(0.0f);
    for (int i : a.bodies) p += bodies[i].v / bodies[i].invMass;
    return a.mass > 0.0f ? p / a.mass : glm::vec3(0.0f);
}

void pushAssembly(std::vector<Body>& bodies, const Assembly& a, const glm::vec3& dv) {
    for (int i : a.bodies) bodies[i].v += dv;
}

void spinAssembly(std::vector<Body>& bodies, const Assembly& a, const glm::vec3& dw) {
    for (int i : a.bodies) {
        bodies[i].w += dw;
        bodies[i].v += glm::cross(dw, bodies[i].pos - a.center);   // (spinning about the middle)
    }
}

// One step (h seconds) of a mover on its assembly.
void applyMover(const ActiveMover& am, std::vector<Body>& bodies, const Assembly& a, float h) {
    using namespace Movers;
    const MoverProps& m = am.node->mover;
    Body& B = bodies[am.body];
    const float M = std::max(a.mass, 1e-4f), I = std::max(a.inertia, 1e-4f);
    const glm::quat attRot = am.att0 ? worldRot(am.att0) : B.rot;
    const glm::vec3 attPos = am.att0 ? glm::vec3(am.att0->worldMatrix()[3]) : B.pos;
    const float huge = 1e30f;
    switch (m.type) {
        case MoverType::BodyForce:
            pushAssembly(bodies, a, m.value * h / M);
            break;
        case MoverType::BodyThrust:   // pushes along the part's own directions, at Location
            applyImpulse(B, B.rot * m.location, (B.rot * m.value) * h);
            break;
        case MoverType::VectorForce: {
            const glm::vec3 f = m.relativeToAttachment ? attRot * m.value : m.value;
            applyImpulse(B, m.atCenterOfMass ? glm::vec3(0.0f) : attPos - B.pos, f * h);
            break;
        }
        case MoverType::Torque: {
            const glm::vec3 t = m.relativeToAttachment ? attRot * m.value : m.value;
            spinAssembly(bodies, a, t * h / I);
            break;
        }
        case MoverType::BodyVelocity: {   // reach Velocity on each axis it has force for
            const glm::vec3 dv = m.value - assemblyVelocity(bodies, a);
            pushAssembly(bodies, a, clampAxes(dv, m.maxAxes * h / M));
            break;
        }
        case MoverType::LinearVelocity: {
            const glm::vec3 want = m.relativeToAttachment ? attRot * m.value : m.value;
            pushAssembly(bodies, a, clampLength(want - assemblyVelocity(bodies, a), m.maxForce * h / M));
            break;
        }
        case MoverType::BodyPosition: {   // a spring to Position (P) with damping (D)
            const glm::vec3 v = assemblyVelocity(bodies, a), x = m.value - B.pos;
            glm::vec3 dv;
            for (int i = 0; i < 3; ++i) dv[i] = springDv(x[i], v[i], m.p, m.d, M, h);
            pushAssembly(bodies, a, clampAxes(dv, m.maxAxes * h / M));
            break;
        }
        case MoverType::AlignPosition: {   // head for the target, quicker the further away
            const glm::vec3 target = am.att1 ? glm::vec3(am.att1->worldMatrix()[3]) : m.value;
            const float resp = m.rigid ? 0.5f / h : m.responsiveness;
            glm::vec3 want = (target - attPos) * resp;
            if (m.maxVelocity > 0.0f && !m.rigid) want = clampLength(want, m.maxVelocity);
            const glm::vec3 dv = want - assemblyVelocity(bodies, a);
            pushAssembly(bodies, a, clampLength(dv, (m.rigid ? huge : m.maxForce) * h / M));
            break;
        }
        case MoverType::BodyAngularVelocity:
            spinAssembly(bodies, a, clampAxes(m.value - B.w, m.maxAxes * h / I));
            break;
        case MoverType::AngularVelocity: {
            const glm::vec3 want = m.relativeToAttachment ? attRot * m.value : m.value;
            spinAssembly(bodies, a, clampLength(want - B.w, m.maxForce * h / I));
            break;
        }
        case MoverType::BodyGyro: {   // a turning spring towards CFrame
            const glm::vec3 e = rotationError(B.rot, eulerQuat(m.rotation));
            glm::vec3 dw;
            for (int i = 0; i < 3; ++i) dw[i] = springDv(e[i], B.w[i], m.p, m.d, I, h);
            spinAssembly(bodies, a, clampAxes(dw, m.maxAxes * h / I));
            break;
        }
        case MoverType::AlignOrientation: {
            const glm::quat target = am.att1 ? worldRot(am.att1) : eulerQuat(m.rotation);
            const float resp = m.rigid ? 0.5f / h : m.responsiveness;
            glm::vec3 want = rotationError(attRot, target) * resp;
            if (m.maxVelocity > 0.0f && !m.rigid) want = clampLength(want, m.maxVelocity);
            spinAssembly(bodies, a, clampLength(want - B.w, (m.rigid ? huge : m.maxForce) * h / I));
            break;
        }
    }
}

} // namespace

// ============================================================================

void Physics::stepParts(Scene& scene, float dt, std::vector<uint64_t>& fallen) {
    // ---- 1. Bodies ------------------------------------------------------------
    const CollisionGroups& groups = scene.world().collisionGroups;
    const bool useGroups = !groups.onlyDefault() || !groups.collides(0, 0);
    std::vector<Body> bodies;
    std::unordered_map<uint64_t, int> index;
    std::vector<SceneNode*> constraintNodes, moverNodes;
    struct Zone { SceneNode* node; OBB box; };
    std::vector<Zone> zones;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        // "Visible" only hides drawing for constraints; hidden parts are switched off.
        if (n->isConstraint()) { if (n->enabled) constraintNodes.push_back(n); continue; }
        if (n->isMover()) { if (n->enabled) moverNodes.push_back(n); continue; }
        if (!n->visible || n->isViewportFrame()) continue;   // (a ViewportFrame's parts don't fall)
        if (scene.isCharacterPart(n)) continue;
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->internal) continue;
        if (n->isForceZone() && n->enabled) zones.push_back({n, Physics::worldOBB(n)});
        bool dynamic = !n->anchored && !hasDynamicAncestorPart(n);
        if (!dynamic && !n->canCollide) {
            // Only matters as an anchor for constraints.
            bool used = false;
            for (auto& c : n->children) if (c->isAttachment()) used = true;
            if (!used) continue;
        }
        index[n->id] = (int)bodies.size();
        bodies.push_back(makeBody(n, dynamic));
        if (useGroups) bodies.back().group = groups.slotOf(n->collisionGroup);
    }
    bool anyDynamic = false;
    for (auto& b : bodies) if (b.dynamic) { anyDynamic = true; break; }
    if (!anyDynamic) return;
    // The terrain is one more body that never moves (it isn't in `index`, so no
    // joint uses it, and it's not "solid" so the part-vs-part loop skips it: its
    // contacts are made separately below).
    const Terrain& terrain = scene.terrain();
    const int terrainBody = terrain.empty() ? -1 : (int)bodies.size();
    if (terrainBody >= 0) {
        Body t;
        t.solid = false;
        t.friction = 0.6f;
        t.elasticity = 0.1f;
        bodies.push_back(t);
    }

    // ---- 2. Joints --------------------------------------------------------------
    auto bodyOfAttachment = [&](uint64_t attId, glm::vec3& worldPos, glm::vec3& worldAxis, glm::quat* worldTurn = nullptr) -> int {
        SceneNode* att = scene.findById(attId);
        if (!att || !att->parent) return -1;
        glm::mat4 m = att->worldMatrix();
        worldPos = glm::vec3(m[3]);
        worldAxis = glm::normalize(glm::vec3(m[0]) + glm::vec3(1e-9f));
        if (worldTurn) *worldTurn = Movers::worldRot(att);
        auto it = index.find(att->parent->id);
        return it == index.end() ? -1 : it->second;
    };
    std::vector<Joint> joints;
    std::set<std::pair<int, int>> noCollide;
    for (SceneNode* cn : constraintNodes) {
        Joint j;
        j.node = cn;
        j.type = cn->constraintType;
        glm::vec3 pa, pb, axa, axb;
        if (j.type == ConstraintType::Weld) {
            auto ia = index.find(cn->ref0), ib = index.find(cn->ref1);
            if (ia == index.end() || ib == index.end() || ia->second == ib->second) continue;
            j.a = ia->second; j.b = ib->second;
            Body &A = bodies[j.a], &B = bodies[j.b];
            if (!cn->jointReady) {
                // Remember how the two parts sit relative to each other right now.
                glm::vec3 mid = (A.pos + B.pos) * 0.5f;
                cn->jointA = glm::inverse(A.rot) * (mid - A.pos);
                cn->jointB = glm::inverse(B.rot) * (mid - B.pos);
                glm::quat q = glm::inverse(A.rot) * B.rot;
                cn->jointRot = {q.x, q.y, q.z, q.w};
                cn->jointReady = true;
            }
            j.la = cn->jointA; j.lb = cn->jointB;
            j.rel = glm::quat(cn->jointRot.w, cn->jointRot.x, cn->jointRot.y, cn->jointRot.z);
        } else {
            glm::quat ta, tb;
            j.a = bodyOfAttachment(cn->ref0, pa, axa, &ta);
            j.b = bodyOfAttachment(cn->ref1, pb, axb, &tb);
            if (j.a < 0 || j.b < 0 || j.a == j.b) continue;
            Body &A = bodies[j.a], &B = bodies[j.b];
            j.la = glm::inverse(A.rot) * (pa - A.pos);
            j.lb = glm::inverse(B.rot) * (pb - B.pos);
            j.axisA = glm::inverse(A.rot) * axa;
            j.axisB = glm::inverse(B.rot) * axb;
            j.qa = glm::inverse(A.rot) * ta;
            j.qb = glm::inverse(B.rot) * tb;
            // A prismatic joint keeps the two attachments turned the same way.
            j.rel = j.qa * glm::inverse(j.qb);
            if (!usesJointProps(j.type)) {
                if (cn->length < 0.0f) cn->length = glm::length(pb - pa);   // "as placed"
                j.length = cn->length;
            }
        }
        if (!bodies[j.a].dynamic && !bodies[j.b].dynamic) continue;
        if (j.type == ConstraintType::Weld || j.type == ConstraintType::Hinge || usesJointProps(j.type))
            noCollide.insert({std::min(j.a, j.b), std::max(j.a, j.b)});
        // A running motor keeps things awake; a connected body wakes its partner.
        const JointProps& jp = cn->joint;
        bool motorOn = (j.type == ConstraintType::Hinge && cn->motorTorque > 0.0f && cn->motorSpeed != 0.0f) ||
                       (jointSlides(j.type) && jp.actuator == Actuator::Motor && jp.motorMaxForce > 0.0f && jp.velocity != 0.0f) ||
                       (j.type == ConstraintType::Cylindrical && jp.angularActuator == Actuator::Motor &&
                        jp.motorMaxTorque > 0.0f && jp.angularVelocity != 0.0f) ||
                       // (a servo that hasn't got there yet: a script may have just moved its target)
                       (jointSlides(j.type) && jp.actuator == Actuator::Servo && jp.servoMaxForce > 0.0f &&
                        std::abs(jp.targetPosition - jp.currentPosition) > 0.01f) ||
                       (j.type == ConstraintType::Cylindrical && jp.angularActuator == Actuator::Servo &&
                        jp.servoMaxTorque > 0.0f && std::abs(wrapAngle(glm::radians(jp.targetAngle - jp.currentAngle))) > 0.01f);
        if (motorOn || bodies[j.a].awake || bodies[j.b].awake) {
            bodies[j.a].awake = bodies[j.b].awake = true;
        }
        joints.push_back(j);
    }

    // ---- 2b. Movers, and the welded assemblies they push ---------------------------
    std::vector<ActiveMover> movers;
    for (SceneNode* mn : moverNodes) {
        ActiveMover am{mn, -1};
        SceneNode* part = mn->parent;
        if (!isBodyMover(mn->mover.type)) {
            am.att0 = mn->ref0 ? scene.findById(mn->ref0) : nullptr;
            am.att1 = mn->ref1 ? scene.findById(mn->ref1) : nullptr;
            if (am.att0 && !am.att0->isAttachment()) am.att0 = nullptr;
            if (am.att1 && !am.att1->isAttachment()) am.att1 = nullptr;
            if (am.att0) part = am.att0->parent;   // (no Attachment0: the part it's in)
        }
        // Parts inside a loose part ride along with it: push that one.
        for (; part; part = part->parent) {
            auto it = index.find(part->id);
            if (it != index.end()) { am.body = it->second; break; }
        }
        if (am.body < 0 || !bodies[am.body].dynamic) continue;
        movers.push_back(am);
    }
    std::vector<int> group(bodies.size(), -1);
    std::vector<Assembly> assemblies;
    if (!movers.empty()) {
        std::vector<int> link(bodies.size());
        for (int i = 0; i < (int)link.size(); ++i) link[i] = i;
        std::function<int(int)> top = [&](int i) { return link[i] == i ? i : link[i] = top(link[i]); };
        for (const Joint& j : joints)
            if (j.type == ConstraintType::Weld && bodies[j.a].dynamic && bodies[j.b].dynamic) link[top(j.a)] = top(j.b);
        std::unordered_map<int, int> which;
        for (const ActiveMover& am : movers) {
            const int t = top(am.body);
            if (which.count(t)) continue;
            which[t] = (int)assemblies.size();
            assemblies.emplace_back();
        }
        for (int i = 0; i < (int)bodies.size(); ++i) {
            if (!bodies[i].dynamic) continue;
            auto it = which.find(top(i));
            if (it == which.end()) continue;
            group[i] = it->second;
            Assembly& a = assemblies[it->second];
            a.bodies.push_back(i);
            const float m = 1.0f / bodies[i].invMass;
            a.mass += m;
            a.center += bodies[i].pos * m;
        }
        for (Assembly& a : assemblies) {
            if (a.mass > 0.0f) a.center /= a.mass;
            for (int i : a.bodies) {
                const Body& b = bodies[i];
                const glm::vec3 il = 1.0f / b.invInertiaLocal;
                a.inertia += (il.x + il.y + il.z) / 3.0f + glm::dot(b.pos - a.center, b.pos - a.center) / b.invMass;
            }
            for (int i : a.bodies) { bodies[i].awake = true; bodies[i].node->sleepTime = 0.0f; }   // (pushed things don't nod off)
        }
    }

    const float g = scene.world().gravity;
    // Small steps (about 240 a second) with fewer solver passes each hold a tall
    // stack still far better than a couple of big steps with lots of passes.
    const int   substeps = std::clamp((int)std::ceil(dt * 240.0f - 0.01f), 2, 8);
    const float h = dt / substeps;

    for (int step = 0; step < substeps; ++step) {
        // ---- 3. Gravity + springs ----------------------------------------------
        // Water first: it wakes up what's in it, so gravity below pulls those too.
        if (scene.water().active())
            for (auto& b : bodies)
                if (b.dynamic) floatIn(b, scene.water(), g, h, scene);
        for (auto& b : bodies)
            if (b.dynamic && b.awake) {
                b.v.y -= g * h;
                b.w *= std::max(0.0f, 1.0f - 0.05f * h);        // a little rolling resistance
            }
        // Force zones: wind, push and pull on whatever loose part is inside one.
        for (const Zone& z : zones)
            for (auto& b : bodies) {
                if (!b.dynamic || !inside(z.box, b.pos, 0.0f)) continue;
                if (!b.awake) { b.awake = true; b.node->sleepTime = 0.0f; }
                b.v += Physics::zonePush(z.node, z.box, b.pos) * h;
            }
        for (const ActiveMover& am : movers)
            if (group[am.body] >= 0) applyMover(am, bodies, assemblies[group[am.body]], h);
        for (auto& j : joints) {
            if (j.type != ConstraintType::Spring) continue;
            Body &A = bodies[j.a], &B = bodies[j.b];
            glm::vec3 ra = A.rot * j.la, rb = B.rot * j.lb;
            glm::vec3 d = (B.pos + rb) - (A.pos + ra);
            float len = glm::length(d);
            if (len < 1e-5f) continue;
            glm::vec3 dir = d / len;
            float relV = glm::dot(B.velocityAt(B.pos + rb) - A.velocityAt(A.pos + ra), dir);
            float f = j.node->stiffness * (len - j.length) + j.node->damping * relV;
            glm::vec3 imp = dir * f * h;
            applyImpulse(A, ra, imp);
            applyImpulse(B, rb, -imp);
            j.imp += imp;
        }

        // ---- 4. Contacts ----------------------------------------------------------
        std::vector<Contact> contacts;
        for (int i = 0; i < (int)bodies.size(); ++i) {
            if (!bodies[i].dynamic || !bodies[i].solid) continue;
            for (int k = 0; k < (int)bodies.size(); ++k) {
                if (k == i || !bodies[k].solid) continue;
                if (bodies[k].dynamic && k < i) continue;              // each dynamic pair once
                const Body &A = bodies[i], &B = bodies[k];
                if (!A.awake && (!B.dynamic || !B.awake)) continue;
                if (A.aabbMax.x < B.aabbMin.x || A.aabbMin.x > B.aabbMax.x || A.aabbMax.y < B.aabbMin.y ||
                    A.aabbMin.y > B.aabbMax.y || A.aabbMax.z < B.aabbMin.z || A.aabbMin.z > B.aabbMax.z)
                    continue;
                if (noCollide.count({std::min(i, k), std::max(i, k)})) continue;
                if (useGroups && !groups.collides(A.group, B.group)) continue;   // collision groups that pass through
                if (A.sphere && B.sphere)      sphereSphere(i, k, A, B, contacts);
                else if (A.sphere)             sphereBox(i, k, A, B, contacts, true);
                else if (B.sphere)             sphereBox(k, i, B, A, contacts, false);
                else                           boxBox(i, k, A, B, contacts);
            }
        }
        // Against the terrain: a ball touches where it dips under the ground, a box
        // wherever its corners (and the middles of its bottom edges) do.
        if (terrainBody >= 0)
            for (int i = 0; i < terrainBody; ++i) {
                const Body& A = bodies[i];
                if (!A.dynamic || !A.solid || !A.awake) continue;
                if (useGroups && !groups.collides(A.group, 0)) continue;   // its group passes through the terrain (Default)
                float top;
                if (!terrain.highestUnder(A.aabbMin.x, A.aabbMin.z, A.aabbMax.x, A.aabbMax.z, top) || top < A.aabbMin.y) continue;
                auto touch = [&](const glm::vec3& p) {
                    float g;
                    if (!terrain.heightAt(p.x, p.z, g) || p.y >= g) return;
                    const glm::vec3 n = terrain.normalAt(p.x, p.z);
                    contacts.push_back({i, terrainBody, p, n, (g - p.y) * n.y});
                };
                if (A.sphere) {
                    float g;
                    if (!terrain.heightAt(A.pos.x, A.pos.z, g)) continue;
                    const glm::vec3 n = terrain.normalAt(A.pos.x, A.pos.z);
                    const float dist = (A.pos.y - g) * n.y;
                    if (dist < A.radius) contacts.push_back({i, terrainBody, A.pos - n * A.radius, n, A.radius - dist});
                    continue;
                }
                glm::vec3 corners[8];
                boxCorners(A.obb(), corners);
                for (const glm::vec3& p : corners) touch(p);
            }

        for (auto& c : contacts) {
            Body &A = bodies[c.a], &B = bodies[c.b];
            if (B.dynamic && !B.awake && A.awake) B.awake = true;   // bumped: wake up
            if (A.dynamic && !A.awake && B.awake && B.dynamic) A.awake = true;
            c.ra = c.p - A.pos;
            c.rb = c.p - B.pos;
            c.t1 = std::abs(c.n.x) < 0.9f ? glm::normalize(glm::cross(c.n, glm::vec3(1, 0, 0)))
                                          : glm::normalize(glm::cross(c.n, glm::vec3(0, 1, 0)));
            c.t2 = glm::cross(c.n, c.t1);
            c.kN  = effMass(A, B, c.ra, c.rb, c.n);
            c.kT1 = effMass(A, B, c.ra, c.rb, c.t1);
            c.kT2 = effMass(A, B, c.ra, c.rb, c.t2);
            c.mu  = std::sqrt(A.friction * B.friction);
            float vn = glm::dot(A.velocityAt(c.p) - B.velocityAt(c.p), c.n);
            float e = std::max(A.elasticity, B.elasticity);
            // Weights (CustomPhysicalProperties): a weighted mix instead, like Roblox's.
            if (A.frictionWeight != 1.0f || B.frictionWeight != 1.0f) {
                const float w = A.frictionWeight + B.frictionWeight;
                if (w > 1e-6f) c.mu = (A.friction * A.frictionWeight + B.friction * B.frictionWeight) / w;
            }
            if (A.elasticityWeight != 1.0f || B.elasticityWeight != 1.0f) {
                const float w = A.elasticityWeight + B.elasticityWeight;
                if (w > 1e-6f) e = (A.elasticity * A.elasticityWeight + B.elasticity * B.elasticityWeight) / w;
            }
            c.bias = 0.25f / h * std::max(c.depth - 0.01f, 0.0f);
            if (vn < -1.5f) c.bias = std::max(c.bias, -e * vn);
        }
        // Warm start: each contact begins pushing as hard as it did last step, so
        // the solver only has to fix what changed. Without it a tall stack never
        // quite settles (it shuffles and slowly topples) and boxes creep down slopes.
        for (auto& c : contacts) {
            Body &A = bodies[c.a], &B = bodies[c.b];
            auto it = m_contactMemory.find(pairKey(A, B));
            if (it == m_contactMemory.end()) continue;
            // The one from last step in (nearly) the same spot on the part.
            const glm::vec3 local = glm::inverse(A.rot) * c.ra;
            const Physics::ContactMemory* was = nullptr;
            float best = 0.15f * 0.15f;
            for (const auto& m : it->second) {
                const glm::vec3 d = m.local - local;
                if (glm::dot(d, d) < best && glm::dot(m.n, c.n) > 0.95f) { best = glm::dot(d, d); was = &m; }
            }
            if (!was) continue;
            c.jn = was->jn;
            const float maxF = c.mu * c.jn;
            c.jt1 = std::clamp(was->jt1, -maxF, maxF);
            c.jt2 = std::clamp(was->jt2, -maxF, maxF);
            const glm::vec3 j = c.n * c.jn + c.t1 * c.jt1 + c.t2 * c.jt2;
            applyImpulse(A, c.ra, j);
            applyImpulse(B, c.rb, -j);
        }

        // ---- 5. Solve (sequential impulses) ------------------------------------
        for (int iter = 0; iter < 8; ++iter) {
            for (auto& c : contacts) {
                Body &A = bodies[c.a], &B = bodies[c.b];
                glm::vec3 vrel = A.velocityAt(c.p) - B.velocityAt(c.p);
                float vn = glm::dot(vrel, c.n);
                float dj = (c.bias - vn) / c.kN;
                float old = c.jn;
                c.jn = std::max(old + dj, 0.0f);
                dj = c.jn - old;
                applyImpulse(A, c.ra, c.n * dj);
                applyImpulse(B, c.rb, -c.n * dj);

                float maxF = c.mu * c.jn;
                for (int t = 0; t < 2; ++t) {
                    glm::vec3 tn = t == 0 ? c.t1 : c.t2;
                    float& acc = t == 0 ? c.jt1 : c.jt2;
                    float k = t == 0 ? c.kT1 : c.kT2;
                    vrel = A.velocityAt(c.p) - B.velocityAt(c.p);
                    float djt = -glm::dot(vrel, tn) / k;
                    float o2 = acc;
                    acc = std::clamp(o2 + djt, -maxF, maxF);
                    djt = acc - o2;
                    applyImpulse(A, c.ra, tn * djt);
                    applyImpulse(B, c.rb, -tn * djt);
                }
            }

            for (auto& j : joints) {
                Body &A = bodies[j.a], &B = bodies[j.b];
                // How much this joint pushes B (or A) this pass, whichever way it leaves the loop below.
                struct Measure {
                    Joint& j; const Body &A, &B; glm::vec3 va, vb;
                    ~Measure() {
                        if (B.dynamic) j.imp += (B.v - vb) / B.invMass;
                        else if (A.dynamic) j.imp -= (A.v - va) / A.invMass;
                    }
                } measure{j, A, B, A.v, B.v};
                glm::vec3 ra = A.rot * j.la, rb = B.rot * j.lb;
                glm::vec3 pa = A.pos + ra, pb = B.pos + rb;
                const float beta = 0.2f / h;

                if (j.type == ConstraintType::Rope || j.type == ConstraintType::Rod) {
                    glm::vec3 d = pb - pa;
                    float len = glm::length(d);
                    if (len < 1e-5f) continue;
                    glm::vec3 dir = d / len;
                    float C = len - j.length;
                    if (j.type == ConstraintType::Rope && C <= 0.0f) continue;   // slack rope does nothing
                    float cdot = glm::dot(B.velocityAt(pb) - A.velocityAt(pa), dir);
                    float lambda = -(cdot + beta * C) / effMass(A, B, ra, rb, dir);
                    if (j.type == ConstraintType::Rope) lambda = std::min(lambda, 0.0f);   // ropes only pull
                    applyImpulse(A, ra, -dir * lambda);
                    applyImpulse(B, rb, dir * lambda);
                    continue;
                }
                if (j.type == ConstraintType::Spring) continue;
                if (usesJointProps(j.type)) { solveJointProps(j, A, B, h, iter == 0); continue; }

                // Weld & hinge: the two anchor points stay together (a ball joint)...
                for (int ax = 0; ax < 3; ++ax) {
                    glm::vec3 e(0.0f);
                    e[ax] = 1.0f;
                    float C = glm::dot(pb - pa, e);
                    float cdot = glm::dot(B.velocityAt(pb) - A.velocityAt(pa), e);
                    float lambda = -(cdot + beta * C) / effMass(A, B, ra, rb, e);
                    applyImpulse(A, ra, -e * lambda);
                    applyImpulse(B, rb, e * lambda);
                }

                // ...and their rotation is locked (weld) or free around one axis (hinge).
                glm::mat3 IA = A.dynamic ? A.invInertia() : glm::mat3(0.0f);
                glm::mat3 IB = B.dynamic ? B.invInertia() : glm::mat3(0.0f);
                auto angularRow = [&](const glm::vec3& u, float error) {
                    float k = glm::dot(u, IA * u) + glm::dot(u, IB * u);
                    if (k < 1e-9f) return;
                    float cdot = glm::dot(B.w - A.w, u);
                    float lambda = -(cdot + beta * error) / k;
                    if (A.dynamic) A.w -= IA * u * lambda;
                    if (B.dynamic) B.w += IB * u * lambda;
                };
                if (j.type == ConstraintType::Weld) {
                    glm::quat target = A.rot * j.rel;
                    glm::quat err = B.rot * glm::inverse(target);
                    if (err.w < 0) err = -err;
                    glm::vec3 e = 2.0f * glm::vec3(err.x, err.y, err.z);
                    for (int ax = 0; ax < 3; ++ax) {
                        glm::vec3 u(0.0f);
                        u[ax] = 1.0f;
                        angularRow(u, glm::dot(e, u));
                    }
                } else {   // hinge
                    glm::vec3 axA = glm::normalize(A.rot * j.axisA);
                    glm::vec3 axB = glm::normalize(B.rot * j.axisB);
                    glm::vec3 e = glm::cross(axA, axB);
                    glm::vec3 p1 = std::abs(axA.x) < 0.9f ? glm::normalize(glm::cross(axA, glm::vec3(1, 0, 0)))
                                                          : glm::normalize(glm::cross(axA, glm::vec3(0, 1, 0)));
                    glm::vec3 p2 = glm::cross(axA, p1);
                    angularRow(p1, glm::dot(e, p1));
                    angularRow(p2, glm::dot(e, p2));

                    // Motor: spin B around the axis at the chosen speed.
                    if (j.node->motorTorque > 0.0f) {
                        float k = glm::dot(axA, IA * axA) + glm::dot(axA, IB * axA);
                        if (k > 1e-9f) {
                            float cdot = glm::dot(B.w - A.w, axA) - j.node->motorSpeed;
                            float lambda = -cdot / k;
                            float maxImp = j.node->motorTorque * h;
                            float old = j.accMotor;
                            j.accMotor = std::clamp(old + lambda, -maxImp, maxImp);
                            lambda = j.accMotor - old;
                            if (A.dynamic) A.w -= IA * axA * lambda;
                            if (B.dynamic) B.w += IB * axA * lambda;
                        }
                    }
                }
            }
        }
        for (auto& j : joints) {
            j.accMotor = 0.0f;
            for (float& a : j.acc) a = 0.0f;
            j.peak = std::max(j.peak, glm::length(j.imp) / h);
            j.imp = glm::vec3(0.0f);
        }
        if (Physics::debugOn() && step == substeps - 1) {
            auto& dbg = Physics::debugView().contacts;
            dbg.clear();
            for (const auto& c : contacts) if (dbg.size() < 4000) dbg.push_back({c.p, c.n});
        }
        m_contactMemory.clear();
        for (const auto& c : contacts)
            if (c.jn > 0.0f)
                m_contactMemory[pairKey(bodies[c.a], bodies[c.b])].push_back(
                    {glm::inverse(bodies[c.a].rot) * c.ra, c.n, c.jn, c.jt1, c.jt2});

        // ---- 6. Move ------------------------------------------------------------------
        for (int bi = 0; bi < (int)bodies.size(); ++bi) {
            Body& b = bodies[bi];
            if (!b.dynamic || !b.awake) continue;
            // Keep things sane even if a script launches something at warp speed.
            float sp = glm::length(b.v);
            if (sp > 200.0f) b.v *= 200.0f / sp;
            float ws = glm::length(b.w);
            if (ws > 60.0f) b.w *= 60.0f / ws;
            // Fast and small (a bullet, a thrown brick): something it would jump right
            // over in one step stops it instead, so it can't pass through a wall.
            const float thin = b.sphere ? b.radius : std::min(b.half.x, std::min(b.half.y, b.half.z));
            const float travel = glm::length(b.v) * h;
            bool stopped = false;
            if (b.solid && travel > thin) {
                const glm::vec3 dir = b.v * (h / travel);
                const glm::vec3 lo = glm::min(b.pos, b.pos + b.v * h) - glm::vec3(thin);
                const glm::vec3 hi = glm::max(b.pos, b.pos + b.v * h) + glm::vec3(thin);
                float best = travel;
                int hit = -1;
                glm::vec3 hitN(0.0f);
                for (int k = 0; k < (int)bodies.size(); ++k) {
                    const Body& o = bodies[k];
                    if (k == bi || !o.solid) continue;
                    if (o.aabbMax.x < lo.x || o.aabbMin.x > hi.x || o.aabbMax.y < lo.y || o.aabbMin.y > hi.y ||
                        o.aabbMax.z < lo.z || o.aabbMin.z > hi.z)
                        continue;
                    if (noCollide.count({std::min(bi, k), std::max(bi, k)})) continue;
                    if (useGroups && !groups.collides(b.group, o.group)) continue;
                    OBB box = o.obb();
                    if (o.sphere) box.half = glm::vec3(o.radius);
                    float t;
                    glm::vec3 n;
                    if (rayIntoBox(b.pos, dir, best, box, thin, t, n)) { best = t; hit = k; hitN = n; }
                }
                if (hit >= 0) {
                    Body& o = bodies[hit];
                    b.pos += dir * std::max(0.0f, best - 0.01f);
                    const float vn = glm::dot(b.v - (o.dynamic ? o.v : glm::vec3(0.0f)), hitN);
                    if (vn < 0.0f) {
                        const float e = std::max(b.elasticity, o.elasticity);
                        const float j = -(1.0f + e) * vn / (b.invMass + (o.dynamic ? o.invMass : 0.0f));
                        b.v += hitN * (j * b.invMass);
                        if (o.dynamic) { o.v -= hitN * (j * o.invMass); o.awake = true; }
                    }
                    // The rest of the step it slides along what it hit (no stutter skimming a floor).
                    const glm::vec3 slide = b.v - hitN * std::max(0.0f, glm::dot(b.v, hitN));
                    b.pos += slide * (h * (1.0f - best / travel));
                    stopped = true;
                }
            }
            if (!stopped) b.pos += b.v * h;
            glm::quat spin(0.0f, b.w.x, b.w.y, b.w.z);
            b.rot = glm::normalize(b.rot + (spin * b.rot) * (0.5f * h));
            // Update the broad-phase box.
            OBB o = b.obb();
            glm::vec3 ext(0.0f);
            for (int i = 0; i < 3; ++i) ext += glm::abs(o.axis[i]) * o.half[i];
            if (b.sphere) ext = glm::vec3(b.radius);
            b.aabbMin = b.pos - ext;
            b.aabbMax = b.pos + ext;
        }
    }

    // Breakable joints: pulled harder than BreakForce, they snap (and switch themselves off).
    for (const Joint& j : joints) {
        j.node->jointForce = j.peak;
        if (j.node->breakForce > 0.0f && j.peak > j.node->breakForce) {
            j.node->enabled = false;
            j.node->broken = true;
            bodies[j.a].node->sleepTime = 0.0f;
            bodies[j.b].node->sleepTime = 0.0f;
        }
    }
    if (Physics::debugOn()) {
        Physics::DebugView& dbg = Physics::debugView();
        dbg.joints.clear();
        for (const Joint& j : joints) {
            const Body &A = bodies[j.a], &B = bodies[j.b];
            // (a weld's two anchors are the same point: draw it from part to part instead)
            if (j.type == ConstraintType::Weld) dbg.joints.push_back({A.pos, B.pos, j.peak, j.node->breakForce});
            else dbg.joints.push_back({A.pos + A.rot * j.la, B.pos + B.rot * j.lb, j.peak, j.node->breakForce});
        }
        ++dbg.steps;
    }

    // Where sliders and spinners are now (CurrentPosition / CurrentAngle).
    for (const Joint& j : joints) {
        if (!jointSlides(j.type)) continue;
        const Body &A = bodies[j.a], &B = bodies[j.b];
        const glm::quat fa = A.rot * j.qa, fb = B.rot * j.qb;
        JointProps& p = j.node->joint;
        p.currentPosition = glm::dot((B.pos + B.rot * j.lb) - (A.pos + A.rot * j.la), fa * glm::vec3(1, 0, 0));
        if (j.type == ConstraintType::Cylindrical) p.currentAngle = glm::degrees(spinAngle(fa, fb, p));
    }

    // ---- 7. Back into the scene (and fall asleep when still) --------------------
    for (auto& b : bodies) {
        if (!b.dynamic) continue;
        SceneNode* n = b.node;
        bool still = glm::length(b.v) < 0.08f && glm::length(b.w) < 0.1f;
        n->sleepTime = (b.awake && still) ? n->sleepTime + dt : (b.awake ? 0.0f : n->sleepTime);
        if (!b.awake) continue;
        if (n->sleepTime > 0.6f) { b.v = glm::vec3(0.0f); b.w = glm::vec3(0.0f); }
        writeBack(b);
        if (b.pos.y < scene.world().fallenPartsHeight) fallen.push_back(n->id);
    }
}

void Physics::wake(SceneNode* n) { if (n) n->sleepTime = 0.0f; }

bool& Physics::debugOn() {
    static bool on = false;
    return on;
}

Physics::DebugView& Physics::debugView() {
    static DebugView view;
    return view;
}

glm::vec3 Physics::zonePush(const SceneNode* zone, const OBB& box, const glm::vec3& p) {
    std::string mode = "Wind";
    float strength = 250.0f;
    glm::vec3 dir(0.0f, 1.0f, 0.0f);
    for (const Attribute& a : zone->attributes) {
        if (a.name == "Mode" && a.type == Attribute::String) mode = a.s;
        else if (a.name == "Strength" && a.type == Attribute::Number) strength = (float)a.n;
        else if (a.name == "Direction" && a.type == Attribute::Vector3 && glm::length(a.v) > 1e-4f) dir = glm::normalize(a.v);
    }
    if (mode == "Push" || mode == "Pull") {
        glm::vec3 out = p - box.center;
        const float len = glm::length(out);
        if (len < 1e-3f) return glm::vec3(0.0f);
        return out / len * (mode == "Push" ? strength : -strength);
    }
    // Wind: Direction is in the zone's own space, so turning the zone turns the wind.
    return glm::normalize(box.axis[0] * dir.x + box.axis[1] * dir.y + box.axis[2] * dir.z) * strength;
}
