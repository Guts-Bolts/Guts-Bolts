#pragma once
#include "../core/IdMap.h"
#include "SceneNode.h"
#include "Environment.h"
#include "Storms.h"
#include "Player.h"
#include "Npc.h"
#include "Water.h"
#include "Terrain.h"
#include "Particles.h"
#include "Blast.h"
#include "Animation.h"
#include "CollisionGroups.h"
#include "Vehicles.h"
#include "PlayerListStyle.h"
#include <functional>
#include <map>
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

// The engine's own on-screen things a game can switch off while playing, like Roblox's
// StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.EmotesMenu, false). Only on this computer.
// (The emote menu listens; the other types are remembered for scripts that read them back.)
struct CoreGuiState {
    static constexpr const char* kTypes[] = {"PlayerList", "Health", "Backpack", "Chat", "EmotesMenu", "SelfView", "Captures"};
    std::map<std::string, bool> on;   // type -> shown (missing = shown)
    bool emotesOpen = false;          // the emote menu is open (B, its button, or GuiService:SetEmotesMenuOpen)
    // Spectate mode (StarterGui:SetCore("Spectate", on) / SetCore("SpectateWhenDead", on)).
    bool spectate = false;            // a script wants this player watching others
    bool spectateWhenDead = true;     // watch someone else while waiting to respawn
    static bool known(const std::string& type) {
        if (type == "All") return true;
        for (const char* t : kTypes) if (type == t) return true;
        return false;
    }
    void set(const std::string& type, bool shown) {
        if (type == "All") { for (const char* t : kTypes) on[t] = shown; }
        else on[type] = shown;
        if (!enabled("EmotesMenu")) emotesOpen = false;
    }
    bool enabled(const std::string& type) const {
        if (type == "All") { for (const char* t : kTypes) if (!enabled(t)) return false; return true; }
        auto it = on.find(type);
        return it == on.end() || it->second;
    }
};

// Gravity: a bit lighter than Roblox's (98.1 at our half size), which made
// everything drop like a rock. Jumps are still quick and snappy, but you hang in
// the air a moment longer and falls feel less heavy. (Earth's is about 17.5 here,
// as one unit is about 56 cm, but that feels floaty with jumps this high.)
constexpr float kDefaultGravity = 60.0f;
// Roblox's 196.2 studs/s² at our half size: the default from 0.7.10 until the Softer Landing update, and
// what an imported Roblox game keeps (its scripts were made for it).
constexpr float kRobloxHalfGravity = 98.1f;
// What gravity was before 0.7.10. Games saved back then (and with Roblox gravity)
// are scaled to the new default when they load, and things thrown "as high as
// before" use it.
constexpr float kClassicGravity = 22.0f;

// Rules and physics for the whole place (like Roblox's Workspace properties).
struct WorldSettings {
    float      gravity           = kDefaultGravity;   // units / second²
    float      fallenPartsHeight = -50.0f;  // below this, parts are destroyed and players die

    DeathStyle deathStyle      = DeathStyle::Ragdoll;
    GoreLevel  gore            = GoreLevel::Blood;
    bool       dismemberment   = true;      // big hits can knock limbs off
    bool       knockdowns      = true;      // Ragdoll style: big hits knock you over as a ragdoll, then you get back up
    bool       fallDamage      = true;
    float      fallDamageSpeed = 33.0f;     // landing faster than this hurts (a fall of about 9 units)
    float      fallDamageScale = 1.0f;      // how much it hurts (2 = twice as much)

    // How much harder something has to be thrown to fly as high as it did under
    // the old gravity (about 2.1 with the default): knockbacks, explosions, waves.
    float launchScale() const { return std::sqrt(std::max(0.0f, gravity) / kClassicGravity); }
    // Water, floods and blood drops keep real-world gravity (the old 22, scaled
    // with the game's gravity), so they look real and the liquid stays stable.
    float fluidGravity() const { return gravity * (kClassicGravity / kDefaultGravity); }

    // What blood looks like (games can make it green slime, blue alien blood...).
    glm::vec3  bloodColor  = {0.50f, 0.02f, 0.03f};
    float      bloodAmount = 1.0f;          // how much sprays out (0.2 = a little, 3 = buckets)
    float      bloodStay   = 30.0f;         // seconds pools and splats stay before drying up
    // Blood runs as real liquid too (drips, streams and pools that flow downhill), using
    // the water simulation with thick, sticky settings of its own. Follows Gore.
    bool       liquidBlood = false;
    // Dents from hits on bodies slowly spring back (soft tissue); very big hits tear.
    bool       softTissue  = true;
    // Wind for cloth, flags and capes (Roblox's Workspace.GlobalWind), studs / second.
    glm::vec3  globalWind{0.0f};
    // The air (Roblox's Workspace.AirDensity, same units as part densities): loose parts
    // are slowed by it and pushed by the wind. Real-world thickness by default, so only
    // light, wide parts really feel it. 0 = no air (like before 0.8).
    float      airDensity = 0.0012f;
    // Realism (Realism.h): a hurt leg limps and a hurt arm is held; running tires you out
    // (off to start with: it changes how a game plays); fire spreads to wood and cloth.
    bool       injuries   = true;
    bool       stamina    = false;
    bool       fireSpread = false;
    // Sound that knows the room (Acoustics.h): muffled behind walls, echoes, Doppler.
    bool       acoustics  = true;
    // Things knocking into each other make the sound of what they're made of.
    bool       impactSounds = true;
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
    // Loading big maps in pieces (Roblox's StreamingEnabled / StreamingTargetRadius):
    // joined players only get the parts of the map within streamRadius studs of them.
    bool       streaming    = false;
    float      streamRadius = 512.0f;
    // Every player's camera to start with: CameraMode and zoom limits (Roblox's StarterPlayer).
    CameraOptions playerCamera;
    // The in-game player list (leaderboard): shown or not, its colours and title.
    PlayerListStyle playerList;
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
    bool        respawnNow = false;      // a script called player:LoadCharacter(): tell them to respawn
    std::string kick;                    // a script called player:Kick(reason): send them away ("" = no)
    bool        kicked = false;
};

// A visual effect to show on every player's screen (multiplayer).
struct FxEvent {
    enum Type { Explosion, Blood, Oil, Gibs, Sparks, Sound } type;
    glm::vec3 pos;
    float     amount;              // Sound: volume
    std::string name = {};         // Sound: which sound
    uint64_t  node = 0;            // Sound: the Sound object it came from (its effects and SoundGroup), 0 = none
    bool      at3d = true;         // Sound: from `pos` (false = heard the same everywhere)
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
    const Environment& environment() const { return m_env; }
    // Severe weather while it runs (Storms.h): not saved.
    Storms::State&       storms() { return m_storms; }
    const Storms::State& storms() const { return m_storms; }
    WorldSettings& world()       { return m_world; }
    const WorldSettings& world() const { return m_world; }
    GameInfo&      info()        { return m_info; }
    Player*        player()      { return m_player.get(); }
    ParticleSystem& particles()  { return m_particles; }
    BlastSystem&    blasts()     { return m_blasts; }      // explosions: shockwaves, smoke, fire, mushroom clouds
    Anim::Animator& animator()   { return m_animator; }   // animations scripts are playing
    ScriptCamera&   camera()     { return m_camera; }     // workspace.CurrentCamera (while playing)
    CoreGuiState&   coreGui()    { return m_coreGui; }    // StarterGui:SetCoreGuiEnabled, the emote menu (while playing)
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
    // Roblox's container services (ReplicatedStorage, ServerScriptService, StarterPack,
    // StarterPlayer (its StarterPlayerScripts folder)...)
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
    // VehicleSeats: who's driving what, and the host spinning the wheels (Vehicles.h).
    Vehicles&        vehicles() { return m_vehicles; }
    // Hills and valleys sculpted in Studio (empty in most places). See Terrain.h.
    Terrain&         terrain() { return m_terrain; }
    const Terrain&   terrain() const { return m_terrain; }
    // What rays and scripts see when they hit the terrain: an object called
    // "Terrain" (workspace.Terrain in Lua). It's not in the Explorer tree and
    // can't be moved or deleted; findById(kTerrainId) returns it.
    static constexpr uint64_t kTerrainId = 4503599627370000ull;
    SceneNode*       terrainNode() { return m_terrainNode.get(); }
    // workspace.CurrentCamera as an object that can hold things (screen effects put in
    // the Camera, like Roblox). Only on this computer: it isn't saved or sent to other
    // players, and it's emptied when a game is loaded. findById(kCameraId) returns it.
    static constexpr uint64_t kCameraId = 4503599627370001ull;
    SceneNode*       cameraNode() { return m_cameraNode.get(); }
    // The Lighting service's folder (its screen effects and Sky), made when asked.
    SceneNode*       lightingFolder(bool create) { return serviceFolder("Lighting", create); }
    bool             isCharacterRoot(uint64_t id) const;
    // Kill any character; `force` 0..1 = how violently.
    void             killCharacter(uint64_t rootId, float force, const glm::vec3& impulse);
    void             markHumanoidEdited(uint64_t rootId, double now);
    // Outfits put on by scripts (humanoid:ApplyDescription, see Player::dressRig): each
    // character's look as JSON (the avatar format), and a count that goes up every time
    // it changes, so the host knows to send that character to everyone again.
    struct Look { nlohmann::json look; int version = 0; };
    std::unordered_map<uint64_t, Look>& looks() { return m_looks; }
    // Gone once this scene is: lets a download that finishes later check it's still here.
    std::weak_ptr<int> alive() const { return m_alive; }

    // Effects the host should show on everyone's screen.
    bool                  recordFx = false;
    std::vector<FxEvent>  fxQueue;
    void pushFx(FxEvent::Type t, const glm::vec3& p, float amount, const std::string& name = {}, uint64_t node = 0, bool at3d = true) {
        if (recordFx) fxQueue.push_back({t, p, amount, name, node, at3d});
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
    Storms::State              m_storms;
    GameInfo                   m_info;
    std::vector<RemoteCharacter> m_remotes;
    std::unordered_map<uint64_t, Look> m_looks;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
    NpcSystem                  m_npcs;
    WaterSystem                m_water;
    Vehicles                   m_vehicles;
    Terrain                    m_terrain;
    std::unique_ptr<SceneNode> m_terrainNode;
    std::unique_ptr<SceneNode> m_cameraNode;
    ParticleSystem             m_particles;
    BlastSystem                m_blasts;
    Anim::Animator             m_animator;
    ScriptCamera               m_camera;
    CoreGuiState               m_coreGui;
    std::unique_ptr<Player>    m_player;

    IdMap<SceneNode*>                        m_index;   // (an IdMap: rebuilt often, so it mustn't ask for memory each time)
    bool                                     m_indexDirty = true;
};
