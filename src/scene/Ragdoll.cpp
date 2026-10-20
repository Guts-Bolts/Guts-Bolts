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
    m_heartTime = 0.0f;
    m_beat = 0.15f;
    // Point blank: nothing left but pieces.
    if (scene.world().dismemberment && m_gore && force >= 0.9f) { place(scene); explode(scene, velocity + impulse, force); }
    if (m_gore) {
        glm::vec3 dir = glm::length(impulse) > 0.1f ? glm::normalize(impulse) : glm::vec3(0, 1, 0);
        scene.particles().spray(m_goreKind, c, dir, 25 + (int)(force * 40), 3.0f + force * 4.0f);
        if (m_goreKind == GoreKind::Oil) scene.particles().sparks(c, 15);
        // The wound that killed them: on the side the hit came from. It keeps bleeding
        // with the heartbeat, so the body ends up lying in a pool. (Not if there's no body left.)
        glm::vec3 hitFrom = glm::length(impulse) > 0.1f ? -dir : glm::normalize(glm::vec3(T[2]));
        int wound = glm::dot(hitFrom, glm::vec3(T[2])) >= 0.0f ? (int)Chest : (glm::dot(hitFrom, glm::vec3(T[0])) >= 0.0f ? (int)ShR : (int)ShL);
        if (m_hidden.empty()) {
            m_stumps.push_back({wound, wound, -1, -1, 0.35f + force * 0.5f, true});
            m_points[wound].blood = 1.0f;
            scene.particles().stain(m_goreKind, torso, c + hitFrom * 0.6f, 0.7f + force * 0.6f);
            if (force > 0.5f) scene.particles().stain(m_goreKind, torso, c - hitFrom * 0.6f, 0.5f);   // came out the other side
        }
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
        // The loose piece drains from its cut end...
        if (limb == 4) m_stumps.push_back({Head, Head, HeadTop, HeadTop, 0.8f, false});
        else           m_stumps.push_back({joint, joint, joint + 1, joint + 1, 0.8f, false});
        // ...and the body pumps blood out where it came off, with every heartbeat.
        switch (limb) {
            case 0:  m_stumps.push_back({ShL, ShL, ShR, ShR, 1.0f, true}); break;
            case 1:  m_stumps.push_back({ShR, ShR, ShL, ShL, 1.0f, true}); break;
            case 2:  m_stumps.push_back({HipL, HipL, ShL, ShL, 1.0f, true}); break;
            case 3:  m_stumps.push_back({HipR, HipR, ShR, ShR, 1.0f, true}); break;
            default: m_stumps.push_back({ShL, ShR, HipL, HipR, 1.2f, true}); break;   // the neck
        }
        // Both cut ends are soaked.
        const char* names[5] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg", "Head"};
        SceneNode* piece = nullptr;
        SceneNode* torso = nullptr;
        for (auto& b : m_bones) {
            SceneNode* n = scene.findById(b.id);
            if (!n) continue;
            if (n->name == names[limb]) piece = n;
            if (n->name == "Torso") torso = n;
        }
        scene.particles().stain(m_goreKind, piece, at, 0.9f);
        scene.particles().stain(m_goreKind, torso, at, 0.8f);
        m_points[joint].blood = 1.0f;
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
            if (iter == 0) p.touching = false;
            bool hit = physics.resolveSphere(p.pos, p.radius, &n);
            if (hit) { p.touching = true; p.contactN = n; }
            if (hit && iter == 9) {
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
    if (m_gore && m_hidden.empty()) {   // (blown to pieces: the chunks do the bleeding)
        bleed(scene, dt);
        smearAll(scene, dt);
    }
    place(scene);
}

glm::vec3 Ragdoll::stumpPos(const Stump& s) const {
    return (m_points[s.a].pos + m_points[s.a2].pos) * 0.5f;
}

glm::vec3 Ragdoll::stumpDir(const Stump& s) const {
    glm::vec3 from = s.f < 0 ? center() : (m_points[s.f].pos + m_points[s.f2].pos) * 0.5f;
    glm::vec3 d = stumpPos(s) - from;
    return glm::length(d) > 1e-4f ? glm::normalize(d) : glm::vec3(0, 1, 0);
}

// Wounds bleed like a real body: while the heart still beats, the ones joined to it
// spurt with each beat (the heart slowing and weakening as they die), and every wound
// keeps dripping in between. Loose pieces just drain.
void Ragdoll::bleed(Scene& scene, float dt) {
    if (m_stumps.empty()) return;
    ParticleSystem& ps = scene.particles();
    m_heartTime += dt;
    const float pressure = std::exp(-m_heartTime / 5.0f);        // blood pressure falls as blood runs out
    const bool  beating  = pressure > 0.12f;                      // ...until the heart stops (about 10 s)
    auto velAt = [&](const Stump& s) {
        glm::vec3 now = stumpPos(s);
        glm::vec3 was = (m_points[s.a].prev + m_points[s.a2].prev) * 0.5f;
        return (now - was) / std::max(dt, 1e-4f);
    };
    if (beating) {
        m_beat -= dt;
        if (m_beat <= 0.0f) {
            float bpm = 40.0f + 80.0f * pressure;                    // racing at first, then slower
            m_beat = 60.0f / bpm;
            for (auto& s : m_stumps)
                if (s.pumped && s.bleed > 0.0f)
                    ps.spurt(m_goreKind, stumpPos(s), stumpDir(s), velAt(s), pressure * s.bleed);
        }
    }
    m_stumpTimer += dt;
    if (m_stumpTimer > 0.12f) {
        m_stumpTimer = 0.0f;
        for (auto& s : m_stumps) {
            if (s.bleed <= 0.02f) continue;
            ps.ooze(m_goreKind, stumpPos(s) + stumpDir(s) * 0.05f, velAt(s));
            m_points[s.a].blood = 1.0f;
            m_points[s.a2].blood = 1.0f;
        }
    }
    for (auto& s : m_stumps)   // wounds slowly stop (clots; the heart stops; no blood left)
        s.bleed = std::max(0.0f, s.bleed - dt * (s.pumped ? (beating ? 0.03f : 0.12f) : 0.2f));
}

// Bloody bits of the body sliding along the floor leave smears, and bits that land
// in a pool get bloody.
void Ragdoll::smearAll(Scene& scene, float dt) {
    ParticleSystem& ps = scene.particles();
    for (auto& p : m_points) {
        if (!p.touching) continue;
        glm::vec3 at = p.pos - p.contactN * p.radius;
        if (p.blood < 0.9f) {
            float soak = ps.wetBloodAt(at, p.radius, 0.0f);
            if (soak > p.blood) p.blood = soak;
        }
        if (p.blood < 0.05f) continue;
        glm::vec3 v = (p.pos - p.prev) / std::max(dt, 1e-4f);
        glm::vec3 along = v - p.contactN * glm::dot(v, p.contactN);
        float speed = glm::length(along);
        if (speed < 0.3f) continue;
        p.slid += speed * dt;
        if (p.slid < 0.22f) continue;
        p.slid = 0.0f;
        ps.smear(m_goreKind, at, along, p.contactN, p.radius * 1.5f, p.blood);
        p.blood *= 0.88f;
    }
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
