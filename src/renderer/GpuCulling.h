#pragma once
// The graphics card picks what to draw (step 7d of the photoreal upgrade).
//
// Before: every frame the processor looked at every part, threw away the ones off
// screen or past the render distance, picked how detailed each one should be, and sent
// the graphics card a fresh list of every copy to draw.
//
// Now, on computers that can (OpenGL 4.3: DeviceFeatures::GpuCulling), plain solid
// parts that share a mesh (the same ones Instancing.h draws in one go) live in a list on
// the graphics card that is only changed where something changed. Each frame a small
// compute shader goes through that list, one part per thread: is it on screen, is it
// close enough, which detail level? The ones that pass are copied into one region per
// mesh and level, and the shader counts them into a "what to draw" record that the
// graphics card reads by itself (indirect drawing). The processor never waits to hear
// how many there were.
//
// Macs, iPhones, the web Player, older cards and Safe mode keep the old way, which
// draws the same picture.
//
// This file has the parts with no OpenGL in them (the layout of the lists, grouping by
// mesh, and the same maths the shader does) so the tests can check them.
#include "Mesh.h"
#include "../core/IdMap.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace GpuCulling {

// Levels a mesh can have on this path: the full one and up to this many simpler ones
// (MeshLod::kLevels is 3).
inline constexpr int kMaxLods = 4;

// One part in the graphics card's list ("std430" layout: the shader reads it as is).
struct Instance {
    InstanceData draw;      // what the lit shader gets for this copy (Mesh.h)
    glm::vec4    sphere;    // middle and reach (half the diagonal), for the checks
    uint32_t     mesh = 0;  // which entry in the mesh list
    uint32_t     pad[3] = {0, 0, 0};
    bool operator==(const Instance& o) const;
};
static_assert(sizeof(InstanceData) == 112, "the shader reads InstanceData as 112 bytes");
static_assert(sizeof(Instance) == 144, "the shader reads Instance as 144 bytes");

// One mesh: where its "what to draw" records and copy regions start.
struct MeshEntry {
    uint32_t firstCommand = 0;   // its first record (one per level, full first)
    uint32_t levels = 1;         // 1 + how many simpler levels it has
    uint32_t base = 0;           // its first copy region (level n starts at base + n * capacity)
    uint32_t capacity = 0;       // how many parts use it (each level's region holds them all)
    float    lodPixels[kMaxLods] = {0, 0, 0, 0};   // each simpler level's "under this many pixels"
};
static_assert(sizeof(MeshEntry) == 32, "the shader reads MeshEntry as 32 bytes");

// The record the graphics card reads for one indirect draw (OpenGL's own layout).
struct Command {
    uint32_t count = 0;          // corners to draw (the level's index count)
    uint32_t instanceCount = 0;  // copies: the shader counts these up from 0
    uint32_t firstIndex = 0, baseVertex = 0;
    uint32_t baseInstance = 0;   // where its copies start in the output list
};
static_assert(sizeof(Command) == 20, "OpenGL reads an indirect draw as 5 numbers");

// The camera, as the shader sees it.
struct View {
    glm::vec4 planes[5]{};        // Frustum::planes (inside = dot >= 0)
    bool      frustumOn = false;  // false: everything is on screen (a broken matrix)
    glm::vec3 viewPos{0.0f};
    float     viewDist = 0.0f;    // render distance, 0 = no limit
    float     pixelsPerStud = 0.0f;   // 0 = detail levels off
    float     bias = 1.0f;        // MeshLod::pick's bias
    bool      ortho = false;      // orthographic: size doesn't change with distance
};

// What a frame draws this way, worked out by plan().
struct Plan {
    std::vector<Instance>    instances;   // grouped by mesh, in the order meshes were first seen
    std::vector<MeshEntry>   meshes;
    std::vector<const Mesh*> meshPtrs;    // (same order as `meshes`)
    std::vector<Command>     commands;    // instanceCount 0: the shader fills them in
    uint32_t                 outputSize = 0;   // copies the output list must have room for
    void clear();
};

// One part that may be drawn this way.
struct Candidate {
    const Mesh* mesh;
    Instance    inst;
};

// Groups the parts by mesh and lays out the records and regions. Keeps its lists'
// room between frames: once they're big enough, planning asks for no memory.
void plan(const std::vector<Candidate>& parts, Plan& out);

// The shader's checks, done the same way on the processor (for the tests and to
// explain it). Same as SceneRenderer: past the render distance or off screen = hidden.
bool visible(const Instance& i, const View& v);
// Which level the shader picks (the same as SceneRenderer::lodFor).
int pickLevel(const Instance& i, const MeshEntry& m, const View& v);

// How much of the list changed since last time: the first and one past the last
// entries that differ (first == last: nothing). A list that changed size counts from
// the first difference to the end.
void changed(const std::vector<Instance>& before, const std::vector<Instance>& now, size_t& first, size_t& last);

// The compute shader (without its #version line), for the build's shader check.
extern const char* const kShader;

} // namespace GpuCulling

// The graphics card side (GpuCulling.cpp). Only on computers; elsewhere `ready()` is
// always false and the renderer uses the old way.
class GpuCuller {
public:
    GpuCuller() = default;
    ~GpuCuller();
    GpuCuller(const GpuCuller&) = delete;
    GpuCuller& operator=(const GpuCuller&) = delete;

    // Makes the shader and lists the first time. False: this device can't (see why()).
    bool ready();
    const char* why() const { return m_why; }

    // Sends what changed, then runs the checks for this camera.
    void cull(const GpuCulling::Plan& plan, const GpuCulling::View& view);
    // Draws everything that passed: one indirect draw per mesh and level. Returns how
    // many draws that was. The lit shader must be bound with uInstanced on.
    int draw(const GpuCulling::Plan& plan);

    // Copies the graphics card drew last time (read back one frame late, so nothing waits).
    long lastCopies() const { return m_lastCopies; }
    long lastTriangles() const { return m_lastTriangles; }
    // Entries sent to the graphics card last time (0 when nothing moved).
    size_t lastUploaded() const { return m_lastUploaded; }

private:
    void release();
    bool     m_tried = false, m_ok = false;
    const char* m_why = "not tried yet";
    unsigned m_program = 0;
    unsigned m_in = 0, m_meshes = 0, m_commands = 0, m_out = 0;   // buffers
    size_t   m_inRoom = 0, m_meshRoom = 0, m_cmdRoom = 0, m_outRoom = 0;   // (in entries)
    std::vector<GpuCulling::Instance> m_sent;   // what the graphics card's list holds now
    std::vector<GpuCulling::Command>  m_read;   // last frame's records, read back
    std::vector<uint32_t>             m_readCount;   // (their index counts)
    long     m_lastCopies = 0, m_lastTriangles = 0;
    size_t   m_lastUploaded = 0;
    void*    m_fence = nullptr;   // (a GLsync, set after the draws: their records can be read once it passes)
    int      m_locPlanes = -1, m_locFrustum = -1, m_locViewPos = -1, m_locViewDist = -1, m_locPps = -1,
             m_locBias = -1, m_locOrtho = -1, m_locCount = -1;
};
