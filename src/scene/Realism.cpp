#include "Realism.h"
#include "SceneNode.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>

namespace Realism {

namespace {
bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
float approach(float v, float to, float rate, float dt) { return v + (to - v) * std::min(1.0f, rate * dt); }
uint32_t nextRand(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
float rand01(uint32_t& s) { return (nextRand(s) & 0xffffff) / float(0xffffff); }
} // namespace

// ---- Injuries -------------------------------------------------------------------------

Limb limbOf(const std::string& n) {
    const bool left = n.rfind("Left", 0) == 0, right = n.rfind("Right", 0) == 0;
    if (left || right) {
        if (has(n, "Leg") || has(n, "Foot")) return left ? LeftLeg : RightLeg;
        if (has(n, "Arm") || has(n, "Hand")) return left ? LeftArm : RightArm;
    }
    if (n == "Torso" || n == "UpperTorso" || n == "LowerTorso" || n == "HumanoidRootPart") return Torso;
    return None;
}

void Injuries::add(Limb limb, float damage) {
    if (limb == None || damage <= 0.0f) return;
    float& h = hurt[limb];
    h = std::min(1.0f, h + damage / 40.0f);
}

void Injuries::heal(float dt) {
    for (float& h : hurt) h = std::max(0.0f, h - dt / 90.0f);
}

void Injuries::clear() { for (float& h : hurt) h = 0.0f; }

bool Injuries::any() const {
    for (float h : hurt) if (h > 0.01f) return true;
    return false;
}

float Injuries::speedScale() const {
    const float leg = std::max(hurt[LeftLeg], hurt[RightLeg]);
    return 1.0f - 0.4f * leg;
}

float Injuries::limp(Limb leg) const {
    if (leg != LeftLeg && leg != RightLeg) return 0.0f;
    return hurt[leg];
}

Limb Injuries::limpingLeg() const {
    if (hurt[LeftLeg] < 0.05f && hurt[RightLeg] < 0.05f) return None;
    return hurt[LeftLeg] >= hurt[RightLeg] ? LeftLeg : RightLeg;
}

Limb Injuries::holdingArm(float* amount) const {
    // A hurt arm: the other hand holds it. A hurt torso: the left hand holds the side
    // (the right is usually busy with a tool).
    Limb arm = None;
    float a = 0.0f;
    if (hurt[LeftArm] > a && hurt[LeftArm] > 0.1f)   { arm = RightArm; a = hurt[LeftArm]; }
    if (hurt[RightArm] > a && hurt[RightArm] > 0.1f) { arm = LeftArm;  a = hurt[RightArm]; }
    if (hurt[Torso] > a && hurt[Torso] > 0.1f)       { arm = LeftArm;  a = hurt[Torso]; }
    if (amount) *amount = std::min(1.0f, a * 1.5f);
    return arm;
}

// ---- Stamina ---------------------------------------------------------------------------

void Stamina::step(float dt, float effort, bool jumped, bool swimming) {
    // Running flat out (at the normal 16 studs a second) empties it in about 40 s,
    // sprinting faster sooner; standing still fills it in about 8 s.
    float drain = effort > 0.6f ? (effort - 0.4f) / 0.6f / 40.0f : 0.0f;
    if (swimming) drain = std::max(drain, 1.0f / 30.0f);
    float use = drain * dt + (jumped ? 0.06f : 0.0f);
    if (use > 0.0f) value -= use;
    else value += dt * (effort < 0.1f ? 1.0f / 8.0f : 1.0f / 14.0f);
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= 0.01f) winded = true;
    if (winded && value > 0.35f) winded = false;
}

float Stamina::speedScale() const {
    // Fresh: full speed. Below a third: slowing, down to 55% when completely worn out.
    const float k = std::clamp(value / 0.33f, 0.0f, 1.0f);
    return (winded ? 0.6f : 0.55f) + 0.45f * (winded ? std::min(k, 0.4f) : k);
}

float Stamina::jumpScale() const {
    const float k = std::clamp(value / 0.25f, 0.0f, 1.0f);
    return 0.65f + 0.35f * k;
}

float Stamina::breathless() const {
    return std::clamp((0.6f - value) / 0.6f, 0.0f, 1.0f);
}

// ---- Blinking and looking ----------------------------------------------------------------

float blinkAt(double time, uint64_t who) {
    // Time is cut into slots of 2-6 s (a different length for each character and slot);
    // each slot starts with a blink, and some with a second one straight after.
    uint32_t s = (uint32_t)(who * 2654435761u) ^ 0x5bd1e995u;
    double start = (s % 1000) * 0.004;   // (not all in step)
    if (time < start) return 0.0f;
    // Walk the slots in fixed 8 s windows so this doesn't depend on how long it's been.
    const double window = 8.0;
    const double w0 = std::floor((time - start) / window);
    uint32_t ws = s ^ (uint32_t)((int64_t)w0 * 40503);
    double t = time - start - w0 * window;
    // Two or three blinks per window, at random spots in it.
    const int count = 2 + (int)(rand01(ws) * 1.5f);
    for (int i = 0; i < count; ++i) {
        const double at = (i + rand01(ws) * 0.7) * window / count;
        const bool twice = rand01(ws) < 0.15f;
        for (int k = 0; k < (twice ? 2 : 1); ++k) {
            const double b = t - (at + k * 0.25);
            if (b < 0.0 || b > 0.16) continue;
            // Shut fast (0.05 s), stay a moment, open a bit slower.
            if (b < 0.05) return (float)(b / 0.05);
            if (b < 0.08) return 1.0f;
            return std::max(0.0f, 1.0f - (float)(b - 0.08) / 0.08f);
        }
    }
    return 0.0f;
}

void Gaze::aim(const glm::vec3& head, const glm::vec3& forward, const glm::vec3& target, bool hasTarget) {
    if (!hasTarget) { wantYaw = wantPitch = 0.0f; return; }
    glm::vec3 f(forward.x, 0.0f, forward.z);
    const glm::vec3 d = target - head;
    if (glm::length(f) < 1e-4f || glm::length(d) < 1e-3f) { wantYaw = wantPitch = 0.0f; return; }
    f = glm::normalize(f);
    const glm::vec3 r = glm::cross(f, glm::vec3(0, 1, 0));
    const float along = glm::dot(d, f), side = glm::dot(d, r);
    float yaw = glm::degrees(std::atan2(-side, along));        // + = turn left
    const float flat = std::sqrt(along * along + side * side);
    float pitch = glm::degrees(std::atan2(d.y, std::max(flat, 1e-3f)));
    if (std::abs(yaw) > kMaxYaw + 40.0f) { wantYaw = wantPitch = 0.0f; return; }   // behind you: don't twist round
    wantYaw = std::clamp(yaw, -kMaxYaw, kMaxYaw);
    wantPitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
}

void Gaze::step(float dt) {
    yaw = approach(yaw, wantYaw, 5.0f, dt);
    pitch = approach(pitch, wantPitch, 5.0f, dt);
}

// ---- Wet bodies ------------------------------------------------------------------------

float wetter(float wet, float dt, bool swimming, float rain) {
    if (swimming) return 1.0f;
    if (rain > 0.0f) wet = std::min(1.0f, wet + dt * rain / 25.0f);   // a storm soaks you in about 25 s
    else wet = std::max(0.0f, wet - dt / 20.0f);
    return wet;
}

// ---- Materials -------------------------------------------------------------------------

Feel feelOf(Material m) {
    switch (m) {
        case Material::Metal: case Material::DiamondPlate: case Material::Foil: return Feel::Metal;
        case Material::Wood: return Feel::Wood;
        case Material::Glass: case Material::Ice: case Material::Neon: return Feel::Glass;
        case Material::Concrete: case Material::Brick: case Material::Slate: case Material::Granite:
        case Material::Marble: case Material::Cobblestone: return Feel::Stone;
        case Material::Sand: case Material::Grass: case Material::Fabric: return Feel::Soft;
        default: return Feel::Plastic;
    }
}

Feel feelOf(TerrainMaterial m) {
    switch (m) {
        case TerrainMaterial::Rock: return Feel::Stone;
        default: return Feel::Soft;
    }
}

const char* hitSound(Feel f) {
    switch (f) {
        case Feel::Metal: return "hit_metal";
        case Feel::Wood: return "hit_wood";
        case Feel::Glass: return "hit_glass";
        case Feel::Stone: return "hit_stone";
        case Feel::Flesh: return "hit_flesh";
        case Feel::Soft: return "step_mud";       // a dull thud
        default: return "hit_wood";               // plastic: a hollow knock, close to wood
    }
}

const char* breakSound(Feel f) {
    switch (f) {
        case Feel::Glass: return "break_glass";
        case Feel::Wood: return "break_wood";
        case Feel::Stone: return "break_stone";
        case Feel::Metal: return "hit_metal";
        default: return "break_wood";
    }
}

const char* stepSound(Material m) {
    switch (m) {
        case Material::Metal: case Material::DiamondPlate: case Material::Foil: return "step_metal";
        case Material::Wood: return "step_wood";
        case Material::Sand: return "step_sand";
        case Material::Grass: case Material::Fabric: return "step_sand";   // soft and quiet
        case Material::Ice: return "step_snow";
        default: return "step_stone";   // concrete, brick, plastic floors...
    }
}

const char* stepSound(TerrainMaterial m) {
    switch (m) {
        case TerrainMaterial::Sand: return "step_sand";
        case TerrainMaterial::Snow: return "step_snow";
        case TerrainMaterial::Mud: return "step_mud";
        case TerrainMaterial::Rock: return "step_stone";
        default: return "step_sand";   // grass and dirt: soft crunch
    }
}

float hitVolume(float speed, float mass) {
    if (speed < 3.0f) return 0.0f;
    const float k = std::clamp((speed - 3.0f) / 30.0f, 0.0f, 1.0f);
    const float heavy = std::clamp(0.4f + std::log10(1.0f + std::max(0.0f, mass)) * 0.3f, 0.3f, 1.0f);
    return std::clamp(0.12f + 0.75f * k, 0.0f, 0.9f) * heavy;
}

float pierceDepth(Material m) {
    switch (m) {
        case Material::Glass: return 3.0f;
        case Material::Wood: return 1.6f;
        case Material::Plastic: case Material::SmoothPlastic: case Material::Neon: return 1.2f;
        case Material::Fabric: case Material::Grass: return 4.0f;
        case Material::Ice: return 1.0f;
        case Material::Foil: return 2.0f;
        case Material::Sand: return 0.4f;
        case Material::Brick: return 0.25f;
        default: return 0.0f;   // metal, concrete and stone stop it
    }
}

bool ricochets(Material m) {
    switch (m) {
        case Material::Metal: case Material::DiamondPlate: case Material::Concrete: case Material::Granite:
        case Material::Marble: case Material::Slate: case Material::Cobblestone: case Material::Brick:
            return true;
        default: return false;
    }
}

float flammability(Material m) {
    switch (m) {
        case Material::Wood: return 1.0f;
        case Material::Fabric: return 1.2f;
        case Material::Grass: return 0.9f;
        case Material::Plastic: case Material::SmoothPlastic: return 0.35f;
        default: return 0.0f;   // metal, stone, glass, sand, ice don't burn
    }
}

bool takesPrints(TerrainMaterial m) {
    return m == TerrainMaterial::Sand || m == TerrainMaterial::Snow || m == TerrainMaterial::Mud ||
           m == TerrainMaterial::Dirt;
}

bool takesPrints(Material m) { return m == Material::Sand || m == Material::Grass; }

glm::vec3 printColor(TerrainMaterial m) {
    switch (m) {
        case TerrainMaterial::Snow: return {0.62f, 0.68f, 0.78f};   // blue-grey shadow in the snow
        case TerrainMaterial::Mud: return {0.17f, 0.11f, 0.07f};
        case TerrainMaterial::Sand: return {0.56f, 0.46f, 0.30f};
        default: return {0.25f, 0.18f, 0.12f};
    }
}

glm::vec3 printColor(Material m, const glm::vec3& c) {
    if (m == Material::Grass) return c * 0.65f;   // pressed-down grass is darker
    return c * 0.72f;
}

} // namespace Realism
