#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

// The currently active manipulation tool, shared between the toolbar (which
// sets it) and the viewport (which draws the matching gizmo).
enum class GizmoTool { Select, Translate, Rotate, Scale };

// What Studio is doing right now, a bit like Blender's modes:
//   Build    - normal editing: place, move and change objects
//   Modeling - reshape one part's mesh: its corners, edges and faces
//   Simulate - physics and scripts run live, you fly the camera around
//   Play     - a real playtest with your character
enum class StudioMode { Build, Modeling, Simulate, Play };
inline const char* const kStudioModeNames[4] = {"Build", "Modeling", "Simulate", "Play"};

// A Blender-style tool that follows the mouse in Modeling mode: G grab, R rotate,
// S scale (X / Y / Z lock an axis, type a number, click or Enter to finish,
// right-click or Esc to put it back), I inset and Ctrl+R loop cut.
enum class MeshModal { None, Grab, Rotate, Scale, Inset, LoopCut };

// Modeling mode: which part is being reshaped and what's picked on it.
struct ModelingState {
    unsigned long long node = 0;
    int               selectMode = 0;   // 0 = vertices, 1 = edges, 2 = faces
    std::vector<char> sel;              // one flag per vertex
    std::vector<char> hidden;           // one flag per vertex: hidden with H (Alt+H shows them again)
    bool              xray = false;     // show (and pick) the back too
    MeshModal         modal = MeshModal::None;
    glm::vec3         modalAxis{0.0f};  // Grab after Extrude: slide along this way (world); 0 = free
    bool              proportional = false;   // O: moving corners drags nearby ones along, fading out
    float             propRadius = 2.0f;      // how far that reaches (studs; mouse wheel while moving)
    int               modalEnded = -1;  // the frame a modal tool finished (its Esc / Enter are used up)
};

// Editor-wide UI state shared across panels.
struct EditorState {
    GizmoTool tool        = GizmoTool::Translate;
    bool      gizmoLocal  = true;   // gizmo orientation: local vs. world space
    bool      snapEnabled = false;   // Move (and Scale) go in steps of snapTranslate studs
    bool      rotSnapEnabled = false; // Rotate goes in steps of snapRotate degrees
    bool      collisions  = false;   // moved parts stop against others instead of going through
    bool      showGrid    = true;
    bool      orthographic = false;   // the viewport camera has no perspective (numpad 5, like Blender)
    bool      showNavMesh = false;    // draw the navigation mesh (where characters can walk)
    bool      showPhysics = false;    // Physics View: joints, contacts, speeds, sleeping parts, force zones
    // Physics Replay: outlines of where the loose parts were at the frame being looked at
    // (box matrices, a unit cube each), and the selected part's path.
    std::vector<glm::mat4> replayGhosts;
    std::vector<glm::vec3> replayTrail;
    int       bakeNavMesh = 0;        // asks the viewport to rebake it now (Bake button)
    std::string navInfo;              // "1234 floor cells, baked in 12 ms" for the ribbon tooltip

    StudioMode    mode = StudioMode::Build;
    ModelingState modeling;
    bool          simPaused = false;  // Simulate / Play: physics and scripts frozen
    bool          simStep   = false;  // run one frame while paused

    // "Connect" tool: click one part, then another, to join them.
    int   connectTool = -1;         // ConstraintType, or -1 when off; 5 = hinge + motor, 6.. = ConstraintType 5..
    unsigned long long connectFirst = 0;
    float connectPoint[3] = {0, 0, 0};

    // TERRAIN tab: the sculpting brush (a Terrain::Brush, or -1 when off) and its settings.
    int   terrainBrush = -1;
    float terrainSize = 16.0f;       // the brush circle's radius, in studs
    float terrainStrength = 0.5f;    // 0.1 .. 1
    int   terrainMaterial = 0;       // TerrainMaterial (what Paint paints)
    float terrainHills = 0.5f;       // Generate: how bumpy
    int   terrainGenSize = 1;        // Generate: 0 = 256, 1 = 512, 2 = 1024 studs across

    // Animation Editor: the rig being animated (clicks pick its parts, not the whole model).
    unsigned long long animRig = 0;
    std::string weightBone;   // Modeling mode: colour the corners by how much this bone pulls them (Armature window)
    // The Armature window's weight brush: drag over the mesh to paint weightBone's weights.
    int   weightPaint = -1;        // -1 off, 0 add, 1 take away, 2 smooth
    float weightRadius = 40.0f;    // the brush circle, in pixels
    float weightStrength = 0.5f;   // 0.05 .. 1: how fast it paints

    float snapTranslate = 0.5f;     // world units
    float snapRotate    = 15.0f;    // degrees
    float snapScale     = 0.25f;    // factor
};
