#pragma once
#include <glm/glm.hpp>
#include <cstdint>

class Scene;

// Severe weather: the supercell (a big rotating thunderstorm), tornadoes and downbursts.
//
// What is where is kept in the Lighting (Environment: stormCenter, tornado, tornadoPos,
// downburst...), so it is saved with the look of the world and sent to every player like
// the rest of it. The host decides what happens (simulate); every computer then follows
// those numbers smoothly (follow) and works out the wind from them (windAt), so loose
// parts on the host and each player's own character feel the same storm.
//
// How the real things work, and what is copied here:
//  - A supercell is one huge storm cloud turning slowly around its updraft. Rain and
//    hail fall on one side of it; under the rain-free base on the other side a lower
//    "wall cloud" hangs down, and that is where tornadoes form.
//  - A tornado is a spinning column of air from the wall cloud to the ground. Close to
//    its middle the air turns like a solid wheel (faster further out); outside that core
//    the wind drops off with distance (a Rankine vortex). Air near the ground is sucked
//    in and goes up the middle. Strength is the EF scale, 0 (weak) to 5 (the worst).
//  - A downburst is cold, rain-heavy air dropping out of the storm. It hits the ground
//    and bursts outwards in every direction, strongest in a ring around where it landed.
namespace Storms {

constexpr float kCloudBase = 240.0f;       // studs above the ground: the flat bottom of storm clouds
constexpr float kFunnelHeight = 250.0f;    // a tornado's wind reaches this high above its foot
constexpr float kMaxTornado = 5.0f;        // EF5
constexpr float kDownburstRadius = 45.0f;  // the falling column of air
constexpr float kWallCloudOffset = 90.0f;  // the wall cloud hangs this far from the storm's middle

// A tornado's core: inside this radius the air turns like a wheel.
inline float coreRadius(float strength) { return 4.0f + 3.0f * strength; }
// The fastest wind in it, at the edge of the core (studs / second). Scaled down from
// the real EF scale (a real EF5 is over 320 km/h) to speeds a game world can hold.
inline float peakWind(float strength) { return 35.0f + 25.0f * strength; }

// What every computer keeps about the storm while it runs. Not saved, not sent.
struct State {
    // Followed smoothly from the Lighting's numbers (see follow()).
    float     tornado = 0.0f;          // strength right now, 0 = none
    glm::vec3 tornadoPos{0.0f};        // its foot on the ground
    float     downburst = 0.0f;        // 0..1 right now
    glm::vec3 downburstPos{0.0f};
    bool      fresh = false;           // follow() ran since the renderer last looked
    // The host's plan (simulate()).
    bool      cellPlaced = false;
    glm::vec3 cellHeading{1.0f, 0.0f, 0.0f};
    float     nextTornado = -1.0f, nextDownburst = -1.0f;   // seconds until one forms by itself (< 0 = not planned)
    float     tornadoAge = 0.0f, tornadoLife = 0.0f, tornadoPeak = 0.0f;
    glm::vec3 tornadoAt{0.0f};
    float     downburstAge = 0.0f, downburstLife = 0.0f, downburstPeak = 0.0f;
    // Earthquake: followed (quake) and the host's plan.
    float     quake = 0.0f;                // how hard the ground shakes right now, 0..1
    float     quakeClock = 0.0f;           // (runs the shaking pattern)
    float     quakeAge = 0.0f, quakeLife = 0.0f, quakePeak = 0.0f;
    float     damage = 0.0f;               // seconds until the next look for things to break
    float     publish = 0.0f;
    float     clock = 0.0f;
    uint32_t  rng = 0x51ED270Bu;
};

// The host, once a frame while the game runs: moves the supercell, lets tornadoes and
// downbursts form (when Lighting.SevereWeather is on), ages the ones there are, and
// writes the result into the Lighting a few times a second.
void simulate(Scene& scene, float dt);
// Every computer, once a frame: follow the Lighting's numbers smoothly.
void follow(Scene& scene, float dt);
// Not running a game (Studio while building): show the Lighting's numbers as they are.
void snap(Scene& scene);

// Start one now (the host; scripts: Lighting:SpawnTornado / SpawnDownburst).
void spawnTornado(Scene& scene, const glm::vec3& foot, float strength, float seconds = 75.0f);
void spawnDownburst(Scene& scene, const glm::vec3& ground, float strength = 1.0f, float seconds = 22.0f);
void clear(Scene& scene);   // stop every tornado and downburst now

// ---- Earthquakes ----------------------------------------------------------------
// The ground doesn't really move here: instead everything loose is pushed the other
// way, which is the same thing seen from the ground. Like a real one it comes in two
// parts: a light, fast trembling first (the P wave), then the heavy rolling that does
// the damage (the S wave), dying away slowly.
// Magnitude, like the news says it: 4 rattles the shelves, 6 slides furniture and
// cracks weak walls, 7 knocks people over and brings Breakable things down, 8+ wrecks.
void  startEarthquake(Scene& scene, float magnitude, float seconds = 20.0f);
void  stopEarthquake(Scene& scene);
float quakeStrength(float magnitude);            // 0..1 at its worst
// How the ground is being thrown about right now (studs / second², mostly sideways).
glm::vec3 quakeAccel(const State& st);
// How hard a camera at `at` should shake (earthquake, and the rumble near a tornado).
float cameraShake(const Scene& scene, const glm::vec3& at);
// Hail: how much of what falls at `p` is ice (0..1). Under a supercell's core only.
float hailAt(const Scene& scene, const glm::vec3& p);

// Is anything blowing besides the plain wind? (Cheap: lets physics skip the rest.)
bool active(const Scene& scene);
// The wind at a place: workspace.GlobalWind plus the tornado's and the downburst's.
glm::vec3 windAt(const Scene& scene, const glm::vec3& p);
// Just the storm's part of it.
glm::vec3 tornadoWind(const State& st, const glm::vec3& p);
glm::vec3 downburstWind(const State& st, const glm::vec3& p);
// How much harder the air pushes there (1 = as usual): a tornado's air is thick with
// dust, rain and flying bits, so it hits far harder than clean wind of the same speed.
float airBoost(const Scene& scene, const glm::vec3& p);
// The push on a character there, as a change of speed per second.
glm::vec3 characterPush(const Scene& scene, const glm::vec3& p, const glm::vec3& velocity);

// Where the wall cloud hangs under a supercell whose middle is `center` (the tornado's
// own place, once there is one).
glm::vec3 wallCloud(const Scene& scene);

} // namespace Storms
