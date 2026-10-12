#pragma once
#include "../../game/GameGui.h"
#include "../../scene/SceneNode.h"
#include <functional>
#include <cstdint>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "../../renderer/Camera.h"
#include "../../renderer/Framebuffer.h"
#include "../../renderer/SceneRenderer.h"
#include "../EditorState.h"
#include "../../scene/Physics.h"
#include <imgui.h>

struct GLFWwindow;
class Scene;
class GameSession;
class TeamCreate;
class SceneNode;
class Player;
struct Transform;

// 3D viewport: renders the scene to an off-screen framebuffer and displays it
// as an ImGui image. In edit mode it handles camera navigation, click-to-select
// and transform gizmos; in Play mode it follows the character, forwards clicks
// to scripts and draws the in-game HUD.
class ViewportPanel {
public:
    ViewportPanel(GLFWwindow* window, Scene* scene, EditorState* state);
    ~ViewportPanel();

    void render(float dt);
    void resetCamera();

    float     cameraYaw() const;                  // for camera-relative controls
    float     swimLook() const;                   // (and swimming up / down)
    glm::vec3 cameraPivot() const { return m_camera.pivot; }
    glm::vec3 cameraPosition() const { return m_camera.position(); }
    void      frameOn(const glm::vec3& target);   // point the camera at a target
    void      followPlayer(Player& p, float dt);   // Play: the camera follows the head (first person too)
    bool      hovered() const { return m_hovered; }
    // Where the mouse points in the 3D view: the part under it (or null) and the
    // spot it touches (on the part, else on the ground). False if the mouse
    // isn't over the 3D view.
    bool      pointAt(ImVec2 mouse, glm::vec3& point, SceneNode*& part);
    // A picture of the game from its spawn point, as a PNG file (for publishing).
    // `fromView`: from where the Studio camera is now (else a nice view of the spawn).
    std::string snapshotPng(int width, int height, bool fromView = false);
    // A picture framed on a box (a Library model's thumbnail).
    std::string snapshotAround(int width, int height, glm::vec3 center, float radius);
    bool      gizmoInUse() const;
    // F: glide the camera to the selected things (only ones with a body:
    // parts, and models / tools with parts in them). False if none.
    bool      focusSelected();
    // Collisions: the parts `movers` go into, and after a move, pull them
    // back to where they only just touch (sliding) or undo it (turning).
    static std::vector<uint64_t> collisionsOf(Scene& scene, const std::vector<SceneNode*>& movers);
    static void stopAtCollisions(Scene& scene, const std::vector<SceneNode*>& movers,
                                 const std::vector<Transform>& before, const std::vector<uint64_t>& hitBefore,
                                 bool sliding);

    // While a session is set, the viewport is in Play mode.
    // (Stop: the editing camera again, whatever the game's scripts did to it.)
    void setSession(GameSession* session) {
        m_session = session;
        if (!session) { m_camera.clip = -1.0f; m_camera.clearScripted(); m_camera.fov = 60.0f; }
    }
    void focus() { m_wantFocus = true; }
    void setTeam(TeamCreate* t) { m_team = t; }
    // Called when the connect tool has picked two parts (and the clicked points).
    std::function<void(SceneNode*, glm::vec3, SceneNode*, glm::vec3)> onConnect;
    // The mode menu in the Viewport's corner asks for a new mode.
    std::function<void(StudioMode)> onMode;
    // Right-clicking the view (without turning the camera): draws the menu's items.
    std::function<void()> contextMenu;

private:
    // The navmesh view: its own look at the parts, rebaked when they change.
    void updateNavOverlay();
    // Dragging the move / scale / rotate handles: where it started (for the readout
    // by the mouse: "4 studs", "45 deg", like Roblox Studio).
    bool      m_gizmoDragging = false;
    bool      m_partBox = false;         // Build mode: dragging a selection box from empty space
    ImVec2    m_boxFrom{0, 0};
    Transform m_dragStart;
    glm::vec3 m_dragStartPivot{0.0f};
    Physics  m_navPhysics;
    uint32_t m_navDrawn = 0;
    double   m_navGather = 0.0;
    bool     m_navShown = false;

    void handleInput(float dt);
    void drawGizmo(const glm::mat4& view, const glm::mat4& proj,
                   const glm::vec2& imgMin, const glm::vec2& imgSize);
    void drawSelectionBoxes(const glm::mat4& view, const glm::mat4& proj,
                            const glm::vec2& imgMin, const glm::vec2& imgSize);
    // Roblox-style handles on each of the 6 sides. Scale: orbs, drag one to move just that side.
    // Move: arrows, drag one to slide everything selected along that way.
    bool scaleHandles(SceneNode* sel, const glm::mat4& view, const glm::mat4& proj,
                      const glm::vec2& imgMin, const glm::vec2& imgSize, bool move = false);
    std::vector<SceneNode*> m_moveNodes;
    std::vector<Transform>  m_moveStart;
    std::vector<uint64_t>   m_moveHits;
    int       m_scaleHover = -1, m_scaleDrag = -1;   // handle 0..5 (axis * 2 + side)
    float     m_scaleT0 = 0.0f;
    Transform m_scaleStart;
    void mouseRay(const glm::vec2& mouse, const glm::vec2& imgMin, const glm::vec2& imgSize,
                  const glm::mat4& view, const glm::mat4& proj, glm::vec3& ro, glm::vec3& rd) const;
    void modelingView(const glm::mat4& view, const glm::mat4& proj,
                      const glm::vec2& imgMin, const glm::vec2& imgSize);   // ViewportModeling.cpp
    bool drawModeMenu(ImVec2 imgPos);   // true while the mouse is on it
    // The TERRAIN tab's brushes: drag on the ground to sculpt it. ViewportTerrain.cpp.
    void terrainView(const glm::mat4& view, const glm::mat4& proj, const glm::vec2& imgMin, const glm::vec2& imgSize, float dt);
    bool  m_sculpting = false;     // the mouse is down with a brush (one undo step)
    float m_sculptLevel = 0.0f;    // Flatten: the height where the stroke started

    GLFWwindow*  m_window;
    Scene*       m_scene;
    EditorState* m_state;
    GameSession* m_session = nullptr;
    TeamCreate*  m_team = nullptr;

    Camera        m_camera;
    Framebuffer   m_fbo;
    SceneRenderer m_renderer;

    int  m_viewW = 0, m_viewH = 0;
    bool m_hovered = false;
    bool m_shiftLock = false;   // play test: Roblox Shift Lock
    bool m_aimSnapshot = false; glm::vec3 m_aimCenter{0.0f}; float m_aimRadius = 1.0f;   // snapshotAround
    // Game UI: pointer state in Play, and dragging a UI object while building.
    GameGui::Input m_guiInput;
    int       m_guiDrag = 0;                  // 1 = moving, 2 = resizing
    uint64_t  m_guiDragId = 0;
    ImVec2    m_guiDragFrom{0, 0};
    UDim2     m_guiDragStart;
    bool m_wantFocus = false;

    // F "zoom to": the camera glides from -> to over a moment.
    float     m_glide = -1.0f;              // seconds in (negative = not gliding)
    glm::vec3 m_glideFrom{0.0f}, m_glideTo{0.0f};
    float     m_glideDistFrom = 8.0f, m_glideDistTo = 8.0f;

    // Modeling mode
    glm::mat4 m_meshGizmo{1.0f};
    bool      m_meshDragging = false;
    bool      m_boxing = false;
    ImVec2    m_boxStart{0, 0};
    ImVec2    m_viewMin{0, 0}, m_viewMax{0, 0};   // where the 3D view was drawn last frame
};
