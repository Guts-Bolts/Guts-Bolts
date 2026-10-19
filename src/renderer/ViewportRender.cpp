#include "ViewportRender.h"
#include "GL.h"
#include "Mesh.h"
#include "Shader.h"
#include "Shaders.h"
#include "../core/Log.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <imgui.h>   // (the frame counter)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace ViewportRender {

namespace {

// One ViewportFrame's picture.
struct Picture {
    GLuint   fbo = 0, color = 0, depth = 0;
    int      w = 0, h = 0;
    uint64_t hash = 0;        // what was drawn (redrawn only when this changes)
    int      usedFrame = 0;   // when it was last asked for
};
std::unordered_map<uint64_t, Picture> g_pictures;
std::unique_ptr<Shader> g_shader;
bool g_broken = false;   // the shader didn't compile (logged once; frames show nothing)

void release(Picture& p) {
    if (p.fbo) glDeleteFramebuffers(1, &p.fbo);
    if (p.color) glDeleteTextures(1, &p.color);
    if (p.depth) glDeleteRenderbuffers(1, &p.depth);
    p = Picture{};
}

bool make(Picture& p, int w, int h) {
    release(p);
    p.w = w;
    p.h = h;
    glGenTextures(1, &p.color);
    glBindTexture(GL_TEXTURE_2D, p.color);
    // Drawn twice as big as it's shown and mipmapped: smooth edges for free.
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &p.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, p.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &p.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, p.depth);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) release(p);
    return ok;
}

struct Item { const SceneNode* node; glm::mat4 model; float dist; };

// The parts inside the frame (through Models, WorldModels and parts in parts; not
// other UI, and not hidden ones).
void gather(const SceneNode* n, std::vector<Item>& out) {
    for (auto& c : n->children) {
        if (c->isGui() || !c->visible) continue;
        if (c->isPart() && c->shownTransparency() < 0.999f) out.push_back({c.get(), c->worldMatrix(), 0.0f});
        gather(c.get(), out);
    }
}

// FNV-1a over what's in the picture.
struct Hasher {
    uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
    }
    template <class T> void add(const T& v) { bytes(&v, sizeof(v)); }
};

} // namespace

unsigned render(Scene& scene, const SceneNode& frame, int w, int h) {
    if (Mesh::headless || g_broken || w < 1 || h < 1) return 0;
    if (!g_shader) {
        try { g_shader = std::make_unique<Shader>(Shaders::viewportVert, Shaders::viewportFrag); }
        catch (const std::exception& e) {
            g_broken = true;
            Log::error(std::string("ViewportFrame: ") + e.what());
            return 0;
        }
    }
    const GuiProps& g = frame.gui;
    std::vector<Item> items;
    gather(&frame, items);

    // The camera: its CurrentCamera, else one that frames everything inside it.
    glm::mat4 cam(1.0f);
    float fov = 70.0f;
    const SceneNode* camNode = g.camera ? scene.findById(g.camera) : nullptr;
    if (camNode && camNode->isCamera()) {
        cam = camNode->worldMatrix();
        for (int i = 0; i < 3; ++i) {
            const float l = glm::length(glm::vec3(cam[i]));
            if (l > 1e-6f) cam[i] /= l;
        }
        fov = std::clamp(camNode->fieldOfView, 1.0f, 120.0f);
    } else {
        if (items.empty()) return 0;
        glm::vec3 lo(1e30f), hi(-1e30f);
        for (const Item& it : items)
            for (int k = 0; k < 8; ++k) {
                const glm::vec3 c((k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f, (k & 4) ? 0.5f : -0.5f);
                const glm::vec3 p(it.model * glm::vec4(c, 1.0f));
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
        const glm::vec3 mid = (lo + hi) * 0.5f;
        const float radius = std::max(0.05f, glm::length(hi - lo) * 0.5f);
        const float half = glm::radians(fov) * 0.5f * std::min(1.0f, (float)w / (float)h);
        const glm::vec3 eye = mid + glm::normalize(glm::vec3(0.55f, 0.45f, -1.0f)) * (radius / std::sin(half) * 1.02f);
        cam = glm::inverse(glm::lookAt(eye, mid, glm::vec3(0, 1, 0)));
    }
    const glm::vec3 eye(cam[3]);
    const glm::mat4 view = glm::inverse(cam);
    const glm::mat4 proj = glm::perspective(glm::radians(fov), (float)w / (float)h, 0.05f, 5000.0f);
    const glm::vec3 lightDir = glm::length(g.lightDir) > 1e-6f ? glm::normalize(g.lightDir) : glm::vec3(0, -1, 0);

    // Twice as big as it's shown (smoothed by the mipmaps), but not huge.
    int tw = w * 2, th = h * 2;
    const int most = 1024;
    if (tw > most || th > most) {
        const float k = (float)most / (float)std::max(tw, th);
        tw = std::max(1, (int)(tw * k));
        th = std::max(1, (int)(th * k));
    }

    Hasher hs;
    hs.add(tw); hs.add(th); hs.add(view); hs.add(fov);
    hs.add(g.ambient); hs.add(g.lightColor); hs.add(lightDir);
    for (const Item& it : items) {
        hs.add(it.node); hs.add(it.model); hs.add(it.node->color); hs.add(it.node->shownTransparency());
        hs.add(it.node->material); hs.add(it.node->mesh.get());
    }
    Picture& pic = g_pictures[frame.id];
    pic.usedFrame = ImGui::GetFrameCount();
    if (pic.color && pic.w == tw && pic.h == th && pic.hash == hs.h) return pic.color;   // nothing changed
    if ((pic.w != tw || pic.h != th || !pic.fbo) && !make(pic, tw, th)) return 0;
    pic.hash = hs.h;

    // Keep the GL state of whoever is drawing (Studio's viewport, the Player's screen).
    GLint oldFbo = 0, oldViewport[4] = {0, 0, 0, 0}, oldProgram = 0, oldVao = 0, oldTex = 0;
    GLint oldSrcRgb = 0, oldDstRgb = 0, oldSrcA = 0, oldDstA = 0;
    GLfloat oldClear[4] = {0, 0, 0, 0};
    GLboolean oldMask = GL_TRUE;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    glGetIntegerv(GL_VIEWPORT, oldViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    glGetIntegerv(GL_BLEND_SRC_RGB, &oldSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &oldDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &oldSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &oldDstA);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &oldMask);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST), blendOn = glIsEnabled(GL_BLEND), cullOn = glIsEnabled(GL_CULL_FACE),
                    scissorOn = glIsEnabled(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER, pic.fbo);
    glViewport(0, 0, tw, th);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glClearColor(0, 0, 0, 0);   // see-through: the frame's background shows behind the parts
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Solid parts first, then see-through ones far to near.
    std::vector<Item> solid, clear;
    for (Item& it : items) {
        it.dist = glm::length(glm::vec3(it.model[3]) - eye);
        (it.node->shownTransparency() > 0.001f ? clear : solid).push_back(it);
    }
    std::sort(clear.begin(), clear.end(), [](const Item& a, const Item& b) { return a.dist > b.dist; });

    g_shader->bind();
    g_shader->setMat4("uViewProj", proj * view);
    g_shader->setVec3("uAmbient", g.ambient);
    g_shader->setVec3("uLightColor", g.lightColor);
    g_shader->setVec3("uLightDir", lightDir);
    auto draw = [&](const Item& it) {
        g_shader->setMat4("uModel", it.model);
        g_shader->setMat3("uNormalMat", glm::transpose(glm::inverse(glm::mat3(it.model))));
        g_shader->setVec3("uColor", glm::clamp(it.node->color, 0.0f, 1.0f));
        g_shader->setFloat("uAlpha", 1.0f - it.node->shownTransparency());
        g_shader->setInt("uGlow", it.node->material == Material::Neon ? 1 : 0);
        it.node->mesh->draw();
    };
    glDisable(GL_BLEND);
    for (const Item& it : solid) draw(it);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const Item& it : clear) draw(it);

    glBindTexture(GL_TEXTURE_2D, pic.color);
    glGenerateMipmap(GL_TEXTURE_2D);

    // Put everything back.
    glBindTexture(GL_TEXTURE_2D, (GLuint)oldTex);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)oldFbo);
    glViewport(oldViewport[0], oldViewport[1], oldViewport[2], oldViewport[3]);
    glUseProgram((GLuint)oldProgram);
    glBindVertexArray((GLuint)oldVao);
    glBlendFuncSeparate((GLenum)oldSrcRgb, (GLenum)oldDstRgb, (GLenum)oldSrcA, (GLenum)oldDstA);
    glClearColor(oldClear[0], oldClear[1], oldClear[2], oldClear[3]);
    glDepthMask(oldMask);
    auto set = [](GLenum cap, GLboolean on) { if (on) glEnable(cap); else glDisable(cap); };
    set(GL_DEPTH_TEST, depthOn);
    set(GL_BLEND, blendOn);
    set(GL_CULL_FACE, cullOn);
    set(GL_SCISSOR_TEST, scissorOn);
    return pic.color;
}

void collect() {
    if (g_pictures.empty()) return;
    const int now = ImGui::GetFrameCount();
    for (auto it = g_pictures.begin(); it != g_pictures.end();) {
        if (now - it->second.usedFrame > 120) { release(it->second); it = g_pictures.erase(it); }
        else ++it;
    }
}

} // namespace ViewportRender
