#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

// The liquid's physics on the graphics card (compute shaders): the same
// Position Based Fluids as Liquid.cpp, but every drop gets its own tiny GPU
// thread, so it handles hundreds of thousands of drops instead of thousands.
//
// Each step:
//   emit      new drops from the taps are copied in
//   predict   gravity moves every drop; walls, parts and meshes stop it
//   sort      drops are sorted by which little grid cell they're in (count,
//             prefix sum, scatter), so neighbours sit next to each other
//   solve     twice: each drop measures how crowded it is (density) and is
//             pushed apart from its neighbours until the liquid doesn't squash
//   velocity  new speeds, plus viscosity (drops drag their neighbours along)
//
// Nothing comes back to the computer except a little "results" block (how
// many drops there are, splashes into pools, and how much liquid is around
// people and floating things); the renderer draws the drops straight from the
// GPU's memory.
class LiquidGpu {
public:
    // What the CPU learns each frame. It comes from a frame or two ago: the
    // graphics card copies it aside and the CPU only picks it up once it's ready,
    // so the CPU never sits and waits for the graphics card to catch up (that
    // waiting used to cost a whole frame's worth of time, every frame).
    struct Results {
        uint32_t count = 0;
        glm::vec3 lo{0.0f}, hi{0.0f};
        float fastest = 0.0f;
        std::vector<glm::vec4> splashes;               // drops that fell into pools: position, speed
        struct Probe { int count = 0; glm::vec3 vel{0.0f}; float top = 0.0f; };
        std::vector<Probe> probes;                     // one per probe in `asked`
        std::vector<glm::vec4> asked;                  // the probes these answer (centre, radius)
        std::vector<uint64_t> wet;                     // parts the liquid ran over (ids)
    };
    struct Pool { glm::vec3 min, max; };               // max.y = the water's surface

    static constexpr int kMaxProbes = 64;
    static constexpr int kMaxPools = 32;
    static constexpr int kMaxSplashes = 64;

    LiquidGpu();
    ~LiquidGpu();
    LiquidGpu(const LiquidGpu&) = delete;
    LiquidGpu& operator=(const LiquidGpu&) = delete;

    // Can this computer do it? (OpenGL 4.3 / OpenGL ES 3.1 and enough buffers.)
    static bool supported(std::string* why = nullptr);
    bool init(std::string& error);                     // compile the compute shaders
    uint32_t capacity() const { return m_cap; }
    static uint32_t maxCapacity();

    // Once a frame, before stepping: what came back from last frame.
    const Results& results() const { return m_res; }
    void beginFrame();
    // Solid things (packed Liquid colliders, 7 vec4s each, and which part each is) and their grid.
    void setColliders(const std::vector<glm::vec4>& packed, const std::vector<uint64_t>& ids,
                      const std::vector<int>& grid, int gridItems,
                      const glm::ivec3& cgMin, const glm::ivec3& cgDim, float cgCell);
    void setPools(const std::vector<Pool>& pools);
    void setMaxAge(float seconds) { m_maxAge = seconds; }
    // Each kind of liquid: viscosity, surface tension (drops carry their kind in X.w / 4096).
    void setFluids(const std::vector<glm::vec2>& params) { m_fluidParams = params; }
    // One physics step. `incoming`: new drops (position, velocity pairs).
    void step(float dt, const glm::vec3& gravity, const std::vector<glm::vec4>& incoming);
    // After the last step: work out the results (read back next frame).
    void endFrame(const std::vector<glm::vec4>& probes);
    void clear();                                      // no drops
    void forget();                                     // the graphics context is gone: just let go of it all

    // For the renderer: the buffer of drops (two vec4s each: (position, w), (shape
    // axis, flatness); see Liquid::drawList) and the draw command.
    unsigned drawBuffer() const { return m_shape; }
    unsigned commandBuffer() const { return m_grid; }
    static constexpr unsigned kDrawCommandOffset = 16;   // bytes into commandBuffer()

    std::vector<glm::vec4> readDrops();               // every drop (slow: tests only)

private:
    void allocate(uint32_t cap);
    void dispatchDrops();                              // one thread per drop (count from the GPU)
    unsigned program(int k) const { return m_prog[k]; }

    unsigned m_prog[16] = {};
    unsigned m_x[2] = {}, m_v[2] = {}, m_p[2] = {};
    unsigned m_scratch = 0, m_lambda = 0, m_key = 0, m_grid = 0, m_coll = 0, m_cgrid = 0, m_in = 0, m_touched = 0, m_shape = 0;
    size_t m_touchedCap = 0;
    int m_cur = 0, m_pcur = 0;
    uint32_t m_cap = 0, m_table = 0;
    size_t m_collCap = 0, m_cgridCap = 0, m_inCap = 0;
    glm::ivec3 m_cgMin{0}, m_cgDim{0};
    float m_cgCell = 6.0f;
    int m_cgItems = 0;
    std::vector<Pool> m_pools;
    std::vector<glm::vec2> m_fluidParams;
    float m_maxAge = 120.0f;
    std::vector<uint64_t> m_collIds;                   // the part behind each collider
    // Results on their way back: a few little buffers the graphics card copies
    // each frame's results into, each with a fence that says when it's done.
    struct Readback {
        unsigned buf = 0;
        void* fence = nullptr;                         // (a GLsync)
        std::vector<glm::vec4> probes;
        std::vector<uint64_t> ids;                     // the colliders' parts that frame
        size_t touched = 0;                            // how many touched flags were copied
    };
    static constexpr int kReadbacks = 3;
    Readback m_rb[kReadbacks];
    int m_rbNext = 0;                                  // the slot the next endFrame() fills (the oldest)
    void collect(Readback& r, bool fresh);             // read a finished slot into m_res
    void dropReadbacks();
    Results m_res;
};
