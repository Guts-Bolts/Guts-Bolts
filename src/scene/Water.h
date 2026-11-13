#pragma once
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "Liquid.h"
#include "OceanWaves.h"

class Scene;
class SceneNode;

// Real water for parts that are water (see Player::isWater): while the game
// runs, each one gets a moving surface.
//
//  * The surface is a grid of little columns of water. Each column is pulled
//    towards the level of its neighbours, which makes waves that spread out,
//    bounce off the sides and slowly calm down (the "wave equation").
//  * Anything that falls in pushes the surface down (a splash and rings of
//    ripples); people and boats moving through it leave a wake.
//  * The physics asks it "how high is the water here?" so things float on the
//    waves (see buoyancy in RigidBodies.cpp) and swimmers bob up and down.
//
// A water part can have attributes: "Waves" (how tall the ocean swell is, in
// units; or "WaveScale", 1 = half a stud), "Flow" (a Vector3: a current that
// carries things along), "Drag" (how thick it feels: 1 = water, 0 = no push
// at all, 5 = like mud) and "Clarity" (0 murky .. 1 crystal clear). The swell is
// made by an FFT from a wave spectrum (OceanWaves.h). A FluidVolume (from scripts) is a water
// part with these set.
//
// Where the surface moves fast (a splash, a wave slapping a wall) it throws up
// spray: real liquid drops (Liquid) that fall back in and soak into the pool.
//
// Flowing water (Flood.cpp): parts called "WaterSource" pour out water that
// runs downhill, spreads, fills holes, piles up behind walls and pours over
// edges. The ground is a grid of columns; each column pushes water to its
// neighbours when its surface is higher than theirs (the "pipe" model).
class WaterSystem {
public:
    struct Body {
        uint64_t  id = 0;
        glm::vec3 min{0.0f}, max{0.0f};   // the water's box in the world
        int       nx = 0, nz = 0;         // grid points across x and z
        float     cell = 1.0f;            // distance between grid points
        std::vector<float> h, v;          // how far each point is above / below the still level, and its speed
        float     swell = 0.0f;           // how tall the ocean swell is ("Waves", or "WaveScale" x 0.5)
        float     clarity = 0.6f;         // how clear: 1 = crystal, 0 = murky ("Clarity", or from Transparency)
        glm::vec3 flow{0.0f};             // "Flow" attribute
        float     drag = 1.0f;            // "Drag" attribute: how hard it pushes on things moving through it
        glm::vec3 color{0.2f, 0.45f, 0.7f};
        // The current across the surface, one arrow per grid point (x, z): the "Flow"
        // bent around anything solid sticking out of the water (a boulder, a pillar),
        // so it splits around it and speeds up past its sides. Empty = no current.
        std::vector<glm::vec2> flowMap;
        uint64_t  flowKey = 0;            // what it was worked out from (so it's only redone when that changes)
        int       flowVersion = 0;        // goes up when it changes (the renderer re-uploads it)
        float     transparency = 0.4f;
    };

    // Water that's tipped over (a water slide, a sloping river). It isn't part of
    // the wave simulation, which is for level water: its surface is its top face.
    static bool tilted(const SceneNode* water);
    // The height of a tilted water part's top face above (x, z).
    static float tiltedSurface(const SceneNode* water, float x, float z);
    // Is `p` inside this tilted water part? (Its real, turned box, not a box around it.)
    static bool insideTilted(const SceneNode* water, const glm::vec3& p);

    void begin(Scene& scene);   // Play pressed: find every water part
    void end();
    bool active() const { return m_active; }
    void update(float dt, Scene& scene);

    // Is `p` in water? Gives the height of the surface above it and the current.
    bool at(const glm::vec3& p, float* surface = nullptr, glm::vec3* flow = nullptr) const;
    // How thick the water at `p` is (its "Drag"; 1 for plain water).
    float dragAt(const glm::vec3& p) const;
    // The surface height of the water part `id` at (x, z) (its top if it isn't simulated).
    float surfaceOf(const SceneNode* water, float x, float z) const;
    // Push the surface down (amount > 0) or up around `p`: makes ripples.
    void disturb(const glm::vec3& p, float amount, float radius);
    // A tsunami: one huge wave racing out in a ring from `at` (a big explosion in
    // the water). It rises and falls with the real surface (so it's drawn, things
    // float on it and swimmers ride it), shrinks as it spreads, and sweeps up people
    // and loose parts, washing them out past the shore. height / width in units.
    void addSurge(const glm::vec3& at, float height, float speed, float width);
    float surgeHeight() const;   // the tallest one going (0 = none)
    // Something hit the water: ripples, spray and a sound. `size` ~ how big it is.
    void splash(Scene& scene, const glm::vec3& p, float speed, float size);
    // A physics body is in the water this frame (for splashes when it first goes in).
    void touching(Scene& scene, uint64_t id, const glm::vec3& p, float downSpeed, float size);

    // Flowing water poured by WaterSource parts.
    struct Flood {
        glm::vec2 origin{0.0f};            // x, z of the grid's corner
        int       n = 0;                   // n x n cells
        float     cell = 0.5f;
        float     ceiling = 0.0f;          // ground is looked for below this
        std::vector<float> ground, depth;  // floor height and water depth of each cell
        std::vector<float> out[4];         // water flowing to each neighbour (+x, -x, +z, -z)
        std::vector<glm::vec2> vel;        // which way the water moves in each cell
        glm::vec3 color{0.2f, 0.5f, 0.95f};
        float     transparency = 0.3f;
        float     groundTimer = 0.0f;
        size_t    idx(int i, int k) const { return (size_t)k * n + i; }
    };
    const Flood* flood() const { return m_flood.n ? &m_flood : nullptr; }
    float floodVolume() const;   // how much water there is (units³)

    // Could anything in this box be in water? (A quick check before the detailed one.)
    bool maybeWet(const glm::vec3& min, const glm::vec3& max) const;

    const std::vector<Body>& bodies() const { return m_bodies; }
    float surface(const Body& b, float x, float z) const { return b.max.y + heightAt(b, x, z) + swellAt(b, x, z); }
    // Just the ripples (no swell: the renderer adds the ocean waves itself).
    float rippleSurface(const Body& b, float x, float z) const { return b.max.y + heightAt(b, x, z); }
    // The current at a point on the surface (the flow map, bilinear).
    glm::vec3 flowAt(const Body& b, float x, float z) const;
    // How fast the surface is moving here (splashes, wakes): for foam.
    float churn(const Body& b, float x, float z) const;
    const Body* find(uint64_t id) const;
    float time() const { return m_time; }
    // The ocean swell's waves at this moment (what swellAt floats things on, and what's drawn).
    const OceanWaves::Field& ocean() const { return m_ocean; }

    // Real liquid (drops that flow) poured by FluidSource parts.
    Liquid&       liquid()       { return m_liquid; }
    const Liquid& liquid() const { return m_liquid; }

private:
    void scan(Scene& scene);
    void buildFlowMap(Body& b, Scene& scene);
    Body* bodyAt(const glm::vec3& p, float pad = 0.0f);
    const Body* bodyAt(const glm::vec3& p, float pad = 0.0f) const;
    float heightAt(const Body& b, float x, float z) const;   // grid part only
public:
    // Near the sides of a pool the waves stop moving sideways (0 at the side, 1 two
    // studs in), so the water never pokes through the walls.
    static float edgeFade(const Body& b, float x, float z) {
        const float d = std::min(std::min(x - b.min.x, b.max.x - x), std::min(z - b.min.z, b.max.z - z));
        return std::clamp(d * 0.5f, 0.0f, 1.0f);
    }
private:
    float swellAt(const Body& b, float x, float z) const;
    float surgeAt(const Body& b, float x, float z) const;
    void  stepSurges(float dt, Scene& scene);
    struct Surge {
        uint64_t  body = 0;
        glm::vec3 center{0.0f};
        float radius = 0.0f, speed = 20.0f, height = 1.0f, width = 3.0f, start = 3.0f, maxRadius = 100.0f;
        float pushTimer = 0.0f;
        std::vector<uint64_t> swept;   // people already picked up by it
    };
    std::vector<Surge> m_surges;
    // Flowing water (Flood.cpp).
    struct Source { uint64_t id; glm::vec3 pos; float radius, rate; };
    void scanSources(Scene& scene);
    void buildGround(Scene& scene);
    void stepFlood(float dt, Scene& scene);
    bool floodAt(const glm::vec3& p, float* surface, glm::vec3* flow) const;
    Flood m_flood;
    std::vector<Source> m_sources;
    float m_streamTimer = 0.0f;

    std::vector<Body> m_bodies;
    OceanWaves::Field m_ocean;
    Liquid m_liquid;
    bool  m_active = false;
    float m_time = 0.0f;
    glm::vec3 m_viewer{0.0f};
    float m_viewDist = 0.0f;
public:
    // Where the camera is and the render distance (0 = no limit): water further away
    // than that stops making waves until you come closer (less lag).
    void setViewer(const glm::vec3& p, float dist) { m_viewer = p; m_viewDist = dist; m_liquid.setViewer(p, dist); }
    // Wet patches: splashes and dripping swimmers leave the things around them wet
    // (darker and shinier) for a while, drying from the edges.
    struct WetSpot { glm::vec3 pos; float radius; float born; };
    void addWetSpot(const glm::vec3& p, float radius);
    const std::vector<WetSpot>& wetSpots() const { return m_wetSpots; }
    float wetness(const WetSpot& s) const;   // 1 soaked .. 0 dry
private:
    std::vector<WetSpot> m_wetSpots;
private:
    std::unordered_map<uint64_t, float> m_lastWet;           // when each body was last in the water
    std::unordered_map<uint64_t, glm::vec3> m_charPrev;      // characters' last positions (for wakes)
    std::unordered_map<uint64_t, bool> m_charWet;
    float m_splashSound = 0.0f;
    float m_scanTime = 0.25f;
};
