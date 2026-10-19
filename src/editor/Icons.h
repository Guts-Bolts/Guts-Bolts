#pragma once
#include <imgui.h>

class SceneNode;

// Small vector icons for Studio (ribbon buttons, Explorer rows), drawn with
// ImGui shapes so they stay sharp at any size.
namespace Icons {

enum class Id {
    Select, Move, Scale, Rotate, Transform,
    Part, Sphere, Cylinder, Plane, Model, Folder, Script, ModuleScript, LocalScript, Remote, Prompt, Click, Highlight, Ribbon, Particles, Light, Sound, Attachment, Constraint,
    ForceField, Workspace, Player, Tool, Value, Decal, Animation, Rig,
    ScreenGui, GuiFrame, GuiText, GuiButton, GuiImage, GuiCorner,
    Play, PlayHere, Run, Stop,
    Paste, Copy, Cut, Duplicate, Undo, Redo, Delete,
    Group, Ungroup, Lock, Anchor, Snap, Collide, Align,
    Insert, Toolbox, Explorer, Properties, Output, CommandBar, Settings, Team, Keyboard, Import, Export, Lighting,
    // Studio modes and Modeling-mode tools
    Build, Mesh, Simulate, Pause, Step,
    Vertex, Edge, Face, Extrude, Inset, Subdivide, Merge, Fill, Flip, Smooth, XRay, Done,
    NavMesh, Bake,
    Union, Negate, Intersect, Separate,   // solid modeling
    Terrain, TerrainRaise, TerrainLower, TerrainSmooth, TerrainFlatten, TerrainPaint, TerrainClear,   // the TERRAIN tab
};

// Draw an icon centred on `c`, `size` pixels across.
void draw(ImDrawList* dl, ImVec2 c, float size, Id id, ImU32 tint = IM_COL32(220, 222, 228, 255));
// The icon for an object in the Explorer.
Id forNode(const SceneNode& n);
// An icon as an ImGui item (for rows of text).
void inlineIcon(Id id, float size = 0.0f);

} // namespace Icons
