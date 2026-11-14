// The graphics card picks what to draw (see GpuCulling.h).
#include "GpuCulling.h"
#include "GL.h"
#include "GpuCaps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace GpuCulling {

bool Instance::operator==(const Instance& o) const { return std::memcmp(this, &o, sizeof(Instance)) == 0; }

void Plan::clear() {
    instances.clear();
    meshes.clear();
    meshPtrs.clear();
    commands.clear();
    outputSize = 0;
}

namespace {
// (Kept between frames: planning asks for no memory once they've grown.)
struct PlanScratch {
    IdMap<uint32_t>       slotOf;   // mesh -> its entry
    std::vector<uint32_t> fill;     // where each mesh's next part goes
};
PlanScratch& scratch() {
    static PlanScratch s;
    return s;
}
} // namespace

void plan(const std::vector<Candidate>& parts, Plan& out) {
    out.clear();
    PlanScratch& s = scratch();
    s.slotOf.clear();
    // 1. One entry per mesh, in the order they're first seen; count each one's parts.
    for (const Candidate& c : parts) {
        const uint64_t key = (uint64_t)(uintptr_t)c.mesh;
        if (const uint32_t* slot = s.slotOf.find(key)) { ++out.meshes[*slot].capacity; continue; }
        MeshEntry m;
        const std::vector<float>& px = c.mesh->lodPixels();
        m.levels = c.mesh->lodCount() == 0 ? 1u : 1u + (uint32_t)std::min<size_t>(px.size(), kMaxLods);
        for (uint32_t l = 0; l + 1 < m.levels; ++l) m.lodPixels[l] = px[l];
        m.capacity = 1;
        s.slotOf.set(key, (uint32_t)out.meshes.size());
        out.meshes.push_back(m);
        out.meshPtrs.push_back(c.mesh);
    }
    // 2. Where each mesh's parts, records and copy regions start.
    s.fill.resize(out.meshes.size());
    uint32_t start = 0, base = 0;
    for (size_t m = 0; m < out.meshes.size(); ++m) {
        MeshEntry& e = out.meshes[m];
        s.fill[m] = start;
        start += e.capacity;
        e.firstCommand = (uint32_t)out.commands.size();
        e.base = base;
        for (uint32_t l = 0; l < e.levels; ++l) {
            Command cmd;
            cmd.count = (uint32_t)out.meshPtrs[m]->lodIndexCount((int)l);
            cmd.baseInstance = base + l * e.capacity;
            out.commands.push_back(cmd);
        }
        base += e.capacity * e.levels;
    }
    out.outputSize = base;
    // 3. The parts, grouped by mesh (in the order they came, within a mesh).
    out.instances.resize(parts.size());
    for (const Candidate& c : parts) {
        const uint32_t slot = *s.slotOf.find((uint64_t)(uintptr_t)c.mesh);
        Instance& i = out.instances[s.fill[slot]++];
        i = c.inst;
        i.mesh = slot;
    }
}

bool visible(const Instance& i, const View& v) {
    const glm::vec3 c(i.sphere);
    const float r = i.sphere.w;
    if (v.viewDist > 0.0f && glm::length(c - v.viewPos) - r > v.viewDist) return false;   // past the render distance
    if (v.frustumOn)
        for (const glm::vec4& p : v.planes)
            if (glm::dot(glm::vec3(p), c) + p.w < -(r * 1.5f + 1.0f)) return false;   // off screen
    return true;
}

int pickLevel(const Instance& i, const MeshEntry& m, const View& v) {
    if (v.pixelsPerStud <= 0.0f || v.bias <= 0.0f) return 0;
    const float d = v.ortho ? 1.0f : glm::length(glm::vec3(i.sphere) - v.viewPos);
    const float px = 2.0f * i.sphere.w * v.pixelsPerStud / std::max(d, 1e-3f);
    int level = 0;
    for (uint32_t k = 0; k + 1 < m.levels; ++k)
        if (px < m.lodPixels[k] * v.bias) level = (int)k + 1;
    return level;
}

void changed(const std::vector<Instance>& before, const std::vector<Instance>& now, size_t& first, size_t& last) {
    const size_t common = std::min(before.size(), now.size());
    first = 0;
    while (first < common && before[first] == now[first]) ++first;
    if (before.size() != now.size()) { last = std::max(first, now.size()); return; }
    if (first == common) { last = first; return; }
    last = common;
    while (last > first && before[last - 1] == now[last - 1]) --last;
}

// One thread per part. Keep the maths the same as visible() and pickLevel() above.
const char* const kShader = R"(
layout(local_size_x = 64) in;
struct Draw { mat4 model; vec4 color; vec4 info; vec4 extra; };
struct Inst { Draw d; vec4 sphere; uvec4 meta; };
struct MeshE { uvec4 a; vec4 lodPx; };   // a: first record, levels, first region, parts
layout(std430, binding = 0) readonly buffer Ins { Inst ins[]; };
layout(std430, binding = 1) readonly buffer Meshes { MeshE meshes[]; };
layout(std430, binding = 2) buffer Cmds { uint cmds[]; };   // 5 numbers per indirect draw
layout(std430, binding = 3) writeonly buffer Outs { Draw outs[]; };
uniform vec4  uPlanes[5];
uniform bool  uFrustum;
uniform vec3  uViewPos;
uniform float uViewDist;
uniform float uPixelsPerStud;
uniform float uBias;
uniform bool  uOrtho;
uniform uint  uCount;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uCount) return;
    vec4 s = ins[i].sphere;
    vec3 c = s.xyz;
    float r = s.w;
    float dist = length(c - uViewPos);
    if (uViewDist > 0.0 && dist - r > uViewDist) return;   // past the render distance
    if (uFrustum)
        for (int k = 0; k < 5; ++k)
            if (dot(uPlanes[k].xyz, c) + uPlanes[k].w < -(r * 1.5 + 1.0)) return;   // off screen
    MeshE m = meshes[ins[i].meta.x];
    uint level = 0u;
    if (uPixelsPerStud > 0.0 && uBias > 0.0) {
        float d = uOrtho ? 1.0 : dist;
        float px = 2.0 * r * uPixelsPerStud / max(d, 1e-3);
        for (uint k = 0u; k + 1u < m.a.y; ++k)
            if (px < m.lodPx[k] * uBias) level = k + 1u;
    }
    uint at = atomicAdd(cmds[(m.a.x + level) * 5u + 1u], 1u);
    outs[m.a.z + level * m.a.w + at] = ins[i].d;
}
)";

} // namespace GpuCulling

using namespace GpuCulling;

#if defined(GB_IOS) || defined(GB_GLES) || defined(__EMSCRIPTEN__)

// Phones and the web Player: the old way (their OpenGL ES can't start indirect draws
// part way through a list).
GpuCuller::~GpuCuller() = default;
bool GpuCuller::ready() { m_why = "this device draws the old way"; return false; }
void GpuCuller::cull(const Plan&, const View&) {}
int GpuCuller::draw(const Plan&) { return 0; }
void GpuCuller::release() {}

#else

namespace {
// Makes `buf` hold at least `n` entries of `size` bytes (growing by half again).
void room(unsigned& buf, size_t& have, size_t n, size_t size, GLenum usage) {
    if (buf && n <= have) return;
    if (!buf) glGenBuffers(1, &buf);
    have = std::max<size_t>({n + n / 2, 256, have});
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)(have * size), nullptr, usage);
}
} // namespace

GpuCuller::~GpuCuller() { release(); }

void GpuCuller::release() {
    if (m_program) glDeleteProgram(m_program);
    unsigned bufs[] = {m_in, m_meshes, m_commands, m_out};
    for (unsigned b : bufs) if (b) glDeleteBuffers(1, &b);
    m_program = m_in = m_meshes = m_commands = m_out = 0;
    m_inRoom = m_meshRoom = m_cmdRoom = m_outRoom = 0;
    if (m_fence) { glDeleteSync((GLsync)m_fence); m_fence = nullptr; }
    m_sent.clear();
    m_ok = false;
}

bool GpuCuller::ready() {
    if (m_tried) return m_ok;
    m_tried = true;
    if (Mesh::headless) { m_why = "no graphics"; return false; }
    if (std::getenv("GB_NO_GPU_CULLING")) { m_why = "turned off (GB_NO_GPU_CULLING)"; return false; }
    const GpuCaps& caps = GpuCapsQuery::get();
    if (caps.es || !DeviceFeatures::can(caps, DeviceFeatures::GpuCulling) || !glDrawElementsIndirect || !glBindBufferBase) {
        m_why = "this graphics card can't (it needs OpenGL 4.3)";
        return false;
    }
    const std::string src = std::string("#version 430 core\n") + kShader;
    const GLuint s = glCreateShader(GL_COMPUTE_SHADER);
    const char* text = src.c_str();
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        static char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[GpuCulling] shader: %s\n", log);
        glDeleteShader(s);
        m_why = "its shader didn't compile";
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, s);
    glLinkProgram(m_program);
    glDeleteShader(s);
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(m_program);
        m_program = 0;
        m_why = "its shader didn't link";
        return false;
    }
    m_locPlanes = glGetUniformLocation(m_program, "uPlanes");
    m_locFrustum = glGetUniformLocation(m_program, "uFrustum");
    m_locViewPos = glGetUniformLocation(m_program, "uViewPos");
    m_locViewDist = glGetUniformLocation(m_program, "uViewDist");
    m_locPps = glGetUniformLocation(m_program, "uPixelsPerStud");
    m_locBias = glGetUniformLocation(m_program, "uBias");
    m_locOrtho = glGetUniformLocation(m_program, "uOrtho");
    m_locCount = glGetUniformLocation(m_program, "uCount");
    m_why = "on";
    m_ok = true;
    return true;
}

void GpuCuller::cull(const Plan& plan, const View& view) {
    if (!m_ok) return;
    // Last time's counts, if the graphics card has finished with them (never waits).
    if (m_fence && !m_read.empty()) {
        const GLenum st = glClientWaitSync((GLsync)m_fence, 0, 0);
        if (st == GL_ALREADY_SIGNALED || st == GL_CONDITION_SATISFIED) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_commands);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(m_read.size() * sizeof(Command)), m_read.data());
            long copies = 0, tris = 0;
            for (const Command& c : m_read) { copies += c.instanceCount; tris += (long)c.instanceCount * (c.count / 3); }
            m_lastCopies = copies;
            m_lastTriangles = tris;
            glDeleteSync((GLsync)m_fence);
            m_fence = nullptr;
        }
    }
    const size_t n = plan.instances.size();
    // The parts' list: only what changed is sent (a scene where nothing moves sends nothing).
    const bool grew = !m_in || n > m_inRoom;
    room(m_in, m_inRoom, n, sizeof(Instance), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_in);
    size_t first = 0, last = n;
    if (!grew) changed(m_sent, plan.instances, first, last);
    if (last > first)
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, (GLintptr)(first * sizeof(Instance)),
                        (GLsizeiptr)((last - first) * sizeof(Instance)), plan.instances.data() + first);
    m_lastUploaded = last - first;
    m_sent.assign(plan.instances.begin(), plan.instances.end());
    // The meshes and the records (small): every time, with every count back at 0.
    room(m_meshes, m_meshRoom, plan.meshes.size(), sizeof(MeshEntry), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_meshes);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(plan.meshes.size() * sizeof(MeshEntry)), plan.meshes.data());
    room(m_commands, m_cmdRoom, plan.commands.size(), sizeof(Command), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_commands);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)(plan.commands.size() * sizeof(Command)), plan.commands.data());
    room(m_out, m_outRoom, plan.outputSize, sizeof(InstanceData), GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    m_read.resize(plan.commands.size());

    glUseProgram(m_program);
    glUniform4fv(m_locPlanes, 5, &view.planes[0].x);
    glUniform1i(m_locFrustum, view.frustumOn ? 1 : 0);
    glUniform3fv(m_locViewPos, 1, &view.viewPos.x);
    glUniform1f(m_locViewDist, view.viewDist);
    glUniform1f(m_locPps, view.pixelsPerStud);
    glUniform1f(m_locBias, view.bias);
    glUniform1i(m_locOrtho, view.ortho ? 1 : 0);
    glUniform1ui(m_locCount, (GLuint)n);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_in);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_meshes);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_commands);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_out);
    if (n) glDispatchCompute((GLuint)((n + 63) / 64), 1, 1);
    // (The draws read the records and the copies the shader just wrote.)
    glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
    for (GLuint b = 0; b < 4; ++b) glBindBufferBase(GL_SHADER_STORAGE_BUFFER, b, 0);
    glUseProgram(0);
}

int GpuCuller::draw(const Plan& plan) {
    if (!m_ok || plan.commands.empty()) return 0;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_commands);
    int draws = 0;
    for (size_t m = 0; m < plan.meshes.size(); ++m) {
        const MeshEntry& e = plan.meshes[m];
        for (uint32_t l = 0; l < e.levels; ++l) {
            Mesh::lod = (int)l;
            plan.meshPtrs[m]->drawIndirect(m_out, (size_t)(e.firstCommand + l) * sizeof(Command));
            ++draws;
        }
    }
    Mesh::lod = 0;
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
    if (m_fence) glDeleteSync((GLsync)m_fence);
    m_fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    return draws;
}

#endif
