#pragma once
#include "SceneNode.h"
#include "Environment.h"
#include "Player.h"
#include "Npc.h"
#include "Water.h"
#include "Terrain.h"
#include "Particles.h"
#include "Blast.h"
#include "Animation.h"
#include "CollisionGroups.h"
#include <functional>
#include <memory>
#include <unordered_map>

// How characters die. Classic = Roblox-style: the parts just fall apart.
// Ragdoll = the body goes limp and tumbles with real joints.
enum class DeathStyle { Classic, Ragdoll };
// Gore = what comes out: nothing, oil & bolts (robots), or blood & guts.
enum class GoreLevel  { Off, OilAndBolts, Blood };

// A player's camera options, like Roblox's Player.CameraMode / CameraMinZoomDistance /
// CameraMaxZoomDistance. The game's defaults are in Studio's Player panel (Roblox's
// StarterPlayer); scripts can change them for one player while playing.
struct CameraOptions {
    bool  lockFirstPerson = false;   // Enum.CameraMode.LockFirstPerson (false = Classic)
    float minZoom = 0.5f;            // studs: how close the mouse wheel can come
    float maxZoom = 60.0f;           // and how far out it can go
    static constexpr float kLimit = 10000.0f;   // the furthest either can be set to
    bool operator==(const CameraOptions& o) const {
        return lockFirstPerson == o.lockFirstPerson && minZoom == o.minZoom && maxZoom == o.maxZoom;
    }
    bool operator!=(const CameraOptions& o) const { return !(*this == o); }
};

// Enum.CameraType. Custom = the normal camera that follows the CameraSubject;
// Scriptable = the engine leaves it alone and shows whatever CFrame scripts set;
// Fixed = it stays where it is (the mouse can still turn it). Roblox's other types
// (Follow, Track, Watch, Attach, Orbital) act like Custom.
enum class CameraType { Custom, Scriptable, Fixed };

// workspace.CurrentCamera while playing. Scripts read and change it (LuaInstance.cpp);
// the play camera (game/PlayCamera.cpp) follows it and writes back where it really is
// every frame. Only on this computer: each player has their own, like Roblox.
struct ScriptCamera {
    CameraType  type = CameraType::Custom;
    std::string typeName = "Custom";     // what a script set it to (Follow etc. read back as set)
    glm::mat4   cframe{1.0f};            // where the camera is; it looks along -Z (LookVector)
    glm::vec3   focus{0.0f};             // the point it orbits (Camera.Focus)
    float       fov = 60.0f;             // FieldOfView: degrees, top to bottom
    uint64_t    subject = 0;             // CameraSubject: a part or a character (0 = your own Humanoid)
    bool        subjectIsHumanoid = true;
    bool        cframeSet = false;       // a script just set CFrame: the play camera takes it
    glm::vec3   fixedAt{0.0f};           // Fixed: where it stays
    glm::vec2   viewport{1.0f, 1.0f};    // ViewportSize, in pixels
    float       orthoHeight = 0.0f;      // orthographic: studs the screen is tall (0 = perspective)
    // The local player's camera options, once a script changed them (else the game's).
    bool          optionsSet = false;
    CameraOptions options;
};

// Rules and physics for the whole place (like Roblox's Workspace properties).
struct WorldSettings {
    float      gravity           = 22.0f;   // units / second²
    float      fallenPartsHeight = -50.0f;  // below this, parts are destroyed and players die

    DeathStyle deathStyle      = DeathStyle::Ragdoll;
    GoreLevel  gore            = GoreLevel::Blood;
    bool       dismemberment   = true;      // big hits can knock limbs off
    bool       fallDamage      = true;
    float      fallDamageSpeed = 20.0f;     // landing faster than this hurts
    float      fallDamageScale = 1.0f;      // how much it hurts (2 = twice as much)

    // What blood looks like (games can make it green slime, blue alien blood...).
    glm::vec3  bloodColor  = {0.50f, 0.02f, 0.03f};
    float      bloodAmount = 1.0f;          // how much sprays out (0.2 = a little, 3 = buckets)
    float      bloodStay   = 30.0f;         // seconds pools and splats stay before drying up
    float      spawnForceField = 4.0f;      // seconds of ForceField after spawning (0 = none)
    bool       playerCollisions = true;     // characters bump into each other (off = walk through)
    CollisionGroups collisionGroups;        // PhysicsService's groups and which of them collide
    bool       voiceChat = true;            // players who turned voice chat on can talk (off = nobody can, in this game)
    int        maxFluidParticles = 100000;  // real liquid: the most drops at once (the oldest are recycled)
    // The players' camera: orthographic = no perspective (2D, isometric and puzzle
    // games). orthographicSize = studs from the bottom of the screen to the top
    // (0 = the mouse wheel zooms like normal).
    bool       orthographic = false;
    float      orthographicSize = 0.0f;
    // Every player's camera to start with: CameraMode and zoom limits (Roblox's StarterPlayer).
    CameraOptions playerCamera;
};

// Another player's character in a multiplayer game. On the host, scripts can
// read and change its Humanoid; the changes are sent to that player.
struct RemoteCharacter {
    int         clientId = 0;
    std::string name;
    uint64_t    rootId = 0;
    Humanoid    humanoid;
    bool        humanoidDirty = false;   // a script changed it: tell its owner
    double      editedAt = -1.0;         // when a script last changed it
    bool        alive = true;
    struct Kill { float force; glm::vec3 impulse; };
    std::vector<Kill> kills;             // violent deaths to send to its owner
    // Their camera options, once a script on the host changed them (sent to them).
    bool          cameraSet = false;
    CameraOptions camera;
    std::vector<uint64_t> toolSlots;     // their hotbar order (the host keeps their tools)
    // Their team (the host's scripts decide; sent to them so they spawn in the right place).
    uint64_t    team = 0;
    bool        neutral = true;
    glm::vec3   teamColor{1.0f};
    bool        teamDirty = false;
};

// A visual effect to show on every player's screen (multiplayer).
struct FxEvent {
    enum Type { Explosion, Blood, Oil, Gibs, Sparks, Sound } type;
    glm::vec3 pos;
    float     amount;              // Sound: volume
    std::string name = {};         // Sound: which sound
};

// Shown on the game's card in the Guts&BoltsPlayer app.
struct GameInfo {
    std::string title       = "My Game";
    std::string description = "A game made with Guts and Bolts.";
    std::string author      = "Builder";
    std::string publishedId;   // the game's id on the Guts&Bolts server, once published
};

class Scene {
public:
    Scene();

    SceneNode*     root()        { return m_root.get(); }
    SceneNode*     selected()    { return m_selected; }
    Environment&   environment() { return m_env; }
    WorldSettings& world()       { return m_world; }
    GameInfo&      info()        { return m_info; }
    Player*        player()      { return m_player.get(); }
    ParticleSystem& particles()  { return m_particles; }
    BlastSystem&    blasts()     { return m_blasts; }      // explosions: shockwaves, smoke, fire, mushroom clouds
    Anim::Animator& animator()   { return m_animator; }   // animations scripts are playing
    ScriptCamera&   camera()     { return m_camera; }     // workspace.CurrentCamera (while playing)
    // This computer's player's camera options: what a script set, or the game's.
    const CameraOptions& cameraOptions() const { return m_camera.optionsSet ? m_camera.options : m_world.playerCamera; }
    // Gore is shown only if the game allows it AND the player hasn't turned it off.
    bool           goreEnabled() const;
    GoreKind       goreKind() const;

    // Selection. select() picks just one thing; addToSelection / toggleSelection
    // build a multi-selection (Ctrl+click). selected() is the one picked last.
    void select  (SceneNode* node);
    void deselect();
    void addToSelection(SceneNode* node);
    void toggleSelection(SceneNode* node);
    const std::vector<SceneNode*>& selection() const { return m_selection; }
    // The selection without anything whose ancestor is also selected
    // (so deleting / copying a model doesn't handle its insides twice).
    std::vector<SceneNode*> selectionRoots() const;

    // Create a Part directly under the Workspace.
    SceneNode* addNode(const std::string& name, PrimitiveType type, std::shared_ptr<Mesh> mesh);
    // Insert an already-built node (and its children) under `parent` (default: Workspace).
    SceneNode* insert(std::unique_ptr<SceneNode> node, SceneNode* parent = nullptr);
    void       removeNode(SceneNode* node);
    // Take a node out of the tree without destroying it (Lua: part.Parent = nil).
    std::unique_ptr<SceneNode> detach(SceneNode* node);
    // Move a node under a new parent, keeping where it is in the world.
    bool reparent(SceneNode* node, SceneNode* newParent);

    SceneNode* findById(uint64_t id);
    void       forEach(std::function<void(SceneNode*)> fn);

    // Nodes the user must not delete / move (Workspace and the character).
    bool isProtected(const SceneNode* node) const;
    // Roblox's container services (ReplicatedStorage, ServerScriptService, StarterPack...)
    // are folders at the top of the scene with those names. Studio shows them as their own
    // rows in the Explorer, next to the Workspace, instead of inside it.
    static const std::vector<const char*>& serviceNames();
    static bool isServiceName(const std::string& name);
    bool isServiceFolder(const SceneNode* node) const;
    SceneNode* serviceFolder(const std::string& name, bool create);
    // True for the character model and every part inside it.
    bool isCharacterPart(const SceneNode* node) const;

    // --- Characters (the local player + other players in multiplayer) ---
    std::vector<RemoteCharacter>& remotes() { return m_remotes; }
    RemoteCharacter* findRemote(uint64_t rootId);
    Humanoid*        humanoidOf(uint64_t rootId);   // local, remote or NPC; null if none
    // Computer-controlled characters (zombies etc.) while the game runs.
    NpcSystem&       npcs() { return m_npcs; }
    // Moving water (waves, floating, splashes) while the game runs.
    WaterSystem&     water() { return m_water; }
    // Hills and valleys sculpted in Studio (empty in most places). See Terrain.h.
    Terrain&         terrain() { return m_terrain; }
    const Terrain&   terrain() const { return m_terrain; }
    // What rays and scripts see when they hit the terrain: an object called
    // "Terrain" (workspace.Terrain in Lua). It's not in the Explorer tree and
    // can't be moved or deleted; findById(kTerrainId) returns it.
    static constexpr uint64_t kTerrainId = 4503599627370000ull;
    SceneNode*       terrainNode() { return m_terrainNode.get(); }
    bool             isCharacterRoot(uint64_t id) const;
    // Kill any character; `force` 0..1 = how violently.
    void             killCharacter(uint64_t rootId, float force, const glm::vec3& impulse);
    void             markHumanoidEdited(uint64_t rootId, double now);

    // Effects the host should show on everyone's screen.
    bool                  recordFx = false;
    std::vector<FxEvent>  fxQueue;
    void pushFx(FxEvent::Type t, const glm::vec3& p, float amount, const std::string& name = {}) {
        if (recordFx) fxQueue.push_back({t, p, amount, name});
    }

    // Wipe everything and build the default starting place.
    void buildDefault();
    // Replace the whole tree (used when loading a file / undoing).
    void replaceRoot(std::unique_ptr<SceneNode> root);
    void markDirty() { m_indexDirty = true; }

private:
    void walk(SceneNode* node, std::function<void(SceneNode*)>& fn);
    void rebuildIndex();

    std::unique_ptr<SceneNode> m_root;
    SceneNode*                 m_selected = nullptr;
    std::vector<SceneNode*>    m_selection;
    void unselectSubtree(SceneNode* node);
    Environment                m_env;
    WorldSettings              m_world;
    GameInfo                   m_info;
    std::vector<RemoteCharacter> m_remotes;
    NpcSystem                  m_npcs;
    WaterSystem                m_water;
    Terrain                    m_terrain;
    std::unique_ptr<SceneNode> m_terrainNode;
    ParticleSystem             m_particles;
    BlastSystem                m_blasts;
    Anim::Animator             m_animator;
    ScriptCamera               m_camera;
    std::unique_ptr<Player>    m_player;

    std::unordered_map<uint64_t, SceneNode*> m_index;
    bool                                     m_indexDirty = true;
};
