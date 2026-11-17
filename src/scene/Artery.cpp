#include "Artery.h"
#include "Gore.h"
#include "Npc.h"
#include "Particles.h"
#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"

#include <algorithm>
#include <cmath>

namespace Artery {

namespace {

bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool has(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }

// The hit in the part's own space (-0.5..0.5 across each side of its box).
glm::vec3 toLocal(const SceneNode& part, const glm::vec3& p) {
    return glm::vec3(glm::inverse(part.worldMatrix()) * glm::vec4(p, 1.0f));
}

// The part of a character called `name` (the wound's part), anywhere in it.
SceneNode* partOf(SceneNode* character, const std::string& name) {
    if (!character || name.empty()) return nullptr;
    return character->findChild(name, true);
}

} // namespace

const char* at(const SceneNode& part, const glm::vec3& worldPoint) {
    const std::string& n = part.name;
    const glm::vec3 l = toLocal(part, worldPoint);
    const bool left = startsWith(n, "Left"), right = startsWith(n, "Right");
    // Towards the middle of the body: a left limb's inside faces its +X, a right one's -X.
    const float inside = left ? l.x : right ? -l.x : 0.0f;
    // The neck: the bottom of the head (the jaw, under it), or the top middle of the
    // chest where it meets the neck (carotid, subclavian).
    if (n == "Head") return l.y < -0.12f ? "Neck" : nullptr;
    if (n == "Torso" || n == "UpperTorso") return l.y > 0.3f && std::abs(l.x) < 0.3f ? "Neck" : nullptr;
    if (!left && !right) return nullptr;
    // The inside of the thigh (femoral): an R6 leg's top half, an R15 upper leg.
    if (has(n, "UpperLeg")) return inside > -0.1f ? (left ? "Left Thigh" : "Right Thigh") : nullptr;
    if (n == "Left Leg" || n == "Right Leg")
        return l.y > -0.05f && inside > -0.05f ? (left ? "Left Thigh" : "Right Thigh") : nullptr;
    // The inside of the upper arm (brachial).
    if (has(n, "UpperArm")) return inside > -0.1f ? (left ? "Left Arm" : "Right Arm") : nullptr;
    if (n == "Left Arm" || n == "Right Arm")
        return l.y > -0.25f && l.y < 0.4f && inside > -0.05f ? (left ? "Left Arm" : "Right Arm") : nullptr;
    return nullptr;
}

void cut(Humanoid& h, const SceneNode& part, const glm::vec3& worldPoint, float seconds) {
    ArteryWound& w = h.artery;
    const glm::vec3 l = toLocal(part, worldPoint);
    const char* which = at(part, worldPoint);
    w.on = true;
    w.part = part.name;
    w.name = which ? which : part.name;
    w.local = glm::clamp(l, glm::vec3(-0.5f), glm::vec3(0.5f));
    // Out of the side it's nearest (where the hole is).
    const glm::vec3 a = glm::abs(w.local);
    int ax = a.x > a.y ? (a.x > a.z ? 0 : 2) : (a.y > a.z ? 1 : 2);
    w.out = glm::vec3(0.0f);
    w.out[ax] = w.local[ax] >= 0.0f ? 1.0f : -1.0f;
    w.time = 0.0f;
    w.bleedOut = std::clamp(seconds, 1.0f, 600.0f);
    w.sinceDeath = -1.0f;
    w.beat = 0.0f;
    w.pulse = 0;
    w.pulseTimer = 0.0f;
    w.drained = 0.0f;
}

bool onHit(Scene& scene, Humanoid& h, const SceneNode* character, const SceneNode& part,
           const glm::vec3& worldPoint, const glm::vec3& velocity) {
    if (!scene.world().arteries || h.artery.on || h.health <= 0.0f) return false;
    if (glm::length(velocity) < kSharpSpeed) return false;
    if (character) {
        const Attribute* a = character->findAttribute("Arteries");
        if (a && a->type == Attribute::Bool && !a->b) return false;
    }
    if (!at(part, worldPoint)) return false;
    cut(h, part, worldPoint);
    return true;
}

float severity(const ArteryWound& w, float health, float maxHealth) {
    if (!w.on) return 0.0f;
    const float byTime = w.time / std::max(0.5f, w.bleedOut);
    const float byHealth = maxHealth > 0.0f ? 1.0f - health / maxHealth : 0.0f;
    return std::clamp(std::max(byTime, byHealth), 0.0f, 1.0f);
}

glm::vec3 worldPoint(const ArteryWound& w, const SceneNode& part) {
    return glm::vec3(part.worldMatrix() * glm::vec4(w.local, 1.0f));
}

glm::vec3 worldDir(const ArteryWound& w, const SceneNode& part) {
    glm::vec3 d = glm::mat3(part.worldMatrix()) * w.out;
    if (glm::length(d) < 1e-5f) return glm::vec3(0, 1, 0);
    d = glm::normalize(d);
    // Blood leaves a little upwards (it's under pressure), not straight down into the floor.
    d = glm::normalize(d + glm::vec3(0.0f, 0.35f, 0.0f));
    return d;
}

void Stagger::step(float dt, float sev, const glm::vec3& facing) {
    t += dt;
    const float s = std::clamp(sev, 0.0f, 1.0f);
    // A wandering direction made of a few slow waves (smooth, never the same twice).
    const float ang = seed * 6.283f + 1.4f * std::sin(t * 0.73f + seed * 3.0f) + 0.9f * std::sin(t * 1.91f + 1.0f)
                    + 0.5f * std::sin(t * 3.7f + seed * 11.0f);
    // Lurches: every second or so the body tips one way and the legs hurry after it.
    const float lurch = std::pow(0.5f + 0.5f * std::sin(t * (2.4f + 1.2f * s) + seed * 5.0f), 3.0f);
    const float amount = (0.3f + 0.7f * s) * (0.35f + 0.65f * lurch);
    const glm::vec3 want(std::cos(ang) * amount, 0.0f, std::sin(ang) * amount);
    drift += (want - drift) * std::min(1.0f, dt * 5.0f);
    // Lean the way it's being pulled, and hunch over the wound more and more.
    glm::vec3 f(facing.x, 0.0f, facing.z);
    f = glm::length(f) > 1e-4f ? glm::normalize(f) : glm::vec3(0, 0, 1);
    const glm::vec3 rightDir(f.z, 0.0f, -f.x);
    const float wantRoll = -glm::dot(drift, rightDir) * 26.0f + 4.0f * std::sin(t * 5.3f) * s;
    const float wantPitch = 10.0f + 22.0f * s + glm::dot(drift, f) * 10.0f;
    const float k = std::min(1.0f, dt * 6.0f);
    roll += (wantRoll - roll) * k;
    pitch += (wantPitch - pitch) * k;
}

void beat(Scene& scene, Humanoid& h, const SceneNode* character, bool alive, float dt) {
    ArteryWound& w = h.artery;
    if (!w.on) return;
    w.time += dt;
    if (!alive && w.sinceDeath < 0.0f) w.sinceDeath = 0.0f;
    if (w.sinceDeath >= 0.0f) w.sinceDeath += dt;
    // The heart races at first, slows as the blood runs out, and stops a few seconds
    // after death. (Pressure: how hard the blood comes out.)
    const float sev = severity(w, h.health, h.maxHealth);
    float pressure = 1.15f - 0.55f * sev;
    if (w.sinceDeath >= 0.0f) pressure *= std::exp(-w.sinceDeath / 2.2f);
    if (w.sinceDeath > 7.0f || pressure < 0.08f) { w.clear(); return; }

    // Alive: the health runs out with it. (Health a script gave back is taken again.)
    if (alive && h.health > 0.0f) {
        const float rate = h.maxHealth / std::max(0.5f, w.bleedOut);
        const float take = std::min(h.health, rate * dt);
        h.health -= take;
        w.drained += take;
        if (w.time >= w.bleedOut) h.health = 0.0f;
    }

    SceneNode* part = partOf(const_cast<SceneNode*>(character), w.part);
    if (!part || !scene.goreEnabled()) return;
    const Gore::Scope look(scene, part);
    if (!look.on()) return;
    ParticleSystem& ps = scene.particles();
    const glm::vec3 p = worldPoint(w, *part);
    const glm::vec3 d = worldDir(w, *part);
    const glm::vec3 vel(0.0f);

    // Each beat is a pulse: three quick spurts, strong then weaker (an arc that rises and falls).
    w.beat -= dt;
    if (w.beat <= 0.0f) {
        const float bpm = (w.sinceDeath >= 0.0f ? 70.0f : 150.0f - 60.0f * sev);
        w.beat += 60.0f / bpm;
        w.pulse = 3;
        w.pulseTimer = 0.0f;
        // The wound soaks the part (and the clothes) around it.
        ps.stain(look.kind(), part, p, std::clamp(0.35f + 0.5f * sev, 0.35f, 0.9f));
    }
    if (w.pulse > 0) {
        w.pulseTimer -= dt;
        if (w.pulseTimer <= 0.0f) {
            const float k = w.pulse == 3 ? 1.0f : w.pulse == 2 ? 0.8f : 0.5f;
            ps.spurt(look.kind(), p, d, vel, pressure * k);
            --w.pulse;
            w.pulseTimer = 0.055f;
        }
    } else if (w.beat > 0.12f && std::fmod(w.time, 0.2f) < dt) {
        ps.ooze(look.kind(), p, vel);   // a trickle between beats
    }
}

void update(Scene& scene, float dt) {
    if (dt <= 0.0f) return;
    if (Player* pl = scene.player())
        if (pl->humanoid().artery.on) {
            Humanoid& h = pl->humanoid();
            const float before = h.health;
            beat(scene, h, pl->root(), !pl->isDead(), dt);
            pl->bledOut(before - h.health);   // (no "hit" for every drop of health it costs)
        }
    for (RemoteCharacter& rc : scene.remotes())
        if (rc.humanoid.artery.on) {
            const float before = rc.humanoid.health;
            beat(scene, rc.humanoid, scene.findById(rc.rootId), rc.alive, dt);
            // (The host doesn't bleed them again for it: the artery does the bleeding.)
            if (rc.humanoid.health != before) rc.bledHealth = rc.humanoid.health;
            rc.humanoidDirty = true;   // (their computer needs to know how far gone they are, or that it stopped)
            if (rc.alive && before > 0.0f && rc.humanoid.health <= 0.0f)
                rc.kills.push_back({0.0f, glm::vec3(0.0f)});   // bled to death: they fall where they are
        }
    for (const auto& n : scene.npcs().all())
        if (n->humanoid.artery.on) beat(scene, n->humanoid, scene.findById(n->rootId), !n->dead, dt);
}

} // namespace Artery
