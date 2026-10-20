#include "Ragdoll.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace {

// Point indices.
enum { ShL, ShR, HipL, HipR, Chest, Head, HeadTop,
       LArm0, LArm1, RArm0, RArm1, LLeg0, LLeg1, RLeg0, RLeg1, kPointCount };

const char* kLimbNames[4] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg"};
const int   kLimbTop[4]   = {LArm0, RArm0, LLeg0, RLeg0};

float rnd(float a, float b) {
    static std::mt19937 r{4242u};
    return std::uniform_real_distribution<float>(a, b)(r);
}

glm::vec3 xf(const glm::mat4& m, glm::vec3 p) { return glm::vec3(m * glm::vec4(p, 1.0f)); }

// Rotation (columns right / up / forward) + position -> node transform.
void setNode(SceneNode* n, const glm::mat3& r, const glm::vec3& pos) {
    float z, y, x;
    glm::extractEulerAngleZYX(glm::mat4(r), z, y, x);
    n->transform.position = pos;
    n->transform.rotation = glm::degrees(glm::vec3(x, y, z));
}

glm::mat3 frameFrom(glm::vec3 up, glm::vec3 right) {
    up = glm::normalize(up);
    right = right - up * glm::dot(right, up);
    if (glm::length(right) < 1e-4f) right = glm::abs(up.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
    right = glm::normalize(right);
    glm::vec3 fwd = glm::cross(right, up);
    return glm::mat3(right, up, fwd);
}

} // namespace

glm::vec3 Ragdoll::center() const {
    if (m_points.size() < kPointCount) return glm::vec3(0.0f);
    return (m_points[ShL].pos + m_points[ShR].pos + m_points[HipL].pos + m_points[HipR].pos) * 0.25f;
}

glm::mat3 Ragdoll::torsoFrame() const {
    glm::vec3 right = m_points[ShR].pos - m_points[ShL].pos + m_points[HipR].pos - m_points[HipL].pos;
    glm::vec3 up    = (m_points[ShL].pos + m_points[ShR].pos) - (m_points[HipL].pos + m_points[HipR].pos);
    glm::mat3 f = frameFrom(up, right);
    // The chest point says which way is "front" (stops the torso flipping inside out).
    glm::vec3 c = center();
    if (glm::dot(m_points[Chest].pos - c, f[2]) < 0.0f) { f[0] = -f[0]; f[2] = -f[2]; }
    return f;
}

void Ragdoll::start(Scene& scene, SceneNode* root, const glm::vec3& velocity,
                    const glm::vec3& impulse, float force) {
    stop();
    SceneNode* torso = root->findChild("Torso");
    SceneNode* head  = root->findChild("Head");
    if (!torso || !head) return;

    m_gore     = scene.goreEnabled();
    m_goreKind = scene.goreKind();
    m_gravity  = scene.world().gravity;
    for (bool& c : m_cut) c = false;

    // --- Points, from where the body parts are right now ---
    glm::mat4 T = torso->worldMatrix();
    glm::mat4 H = head->worldMatrix();
    m_points.resize(kPointCount);
    auto set = [&](int i, glm::vec3 p, float r) { m_points[i] = {p, p, r}; };
    set(ShL,  xf(T, {-0.5f,  0.5f, 0.0f}), 0.22f);
    set(ShR,  xf(T, { 0.5f,  0.5f, 0.0f}), 0.22f);
    set(HipL, xf(T, {-0.5f, -0.5f, 0.0f}), 0.22f);
    set(HipR, xf(T, { 0.5f, -0.5f, 0.0f}), 0.22f);
    set(Chest, xf(T, {0.0f, 0.0f, 0.5f}), 0.12f);
    set(Head,    xf(H, {0.0f, 0.0f, 0.0f}), 0.32f);
    set(HeadTop, xf(H, {0.0f, 0.5f, 0.0f}), 0.2f);
    for (int i = 0; i < 4; ++i) {
        SceneNode* limb = root->findChild(kLimbNames[i]);
        glm::mat4 L = limb ? limb->worldMatrix() : T;
        set(kLimbTop[i],     xf(L, {0.0f,  0.5f, 0.0f}), 0.22f);
        set(kLimbTop[i] + 1, xf(L, {0.0f, -0.5f, 0.0f}), 0.24f);
    }

    // --- Sticks: every rest length comes from the starting pose ---
    auto stick = [&](int a, int b, int limb = -1) {
        m_sticks.push_back({a, b, glm::length(m_points[a].pos - m_points[b].pos), limb, true});
    };
    const int torsoPts[5] = {ShL, ShR, HipL, HipR, Chest};
    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j) stick(torsoPts[i], torsoPts[j]);
    // Head hangs off the shoulders (it can nod and loll).
    stick(Head, HeadTop, 4);
    stick(Head, ShL, 4); stick(Head, ShR, 4); stick(HeadTop, ShL, 4); stick(HeadTop, ShR, 4);
    stick(Head, Chest, 4);
    for (int i = 0; i < 4; ++i) {
        int top = kLimbTop[i];
        for (int t : torsoPts) stick(top, t, i);   // shoulder / hip joint: pinned to the torso
        stick(top, top + 1);                       // the limb itself stays straight
    }

    // --- Starting motion: whatever the character was doing, plus the hit ---
    const float dt = 1.0f / 60.0f;
    glm::vec3 c = center();
    for (auto& p : m_points) {
        glm::vec3 v = velocity + impulse;
        // A little spin so it tumbles instead of falling like a plank.
        v += glm::cross(glm::vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)) * (1.0f + glm::length(impulse) * 0.2f), p.pos - c);
        p.prev = p.pos - v * dt;
    }

    // --- Bones: which scene node follows which points ---
    m_bones.push_back({torso->id, 0, 0, 0, {1, 0, 0}});
    if (SceneNode* hrp = root->findChild("HumanoidRootPart")) m_bones.push_back({hrp->id, 0, 0, 0, {1, 0, 0}});
    m_bones.push_back({head->id, 1, Head, HeadTop, {1, 0, 0}});
    for (int i = 0; i < 4; ++i)
        if (SceneNode* limb = root->findChild(kLimbNames[i]))
            m_bones.push_back({limb->id, 2, kLimbTop[i], kLimbTop[i] + 1, glm::vec3(T[0])});

    // Hats and other accessories stick to the head.
    glm::mat3 hr = frameFrom(m_points[HeadTop].pos - m_points[Head].pos, glm::vec3(T[0]));
    glm::mat4 headFrame(hr);
    headFrame[3] = glm::vec4(m_points[Head].pos, 1.0f);
    for (auto& ch : root->children) {
        if (!ch->isPart()) continue;
        const std::string& n = ch->name;
        if (n.rfind("Hat", 0) == 0) {
            Bone b{ch->id, 3, 0, 0, {1, 0, 0}};
            b.offset = glm::inverse(headFrame) * ch->worldMatrix();
            m_bones.push_back(b);
        }
    }

    // The root becomes a plain container at the origin; parts move in world space.
    root->transform = Transform{};
    m_active = true;

    // Violent deaths tear limbs off.
    if (scene.world().dismemberment && m_gore && force > 0.25f) {
        for (int i = 0; i < 4; ++i)
            if (rnd(0, 1) < force * 0.8f) dismember(scene, i);
        if (force > 0.8f && rnd(0, 1) < force * 0.7f) dismember(scene, 4);
    }
    // Point blank: nothing left but pieces.
    if (scene.world().dismemberment && m_gore && force >= 0.9f) { place(scene); explode(scene, velocity + impulse, force); }
    if (m_gore) {
        glm::vec3 dir = glm::length(impulse) > 0.1f ? glm::normalize(impulse) : glm::vec3(0, 1, 0);
        scene.particles().spray(m_goreKind, c, dir, 25 + (int)(force * 40), 3.0f + force * 4.0f);
        if (m_goreKind == GoreKind::Oil) scene.particles().sparks(c, 15);
    }
    place(scene);
}

void Ragdoll::dismember(Scene& scene, int limb) {
    if (limb < 0 || limb > 4 || m_cut[limb]) return;
    m_cut[limb] = true;
    for (auto& s : m_sticks)
        if (s.limb == limb && !(limb == 4 && ((s.a == Head && s.b == HeadTop) || (s.a == HeadTop && s.b == Head))))
            s.alive = false;

    int joint = limb == 4 ? Head : kLimbTop[limb];
    glm::vec3 at = m_points[joint].pos;
    glm::vec3 out = glm::normalize(at - center() + glm::vec3(0, 0.3f, 0));
    // Give the loose piece a kick.
    for (int p : {joint, limb == 4 ? (int)HeadTop : joint + 1}) m_points[p].prev -= out * 0.08f;

    if (m_gore) {
        scene.particles().gibs(m_goreKind, at, out * 3.0f, 5);
        scene.particles().spray(m_goreKind, at, out, 20, 4.0f);
        if (m_goreKind == GoreKind::Oil) scene.particles().sparks(at, 20);
        m_stumps.push_back({joint, 2.5f, out});   // the loose piece leaks
        // ...and so does the body where it came off.
        int bodyPt = limb == 4 ? ShL : (limb == 0 ? ShL : limb == 1 ? ShR : limb == 2 ? HipL : HipR);
        m_stumps.push_back({bodyPt, 3.0f, -out});
    }
}

void Ragdoll::solve(const Physics& physics) {
    for (int iter = 0; iter < 10; ++iter) {
        for (const auto& s : m_sticks) {
            if (!s.alive) continue;
            glm::vec3 d = m_points[s.b].pos - m_points[s.a].pos;
            float len = glm::length(d);
            if (len < 1e-6f) continue;
            glm::vec3 corr = d * (0.5f * (len - s.length) / len);
            m_points[s.a].pos += corr;
            m_points[s.b].pos -= corr;
        }
        for (auto& p : m_points) {
            glm::vec3 n;
            if (physics.resolveSphere(p.pos, p.radius, &n) && iter == 9) {
                // Friction: slow sliding along the surface.
                glm::vec3 v = p.pos - p.prev;
                glm::vec3 vn = n * glm::dot(v, n);
                glm::vec3 vt = v - vn;
                p.prev = p.pos - (vt * 0.75f + (glm::dot(v, n) < 0.0f ? -vn * 0.2f : vn));
            }
        }
    }
}

void Ragdoll::update(float dt, Scene& scene, const Physics& physics) {
    if (!m_active) return;
    dt = std::min(dt, 1.0f / 30.0f);
    glm::vec3 g(0.0f, -m_gravity * dt * dt, 0.0f);
    for (auto& p : m_points) {
        glm::vec3 v = (p.pos - p.prev) * 0.995f;   // a touch of air drag
        p.prev = p.pos;
        p.pos += v + g;
    }
    solve(physics);

    // Leaking stumps.
    m_stumpTimer += dt;
    if (m_gore && m_stumpTimer > 0.05f) {
        m_stumpTimer = 0.0f;
        for (auto& s : m_stumps) {
            if (s.time <= 0.0f) continue;
            glm::vec3 vel = (m_points[s.point].pos - m_points[s.point].prev) / dt;
            scene.particles().spray(m_goreKind, m_points[s.point].pos, s.dir, 2, 2.5f * std::min(1.0f, s.time));
            (void)vel;
        }
    }
    for (auto& s : m_stumps) s.time -= dt;

    place(scene);
}

void Ragdoll::place(Scene& scene) {
    glm::mat3 tf = torsoFrame();
    glm::mat3 headR(1.0f);
    glm::vec3 headPos(0.0f);

    for (auto& b : m_bones) {
        SceneNode* n = scene.findById(b.id);
        if (!n) continue;
        switch (b.kind) {
            case 0:   // torso
                setNode(n, tf, center());
                break;
            case 1: { // head: up = centre -> top of the head
                glm::vec3 up = m_points[b.b].pos - m_points[b.a].pos;
                if (!m_cut[4]) b.right = tf[0];
                headR = frameFrom(up, b.right);
                b.right = headR[0];
                headPos = m_points[b.a].pos;
                setNode(n, headR, headPos);
                break;
            }
            case 2: { // limb: from the top point to the bottom point
                glm::vec3 top = m_points[b.a].pos, bot = m_points[b.b].pos;
                int limb = (int)(std::find(std::begin(kLimbTop), std::end(kLimbTop), b.a) - std::begin(kLimbTop));
                if (limb < 4 && !m_cut[limb]) b.right = tf[0];
                glm::mat3 r = frameFrom(top - bot, b.right);
                b.right = r[0];
                setNode(n, r, (top + bot) * 0.5f);
                break;
            }
            default: break;
        }
    }
    // Accessories last, once the head is placed.
    glm::mat4 headFrame(headR);
    headFrame[3] = glm::vec4(headPos, 1.0f);
    for (auto& b : m_bones) {
        if (b.kind != 3) continue;
        SceneNode* n = scene.findById(b.id);
        if (!n) continue;
        glm::mat4 m = headFrame * b.offset;
        glm::mat3 r(m);
        for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
        setNode(n, r, glm::vec3(m[3]));
    }
}

void Ragdoll::explode(Scene& scene, const glm::vec3& velocity, float force) {
    for (auto& b : m_bones) {
        SceneNode* n = scene.findById(b.id);
        if (!n || !n->visible) continue;
        if (b.kind != 3 && n->name != "HumanoidRootPart")
            scene.particles().burst(m_goreKind, n, velocity * 0.6f, force);
        n->visible = false;
        m_hidden.push_back(n->id);
    }
    for (int i = 0; i < 5; ++i) m_cut[i] = true;   // (nothing holds together any more)
    for (auto& s : m_sticks) s.alive = s.limb < 0;
    m_stumps.clear();
}

void Ragdoll::unhide(Scene& scene) {
    for (uint64_t id : m_hidden)
        if (SceneNode* n = scene.findById(id)) n->visible = true;
    m_hidden.clear();
}
