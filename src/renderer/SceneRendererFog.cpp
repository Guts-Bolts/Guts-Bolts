// Volumetric fog for SceneRenderer: fog that takes light (sun shafts through gaps, lamps
// glowing in mist) and drifts in wisps. How it works is in FogVolume.h; the shaders are
// in FogShaders.h. High quality and up (GraphicsSettings::volumetricFog); everything
// else keeps the simple fog in the lit shader.
#include "SceneRenderer.h"
#include "Shader.h"
#include "Shaders.h"
#include "FogShaders.h"
#include "FogVolume.h"
#include "Camera.h"
#include "Mesh.h"
#include "GL.h"
#include "../scene/Scene.h"
#include "../scene/Environment.h"
#include "../core/Settings.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

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

void use(int unit, unsigned tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

double seconds() {
    using namespace std::chrono;
    static const steady_clock::time_point start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

glm::vec3 wrapped(const glm::vec3& v) { return v - glm::floor(v); }

} // namespace

struct SceneRenderer::FogVol {
    Shader trace{Shaders::fullscreenVert, Shaders::fogTraceFrag};
    Shader apply{Shaders::fullscreenVert, Shaders::fogApplyFrag};
    // The fog so far, twice (read one, write the other): light + how much shows
    // through, and how far away each pixel was.
    unsigned fbo[2] = {}, fog[2] = {}, dist[2] = {};
    int  w = 0, h = 0, cur = 0, frame = 0;
    bool histOk = false;
    glm::mat4 prevVP{1.0f};
    glm::vec3 prevPos{0.0f};
    unsigned noise = 0;
    // The lit picture's colour without its depth, so the fog can be drawn onto it
    // while reading that depth.
    unsigned colorFbo = 0, colorOf = 0;
    // The wisps drift with the wind.
    glm::vec3 driftBig{0.0f}, driftSmall{0.0f};
    double driftAt = -1.0;

    void freeTargets() {
        for (int i = 0; i < 2; ++i) {
            if (fbo[i]) glDeleteFramebuffers(1, &fbo[i]);
            if (fog[i]) glDeleteTextures(1, &fog[i]);
            if (dist[i]) glDeleteTextures(1, &dist[i]);
            fbo[i] = fog[i] = dist[i] = 0;
        }
        w = h = 0;
        histOk = false;
    }
    void makeTargets(int nw, int nh) {
        freeTargets();
        w = nw; h = nh;
        const GLenum both[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        for (int i = 0; i < 2; ++i) {
            fog[i] = makeTex(w, h, true);
            dist[i] = makeTex(w, h, false);
            glGenFramebuffers(1, &fbo[i]);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fog[i], 0);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, dist[i], 0);
            glDrawBuffers(2, both);
        }
    }
    void makeNoise() {
        const std::vector<uint8_t> data = FogVolume::noise();
        const int n = FogVolume::kNoiseSize;
        glGenTextures(1, &noise);
        glBindTexture(GL_TEXTURE_3D, noise);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, n, n, n, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
        glBindTexture(GL_TEXTURE_3D, 0);
    }
    void colorOnly(unsigned color) {
        if (colorFbo && colorOf == color) return;
        if (!colorFbo) glGenFramebuffers(1, &colorFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, colorFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        colorOf = color;
    }
};

void SceneRenderer::destroyFog() {
    if (!m_fog) return;
    m_fog->freeTargets();
    if (m_fog->noise) glDeleteTextures(1, &m_fog->noise);
    if (m_fog->colorFbo) glDeleteFramebuffers(1, &m_fog->colorFbo);
    delete m_fog;
    m_fog = nullptr;
}

bool SceneRenderer::wantFogVolume(Scene& scene, const Camera& camera) const {
#ifdef GB_GLES
    (void)scene; (void)camera;
    return false;   // (phones and the web Player keep the simple fog until it's been tried on them)
#else
    const Environment& env = scene.environment();
    return GraphicsSettings::get().volumetricFog() >= 1 && temporal && shading == Shading::Material && !xray &&
           !camera.orthographic && !camera.useProjection && !Mesh::headless && env.fogEnabled && env.fogDensity > 0.0f;
#endif
}

// Called once the solid things are drawn (and before anything see-through, which keeps
// the simple fog: a puff of smoke close by mustn't get the fog of the hill far behind it).
void SceneRenderer::drawFogVolume(Scene& scene, const Camera& camera) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    const Environment& env = scene.environment();
    if (!m_fog) m_fog = new FogVol();
    FogVol& f = *m_fog;
    const int level = std::clamp(gs.volumetricFog(), 1, 3);
    // Half the width and height (a quarter of the pixels); Cinematic works at full size.
    const int div = level >= 3 ? 1 : 2;
    const int w = std::max(1, m_hdr.w / div), h = std::max(1, m_hdr.h / div);
    if (f.w != w || f.h != h) f.makeTargets(w, h);
    if (!f.noise) f.makeNoise();
    if (glm::length(camera.position() - f.prevPos) > 8.0f) f.histOk = false;   // (a teleport: start again)

    // The wisps drift with the wind (and a little on their own when the air is still).
    const double t = seconds();
    const float dt = f.driftAt < 0.0 ? 0.0f : (float)std::clamp(t - f.driftAt, 0.0, 0.25);
    f.driftAt = t;
    const glm::vec3 wind = glm::clamp(scene.world().globalWind, glm::vec3(-40.0f), glm::vec3(40.0f)) * 0.5f +
                           glm::vec3(0.7f, 0.05f, 0.3f);
    f.driftBig = wrapped(f.driftBig - wind * dt / 48.0f);
    f.driftSmall = wrapped(f.driftSmall - wind * 1.6f * dt / 13.0f);

    const glm::mat4 vp = camera.projection() * camera.view();
    const glm::mat4 invVP = glm::inverse(vp);

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glBindVertexArray(m_emptyVao);

    // 1. Walk the rays, on the small picture.
    const int prev = f.cur, next = 1 - f.cur;
    glBindFramebuffer(GL_FRAMEBUFFER, f.fbo[next]);
    glViewport(0, 0, w, h);
    Shader& s = f.trace;
    s.bind();
    use(1, m_hdr.depth);
    m_shadow.bindForRead(2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, f.noise);
    use(4, f.fog[prev]);
    use(8, f.dist[prev]);
    s.setInt("uDepth", 1);
    s.setInt("uShadowMap", 2);
    s.setInt("uNoise", 3);
    s.setInt("uHist", 4);
    s.setInt("uHistDist", 8);
    s.setMat4("uInvViewProj", invVP);
    s.setMat4("uPrevViewProj", f.prevVP);
    s.setMat4("uLightSpace", m_lightSpace);
    s.setVec3("uViewPos", camera.position());
    s.setBool("uHistOk", f.histOk);
    s.setBool("uShadows", m_shadowsOn);
    s.setFloat("uShadowStrength", env.shadowStrength);
    s.setVec3("uFogColor", env.fogColor);
    s.setFloat("uFogDensity", env.fogDensity);
    s.setFloat("uFogHeight", env.fogHeight);
    s.setFloat("uFogFalloff", std::max(0.0f, env.fogFalloff));
    s.setFloat("uFogSunGlow", env.fogSunGlow);
    s.setVec3("uSunDir", env.sunDirection());
    s.setVec3("uSunColor", env.sunColor);
    s.setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
    s.setInt("uSteps", FogVolume::stepsFor(level));
    s.setFloat("uMaxDist", FogVolume::kMaxDistance);
    s.setFloat("uFrame", (float)(f.frame++ % 64));
    s.setFloat("uKeep", level >= 3 ? 0.93f : 0.88f);
    s.setFloat("uWisps", std::clamp(env.fogWisps, 0.0f, 1.0f));
    s.setVec3("uDriftBig", f.driftBig);
    s.setVec3("uDriftSmall", f.driftSmall);
    s.setInt("uLightCount", m_fogLightCount);
    if (m_fogLightCount > 0) {
        s.setVec4Array("uLightPosRange", m_fogLightPos, m_fogLightCount);
        s.setVec4Array("uLightColor", m_fogLightColor, m_fogLightCount);
        s.setVec4Array("uLightDir", m_fogLightDir, m_fogLightCount);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_3D, 0);

    // 2. Put it onto the lit picture: picture x (how much shows through) + the fog's light.
    f.colorOnly(m_hdr.color);
    glBindFramebuffer(GL_FRAMEBUFFER, f.colorFbo);
    glViewport(0, 0, m_hdr.w, m_hdr.h);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
    Shader& a = f.apply;
    a.bind();
    use(1, f.fog[next]);
    use(2, f.dist[next]);
    use(4, m_hdr.depth);
    a.setInt("uFog", 1);
    a.setInt("uFogDist", 2);
    a.setInt("uDepth", 4);
    a.setMat4("uInvViewProj", invVP);
    a.setVec3("uViewPos", camera.position());
    glDrawArrays(GL_TRIANGLES, 0, 3);

    f.cur = next;
    f.prevVP = vp;
    f.prevPos = camera.position();
    f.histOk = true;
    m_usedFogVol = true;

    // Back to how the part drawing left things.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdr.fbo);
    glViewport(0, 0, m_hdr.w, m_hdr.h);
}
