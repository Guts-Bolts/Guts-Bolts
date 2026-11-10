#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <glm/glm.hpp>

class Scene;
class SceneNode;
class LiquidGpu;

// Real liquid, like Blender's fluid simulations: lots of little drops that
// pull and push on each other so they behave like water (Position Based Fluids).
// It pours out of FluidSource parts, runs downhill, fills dips, splashes off
// parts (tilted and curved ones, and the triangles of custom meshes), carries
// people and floating things along, and soaks into pools of still water. The
// renderer melts the drops into one smooth, see-through, shiny surface.
//
// Where the graphics card can run compute shaders (OpenGL 4.3 / OpenGL ES 3.1)
// the physics runs there (LiquidGpu: up to a million drops); otherwise on the
// computer's cores (up to kMaxDrops).
//
// Only while the game runs. Parts called "FluidSource" (or tagged FluidSource)
// pour it out of their front (LookVector). Attributes:
//   Speed  how fast it pours out (default 8; 0 turns it off). The stream is as
//          wide and tall as the part, so a bigger part pours more.
//   Rate   the most drops it makes each second (default 600)
class Liquid {
public:
    static constexpr float kSpacing = 0.5f;          // how far apart drops sit at rest
    static constexpr float kRadius  = 0.3f;          // a drop's size (for collisions and drawing)
    static constexpr int   kMaxDrops = 14000;       // without the graphics card
    // PBF tensile-instability correction (Macklin & Müller): deliberately small;
    // keep the density relaxation and rest-density terms tuned with this value.
    static constexpr float kTensileCorrection = 0.02f;

    Liquid();
    ~Liquid();
    Liquid(const Liquid&) = delete;
    Liquid& operator=(const Liquid&) = delete;

    // The renderer says when a graphics context exists (+1 when made, -1 when gone):
    // only then can the physics run on the graphics card.
    static void graphicsContext(int change);

    void begin(Scene& scene);
    void end();
    void update(float dt, Scene& scene);
    bool active() const { return m_active; }

    // Drops for drawing, two vec4s each: (position, w) and (shape axis, flatness).
    // w = kind of liquid x 4096 + neighbours x 64 + speed. Each drop is drawn as a
    // squashed ball lined up with its neighbours (flat along the surface, round
    // inside), so a surface or a film on the floor comes out smooth, not bumpy.
    // Empty when the graphics card has them (see gpuDrawBuffer()).
    const std::vector<glm::vec4>& drawList() const { return m_draw; }
    // A drop's shape from where its neighbours are (Yu & Turk's anisotropic kernels):
    // the direction they're least spread along, and how flat to make it (1 = round).
    static glm::vec4 dropShape(const glm::vec3& center, const std::vector<glm::vec3>& near, glm::vec3& smoothed);
    size_t count() const;
    bool onGpu() const { return (bool)m_gpu; }
    const char* backend() const { return m_gpu ? "gpu" : "cpu"; }
    // On the graphics card: the buffer of drops (vec4 position, speed), and a
    // buffer with a DrawArraysIndirect command at gpuDrawCommandOffset().
    unsigned gpuDrawBuffer() const;
    unsigned gpuCommandBuffer() const;
    static unsigned gpuDrawCommandOffset();
    // The box around all the liquid and the fastest drop (for tests).
    struct Stats { size_t count = 0; glm::vec3 lo{0.0f}, hi{0.0f}; float fastest = 0.0f; };
    Stats stats() const;
    std::vector<glm::vec4> debugDrops();                // every drop: position, speed (slow)
    float  lastStepMs() const { return m_ms; }   // how long the last update took
    const float* profile() const { return m_prof; }   // move, neighbours, solve, velocity, scan (ms)

    // How much liquid is around `p` (within `radius`): the number of drops, their
    // average velocity and the top of the liquid there. For people and floating things.
    int sample(const glm::vec3& p, float radius, glm::vec3* velocity = nullptr, float* top = nullptr) const;

    // Has liquid run over this part lately? (Wet things are slippery: a water slide
    // stays slick even where the stream has thinned out.)
    bool isWet(uint64_t partId) const;

    static bool isSource(const SceneNode* n);

    // Kinds of liquid (FluidSystem objects; number 0 is plain water). Each drop
    // belongs to one: its colour, how thick it is and how much it sticks together.
    struct Fluid { uint64_t id = 0; glm::vec3 color{0.12f, 0.42f, 0.62f}; float viscosity = 0.015f, tension = 0.08f; };
    static constexpr int kMaxFluids = 16;
    const std::vector<Fluid>& fluids() const { return m_fluids; }
    // Liquid blood and oil (workspace.LiquidBlood with Gore on): kinds of their own, thick
    // and sticky, so they drip, run and pool but never mix into the water. -1 = none.
    int goreFluid(bool oil) const { return oil ? m_oilFluid : m_bloodFluid; }
    // The most drops there can be right now (workspace.MaxFluidParticles, and what
    // this computer can do). When it's full the oldest drops are recycled.
    size_t capacity() const { return m_cap; }
    // The camera and the render distance (0 = no limit): taps further away than that
    // stop pouring until you come closer.
    void setViewer(const glm::vec3& p, float dist) { m_viewer = p; m_viewDist = dist; }
    // Throw one drop of liquid (spray from a splash or a wave slapping a wall). It
    // joins the rest next step, falls back and soaks into the pool it lands in.
    void spray(const glm::vec3& p, const glm::vec3& v, int fluid = 0) {
        if (m_active && m_spray.size() < 4000) { m_spray.push_back(glm::vec4(p, (float)fluid)); m_spray.push_back(glm::vec4(v, 0.0f)); }
    }

private:
    struct Collider {
        uint64_t  id = 0;
        int       shape = 0;          // 0 box, 1 sphere (ellipsoid), 2 cylinder (along local y), 3 triangle
                                      // (a triangle keeps its corners in center, axis[0], axis[1]; axis[2] = its normal)
        glm::vec3 center{0.0f};
        glm::vec3 axis[3];            // unit axes
        glm::vec3 half{0.5f};         // half size along each axis
        glm::vec3 velocity{0.0f};     // moving parts push the water
        glm::vec3 min{0.0f}, max{0.0f};
    };
    struct Source { uint64_t id; glm::vec3 pos, dir, side, up; float rate, speed, width, height, carry = 0.0f; int fluid = 0;
                    bool part = false; };   // part: a FluidSource part (Speed 0 turns it off)

    void scan(Scene& scene);
    void buildColliderGrid();
    // New drops from the taps: straight into the drop lists, or into `out`
    // (position, velocity pairs) for the graphics card. At most `room` drops.
    void emit(float dt, size_t room, std::vector<glm::vec4>* out = nullptr);
    void updateGpu(float dt, Scene& scene);
    // How many drops there can be (m_cap), and recycling the oldest when it's full (m_maxAge).
    void budget(Scene& scene, size_t count, float dt);
    // Push a drop out of solid things; with dt > 0, also rub it along them (friction).
    // `prev` is where the drop was before this step (it goes back out the way it came).
    void collide(glm::vec3& p, glm::vec3& v, float dt, const glm::vec3& prev) const;
    void buildGrid(const std::vector<glm::vec3>& pos);
    template <typename F> void forNeighbours(const glm::vec3& p, F&& f) const;
    void step(float dt, Scene& scene);
    void soakIntoPools(Scene& scene);
    void pushThings(float dt, Scene& scene);

    bool m_active = false;
    int  m_bloodFluid = -1, m_oilFluid = -1;
    float m_ms = 0.0f, m_prof[5] = {};
    float m_scanTime = 0.0f, m_rest = 1.0f;
    std::vector<glm::vec3> m_x, m_v, m_p;          // positions, velocities, predicted positions
    std::vector<glm::vec3> m_dp;                   // scratch: position fixes, then velocities
    std::vector<float> m_lambda, m_age, m_near;     // m_near: how many neighbours (for drawing)
    std::vector<uint8_t> m_kind;                   // which Fluid each drop is
    std::vector<Fluid> m_fluids;
    size_t m_cap = kMaxDrops;
    float m_maxAge = 120.0f;                       // drops older than this go (lower while it's full: recycling)
    std::vector<glm::vec4> m_draw;
    std::vector<Source> m_sources;
    std::vector<glm::vec4> m_spray;   // drops thrown in since the last step (position + kind, velocity)
    glm::vec3 m_viewer{0.0f};
    float m_viewDist = 0.0f;
    std::vector<Collider> m_colliders;
    // Colliders by grid cell (broad phase).
    float m_cgCell = 6.0f;
    glm::ivec3 m_cgMin{0}, m_cgDim{0};
    std::vector<int> m_cgStart, m_cgItems;
    // Drops by grid cell (neighbour search), hashed.
    std::vector<int> m_cellStart, m_cellCount, m_sorted;
    std::vector<int> m_neigh;                     // kMaxNeigh per drop
    std::vector<int> m_neighCount;
    std::vector<uint32_t> m_cellOf;
    uint32_t m_mask = 1023;                       // hash table size - 1

    // The graphics card version, and what's been asked of it: "how much liquid
    // is around here?" is answered a frame or two later (see LiquidGpu::Results).
    std::unique_ptr<LiquidGpu> m_gpu;
    std::vector<uint64_t> m_gpuColliderIds;         // the colliders the graphics card had last frame
    // Which parts the liquid touched this frame (one flag per collider), and until when each part is wet.
    std::unique_ptr<std::atomic<uint8_t>[]> m_touched;
    size_t m_touchedCap = 0;
    std::unordered_map<uint64_t, float> m_wetUntil;
    float m_time = 0.0f;
    void markWet(uint64_t id) { m_wetUntil[id] = m_time + 20.0f; }
    std::vector<glm::vec4> m_incoming;
    mutable std::vector<glm::vec4> m_probeAsk;     // asked this frame (centre, radius)
    static int s_contexts;
};
