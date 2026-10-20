#pragma once
#include <map>
#include <algorithm>
#include <cmath>
#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "SceneNode.h"
#include "Ragdoll.h"

class Scene;
class Physics;

// Roblox-style Humanoid: the tunable properties of a character.
struct Humanoid {
    float walkSpeed  = 6.0f;    // units / second
    // Jumping, like Roblox: by default you say how HIGH to jump (JumpHeight);
    // with useJumpPower on, how FAST you leave the ground (JumpPower) instead.
    // The defaults jump about 1.4 times your own height, like a Roblox character.
    float jumpHeight   = 3.6f;  // units (Roblox's 7.2 studs at our half size)
    float jumpPower    = 12.6f; // launch velocity, units / second
    bool  useJumpPower = false;
    // How fast to leave the ground to reach that height under `gravity`.
    float launchSpeed(float gravity) const {
        return useJumpPower ? jumpPower : std::sqrt(2.0f * std::max(0.0f, gravity) * std::max(0.0f, jumpHeight));
    }
    float health     = 100.0f;
    float maxHealth  = 100.0f;
    bool  autoRotate = true;    // face the direction of travel
    // PlatformStand: the character stops holding itself up (no walking, no
    // jumping) and topples over stiffly, like a 2011 Roblox character.
    bool  platformStand = false;
};

// The six body colours of an R6 character.
struct BodyColors {
    glm::vec3 head, torso, leftArm, rightArm, leftLeg, rightLeg;
};

enum class HatStyle { None, TopHat, Cap, Crown, Ponytail };   // Ponytail: hair, not a hat, but worn the same way
inline constexpr int kHatStyleCount = 5;

// A character's pose, sent over the network in multiplayer: where the model
// is, and where each body part is (this covers walking, jumping and ragdolls).
struct CharacterPose {
    Transform root;
    std::vector<std::pair<std::string, Transform>> parts;
    bool forceField = false;
};

// A Roblox-style R6 character: HumanoidRootPart, Torso, Head (with a smiley
// face), two arms and two legs, plus an optional hat. In Play mode it walks
// with swinging limbs, jumps, stands on parts, and falls apart when it dies.
//
// The character's parts live in the normal scene tree (so they can be saved,
// undone and picked); this class only remembers the id of the "Player" model.
class Player {
public:
    explicit Player(Scene* scene) : m_scene(scene) {}
    Scene* scene() const { return m_scene; }

    void build();                     // create a fresh rig at the spawn point
    void resetSettings();             // default Humanoid values
    SceneNode* root() const;          // the "Player" model node (may be null)
    uint64_t   rootId() const { return m_rootId; }
    void       setRootId(uint64_t id) { m_rootId = id; }

    // --- Play mode ---------------------------------------------------------
    void beginPlay();                 // remember the pose, move to the spawn
    void endPlay();
    // One simulation step. Fills `touched` with ids of parts the body touches.
    void update(float dt, const glm::vec3& moveDir, bool jump, Physics& physics);
    bool consumeDied();               // true once, right after dying
    bool consumeRespawned();          // true once, right after coming back

    // --- Tools (like Roblox's Backpack and hotbar) ---
    // Tools you carry live in a hidden "Backpack" inside the character; the one
    // you hold sits in the character itself, its Handle in your right hand.
    uint64_t   backpackId();                           // makes the Backpack if needed
    SceneNode* equippedTool() const;
    std::vector<SceneNode*> tools();                   // hotbar order (slot 1 first), held one included
    bool       give(SceneNode* tool);                  // into the backpack (false if full / not a tool)
    void       equip(uint64_t toolId);                 // 0 = put it away
    void       toggleSlot(int slot);                   // 0-based: equip it, or put it away if held
    SceneNode* drop();                                 // the held tool goes on the ground in front of you
    void       clearTools();                           // everything in the backpack and hand, gone
    // Joined games: the host decides what you carry, in its hotbar order.
    void       setSlotOrder(const std::vector<uint64_t>& ids) { m_slots = ids; syncSlots(); }
    // The same for any character: people who joined a game you host (on the host),
    // and everyone else's held tool (on each screen).
    static SceneNode* backpackOf(Scene& scene, SceneNode* rig);    // makes it if needed
    static SceneNode* heldTool(const SceneNode* rig);
    static void       syncSlotList(Scene& scene, std::vector<uint64_t>& slots, SceneNode* rig);
    static void       placeInHand(SceneNode* rig, SceneNode* tool);   // its Handle in the right hand (the Grip)
    static SceneNode* layDown(Scene& scene, SceneNode* rig);           // the held tool on the ground in front (or null)
    // Hold the tool from their backpack (0 = put it away); `events` hears (tool, equipped).
    static void       equipOn(Scene& scene, SceneNode* rig, uint64_t toolId,
                              const std::function<void(uint64_t, bool)>& events);
    void       setCheckpoint(uint64_t partId) { m_checkpoint = partId; }   // respawn on this part (0 = the spawn)
    // Teams (player.Team / TeamColor / Neutral): which SpawnLocations we may use. A Neutral
    // player only spawns on Neutral ones; a team player on Neutral ones or their team's.
    void       setTeam(bool neutral, const glm::vec3& color) { m_neutral = neutral; m_teamColor = color; }
    bool       neutral() const { return m_neutral; }
    glm::vec3  teamColor() const { return m_teamColor; }
    // Where we'd spawn now (a SpawnLocation we may use, a checkpoint, or the spawn point).
    glm::vec3  spawnSpot() const;
    // Move to spawnSpot() without dying (the team was set just after we came in).
    void       placeAtSpawn();
    float      aliveTime() const { return m_aliveTime; }   // seconds since we last (re)spawned
    // Called just before the character comes back to life (Player.CharacterRemoving).
    std::function<void()> onRespawning;
    // A random spot on top of a SpawnLocation someone on this team may use (Roblox's rules);
    // if none suits, the first visible one. False if there are no SpawnLocations.
    static bool findSpawn(Scene& scene, bool neutral, const glm::vec3& teamColor, glm::vec3& out);
    uint64_t   checkpoint() const { return m_checkpoint; }
    void       swingTool() { if (equippedTool()) m_toolSwing = kToolSwingTime; }   // the "use" animation
    // Emotes, like old Roblox's "/e dance": dance (dance1), dance2, dance3, laugh,
    // cheer, wave, point (keyframes in Emotes.h), and the ones you equipped from the
    // catalog (by name: "Jumping Jacks" or "jumpingjacks"). Dances go on until you move;
    // the rest play once. They play as an AnimationTrack on your character, so everyone
    // in the game sees them. False if there's no such emote (or you can't right now).
    bool       playEmote(const std::string& name);
    // humanoid.Jump = true from a script: jump once, as if Space was pressed.
    void       scriptJump() { m_scriptJump = true; }
    // humanoid:Move(direction) from a script: keep walking that way (a zero vector stops)
    // while no keys are pushed. Respawning forgets it.
    void       scriptMove(const glm::vec3& dir) { m_scriptMove = glm::vec3(dir.x, 0.0f, dir.z); }
    const std::string& emote() const { return m_emote; }
    // Knocked off your feet for `seconds` (tripped, flung): no control, the body
    // falls over and gets back up afterwards. 0 = get up now.
    void       trip(float seconds, const glm::vec3& spin = glm::vec3(0.0f));
    bool       tripped() const { return m_tripTime > 0.0f || m_humanoid.platformStand; }
    // Roblox's HumanoidStateType names: Running, Jumping, Freefall, Climbing,
    // Swimming, Seated, FallingDown, GettingUp, PlatformStanding, Dead.
    const char* stateName() const;
    // The character's Animate script (made if it's missing): values named after
    // each animation (walk, idle, dance...), each holding an Animation. Give one
    // keyframes and it plays instead of the built-in classic one.
    static void addAnimateScript(Scene& scene, SceneNode* rig);
    // Tool events for scripts (Equipped / Unequipped), and when the world changed.
    std::function<void(uint64_t tool, bool equipped)> onToolEquip;
    static constexpr int kMaxTools = 9;
    // Kill the character. `force` 0..1 = how violent (1 = limbs fly off),
    // `impulse` = which way the body gets thrown.
    void kill(float force = 0.0f, const glm::vec3& impulse = glm::vec3(0.0f));
    // Damage with a direction (used by explosions); kills if health runs out.
    void hurt(float damage, float force, const glm::vec3& impulse);
    bool isDead() const { return m_dead; }
    bool hasForceField() const;
    float respawnIn() const { return m_dead ? m_respawnDelay - m_deadTime : 0.0f; }

    void      setSpawn(const glm::vec3& p) { m_spawn = p; }
    glm::vec3 spawn() const { return m_spawn; }
    glm::vec3 position() const;
    glm::vec3 focusPoint() const;     // where the play camera should look (the head)
    void      faceYaw(float degrees) { m_faceLock = true; m_faceYaw = degrees; }   // first person: turn to the camera

    Humanoid& humanoid() { return m_humanoid; }
    // How the character's parts were when Play started (animations pose from there).
    const std::unordered_map<uint64_t, Transform>& restPose() const { return m_rest; }
    // The parts walking moves every frame (the arms and legs).
    bool drivesPart(const SceneNode* part) const;
    static bool isLimb(const SceneNode* part);   // an arm or a leg (walking swings these)
    // The walk cycle follows how fast the body really moves over the ground (like
    // Roblox's Animate script): faster = quicker, longer strides. `speed` in units / s.
    static float strideRate(float speed)  { return speed * 1.8f; }                        // radians of the cycle per second
    static float strideSwing(float speed) { return speed < 0.05f ? 0.0f : std::min(62.0f, 18.0f + speed * 4.4f); }   // degrees
    glm::vec3 velocity() const { return m_velocity; }
    void      launch(const glm::vec3& v) { m_velocity = v; m_grounded = false; }   // jump pads etc.

    // --- Appearance ----------------------------------------------------------
    BodyColors bodyColors() const;
    void       setBodyColors(const BodyColors& c);
    HatStyle   hat() const { return m_hat; }
    void       setHat(HatStyle style, glm::vec3 tint = glm::vec3(-1.0f));
    void       rememberHat(HatStyle style) { m_hat = style; }   // no rebuild (loading)
    // Clothing pictures (the 585 x 559 template): "gb:<id>" or a file; "" = none.
    // The shirt goes on the torso and arms, pants on the legs (and the torso if no shirt).
    void       setClothing(const std::string& shirt, const std::string& pants, const std::string& tshirt = "");
    // tshirt: a picture flat on the front of the torso (over the shirt, if there is one).
    static void applyClothing(SceneNode* root, const std::string& shirt, const std::string& pants,
                              const std::string& tshirt = "");
    // Accessories made in Studio's Accessory window: kind ("hat", "hair", "faceacc",
    // "neck", "shoulder", "waist") -> "gb:<id>" or a file holding the accessory.
    // Ones not downloaded yet are skipped (set them again once they arrive).
    using Accessories = std::map<std::string, std::string>;
    void        setAccessories(const Accessories& acc);
    void        rememberWornRest();   // (Play) where new hats / accessories sit before animating
    static void applyAccessories(Scene& scene, SceneNode* root, const Accessories& acc);
    static void hideBuiltInHat(SceneNode* root);
    // A face picture instead of the smiley ("gb:<id>" or a file; "" = the smiley).
    void        setFace(const std::string& face);
    static void applyFace(Scene& scene, SceneNode* root, const std::string& face);
    // An accessory file's contents -> its objects (placed relative to the character's feet).
    static std::unique_ptr<SceneNode> accessoryFrom(const std::string& json);

    static const char* hatName(HatStyle s);

    // --- Building characters (also used for other players' characters) ----
    static SceneNode* buildRig(Scene& scene, const std::string& name, const glm::vec3& feet);
    // The default character model (assets/models/player.obj) for one rig part; false if it has none.
    static bool usePlayerModel(SceneNode& part);
    // A character saved before that model: give it the model's parts.
    static void upgradeRig(SceneNode* rig);
    static void addFace(SceneNode* head);   // the default smiley (a flat picture) on a head
    static void removeOldFace(Scene& scene, SceneNode* head);   // old 3D / card faces
    void        upgradeFace();              // old saved characters: new face
    static void       applyColors(SceneNode* root, const BodyColors& c);
    // `tint` recolours the hat (catalog hats); negative = its normal colours.
    static void       applyHat(Scene& scene, SceneNode* root, HatStyle style, glm::vec3 tint = glm::vec3(-1.0f));
    static CharacterPose capturePose(const SceneNode* root);
    static void          applyPose(SceneNode* root, const CharacterPose& pose);
    static std::vector<std::pair<const char*, BodyColors>> colorPresets();

private:
    SceneNode* part(const char* name) const;
    void animate(float dt, float groundSpeed, bool grounded);
    void footsteps(bool running, const glm::vec3& at);   // loop the running sound while on the ground
    void updateGrip();                // put the held tool's Handle in the right hand
    void syncSlots();
    void startDeath();
    void bleed(float damage);
    void updateDeath(float dt, Physics& physics);
    void respawn(bool firstSpawn = false);   // firstSpawn: arriving in the game (a different sound)

    std::vector<uint64_t> m_slots;    // tool ids in hotbar order
    float    m_holdBlend = 0.0f;       // right arm raised to hold a tool
    float    m_toolSwing = 0.0f;       // seconds left of the swing animation
    uint64_t m_checkpoint = 0;         // RespawnLocation / last Checkpoint touched
    bool      m_neutral = true;        // no team (see setTeam)
    glm::vec3 m_teamColor{1.0f};
    float     m_aliveTime = 0.0f;
    static constexpr float kToolSwingTime = 0.4f;
public:
    bool climbing() const { return m_climbing; }
    bool grounded() const { return m_grounded; }
    bool swimming() const { return m_swimming; }
    bool underwater() const { return m_underwater; }   // swimming with the head under
    float dripping() const { return m_drip; }          // just out of the water: 1 soaked .. 0 dry
    // Swimming controls for the next update: look = the camera's up/down (-1 looking
    // straight down .. 1 straight up), down = the dive key (C / Ctrl).
    void setSwimInput(float look, bool down) { m_swimLook = look; m_swimDown = down; }
    // Is this point in water (a Water part, waves, a flood)? The surface above it, how
    // the water flows and its colour.
    static bool waterAt(Scene& scene, const glm::vec3& p, float* top = nullptr, glm::vec3* flow = nullptr,
                        glm::vec3* color = nullptr);
    static constexpr float kFloatHeight = 1.45f;   // floating: the water comes up to here (the shoulders)
    // Parts you climb (TrussPart, anything called Ladder, or tagged / attributed "Climbable")
    // and swim in (called Water, or tagged / attributed "Water").
    static bool isClimbable(const SceneNode* n);
    static bool isWater(const SceneNode* n);
    // Seats, like Roblox's: touch one and you sit on it (facing its Front), riding
    // along if it moves; jump to get up. A part is a seat when it's called Seat /
    // VehicleSeat or tagged "Seat"; an attribute Disabled = true switches it off.
    static bool isSeat(const SceneNode* n);
    static bool seatDisabled(const SceneNode* n);
    bool        sitting() const { return m_seatId != 0 || m_sitHere; }
    uint64_t    seatId() const { return m_seatId; }
    void        sit(SceneNode* seat);        // (scripts: Seat:Sit(humanoid))
    // Sit down right where you stand, no seat needed (Humanoid.Sit = true, or "/e sit").
    // In the air you sit as soon as you land. Jump to get up.
    void        sitDown();
    void        standUp(bool jumpOff = false);
private:
    bool     m_respawnedFlag = false;
    Scene*   m_scene  = nullptr;
    uint64_t m_rootId = 0;
    Humanoid m_humanoid;
    HatStyle m_hat = HatStyle::None;
    glm::vec3 m_hatTint = glm::vec3(-1.0f);
    std::string m_shirt, m_pants, m_tshirt, m_face;
    Accessories m_accessories;

    glm::vec3 m_spawn{0.0f};
    glm::vec3 m_velocity{0.0f};
    bool      m_grounded = false;
    // Steps and curbs: the physics lifts the feet straight up; on screen the body
    // catches up over a moment instead of snapping (m_stepShown is in the root's height now).
    float     m_stepOffset = 0.0f, m_stepShown = 0.0f;
    glm::vec3 m_shownAt{0.0f};

    // Moving platforms carry the character.
    uint64_t  m_groundId = 0;
    glm::mat4 m_groundPrevM{1.0f};   // where the part we stand on was last frame
    glm::vec3 m_platformVel{0.0f};   // how fast it's carrying us
    static constexpr float kBodyMass = 2.0f;   // for being knocked around by loose parts

    // Walk-cycle animation.
    float m_walkPhase = 0.0f;
    float m_swing     = 0.0f;   // current limb swing amplitude (degrees)
    float m_airBlend  = 0.0f;   // 0 = on ground, 1 = jump pose
    float m_groundSpeed = 0.0f; // how fast we're really moving along the ground (smoothed)
    bool  m_faceLock = false;   // first person: face m_faceYaw next step
    float m_faceYaw = 0.0f;
    // Climbing trusses / ladders, and swimming in water.
    bool  m_climbing = false, m_swimming = false, m_underwater = false;
    float m_swimLook = 0.0f, m_bobPhase = 0.0f;
    float m_drip = 0.0f, m_dripTimer = 0.0f;   // dripping wet after swimming (leaves wet footprints)
    bool  m_swimDown = false;
    float m_climbBlend = 0.0f, m_swimBlend = 0.0f;
    float m_climbPhase = 0.0f;
    float m_climbCooldown = 0.0f;   // just jumped off: don't grab straight back on
    uint64_t m_seatId = 0;          // the seat we're sitting on (0 = standing)
    float m_seatCooldown = 0.0f;    // just got up: don't sit straight back down
    float m_sitBlend = 0.0f;        // 0 standing .. 1 sitting (the pose)
    std::string m_emote;            // playing now ("" = none)
    bool  m_scriptJump = false;      // humanoid.Jump = true (next update)
    glm::vec3 m_scriptMove{0.0f};    // humanoid:Move(direction)
    int   m_emoteTrack = 0;          // its AnimationTrack (0 = the game's Animate script plays it)
    std::vector<int> m_fadingTracks; // emotes letting go (forgotten once they have)
    uint64_t m_gripMade = 0;         // the tool we made a RightGrip for
    float m_emoteTime = 0.0f, m_emoteLength = 0.0f, m_idleTime = 0.0f;
    void  stopEmote(float fade);
    // An animation pack (Emotes::mine().pack): which of its animations plays, and the one before.
    std::string m_packState, m_packPrev;
    float m_packTime = 0.0f, m_packPrevTime = 0.0f, m_packFade = 1.0f, m_packBlend = 0.0f;
    std::vector<uint64_t> m_packMoved;   // parts it moved (put back next frame)
    void  animationPack(const std::string& state, float dt, float groundSpeed);
    float m_airTime = 0.0f;          // how long we've been off the ground
    // Tripped / PlatformStand: the whole body tips over (pitch about the feet) and
    // spins when flung; then it gets back up.
    float m_tripTime = 0.0f, m_tilt = 0.0f, m_tiltVel = 0.0f, m_getUp = 0.0f;
    bool  m_wasTripped = false;
    // Custom animations from the Animate script (parsed once per change).
    struct CustomClip { size_t hash = 0; bool has = false; float time = 0.0f; };
    std::unordered_map<uint64_t, CustomClip> m_customClips;
    bool applyCustomAnimation(const char* state, float dt);
    SceneNode* customAnimation(const char* state) const;   // the Animate script's Animation for it
    bool hasCustomAnimation(const char* state) const;      // ... and it has keyframes
    void  sitStep(float dt, bool jump);   // while sitting: stay on the seat
    void  sitHereStep(float dt, bool jump);   // while sitting on the floor
    bool      m_sitHere = false;     // sitting on the floor (not a seat)
    bool      m_sitPending = false;  // asked to sit in the air: sit on landing
    glm::vec3 m_sitSpot{0.0f};       // where our feet were when we sat down
    // BodyVelocity, BodyGyro... inside the character (flying, jetpacks).
    void  characterMovers(float dt);
    bool  m_gyro = false;            // a BodyGyro / AlignOrientation holds our tilt
    float m_gyroTilt = 0.0f;         // ...at this (degrees, + = leaning forward)
    float m_wet = 0.0f;              // seconds you stay slippery after leaving a stream of liquid (a wet slide)
    int   m_stepSound = 0;      // the looping footsteps sound while running (0 = quiet)

    // Rest pose captured when Play starts (local transforms by node id).
    std::unordered_map<uint64_t, Transform> m_rest;
    Transform m_rootRest;

    // Death ("oof") — parts tumble apart, then the character respawns.
    bool  m_dead = false, m_diedFlag = false;
    float m_deadTime = 0.0f;
    const float m_respawnDelay = 4.0f;
    struct Debris { uint64_t id; glm::vec3 vel; glm::vec3 spin; };
    std::vector<Debris> m_debris;

    Ragdoll   m_ragdoll;
    float     m_pendingForce = 0.0f;
    glm::vec3 m_pendingImpulse{0.0f};
    float     m_lastHealth = 100.0f;
    uint64_t  m_spawnFF = 0;          // the ForceField we gave on spawn
    float     m_spawnFFTime = 0.0f;
};
