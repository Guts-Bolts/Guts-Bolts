// The temporal upscaler (like Unreal's TSR) and the bounce light traced across the
// screen (like Lumen's screen traces), for SceneRenderer. The shaders and what they do
// are in TemporalShaders.h; the levels come from the graphics settings (Scalability.h).
#include "SceneRenderer.h"
#include "Shader.h"
#include "Shaders.h"
#include "TemporalShaders.h"
#include "Camera.h"
#include "GL.h"
#include "../core/Settings.h"
#include "../core/Scalability.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>

namespace {

// Both need float colour targets to keep the light (computers always have them; on
// phones it's an extension, and without it they simply stay off).
bool floatTargets() {
#ifdef GB_GLES
    static const bool ok = [] {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; ++i) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && (std::strcmp(e, "GL_EXT_color_buffer_float") == 0 || std::strcmp(e, "GL_EXT_color_buffer_half_float") == 0))
                return true;
        }
        return false;
    }();
    return ok;
#else
    return true;
#endif
}

unsigned makeTex(int w, int h, bool smooth) {
    unsigned t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

void freeTex(unsigned& t) { if (t) glDeleteTextures(1, &t); t = 0; }
void freeFbo(unsigned& f) { if (f) glDeleteFramebuffers(1, &f); f = 0; }

void use(int unit, unsigned tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

} // namespace

struct SceneRenderer::Temporal {
    Shader resolve{Shaders::fullscreenVert, Shaders::temporalFrag};
    Shader trace{Shaders::fullscreenVert, Shaders::ssgiFrag};

    // The upscaler: the result so far, twice (read one, write the other).
    unsigned histFbo[2] = {}, histTex[2] = {};
    int  hw = 0, hh = 0, cur = 0, quality = -1;
    bool histOk = false, on = false;
    glm::mat4 prevVP{1.0f}, vp{1.0f};
    glm::vec3 prevPos{0.0f};
    glm::vec2 jitterPx{0.0f};
    int frame = 0;

    // The bounce light: light + sky (gi) and distance (dist), twice as well.
    unsigned giFbo[2] = {}, giTex[2] = {}, giDist[2] = {};
    int  gw = 0, gh = 0, gcur = 0, giLevel = -1;
    bool giOk = false, giReady = false;
    glm::mat4 giView{1.0f}, giVP{1.0f};
    int giFrame = 0;

    void freeHistory() {
        for (int i = 0; i < 2; ++i) { freeFbo(histFbo[i]); freeTex(histTex[i]); }
        hw = hh = 0;
        histOk = false;
    }
    void freeBounce() {
        for (int i = 0; i < 2; ++i) { freeFbo(giFbo[i]); freeTex(giTex[i]); freeTex(giDist[i]); }
        gw = gh = 0;
        giOk = giReady = false;
    }
    void makeHistory(int w, int h) {
        freeHistory();
        hw = w; hh = h;
        for (int i = 0; i < 2; ++i) {
            histTex[i] = makeTex(w, h, true);
            glGenFramebuffers(1, &histFbo[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, histFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, histTex[i], 0);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void makeBounce(int w, int h) {
        freeBounce();
        gw = w; gh = h;
        const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        for (int i = 0; i < 2; ++i) {
            giTex[i] = makeTex(w, h, true);
            giDist[i] = makeTex(w, h, false);
            glGenFramebuffers(1, &giFbo[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, giFbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, giTex[i], 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, giDist[i], 0);
            glDrawBuffers(2, both);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};

void SceneRenderer::destroyTemporal() {
    if (!m_tmp) return;
    m_tmp->freeHistory();
    m_tmp->freeBounce();
    delete m_tmp;
    m_tmp = nullptr;
}

bool SceneRenderer::beginTemporal(const Camera& camera, int outW, int outH, int inW, int inH) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    m_usedUpscaler = false;
    camera.jitter = glm::vec2(0.0f);
    const bool want = temporal && gs.aaQuality >= 1 && shading == Shading::Material && !xray &&
                      !camera.useProjection && floatTargets();
    if (!want) {
        if (m_tmp) m_tmp->histOk = false;
        return false;
    }
    if (!m_tmp) m_tmp = new Temporal();
    Temporal& t = *m_tmp;
    if (t.hw != outW || t.hh != outH) t.makeHistory(outW, outH);
    if (t.quality != gs.aaQuality) { t.quality = gs.aaQuality; t.histOk = false; }
    // A camera cut (a teleport, a new game): start again instead of smearing the old view in.
    if (glm::length(camera.position() - t.prevPos) > 8.0f) t.histOk = false;
    t.vp = camera.projection() * camera.view();   // (without the jitter, for moving the old frames)
    // Upscaling needs more different spots inside each pixel to fill in the detail.
    const int phases = (inW < outW * 0.9f || gs.aaQuality >= 3) ? 16 : 8;
    const int i = t.frame++ % phases;
    t.jitterPx = glm::vec2(Scalability::halton(i, 2) - 0.5f, Scalability::halton(i, 3) - 0.5f);
    camera.jitter = glm::vec2(t.jitterPx.x * 2.0f / (float)std::max(1, inW), t.jitterPx.y * 2.0f / (float)std::max(1, inH));
    t.on = true;
    m_usedUpscaler = true;
    return true;
}

unsigned SceneRenderer::resolveTemporal(const Camera&) {
    if (!m_tmp || !m_tmp->on) return 0;
    Temporal& t = *m_tmp;
    const GraphicsSettings& gs = GraphicsSettings::get();
    const int prev = t.cur, next = 1 - t.cur;
    glBindFramebuffer(GL_FRAMEBUFFER, t.histFbo[next]);
    glViewport(0, 0, t.hw, t.hh);
    t.resolve.bind();
    use(0, m_hdr.color);
    use(1, m_hdr.depth);
    use(2, t.histTex[prev]);
    t.resolve.setInt("uCur", 0);
    t.resolve.setInt("uDepth", 1);
    t.resolve.setInt("uHist", 2);
    t.resolve.setMat4("uInvViewProj", glm::inverse(t.vp));
    t.resolve.setMat4("uPrevViewProj", t.prevVP);
    t.resolve.setVec2("uJitter", t.jitterPx);
    t.resolve.setVec2("uInSize", glm::vec2((float)m_hdr.w, (float)m_hdr.h));
    t.resolve.setVec2("uOutSize", glm::vec2((float)t.hw, (float)t.hh));
    t.resolve.setBool("uHistOk", t.histOk);
    const int q = std::clamp(gs.aaQuality, 1, 3);
    t.resolve.setFloat("uBlend", q == 1 ? 0.1f : q == 2 ? 0.08f : 0.06f);
    t.resolve.setInt("uQuality", q);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    t.cur = next;
    return t.histTex[next];
}

float SceneRenderer::upscaleSharpen() const {
    if (!m_tmp || !m_tmp->on) return 0.0f;
    const int q = GraphicsSettings::get().aaQuality;
    if (q < 2) return 0.0f;
    // More sharpening the more was filled in.
    const float drawn = m_tmp->hw > 0 ? std::min(1.0f, (float)m_hdr.w / (float)m_tmp->hw) : 1.0f;
    return (q >= 3 ? 0.2f : 0.3f) + 0.6f * (1.0f - drawn);
}

void SceneRenderer::endTemporal(const Camera& camera) {
    camera.jitter = glm::vec2(0.0f);
    if (!m_tmp || !m_tmp->on) return;
    m_tmp->prevVP = m_tmp->vp;
    m_tmp->prevPos = camera.position();
    m_tmp->histOk = true;
    m_tmp->on = false;
}

void SceneRenderer::bindBounce(const Shader& lit) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    const bool on = m_tmp && m_tmp->giReady && temporal && gs.giQuality >= 1 && shading == Shading::Material;
    lit.setBool("uBounceOn", on);
    lit.setInt("uBounce", 11);
    lit.setInt("uBounceDist", 12);
    if (on) {
        use(11, m_tmp->giTex[m_tmp->gcur]);
        use(12, m_tmp->giDist[m_tmp->gcur]);
        lit.setMat4("uBounceView", m_tmp->giView);
        lit.setMat4("uBounceViewProj", m_tmp->giVP);
        glActiveTexture(GL_TEXTURE0);
    }
}

void SceneRenderer::traceBounce(const Camera& camera) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    m_usedBounce = false;
    const int level = std::min(gs.giQuality, 3);
    const bool want = temporal && level >= 1 && shading == Shading::Material && !xray && !camera.orthographic &&
                      !camera.useProjection && floatTargets();
    if (!want) {
        if (m_tmp) { m_tmp->giReady = false; m_tmp->giOk = false; }
        return;
    }
    if (!m_tmp) m_tmp = new Temporal();
    Temporal& t = *m_tmp;
    // Medium works at a quarter of the size; High and Epic at half, with more rays.
    const int div = level == 1 ? 4 : 2;
    const int w = std::max(1, m_hdr.w / div), h = std::max(1, m_hdr.h / div);
    if (t.gw != w || t.gh != h) t.makeBounce(w, h);
    if (t.giLevel != level) { t.giLevel = level; t.giOk = false; }
    static const int   kRays[4]  = {0, 4, 6, 10};
    static const int   kSteps[4] = {0, 8, 12, 16};
    static const float kKeep[4]  = {0.0f, 0.8f, 0.85f, 0.9f};

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_emptyVao);
    const int prev = t.gcur, next = 1 - t.gcur;
    glBindFramebuffer(GL_FRAMEBUFFER, t.giFbo[next]);
    glViewport(0, 0, w, h);
    const glm::mat4 proj = camera.projection(), view = camera.view();
    Shader& s = t.trace;
    s.bind();
    use(0, m_hdr.depth);
    use(1, m_hdr.color);
    use(2, t.giTex[prev]);
    use(3, t.giDist[prev]);
    s.setInt("uDepth", 0);
    s.setInt("uColor", 1);
    s.setInt("uHist", 2);
    s.setInt("uHistDepth", 3);
    s.setMat4("uProj", proj);
    s.setMat4("uInvProj", glm::inverse(proj));
    s.setMat4("uInvView", glm::inverse(view));
    s.setMat4("uPrevView", t.giView);
    s.setMat4("uPrevViewProj", t.giVP);
    s.setBool("uHistOk", t.giOk);
    s.setVec2("uTexel", glm::vec2(1.0f / (float)m_hdr.w, 1.0f / (float)m_hdr.h));
    s.setInt("uRays", kRays[level]);
    s.setInt("uSteps", kSteps[level]);
    s.setFloat("uRadius", 6.0f);
    s.setFloat("uFrame", (float)(t.giFrame++ % 64));
    s.setFloat("uKeep", kKeep[level]);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    glDepthMask(GL_TRUE);
    t.giView = view;
    t.giVP = proj * view;
    t.gcur = next;
    t.giOk = t.giReady = true;
    m_usedBounce = true;
}
