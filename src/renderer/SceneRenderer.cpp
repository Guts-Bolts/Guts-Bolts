#include "SceneRenderer.h"
#include "MeshLod.h"
#include "../core/Audio.h"
#include "../core/Log.h"
#include "Textures.h"
#include "Shader.h"
#include "Shaders.h"
#include "Mesh.h"
#include "Camera.h"
#include "Framebuffer.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Player.h"
#include "../scene/Physics.h"
#include "../scene/Water.h"
#include "../scene/Liquid.h"
#include "../scene/OceanWaves.h"
#include "OceanGpu.h"
#include "../scene/Skin.h"
#include "../scene/Cloth.h"
#include "../scene/BounceLight.h"
#include "../scene/LipSync.h"
#include "../scene/Realism.h"
#include "../core/Settings.h"
#include "../scene/Storms.h"
#include "FogVolume.h"
#include "../core/FrameProfile.h"
#include "MeshLibrary.h"

#include "GL.h"
#include "Frustum.h"
#include "Instancing.h"
#include <cstddef>
#include <functional>
#include <cstring>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <map>
#include <chrono>
#include <cmath>
#include <vector>

// A MeshPart with Bones: its mesh can bend far out of its box, so it's always drawn.
static bool bentByBones(const SceneNode* n) {
    for (const auto& c : n->children) if (c->isBone()) return true;
    return false;
}

// How far a part reaches from its middle (half its diagonal): for render distance.
static float partRadius(const glm::mat4& m) {
    return 0.5f * std::sqrt(glm::dot(glm::vec3(m[0]), glm::vec3(m[0])) + glm::dot(glm::vec3(m[1]), glm::vec3(m[1])) +
                            glm::dot(glm::vec3(m[2]), glm::vec3(m[2])));
}

int SceneRenderer::lodFor(const Mesh* mesh, const glm::mat4& model) const {
    if (!mesh || mesh->lodCount() == 0 || m_lodPixelsPerStud <= 0.0f) return 0;
    const float distance = m_lodOrtho ? 1.0f : glm::length(glm::vec3(model[3]) - m_viewPos);
    return MeshLod::pick(MeshLod::pixelsAcross(partRadius(model), distance, m_lodPixelsPerStud), m_lodBias, mesh->lodPixels());
}

namespace {

// Grid lines sit a hair above y = 0 so they show on top of the Baseplate.
constexpr float kGridY = 0.01f;
constexpr int   kMaxLights = 32;

double now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Studio's Wireframe view: draw only the edges of triangles (computers only; phones'
// OpenGL ES has no lines mode, and only Studio asks for it).
void wireframe(bool on) {
#if !defined(GB_GLES) && !defined(GB_IOS) && !defined(__EMSCRIPTEN__)
    glPolygonMode(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
#else
    (void)on;
#endif
}

void destroyTarget(SceneRenderer::Target& t) {
    if (t.fbo)   glDeleteFramebuffers(1, &t.fbo);
    if (t.color) glDeleteTextures(1, &t.color);
    if (t.depth) glDeleteTextures(1, &t.depth);
    t = SceneRenderer::Target{};
}

// The HDR buffers need float colour targets. Every computer has them; phones
// usually do (an extension), and the rare ones that don't fall back to 8 bits.
GLenum hdrFormat() {
#ifdef GB_GLES
    static GLenum fmt = [] {
        GLint n = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &n);
        for (GLint i = 0; i < n; ++i) {
            const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i);
            if (e && (std::strcmp(e, "GL_EXT_color_buffer_float") == 0 ||
                      std::strcmp(e, "GL_EXT_color_buffer_half_float") == 0))
                return (GLenum)GL_RGBA16F;
        }
        return (GLenum)GL_RGBA8;
    }();
    return fmt;
#else
    return GL_RGBA16F;
#endif
}

void createTarget(SceneRenderer::Target& t, int w, int h, GLenum fmt, bool withDepth) {
    destroyTarget(t);
    t.w = std::max(1, w);
    t.h = std::max(1, h);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);

    glGenTextures(1, &t.color);
    glBindTexture(GL_TEXTURE_2D, t.color);
    GLenum base = (fmt == GL_R8 || fmt == GL_R32F || fmt == GL_R16F) ? GL_RED : GL_RGBA;
    GLenum type = (fmt == GL_RGBA16F || fmt == GL_R32F || fmt == GL_R16F) ? GL_FLOAT : GL_UNSIGNED_BYTE;   // OpenGL ES is strict about this
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, t.w, t.h, 0, base, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);

    if (withDepth) {
        glGenTextures(1, &t.depth);
        glBindTexture(GL_TEXTURE_2D, t.depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, t.w, t.h, 0,
                     GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void bindTarget(const SceneRenderer::Target& t) {
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glViewport(0, 0, t.w, t.h);
}

void bindTex(int unit, unsigned int tex) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

} // namespace

SceneRenderer::SceneRenderer() {
    using namespace Shaders;
    m_lit       = std::make_unique<Shader>(litVert, litFrag);
    m_grid      = std::make_unique<Shader>(gridVert, gridFrag);
    m_overlay   = std::make_unique<Shader>(overlayVert, overlayFrag);
    m_puff      = std::make_unique<Shader>(puffVert, puffFrag);
    m_sky       = std::make_unique<Shader>(skyVert, skyFrag);
    m_depth     = std::make_unique<Shader>(depthVert, depthFrag);
    m_gtao      = std::make_unique<Shader>(fullscreenVert, gtaoFrag);
    m_gtaoBlur  = std::make_unique<Shader>(fullscreenVert, gtaoBlurFrag);
    m_bloomPre  = std::make_unique<Shader>(fullscreenVert, bloomPrefilterFrag);
    m_bloomDown = std::make_unique<Shader>(fullscreenVert, bloomDownFrag);
    m_bloomUp   = std::make_unique<Shader>(fullscreenVert, bloomUpFrag);
    m_composite = std::make_unique<Shader>(fullscreenVert, compositeFrag);
    m_fxaa      = std::make_unique<Shader>(fullscreenVert, fxaaFrag);
    m_water       = std::make_unique<Shader>(waterVert, waterFrag);
    m_hlMask      = std::make_unique<Shader>(highlightMaskVert, highlightMaskFrag);
    m_hl          = std::make_unique<Shader>(fullscreenVert, highlightFrag);
    m_ribbon      = std::make_unique<Shader>(ribbonVert, ribbonFrag);
    m_sunRays     = std::make_unique<Shader>(fullscreenVert, sunRaysFrag);
    m_lumShader   = std::make_unique<Shader>(fullscreenVert, lumFrag);
    m_motionBlur  = std::make_unique<Shader>(fullscreenVert, motionBlurFrag);
    m_sprite      = std::make_unique<Shader>(spriteVert, spriteFrag);
    m_fluidDepth  = std::make_unique<Shader>(fluidVert, fluidDepthFrag);
    m_fluidThick  = std::make_unique<Shader>(fluidVert, fluidThickFrag);
    m_fluidColor  = std::make_unique<Shader>(fluidVert, fluidColorFrag);
    m_fluidBlur   = std::make_unique<Shader>(fullscreenVert, fluidBlurFrag);
    m_fluidShade  = std::make_unique<Shader>(fullscreenVert, fluidShadeFrag);
    m_fluidSimple = std::make_unique<Shader>(fluidVert, fluidSimpleFrag);
    buildGrid();
    buildAxes();
    Liquid::graphicsContext(+1);   // the liquid's physics may use the graphics card now
    // Fullscreen passes make their own vertices, but core GL still needs a VAO.
    glGenVertexArrays(1, &m_emptyVao);
    m_startTime = now();
}

SceneRenderer::~SceneRenderer() {
    Liquid::graphicsContext(-1);
    destroyTemporal();
    destroyFog();
    m_ocean.reset();
    if (m_gridVbo)  glDeleteBuffers(1, &m_gridVbo);
    if (m_instanceVbo) glDeleteBuffers(1, &m_instanceVbo);
    if (m_overlayVbo) glDeleteBuffers(1, &m_overlayVbo);
    if (m_overlayVao) glDeleteVertexArrays(1, &m_overlayVao);
    if (m_gridVao)  glDeleteVertexArrays(1, &m_gridVao);
    if (m_axisVbo)  glDeleteBuffers(1, &m_axisVbo);
    if (m_axisVao)  glDeleteVertexArrays(1, &m_axisVao);
    if (m_emptyVao) glDeleteVertexArrays(1, &m_emptyVao);
    if (m_fluidVbo) glDeleteBuffers(1, &m_fluidVbo);
    if (m_fluidVao) glDeleteVertexArrays(1, &m_fluidVao);
    destroyTarget(m_fDepth); destroyTarget(m_fTmp); destroyTarget(m_fThick); destroyTarget(m_fColor); destroyTarget(m_sceneCopy); destroyTarget(m_waterCopy); destroyTarget(m_hlTarget); destroyTarget(m_ssrCopy);
    if (m_ribbonVbo) glDeleteBuffers(1, &m_ribbonVbo);
    if (m_ribbonVao) glDeleteVertexArrays(1, &m_ribbonVao);
    destroyTarget(m_hdr);
    destroyTarget(m_ao);
    destroyTarget(m_aoRaw);
    destroyTarget(m_ldr);
    for (auto& b : m_bloom) destroyTarget(b);
    for (auto& b : m_bdDown) destroyTarget(b);
    for (auto& b : m_bdUp) destroyTarget(b);
    for (auto& b : m_blurDown) destroyTarget(b);
    for (auto& b : m_blurUp) destroyTarget(b);
    destroyTarget(m_rays);
    destroyTarget(m_lum);
    destroyTarget(m_blurred);
}

void SceneRenderer::gatherScreenFx(Scene& scene) {
    m_fx = ScreenFx{};
    ScreenFx& f = m_fx;
    auto take = [&](const SceneNode* holder, bool lighting) {
        for (const auto& c : holder->children) {
            if (lighting && c->isSky() && !f.sky) f.sky = c.get();
            if (!c->isPostEffect() || !c->enabled) continue;
            const PostEffectProps& p = c->post;
            switch (p.type) {
                case PostEffectType::ColorCorrection:
                    f.grade = true;
                    f.brightness += p.brightness;
                    f.contrast *= 1.0f + p.contrast;
                    f.saturation *= 1.0f + p.saturation;
                    f.tint *= p.tint;
                    break;
                case PostEffectType::SunRays:
                    f.rays += p.intensity;
                    f.raysSpread = std::max(f.raysSpread, p.spread);
                    break;
                case PostEffectType::DepthOfField:
                    if (!f.dof) { f.focus = p.focusDistance; f.inFocus = p.inFocusRadius; }
                    f.dof = true;
                    f.nearAmt = std::max(f.nearAmt, p.nearIntensity);
                    f.farAmt = std::max(f.farAmt, p.farIntensity);
                    break;
                case PostEffectType::Bloom:
                    f.bloom = true;
                    f.bloomIntensity += p.intensity;
                    f.bloomThreshold = std::min(f.bloomThreshold, p.threshold);
                    f.bloomSize = std::max(f.bloomSize, p.size);
                    break;
                case PostEffectType::Blur:
                    f.blur = std::sqrt(f.blur * f.blur + p.size * p.size);   // two blurs: a bit blurrier
                    break;
            }
        }
    };
    for (const auto& c : scene.root()->children)
        if (c->name == "Lighting" && scene.isServiceFolder(c.get())) take(c.get(), true);
    take(scene.cameraNode(), false);
    // Light rays without a SunRaysEffect (Lighting.LightRays): shafts of sunlight through
    // gaps, stronger in haze and fog.
    if (f.rays <= 0.0f && scene.environment().lightRays > 0.0f) {
        const Environment& e = scene.environment();
        f.rays = e.lightRays * (0.3f + std::clamp(e.fogEnabled ? e.fogDensity * 30.0f : 0.0f, 0.0f, 0.7f));
        f.raysSpread = std::max(f.raysSpread, 0.6f);
    }
    f.rays = std::min(f.rays, 1.0f);
    f.bloomIntensity = std::min(f.bloomIntensity, 2.0f);
    if (f.dof && f.nearAmt <= 0.0f && f.farAmt <= 0.0f) f.dof = false;
}

void SceneRenderer::setOverlay(const std::vector<OverlayVertex>& tris, const std::vector<OverlayVertex>& lines) {
    m_overlayTris = (int)tris.size();
    m_overlayLines = (int)lines.size();
    if (!m_overlayTris && !m_overlayLines) return;
    if (!m_overlayVao) {
        glGenVertexArrays(1, &m_overlayVao);
        glGenBuffers(1, &m_overlayVbo);
        glBindVertexArray(m_overlayVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex), (void*)sizeof(glm::vec3));
    }
    glBindVertexArray(m_overlayVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_overlayVbo);
    std::vector<OverlayVertex> all(tris);
    all.insert(all.end(), lines.begin(), lines.end());
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(all.size() * sizeof(OverlayVertex)), all.data(), GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void SceneRenderer::drawBlasts(Scene& scene, const Camera& camera) {
    const BlastSystem& bs = scene.blasts();
    const glm::mat4 view = camera.view(), proj = camera.projection();
    // The shockwave: a thin, see-through bubble growing out from the blast.
    if (!bs.shocks().empty()) {
        auto sphere = MeshLibrary::get(PrimitiveType::Sphere);
        m_lit->bind();
        m_lit->setBool("uSelected", false);
        glDisable(GL_CULL_FACE);
        for (const BlastSystem::Shock& s : bs.shocks()) {
            const float t = s.radius / std::max(s.maxRadius, 1e-3f);
            const float a = 0.22f * (1.0f - t) * (1.0f - t);
            if (a < 0.005f) continue;
            glm::mat4 m = glm::scale(glm::translate(glm::mat4(1.0f), s.center), glm::vec3(s.radius * 2.0f));
            m_lit->setMat4("uModel", m);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
            m_lit->setVec3("uColor", glm::vec3(1.0f, 0.97f, 0.92f));
            m_lit->setInt("uMaterial", (int)Material::Glass);
            m_lit->setFloat("uAlpha", a);
            sphere->draw();
        }
        glEnable(GL_CULL_FACE);
    }
    const auto& puffs = bs.puffs();
    if (puffs.empty()) return;
    // Back to front, so the see-through ones layer properly.
    std::vector<std::pair<float, const BlastSystem::Puff*>> order;
    order.reserve(puffs.size());
    for (const auto& p : puffs) {
        const float z = (view * glm::vec4(p.pos, 1.0f)).z;
        if (z > p.radius) continue;   // behind the camera
        order.push_back({z, &p});
    }
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    struct V { glm::vec3 c; glm::vec2 k; glm::vec4 col; glm::vec3 info; };
    std::vector<V> verts;
    verts.reserve(order.size() * 6);
    const glm::vec2 corners[6] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    for (const auto& [z, p] : order) {
        const float fade = std::min(1.0f, p->life / std::max(p->maxLife * 0.35f, 1e-3f));   // fades out at the end
        const glm::vec4 col(p->color, p->alpha * fade);
        const glm::vec3 info(p->radius, p->glow, p->seed);
        for (const glm::vec2& k : corners) verts.push_back({p->pos, k, col, info});
    }
    if (verts.empty()) return;
    if (!m_puffVao) {
        glGenVertexArrays(1, &m_puffVao);
        glGenBuffers(1, &m_puffVbo);
        glBindVertexArray(m_puffVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_puffVbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, c));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, k));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, col));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, info));
    }
    glBindVertexArray(m_puffVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_puffVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(V)), verts.data(), GL_DYNAMIC_DRAW);
    const Environment& env = scene.environment();
    m_puff->bind();
    m_puff->setMat4("uView", view);
    m_puff->setMat4("uProj", proj);
    m_puff->setVec3("uSunView", glm::vec3(view * glm::vec4(env.sunDirection(), 0.0f)));
    const float day = std::clamp(env.sunDirection().y * 2.0f + 0.3f, 0.15f, 1.0f);
    m_puff->setVec3("uSunColor", glm::vec3(1.0f, 0.95f, 0.85f) * day);
    m_puff->setVec3("uAmbient", glm::vec3(0.42f, 0.45f, 0.5f) * (0.4f + 0.6f * day));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

void SceneRenderer::drawOverlay(const glm::mat4& view, const glm::mat4& proj) {
    if (!m_overlayVao || (!m_overlayTris && !m_overlayLines)) return;
    m_overlay->bind();
    m_overlay->setMat4("uView", view);
    m_overlay->setMat4("uProj", proj);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(m_overlayVao);
    if (m_overlayTris) glDrawArrays(GL_TRIANGLES, 0, m_overlayTris);
    if (m_overlayLines) glDrawArrays(GL_LINES, m_overlayTris, m_overlayLines);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    m_lit->bind();
}

void SceneRenderer::setGridSpacing(float studs) {
    studs = std::clamp(studs, 0.25f, 64.0f);
    if (std::fabs(studs - m_gridSpacing) < 1e-4f) return;
    m_gridSpacing = studs;
    if (m_gridVbo)  glDeleteBuffers(1, &m_gridVbo);
    if (m_gridVao)  glDeleteVertexArrays(1, &m_gridVao);
    m_gridVbo = m_gridVao = 0;
    buildGrid();
}

void SceneRenderer::buildGrid() {
    std::vector<glm::vec3> lines;
    // About 20 studs each way (more for big steps), at most 80 lines each way.
    const float s    = m_gridSpacing;
    const int   half = std::clamp((int)std::ceil(20.0f / s), 10, 80);
    const float ext  = half * s;
    for (int i = -half; i <= half; ++i) {
        // Skip the two centre lines; they are drawn separately as coloured axes.
        if (i == 0) continue;
        lines.push_back({i * s, kGridY, -ext});
        lines.push_back({i * s, kGridY,  ext});
        lines.push_back({-ext, kGridY, i * s});
        lines.push_back({ ext, kGridY, i * s});
    }
    m_gridVertexCount = (int)lines.size();

    glGenVertexArrays(1, &m_gridVao);
    glGenBuffers(1, &m_gridVbo);
    glBindVertexArray(m_gridVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_gridVbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(lines.size() * sizeof(glm::vec3)),
                 lines.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void SceneRenderer::buildAxes() {
    const glm::vec3 verts[] = {
        {-10, kGridY, 0}, {10, kGridY, 0},   // X axis  (range 0..2)
        {0, kGridY, -10}, {0, kGridY, 10},   // Z axis  (range 2..4)
    };
    glGenVertexArrays(1, &m_axisVao);
    glGenBuffers(1, &m_axisVbo);
    glBindVertexArray(m_axisVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_axisVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void SceneRenderer::ensureTargets(int w, int h) {
    if (m_hdr.w == w && m_hdr.h == h && m_hdr.fbo) return;
    createTarget(m_hdr, w, h, hdrFormat(), true);
    createTarget(m_ao, w, h, GL_R8, false);
    createTarget(m_aoRaw, w, h, GL_R8, false);
    createTarget(m_ldr, w, h, GL_RGBA8, false);
    int bw = w / 2, bh = h / 2;
    for (auto& b : m_bloom) {
        createTarget(b, bw, bh, hdrFormat(), false);
        bw = std::max(1, bw / 2);
        bh = std::max(1, bh / 2);
    }
}

void SceneRenderer::makeBackdrop(const Framebuffer& target) {
    const int w = target.width(), h = target.height();
    if (w <= 0 || h <= 0) return;
    if (m_bdDown[0].w != std::max(1, w / 2) || m_bdDown[0].h != std::max(1, h / 2) || !m_bdDown[0].fbo) {
        int bw = w / 2, bh = h / 2;
        for (int i = 0; i <= kBackdropLevels; ++i) {
            createTarget(m_bdDown[i], bw, bh, GL_RGBA8, false);
            if (i < kBackdropLevels) createTarget(m_bdUp[i], bw, bh, GL_RGBA8, false);
            bw = std::max(1, bw / 2);
            bh = std::max(1, bh / 2);
        }
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_emptyVao);
    // Halve it again and again (each step blurs a little)...
    m_bloomDown->bind();
    m_bloomDown->setInt("uSrc", 0);
    unsigned src = target.colorTexture();
    int sw = w, sh = h;
    for (int i = 0; i <= kBackdropLevels; ++i) {
        bindTarget(m_bdDown[i]);
        bindTex(0, src);
        m_bloomDown->setVec2("uTexel", glm::vec2(1.0f / sw, 1.0f / sh));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        src = m_bdDown[i].color;
        sw = m_bdDown[i].w;
        sh = m_bdDown[i].h;
    }
    // ...then smooth each one back up a size, so it doesn't look blocky when stretched.
    m_bloomUp->bind();
    m_bloomUp->setInt("uSrc", 0);
    for (int i = 0; i < kBackdropLevels; ++i) {
        bindTarget(m_bdUp[i]);
        bindTex(0, m_bdDown[i + 1].color);
        m_bloomUp->setVec2("uTexel", glm::vec2(1.0f / m_bdDown[i + 1].w, 1.0f / m_bdDown[i + 1].h));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

unsigned SceneRenderer::backdrop(int level) const {
    return m_bdUp[std::clamp(level, 0, kBackdropLevels - 1)].color;
}

void SceneRenderer::renderShadowPass(Scene& scene, const glm::mat4& lightSpace, ShadowMap& target, bool tile, float extent) {
    if (!tile) target.bindForWrite();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    // Every face goes in (not just the back ones: then a box standing on the ground
    // shadows it right up to its edge, with no light leaking under it). Acne is kept
    // away by pushing the depths back a little, more on steep faces (slope-scaled).
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.25f, 2.0f);

    m_depth->bind();
    m_depth->setMat4("uLightSpace", lightSpace);
    scene.forEach([&](SceneNode* node) {
        if (!node->mesh || !node->shown() || !node->castShadow) return;
        for (SceneNode* p = node->parent; p; p = p->parent)   // inside something hidden (a backpack) or a ViewportFrame
            if (!p->shown() || p->isViewportFrame()) return;
        if (node->kind != NodeKind::Part || node->transparency > 0.5f) return;
        const glm::mat4 m = node->worldMatrix();
        const float radius = partRadius(m);
        if (tooFar(glm::vec3(m[3]), radius)) return;
        if (extent > 0.0f) {   // off to the side of this map: it can't cast into it
            const glm::vec4 c = lightSpace * glm::vec4(glm::vec3(m[3]), 1.0f);
            const float r = 1.0f + radius / extent;
            if (std::abs(c.x) > r || std::abs(c.y) > r) return;
        }
        m_depth->setMat4("uModel", m);
        Mesh::lod = lodFor(node->mesh.get(), m);   // (the same level the camera sees, so a shadow fits its part)
        node->mesh->draw();
        Mesh::lod = 0;
    });
    if (Mesh* tm = terrainMesh(scene)) {
        m_depth->setMat4("uModel", glm::mat4(1.0f));
        tm->draw();
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glEnable(GL_BLEND);
}

glm::mat4 SceneRenderer::lampFaceView(const glm::vec3& lampPos, int face) {
    static const glm::vec3 kForward[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const glm::vec3 F = kForward[std::clamp(face, 0, 5)];
    const glm::vec3 U = (face == 2 || face == 3) ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::vec3 B = -F;
    const glm::vec3 R = glm::cross(U, B);
    // Rows R, U, B (a camera looks down its -Z), moved to the lamp.
    glm::mat4 v(1.0f);
    for (int c = 0; c < 3; ++c) { v[c][0] = R[c]; v[c][1] = U[c]; v[c][2] = B[c]; }
    v[3][0] = -glm::dot(R, lampPos);
    v[3][1] = -glm::dot(U, lampPos);
    v[3][2] = -glm::dot(B, lampPos);
    return v;
}

void SceneRenderer::renderLampShadows(Scene& scene, const glm::vec3& camPos, int most) {
    m_lampShadowNodes.clear();
    if (most <= 0) return;
    struct Lamp { const SceneNode* n; glm::vec3 pos; float range, dist; };
    std::vector<Lamp> lamps;
    scene.forEach([&](SceneNode* n) {
        if (!n->isLight() || !n->lightShadows || !n->enabled || n->brightness <= 0.0f) return;
        for (SceneNode* p = n; p; p = p->parent) if (!p->shown()) return;
        const glm::vec3 pos(n->worldMatrix()[3]);
        if (tooFar(pos, n->range)) return;
        lamps.push_back({n, pos, std::max(0.2f, n->range), glm::length(pos - camPos)});
    });
    if (lamps.empty()) return;
    std::sort(lamps.begin(), lamps.end(), [](const Lamp& a, const Lamp& b) { return a.dist < b.dist; });
    if ((int)lamps.size() > most) lamps.resize((size_t)most);

    const int tile = std::clamp(GraphicsSettings::get().shadowRes / 4, 128, 1024);
    if (tile != m_lampTile) { m_lampShadows.init(tile * 6, tile * 4); m_lampTile = tile; }
    m_lampShadows.bindForWrite();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 3.0f);
    glEnable(GL_SCISSOR_TEST);
    m_depth->bind();
    // What could cast a shadow from these lamps (worked out once, not 6 times a lamp).
    struct Caster { SceneNode* n; glm::mat4 m; glm::vec3 c; float r; };
    std::vector<Caster> casters;
    scene.forEach([&](SceneNode* node) {
        if (!node->mesh || !node->shown() || !node->castShadow) return;
        if (node->kind != NodeKind::Part || node->transparency > 0.5f) return;
        for (SceneNode* p = node->parent; p; p = p->parent)
            if (!p->shown() || p->isViewportFrame()) return;
        const glm::mat4 m = node->worldMatrix();
        const glm::vec3 c(m[3]);
        const float r = partRadius(m);
        for (const Lamp& l : lamps)
            if (glm::length(c - l.pos) < l.range + r) { casters.push_back({node, m, c, r}); return; }
    });
    for (int s = 0; s < (int)lamps.size(); ++s) {
        const Lamp& l = lamps[(size_t)s];
        m_lampShadowNodes.push_back(l.n);
        const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, l.range);
        for (int f = 0; f < 6; ++f) {
            const int tx = (s % 2) * 3 + f % 3, ty = (s / 2) * 2 + f / 3;
            glViewport(tx * tile, ty * tile, tile, tile);
            glScissor(tx * tile, ty * tile, tile, tile);
            m_depth->setMat4("uLightSpace", proj * lampFaceView(l.pos, f));
            for (const Caster& c : casters) {
                if (glm::length(c.c - l.pos) > l.range + c.r) continue;
                m_depth->setMat4("uModel", c.m);
                c.n->mesh->draw();
            }
            if (Mesh* tm = terrainMesh(scene)) {
                m_depth->setMat4("uModel", glm::mat4(1.0f));
                tm->draw();
            }
        }
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glEnable(GL_BLEND);
}

void SceneRenderer::render(Scene& scene, const Camera& camera, Framebuffer& target, bool showGrid) {
    GraphicsSettings& gs = GraphicsSettings::get();
    m_frame.reset();   // last frame's lists are free again (FrameArena.h)
    // Weather: drops fall on, and while drawing the sky, sun and fog look stormy
    // (put back as the game set them when this frame is done).
    {
        const double t = now();
        const float dt = m_weatherClock > 0.0 ? (float)(t - m_weatherClock) : 0.0f;
        m_weatherClock = t;
        // (For screenshots and checks: GB_TEST_LIGHTNING=2 makes a bolt strike every 2 seconds.)
        static const char* testBolts = std::getenv("GB_TEST_LIGHTNING");
        static const float testGap = testBolts ? std::max(0.05f, (float)std::atof(testBolts)) : 0.0f;
        if (testBolts && m_weather.nextStrike > testGap) m_weather.nextStrike = testGap;
        // Not in a running game (Studio while building): show the storm as the Lighting has it.
        if (!scene.storms().fresh) Storms::snap(scene);
        scene.storms().fresh = false;
        m_weather.update(scene, camera.position(), dt);
    }
    struct KeepLighting {
        Scene& scene; Environment saved;
        ~KeepLighting() { scene.environment() = saved; }
    } keep{scene, scene.environment()};
    scene.environment() = withWeather(keep.saved);
    // Lightning (a Storm): while the bolt is lit, IT is the light. The "sun" moves to the
    // bolt, blue-white and strong, so everything is lit from that side and throws hard
    // shadows away from it, the clouds around it glow, and the rest of the sky lifts a
    // little. And its thunder, when it gets here (the main view only, so a Studio with
    // four views doesn't play it four times).
    if (const float f = m_weather.flash(); f > 0.01f) {
        Environment& e = scene.environment();
        const glm::vec3 white(0.80f, 0.87f, 1.0f);
        glm::vec3 to = m_weather.boltMiddle() - camera.position();
        if (glm::length(to) > 1.0f && f > 0.03f) {
            to = glm::normalize(to);
            e.sunElevation = glm::degrees(std::asin(glm::clamp(to.y, 0.12f, 1.0f)));
            e.sunAzimuth = glm::degrees(std::atan2(to.z, to.x));
            e.sunColor = white;
            e.sunIntensity = 3.4f * f;
            e.sunSize = 0.05f;   // (no sun disc in the sky: only its light)
            e.shadowStrength = glm::max(keep.saved.shadowStrength, 0.85f);
        }
        e.ambientColor = glm::mix(e.ambientColor, white, 0.6f * f);
        e.groundAmbient = glm::mix(e.groundAmbient, white * 0.7f, 0.4f * f);
        e.ambientIntensity += 0.35f * f;
        e.skyHorizon = glm::mix(e.skyHorizon, white * 0.7f, 0.45f * f);
        e.skyZenith = glm::mix(e.skyZenith, white * 0.45f, 0.4f * f);
        e.cloudColor = glm::mix(e.cloudColor, white, 0.5f * f);
        e.fogColor = glm::mix(e.fogColor, white * 0.7f, 0.45f * f);
    }
    {
        float volume = 0.0f;
        glm::vec3 from(0.0f);
        if (m_weather.takeThunder(volume, from) && temporal && !Mesh::headless) {
            Audio::play("thunder", volume, 0.85f + 0.3f * (float)std::fmod(now(), 1.0));
            Audio::caption("Thunder");
        }
    }
    const Environment& env = scene.environment();
    glm::vec3 sunDir = env.sunDirection();
    gatherScreenFx(scene);
    Cloth::update(scene);         // capes and flags flap; Jiggle accessories get their springy bones
    Skin::update(scene.root());   // meshes bent by their bones (armatures)

    // Render distance (10 = no limit). Water and liquid out there rest too (less lag).
    m_viewPos = camera.position();
    m_viewDist = gs.renderDistance >= GraphicsSettings::kMaxRenderDistance ? 0.0f : gs.renderDistanceStuds();
    scene.water().setViewer(m_viewPos, m_viewDist);
    // Detail levels: far (small on screen) meshes are drawn as their simpler copies.
    m_lodOrtho = camera.orthographic;
    m_lodBias = gs.meshDetailBias();
    {
        const float screenH = (float)std::max(1, target.height());
        m_lodPixelsPerStud = camera.useProjection ? screenH * 0.5f * camera.projectionOverride[1][1]
                           : camera.orthographic ? screenH / std::max(camera.viewHeight(), 1e-3f)
                           : screenH * 0.5f / std::tan(glm::radians(camera.fov) * 0.5f);
        static const bool off = std::getenv("GB_NO_LODS") != nullptr;   // (to compare)
        if (off) m_lodPixelsPerStud = 0.0f;
    }
    Mesh::trianglesDrawn = 0;

    int w = std::max(1, (int)(target.width()  * std::clamp(gs.renderScale, 0.25f, 2.0f)));
    int h = std::max(1, (int)(target.height() * std::clamp(gs.renderScale, 0.25f, 2.0f)));
    ensureTargets(w, h);
    // The temporal upscaler (High and up): this frame is drawn moved by a fraction of a pixel.
    beginTemporal(camera, target.width(), target.height(), w, h);
    // The atlas holds 2 x 2 tiles (no bigger than 2048 each: wide maps don't need more).
    const int cascadeTile = std::min(gs.shadowRes, 2048);
    if (m_shadowRes != gs.shadowRes) {
        m_shadow.init(gs.shadowRes);
        m_shadowAtlas.init(cascadeTile * 2);
        m_shadowRes = gs.shadowRes;
        for (CascadeTile& t : m_cascadeTile) t.drawn = false;
    }

    // --- Shadow maps, centred on what the camera looks at and snapped to their pixel
    //     grids so shadows don't shimmer as the camera moves (ShadowCascades.h) ---
    const float extent = std::max(5.0f, env.shadowDistance * 0.5f);
    glm::mat4 lightView0(1.0f);
    float texel = 0.0f, baseDepthRange = 0.0f;
    glm::mat4 lightSpace = ShadowCascades::lightSpace(sunDir, camera.pivot, extent, m_shadow.size(), extent,
                                                      lightView0, texel, baseDepthRange);
    bool shadows = env.shadows && gs.shadows && env.sunElevation > -5.0f && shading == Shading::Material;   // (Studio's plain views have none)
    FrameProfile::finishGpu() = []() { glFinish(); };
    if (shadows) { FrameProfile::Scope prof("draw: shadows", true); renderShadowPass(scene, lightSpace, m_shadow); }
    m_lightSpace = lightSpace;
    m_shadowsOn = shadows;
    // The other maps: sharper ones inside the base, wider ones outside it.
#ifdef GB_MOBILE
    const bool phone = true;
#else
    const bool phone = false;
#endif
    const ShadowCascades::Plan cascades = ShadowCascades::plan(extent, shadows ? ShadowCascades::mapsFor(gs.shadowQuality, phone) : 1);
    m_cascadeMaps = shadows ? cascades.count : 0;
    ++m_shadowFrame;
    for (int i = 0, tile = 0; i < cascades.count; ++i) {
        if (i == cascades.base) continue;
        CascadeTile& t = m_cascadeTile[i];
        // (One-off pictures, like thumbnails, can't lean on the frame before: always redraw.)
        const bool stale = !temporal || !t.drawn || std::abs(t.extent - cascades.extent[i]) > 1e-3f;
        if (ShadowCascades::redrawNow(cascades, i, m_shadowFrame, stale)) {
            glm::mat4 view;
            t.extent = cascades.extent[i];
            t.space = ShadowCascades::lightSpace(sunDir, camera.pivot, t.extent, cascadeTile, extent, view, t.texel, t.depthRange);
            t.drawn = true;
            FrameProfile::Scope prof("draw: shadow cascades", true);
            m_shadowAtlas.bindTileForWrite((tile % 2) * cascadeTile, (tile / 2) * cascadeTile, cascadeTile, cascadeTile);
            renderShadowPass(scene, t.space, m_shadowAtlas, true, t.extent);
        }
        ++tile;
    }

    // Lamps with Shadows on (4 nearest; 2 on low shadow quality).
    m_lampShadowNodes.clear();
    if (gs.shadows && shading == Shading::Material) {
        FrameProfile::Scope prof("draw: lamp shadows", true);
        renderLampShadows(scene, camera.position(), gs.shadowQuality > 0 ? 4 : 2);
    }

    // --- Main HDR pass ---
    bindTarget(m_hdr);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 view = camera.view();
    glm::mat4 proj = camera.projection();
    float time = (float)(now() - m_startTime);

    if (shading != Shading::Material) {
        // Studio's plain views: a plain grey backdrop (like Blender's), not the sky.
        glClearColor(0.045f, 0.047f, 0.052f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    } else if (env.showSky) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_sky->bind();
        m_sky->setMat4("uInvViewProj", glm::inverse(proj * view));
        m_sky->setVec3("uZenith",  env.skyZenith);
        m_sky->setVec3("uHorizon", env.skyHorizon);
        m_sky->setVec3("uGround",  env.skyGround);
        m_sky->setFloat("uSkyBrightness", env.skyBrightness);
        m_sky->setVec3("uSunDir",  sunDir);
        m_sky->setVec3("uSunColor", env.sunColor);
        m_sky->setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
        m_sky->setFloat("uSunSize", env.sunSize);
        m_sky->setVec3("uAmbient", env.ambientColor * env.ambientIntensity);
        m_sky->setBool("uClouds", env.clouds);
        m_sky->setFloat("uCloudCover", env.cloudCover);
        m_sky->setFloat("uCloudSpeed", env.cloudSpeed);
        m_sky->setVec3("uCloudColor", env.cloudColor);
        m_sky->setBool("uStars", env.stars);
        {
            // Real 3D clouds from High graphics up (the flat painted ones below that).
            static const int kSteps[4] = {0, 28, 44, 72};
            const int level = std::clamp(gs.volumetricClouds(), 0, 3);
            const int shape = level > 0 && env.clouds ? cloudShape(env.cloudType, env.weather, env.cloudCover) : 0;
            if (!m_cloudNoise) {
                const std::vector<uint8_t> data = FogVolume::noise();
                const int n = FogVolume::kNoiseSize;
                glGenTextures(1, &m_cloudNoise);
                glBindTexture(GL_TEXTURE_3D, m_cloudNoise);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage3D(GL_TEXTURE_3D, 0, GL_R8, n, n, n, 0, GL_RED, GL_UNSIGNED_BYTE, data.data());
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_REPEAT);
            }
            glActiveTexture(GL_TEXTURE0 + 9);
            glBindTexture(GL_TEXTURE_3D, m_cloudNoise);
            glActiveTexture(GL_TEXTURE0);
            m_sky->setInt("uCloudNoise", 9);
            m_sky->setInt("uCloudShape", shape);
            m_sky->setInt("uCloudSteps", kSteps[level]);
            m_sky->setVec3("uCamPos", camera.position());
            // (Storm clouds race along; fair-weather ones drift.)
            m_sky->setVec2("uCloudDrift", glm::vec2(1.0f, 0.35f) * (time * env.cloudSpeed * (isStorm(env.weather) ? 14.0f : 5.0f)));
            const Storms::State& st = scene.storms();
            m_sky->setVec3("uStorm", {env.stormCenter.x, env.stormCenter.z, time * 0.03f});
            const glm::vec3 wall = st.tornado > 0.0f ? st.tornadoPos : Storms::wallCloud(scene);
            m_sky->setVec4("uWallCloud", {wall.x, wall.z, 60.0f + 25.0f * std::min(st.tornado, 1.0f), 170.0f});
        }
        m_sky->setFloat("uTime", time);
        {   // A Sky in Lighting: its skybox, stars, sun and moon.
            const SkyProps* sk = m_fx.sky ? &m_fx.sky->sky : nullptr;
            m_sky->setBool("uSkyObj", sk != nullptr);
            bool box = false, sunTex = false, moonTex = false;
            if (sk) {
                const std::string* faces[6] = {&sk->bk, &sk->dn, &sk->ft, &sk->lf, &sk->rt, &sk->up};
                static const char* const names[6] = {"uSkyBk", "uSkyDn", "uSkyFt", "uSkyLf", "uSkyRt", "uSkyUp"};
                unsigned tex[6];
                box = true;
                for (int i = 0; i < 6; ++i) { tex[i] = Textures::get(*faces[i]); box = box && tex[i]; }
                if (box)
                    for (int i = 0; i < 6; ++i) { bindTex(1 + i, tex[i]); m_sky->setInt(names[i], 1 + i); }
                const unsigned st = Textures::get(sk->sun), mt = Textures::get(sk->moon);
                sunTex = st != 0;
                moonTex = mt != 0;
                if (st) { bindTex(7, st); m_sky->setInt("uSunTex", 7); }
                if (mt) { bindTex(8, mt); m_sky->setInt("uMoonTex", 8); }
                m_sky->setFloat("uStarDensity", (float)sk->starCount / 3000.0f);
                m_sky->setBool("uCelestial", sk->celestial);
                m_sky->setFloat("uSunAngle", sk->sunSize);
                m_sky->setFloat("uMoonAngle", sk->moonSize);
                // Night (the light comes from the moon), the way Lighting.ClockTime works it out.
                const float h = std::fmod(std::fmod(env.clockTime, 24.0f) + 24.0f, 24.0f);
                m_sky->setBool("uNight", std::sin((h - 6.0f) / 12.0f * 3.14159265f) < -0.05f);
            }
            m_sky->setBool("uSkybox", box);
            m_sky->setBool("uSunTexOn", sunTex);
            m_sky->setBool("uMoonTexOn", moonTex);
            glActiveTexture(GL_TEXTURE0);
        }
        glBindVertexArray(m_emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    } else {
        glm::vec3 c = glm::pow(env.skyHorizon, glm::vec3(2.2f));
        glClearColor(c.r, c.g, c.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    if (showGrid) {
        m_grid->bind();
        m_grid->setMat4("uView", view);
        m_grid->setMat4("uProj", proj);
        m_grid->setVec3("uColor", {0.12f, 0.13f, 0.16f});
        glBindVertexArray(m_gridVao);
        glDrawArrays(GL_LINES, 0, m_gridVertexCount);
        glBindVertexArray(m_axisVao);
        m_grid->setVec3("uColor", {0.55f, 0.06f, 0.06f});
        glDrawArrays(GL_LINES, 0, 2);
        m_grid->setVec3("uColor", {0.06f, 0.2f, 0.62f});
        glDrawArrays(GL_LINES, 2, 2);
    }

    // --- Lit geometry ---
    m_lit->bind();
    m_lit->setBool("uClothing", false);
    m_lit->setBool("uFace", false);
    m_lit->setFloat("uBlink", 0.0f);
    m_lit->setMat4("uView", view);
    m_lit->setMat4("uProj", proj);
    m_lit->setVec3("uViewPos", camera.position());
    m_lit->setFloat("uRenderDist", m_viewDist);
    {   // Wet patches near the camera (the wettest, closest ones).
        glm::vec4 spots[16];
        float amt[16];
        int n = 0;
        const WaterSystem& ws = scene.water();
        std::vector<std::pair<float, int>> order;
        for (size_t i = 0; i < ws.wetSpots().size(); ++i)
            order.push_back({glm::length(ws.wetSpots()[i].pos - m_viewPos), (int)i});
        std::sort(order.begin(), order.end());
        for (auto& [d, i] : order) {
            if (n >= 16) break;
            const WaterSystem::WetSpot& s = ws.wetSpots()[(size_t)i];
            spots[n] = glm::vec4(s.pos, s.radius);
            amt[n] = ws.wetness(s);
            ++n;
        }
        m_lit->setInt("uWetCount", n);
        if (n) { m_lit->setVec4Array("uWetSpots", spots, n); m_lit->setFloatArray("uWetAmt", amt, n); }
        m_lit->setFloat("uWetPart", 0.0f);
        m_lit->setFloat("uRainWet", m_weather.wetness());
        m_lit->setFloat("uSnow", m_weather.snowCover());
    }
    m_lit->setFloat("uTime", time);
    m_lit->setVec3("uSunDir", sunDir);
    m_lit->setVec3("uSunColor", env.sunColor);
    m_lit->setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
    m_lit->setVec3("uAmbientSky", env.ambientColor);
    m_lit->setVec3("uAmbientGround", env.groundAmbient);
    m_lit->setFloat("uAmbientIntensity", env.ambientIntensity);
    m_lit->setFloat("uReflections", env.reflections);
    m_lit->setVec3("uZenith",  env.skyZenith);
    m_lit->setVec3("uHorizon", env.skyHorizon);
    m_lit->setVec3("uGround",  env.skyGround);
    m_lit->setBool("uFogEnabled", env.fogEnabled);
    m_lit->setVec3("uFogColor", env.fogColor);
    m_lit->setFloat("uFogDensity", env.fogDensity);
    m_lit->setFloat("uFogSunGlow", env.fogSunGlow);
    m_lit->setFloat("uFogHeight", env.fogHeight);
    m_lit->setFloat("uFogFalloff", std::max(0.0f, env.fogFalloff));
    // Volumetric fog: the solid things are drawn without fog, and get it afterwards
    // (drawFogVolume, from drawGeometry).
    m_fogVolOn = wantFogVolume(scene, camera);
    m_usedFogVol = false;
    m_lit->setBool("uFogVolumetric", m_fogVolOn);
    m_lit->setBool("uShadowsEnabled", shadows);
    // Studio's view style (Wireframe's lines are the plain colours) and X-ray.
    m_lit->setInt("uShading", shading == Shading::Wireframe ? 2 : (int)shading);
    m_lit->setFloat("uXrayFade", 0.0f);
    m_lit->setInt("uShadowQuality", gs.shadowQuality);
    m_lit->setFloat("uShadowSoftness", env.shadowSoftness);
    m_lit->setFloat("uShadowStrength", env.shadowStrength);
    m_lit->setFloat("uShadowTexelWorld", texel);
    m_lit->setFloat("uShadowDepthRange", baseDepthRange);
    m_lit->setMat4("uLightSpace", lightSpace);
    m_shadow.bindForRead(0);
    m_lit->setInt("uShadowMap", 0);
    // Every cascade, smallest first: its matrix, pixel size and depth range, and where
    // its tile is in the atlas (z < 0 = it's the base map, not a tile).
    m_shadowAtlas.bindForRead(6);
    m_lit->setInt("uShadowAtlas", 6);
    m_lit->setInt("uCascadeCount", cascades.count);
    for (int i = 0, tile = 0; i < cascades.count; ++i) {
        char name[32];
        const bool base = i == cascades.base;
        std::snprintf(name, sizeof(name), "uCascadeSpace[%d]", i);
        m_lit->setMat4(name, base ? lightSpace : m_cascadeTile[i].space);
        std::snprintf(name, sizeof(name), "uCascadeInfo[%d]", i);
        m_lit->setVec2(name, base ? glm::vec2(texel, baseDepthRange) : glm::vec2(m_cascadeTile[i].texel, m_cascadeTile[i].depthRange));
        std::snprintf(name, sizeof(name), "uCascadeRect[%d]", i);
        m_lit->setVec4(name, base ? glm::vec4(0.0f, 0.0f, -1.0f, -1.0f) : ShadowCascades::tileRect(tile));
        if (!base) ++tile;
    }

    // Collect point / spot lights; keep the ones closest to the camera.
    struct LightItem { glm::vec4 pos, col, dir; float dist; const SceneNode* node; };
    FrameList<LightItem> lights(m_frame, 32);   // (frame memory: nothing asked from the system here)
    glm::vec3 camPos = camera.position();
    scene.forEach([&](SceneNode* n) {
        if (!n->isLight() || !n->enabled || n->brightness <= 0.0f) return;
        for (SceneNode* p = n; p; p = p->parent) if (!p->shown()) return;
        glm::mat4 m = n->worldMatrix();
        glm::vec3 pos(m[3]);
        if (tooFar(pos, n->range)) return;   // its light can't reach anything we draw
        glm::vec3 dir = glm::normalize(glm::vec3(m * glm::vec4(0, -1, 0, 0)));
        float cosHalf = std::cos(glm::radians(std::clamp(n->spotAngle, 1.0f, 179.0f) * 0.5f));
        glm::vec3 col = glm::pow(n->color, glm::vec3(2.2f)) * n->brightness;
        lights.push_back({glm::vec4(pos, n->range), glm::vec4(col, n->lightType == LightType::Spot ? 1.0f : 0.0f),
                          glm::vec4(dir, cosHalf), glm::length(pos - camPos), n});
    });
    std::sort(lights.begin(), lights.end(), [](auto& a, auto& b) { return a.dist < b.dist; });
    const int most = std::min(kMaxLights, gs.maxLights);
    // The nearest real lamps also glow in the volumetric fog.
    m_fogLightCount = 0;
    for (const LightItem& l : lights) {
        if (m_fogLightCount >= 8) break;
        m_fogLightPos[m_fogLightCount] = l.pos;
        m_fogLightColor[m_fogLightCount] = l.col;
        m_fogLightDir[m_fogLightCount] = l.dir;
        ++m_fogLightCount;
    }
    // Bouncy light: soft coloured lights where sun and lamp light land (BounceLight.h),
    // worked out about 4 times a second, after the real lamps (if there's room).
    if (env.bounceLight > 0.0f && shading == Shading::Material) {
        const double t = now();
        if (t - m_bounceAt > 0.25 || t < m_bounceAt || glm::length(camera.pivot - m_bounceAround) > BounceLight::kSpacing) {
            m_bounceAt = t;
            m_bounceAround = camera.pivot;
            std::vector<BounceLight::Lamp> near;
            for (const LightItem& l : lights)
                if (near.size() < (size_t)BounceLight::kMostLamps)
                    near.push_back({glm::vec3(l.pos), glm::vec3(l.col), l.pos.w});
            m_bounce = BounceLight::gather(scene, env, camera.pivot, near);
        }
        if ((int)lights.size() > most) lights.resize((size_t)most);
        for (const BounceLight::Point& b : m_bounce)
            if ((int)lights.size() < most) lights.push_back({glm::vec4(b.pos, b.range), glm::vec4(b.color, 2.0f), glm::vec4(b.normal, 0.0f), 0.0f, nullptr});
    } else {
        m_bounce.clear();
    }
    int count = std::min<int>((int)lights.size(), most);
    glm::vec4 lp[kMaxLights], lc[kMaxLights], ld[kMaxLights];
    for (int i = 0; i < count; ++i) { lp[i] = lights[i].pos; lc[i] = lights[i].col; ld[i] = lights[i].dir; }
    m_lit->setInt("uLightCount", count);
    m_lit->setVec4Array("uLightPosRange", lp, count);
    m_lit->setVec4Array("uLightColor", lc, count);
    m_lit->setVec4Array("uLightDir", ld, count);
    float slots[kMaxLights];
    for (int i = 0; i < count; ++i) {
        slots[i] = -1.0f;
        for (size_t k = 0; k < m_lampShadowNodes.size(); ++k)
            if (m_lampShadowNodes[k] == lights[i].node) slots[i] = (float)k;
    }
    m_lit->setFloatArray("uLampShadowSlot", slots, count);
    m_lampShadows.bindForRead(10);
    m_lit->setInt("uLampShadows", 10);
    m_lit->setFloat("uLampTileTexel", m_lampTile > 0 ? 1.0f / (float)m_lampTile : 0.01f);
    bindBounce(*m_lit);   // last frame's traced bounce light (Medium and up)
    bindReflections(camera);   // last frame's picture, for shiny parts to mirror (High and up)

    { FrameProfile::Scope prof("draw: parts and water", true); drawGeometry(scene, camera, showGrid); }
    drawOverlay(view, proj);
    glBindVertexArray(0);
    { FrameProfile::Scope prof("draw: liquid", true); renderLiquid(scene, camera); }

    { FrameProfile::Scope prof("draw: bounce light", true); traceBounce(camera); }
    { FrameProfile::Scope prof("draw: reflections", true); keepForReflections(camera); }

    { FrameProfile::Scope prof("draw: post effects", true); postProcess(scene, camera, target); }
    endTemporal(camera);   // (Highlights are drawn on the finished picture, without the jitter)
    drawHighlights(scene, camera, target);
    m_lastTriangles = Mesh::trianglesDrawn;
    static const bool sayTriangles = std::getenv("GB_TEST_TRIANGLES") != nullptr;   // (to measure detail levels)
    if (sayTriangles && temporal) {
        static int frame = 0;
        if (++frame % 60 == 0) Log::info("triangles drawn: " + std::to_string(m_lastTriangles));
    }
}

// Shiny parts mirror the world (screen-space reflections, like Unreal's). Drawing a
// part can't read the picture it's drawing into, so each frame ends with a copy of the
// lit picture and its depth, and the next frame's shiny parts walk their reflected rays
// through that copy (seen from last frame's camera, so it still lines up when you turn).
// High and up only (GraphicsSettings::shinyReflections); the main views only.
void SceneRenderer::bindReflections(const Camera& camera) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    const int level = gs.shinyReflections();
    const bool on = m_ssrReady && level >= 1 && temporal && shading == Shading::Material && !xray &&
                    !camera.orthographic && !camera.useProjection && m_ssrCopy.w == m_hdr.w && m_ssrCopy.h == m_hdr.h &&
                    glm::length(camera.position() - m_ssrPos) < 8.0f;   // (not across a teleport)
    m_usedSsr = on;
    m_lit->setBool("uSsrOn", on);
    m_lit->setInt("uSsrColor", 13);
    m_lit->setInt("uSsrDepth", 14);
    if (!on) return;
    static const int kSteps[4] = {0, 20, 32, 48};   // High, Epic, Cinematic
    bindTex(13, m_ssrCopy.color);
    bindTex(14, m_ssrCopy.depth);
    glActiveTexture(GL_TEXTURE0);
    m_lit->setMat4("uSsrViewProj", m_ssrViewProj);
    m_lit->setVec2("uSsrNearFar", glm::vec2(0.1f, 1000.0f));   // (Camera::projection's)
    m_lit->setInt("uSsrSteps", kSteps[std::clamp(level, 0, 3)]);
    m_lit->setFloat("uSsrNoise", m_usedUpscaler ? 1.0f : 0.0f);   // (random starts only when frames get mixed)
}

void SceneRenderer::keepForReflections(const Camera& camera) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    if (gs.shinyReflections() < 1 || !temporal || shading != Shading::Material || xray || camera.orthographic ||
        camera.useProjection || Mesh::headless) {
        m_ssrReady = false;
        return;
    }
    if (m_ssrCopy.w != m_hdr.w || m_ssrCopy.h != m_hdr.h || !m_ssrCopy.fbo)
        createTarget(m_ssrCopy, m_hdr.w, m_hdr.h, hdrFormat(), true);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hdr.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ssrCopy.fbo);
    glBlitFramebuffer(0, 0, m_hdr.w, m_hdr.h, 0, 0, m_hdr.w, m_hdr.h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hdr.fbo);
    m_ssrViewProj = camera.projection() * camera.view();   // (with this frame's jitter: it matches the copy)
    m_ssrPos = camera.position();
    m_ssrReady = true;
}

// Highlights: each one's parts are drawn flat into a mask (what covers the screen,
// and what of that isn't behind something), then the mask is filled in and an
// outline drawn round it, on top of the finished picture (so lighting, fog and
// bloom don't change the colours). Like Roblox, at most 31 show at once.
void SceneRenderer::drawHighlights(Scene& scene, const Camera& camera, Framebuffer& target) {
    FrameList<SceneNode*> lights(m_frame, 32);
    scene.forEach([&](SceneNode* n) {
        if (lights.size() >= 31 || !n->isHighlight() || !n->enabled) return;
        const HighlightProps& h = n->highlight;
        if (h.fillTransparency >= 0.999f && h.outlineTransparency >= 0.999f) return;
        lights.push_back(n);
    });
    if (lights.empty()) return;

    if (m_hlTarget.w != m_hdr.w || m_hlTarget.h != m_hdr.h || !m_hlTarget.fbo)
        createTarget(m_hlTarget, m_hdr.w, m_hdr.h, GL_RGBA8, false);
    const glm::mat4 proj = camera.projection();
    const glm::mat4 viewProj = proj * camera.view();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    std::vector<SceneNode*> parts, stack;
    for (SceneNode* hl : lights) {
        const HighlightProps& h = hl->highlight;
        SceneNode* on = h.adornee ? scene.findById(h.adornee) : hl->parent;
        if (!on) continue;
        // Its parts: the adornee and everything in it that shows (hidden models hide theirs).
        bool hidden = false;
        for (SceneNode* p = on; p; p = p->parent) if (!p->shown()) hidden = true;
        if (hidden) continue;
        parts.clear();
        stack.assign(1, on);
        while (!stack.empty()) {
            SceneNode* n = stack.back();
            stack.pop_back();
            if (!n->shown()) continue;
            if (n->kind == NodeKind::Part && n->mesh && n->shownTransparency() < 0.99f && !Player::isWater(n)) parts.push_back(n);
            for (auto& c : n->children) stack.push_back(c.get());
        }
        if (parts.empty()) continue;

        // 1. The mask.
        bindTarget(m_hlTarget);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);   // covered anywhere = covered
        glDisable(GL_CULL_FACE);       // (inside a part counts as covered too)
        m_hlMask->bind();
        m_hlMask->setMat4("uViewProj", viewProj);
        bindTex(0, m_hdr.depth);
        m_hlMask->setInt("uDepth", 0);
        m_hlMask->setVec2("uSize", glm::vec2((float)m_hlTarget.w, (float)m_hlTarget.h));
        m_hlMask->setVec2("uDepthParams", glm::vec2(proj[3][2], proj[2][2]));
        m_hlMask->setBool("uOrtho", camera.orthographic);
        for (SceneNode* n : parts) {
            const glm::mat4 m = n->worldMatrix();
            if (tooFar(glm::vec3(m[3]), partRadius(m))) continue;
            m_hlMask->setMat4("uModel", m);
            n->mesh->draw();
        }
        glEnable(GL_CULL_FACE);

        // 2. Fill and outline, over the picture.
        target.bind();
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);   // (keep it solid)
        m_hl->bind();
        bindTex(0, m_hlTarget.color);
        m_hl->setInt("uMask", 0);
        m_hl->setVec2("uTexel", glm::vec2(1.0f / std::max(1, target.width()), 1.0f / std::max(1, target.height())));
        m_hl->setBool("uOnTop", h.onTop);
        m_hl->setVec4("uFill", glm::vec4(h.fill, 1.0f - h.fillTransparency));
        m_hl->setVec4("uOutline", glm::vec4(h.outline, 1.0f - h.outlineTransparency));
        glBindVertexArray(m_emptyVao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    glActiveTexture(GL_TEXTURE0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glDepthMask(GL_TRUE);
    target.unbind();
}

namespace {
// A cylinder from a to b (used for ropes, rods and springs).
glm::mat4 segment(const glm::vec3& a, const glm::vec3& b, float thickness) {
    glm::vec3 d = b - a;
    float len = glm::length(d);
    glm::vec3 y = len > 1e-6f ? d / len : glm::vec3(0, 1, 0);
    glm::vec3 x = std::abs(y.y) < 0.99f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), y)) : glm::vec3(1, 0, 0);
    glm::vec3 z = glm::cross(x, y);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x * thickness, 0);
    m[1] = glm::vec4(y * len, 0);
    m[2] = glm::vec4(z * thickness, 0);
    m[3] = glm::vec4((a + b) * 0.5f, 1);
    return m;
}
} // namespace

void SceneRenderer::drawConstraints(Scene& scene, bool editing) {
    auto cyl = MeshLibrary::get(PrimitiveType::Cylinder);
    auto ball = MeshLibrary::get(PrimitiveType::Sphere);
    m_lit->setBool("uSelected", false);
    m_lit->setFloat("uAlpha", 1.0f);
    auto draw = [&](const glm::mat4& m, const glm::vec3& color, Material mat, Mesh& mesh) {
        m_lit->setMat4("uModel", m);
        m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
        m_lit->setVec3("uColor", color);
        m_lit->setInt("uMaterial", materialShaderId(mat));
        mesh.draw();
    };
    scene.forEach([&](SceneNode* n) {
        if (n->isAttachment() && editing) {
            glm::vec3 p(n->worldMatrix()[3]);
            glm::mat4 m = glm::translate(glm::mat4(1.0f), p) * glm::scale(glm::mat4(1.0f), glm::vec3(0.18f));
            draw(m, n->selected ? glm::vec3(1, 0.6f, 0.1f) : glm::vec3(0.2f, 1.0f, 0.3f), Material::Neon, *ball);
            return;
        }
        if (!n->isConstraint() || !n->shown() || n->constraintType == ConstraintType::Weld) return;
        SceneNode* a0 = scene.findById(n->ref0);
        SceneNode* a1 = scene.findById(n->ref1);
        if (!a0 || !a1) return;
        glm::vec3 p0(a0->worldMatrix()[3]), p1(a1->worldMatrix()[3]);
        float th = std::max(0.02f, n->thickness);
        glm::vec3 col = n->selected ? glm::vec3(1, 0.6f, 0.1f) : n->color;
        switch (n->constraintType) {
            case ConstraintType::Rope: {
                // Slack ropes sag in the middle.
                float d = glm::length(p1 - p0);
                float sag = n->length > d ? std::sqrt(n->length * n->length - d * d) * 0.5f : 0.0f;
                const int segs = 12;
                glm::vec3 prev = p0;
                for (int i = 1; i <= segs; ++i) {
                    float t = (float)i / segs;
                    glm::vec3 p = glm::mix(p0, p1, t) - glm::vec3(0, sag * 4.0f * t * (1.0f - t), 0);
                    draw(segment(prev, p, th), col, Material::Plastic, *cyl);
                    prev = p;
                }
                break;
            }
            case ConstraintType::Rod:
                draw(segment(p0, p1, th), col, Material::Metal, *cyl);
                break;
            case ConstraintType::Spring: {
                // Zig-zag coil.
                glm::vec3 d = p1 - p0;
                float len = glm::length(d);
                if (len < 1e-4f) break;
                glm::vec3 y = d / len;
                glm::vec3 x = std::abs(y.y) < 0.99f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), y)) : glm::vec3(1, 0, 0);
                glm::vec3 z = glm::cross(x, y);
                const int turns = 8, per = 6;
                glm::vec3 prev = p0;
                for (int i = 1; i <= turns * per; ++i) {
                    float t = (float)i / (turns * per);
                    float ang = t * turns * 6.2831853f;
                    float r = (i == turns * per) ? 0.0f : 0.25f;
                    glm::vec3 p = p0 + d * t + (x * std::cos(ang) + z * std::sin(ang)) * r;
                    draw(segment(prev, p, th * 0.6f), col, Material::Metal, *cyl);
                    prev = p;
                }
                break;
            }
            case ConstraintType::Hinge:
                draw(segment(p0 - glm::vec3(a0->worldMatrix()[0]) * 0.0f, p1, th * 0.5f), col, Material::Metal, *cyl);
                break;
            case ConstraintType::BallSocket: {
                // A ball in its socket, with a thin line to Attachment1 if they've drifted apart.
                const float r = std::max(0.15f, th * 2.0f);
                draw(glm::translate(glm::mat4(1.0f), p0) * glm::scale(glm::mat4(1.0f), glm::vec3(r)), col, Material::Metal, *ball);
                if (glm::length(p1 - p0) > r * 0.5f) draw(segment(p0, p1, th * 0.5f), col, Material::Metal, *cyl);
                break;
            }
            case ConstraintType::Prismatic:
            case ConstraintType::Cylindrical: {
                // The rail along Attachment0's axis (from LowerLimit to UpperLimit when
                // limited), and a sleeve where Attachment1 rides on it.
                glm::vec3 x(a0->worldMatrix()[0]);
                if (glm::length(x) < 1e-6f) break;
                x = glm::normalize(x);
                const JointProps& jp = n->joint;
                const float s = glm::dot(p1 - p0, x);
                float lo = std::min(0.0f, s), hi = std::max(0.0f, s);
                if (jp.limitsEnabled) {
                    lo = std::min(lo, std::min(jp.lowerLimit, jp.upperLimit));
                    hi = std::max(hi, std::max(jp.lowerLimit, jp.upperLimit));
                }
                if (hi - lo < 0.2f) { lo -= 0.1f; hi += 0.1f; }
                draw(segment(p0 + x * lo, p0 + x * hi, th * 0.5f), col, Material::Metal, *cyl);
                const float sleeve = std::max(0.25f, th * 4.0f);
                draw(segment(p1 - x * sleeve, p1 + x * sleeve, th * 1.6f), col * 0.8f, Material::Metal, *cyl);
                if (n->constraintType == ConstraintType::Cylindrical) {
                    // A ring around the sleeve shows it can spin.
                    const glm::vec3 u = std::abs(x.y) < 0.99f ? glm::normalize(glm::cross(x, glm::vec3(0, 1, 0)))
                                                              : glm::normalize(glm::cross(x, glm::vec3(1, 0, 0)));
                    const glm::vec3 v = glm::cross(x, u);
                    const float rr = sleeve * 1.2f;
                    glm::vec3 prev = p1 + u * rr;
                    for (int i = 1; i <= 16; ++i) {
                        const float a = i / 16.0f * 6.2831853f;
                        const glm::vec3 q = p1 + (u * std::cos(a) + v * std::sin(a)) * rr;
                        draw(segment(prev, q, th * 0.4f), col, Material::Metal, *cyl);
                        prev = q;
                    }
                }
                break;
            }
            default: break;
        }
    });
}

// Where a decal sits: the plane mesh (flat in X/Z, facing +Y) turned to lie on
// one side of its part's unit box, so the picture reads the right way round
// when you look at that side.
glm::mat4 SceneRenderer::decalMatrix(const SceneNode& d) {
    glm::vec3 R, U, N;
    switch (d.face) {
        case Face::Front:  R = {-1, 0, 0}; U = {0, 1, 0};  N = {0, 0, -1}; break;
        case Face::Back:   R = {1, 0, 0};  U = {0, 1, 0};  N = {0, 0, 1};  break;
        case Face::Right:  R = {0, 0, -1}; U = {0, 1, 0};  N = {1, 0, 0};  break;
        case Face::Left:   R = {0, 0, 1};  U = {0, 1, 0};  N = {-1, 0, 0}; break;
        case Face::Top:    R = {1, 0, 0};  U = {0, 0, -1}; N = {0, 1, 0};  break;
        default:           R = {1, 0, 0};  U = {0, 0, 1};  N = {0, -1, 0}; break;   // Bottom
    }
    glm::mat4 local(1.0f);
    local[0] = glm::vec4(R, 0);
    local[1] = glm::vec4(N, 0);
    local[2] = glm::vec4(U, 0);
    local[3] = glm::vec4(N * 0.5f, 1);
    return d.parent->worldMatrix() * local;
}

namespace {

// The shape of a water part while its waves move: the wavy top, plus the sides
// down to the bottom (so a pool looks full from the side too). World space.
void buildWaterMesh(const WaterSystem& ws, const WaterSystem::Body& b, std::vector<Vertex>& verts,
                    std::vector<uint32_t>& idx) {
    verts.clear();
    idx.clear();
    // Finer than the ripple grid, so the ocean waves (added in the vertex shader)
    // stay smooth: about 2 points a stud, at most 180 along a side.
    const glm::vec3 size = b.max - b.min;
    const float cell = std::max({0.5f, size.x / 179.0f, size.z / 179.0f});
    const int nx = std::max(2, (int)std::ceil(size.x / cell) + 1), nz = std::max(2, (int)std::ceil(size.z / cell) + 1);
    auto X = [&](int i) { return std::min(b.min.x + i * cell, b.max.x); };
    auto Z = [&](int k) { return std::min(b.min.z + k * cell, b.max.z); };
    std::vector<float> top((size_t)nx * nz);
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) top[(size_t)k * nx + i] = ws.rippleSurface(b, X(i), Z(k));
    auto T = [&](int i, int k) { return top[(size_t)std::clamp(k, 0, nz - 1) * nx + std::clamp(i, 0, nx - 1)]; };
    // uv: x = foam where the surface is churned up, y = 1 (moves with the waves).
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
            glm::vec3 n(-(T(i + 1, k) - T(i - 1, k)) / (2.0f * cell), 1.0f, -(T(i, k + 1) - T(i, k - 1)) / (2.0f * cell));
            const float foam = std::clamp((ws.churn(b, X(i), Z(k)) - 0.4f) * 1.2f, 0.0f, 1.0f);
            verts.push_back({{X(i), T(i, k), Z(k)}, glm::normalize(n), {foam, 1.0f}});
        }
    for (int k = 0; k + 1 < nz; ++k)
        for (int i = 0; i + 1 < nx; ++i) {
            uint32_t a = (uint32_t)(k * nx + i), c = a + (uint32_t)nx;
            idx.insert(idx.end(), {a, c, a + 1, a + 1, c, c + 1});
        }
    // Sides: a strip along each edge from the bottom up to the waves (the top edge
    // rises and falls with them).
    auto side = [&](int count, auto pointAt, glm::vec3 normal) {
        uint32_t base = (uint32_t)verts.size();
        for (int s = 0; s < count; ++s) {
            glm::vec3 p = pointAt(s);
            verts.push_back({{p.x, b.min.y, p.z}, normal, {0, 0}});
            verts.push_back({p, normal, {0, 1}});
        }
        for (int s = 0; s + 1 < count; ++s) {
            uint32_t a = base + 2 * s;
            idx.insert(idx.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
        }
    };
    side(nx, [&](int i) { return glm::vec3(X(i), T(i, 0), b.min.z); }, {0, 0, -1});
    side(nx, [&](int i) { return glm::vec3(X(i), T(i, nz - 1), b.max.z); }, {0, 0, 1});
    side(nz, [&](int k) { return glm::vec3(b.min.x, T(0, k), Z(k)); }, {-1, 0, 0});
    side(nz, [&](int k) { return glm::vec3(b.max.x, T(nx - 1, k), Z(k)); }, {1, 0, 0});
}

} // namespace

// Water parts: after the solid scene (which they read to see how deep they are and
// what's under them), before particles and see-through things.
void SceneRenderer::drawWater(Scene& scene, const Camera& camera, const std::vector<WaterItem>& waters) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    const Environment& env = scene.environment();
    const WaterSystem& waves = scene.water();
    // A copy of what's been drawn so far: the ground, walls and things under the water.
    if (m_waterCopy.w != m_hdr.w || m_waterCopy.h != m_hdr.h || !m_waterCopy.fbo)
        createTarget(m_waterCopy, m_hdr.w, m_hdr.h, hdrFormat(), true);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hdr.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_waterCopy.fbo);
    glBlitFramebuffer(0, 0, m_hdr.w, m_hdr.h, 0, 0, m_hdr.w, m_hdr.h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    bindTarget(m_hdr);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);   // seen from underwater too
    // A pool's water box usually fills it exactly, so its sides lie right on the
    // walls. Push the water a hair further back so the wall always wins there,
    // instead of flickering stripes (z-fighting).
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 8.0f);

    // The ocean swell's wave pictures, if any water here has a swell.
    int oceanBands = 0;
    for (const WaterItem& it : waters)
        if (const WaterSystem::Body* wb = waves.find(it.node->id); wb && wb->swell > 0.0f) {
            FrameProfile::Scope prof("draw: ocean waves", true);
            if (!m_ocean) m_ocean = std::make_unique<OceanGpu>();
            oceanBands = m_ocean->prepare(waves.ocean(), gs.waterQuality >= 2);
            break;
        }

    Shader& w = *m_water;
    w.bind();
    const glm::mat4 view = camera.view(), proj = camera.projection();
    w.setMat4("uView", view);
    w.setMat4("uProj", proj);
    w.setVec2("uTexel", glm::vec2(1.0f / m_hdr.w, 1.0f / m_hdr.h));
    w.setVec3("uViewPos", camera.position());
    const float t = waves.active() ? waves.time() : (float)(now() - m_startTime);
    w.setFloat("uTime", t);
    w.setVec3("uSunDir", env.sunDirection());
    w.setVec3("uSunColor", env.sunColor);
    w.setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
    w.setVec3("uZenith", env.skyZenith);
    w.setVec3("uHorizon", env.skyHorizon);
    w.setVec3("uGround", env.skyGround);
    w.setFloat("uSkyBrightness", env.skyBrightness);
    w.setVec3("uAmbient", env.ambientColor * env.ambientIntensity);
    w.setBool("uReflections", gs.waterReflections());
    w.setBool("uWaterShadows", m_shadowsOn);
    m_shadow.bindForRead(5); w.setInt("uShadowMap", 5);
    w.setMat4("uLightSpace", m_lightSpace);
    w.setFloat("uShadowStrength", env.shadowStrength);
    w.setBool("uFogEnabled", env.fogEnabled);
    w.setVec3("uFogColor", env.fogColor);
    w.setFloat("uFogDensity", env.fogDensity);
    w.setFloat("uFogSunGlow", env.fogSunGlow);
    w.setFloat("uFogHeight", env.fogHeight);
    w.setFloat("uFogFalloff", std::max(0.0f, env.fogFalloff));
    w.setFloat("uRenderDist", m_viewDist);
    bindTex(2, m_waterCopy.color); w.setInt("uScene", 2);
    bindTex(3, m_waterCopy.depth); w.setInt("uSceneDepth", 3);
    // The ocean swell (the same waves the physics floats things on: scene/OceanWaves.h),
    // as pictures the shader reads.
    w.setInt("uOcBands", oceanBands);
    w.setBool("uHasDye", false);
    w.setInt("uOcD0", 8);  w.setInt("uOcD1", 9);  w.setInt("uOcD2", 10);
    w.setInt("uOcS0", 11); w.setInt("uOcS1", 12); w.setInt("uOcS2", 13);
    if (oceanBands) {
        for (int b = 0; b < 3; ++b) {
            const int from = std::min(b, oceanBands - 1);   // (an unused band reads a real picture too)
            bindTex(8 + b, m_ocean->disp(from));
            bindTex(11 + b, m_ocean->slope(from));
        }
        const float n = (float)m_ocean->size();
        w.setVec3("uOcLen", glm::vec3(OceanWaves::kBand[0].length, OceanWaves::kBand[1].length, OceanWaves::kBand[2].length));
        w.setFloat("uOcHalf", 0.5f / n);
        w.setFloat("uOcFoamBlur", std::log2(n / (float)OceanWaves::kCpuN));
        w.setVec3("uOcTexel", glm::vec3(OceanWaves::kBand[0].length, OceanWaves::kBand[1].length, OceanWaves::kBand[2].length) / n);
    }

    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    for (const WaterItem& it : waters) {
        SceneNode* node = it.node;
        const WaterSystem::Body* wb = waves.find(node->id);
        w.setVec3("uColor", node->color);
        w.setBool("uSelected", node->selected);
        float clarity = std::clamp(0.2f + node->transparency, 0.0f, 1.0f);
        if (const Attribute* a = node->findAttribute("Clarity"); a && a->type == Attribute::Number)
            clarity = std::clamp((float)a->n, 0.0f, 1.0f);
        w.setFloat("uClarity", wb ? wb->clarity : clarity);
        if (wb) {
            // While playing: the moving surface (ripples from the physics, the ocean
            // swell added in the vertex shader).
            w.setMat4("uModel", glm::mat4(1.0f));
            w.setMat3("uNormalMat", glm::mat3(1.0f));
            w.setBool("uWaveMesh", true);
            w.setFloat("uWaveAmp", wb->swell);
            w.setFloat("uChop", OceanWaves::chop(wb->swell));
            {   // (how far apart the surface's points are: the same sum as buildWaterMesh)
                const glm::vec3 size = wb->max - wb->min;
                w.setFloat("uCell", std::max({0.5f, size.x / 179.0f, size.z / 179.0f}));
            }
            w.setVec3("uBodyMin", wb->min);
            w.setVec3("uBodyMax", wb->max);
            // The current: a flow map (bent around rocks) if there is one.
            w.setVec2("uFlowBase", glm::vec2(wb->flow.x, wb->flow.z));
            w.setBool("uHasFlowMap", !wb->flowMap.empty());
            if (!wb->flowMap.empty()) {
                FlowTex& ft = m_flowTex[node->id];
                if (!ft.tex) glGenTextures(1, &ft.tex);
                bindTex(6, ft.tex);   // (its own unit: don't disturb the scene pictures on the others)
                if (ft.version != wb->flowVersion) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, wb->nx, wb->nz, 0, GL_RG, GL_FLOAT, wb->flowMap.data());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    ft.version = wb->flowVersion;
                }
                bindTex(6, ft.tex);
                w.setInt("uFlowMap", 6);
                w.setVec3("uFlowGrid", glm::vec3(wb->cell, (float)wb->nx, (float)wb->nz));
            }
            // Colour hanging in the water (blood, oil): a picture of the pool from above.
            // Only made once a pool has had colour in it; after that it's kept up to date
            // (so the last of a cloud fades out, and a clean pool costs nothing).
            {
                auto dt = m_dyeTex.find(node->id);
                const bool had = dt != m_dyeTex.end();
                const bool has = wb->dye.live || had;
                w.setBool("uHasDye", has);
                if (has) {
                    FlowTex& ft = had ? dt->second : m_dyeTex[node->id];
                    const bool fresh = !ft.tex;
                    if (fresh) glGenTextures(1, &ft.tex);
                    bindTex(7, ft.tex);
                    if (fresh || ft.version != wb->dye.version) {
                        // (the grid's own byte list: nothing is allocated here)
                        const std::vector<uint8_t>& px = WaterDye::pixels(const_cast<WaterDye::Grid&>(wb->dye));
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wb->dye.nx, wb->dye.nz, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        ft.version = wb->dye.version;
                    }
                    w.setInt("uDye", 7);
                    w.setVec3("uDyeGrid", glm::vec3(wb->dye.cell, (float)wb->dye.nx, (float)wb->dye.nz));
                }
            }
            auto& mesh = m_waterMeshes[node->id];
            if (!mesh) mesh = std::make_unique<Mesh>();
            buildWaterMesh(waves, *wb, verts, idx);
            mesh->update(verts, idx);
            mesh->draw();
        } else {
            w.setMat4("uModel", it.model);
            w.setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(it.model))));
            w.setBool("uWaveMesh", false);
            w.setFloat("uWaveAmp", 0.0f);
            w.setFloat("uChop", 0.0f);
            w.setBool("uHasFlowMap", false);
            w.setBool("uHasDye", false);
            glm::vec2 fl(0.0f);
            if (const Attribute* a = node->findAttribute("Flow"); a && a->type == Attribute::Vector3) fl = glm::vec2(a->v.x, a->v.z);
            w.setVec2("uFlowBase", fl);
            const AABB box = Physics::worldBounds(node);
            w.setVec3("uBodyMin", box.min);
            w.setVec3("uBodyMax", box.max);
            node->mesh->draw();
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(0.0f, 0.0f);
    glEnable(GL_CULL_FACE);
}

namespace {
// Flowing water: one vertex per square at the water's surface. Dry squares next
// to wet ones dip just under the floor, so the water's edge meets the ground.
bool buildFloodMesh(const WaterSystem::Flood& f, std::vector<Vertex>& verts, std::vector<uint32_t>& idx) {
    verts.clear();
    idx.clear();
    const int n = f.n;
    const float wet = 0.03f;
    std::vector<float> H((size_t)n * n, 0.0f);
    std::vector<char> use((size_t)n * n, 0);
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i) {
            size_t id = f.idx(i, k);
            if (f.depth[id] > wet) { H[id] = f.ground[id] + f.depth[id]; use[id] = 1; continue; }
            float sum = 0.0f; int cnt = 0;
            for (auto [di, dk] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
                int ni = i + di, nk = k + dk;
                if (ni < 0 || nk < 0 || ni >= n || nk >= n) continue;
                size_t nid = f.idx(ni, nk);
                if (f.depth[nid] > wet) { sum += f.ground[nid] + f.depth[nid]; ++cnt; }
            }
            if (!cnt) continue;
            float avg = sum / cnt;
            H[id] = f.ground[id] < -1000.0f ? avg - 0.5f : std::min(f.ground[id] - 0.02f, avg);
            use[id] = 2;
        }
    std::vector<uint32_t> vid((size_t)n * n, UINT32_MAX);
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i) {
            size_t id = f.idx(i, k);
            if (!use[id]) continue;
            float hc = H[id];
            auto side = [&](int ni, int nk) { size_t nid = f.idx(std::clamp(ni, 0, n - 1), std::clamp(nk, 0, n - 1)); return use[nid] ? H[nid] : hc; };
            glm::vec3 nrm(-(side(i + 1, k) - side(i - 1, k)) / (2.0f * f.cell), 1.0f, -(side(i, k + 1) - side(i, k - 1)) / (2.0f * f.cell));
            vid[id] = (uint32_t)verts.size();
            verts.push_back({{f.origin.x + (i + 0.5f) * f.cell, hc, f.origin.y + (k + 0.5f) * f.cell}, glm::normalize(nrm), {0, 0}});
        }
    for (int k = 0; k + 1 < n; ++k)
        for (int i = 0; i + 1 < n; ++i) {
            size_t a = f.idx(i, k), b = f.idx(i + 1, k), c = f.idx(i, k + 1), d = f.idx(i + 1, k + 1);
            if (vid[a] == UINT32_MAX || vid[b] == UINT32_MAX || vid[c] == UINT32_MAX || vid[d] == UINT32_MAX) continue;
            if (use[a] != 1 && use[b] != 1 && use[c] != 1 && use[d] != 1) continue;   // all edge: nothing wet here
            idx.insert(idx.end(), {vid[a], vid[c], vid[b], vid[b], vid[c], vid[d]});
        }
    return !idx.empty();
}

} // namespace

Mesh* SceneRenderer::terrainMesh(Scene& scene) {
    const Terrain& t = scene.terrain();
    if (t.empty()) { m_terrainMesh.reset(); m_terrainVersion = 0; return nullptr; }
    if (m_terrainMesh && m_terrainVersion == t.version()) return m_terrainMesh.get();
    // Two triangles per grid square. Each corner says how much of each ground it
    // is (one of them, fully), and the shader blends between corners.
    const int n = t.cells(), np = n + 1;
    std::vector<Vertex> verts((size_t)np * np);
    std::vector<glm::vec4> blend(verts.size());
    for (int k = 0; k < np; ++k)
        for (int i = 0; i < np; ++i) {
            const size_t p = (size_t)k * np + i;
            const int m = (int)t.material(i, k);
            verts[p] = {t.pointPos(i, k), t.pointNormal(i, k), glm::vec2(m == 4 ? 1.0f : 0.0f, m == 5 ? 1.0f : 0.0f)};
            blend[p] = glm::vec4(m == 0, m == 1, m == 2, m == 3);
        }
    std::vector<uint32_t> idx;
    idx.reserve((size_t)n * n * 6);
    for (int k = 0; k < n; ++k)
        for (int i = 0; i < n; ++i) {
            const uint32_t a = (uint32_t)(k * np + i), b = a + 1, c = a + np, d = c + 1;
            idx.insert(idx.end(), {a, d, b, a, c, d});   // (facing up)
        }
    if (!m_terrainMesh) m_terrainMesh = std::make_unique<Mesh>();
    m_terrainMesh->update(verts, idx);
    m_terrainMesh->setExtra(blend);
    m_terrainVersion = t.version();
    return m_terrainMesh.get();
}

void SceneRenderer::drawGeometry(Scene& scene, const Camera& camera, bool editing) {
    const WaterSystem& waves = scene.water();
    if (!waves.active()) {
        m_waterMeshes.clear();
        for (auto& [id, ft] : m_flowTex) if (ft.tex) glDeleteTextures(1, &ft.tex);
        m_flowTex.clear();
        for (auto& [id, ft] : m_dyeTex) if (ft.tex) glDeleteTextures(1, &ft.tex);
        m_dyeTex.clear();
    }
    struct Item { SceneNode* node; glm::mat4 model; float dist; };
    std::vector<Item> opaque, transparent, shielded, decals;
    std::vector<WaterItem> waters;
    glm::vec3 camPos = camera.position();
    // Parts off to the side of or behind the camera aren't drawn. The ball is a bit
    // bigger than the part, for meshes that poke out of their box.
    const Frustum view(camera.projection() * camera.view());
    auto offScreen = [&](const SceneNode* n) {
        const glm::mat4& m = n->worldMatrix();
        return !view.sees(glm::vec3(m[3]), partRadius(m) * 1.5f + 1.0f);
    };

    // Walk manually so hidden models hide everything inside them, and so
    // everything inside a character with a ForceField gets the glowing shell.
    std::vector<std::pair<SceneNode*, bool>> stack{{scene.root(), false}};
    while (!stack.empty()) {
        auto [node, ff] = stack.back();
        stack.pop_back();
        if (!node->shown()) continue;
        ff = ff || node->hasForceField();
        if (!node->isViewportFrame())   // (the parts in a ViewportFrame are only its picture)
            for (auto& c : node->children) stack.push_back({c.get(), ff});
        if (node->isDecal() && !node->texture.empty() && node->shownTransparency() < 0.99f &&
            node->parent && node->parent->kind == NodeKind::Part)
            if (!tooFar(glm::vec3(node->parent->worldMatrix()[3]), partRadius(node->parent->worldMatrix())) &&
                !offScreen(node->parent))
                decals.push_back({node, decalMatrix(*node), 0.0f});
        if (!node->mesh || node->kind != NodeKind::Part) continue;
        if (tooFar(glm::vec3(node->worldMatrix()[3]), partRadius(node->worldMatrix()))) continue;   // past the render distance
        if (offScreen(node) && !bentByBones(node)) continue;   // not on screen
        if (ff && !node->internal && node->shownTransparency() < 0.99f) shielded.push_back({node, node->worldMatrix(), 0.0f});
        float alpha = 1.0f - node->shownTransparency();
        if (alpha <= 0.001f) continue;   // fully transparent — nothing to draw
        glm::mat4 model = node->worldMatrix();
        float dist = glm::length(glm::vec3(model[3]) - camPos);
        if (Player::isWater(node)) { waters.push_back({node, model}); continue; }   // drawn on their own (drawWater)
        bool see = alpha < 0.999f || node->material == Material::Glass;
        (see ? transparent : opaque).push_back({node, model, dist});
    }
    // X-ray (Studio): everything is drawn see-through, with the see-through things.
    const bool xrayOn = xray || shading == Shading::Wireframe;
    if (xray) {
        transparent.insert(transparent.end(), opaque.begin(), opaque.end());
        opaque.clear();
    }
    // Transparent things are drawn last, far to near, so blending looks right.
    std::sort(transparent.begin(), transparent.end(), [](auto& a, auto& b) { return a.dist > b.dist; });

    std::vector<Vertex> waterVerts;
    std::vector<uint32_t> waterIdx;
    // A part the liquid just ran over, or your character dripping after a swim.
    const Player* me = scene.player();
    const SceneNode* myRoot = me ? scene.findById(me->rootId()) : nullptr;
    auto wetPart = [&](const SceneNode* node) {
        float w = waves.liquid().isWet(node->id) ? 0.85f : 0.0f;
        if (me && me->dripping() > 0.0f && myRoot && myRoot->isAncestorOf(node)) w = std::max(w, me->dripping() * 0.9f);
        return w;
    };
    // MaterialVariants, by name (the first one wins, like Roblox).
    std::map<std::string, const SceneNode*> variants;
    if (SceneNode* ms = scene.serviceFolder("MaterialService", false))
        for (const auto& c : ms->children)
            if (c->isMaterialVariant()) variants.emplace(c->name, c.get());
    // A part's MaterialVariant: its pictures, if the part's Material is the variant's BaseMaterial.
    auto useVariant = [&](const SceneNode* node) {
        if (node->materialVariant.empty() || variants.empty()) return false;
        auto v = variants.find(node->materialVariant);
        if (v == variants.end() || v->second->variant.base != node->material) return false;
        const MaterialVariantProps& p = v->second->variant;
        const std::string* pics[] = {&p.colorMap, &p.normalMap};
        const char* names[] = {"uVColor", "uVNormal"};
        glm::vec4 has(0.0f);
        for (int i = 0; i < 2; ++i) {
            unsigned tex = pics[i]->empty() ? 0 : Textures::get(*pics[i]);
            if (!tex) continue;
            bindTex(9 + i, tex);
            m_lit->setInt(names[i], 9 + i);
            has[i] = 1.0f;
        }
        m_lit->setVec4("uVHas", has);
        // Occlusion, roughness and metalness: one packed picture (the OrmMap, or the
        // RoughnessMap and MetalnessMap packed together the first time they're used).
        glm::vec3 ormHas(0.0f);
        unsigned orm = 0;
        if (!p.ormMap.empty()) {
            orm = Textures::get(p.ormMap);
            if (orm) ormHas = glm::vec3(1.0f);
        } else if (!p.roughnessMap.empty() || !p.metalnessMap.empty()) {
            bool got[3] = {false, false, false};
            orm = Textures::orm("", p.roughnessMap, p.metalnessMap, got);
            if (orm) ormHas = glm::vec3(got[0] ? 1.0f : 0.0f, got[1] ? 1.0f : 0.0f, got[2] ? 1.0f : 0.0f);
        }
        if (orm) {
            bindTex(11, orm);
            m_lit->setInt("uVOrm", 11);
        }
        m_lit->setVec3("uVOrmHas", ormHas);
        m_lit->setFloat("uVTile", p.studsPerTile * 0.5f);   // (a Roblox stud is half a unit)
        m_lit->setVec3("uVParams", glm::vec3(p.roughness, p.metalness, p.glow));
        m_lit->setBool("uVariant", true);
        return true;
    };
    auto draw = [&](const Item& it) {
        SceneNode* node = it.node;
        // Water gets the water look; while playing, its surface is the moving waves.
        const bool water = Player::isWater(node);
        const WaterSystem::Body* wb = water ? waves.find(node->id) : nullptr;
        glm::mat4 model = wb ? glm::mat4(1.0f) : it.model;
        std::shared_ptr<Mesh> renderMesh = node->mesh;
        // Legacy Roblox SpecialMesh instances are stored as child objects with
        // RobloxClass / MeshType / Scale / Offset attributes. Render built-in
        // primitive mesh types on the parent part instead of silently ignoring them.
        // FileMesh asset IDs still require the future asset importer and intentionally
        // fall back to the parent part's current geometry.
        if (!water && node->kind == NodeKind::Part) {
            for (const auto& child : node->children) {
                const SceneNode* sm = child.get();
                const Attribute* cls = sm->findAttribute("RobloxClass");
                if (!cls || cls->type != Attribute::String || cls->s != "SpecialMesh") continue;
                const Attribute* type = sm->findAttribute("MeshType");
                const std::string meshType = type && type->type == Attribute::String ? type->s : "Head";
                if (meshType == "Sphere" || meshType == "Head")
                    renderMesh = MeshLibrary::get(PrimitiveType::Sphere);
                else if (meshType == "Cylinder")
                    renderMesh = MeshLibrary::get(PrimitiveType::Cylinder);
                else if (meshType == "Brick" || meshType == "Torso" || meshType == "Wedge")
                    renderMesh = MeshLibrary::get(PrimitiveType::Cube);
                const Attribute* offset = sm->findAttribute("Offset");
                if (offset && offset->type == Attribute::Vector3)
                    model = glm::translate(model, offset->v);
                const Attribute* scale = sm->findAttribute("Scale");
                if (scale && scale->type == Attribute::Vector3)
                    model = model * glm::scale(glm::mat4(1.0f), scale->v);
                break;
            }
        }
        glm::mat3 nrm = glm::transpose(glm::inverse(glm::mat3(model)));
        m_lit->setMat4("uModel", model);
        m_lit->setMat3("uNormalMat", nrm);
        m_lit->setVec3("uColor", node->color);
        m_lit->setBool("uSelected", node->selected);
        m_lit->setInt("uMaterial", water ? 7 : materialShaderId(node->material));
        m_lit->setFloat("uAlpha", 1.0f - node->shownTransparency());
        m_lit->setFloat("uWetPart", wetPart(node));
        if (water) {
            glDisable(GL_CULL_FACE);   // seen from underwater too
            // A pool's water box usually fills it exactly, so its sides lie right on
            // the walls. Push the water a hair further back so the wall always wins
            // there, instead of flickering stripes (z-fighting).
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 8.0f);
        }
        if (wb) {
            auto& mesh = m_waterMeshes[node->id];
            if (!mesh) mesh = std::make_unique<Mesh>();
            buildWaterMesh(waves, *wb, waterVerts, waterIdx);
            mesh->update(waterVerts, waterIdx);
            mesh->draw();
        } else {
            // Clothing: a shirt / pants picture (the template layout) over the body colour.
            unsigned cloth = !node->texture.empty() && !node->isDecal() ? Textures::get(node->texture) : 0;
            // A character's head: the texture is its face, painted on the front.
            const bool face = cloth && node->name == "Head";
            if (cloth) {
                bindTex(5, cloth);
                m_lit->setInt("uDecal", 5);
                m_lit->setBool(face ? "uFace" : "uClothing", true);
                if (face) m_lit->setFloat("uMouthOpen", LipSync::mouth(node->id));   // lip sync (LipSync.h)
                if (face) m_lit->setFloat("uBlink", Realism::blinkAt(now() - m_startTime, node->id));   // blinking (Realism.h)   // blinking (Realism.h)
            }
            // A T-shirt: a picture on the front of the torso.
            unsigned tee = !node->tshirt.empty() ? Textures::get(node->tshirt) : 0;
            if (tee) {
                bindTex(7, tee);
                m_lit->setInt("uTShirt", 7);
                m_lit->setBool("uHasTShirt", true);
            }
            const bool variant = useVariant(node);
            Mesh* shownMesh = (renderMesh ? renderMesh : node->mesh).get();
            Mesh::lod = lodFor(shownMesh, model);
            shownMesh->draw();
            Mesh::lod = 0;
            if (variant) m_lit->setBool("uVariant", false);
            if (cloth) m_lit->setBool(face ? "uFace" : "uClothing", false);
            if (tee) m_lit->setBool("uHasTShirt", false);
        }
        if (water) {
            glEnable(GL_CULL_FACE);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(0.0f, 0.0f);
        }
    };

    // Many copies of one mesh: one draw for all of them. The rest go one by one.
    auto drawCopies = [&](std::vector<Item>& items, auto& wet, auto& one) {
        // (One group per mesh and detail level: near copies full, far ones simpler.)
        struct MeshLevel {
            const Mesh* mesh; int level;
            bool operator==(const MeshLevel& o) const { return mesh == o.mesh && level == o.level; }
        };
        struct MeshLevelHash {
            size_t operator()(const MeshLevel& k) const { return std::hash<const Mesh*>()(k.mesh) * 31u + (size_t)k.level; }
        };
        std::unordered_map<MeshLevel, std::vector<const Item*>, MeshLevelHash> groups;
        std::vector<const Item*> single;
        static const bool off = std::getenv("GB_NO_INSTANCING") != nullptr;   // (to compare, or if a graphics card mis-draws it)
        for (const Item& it : items) {
            if (!off && Instancing::canCopy(*it.node, it.model, Player::isWater(it.node))) groups[{it.node->mesh.get(), lodFor(it.node->mesh.get(), it.model)}].push_back(&it);
            else single.push_back(&it);
        }
        if (!Mesh::headless && !m_instanceVbo) glGenBuffers(1, &m_instanceVbo);
        std::vector<InstanceData> data;
        int batches = 0;
        for (auto& [key, list] : groups) {
            const Mesh* mesh = key.mesh;
            if ((int)list.size() < Instancing::kMinCopies || !m_instanceVbo) {
                single.insert(single.end(), list.begin(), list.end());
                continue;
            }
            data.clear();
            data.reserve(list.size());
            for (const Item* it : list) {
                const SceneNode* n = it->node;
                data.push_back({it->model, glm::vec4(n->color, 1.0f - n->shownTransparency()),
                                glm::vec4((float)materialShaderId(n->material), n->selected ? 1.0f : 0.0f, wet(n), 0.0f)});
            }
            glBindBuffer(GL_ARRAY_BUFFER, m_instanceVbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(data.size() * sizeof(InstanceData)), data.data(), GL_STREAM_DRAW);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            m_lit->setBool("uInstanced", true);
            Mesh::lod = key.level;
            mesh->drawInstanced(m_instanceVbo, (int)data.size());
            Mesh::lod = 0;
            m_lit->setBool("uInstanced", false);
            ++batches;
        }
        for (const Item* it : single) one(*it);
        m_lastCopies = batches;
    };

    glDisable(GL_BLEND);
    // Wireframe (Studio): only the edges, the back ones too.
    if (shading == Shading::Wireframe) { wireframe(true); glDisable(GL_CULL_FACE); }
    drawCopies(opaque, wetPart, draw);   // parts that share a mesh, in one go each (Instancing.h)
    if (Mesh* tm = terrainMesh(scene)) {
        m_lit->setMat4("uModel", glm::mat4(1.0f));
        m_lit->setMat3("uNormalMat", glm::mat3(1.0f));
        m_lit->setVec3("uColor", glm::vec3(1.0f));
        m_lit->setBool("uSelected", false);
        m_lit->setInt("uMaterial", 9);
        m_lit->setFloat("uAlpha", 1.0f);
        m_lit->setFloat("uWetPart", 0.0f);
        tm->draw();
    }
    if (shading == Shading::Wireframe) { wireframe(false); glEnable(GL_CULL_FACE); }
    if (m_fogVolOn) {
        // The fog over the solid things; what's drawn from here on (see-through parts,
        // water, sparks) fogs itself the simple way.
        { FrameProfile::Scope prof("draw: volumetric fog", true); drawFogVolume(scene, camera); }
        m_lit->bind();
        m_lit->setBool("uFogVolumetric", false);
    }
    drawConstraints(scene, editing);
    if (!waters.empty()) {
        { FrameProfile::Scope prof("draw: water surfaces", true); drawWater(scene, camera, waters); }
        m_lit->bind();
    }

    // --- Particles (blood, oil, gibs, bolts, sparks, fire, smoke) ---
    const auto& parts = scene.particles().items();
    std::vector<const Particle*> fading;
    if (!parts.empty()) {
        auto cube = MeshLibrary::get(PrimitiveType::Cube);
        auto cyl  = MeshLibrary::get(PrimitiveType::Cylinder);
        m_lit->setBool("uSelected", false);
        const float dryTime = scene.particles().dryTime();
        auto drawParticle = [&](const Particle& p, float alpha) {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), p.pos);
            const float speed = glm::length(p.vel);
            if (p.kind == Particle::Drop && speed > 0.5f) {
                // Flying liquid stretches into a streak along the way it's going.
                glm::vec3 y = p.vel / speed;
                glm::vec3 x = glm::normalize(glm::cross(std::abs(y.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0), y));
                glm::vec3 z = glm::cross(x, y);
                m = m * glm::mat4(glm::vec4(x, 0), glm::vec4(y, 0), glm::vec4(z, 0), glm::vec4(0, 0, 0, 1));
                m = glm::scale(m, glm::vec3(p.size.x * 0.8f, p.size.y * std::min(3.5f, 1.0f + speed * 0.12f), p.size.z * 0.8f));
            } else if (p.kind == Particle::Splat) {
                // Lying flat on its surface (up = the surface's normal), turned about it.
                glm::vec3 up = p.normal, x0;
                if (glm::length(p.tangent) > 0.5f) x0 = glm::normalize(p.tangent - up * glm::dot(p.tangent, up));
                else x0 = std::abs(up.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::normalize(glm::cross(glm::vec3(0, 1, 0), up));
                glm::vec3 z0 = glm::cross(x0, up);
                float a = glm::radians(p.rot.y), c = std::cos(a), sn = std::sin(a);
                glm::vec3 x = x0 * c - z0 * sn, z = z0 * c + x0 * sn;
                m = m * glm::mat4(glm::vec4(x, 0), glm::vec4(up, 0), glm::vec4(z, 0), glm::vec4(0, 0, 0, 1));
                m = glm::scale(m, p.size);
            } else {
                m = glm::rotate(m, glm::radians(p.rot.z), {0, 0, 1});
                m = glm::rotate(m, glm::radians(p.rot.y), {0, 1, 0});
                m = glm::rotate(m, glm::radians(p.rot.x), {1, 0, 0});
                m = glm::scale(m, p.size);
            }
            m_lit->setMat4("uModel", m);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
            m_lit->setVec3("uColor", p.color);
            int mat = (p.kind == Particle::Fire || p.kind == Particle::Spark) ? (int)Material::Neon
                    : p.wet ? 8   // wet liquid (see the shader)
                    : p.glossy ? (int)Material::Metal : (int)Material::Plastic;
            if (p.kind == Particle::Chunk && (p.gib == 0 || p.gib == 2 || p.gib == 3)) {   // flesh / organ / intestine-like pieces
                mat = 31;
                m_lit->setBool("uGibSkinOn", p.gib == 0 && p.skin.x >= 0.0f);
                m_lit->setVec3("uGibSkin", glm::max(p.skin, glm::vec3(0.0f)));
                m_lit->setFloat("uGibSeed", p.gibSeed);
            } else if (p.kind == Particle::Splat) {
                // Pools, smears, footprints and stains: wobbly-edged, and drying as they age.
                mat = 30;
                m_lit->setInt("uSplatLook", p.look);
                m_lit->setFloat("uSplatSeed", p.seed);
                m_lit->setFloat("uSplatDry", std::clamp(p.age / dryTime, 0.0f, 1.0f));
                m_lit->setInt("uSplatMark", p.mark);
            }
            m_lit->setInt("uMaterial", mat);
            m_lit->setFloat("uAlpha", alpha);
            (p.kind == Particle::Splat ? cube
             : p.kind == Particle::Bolt || (p.kind == Particle::Drop && p.wet) ||
               (p.kind == Particle::Chunk && (p.gib == 2 || p.gib == 3)) ? cyl : cube)->draw();
        };
        for (const Particle& p : parts) {
            if (tooFar(p.pos, 1.0f)) continue;
            bool fade = p.kind == Particle::Smoke || p.kind == Particle::Spray || (p.kind == Particle::Splat && (p.life < 1.0f || p.alpha < 0.99f));
            if (fade) fading.push_back(&p);
            else      drawParticle(p, 1.0f);
        }
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        for (const Particle* p : fading) {
            float a = p->kind == Particle::Smoke ? 0.55f * std::min(1.0f, p->life / p->maxLife * 2.0f)
                    : p->kind == Particle::Spray ? 0.8f * std::min(1.0f, p->life / p->maxLife * 3.0f)
                                                 : std::max(0.0f, std::min(p->life, p->alpha));
            drawParticle(*p, a);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);

    // --- Explosions: smoke, fire, dust and spray puffs; shockwave bubbles ---
    if (!scene.blasts().puffs().empty() || !scene.blasts().shocks().empty()) {
        drawBlasts(scene, camera);
        m_lit->bind();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }

    // --- Decals: pictures stuck flat on a side of their part ---
    if (!decals.empty()) {
        auto plane = MeshLibrary::get(PrimitiveType::Plane);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -4.0f);          // win the depth fight with the part's own side
        glDisable(GL_CULL_FACE);
        m_lit->setBool("uUseDecal", true);
        m_lit->setInt("uDecal", 5);
        for (auto& it : decals) {
            unsigned tex = Textures::get(it.node->texture);
            if (!tex) continue;
            bindTex(5, tex);
            m_lit->setMat4("uModel", it.model);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(it.model))));
            m_lit->setVec3("uColor", it.node->color);
            m_lit->setBool("uSelected", it.node->selected || it.node->parent->selected);
            m_lit->setInt("uMaterial", (int)Material::Plastic);
            m_lit->setFloat("uAlpha", 1.0f - it.node->shownTransparency());
            plane->draw();
        }
        m_lit->setBool("uUseDecal", false);
        glActiveTexture(GL_TEXTURE0);
        glEnable(GL_CULL_FACE);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    if (xrayOn) {
        // X-ray: faded, and the backs show too (Wireframe: just the lines).
        if (xray) m_lit->setFloat("uXrayFade", 0.65f);
        if (shading == Shading::Wireframe) wireframe(true);
        glDisable(GL_CULL_FACE);
    }
    for (auto& it : transparent) draw(it);
    if (xrayOn) {
        m_lit->setFloat("uXrayFade", 0.0f);
        wireframe(false);
        glEnable(GL_CULL_FACE);
    }

    // Flowing water from WaterSources.
    if (const WaterSystem::Flood* fl = waves.flood()) {
        if (buildFloodMesh(*fl, waterVerts, waterIdx)) {
            if (!m_floodMesh) m_floodMesh = std::make_unique<Mesh>();
            m_floodMesh->update(waterVerts, waterIdx);
            m_lit->setMat4("uModel", glm::mat4(1.0f));
            m_lit->setMat3("uNormalMat", glm::mat3(1.0f));
            m_lit->setVec3("uColor", fl->color);
            m_lit->setBool("uSelected", false);
            m_lit->setInt("uMaterial", 7);
            m_lit->setFloat("uAlpha", 1.0f - fl->transparency);
            glDisable(GL_CULL_FACE);
            m_floodMesh->draw();
            glEnable(GL_CULL_FACE);
        }
    }

    // --- ForceFields: a glowing neon shell cycling through the rainbow ---
    if (!shielded.empty()) {
        float t = (float)(now() - m_startTime);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);          // additive: it glows (and blooms)
        m_lit->setBool("uSelected", false);
        m_lit->setInt("uMaterial", (int)Material::Neon);
        for (size_t i = 0; i < shielded.size(); ++i) {
            const Item& it = shielded[i];
            // Slightly bigger than the part, pulsing a little.
            float grow = 1.12f + 0.03f * std::sin(t * 5.0f + (float)i);
            glm::mat4 m = it.model * glm::scale(glm::mat4(1.0f), glm::vec3(grow));
            float hue = std::fmod(t * 0.35f + (float)i * 0.04f, 1.0f);
            glm::vec3 c = glm::clamp(glm::abs(glm::mod(hue * 6.0f + glm::vec3(0, 4, 2), 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
            m_lit->setMat4("uModel", m);
            m_lit->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(m))));
            m_lit->setVec3("uColor", c);
            m_lit->setFloat("uAlpha", 0.15f + 0.06f * std::sin(t * 7.0f + (float)i * 0.7f));
            it.node->mesh->draw();
        }
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    drawRibbons(scene, camera);
    drawSprites(scene, camera);
    drawStorms(scene, camera);
    drawWeather(scene, camera);
    drawSplashes(scene, camera);
    drawLightning(camera);
    m_lit->bind();
    glDepthMask(GL_TRUE);
}

// ParticleEmitter / Fire / Smoke / Sparkles particles (ParticleSystem::runEmitters has
// already moved them and worked out their looks): camera-facing squares, furthest first.
void SceneRenderer::drawSprites(Scene& scene, const Camera& camera) {
    const ParticleSystem& ps = scene.particles();
    const std::vector<Sprite>& sprites = ps.sprites();
    if (sprites.empty()) return;
    const glm::mat4 view = camera.view();
    std::vector<std::pair<float, const Sprite*>> order;
    order.reserve(sprites.size());
    for (const Sprite& s : sprites) {
        if (s.alpha <= 0.003f || s.size <= 0.0f || tooFar(s.pos, s.size)) continue;
        const float z = (view * glm::vec4(s.pos, 1.0f)).z;
        if (z > s.size) continue;   // behind the camera
        order.push_back({z, &s});
    }
    if (order.empty()) return;
    std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    struct V { glm::vec3 c; glm::vec2 k; glm::vec4 col; glm::vec4 info; float seed; };
    std::vector<V> verts;
    verts.reserve(order.size() * 6);
    const glm::vec2 corners[6] = {{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
    if (!m_spriteVao) {
        glGenVertexArrays(1, &m_spriteVao);
        glGenBuffers(1, &m_spriteVbo);
        glBindVertexArray(m_spriteVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, c));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, k));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, col));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, info));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(V), (void*)offsetof(V, seed));
    }
    const Environment& env = scene.environment();
    const glm::vec3 light = glm::clamp(env.ambientColor * env.ambientIntensity +
                                       env.sunColor * std::max(0.0f, env.sunIntensity) * std::max(env.sunDirection().y, 0.0f),
                                       glm::vec3(0.05f), glm::vec3(1.2f));
    m_sprite->bind();
    m_sprite->setMat4("uView", view);
    m_sprite->setMat4("uProj", camera.projection());
    m_sprite->setVec3("uLight", light);
    m_sprite->setInt("uTex", 5);
    glBindVertexArray(m_spriteVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_spriteVbo);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    // Colours come out already multiplied by their alpha: a glowing one adds its light,
    // a plain one covers what's behind it, and anything between.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // In runs that use the same picture (most use a built-in one: no picture at all).
    size_t i = 0;
    while (i < order.size()) {
        const int tex = order[i].second->tex;
        verts.clear();
        for (; i < order.size() && order[i].second->tex == tex; ++i) {
            const Sprite& s = *order[i].second;
            const glm::vec4 col(s.color, std::clamp(s.alpha, 0.0f, 1.0f));
            const glm::vec4 info(s.size * 0.5f, glm::radians(s.rot), (float)s.look, std::clamp(s.glow, 0.0f, 1.0f));
            for (const glm::vec2& k : corners) verts.push_back({s.pos, k, col, info, s.seed});
        }
        const std::vector<std::string>& texNames = ps.spriteTextures();
        const unsigned t = tex >= 0 && tex < (int)texNames.size() ? Textures::get(texNames[tex]) : 0;
        if (t) bindTex(5, t);
        m_sprite->setBool("uHasTex", t != 0);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(V)), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
}

namespace {
struct RibbonVertex { glm::vec3 pos; glm::vec2 uv; glm::vec4 color; };
} // namespace

void SceneRenderer::ribbonBuffers() {
    if (m_ribbonVao) return;
    glGenVertexArrays(1, &m_ribbonVao);
    glGenBuffers(1, &m_ribbonVbo);
    glBindVertexArray(m_ribbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (void*)offsetof(RibbonVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (void*)offsetof(RibbonVertex, color));
}

// Severe weather you can see (Storms.h): the tornado's funnel hanging from the wall
// cloud with the dust it whips up, and a downburst's column of rain with its foot
// curling out along the ground. Soft see-through columns with wisps winding round them.
void SceneRenderer::drawStorms(Scene& scene, const Camera& camera) {
    const Storms::State& st = scene.storms();
    if (st.tornado <= 0.0f && st.downburst <= 0.0f) return;
    ribbonBuffers();
    const glm::vec3 eye = camera.position();
    const float time = (float)(now() - m_startTime);
    const Environment& env = scene.environment();
    m_ribbon->bind();
    m_ribbon->setMat4("uViewProj", camera.projection() * camera.view());
    m_ribbon->setVec3("uLight", glm::clamp(env.ambientColor * env.ambientIntensity * 1.4f +
                                           env.sunColor * std::max(0.0f, env.sunIntensity) * std::max(env.sunDirection().y, 0.0f) * 0.6f,
                                           glm::vec3(0.04f), glm::vec3(1.6f)));
    m_ribbon->setBool("uHasTex", false);
    m_ribbon->setFloat("uGlow", 0.0f);
    m_ribbon->setFloat("uExtra", 0.0f);
    m_ribbon->setInt("uSoft", 3);
    glBindVertexArray(m_ribbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    std::vector<RibbonVertex> verts;
    // One column: rings of points round a middle line (v = 0 at the ground .. 1 at the
    // top). Its sides fade out where you look along them, so it has no hard outline.
    auto column = [&](const std::function<glm::vec3(float)>& middle, const std::function<float(float)>& radius,
                      const std::function<float(float)>& alpha, const glm::vec3& tint, float v0, float v1,
                      const glm::vec2& flow, bool bothSides) {
        const int rings = 40, around = 28;
        struct Pt { glm::vec3 pos; glm::vec2 uv; float a; bool front; };
        std::vector<Pt> pts((size_t)(rings + 1) * (around + 1));
        for (int i = 0; i <= rings; ++i) {
            const float v = v0 + (v1 - v0) * (float)i / (float)rings;
            const glm::vec3 c = middle(v);
            const float r = radius(v), a = alpha(v);
            for (int j = 0; j <= around; ++j) {
                const float ang = (float)j / (float)around * 6.2831853f;
                const glm::vec3 out(std::cos(ang), 0.0f, std::sin(ang));
                Pt& p = pts[(size_t)i * (around + 1) + j];
                p.pos = c + out * r;
                p.uv = {(float)j / (float)around, v};
                const float facing = glm::dot(out, glm::normalize(eye - p.pos));
                p.front = facing > 0.0f;
                p.a = a * glm::smoothstep(0.0f, 0.55f, std::abs(facing));
            }
        }
        verts.clear();
        for (int i = 0; i < rings; ++i)
            for (int j = 0; j < around; ++j) {
                const Pt &a = pts[(size_t)i * (around + 1) + j], &b = pts[(size_t)i * (around + 1) + j + 1],
                         &c = pts[(size_t)(i + 1) * (around + 1) + j + 1], &d = pts[(size_t)(i + 1) * (around + 1) + j];
                if (!bothSides && !a.front && !b.front && !c.front && !d.front) continue;
                auto vert = [&](const Pt& p) { return RibbonVertex{p.pos, p.uv, glm::vec4(tint, !bothSides && !p.front ? 0.0f : p.a)}; };
                verts.insert(verts.end(), {vert(a), vert(b), vert(c), vert(a), vert(c), vert(d)});
            }
        if (verts.empty()) return;
        m_ribbon->setVec2("uFlow", flow);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(RibbonVertex)), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    };

    if (st.downburst > 0.0f) {
        // The rain shaft: a wide grey column from the cloud, flaring out where it lands.
        const glm::vec3 foot = st.downburstPos;
        const float R = Storms::kDownburstRadius, H = Storms::kCloudBase + 20.0f, k = st.downburst;
        column([&](float v) { return foot + glm::vec3(0.0f, v * H, 0.0f); },
               [&](float v) { return R * (0.8f + 0.25f * v) + R * 1.8f * std::pow(1.0f - v, 7.0f); },
               [&](float v) { return 0.34f * k * glm::smoothstep(0.0f, 0.05f, v) * (0.7f + 0.3f * v); },
               glm::vec3(0.46f, 0.50f, 0.56f), 0.0f, 1.0f, {time * 0.05f, -time * 2.6f}, true);
    }
    if (st.tornado > 0.0f) {
        const float s = st.tornado, core = Storms::coreRadius(s);
        const glm::vec3 foot = st.tornadoPos;
        const float H = Storms::kCloudBase + 15.0f;
        // It comes down out of the wall cloud as it forms, and thins back up as it dies.
        const float down = glm::smoothstep(0.03f, 0.6f, s);
        auto middle = [&](float v) {
            // A real funnel is never straight: it leans and snakes slowly.
            const float sway = v * (1.25f - v) * (9.0f + 5.0f * s);
            return foot + glm::vec3(std::sin(v * 2.4f + time * 0.27f) * sway + v * v * 26.0f, v * H,
                                    std::cos(v * 1.9f + time * 0.21f) * sway);
        };
        column(middle,
               [&](float v) { return core * (0.55f + 0.4f * v) * (0.4f + 0.6f * down) + (core * 3.0f + 22.0f) * std::pow(v, 6.0f); },
               [&](float v) { return 0.95f * glm::smoothstep(1.0f - down, 1.0f - down + 0.1f, v + 0.02f); },
               glm::vec3(0.50f, 0.52f, 0.56f), 0.0f, 1.0f, {time * (1.6f + 0.5f * s), time * 0.9f}, false);
        // The debris cloud: dirt and bits whipped up round its foot once it touches down.
        const float touched = glm::smoothstep(0.75f, 1.0f, down);
        if (touched > 0.0f) {
            const float high = core * 5.0f + 8.0f;
            column([&](float v) { return middle(v * high / H); },
                   [&](float v) { return core * (1.5f + 2.4f * std::sqrt(v)); },
                   [&](float v) { return 0.62f * touched * std::pow(1.0f - v, 1.8f) * glm::smoothstep(0.0f, 0.06f, v); },
                   glm::vec3(0.34f, 0.29f, 0.23f), 0.0f, 1.0f, {time * (2.6f + 0.6f * s), time * 1.5f}, false);
        }
    }
    m_ribbon->setInt("uSoft", 0);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// Rain streaks and snowflakes: thin strips facing the camera, drawn with the
// Trails and Beams shader (lit by the sky, so they're dim at night).
void SceneRenderer::drawWeather(Scene& scene, const Camera& camera) {
    const std::vector<WeatherSim::Streak>& streaks = m_weather.streaks();
    if (streaks.empty()) return;
    ribbonBuffers();
    const bool snow = m_weather.kind() == (int)Weather::Snow;
    const glm::vec3 eye = camera.position();
    const glm::vec3 tint = snow ? glm::vec3(1.0f) : glm::vec3(0.78f, 0.84f, 0.92f);
    std::vector<RibbonVertex> verts;
    verts.reserve(streaks.size() * 6);
    for (const WeatherSim::Streak& s : streaks) {
        const glm::vec3 mid = (s.top + s.bottom) * 0.5f;
        if (tooFar(mid, 1.0f)) continue;
        glm::vec3 along = s.bottom - s.top;
        glm::vec3 side = glm::cross(glm::length(along) > 1e-5f ? along : glm::vec3(0, -1, 0), eye - mid);
        const float len = glm::length(side);
        if (len < 1e-6f) continue;
        side *= s.width * 0.5f / len;
        // Fade out the ones right in front of the lens and the far ones (so the box edge can't be seen).
        const float d = glm::length(mid - eye);
        const float a = s.alpha * glm::clamp((d - 1.5f) / 3.0f, 0.0f, 1.0f) *
                        glm::clamp((WeatherSim::kHalfBox - d) / 6.0f, 0.0f, 1.0f);
        if (a <= 0.003f) continue;
        const glm::vec4 top(tint, snow ? a : a * 0.3f), bottom(tint, a);   // rain: fainter at the tail
        const RibbonVertex v0{s.top - side, {0, 0}, top}, v1{s.top + side, {1, 0}, top},
                           v2{s.bottom + side, {1, 1}, bottom}, v3{s.bottom - side, {0, 1}, bottom};
        verts.insert(verts.end(), {v0, v1, v2, v0, v2, v3});
    }
    if (verts.empty()) return;
    m_ribbon->bind();
    m_ribbon->setMat4("uViewProj", camera.projection() * camera.view());
    // Lit by the sky and sun, like the clouds.
    const Environment& env = scene.environment();
    m_ribbon->setVec3("uLight", glm::clamp(env.ambientColor * env.ambientIntensity * 1.4f +
                                           env.sunColor * std::max(0.0f, env.sunIntensity) * std::max(env.sunDirection().y, 0.0f) * 0.6f,
                                           glm::vec3(0.04f), glm::vec3(1.6f)));
    m_ribbon->setBool("uHasTex", false);
    m_ribbon->setFloat("uGlow", 0.0f);
    m_ribbon->setInt("uSoft", snow ? 0 : 1);   // rain: soft-edged blurred lines
    glBindVertexArray(m_ribbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(RibbonVertex)), verts.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    m_ribbon->setInt("uSoft", 0);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// Where rain hits: a ring of water lying on the ground that spreads and fades, and a
// short spray jumping up from the middle.
void SceneRenderer::drawSplashes(Scene& scene, const Camera& camera) {
    const std::vector<WeatherSim::Splash>& splashes = m_weather.splashes();
    if (splashes.empty() || !m_ribbonVao) return;
    const glm::vec3 eye = camera.position();
    const glm::vec3 tint(0.82f, 0.87f, 0.94f);
    std::vector<RibbonVertex> rings, sprays;
    rings.reserve(splashes.size() * 6);
    sprays.reserve(splashes.size() * 6);
    for (const WeatherSim::Splash& sp : splashes) {
        const float d = glm::length(sp.pos - eye);
        if (d > WeatherSim::kHalfBox || tooFar(sp.pos, 1.0f)) continue;
        const float t = sp.age / WeatherSim::kSplashSeconds;   // 0 just hit .. 1 gone
        const float fade = (1.0f - t) * glm::clamp((WeatherSim::kHalfBox - d) / 6.0f, 0.0f, 1.0f);
        const float r = (0.04f + 0.17f * std::sqrt(t)) * (0.7f + 0.6f * sp.seed);
        const glm::vec3 c = sp.pos + glm::vec3(0.0f, 0.03f, 0.0f);
        const glm::vec4 ringCol(tint, 0.22f * fade);
        const RibbonVertex r0{c + glm::vec3(-r, 0, -r), {0, 0}, ringCol}, r1{c + glm::vec3(r, 0, -r), {1, 0}, ringCol},
                           r2{c + glm::vec3(r, 0, r), {1, 1}, ringCol}, r3{c + glm::vec3(-r, 0, r), {0, 1}, ringCol};
        rings.insert(rings.end(), {r0, r1, r2, r0, r2, r3});
        if (t < 0.6f) {   // the spray: up fast, gone before the ring
            const float up = 0.2f * std::sin(t / 0.6f * 3.14159f) + 0.04f;
            glm::vec3 side = glm::cross(glm::vec3(0, 1, 0), eye - c);
            const float len = glm::length(side);
            if (len < 1e-6f) continue;
            side *= 0.03f / len;
            const glm::vec4 bottom(tint, 0.35f * (1.0f - t / 0.6f)), top(tint, 0.0f);
            const RibbonVertex s0{c - side, {0, 0}, bottom}, s1{c + side, {1, 0}, bottom},
                               s2{c + side + glm::vec3(0, up, 0), {1, 1}, top}, s3{c - side + glm::vec3(0, up, 0), {0, 1}, top};
            sprays.insert(sprays.end(), {s0, s1, s2, s0, s2, s3});
        }
    }
    if (rings.empty()) return;
    const Environment& env = scene.environment();
    m_ribbon->bind();
    m_ribbon->setMat4("uViewProj", camera.projection() * camera.view());
    m_ribbon->setVec3("uLight", glm::clamp(env.ambientColor * env.ambientIntensity * 1.4f +
                                           env.sunColor * std::max(0.0f, env.sunIntensity) * std::max(env.sunDirection().y, 0.0f) * 0.6f,
                                           glm::vec3(0.04f), glm::vec3(1.6f)));
    m_ribbon->setBool("uHasTex", false);
    m_ribbon->setFloat("uGlow", 0.0f);
    glBindVertexArray(m_ribbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_ribbon->setInt("uSoft", 2);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(rings.size() * sizeof(RibbonVertex)), rings.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)rings.size());
    if (!sprays.empty()) {
        m_ribbon->setInt("uSoft", 1);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sprays.size() * sizeof(RibbonVertex)), sprays.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)sprays.size());
    }
    m_ribbon->setInt("uSoft", 0);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// A Storm's lightning bolt, as light: a thin white-hot core, far brighter than
// anything else on screen (so the bloom spreads it, like a camera's glare), inside a
// wide soft blue-violet glow of lit-up air and rain. Branches are thinner and dimmer.
// It flickers with the flash and is gone in half a second.
void SceneRenderer::drawLightning(const Camera& camera) {
    const std::vector<WeatherSim::BoltPiece>& bolt = m_weather.bolt();
    if (bolt.empty() || !m_ribbonVao) return;   // (drawWeather made the strip buffers: a Storm always rains)
    const glm::vec3 eye = camera.position();
    const float glow = m_weather.boltGlow();
    std::vector<RibbonVertex> verts;
    verts.reserve(bolt.size() * 6);
    auto strips = [&](float width, const glm::vec3& rgb, float alpha, float branchFade) {
        verts.clear();
        for (const WeatherSim::BoltPiece& p : bolt) {
            const glm::vec3 mid = (p.a + p.b) * 0.5f;
            glm::vec3 along = p.b - p.a;
            glm::vec3 side = glm::cross(along, eye - mid);
            const float len = glm::length(side);
            if (len < 1e-6f) continue;
            // (Wider the further away it is, so a far bolt is still more than a hair.)
            side *= width * p.width * (1.0f + glm::length(mid - eye) / 140.0f) * 0.5f / len;
            // Pieces overlap a little at the bends, so the line has no gaps.
            along *= 0.06f;
            const glm::vec4 color(rgb, alpha * glow * std::pow(p.bright, branchFade));
            const glm::vec3 a = p.a - along, b = p.b + along;
            const RibbonVertex v0{a - side, {0, 0}, color}, v1{a + side, {1, 0}, color},
                               v2{b + side, {1, 1}, color}, v3{b - side, {0, 1}, color};
            verts.insert(verts.end(), {v0, v1, v2, v0, v2, v3});
        }
        if (verts.empty()) return;
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(RibbonVertex)), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    };
    m_ribbon->bind();
    m_ribbon->setMat4("uViewProj", camera.projection() * camera.view());
    m_ribbon->setVec3("uLight", glm::vec3(1.0f));
    m_ribbon->setBool("uHasTex", false);
    m_ribbon->setFloat("uGlow", 1.0f);
    m_ribbon->setInt("uSoft", 1);
    glBindVertexArray(m_ribbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);   // adds light
    m_ribbon->setFloat("uExtra", 0.0f);
    strips(14.0f, glm::vec3(0.45f, 0.5f, 1.0f), 0.10f, 1.6f);    // lit-up air, wide and faint
    strips(4.0f, glm::vec3(0.62f, 0.7f, 1.0f), 0.35f, 1.2f);     // the glow hugging the channel
    m_ribbon->setFloat("uExtra", 9.0f);
    strips(0.9f, glm::vec3(1.0f, 0.97f, 1.0f), 1.0f, 0.7f);      // the white-hot core
    m_ribbon->setFloat("uExtra", 0.0f);
    m_ribbon->setInt("uSoft", 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

namespace {
// A strip of quads from pairs of edge points (a[i], b[i]).
void addStrip(std::vector<RibbonVertex>& out, const std::vector<RibbonVertex>& a, const std::vector<RibbonVertex>& b) {
    for (size_t i = 1; i < a.size() && i < b.size(); ++i) {
        out.push_back(a[i - 1]); out.push_back(b[i - 1]); out.push_back(b[i]);
        out.push_back(a[i - 1]); out.push_back(b[i]);     out.push_back(a[i]);
    }
}
glm::vec3 axisOf(const SceneNode* att, int i) {   // an Attachment's own axis, in the world
    glm::vec3 v(att->worldMatrix()[i]);
    float l = glm::length(v);
    return l > 1e-6f ? v / l : glm::vec3(i == 0, i == 1, i == 2);
}
} // namespace

// Trails and Beams: ribbons between two Attachments. A Trail remembers where its
// attachments have been for Lifetime seconds; a Beam joins them
// now, bent by CurveSize0/1 along each attachment's axis.
void SceneRenderer::drawRibbons(Scene& scene, const Camera& camera) {
    std::vector<SceneNode*> items;
    scene.forEach([&](SceneNode* n) { if (n->isEffect()) items.push_back(n); });
    const double tNow = now();
    // Forget trails that are gone.
    for (auto it = m_trails.begin(); it != m_trails.end();) {
        if (tNow - it->second.seen > 1.0) it = m_trails.erase(it);
        else ++it;
    }
    if (items.empty()) return;

    if (!m_ribbonVao) {
        glGenVertexArrays(1, &m_ribbonVao);
        glGenBuffers(1, &m_ribbonVbo);
        glBindVertexArray(m_ribbonVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (void*)offsetof(RibbonVertex, uv));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(RibbonVertex), (void*)offsetof(RibbonVertex, color));
    }
    const Environment& env = scene.environment();
    const glm::vec3 light = glm::clamp(env.ambientColor * env.ambientIntensity +
                                       env.sunColor * std::max(0.0f, env.sunIntensity) * std::max(env.sunDirection().y, 0.0f),
                                       glm::vec3(0.05f), glm::vec3(1.2f));
    const glm::vec3 eye = camera.position();
    m_ribbon->bind();
    m_ribbon->setMat4("uViewProj", camera.projection() * camera.view());
    m_ribbon->setVec3("uLight", light);
    m_ribbon->setInt("uTex", 5);
    glBindVertexArray(m_ribbonVao);
    glBindBuffer(GL_ARRAY_BUFFER, m_ribbonVbo);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glDepthMask(GL_FALSE);

    std::vector<RibbonVertex> verts, edgeA, edgeB;
    auto shown = [](const SceneNode* n) {
        for (const SceneNode* p = n; p; p = p->parent) if (!p->shown()) return false;
        return true;
    };
    for (SceneNode* n : items) {
        const EffectProps& e = n->effect;
        SceneNode* a0 = e.a0 ? scene.findById(e.a0) : nullptr;
        SceneNode* a1 = e.a1 ? scene.findById(e.a1) : nullptr;
        const bool attached = a0 && a1 && a0->isAttachment() && a1->isAttachment();
        edgeA.clear(); edgeB.clear(); verts.clear();
        auto edge = [&](glm::vec3 c, glm::vec3 half, float u, float t) {
            const glm::vec4 col(sampleSequence(e.color, t), 1.0f - sampleSequence(e.transparency, t));
            edgeA.push_back({c - half, {u, 0.0f}, col});
            edgeB.push_back({c + half, {u, 1.0f}, col});
        };

        if (n->kind == NodeKind::Trail) {
            if (m_viewDist > 0.0f && attached && tooFar(glm::vec3(a0->worldMatrix()[3]), 0.0f)) continue;
            TrailState& st = m_trails[n->id];
            st.seen = tNow;
            if (st.clears != e.clears) { st.points.clear(); st.clears = e.clears; }   // trail:Clear()
            const bool live = attached && n->enabled && shown(n);
            glm::vec3 ha, hb;
            if (live) {
                ha = glm::vec3(a0->worldMatrix()[3]);
                hb = glm::vec3(a1->worldMatrix()[3]);
                const glm::vec3 mid = (ha + hb) * 0.5f;
                if (st.points.empty() || glm::length(mid - (st.points.back().a + st.points.back().b) * 0.5f) >= std::max(0.01f, e.minLength))
                    st.points.push_back({ha, hb, tNow});
            }
            const double life = std::max(0.01f, e.lifetime);
            st.points.erase(std::remove_if(st.points.begin(), st.points.end(), [&](const TrailPoint& p) { return tNow - p.time > life; }),
                            st.points.end());
            if (st.points.size() > 2000) st.points.erase(st.points.begin(), st.points.begin() + (st.points.size() - 2000));
            // Newest first: the attachments now, then where they were.
            std::vector<TrailPoint> pts;
            if (live) pts.push_back({ha, hb, tNow});
            for (auto it = st.points.rbegin(); it != st.points.rend(); ++it) pts.push_back(*it);
            if (e.maxLength > 0.0f) {   // no longer than MaxLength
                float len = 0.0f;
                for (size_t i = 1; i < pts.size(); ++i) {
                    len += glm::length((pts[i].a + pts[i].b) * 0.5f - (pts[i - 1].a + pts[i - 1].b) * 0.5f);
                    if (len > e.maxLength) { pts.resize(i + 1); break; }
                }
            }
            if (pts.size() < 2) continue;
            for (size_t i = 0; i < pts.size(); ++i) {
                const float age = (float)std::clamp((tNow - pts[i].time) / life, 0.0, 1.0);
                const glm::vec3 c = (pts[i].a + pts[i].b) * 0.5f;
                glm::vec3 half = (pts[i].b - pts[i].a) * 0.5f;
                if (e.faceCamera) {
                    const glm::vec3 along = i + 1 < pts.size() ? (pts[i + 1].a + pts[i + 1].b) * 0.5f - c : c - (pts[i - 1].a + pts[i - 1].b) * 0.5f;
                    glm::vec3 side = glm::cross(along, eye - c);
                    const float l = glm::length(side);
                    if (l > 1e-6f) half = side / l * glm::length(half);
                }
                edge(c, half * sampleSequence(e.widthScale, age), age, age);
            }
        } else {   // Beam
            if (!attached || !n->enabled || !shown(n)) continue;
            const glm::vec3 p0(a0->worldMatrix()[3]), p3(a1->worldMatrix()[3]);
            if (tooFar((p0 + p3) * 0.5f, glm::length(p3 - p0) * 0.5f + std::max(e.width0, e.width1))) continue;
            const glm::vec3 p1 = p0 + axisOf(a0, 0) * e.curve0, p2 = p3 - axisOf(a1, 0) * e.curve1;
            const glm::vec3 z0 = axisOf(a0, 2), z1 = axisOf(a1, 2);
            const int segs = std::clamp(e.segments, 1, 1000);
            std::vector<glm::vec3> pos(segs + 1);
            for (int i = 0; i <= segs; ++i) {
                const float t = (float)i / segs, u = 1.0f - t;
                pos[i] = u * u * u * p0 + 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t * p3;
            }
            float dist = 0.0f;
            const float total = [&] { float l = 0.0f; for (int i = 1; i <= segs; ++i) l += glm::length(pos[i] - pos[i - 1]); return l; }();
            const float scroll = (float)(tNow - m_startTime) * e.textureSpeed;
            for (int i = 0; i <= segs; ++i) {
                const float t = (float)i / segs;
                if (i) dist += glm::length(pos[i] - pos[i - 1]);
                glm::vec3 tangent = pos[std::min(i + 1, segs)] - pos[std::max(i - 1, 0)];
                const float tl = glm::length(tangent);
                tangent = tl > 1e-6f ? tangent / tl : glm::vec3(1, 0, 0);
                glm::vec3 side = e.faceCamera ? glm::cross(tangent, eye - pos[i]) : glm::mix(z0, z1, t);
                side -= tangent * glm::dot(side, tangent);
                if (glm::length(side) < 1e-5f) side = glm::cross(tangent, std::abs(tangent.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
                side = glm::normalize(side);
                const float u = (e.textureWrap ? dist / e.textureLength : (total > 0.0f ? dist / total : t) * e.textureLength) - scroll;
                edge(pos[i], side * glm::mix(e.width0, e.width1, t) * 0.5f, u, t);
            }
        }

        addStrip(verts, edgeA, edgeB);
        if (verts.empty()) continue;
        const unsigned tex = e.texture.empty() ? 0 : Textures::get(e.texture);
        if (tex) {
            bindTex(5, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);   // (beams scroll their picture)
        }
        m_ribbon->setBool("uHasTex", tex != 0);
        m_ribbon->setFloat("uGlow", e.lightEmission);
        glBlendFunc(GL_SRC_ALPHA, e.lightEmission >= 0.5f ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(verts.size() * sizeof(RibbonVertex)), verts.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)verts.size());
    }
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
}

void SceneRenderer::renderLiquid(Scene& scene, const Camera& camera) {
    const Liquid& liquid = scene.water().liquid();
    const std::vector<glm::vec4>& drops = liquid.drawList();
    // With the physics on the graphics card the drops never leave it: they're
    // drawn straight from its buffer, as many as it says (an indirect draw).
#if defined(GB_IOS) || defined(__EMSCRIPTEN__)
    // OpenGL ES on iOS and WebGL 2 lack the indirect draw entry points used by the desktop GPU path.
    const bool onGpu = false;
#else
    const bool onGpu = liquid.onGpu() && liquid.gpuDrawBuffer();
#endif
    if (onGpu ? liquid.count() == 0 : drops.empty()) return;
    const Environment& env = scene.environment();
    const int w = m_hdr.w, h = m_hdr.h;
    if (!m_fluidVao) {
        glGenVertexArrays(1, &m_fluidVao);
        glGenBuffers(1, &m_fluidVbo);
        glBindVertexArray(m_fluidVao);
        glBindBuffer(GL_ARRAY_BUFFER, m_fluidVbo);
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }
    glBindVertexArray(m_fluidVao);
    if (onGpu) {
        glBindBuffer(GL_ARRAY_BUFFER, liquid.gpuDrawBuffer());
#if !defined(GB_IOS) && !defined(__EMSCRIPTEN__)
        glBindBuffer(GL_DRAW_INDIRECT_BUFFER, liquid.gpuCommandBuffer());
#endif
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, m_fluidVbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(drops.size() * sizeof(glm::vec4)), drops.data(), GL_STREAM_DRAW);
    }
    // Two vec4s a drop: (position, w) and (shape axis, flatness).
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec4), nullptr);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 2 * sizeof(glm::vec4), (const void*)sizeof(glm::vec4));
    auto drawDrops = [&]() {
#if !defined(GB_IOS) && !defined(__EMSCRIPTEN__)
        if (onGpu) glDrawArraysIndirect(GL_POINTS, (const void*)(uintptr_t)Liquid::gpuDrawCommandOffset());
        else
#endif
            glDrawArrays(GL_POINTS, 0, (GLsizei)(drops.size() / 2));
    };
    auto done = [&]() {
#if !defined(GB_IOS) && !defined(__EMSCRIPTEN__)
        if (onGpu) glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#endif
        glBindVertexArray(0);
    };
#ifndef GB_GLES
    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
    const glm::mat4 view = camera.view(), proj = camera.projection();
    const float pointScale = proj[1][1] * 0.5f * (float)h;
    const float radius = Liquid::kRadius * 1.25f;   // drawn a bit fatter so they join up
    const glm::vec3 ambient = env.ambientColor * env.ambientIntensity;
    // Each kind of liquid's colour (FluidSystem.Color; number 0 is plain water).
    glm::vec3 palette[Liquid::kMaxFluids];
    for (int k = 0; k < Liquid::kMaxFluids; ++k) palette[k] = glm::vec3(0.12f, 0.42f, 0.62f);
    const auto& fluids = liquid.fluids();
    for (size_t k = 0; k < fluids.size() && k < (size_t)Liquid::kMaxFluids; ++k) palette[k] = fluids[k].color;
    const bool tinted = fluids.size() > 1;
    auto setDrops = [&](Shader& s) {
        s.bind();
        s.setMat4("uView", view);
        s.setMat4("uProj", proj);
        s.setFloat("uPointScale", pointScale);
        s.setFloat("uRenderDist", m_viewDist);
        s.setFloat("uRadius", radius);
        s.setVec3Array("uFluidColor", palette, Liquid::kMaxFluids);
    };

    // Without float pictures (some phones): shiny balls straight into the scene.
    if (hdrFormat() != (GLenum)GL_RGBA16F) {
        bindTarget(m_hdr);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        setDrops(*m_fluidSimple);
        m_fluidSimple->setVec3("uLightDir", glm::normalize(glm::vec3(view * glm::vec4(env.sunDirection(), 0.0f))));
        m_fluidSimple->setVec3("uAmbient", ambient);
        drawDrops();
        glDisable(GL_BLEND);
        done();
        return;
    }

    // Water Quality: lower draws the liquid at a lower resolution (then smooths it),
    // which is much faster; higher smooths it more.
    const GraphicsSettings& gq = GraphicsSettings::get();
    const int fw = std::max(1, (int)std::lround(w * gq.waterScale())), fh = std::max(1, (int)std::lround(h * gq.waterScale()));
    if (m_fDepth.w != fw || m_fDepth.h != fh || m_sceneCopy.w != w || m_sceneCopy.h != h || !m_fDepth.fbo) {
#ifdef GB_GLES
        const GLenum depthFmt = GL_RGBA16F;
#else
        const GLenum depthFmt = GL_R32F;
#endif
        createTarget(m_fDepth, fw, fh, depthFmt, true);
        createTarget(m_fTmp, fw, fh, depthFmt, false);
        // How much liquid (and its colour) changes slowly across the screen, so those
        // two are worked out at half size: a quarter of the work for the busiest pass
        // (every drop adds itself in, even the ones hidden behind others).
        createTarget(m_fThick, (fw + 1) / 2, (fh + 1) / 2, GL_RGBA16F, false);
        createTarget(m_fColor, (fw + 1) / 2, (fh + 1) / 2, GL_RGBA16F, false);
        createTarget(m_sceneCopy, w, h, hdrFormat(), true);   // colour and depth of what's behind
    }
    const glm::vec2 texel(1.0f / fw, 1.0f / fh);
    const float fluidScale = pointScale * (float)fh / (float)h;   // drop sizes in the liquid's (maybe smaller) pictures
    auto setDropsSmall = [&](Shader& s) { setDrops(s); s.setFloat("uPointScale", fluidScale); };
    const glm::vec2 halfTexel(1.0f / m_fThick.w, 1.0f / m_fThick.h);
    const float halfScale = fluidScale * (float)m_fThick.h / (float)fh;

    // 1. Nearest liquid in each pixel.
    bindTarget(m_fDepth);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    setDropsSmall(*m_fluidDepth);
    bindTex(0, m_hdr.depth);
    m_fluidDepth->setInt("uSceneDepth", 0);
    m_fluidDepth->setVec2("uTexel", texel);
    drawDrops();

    // 2. How much liquid is along each pixel.
    bindTarget(m_fThick);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    setDrops(*m_fluidThick);
    m_fluidThick->setFloat("uPointScale", halfScale);
    bindTex(0, m_hdr.depth);
    m_fluidThick->setInt("uSceneDepth", 0);
    m_fluidThick->setVec2("uTexel", halfTexel);
    drawDrops();
    // 2b. With more than one kind of liquid: which colour, where.
    if (tinted) {
        bindTarget(m_fColor);
        glClear(GL_COLOR_BUFFER_BIT);
        setDrops(*m_fluidColor);
        m_fluidColor->setFloat("uPointScale", halfScale);
        bindTex(0, m_hdr.depth);
        m_fluidColor->setInt("uSceneDepth", 0);
        m_fluidColor->setVec2("uTexel", halfTexel);
        drawDrops();
    }
    glDisable(GL_BLEND);

    // 3. Smooth the surface (twice across, twice down).
#if !defined(GB_IOS) && !defined(__EMSCRIPTEN__)
    if (onGpu) glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#endif
    glBindVertexArray(m_emptyVao);
    m_fluidBlur->bind();
    m_fluidBlur->setInt("uSrc", 0);
    m_fluidBlur->setFloat("uRange", 0.45f);   // studs: bigger depth jumps are edges
    m_fluidBlur->setFloat("uPointScale", fluidScale);
    m_fluidBlur->setVec2("uTexel", texel);
    float step = 0.06f;   // studs; doubles every pass (4 passes reach ~1.8 studs)
    for (int pass = 0; pass < gq.waterBlurPasses(); ++pass) {
        bindTarget(m_fTmp);
        bindTex(0, m_fDepth.color);
        m_fluidBlur->setFloat("uStep", step);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        step *= 2.0f;
        glBindFramebuffer(GL_FRAMEBUFFER, m_fDepth.fbo);
        glViewport(0, 0, fw, fh);
        bindTex(0, m_fTmp.color);
        m_fluidBlur->setFloat("uStep", step);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        step *= 2.0f;
    }

    // 4. The scene behind the water (and how far away it is), then the lit surface on top of it.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hdr.fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneCopy.fbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    bindTarget(m_hdr);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);   // (the depth pass already kept it in front of solid things)
    glDepthMask(GL_TRUE);
    m_fluidShade->bind();
    bindTex(0, m_fDepth.color);  m_fluidShade->setInt("uDepth", 0);
    bindTex(1, m_fThick.color);  m_fluidShade->setInt("uThick", 1);
    bindTex(2, m_sceneCopy.color); m_fluidShade->setInt("uScene", 2);
    bindTex(3, m_sceneCopy.depth); m_fluidShade->setInt("uSceneDepth", 3);
    m_fluidShade->setMat4("uProj", proj);
    m_fluidShade->setMat4("uInvView", glm::inverse(view));
    m_fluidShade->setVec2("uTexel", texel);
    m_fluidShade->setVec3("uSunDir", env.sunDirection());
    m_fluidShade->setVec3("uSunColor", env.sunColor);
    m_fluidShade->setFloat("uSunIntensity", std::max(0.0f, env.sunIntensity));
    m_fluidShade->setVec3("uZenith", env.skyZenith);
    m_fluidShade->setVec3("uHorizon", env.skyHorizon);
    m_fluidShade->setVec3("uGround", env.skyGround);
    m_fluidShade->setFloat("uSkyBrightness", env.skyBrightness);
    m_fluidShade->setVec3("uAmbient", ambient);
    m_fluidShade->setBool("uTinted", tinted);
    m_fluidShade->setVec3("uTint", palette[0]);
    bindTex(4, m_fColor.color); m_fluidShade->setInt("uColorTex", 4);
    m_fluidShade->setBool("uWaterShadows", m_shadowsOn);
    m_shadow.bindForRead(5); m_fluidShade->setInt("uShadowMap", 5);
    m_fluidShade->setMat4("uLightSpace", m_lightSpace);
    m_fluidShade->setFloat("uShadowStrength", env.shadowStrength);
    m_fluidShade->setBool("uReflections", gq.waterReflections());
    m_fluidShade->setFloat("uTime", (float)(now() - m_startTime));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthFunc(GL_LESS);
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
}

// Eyes adjusting: the average brightness a normal sunny scene has (the composite brightens
// pictures darker than this and dims brighter ones, gently).
constexpr float kLumKey = 0.42f;

void SceneRenderer::postProcess(Scene& scene, const Camera& camera, Framebuffer& target) {
    GraphicsSettings& gs = GraphicsSettings::get();
    const Environment& env = scene.environment();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(m_emptyVao);

    // --- Temporal upscaler: this frame on top of the ones before, at the screen's size ---
    unsigned upscaled = 0;
    { FrameProfile::Scope prof("draw: upscaler", true); upscaled = resolveTemporal(camera); }
    const int sceneW = upscaled ? target.width() : m_hdr.w, sceneH = upscaled ? target.height() : m_hdr.h;

    // --- Ambient occlusion (GTAO, see Gtao.h): dark corners and creases from the depth
    //     picture, then a small blur that takes its noise out. With traced bounce light
    //     the lit shader already darkened the big corners, so it only adds the fine ones. ---
    const Gtao::Level ao = Gtao::levelFor(gs.giQuality, m_usedBounce);
    bool useAO = ao.slices > 0 && env.aoIntensity > 0.0f;
    if (useAO) {
        FrameProfile::Scope prof("draw: ambient occlusion", true);
        const glm::mat4 proj = camera.projection(), invProj = glm::inverse(proj);
        bindTarget(m_aoRaw);
        m_gtao->bind();
        bindTex(0, m_hdr.depth);
        m_gtao->setInt("uDepth", 0);
        m_gtao->setMat4("uProj", proj);
        m_gtao->setMat4("uInvProj", invProj);
        m_gtao->setVec2("uTexel", glm::vec2(1.0f / m_hdr.w, 1.0f / m_hdr.h));
        m_gtao->setFloat("uRadius", ao.radius);
        m_gtao->setInt("uSlices", ao.slices);
        m_gtao->setInt("uSteps", ao.steps);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        bindTarget(m_ao);
        m_gtaoBlur->bind();
        bindTex(0, m_aoRaw.color);
        bindTex(1, m_hdr.depth);
        m_gtaoBlur->setInt("uAO", 0);
        m_gtaoBlur->setInt("uDepth", 1);
        m_gtaoBlur->setMat4("uInvProj", invProj);
        m_gtaoBlur->setVec2("uTexel", glm::vec2(1.0f / m_aoRaw.w, 1.0f / m_aoRaw.h));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // --- Bloom: bright-pass, then a blur pyramid down and back up. A BloomEffect in
    //     Lighting / the Camera takes over from the Lighting panel's bloom: its Size says
    //     how far it spreads (how many levels of the pyramid). Off when bloom is off in
    //     the graphics settings (Low, so phones on Low skip it). ---
    const float bloomIntensity = m_fx.bloom ? m_fx.bloomIntensity * 2.5f : env.bloomIntensity;
    const float bloomThreshold = m_fx.bloom ? m_fx.bloomThreshold : env.bloomThreshold;
    const int bloomLevels = m_fx.bloom ? std::clamp(1 + (int)std::lround(m_fx.bloomSize / 56.0f * (kBloomLevels - 1)), 1, kBloomLevels)
                                       : kBloomLevels;
    bool useBloom = gs.bloom && bloomIntensity > 0.0f;
    if (useBloom) {
        bindTarget(m_bloom[0]);
        m_bloomPre->bind();
        bindTex(0, m_hdr.color);
        m_bloomPre->setInt("uSrc", 0);
        m_bloomPre->setFloat("uThreshold", bloomThreshold);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        m_bloomDown->bind();
        m_bloomDown->setInt("uSrc", 0);
        for (int i = 1; i < bloomLevels; ++i) {
            bindTarget(m_bloom[i]);
            bindTex(0, m_bloom[i - 1].color);
            m_bloomDown->setVec2("uTexel", glm::vec2(1.0f / m_bloom[i - 1].w, 1.0f / m_bloom[i - 1].h));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        m_bloomUp->bind();
        m_bloomUp->setInt("uSrc", 0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        for (int i = bloomLevels - 1; i > 0; --i) {
            bindTarget(m_bloom[i - 1]);
            bindTex(0, m_bloom[i].color);
            m_bloomUp->setVec2("uTexel", glm::vec2(1.0f / m_bloom[i].w, 1.0f / m_bloom[i].h));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    // --- BlurEffect / DepthOfFieldEffect: blurrier and blurrier copies of the picture
    //     (each half the size of the last, smoothed back up one size, like the UI's blur),
    //     mixed in by the composite. A handful of tiny passes, so phones keep it. Depth of
    //     field is skipped on Low graphics. ---
    const bool lowEnd = gs.lowEnd();
    const bool useDof = m_fx.dof && !lowEnd && gs.postQuality >= 2;
    // BlurEffect.Size in pixels -> which copy: each is about twice as blurry as the last.
    const float blurLevel = m_fx.blur > 0.5f ? std::clamp(std::log2(m_fx.blur / 1.5f), 0.0f, 4.0f) * (m_fx.blur < 3.0f ? m_fx.blur / 3.0f : 1.0f) : 0.0f;
    const bool useBlur = blurLevel > 0.0f || useDof;
    if (useBlur) {
        if (m_blurDown[0].w != std::max(1, m_hdr.w / 2) || m_blurDown[0].h != std::max(1, m_hdr.h / 2) || !m_blurDown[0].fbo) {
            int bw = m_hdr.w / 2, bh = m_hdr.h / 2;
            for (int i = 0; i < kBlurLevels; ++i) {
                createTarget(m_blurDown[i], bw, bh, hdrFormat(), false);
                if (i < kBlurLevels - 1) createTarget(m_blurUp[i], bw, bh, hdrFormat(), false);
                bw = std::max(1, bw / 2);
                bh = std::max(1, bh / 2);
            }
        }
        m_bloomDown->bind();
        m_bloomDown->setInt("uSrc", 0);
        unsigned src = m_hdr.color;
        int sw = m_hdr.w, sh = m_hdr.h;
        for (int i = 0; i < kBlurLevels; ++i) {
            bindTarget(m_blurDown[i]);
            bindTex(0, src);
            m_bloomDown->setVec2("uTexel", glm::vec2(1.0f / sw, 1.0f / sh));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            src = m_blurDown[i].color;
            sw = m_blurDown[i].w;
            sh = m_blurDown[i].h;
        }
        m_bloomUp->bind();
        m_bloomUp->setInt("uSrc", 0);
        for (int i = 0; i < kBlurLevels - 1; ++i) {
            bindTarget(m_blurUp[i]);
            bindTex(0, m_blurDown[i + 1].color);
            m_bloomUp->setVec2("uTexel", glm::vec2(1.0f / m_blurDown[i + 1].w, 1.0f / m_blurDown[i + 1].h));
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    // --- SunRaysEffect: streaks from the sun, worked out at a quarter of the size. Phones
    //     take half as many steps; Low graphics skips it. ---
    glm::vec3 raysColor(0.0f);
    bool useRays = false;
    if (m_fx.rays > 0.0f && !lowEnd && gs.postQuality >= 2 && env.sunElevation > -2.0f && !camera.orthographic) {
        const glm::vec4 clip = camera.projection() * camera.view() * glm::vec4(camera.position() + env.sunDirection() * 1000.0f, 1.0f);
        const float facing = glm::dot(camera.forward(), env.sunDirection());
        if (clip.w > 0.0f && facing > 0.05f) {
            const glm::vec2 sun = glm::vec2(clip) / clip.w * 0.5f + 0.5f;
            const int rw = std::max(1, m_hdr.w / 4), rh = std::max(1, m_hdr.h / 4);
            if (m_rays.w != rw || m_rays.h != rh || !m_rays.fbo) createTarget(m_rays, rw, rh, hdrFormat(), false);
            bindTarget(m_rays);
            m_sunRays->bind();
            bindTex(0, m_hdr.color);
            bindTex(1, m_hdr.depth);
            m_sunRays->setInt("uHdr", 0);
            m_sunRays->setInt("uDepth", 1);
            m_sunRays->setVec2("uSun", sun);
            m_sunRays->setFloat("uSpread", m_fx.raysSpread);
            m_sunRays->setFloat("uStrength", 1.0f);
            m_sunRays->setFloat("uAspect", (float)m_hdr.w / (float)std::max(1, m_hdr.h));
#ifdef GB_GLES
            m_sunRays->setInt("uSamples", 16);
#else
            m_sunRays->setInt("uSamples", gs.postQuality >= 4 ? 64 : gs.postQuality >= 3 ? 48 : 32);
#endif
            glDrawArrays(GL_TRIANGLES, 0, 3);
            // Fades as the sun leaves the view; tinted like the sunlight.
            const float edge = glm::clamp((facing - 0.05f) / 0.35f, 0.0f, 1.0f);
            raysColor = glm::pow(env.sunColor, glm::vec3(2.2f)) * m_fx.rays * edge * 4.0f;
            useRays = true;
        }
    }

    // --- Motion blur (Lighting.MotionBlur): smear along how far each pixel moved since
    //     last frame. Skipped on Low graphics and in orthographic views. ---
    const glm::mat4 viewProj = camera.projection() * camera.view();
    unsigned sceneColor = upscaled ? upscaled : m_hdr.color;
    if (env.motionBlur > 0.0f && m_hasPrev && gs.postQuality >= 2 && !lowEnd && !camera.orthographic) {
        if (m_blurred.w != sceneW || m_blurred.h != sceneH || !m_blurred.fbo) createTarget(m_blurred, sceneW, sceneH, hdrFormat(), false);
        bindTarget(m_blurred);
        m_motionBlur->bind();
        bindTex(0, sceneColor);
        bindTex(1, m_hdr.depth);
        m_motionBlur->setInt("uHdr", 0);
        m_motionBlur->setInt("uDepth", 1);
        m_motionBlur->setMat4("uInvViewProj", glm::inverse(viewProj));
        m_motionBlur->setMat4("uPrevViewProj", m_prevViewProj);
        m_motionBlur->setFloat("uAmount", env.motionBlur);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        sceneColor = m_blurred.color;
    }
    m_prevViewProj = viewProj;
    m_hasPrev = true;

    // --- Eyes adjusting (Lighting.AutoExposure): the average brightness, eased toward over
    //     a couple of seconds into a 1x1 picture the composite reads. No reading back. ---
    const bool autoExp = env.autoExposure && gs.postFx;
    if (autoExp) {
        if (!m_lum.fbo) { createTarget(m_lum, 1, 1, GL_RGBA16F, false); m_lumFresh = true; }
        const double t = now();
        const float ldt = m_lumClock > 0.0 ? (float)std::min(0.5, t - m_lumClock) : 0.0f;
        m_lumClock = t;
        bindTarget(m_lum);
        m_lumShader->bind();
        bindTex(0, m_hdr.color);
        m_lumShader->setInt("uHdr", 0);
        m_lumShader->setFloat("uRate", m_lumFresh ? 1.0f : 1.0f - std::exp(-ldt * 1.2f));
        m_lumFresh = false;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glDisable(GL_BLEND);
    } else {
        m_lumFresh = true;
    }

    // --- Composite: tone mapping + grading, then optional FXAA ---
    bool useFxaa = gs.aaQuality == 0 && !upscaled;
    if (useFxaa) bindTarget(m_ldr);
    else         target.bind();
    m_composite->bind();
    bindTex(0, sceneColor);
    bindTex(1, m_bloom[0].color);
    bindTex(2, m_ao.color);
    m_composite->setInt("uHdr", 0);
    m_composite->setInt("uBloom", 1);
    m_composite->setInt("uAO", 2);
    m_composite->setBool("uUseBloom", useBloom);
    m_composite->setBool("uUseAO", useAO);
    m_composite->setBool("uPost", gs.postFx);
    m_composite->setBool("uLumaAlpha", useFxaa);
    m_composite->setFloat("uSharpen", upscaleSharpen());
    m_composite->setVec2("uAOTexel", glm::vec2(0.33f / m_ao.w, 0.33f / m_ao.h));   // (already blurred: just a touch)
    m_composite->setFloat("uExposure", env.exposure);
    m_composite->setBool("uAutoExp", autoExp);
    if (autoExp) {
        bindTex(9, m_lum.color);
        m_composite->setInt("uLum", 9);
        m_composite->setFloat("uLumKey", kLumKey);
    }
    m_composite->setFloat("uBloomIntensity", bloomIntensity);
    m_composite->setFloat("uAOIntensity", env.aoIntensity * ao.strength);
    m_composite->setFloat("uContrast", env.contrast);
    m_composite->setFloat("uSaturation", env.saturation);
    m_composite->setFloat("uVignette", env.vignette);
    m_composite->setVec3("uTint", env.tint);
    // Screen effects.
    m_composite->setBool("uBlurOn", useBlur);
    m_composite->setBool("uDof", useDof);
    m_composite->setBool("uRaysOn", useRays);
    m_composite->setBool("uGrade", m_fx.grade);
    m_composite->setInt("uColorBlind", std::clamp(gs.colorBlind, 0, 3));   // Settings > Accessibility
    if (useBlur) {
        static const char* const names[4] = {"uBlur1", "uBlur2", "uBlur3", "uBlur4"};
        for (int i = 0; i < 4; ++i) { bindTex(4 + i, m_blurUp[i].color); m_composite->setInt(names[i], 4 + i); }
        m_composite->setFloat("uBlurLevel", blurLevel);
        m_composite->setVec4("uDofParams", glm::vec4(m_fx.focus, m_fx.inFocus, m_fx.nearAmt, m_fx.farAmt));
        if (useDof) {
            const glm::mat4 proj = camera.projection();
            bindTex(3, m_hdr.depth);
            m_composite->setInt("uDepth", 3);
            m_composite->setVec2("uDepthParams", glm::vec2(proj[3][2], proj[2][2]));
            m_composite->setBool("uOrthoDepth", camera.orthographic);
        }
    }
    if (useRays) {
        bindTex(8, m_rays.color);
        m_composite->setInt("uRays", 8);
        m_composite->setVec3("uRaysColor", raysColor);
    }
    if (m_fx.grade) {
        m_composite->setVec4("uGradeParams", glm::vec4(m_fx.brightness, m_fx.contrast, m_fx.saturation, 0.0f));
        m_composite->setVec3("uGradeTint", m_fx.tint);
    }
    // Is the camera under water?
    {
        const glm::vec3 eye = camera.position();
        float top = 0.0f;
        glm::vec3 color(0.2f, 0.45f, 0.7f);
        bool under = Player::waterAt(scene, eye, &top, nullptr, &color);
        if (!under && scene.water().liquid().count()) {   // real liquid (FluidSource) deep enough to be in
            const int n = scene.water().liquid().sample(eye, 0.9f, nullptr, &top);
            under = n >= 12 && top > eye.y + 0.1f;
            const auto& kinds = scene.water().liquid().fluids();
            if (under) color = kinds.empty() ? glm::vec3(0.12f, 0.42f, 0.62f) : kinds[0].color;
        }
        m_composite->setBool("uUnderwater", under);
        // Just came up for air: water runs down the screen for a few seconds.
        const double tNow = now();
        const float pdt = m_lastPost > 0.0 ? (float)std::min(0.1, tNow - m_lastPost) : 0.0f;
        m_lastPost = tNow;
        if (m_wasUnder && !under) m_drip = 1.0f;
        else m_drip = std::max(0.0f, m_drip - pdt / 5.0f);
        m_wasUnder = under;
        m_composite->setFloat("uDrip", m_drip);
        m_composite->setFloat("uTime", (float)(tNow - m_startTime));
        if (under) {
            const glm::mat4 proj = camera.projection();
            bindTex(3, m_hdr.depth);
            m_composite->setInt("uDepth", 3);
            m_composite->setVec2("uDepthParams", glm::vec2(proj[3][2], proj[2][2]));
            m_composite->setBool("uOrthoDepth", camera.orthographic);
            const glm::vec3 tint = glm::clamp(color, glm::vec3(0.02f), glm::vec3(1.0f));
            m_composite->setVec3("uWaterSigma", 0.035f + 0.1f * -glm::log(tint));
            // The deeper you are, the less light gets down there.
            const float deep = std::exp(-std::max(0.0f, top - eye.y) * 0.04f);
            const glm::vec3 light = env.ambientColor * env.ambientIntensity * 1.2f +
                                    env.sunColor * std::max(0.0f, env.sunIntensity) * 0.25f;
            m_composite->setVec3("uWaterFog", glm::pow(tint, glm::vec3(2.2f)) * light * deep);
            m_composite->setMat4("uInvViewProj", glm::inverse(proj * camera.view()));
            m_composite->setFloat("uWaterTop", top);
            m_composite->setFloat("uCaustics", std::max(0.0f, env.sunIntensity) * std::max(env.sunDirection().y, 0.0f) * 0.9f);
        }
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    if (useFxaa) {
        target.bind();
        m_fxaa->bind();
        bindTex(0, m_ldr.color);
        m_fxaa->setInt("uSrc", 0);
        m_fxaa->setVec2("uTexel", glm::vec2(1.0f / m_ldr.w, 1.0f / m_ldr.h));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    target.unbind();
}
