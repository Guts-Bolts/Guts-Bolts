// The ocean's waves on the graphics card (see OceanGpu.h).
#include "OceanGpu.h"
#include "GL.h"
#include "GpuCaps.h"
#include "../scene/OceanWaves.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

// `fixedSize`: storage that can never change size (compute shaders need it; it's newer
// than what Macs have, so the processor's pictures are made the older way).
void makeTexture(unsigned& tex, int size, GLenum format, bool mips, bool fixedSize) {
    if (tex) glDeleteTextures(1, &tex);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    int levels = 1;
    if (mips) for (int s = size; s > 1; s >>= 1) ++levels;
    if (fixedSize) {
        glTexStorage2D(GL_TEXTURE_2D, levels, format, size, size);
    } else {
        for (int l = 0; l < levels; ++l)
            glTexImage2D(GL_TEXTURE_2D, l, format, std::max(1, size >> l), std::max(1, size >> l), 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mips ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// Uploads a grid and its smaller copies (each one the average of 2 x 2 points of the
// one before). Done here and not by the graphics card: phones and browsers can't always
// shrink this kind of picture themselves.
void uploadWithCopies(unsigned tex, const glm::vec4* grid, int size, std::vector<glm::vec4>& a, std::vector<glm::vec4>& b) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, size, GL_RGBA, GL_FLOAT, grid);
    const glm::vec4* from = grid;
    glm::vec4* to = a.data();
    int level = 1;
    for (int n = size / 2; n >= 1; n /= 2, ++level) {
        const int w = n * 2;
        for (int z = 0; z < n; ++z)
            for (int x = 0; x < n; ++x)
                to[(size_t)z * n + x] = (from[(size_t)(2 * z) * w + 2 * x] + from[(size_t)(2 * z) * w + 2 * x + 1] +
                                         from[(size_t)(2 * z + 1) * w + 2 * x] + from[(size_t)(2 * z + 1) * w + 2 * x + 1]) * 0.25f;
        glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, n, n, GL_RGBA, GL_FLOAT, to);
        from = to;
        to = to == a.data() ? b.data() : a.data();
    }
}

} // namespace

OceanGpu::OceanGpu() = default;
OceanGpu::~OceanGpu() { release(); }

void OceanGpu::makePictures(int size, int bands, bool fixedSize) {
    for (int b = 0; b < 3; ++b) {
        if (b < bands) {
            makeTexture(m_disp[b], size, GL_RGBA16F, true, fixedSize);
            makeTexture(m_slope[b], size, GL_RGBA16F, true, fixedSize);
        } else {
            if (m_disp[b]) glDeleteTextures(1, &m_disp[b]);
            if (m_slope[b]) glDeleteTextures(1, &m_slope[b]);
            m_disp[b] = m_slope[b] = 0;
        }
    }
    m_size = size;
    m_bands = bands;
    m_version = -1;
    m_time = -1.0f;
}

void OceanGpu::uploadFromProcessor(const OceanWaves::Field& field) {
    if (m_version == field.version()) return;
    m_version = field.version();
    const int N = OceanWaves::kCpuN;
    if (m_small[0].empty()) { m_small[0].resize((size_t)N * N / 4); m_small[1].resize((size_t)N * N / 16); }
    for (int b = 0; b < OceanWaves::kCpuBands; ++b) {
        uploadWithCopies(m_disp[b], field.disp(b), N, m_small[0], m_small[1]);
        uploadWithCopies(m_slope[b], field.slope(b), N, m_small[0], m_small[1]);
    }
}

int OceanGpu::prepare(const OceanWaves::Field& field, bool wantGpu) {
    const bool gpu = wantGpu && !m_gpuFailed && supported();
    if (gpu && !m_gpu) {
        if (initCompute(field)) {
            m_gpu = true;
            makePictures(OceanWaves::kGpuN, OceanWaves::kBands, true);
        } else {
            m_gpuFailed = true;
            std::fprintf(stderr, "Ocean: the graphics card can't work the waves out (%s). Using the processor's.\n", m_error.c_str());
        }
    } else if (!gpu && m_gpu) {
        m_gpu = false;
        m_size = 0;
    }
    if (m_gpu) {
        if (m_time != field.time()) { m_time = field.time(); runCompute(m_time); }
        return OceanWaves::kBands;
    }
    if (m_size != OceanWaves::kCpuN || m_bands != OceanWaves::kCpuBands) makePictures(OceanWaves::kCpuN, OceanWaves::kCpuBands, false);
    uploadFromProcessor(field);
    return OceanWaves::kCpuBands;
}

void OceanGpu::release() {
    for (int b = 0; b < 3; ++b) {
        if (m_disp[b]) glDeleteTextures(1, &m_disp[b]);
        if (m_slope[b]) glDeleteTextures(1, &m_slope[b]);
        if (m_start[b]) glDeleteTextures(1, &m_start[b]);
        m_disp[b] = m_slope[b] = m_start[b] = 0;
    }
    for (unsigned& w : m_work) { if (w) glDeleteTextures(1, &w); w = 0; }
    for (unsigned* p : {&m_progSpectrum, &m_progFft, &m_progFinish}) { if (*p) glDeleteProgram(*p); *p = 0; }
    m_size = m_bands = 0;
    m_gpu = false;
}

#if defined(GB_IOS) || defined(__EMSCRIPTEN__)

// OpenGL ES 3.0 / WebGL 2: no compute shaders. These draw the processor's waves.
bool OceanGpu::supported(std::string* why) {
    if (why) *why = "this device has no compute shaders";
    return false;
}
bool OceanGpu::initCompute(const OceanWaves::Field&) { m_error = "no compute shaders"; return false; }
void OceanGpu::runCompute(float) {}

#else

namespace {

#ifdef GB_GLES
const char* kHead = "#version 310 es\nprecision highp float;\nprecision highp int;\nprecision highp image2D;\n";
#else
const char* kHead = "#version 430 core\n";
#endif

// Each wave now: its start turned by its speed, and from it everything the two
// pictures need, as 8 grids of wave sizes packed two to a complex number.
const char* kSpectrum = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba32f, binding = 0) uniform readonly image2D uStart;   // a (re, im), b (re, im)
layout(rgba32f, binding = 1) uniform writeonly image2D uA;
layout(rgba32f, binding = 2) uniform writeonly image2D uB;
uniform float uTime;
uniform float uDk;       // 2 pi / the band's square
uniform int   uN;
// Two real grids as one complex one: a + i b.
vec2 pack(vec2 a, vec2 b) { return vec2(a.x - b.y, a.y + b.x); }
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    vec2 n = vec2(p.x < uN / 2 ? p.x : p.x - uN, p.y < uN / 2 ? p.y : p.y - uN);
    vec2 k = n * uDk;
    float kl = length(k);
    vec4 s = imageLoad(uStart, p);
    if (kl < 1e-6) { imageStore(uA, p, vec4(0.0)); imageStore(uB, p, vec4(0.0)); return; }
    float w = sqrt(9.8 * kl) * uTime;
    float c = cos(w), sn = sin(w);
    vec2 h = vec2(s.x * c - s.y * sn + s.z * c + s.w * sn, s.x * sn + s.y * c - s.z * sn + s.w * c);
    vec2 d = k / kl;
    vec2 mi = vec2(h.y, -h.x);     // -i h
    vec2 pi = vec2(-h.y, h.x);     //  i h
    vec2 Dx = d.x * mi, Dz = d.y * mi;              // the sideways pull
    vec2 hx = k.x * pi, hz = k.y * pi;              // how steep
    vec2 Jxx = (k.x * d.x) * h, Jzz = (k.y * d.y) * h, Jxz = (k.x * d.y) * h;   // how squeezed
    imageStore(uA, p, vec4(pack(Dx, Dz), pack(h, Jxz)));
    imageStore(uB, p, vec4(pack(hx, hz), pack(Jxx, Jzz)));
}
)";

// The FFT of one line (a row, or a column) per work group: its 256 points each belong
// to one invocation, which all step through the 8 joining stages together, sharing two
// work arrays and waiting for each other between stages.
const char* kFft = R"(
layout(local_size_x = 256) in;
layout(rgba32f, binding = 0) uniform readonly image2D uSrc;
layout(rgba32f, binding = 1) uniform writeonly image2D uDst;
uniform int uVertical;
shared vec4 sa[256];
shared vec4 sb[256];
// Two complex numbers (xy, zw), both times w.
vec4 turn(vec2 w, vec4 v) { return vec4(w.x * v.x - w.y * v.y, w.x * v.y + w.y * v.x, w.x * v.z - w.y * v.w, w.x * v.w + w.y * v.z); }
void main() {
    uint i = gl_LocalInvocationID.x;
    int line = int(gl_WorkGroupID.y);
    int r = int(bitfieldReverse(i) >> 24u);
    sa[i] = imageLoad(uSrc, uVertical != 0 ? ivec2(line, r) : ivec2(r, line));
    memoryBarrierShared();
    barrier();
    for (int s = 0; s < 8; ++s) {
        uint half_ = 1u << uint(s), len = half_ << 1u;
        uint j = i & (len - 1u);
        bool lo = j < half_;
        uint jj = lo ? j : j - half_;
        float ang = 6.2831853 * float(jj) / float(len);
        uint iu = lo ? i : i - half_, iv = iu + half_;
        vec4 u, v;
        if ((s & 1) == 0) { u = sa[iu]; v = sa[iv]; } else { u = sb[iu]; v = sb[iv]; }
        v = turn(vec2(cos(ang), sin(ang)), v);
        vec4 res = lo ? u + v : u - v;
        if ((s & 1) == 0) sb[i] = res; else sa[i] = res;
        memoryBarrierShared();
        barrier();
    }
    imageStore(uDst, uVertical != 0 ? ivec2(line, int(i)) : ivec2(int(i), line), sa[i]);
}
)";

// Sorts the FFT's results into the two pictures the water shader reads.
const char* kFinish = R"(
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba32f, binding = 0) uniform readonly image2D uA;       // Dx, Dz, height, Jxz
layout(rgba32f, binding = 1) uniform readonly image2D uB;       // slope x, slope z, Jxx, Jzz
layout(rgba16f, binding = 2) uniform writeonly image2D uDisp;
layout(rgba16f, binding = 3) uniform writeonly image2D uSlope;
void main() {
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    vec4 a = imageLoad(uA, p);
    imageStore(uDisp, p, vec4(a.x, a.z, a.y, a.w));
    imageStore(uSlope, p, imageLoad(uB, p));
}
)";

unsigned compile(const char* body, const char* name, std::string& error) {
    const std::string src = std::string(kHead) + body;
    const GLuint s = glCreateShader(GL_COMPUTE_SHADER);
    const char* text = src.c_str();
    glShaderSource(s, 1, &text, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    char log[2048] = "";
    if (!ok) {
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        error = std::string(name) + ": " + log;
        glDeleteShader(s);
        return 0;
    }
    const GLuint p = glCreateProgram();
    glAttachShader(p, s);
    glLinkProgram(p);
    glDeleteShader(s);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        error = std::string(name) + ": " + log;
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

} // namespace

bool OceanGpu::supported(std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (const char* e = std::getenv("GB_OCEAN_CPU"); e && *e && *e != '0') return no("GB_OCEAN_CPU is set");
    if (!GpuCapsQuery::can(DeviceFeatures::FftOcean)) return no("compute shaders are missing");
    return true;
}

bool OceanGpu::initCompute(const OceanWaves::Field& field) {
    // (One work group does a whole line, and they share two arrays: check the card allows it.)
    GLint groupX = 0, shared = 0;
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &groupX);
    glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &shared);
    if (groupX < OceanWaves::kGpuN || shared < OceanWaves::kGpuN * 32) { m_error = "its work groups are too small"; return false; }
    if (!m_progSpectrum) m_progSpectrum = compile(kSpectrum, "ocean spectrum", m_error);
    if (!m_progFft) m_progFft = compile(kFft, "ocean FFT", m_error);
    if (!m_progFinish) m_progFinish = compile(kFinish, "ocean finish", m_error);
    if (!m_progSpectrum || !m_progFft || !m_progFinish) return false;

    // Every wave's start, from the same made-up numbers the processor uses (so the big
    // waves are the very same ones), for the bigger grid. Done once.
    const int N = OceanWaves::kGpuN;
    std::vector<float> start((size_t)N * N * 4);
    for (int b = 0; b < OceanWaves::kBands; ++b) {
        for (int iz = 0; iz < N; ++iz)
            for (int ix = 0; ix < N; ++ix) {
                const int nx = ix < N / 2 ? ix : ix - N, nz = iz < N / 2 ? iz : iz - N;
                float* o = &start[((size_t)iz * N + ix) * 4];
                o[0] = o[1] = o[2] = o[3] = 0.0f;
                if (nx == -N / 2 || nz == -N / 2) continue;
                const glm::vec2 a = OceanWaves::startWave(b, nx, nz) * field.scale();
                const glm::vec2 c = OceanWaves::startWave(b, -nx, -nz) * field.scale();
                o[0] = a.x; o[1] = a.y; o[2] = c.x; o[3] = -c.y;
            }
        makeTexture(m_start[b], N, GL_RGBA32F, false, true);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, N, N, GL_RGBA, GL_FLOAT, start.data());
    }
    for (unsigned& w : m_work) makeTexture(w, N, GL_RGBA32F, false, true);
    return true;
}

void OceanGpu::runCompute(float time) {
    const int N = OceanWaves::kGpuN;
    const GLbitfield wait = GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT;
    for (int b = 0; b < OceanWaves::kBands; ++b) {
        // 1. The waves at this moment.
        glUseProgram(m_progSpectrum);
        glUniform1f(glGetUniformLocation(m_progSpectrum, "uTime"), time);
        glUniform1f(glGetUniformLocation(m_progSpectrum, "uDk"), 6.2831853f / OceanWaves::kBand[b].length);
        glUniform1i(glGetUniformLocation(m_progSpectrum, "uN"), N);
        glBindImageTexture(0, m_start[b], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_work[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glBindImageTexture(2, m_work[2], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
        glDispatchCompute(N / 8, N / 8, 1);
        glMemoryBarrier(wait);
        // 2. Add them up: every row (into the spare), then every column (back again).
        glUseProgram(m_progFft);
        const GLint vertical = glGetUniformLocation(m_progFft, "uVertical");
        for (int w = 0; w < 4; w += 2) {
            glUniform1i(vertical, 0);
            glBindImageTexture(0, m_work[w], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(1, m_work[w + 1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glDispatchCompute(1, N, 1);
            glMemoryBarrier(wait);
            glUniform1i(vertical, 1);
            glBindImageTexture(0, m_work[w + 1], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
            glBindImageTexture(1, m_work[w], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glDispatchCompute(1, N, 1);
            glMemoryBarrier(wait);
        }
        // 3. Into the two pictures, with smaller copies for far-away water.
        glUseProgram(m_progFinish);
        glBindImageTexture(0, m_work[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(1, m_work[2], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
        glBindImageTexture(2, m_disp[b], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glBindImageTexture(3, m_slope[b], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        glDispatchCompute(N / 8, N / 8, 1);
        glMemoryBarrier(wait);
        glBindTexture(GL_TEXTURE_2D, m_disp[b]);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, m_slope[b]);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glUseProgram(0);
}

#endif
