#pragma once
#include <glm/glm.hpp>
#include <vector>

#include "../scene/SceneNode.h"
class Scene;

// Ready-made objects for the Toolbox. Most come with a Script inside, so they
// work straight away when you press Play — and double as scripting examples.
enum class Premade {
    SpawnLocation, KillBrick, Spinner, JumpPad, Coin, DisappearingPlatform,
    MovingPlatform, SpeedPad, ClickButton, FallingBall, DayNightCycle, LampPost, DiscoFloor,
    Landmine, SawBlade, SpikeTrap, ExplodingBarrel, Ramp,
    SwingingRope, WreckingBall, Windmill, Seesaw, MotorCart, DominoRun, CratePyramid, Trampoline,
    Checkpoint, Zombie,
    TimeBomb, Nuke, DepthCharge,   // explosions (Liquid Assets & Explosive Results)
    WaterWheel,                    // water pushing things (a river turns it)
    // Ready-made Tools, like Roblox's classic gear (staff can sell them in the catalog: File > Make Gear)
    GearSword, GearRocketLauncher, GearSpeedCoil, GearGravityCoil, GearBomb,
    WindZone, BreakableBridge,     // the physics workbench: force zones and joints that snap
};
inline bool isGearPremade(Premade p) { return p >= Premade::GearSword && p <= Premade::GearBomb; }

struct PremadeInfo {
    Premade     kind;
    const char* name;
    const char* tip;
};

const std::vector<PremadeInfo>& premadeList();
SceneNode* buildPremade(Scene& scene, Premade kind, const glm::vec3& at);

// Join two parts with a constraint. Attachments are made at the given world
// points; `axis` is the hinge axis (world space). Returns the constraint.
SceneNode* makeConstraint(Scene& scene, ConstraintType type, SceneNode* a, const glm::vec3& pa,
                          SceneNode* b, const glm::vec3& pb, const glm::vec3& axis, bool motor);

// Code snippets offered by the script editor's "Insert" menu.
struct Snippet {
    const char* name;
    const char* code;
};
const std::vector<Snippet>& snippetList();
