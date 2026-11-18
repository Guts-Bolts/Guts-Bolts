#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

class Scene;

// Natural disasters a game makes happen: lightning that strikes a chosen player or
// spot, tornadoes that pick people up and throw them, tsunamis that wash over
// everything, and earthquakes of any magnitude. Scripts start them with the
// Disasters service (game:GetService("Disasters"), see LuaDisasters.cpp), or Studio's
// disaster spawner parts start them by themselves.
//
// Who decides what: the host (a player hosting, or a game server machine with nobody
// playing on it) runs every disaster, decides who gets hit, hurt, ragdolled, thrown or
// blown up, and sends the list of disasters to every player about ten times a second.
// Each computer then draws them and feels them on its own character (the push of the
// wind and the water, the shaking ground) from the same numbers, so everybody sees
// the same thing at the same place.
//
// This is on top of the weather's own storms (Storms.h): those keep working as before.
namespace Disasters {

enum class Kind : int { Lightning = 0, Tornado = 1, Tsunami = 2, Earthquake = 3 };
inline constexpr int kKinds = 4;
inline const char* const kKindNames[kKinds] = {"Lightning", "Tornado", "Tsunami", "Earthquake"};
bool kindFromName(const std::string& name, Kind& out);   // any capitals

struct Disaster {
    uint32_t  id = 0;
    Kind      kind = Kind::Tornado;
    glm::vec3 pos{0.0f};          // tornado: its foot; tsunami: the middle of its front; quake: the middle; lightning: where it hits
    glm::vec3 velocity{0.0f};     // tornado: how it drifts along (studs / second)
    glm::vec3 dir{1.0f, 0.0f, 0.0f};   // tsunami: the way it rolls (flat)
    float age = 0.0f;
    float duration = 30.0f;       // seconds (0 = until a script stops it)
    float strength = 2.0f;        // tornado: EF 0-5; earthquake: magnitude 1-9.5; lightning: how bright / loud (0-2)
    float radius = 0.0f;          // tornado: its core (0 = from strength); tsunami: half its width;
                                  // earthquake: how far it reaches (0 = everywhere); lightning: who it hurts around the spot
    float height = 0.0f;          // tsunami: the wave's height; tornado: how high it carries people (0 = from strength)
    float speed = 0.0f;           // tsunami: studs / second
    float distance = 0.0f;        // tsunami: how far it rolls before it's gone (0 = until its duration ends)
    // Tornado tuning (1 = normal).
    float spin = 1.0f;            // RotationSpeed: how fast it whirls people round
    float pull = 1.0f;            // PullForce: how hard it sucks people in
    float lift = 1.0f;            // LiftForce: how fast it lifts them up
    float throwForce = 1.0f;      // ThrowForce: how hard it flings them out
    float throwAfter = 2.5f;      // ThrowAfter: seconds it carries someone before throwing them
    // What it does to people (the host decides).
    float damage = 0.0f;          // lightning: once; the rest: every second someone is in it
    bool  kill = false;           // lightning: kills whoever it hits; tornado: thrown people die; tsunami: drowns on touch
    bool  ragdoll = true;         // knocked over as a floppy ragdoll
    bool  explode = false;        // tornado: people it throws blow up when they land (ExplodeOnImpact)
    bool  loseFooting = true;     // earthquake: people stumble and fall now and then
    bool  destroy = true;         // Breakable parts in the way are smashed
    float shake = 1.0f;           // how much it shakes cameras (1 = normal, 0 = none)
    uint64_t follow = 0;          // tornado: a character it chases; lightning: the one it strikes
    float followSpeed = 0.0f;     // tornado: how fast it chases (0 = 12 studs / second)
    bool  stopping = false;       // :Stop() was called: it fades out and goes
    float fade = 0.0f;            // how much of it there is right now (0..1: it grows in and fades out)
    float lifeLeft = -1.0f;       // (fading out: seconds to go)
    std::vector<uint64_t> released;   // tornado: characters it just threw (it leaves them alone for a few seconds)
    std::vector<float> releasedFor;   // (seconds left, host only)
    uint64_t marker = 0;          // the Studio spawner part it came from (0 = a script)
    bool  struck = false;         // lightning: its hit is done (host)
};

// Something that happened, for the scripts' events (host only).
struct Event {
    enum What { Hit, Thrown, Exploded, Killed, Ended } what;
    uint32_t  disaster;
    uint64_t  character;          // 0 = none (Ended)
    glm::vec3 pos;
};

struct State {
    std::vector<Disaster> list;
    uint32_t nextId = 1;
    std::vector<Event> events;    // waiting for the scripts (LuaDisasters takes them)
    // Who is caught in what (host only).
    struct Victim {
        uint64_t  root = 0;
        uint32_t  disaster = 0;
        float     inside = 0.0f;      // seconds caught
        float     tripAgain = 0.0f;   // seconds until we may knock them over again
        bool      thrown = false, armed = false;
        float     sinceThrow = 0.0f, peakSpeed = 0.0f;
        float     hurtLeft = 0.0f;    // damage to hand out (rounded into whole points)
        bool      seen = false;
    };
    std::vector<Victim> victims;
    struct Track { uint64_t root; glm::vec3 pos, vel; bool fresh; };   // how fast each character moves (host)
    std::vector<Track> tracks;
    float publish = 0.0f;
    std::string published;        // the list as last sent ("" = none)
    bool  markersStarted = false; // Studio spawners with AutoStart went off
    std::vector<std::pair<uint64_t, float>> timers;   // spawner parts waiting to go off: part, seconds to go
    float clock = 0.0f;
    uint32_t rng = 0x7A11BEEFu;
    // New lightning bolts for the renderer to draw (every computer), and thunder.
    struct Bolt { glm::vec3 pos; float strength; uint32_t serial; };
    std::vector<Bolt> bolts;      // the last few (each renderer remembers which serial it drew last)
    uint32_t boltSerial = 0;
    void addBolt(const glm::vec3& p, float strength) {
        bolts.push_back({p, strength, ++boltSerial});
        if (bolts.size() > 16) bolts.erase(bolts.begin());
    }
    std::vector<uint32_t> seenBolts;
};

// ---- The host -------------------------------------------------------------------
// Start one. Gives back its id. `d.id` is ignored.
uint32_t spawn(Scene& scene, Disaster d);
// Sensible numbers for a kind (what scripts get when they leave things out).
Disaster defaults(Kind kind);
Disaster* find(Scene& scene, uint32_t id);
const Disaster* find(const Scene& scene, uint32_t id);
void stop(Scene& scene, uint32_t id, bool now = false);   // fades out (or is gone now)
void stopAll(Scene& scene);
// Forget everything (a playtest starting or stopping): no disasters, no victims, the
// Studio spawners ready to go off again. The bolt count keeps going up, so renderers
// still notice the next bolt.
void reset(Scene& scene);
// Studio spawner parts: a part with a "Disaster" attribute (Tornado, Lightning,
// Tsunami, Earthquake) and the tuning in other attributes. Gives back the id (0 = not one).
uint32_t spawnFromPart(Scene& scene, uint64_t partId);

// Once a frame on every computer while the game runs. `host`: this computer runs the
// disasters (decides who's hit); the others follow the list the host sends.
void update(Scene& scene, float dt, bool host);

// ---- Sending it to everyone -------------------------------------------------------
// The list as text (rounded), and reading it back on a player's computer.
std::string encode(const State& st);
void decode(Scene& scene, const std::string& text);

// ---- What every computer feels ----------------------------------------------------
bool any(const Scene& scene);
// Extra wind (tornadoes, tsunami water) at a place.
glm::vec3 wind(const Scene& scene, const glm::vec3& p);
// How much harder that air (or water) pushes than plain air (1 = as usual, 0 = no extra).
float airBoost(const Scene& scene, const glm::vec3& p);
// The push on this computer's own character there, as a change of speed per second.
glm::vec3 characterPush(const Scene& scene, const glm::vec3& p, const glm::vec3& velocity);
// How hard the ground shakes at a place from disaster earthquakes (0..1).
float quakeAt(const Scene& scene, const glm::vec3& p);
float cameraShake(const Scene& scene, const glm::vec3& at);
// Is `p` somewhere a disaster could be throwing someone about? (anti-cheat leaves them alone)
bool near(const Scene& scene, const glm::vec3& p);
// Under the tsunami's water? Its surface height there (or a very low number when not).
float waterTop(const Disaster& d, const glm::vec3& p);

// Numbers used by both the physics and the drawing.
float tornadoCore(const Disaster& d);      // core radius (studs)
float tornadoReach(const Disaster& d);     // how far out it pulls
float tornadoCarry(const Disaster& d);     // how high it carries people
float tsunamiLength(const Disaster& d);    // the hump of water, front to back
float quakeLevel(const Disaster& d);       // 0..1 right now (with the P and S waves)

} // namespace Disasters
