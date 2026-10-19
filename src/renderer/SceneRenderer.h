#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ShadowMap.h"

class Scene;
class SceneNode;
class Shader;
class Camera;
class Framebuffer;
class Mesh;

// Draws a Scene into a framebuffer: sun shadows, sky with clouds and stars,
// physically-based lit parts, point / spot lights, fog, then post-processing
// (ambient occlusion, bloom, tone mapping, colour grading, FXAA).
// Shared by the editor viewport and the Player app.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    void render(Scene& scene, const Camera& camera, Framebuffer& target, bool showGrid);

    // Blurred copies of what render() just drew into `target`, for game UI that blurs
    // the world behind it (UIBlur). Only made when something asks (it's a few tiny
    // passes). backdrop(0) is a little blurry, backdrop(kBackdropLevels - 1) very.
    static constexpr int kBackdropLevels = 4;
    void makeBackdrop(const Framebuffer& target);
    unsigned backdrop(int level) const;

    // Offscreen buffer with a colour texture and (optionally) a depth texture.
    struct Target {
        unsigned int fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    };

private:
    void renderShadowPass(Scene& scene, const glm::mat4& lightSpace, ShadowMap& target);
    void drawGeometry(Scene& scene, const Camera& camera, bool editing);
    // The terrain's mesh, rebuilt when the terrain changes (null = no terrain).
    Mesh* terrainMesh(Scene& scene);
    struct WaterItem { SceneNode* node; glm::mat4 model; };
    void drawWater(Scene& scene, const Camera& camera, const std::vector<WaterItem>& waters);
    static glm::mat4 decalMatrix(const SceneNode& decal);
    void drawConstraints(Scene& scene, bool editing);
    void postProcess(Scene& scene, const Camera& camera, Framebuffer& target);
    // Screen effects and the Sky (ColorCorrectionEffect, SunRaysEffect, DepthOfFieldEffect,
    // BloomEffect, BlurEffect, Sky) found right inside Lighting and the Camera, all added
    // together. Gathered once a frame.
    struct ScreenFx {
        bool      grade = false;                   // any ColorCorrectionEffect
        float     brightness = 0.0f, contrast = 1.0f, saturation = 1.0f;
        glm::vec3 tint{1.0f};
        bool      bloom = false;                   // any BloomEffect (replaces the Lighting panel's bloom)
        float     bloomIntensity = 0.0f, bloomThreshold = 4.0f, bloomSize = 0.0f;
        float     blur = 0.0f;                     // BlurEffect sizes, together (pixels)
        bool      dof = false;
        float     focus = 0.0f, inFocus = 0.0f, nearAmt = 0.0f, farAmt = 0.0f;
        float     rays = 0.0f, raysSpread = 0.0f;  // SunRaysEffect
        const SceneNode* sky = nullptr;            // the Sky in Lighting (the first one)
    };
    ScreenFx m_fx;
    void gatherScreenFx(Scene& scene);
    void renderLiquid(Scene& scene, const Camera& camera);   // real liquid (Liquid.cpp)
    void drawHighlights(Scene& scene, const Camera& camera, Framebuffer& target);   // Highlight objects
    void drawRibbons(Scene& scene, const Camera& camera);   // Trails and Beams
    void ensureTargets(int w, int h);
    void buildGrid();
public:
    // The floor grid's line spacing in studs (it follows Move snapping).
    void setGridSpacing(float studs);
    // Something extra to draw over the world (the navmesh view): triangles and
    // lines, x y z r g b a per corner. Empty lists = nothing.
    struct OverlayVertex { glm::vec3 pos; glm::vec4 color; };
    void setOverlay(const std::vector<OverlayVertex>& tris, const std::vector<OverlayVertex>& lines);
private:
    float m_gridSpacing = 1.0f;
    void buildAxes();

    std::unique_ptr<Shader> m_lit, m_grid, m_sky, m_depth;
    std::unique_ptr<Shader> m_ssao, m_bloomPre, m_bloomDown, m_bloomUp, m_composite, m_fxaa;
    std::unique_ptr<Shader> m_water;   // water parts (see Shaders::waterFrag)
    std::unique_ptr<Shader> m_hlMask, m_hl;   // Highlights (see Shaders::highlightFrag)
    Target m_hlTarget;                        // a Highlight's shape on screen
    std::unique_ptr<Shader> m_ribbon;         // Trails and Beams
    std::unique_ptr<Shader> m_sunRays;        // SunRaysEffect
    static constexpr int kBlurLevels = 5;
    Target m_blurDown[kBlurLevels], m_blurUp[kBlurLevels - 1], m_rays;   // BlurEffect / DepthOfFieldEffect, SunRaysEffect
    unsigned int m_ribbonVao = 0, m_ribbonVbo = 0;
    std::unique_ptr<Shader> m_sprite;         // ParticleEmitter / Fire / Smoke / Sparkles particles
    unsigned int m_spriteVao = 0, m_spriteVbo = 0;
    void drawSprites(Scene& scene, const Camera& camera);
    // Each Trail's pieces: where its two attachments were, and when.
    struct TrailPoint { glm::vec3 a, b; double time; };
    struct TrailState { std::vector<TrailPoint> points; double seen = 0.0; int clears = 0; };
    std::unordered_map<uint64_t, TrailState> m_trails;
    std::unique_ptr<Shader> m_fluidDepth, m_fluidThick, m_fluidColor, m_fluidBlur, m_fluidShade, m_fluidSimple;
    Target m_waterCopy;   // water parts: the solid scene behind them (colour and depth)
    struct FlowTex { unsigned tex = 0; int version = -1; };
    std::unordered_map<uint64_t, FlowTex> m_flowTex;   // each water body's flow map, on the graphics card
    Target m_fDepth, m_fTmp, m_fThick, m_fColor, m_sceneCopy;   // liquid: depth, blur scratch, thickness, the scene behind
    unsigned int m_fluidVao = 0, m_fluidVbo = 0;
    ShadowMap               m_shadow, m_shadowNear;   // wide, and sharp close to the camera
    int                     m_shadowRes = 0;
    glm::mat4               m_lightSpace{1.0f};       // (the wide shadow map's, for the water)
    bool                    m_shadowsOn = false;
    bool                    m_wasUnder = false;       // the camera was underwater last frame
    float                   m_drip = 0.0f;            // water running down the screen after surfacing
    double                  m_lastPost = 0.0;
    // Render distance (GraphicsSettings::renderDistance) for this frame: things further
    // than m_viewDist from m_viewPos aren't drawn. 0 = no limit.
    glm::vec3               m_viewPos{0.0f};
    float                   m_viewDist = 0.0f;
    bool tooFar(const glm::vec3& p, float radius) const {
        return m_viewDist > 0.0f && glm::length(p - m_viewPos) - radius > m_viewDist;
    }

    static constexpr int kBloomLevels = 6;
    Target m_hdr, m_ao, m_ldr, m_bloom[kBloomLevels];
    Target m_bdDown[kBackdropLevels + 1], m_bdUp[kBackdropLevels];   // makeBackdrop()

    unsigned int m_gridVao = 0, m_gridVbo = 0;
    std::unique_ptr<Shader> m_overlay;
    unsigned int m_overlayVao = 0, m_overlayVbo = 0;
    // Explosions' smoke and fire (Blast.h), and their shockwaves.
    std::unique_ptr<Shader> m_puff;
    unsigned int m_puffVao = 0, m_puffVbo = 0;
    void drawBlasts(Scene& scene, const Camera& camera);
    int          m_overlayTris = 0, m_overlayLines = 0;   // vertex counts
    void drawOverlay(const glm::mat4& view, const glm::mat4& proj);
    unsigned int m_axisVao = 0, m_axisVbo = 0;
    unsigned int m_emptyVao = 0;
    int          m_gridVertexCount = 0;
    double       m_startTime = 0.0;
    std::unordered_map<uint64_t, std::unique_ptr<Mesh>> m_waterMeshes;   // wavy surfaces while playing
    std::unique_ptr<Mesh> m_terrainMesh;
    uint64_t              m_terrainVersion = 0;
    std::unique_ptr<Mesh> m_floodMesh;                                    // flowing water
};
