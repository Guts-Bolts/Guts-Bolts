#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include "ShadowMap.h"
#include "../scene/Weather.h"
#include "../scene/BounceLight.h"

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

    // Studio's view styles (like Blender's viewport shading). Material is the real look;
    // the others are for building: Solid (plain colours, soft studio light, no shadows),
    // Flat (just the colours, no light at all) and Wireframe (only the edges).
    enum class Shading { Material = 0, Solid = 1, Flat = 2, Wireframe = 3 };
    Shading shading = Shading::Material;
    // X-ray: everything see-through, so you can see (and box-select) what's behind.
    bool xray = false;

    // Blurred copies of what render() just drew into `target`, for game UI that blurs
    // the world behind it (UIBlur). Only made when something asks (it's a few tiny
    // passes). backdrop(0) is a little blurry, backdrop(kBackdropLevels - 1) very.
    static constexpr int kBackdropLevels = 4;
    void makeBackdrop(const Framebuffer& target);
    unsigned backdrop(int level) const;

    // The temporal upscaler and the traced bounce light (Scalability.h) build on the frames
    // before, so only the main view of a window turns this on: thumbnails, the avatar
    // preview and other one-off pictures drawn with the same renderer leave it off.
    bool temporal = false;
    // What the last frame really used (for tests and the settings menu).
    bool usedUpscaler() const { return m_usedUpscaler; }
    bool usedBounce() const { return m_usedBounce; }
    bool usedReflections() const { return m_usedSsr; }
    bool usedVolumetricFog() const { return m_usedFogVol; }   // the fog took light this frame (shafts, glowing lamps)   // shiny parts mirrored the world this frame

    // Offscreen buffer with a colour texture and (optionally) a depth texture.
    struct Target {
        unsigned int fbo = 0, color = 0, depth = 0;
        int w = 0, h = 0;
    };

private:
    void renderShadowPass(Scene& scene, const glm::mat4& lightSpace, ShadowMap& target);
    // Lamp shadows: the nearest few lights with Shadows on, six sides each, into
    // m_lampShadows. Fills m_lampShadowNodes (slot -> light).
    void renderLampShadows(Scene& scene, const glm::vec3& camPos, int most);
public:
    // One side of a lamp's shadow cube: which way it looks (0 +X, 1 -X, 2 +Y, 3 -Y,
    // 4 +Z, 5 -Z). The lit shader works out the same thing by itself (lampVisibility).
    static glm::mat4 lampFaceView(const glm::vec3& lampPos, int face);
private:
    void drawGeometry(Scene& scene, const Camera& camera, bool editing);
public:
    // How many groups of copies were drawn in one go last frame (Instancing.h; for tests).
    int lastCopyGroups() const { return m_lastCopies; }
    // How many triangles the last frame drew (shadows and all). Detail levels (MeshLod.h)
    // bring it down; GB_NO_LODS=1 turns them off to compare.
    long lastTriangles() const { return m_lastTriangles; }
    // Which detail level a mesh at `model` gets this frame (0 = full).
    int  lodFor(const Mesh* mesh, const glm::mat4& model) const;
private:
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
        float     rays = 0.0f, raysSpread = 0.0f;  // SunRaysEffect (or Lighting.LightRays)
        const SceneNode* sky = nullptr;            // the Sky in Lighting (the first one)
    };
    ScreenFx m_fx;
    void gatherScreenFx(Scene& scene);
    void renderLiquid(Scene& scene, const Camera& camera);   // real liquid (Liquid.cpp)
    void drawHighlights(Scene& scene, const Camera& camera, Framebuffer& target);   // Highlight objects
    void drawRibbons(Scene& scene, const Camera& camera);   // Trails and Beams
    void ensureTargets(int w, int h);
    // SceneRendererTemporal.cpp: the upscaler and the bounce light, kept apart from the rest.
    struct Temporal;
    Temporal* m_tmp = nullptr;
    bool m_usedUpscaler = false, m_usedBounce = false;
    void destroyTemporal();
    // Before drawing: whether this frame goes through the upscaler; if so the camera
    // gets this frame's jitter (put back by endTemporal).
    bool beginTemporal(const Camera& camera, int outW, int outH, int inW, int inH);
    void bindBounce(const Shader& lit);                 // last frame's bounce light, for the lit shader
    void traceBounce(const Camera& camera);             // after drawing: work out the bounce light
    unsigned resolveTemporal(const Camera& camera);     // the upscaled picture (0 = not upscaling)
    float upscaleSharpen() const;                       // how much the composite sharpens it
    void endTemporal(const Camera& camera);
    // Shiny parts mirroring the world (screen-space reflections, High and up): the lit
    // shader walks reflected rays through a copy of last frame's picture and depth.
    Target    m_ssrCopy;
    glm::mat4 m_ssrViewProj{1.0f};
    glm::vec3 m_ssrPos{0.0f};
    bool      m_ssrReady = false, m_usedSsr = false;
    void bindReflections(const Camera& camera);   // before drawing the parts
    void keepForReflections(const Camera& camera); // after drawing: copy this frame for the next one
    // Volumetric fog (SceneRendererFog.cpp, High and up): fog that takes light, so the
    // sun cuts shafts through it and lamps glow in it, and that drifts in wisps.
    struct FogVol;
    FogVol*   m_fog = nullptr;
    bool      m_fogVolOn = false, m_usedFogVol = false;
    glm::vec4 m_fogLightPos[8], m_fogLightColor[8], m_fogLightDir[8];
    int       m_fogLightCount = 0;
    bool wantFogVolume(Scene& scene, const Camera& camera) const;
    void drawFogVolume(Scene& scene, const Camera& camera);   // once the solid things are drawn
    void destroyFog();
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
    // Realism: eyes adjusting (auto exposure) and motion blur.
    std::unique_ptr<Shader> m_lumShader, m_motionBlur;
    Target       m_lum, m_blurred;
    glm::mat4    m_prevViewProj{1.0f};
    bool         m_hasPrev = false, m_lumFresh = true;
    double       m_lumClock = 0.0;
    static constexpr int kBlurLevels = 5;
    Target m_blurDown[kBlurLevels], m_blurUp[kBlurLevels - 1], m_rays;   // BlurEffect / DepthOfFieldEffect, SunRaysEffect
    unsigned int m_ribbonVao = 0, m_ribbonVbo = 0;
    std::unique_ptr<Shader> m_sprite;         // ParticleEmitter / Fire / Smoke / Sparkles particles
    unsigned int m_spriteVao = 0, m_spriteVbo = 0;
    void drawSprites(Scene& scene, const Camera& camera);
    // Rain and snow (Lighting.Weather): the falling drops, and how wet / snowy things are.
    WeatherSim m_weather;
    double     m_weatherClock = 0.0;
    void drawWeather(Scene& scene, const Camera& camera);
    void drawSplashes(Scene& scene, const Camera& camera);   // rings and spray where rain hits
    void drawLightning(const Camera& camera);   // a Storm's bolt, glowing
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
    ShadowMap               m_lampShadows;            // 6 x 4 tiles: up to 4 lamps, 6 sides each
    int                     m_lampTile = 0;           // pixels across one tile
    std::vector<const SceneNode*> m_lampShadowNodes;  // which lamp has which slot this frame
    std::vector<BounceLight::Point> m_bounce;         // bouncy light (redone ~4 times a second)
    double                  m_bounceAt = 0.0;
    glm::vec3               m_bounceAround{0.0f};
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
    unsigned int m_instanceVbo = 0;   // each copy's settings when many are drawn at once (Instancing.h)
    long         m_lastTriangles = 0;
    float        m_lodPixelsPerStud = 0.0f;   // 1 stud, 1 stud away, covers this many pixels (0 = detail levels off)
    float        m_lodBias = 1.0f;            // from the graphics level: > 1 simpler sooner
    bool         m_lodOrtho = false;          // orthographic: size doesn't change with distance
    int          m_lastCopies = 0;    // how many groups of copies the last frame drew in one go
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
