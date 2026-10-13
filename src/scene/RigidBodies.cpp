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

struct MaterialPhysics { float density, friction, elasticity; };

MaterialPhysics materialPhysics(Material m) {
    switch (m) {
        case Material::Metal:    return {3.0f, 0.35f, 0.15f};
        case Material::Wood:     return {0.7f, 0.50f, 0.25f};
        case Material::Glass:    return {2.5f, 0.25f, 0.20f};
        case Material::Concrete: return {2.4f, 0.70f, 0.10f};
        case Material::Ice:      return {0.9f, 0.03f, 0.10f};
        case Material::Neon:     return {1.0f, 0.40f, 0.20f};
        default:                 return {1.0f, 0.50f, 0.30f};   // Plastic
    }
}

struct Body {
    SceneNode* node = nullptr;
    bool       dynamic = false;
    bool       sphere = false;
    bool       solid = true;
    glm::vec3  pos{0.0f};
    glm::quat  rot{1, 0, 0, 0};
    glm::vec3  half{0.5f};
    glm::vec3  scale{1.0f};
    float      radius = 0.5f;
    glm::vec3  v{0.0f}, w{0.0f};
    float      invMass = 0.0f;
    glm::vec3  invInertiaLocal{0.0f};
    float      friction = 0.5f, elasticity = 0.3f;
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

void boxBox(int ia, int ib, const Body& A, const Body& B, std::vector<Contact>& out) {
    OBB a = A.obb(), b = B.obb();
    glm::vec3 n; float depth;
    if (!Physics::obbOverlap(a, b, n, depth)) return;
    glm::vec3 ca[8], cb[8];
    boxCorners(a, ca);
    boxCorners(b, cb);
    size_t before = out.size();
    for (auto& p : ca)
        if (inside(b, p, 0.02f)) out.push_back({ia, ib, p, n, depth});
    for (auto& p : cb)
        if (inside(a, p, 0.02f)) out.push_back({ia, ib, p, n, depth});
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

    MaterialPhysics mp = materialPhysics(n->material);
    b.friction   = n->friction   >= 0 ? n->friction   : mp.friction;
    b.elasticity = n->elasticity >= 0 ? n->elasticity : mp.elasticity;
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

// The speed a one-sided limit row aims for, given how far inside the limit it is
// (gap > 0: inside; < 0: past it). Inside, it may close the gap in one step and no
// faster; past it, it's pushed back. `bounce` (from Restitution) kicks it back out.
float limitTarget(float gap, float h, float bounce) {
    const float t = gap >= 0.0f ? -gap / h : -0.2f / h * gap;
    return gap < 0.05f ? std::max(t, bounce) : t;
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
                angularRow(A, B, u, limitTarget(gap, h, b), j.acc[Joint::Cone], 0.0f, kFree);
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
            angularRow(A, B, axis, limitTarget(twist - lo, h, bounceFor(Joint::AngLow, w)), j.acc[Joint::AngLow], 0.0f, kFree);
            angularRow(A, B, -axis, limitTarget(hi - twist, h, bounceFor(Joint::AngHigh, -w)), j.acc[Joint::AngHigh], 0.0f, kFree);
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
    std::vector<Body> bodies;
    std::unordered_map<uint64_t, int> index;
    std::vector<SceneNode*> constraintNodes, moverNodes;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        // "Visible" only hides drawing for constraints; hidden parts are switched off.
        if (n->isConstraint()) { if (n->enabled) constraintNodes.push_back(n); continue; }
        if (n->isMover()) { if (n->enabled) moverNodes.push_back(n); continue; }
        if (!n->visible) continue;
        if (scene.isCharacterPart(n)) continue;
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || n->internal) continue;
        bool dynamic = !n->anchored && !hasDynamicAncestorPart(n);
        if (!dynamic && !n->canCollide) {
            // Only matters as an anchor for constraints.
            bool used = false;
            for (auto& c : n->children) if (c->isAttachment()) used = true;
            if (!used) continue;
        }
        index[n->id] = (int)bodies.size();
        bodies.push_back(makeBody(n, dynamic));
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
    const int   substeps = 2;
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
            c.bias = 0.25f / h * std::max(c.depth - 0.01f, 0.0f);
            if (vn < -1.5f) c.bias = std::max(c.bias, -e * vn);
        }

        // ---- 5. Solve (sequential impulses) ------------------------------------
        for (int iter = 0; iter < 12; ++iter) {
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
        }

        // ---- 6. Move ------------------------------------------------------------------
        for (auto& b : bodies) {
            if (!b.dynamic || !b.awake) continue;
            // Keep things sane even if a script launches something at warp speed.
            float sp = glm::length(b.v);
            if (sp > 200.0f) b.v *= 200.0f / sp;
            float ws = glm::length(b.w);
            if (ws > 60.0f) b.w *= 60.0f / ws;
            b.pos += b.v * h;
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
