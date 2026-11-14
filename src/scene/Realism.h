#pragma once
// Small bits of real life for characters and the world, kept apart from Player so each
// can be tested on its own:
//   * Injuries: a hurt leg limps, a hurt arm is held against the body (workspace.Injuries).
//   * Stamina: running, jumping and swimming tire you out (workspace.Stamina, off by default).
//   * Blinking and glancing at whoever is near.
//   * What each material sounds like when hit or walked on, and how it takes a bullet,
//     fire or a footprint.
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

enum class Material;
enum class TerrainMaterial : uint8_t;

namespace Realism {

// ---- Injuries -------------------------------------------------------------------------
// Body parts, in Player's limb order, plus the torso.
enum Limb { LeftArm = 0, RightArm = 1, LeftLeg = 2, RightLeg = 3, Torso = 4, None = -1 };
constexpr int kLimbCount = 5;
// Which limb a body part is ("Left Leg", R15's "LeftUpperLeg", "LeftFoot", "LeftHand"...).
Limb limbOf(const std::string& partName);

struct Injuries {
    float hurt[kLimbCount] = {0, 0, 0, 0, 0};   // 0 fine .. 1 badly hurt
    // `damage` points of harm to a limb (a bullet in the leg is about 20-35).
    void  add(Limb limb, float damage);
    // Heals slowly by itself (fully in about a minute and a half).
    void  heal(float dt);
    void  clear();
    bool  any() const;
    // How fast you can walk: a badly hurt leg drops you to about 60%.
    float speedScale() const;
    // 0..1 for a leg: how much it drags (shorter, stiffer stride).
    float limp(Limb leg) const;
    // The worse-hurt leg (LeftLeg or RightLeg), or None.
    Limb  limpingLeg() const;
    // Which arm holds a wound: the one opposite a hurt arm (it clutches it), or one on a
    // hurt torso. None = nothing to hold. `amount` = how much.
    Limb  holdingArm(float* amount = nullptr) const;
};

// ---- Stamina ---------------------------------------------------------------------------
struct Stamina {
    float value = 1.0f;          // 1 fresh .. 0 worn out
    bool  winded = false;        // ran out: has to get some back before running fast again
    // One step: `effort` 0 standing .. 1 running flat out, plus jumps and swimming.
    void  step(float dt, float effort, bool jumped, bool swimming);
    void  reset() { value = 1.0f; winded = false; }
    // Tired legs walk slower and jump lower.
    float speedScale() const;
    float jumpScale() const;
    // 0 fresh .. 1 out of breath (how hard the breathing sound and heaving chest are).
    float breathless() const;
};

// ---- Blinking and looking ----------------------------------------------------------------
// How shut a character's eyes are at `time` (0 open .. 1 shut): every 2-6 seconds,
// now and then twice in a row, each character on its own rhythm (`who` = its id).
float blinkAt(double time, uint64_t who);

// The head turns to look at something interesting nearby (another character), within
// what a neck can do, easing there; now and then it glances somewhere else.
struct Gaze {
    float yaw = 0.0f, pitch = 0.0f;          // degrees, where the head points now (from its rest)
    float wantYaw = 0.0f, wantPitch = 0.0f;
    // Look toward `target` (world) from a head at `head` facing `forward` (flat).
    // hasTarget false = back to straight ahead.
    void  aim(const glm::vec3& head, const glm::vec3& forward, const glm::vec3& target, bool hasTarget);
    void  step(float dt);
    static constexpr float kMaxYaw = 70.0f, kMaxPitch = 35.0f;
};

// ---- Wet bodies ------------------------------------------------------------------------
// Swimming soaks you; rain wets you a little at a time; you dry off over about 20 s.
float wetter(float wet, float dt, bool swimming, float rain);

// ---- Materials -------------------------------------------------------------------------
// The family of a material, for the sounds and how it takes hits.
enum class Feel { Metal, Wood, Glass, Stone, Flesh, Soft, Plastic };
Feel        feelOf(Material m);
Feel        feelOf(TerrainMaterial m);
// The real recorded sound for hitting / breaking / stepping on it ("hit_metal"...).
const char* hitSound(Feel f);
const char* breakSound(Feel f);
const char* stepSound(Material m);
const char* stepSound(TerrainMaterial m);
// How loud a hit at `speed` studs a second sounds (0 = too soft to hear).
float       hitVolume(float speed, float mass);

// Bullets: how many studs of it a bullet at full power can go through (0 = stops it).
float       pierceDepth(Material m);
// Hard enough for a glancing bullet to bounce off.
bool        ricochets(Material m);

// Fire: how readily it catches (0 = never) and how fast it burns away.
float       flammability(Material m);

// Footprints: does a foot / tyre leave a mark in it, and what colour is the dent.
bool        takesPrints(TerrainMaterial m);
bool        takesPrints(Material m);
glm::vec3   printColor(TerrainMaterial m);
glm::vec3   printColor(Material m, const glm::vec3& partColor);

} // namespace Realism
