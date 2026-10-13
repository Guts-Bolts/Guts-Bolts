#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "../renderer/Mesh.h"

struct EditMesh;   // see EditMesh.h

enum class PrimitiveType { None, Cube, Sphere, Plane, Cylinder, Mesh };   // Mesh = built in Modeling mode

// What an object *is*, à la Roblox classes:
//   Part   — a visible, physical shape (Cube / Sphere / Plane / Cylinder)
//   Model  — an empty container used to group other objects
//   Script — Lua code that runs when you press Play (script.Parent = its parent). A
//            LocalScript (isLocal) runs on each player's own computer instead.
//   Remote — a RemoteEvent / RemoteFunction: how LocalScripts and the game's
//            server Scripts send each other messages
//   Prompt — a ProximityPrompt: "E  Open" pops up when a player walks up to the
//            part it's in; pressing the key (or tapping it) fires Triggered
//   Highlight — colours a part or model and draws an outline round it, even
//            through walls (to show teammates, the thing you're aiming at...)
//   Trail  — a ribbon left behind between two Attachments as they move (a sword
//            swing, a speed streak)
//   Beam   — a ribbon (straight or curved) between two Attachments: lasers,
//            ropes of light, waterfalls...
//   Light  — a PointLight / SpotLight, usually placed inside a part
//   ForceField — inside a character: a glowing shield (like Roblox's spawn ForceField)
//   Sound  — a sound effect or music (inside a part = it comes from there)
//   Attachment — a point on a part that constraints connect to
//   Constraint — joins two parts: rope, rod, spring, weld or hinge (+ motor)
//   Tool       — something a character can carry and use (a sword, a gun...): a
//                container whose part named "Handle" goes in the character's hand
//   Value      — holds one value (IntValue, NumberValue, StringValue, BoolValue,
//                Vector3Value, Color3Value); leaderstats are made of these
//   Decal      — a picture on one side of the part it's inside
//   Animation  — keyframes that pose a rig's parts (made in Studio's Animation
//                Editor, played by scripts: humanoid:LoadAnimation(anim):Play())
enum class NodeKind { Part, Model, Script, Light, ForceField, Sound, Attachment, Constraint, Tool, Value, Decal, Animation, Gui,
                      FluidSystem, FluidEmitter, Mover, Remote, Prompt, Highlight, Trail, Beam };

// A ProximityPrompt's settings (kind == Prompt; `enabled` switches it off).
struct PromptProps {
    std::string action = "Interact";   // ActionText: what pressing it does
    std::string object;                // ObjectText: what it's on (shown above, smaller)
    std::string key = "E";             // KeyboardKeyCode
    float       hold = 0.0f;           // HoldDuration: seconds to hold the key (0 = just press)
    float       range = 5.0f;          // MaxActivationDistance (Roblox's default 10 studs)
    bool        lineOfSight = true;    // RequiresLineOfSight: hidden behind walls
    bool        clickable = true;      // ClickablePrompt: can be clicked / tapped too
};

// ColorSequence / NumberSequence: a colour or number that changes along something
// (a Trail's age, a Beam's length), as keypoints from t = 0 to 1.
struct ColorKey  { float t; glm::vec3 c; };
struct NumberKey { float t; float v; };
glm::vec3 sampleSequence(const std::vector<ColorKey>& keys, float t);
float     sampleSequence(const std::vector<NumberKey>& keys, float t);

// A Trail's or Beam's settings (kind == Trail / Beam; `enabled` switches it off).
// Defaults are Roblox's.
struct EffectProps {
    uint64_t a0 = 0, a1 = 0;                                   // Attachment0, Attachment1
    std::vector<ColorKey>  color{{0.0f, glm::vec3(1.0f)}, {1.0f, glm::vec3(1.0f)}};
    std::vector<NumberKey> transparency{{0.0f, 0.5f}, {1.0f, 0.5f}};
    std::string texture;                                       // a picture along it ("" = plain)
    float lightEmission = 0.0f;                                // 1 = glows (adds its light)
    bool  faceCamera = false;                                  // always turned to face you
    // Trail
    float lifetime = 2.0f;                                     // seconds a piece lasts
    float minLength = 0.1f;                                    // studs moved before a new piece
    float maxLength = 0.0f;                                    // longest it gets (0 = no limit)
    std::vector<NumberKey> widthScale{{0.0f, 1.0f}, {1.0f, 1.0f}};   // width over its age
    int   clears = 0;                                          // trail:Clear() counts up (not saved)
    // Beam
    float width0 = 1.0f, width1 = 1.0f;                        // at each end
    float curve0 = 0.0f, curve1 = 0.0f;                        // CurveSize0/1: bends it along each Attachment's axis
    int   segments = 10;
    float textureLength = 1.0f, textureSpeed = 1.0f;
    bool  textureWrap = false;                                 // TextureMode: Wrap (per stud) / Stretch (repeats TextureLength times)
};

// A Highlight's settings (kind == Highlight; `enabled` switches it off). Like Roblox's
// defaults: a see-through red fill with a white outline, shown through walls.
struct HighlightProps {
    glm::vec3 fill{1.0f, 0.0f, 0.0f};      // FillColor
    glm::vec3 outline{1.0f, 1.0f, 1.0f};   // OutlineColor
    float     fillTransparency = 0.5f;     // FillTransparency (1 = no fill)
    float     outlineTransparency = 0.0f;  // OutlineTransparency (1 = no outline)
    bool      onTop = true;                // DepthMode: AlwaysOnTop (seen through walls), else Occluded
    uint64_t  adornee = 0;                 // Adornee: what it highlights (0 = what it's in)
};

// Game UI (kind == Gui), like Roblox's: a ScreenGui holds Frames, labels,
// buttons and pictures, laid out with UDim2 (a fraction of the parent plus pixels).
// UICorner, UIStroke, UIShadow and UIBlur go inside a Frame / label / button and change
// how it's drawn (a "modifier", not a box of its own). UIListLayout / UIGridLayout line
// up the things next to them, and UIPadding keeps them away from the edges.
// A TextBox is a label you can type in; a ScrollingFrame holds more than fits and scrolls.
// BillboardGui and SurfaceGui are like ScreenGui but sit on a part (its Adornee, else the
// part they're in): a billboard floats over it facing you, a surface is painted on one side.
enum class GuiType { ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ImageButton, UICorner, UIStroke, UIShadow, UIBlur,
                     TextBox, ScrollingFrame, UIListLayout, UIGridLayout, UIPadding, BillboardGui, SurfaceGui };
inline constexpr int kGuiTypeCount = 17;
inline const char* const kGuiClassNames[kGuiTypeCount] = {"ScreenGui", "Frame", "TextLabel", "TextButton", "ImageLabel",
                                                          "ImageButton", "UICorner", "UIStroke", "UIShadow", "UIBlur",
                                                          "TextBox", "ScrollingFrame", "UIListLayout", "UIGridLayout", "UIPadding",
                                                          "BillboardGui", "SurfaceGui"};
inline bool isGuiModifier(GuiType t) {
    return t == GuiType::UICorner || t == GuiType::UIStroke || t == GuiType::UIShadow || t == GuiType::UIBlur ||
           t == GuiType::UIListLayout || t == GuiType::UIGridLayout || t == GuiType::UIPadding;
}
// A whole layer of UI (holds the rest): ScreenGui, BillboardGui, SurfaceGui.
inline bool isGuiLayer(GuiType t) { return t == GuiType::ScreenGui || t == GuiType::BillboardGui || t == GuiType::SurfaceGui; }
inline bool isGuiLayout(GuiType t) { return t == GuiType::UIListLayout || t == GuiType::UIGridLayout; }
inline bool guiHasText(GuiType t) { return t == GuiType::TextLabel || t == GuiType::TextButton || t == GuiType::TextBox; }
struct UDim2 {
    float xs = 0.0f, xo = 0.0f, ys = 0.0f, yo = 0.0f;   // X scale, X offset (pixels), Y scale, Y offset
    bool operator==(const UDim2& o) const { return xs == o.xs && xo == o.xo && ys == o.ys && yo == o.yo; }
};
struct GuiProps {
    GuiType   type = GuiType::Frame;
    UDim2     pos;                                  // Position
    UDim2     size{0.0f, 100.0f, 0.0f, 100.0f};     // Size
    glm::vec2 anchor{0.0f};                         // AnchorPoint (0..1: which point of it sits at Position)
    glm::vec3 bg{1.0f};                             // BackgroundColor3
    float     bgTransparency = 0.0f;
    glm::vec3 borderColor{0.11f, 0.16f, 0.2f};      // BorderColor3 (and UIStroke.Color)
    int       border = 1;                           // BorderSizePixel
    int       zIndex = 1;
    bool      clips = false;                        // ClipsDescendants
    // Text (TextLabel / TextButton)
    std::string text;
    glm::vec3 textColor{0.1f};
    float     textSize = 14.0f;
    bool      textScaled = false, textWrapped = false, bold = false;
    int       xAlign = 1, yAlign = 1;               // 0 = Left/Top, 1 = Center, 2 = Right/Bottom
    float     textTransparency = 0.0f;
    glm::vec3 strokeColor{0.0f};                    // TextStrokeColor3
    float     strokeTransparency = 1.0f;            // TextStrokeTransparency (1 = no outline)
    // Pictures (ImageLabel / ImageButton): "gb:<id>" or a file, like a Decal
    std::string image;
    glm::vec3 imageColor{1.0f};
    float     imageTransparency = 0.0f;
    bool      autoButtonColor = true;               // buttons darken when you point at / press them
    int       displayOrder = 0;                     // ScreenGui: higher ones are drawn on top
    UDim2     corner{0.0f, 8.0f, 0.0f, 0.0f};       // UICorner.CornerRadius (xs, xo used)
    // UICorner.TopLeft / TopRight / BottomRight / BottomLeft (UDims: corners = the pixels,
    // cornerScales = the fraction), so each corner can be rounded on its own.
    // A pixel value below 0 = the same as CornerRadius.
    glm::vec4 corners{-1.0f}, cornerScales{0.0f};
    // UIShadow: Color is bg, Transparency is bgTransparency. Blur is how soft its edge is,
    // Spread makes it bigger than the box, Offset moves it (pixels).
    glm::vec2 shadowOffset{0.0f, 4.0f};
    float     shadowBlur = 12.0f, shadowSpread = 0.0f;
    // UIBlur.Size: how blurry the world behind it looks (pixels).
    float     blurSize = 12.0f;
    float     thickness = 1.0f;                     // UIStroke.Thickness
    int       layoutOrder = 0;                      // LayoutOrder: UIListLayout / UIGridLayout put lower ones first
    // TextBox
    std::string placeholder;                        // PlaceholderText (shown greyed out while it's empty)
    glm::vec3 placeholderColor{0.7f};               // PlaceholderColor3
    bool      clearOnFocus = true;                  // ClearTextOnFocus
    bool      editable = true;                      // TextEditable
    bool      multiLine = false;                    // MultiLine (Enter makes a new line instead of finishing)
    // ScrollingFrame: what's inside is laid out on a canvas CanvasSize big, and
    // CanvasPosition is how far it's scrolled (pixels).
    UDim2     canvasSize{0.0f, 0.0f, 2.0f, 0.0f};
    glm::vec2 canvasPos{0.0f};
    int       scrollBar = 12;                       // ScrollBarThickness (pixels)
    glm::vec3 scrollColor{0.0f};                    // ScrollBarImageColor3
    float     scrollTransparency = 0.0f;            // ScrollBarImageTransparency
    int       scrollDir = 3;                        // ScrollingDirection: 1 = X, 2 = Y, 3 = XY
    int       autoCanvas = 0;                       // AutomaticCanvasSize: 0 None, 1 X, 2 Y, 3 XY (grows to fit)
    bool      scrolling = true;                     // ScrollingEnabled
    // UIListLayout / UIGridLayout
    int       fill = 0;                             // FillDirection: 0 = Vertical, 1 = Horizontal
    UDim2     padding;                              // Padding (list: xs, xo) / CellPadding (grid)
    UDim2     cellSize{0.0f, 100.0f, 0.0f, 100.0f}; // UIGridLayout.CellSize
    int       hAlign = 0, vAlign = 0;               // HorizontalAlignment / VerticalAlignment: 0 Left/Top, 1 Center, 2 Right/Bottom
    bool      sortByName = false;                   // SortOrder: Name (else LayoutOrder)
    int       maxCells = 0;                         // UIGridLayout.FillDirectionMaxCells (0 = as many as fit)
    // UIPadding: PaddingLeft, PaddingTop, PaddingRight, PaddingBottom (scale, pixels)
    glm::vec4 padScale{0.0f}, padPx{0.0f};
    // BillboardGui (its Size: offset = pixels, scale = studs) / SurfaceGui
    uint64_t  adornee = 0;                          // Adornee: the part it's on (0 = the part it's in)
    glm::vec3 studsOffset{0.0f};                    // BillboardGui.StudsOffset (right, up, towards you)
    glm::vec3 worldOffset{0.0f};                    // BillboardGui.StudsOffsetWorldSpace
    bool      alwaysOnTop = false;                  // AlwaysOnTop: shown through walls
    float     maxDistance = 0.0f;                   // MaxDistance: hidden further away (0 = never)
    int       face = 5;                             // SurfaceGui.Face (Face order: 5 = Front)
    bool      perStud = true;                       // SizingMode: PixelsPerStud (else FixedSize: CanvasSize)
    float     pixelsPerStud = 50.0f;                // PixelsPerStud
    glm::vec2 surfaceCanvas{800.0f, 600.0f};        // SurfaceGui.CanvasSize (FixedSize)
    // Where it was last drawn (AbsolutePosition / AbsoluteSize). Runtime only.
    glm::vec2 absPos{0.0f}, absSize{0.0f};
    glm::vec2 contentSize{0.0f};                    // layouts: AbsoluteContentSize. Runtime only.
};

// Sides of a part, in Roblox's NormalId order.
enum class Face { Right, Top, Back, Left, Bottom, Front };
inline const char* const kFaceNames[6] = {"Right", "Top", "Back", "Left", "Bottom", "Front"};

enum class ConstraintType { Rope, Rod, Spring, Weld, Hinge };
inline const char* const kConstraintNames[5] = {"Rope", "Rod", "Spring", "Weld", "Hinge"};

enum class LightType { Point, Spot };

// Things that push a part around (kind == Mover). The classic BodyMovers go straight
// inside a part; the newer constraint ones use Attachment0 (an Attachment in the part).
// Both kinds work, so old games keep their handling and new ones can use either.
enum class MoverType {
    BodyVelocity, BodyPosition, BodyGyro, BodyAngularVelocity, BodyThrust, BodyForce,         // classic
    LinearVelocity, AlignPosition, AlignOrientation, AngularVelocity, VectorForce, Torque,  // constraints
};
inline constexpr int kMoverTypeCount = 12;
inline const char* const kMoverClassNames[kMoverTypeCount] = {
    "BodyVelocity", "BodyPosition", "BodyGyro", "BodyAngularVelocity", "BodyThrust", "BodyForce",
    "LinearVelocity", "AlignPosition", "AlignOrientation", "AngularVelocity", "VectorForce", "Torque"};
inline bool isBodyMover(MoverType t) { return (int)t <= (int)MoverType::BodyForce; }

// A mover's settings. Which ones count depends on the type (Roblox's names in brackets).
struct MoverProps {
    MoverType type = MoverType::BodyVelocity;
    // The main value: BodyVelocity / LinearVelocity [Velocity / VectorVelocity],
    // BodyPosition / AlignPosition [Position], BodyAngularVelocity / AngularVelocity
    // [AngularVelocity], BodyForce / BodyThrust / VectorForce [Force], Torque [Torque].
    glm::vec3 value{0.0f};
    // Classic movers: how hard it may push along each world axis [MaxForce] or turn
    // around it [MaxTorque] (0 on an axis = leave that axis alone).
    glm::vec3 maxAxes{4000.0f};
    // Constraint movers: the most force [MaxForce] or turn [MaxTorque] in any direction.
    float     maxForce = 10000.0f;
    float     p = 10000.0f, d = 1250.0f;     // BodyPosition / BodyGyro / BodyVelocity: spring [P] and damping [D]
    glm::vec3 rotation{0.0f};                // BodyGyro / AlignOrientation [CFrame]: the way to face (Euler degrees)
    glm::vec3 location{0.0f};                // BodyThrust [Location]: where it pushes (the part's own space)
    float     responsiveness = 10.0f;        // AlignPosition / AlignOrientation: how quickly it gets there
    float     maxVelocity = 0.0f;            // AlignPosition [MaxVelocity] / AlignOrientation [MaxAngularVelocity] (0 = no limit)
    bool      relativeToAttachment = false;  // LinearVelocity / AngularVelocity / VectorForce / Torque [RelativeTo]: Attachment0's own space
    bool      atCenterOfMass = false;        // VectorForce [ApplyAtCenterOfMass]
    bool      rigid = false;                 // AlignPosition / AlignOrientation [RigidityEnabled]: as hard and fast as it can
};

// Roblox's starting values for each mover.
inline MoverProps moverDefaults(MoverType t) {
    MoverProps m;
    m.type = t;
    switch (t) {
        case MoverType::BodyVelocity:        m.value = {0, 2, 0}; m.maxAxes = glm::vec3(4000); m.p = 1250; break;
        case MoverType::BodyPosition:        m.value = {0, 50, 0}; m.maxAxes = glm::vec3(4000); m.p = 10000; m.d = 1250; break;
        case MoverType::BodyGyro:            m.maxAxes = {400000, 0, 400000}; m.p = 3000; m.d = 500; break;
        case MoverType::BodyAngularVelocity: m.value = {0, 2, 0}; m.maxAxes = glm::vec3(4000); m.p = 1250; break;
        case MoverType::BodyThrust:
        case MoverType::BodyForce:           break;
        case MoverType::LinearVelocity:      m.maxForce = 1000; break;
        case MoverType::AlignPosition:       m.maxForce = 10000; break;
        case MoverType::AlignOrientation:    m.maxForce = 10000; break;
        case MoverType::AngularVelocity:     m.maxForce = 1000; break;
        case MoverType::VectorForce:         m.value = {1000, 0, 0}; m.relativeToAttachment = true; break;
        case MoverType::Torque:              m.value = {1000, 0, 0}; m.relativeToAttachment = true; break;
    }
    return m;
}

// Surface look, à la Roblox materials — affects shading in the lit shader.
// New ones go on the end: the number is what the network sends.
enum class Material { Plastic, Metal, Neon, Wood, Glass, Concrete, Ice,
                      Sand, Grass, Brick, Fabric, Slate, Granite, Marble, Cobblestone, Foil, DiamondPlate,
                      SmoothPlastic };
inline constexpr int kMaterialCount = 18;
inline const char* const kMaterialNames[kMaterialCount] =
    {"Plastic", "Metal", "Neon", "Wood", "Glass", "Concrete", "Ice",
     "Sand", "Grass", "Brick", "Fabric", "Slate", "Granite", "Marble", "Cobblestone", "Foil", "DiamondPlate",
     "SmoothPlastic"};
// "Plastic, Metal, ... or SmoothPlastic": for error messages and hints.
inline const char* materialNameList() {
    static std::string list = [] {
        std::string s;
        for (int i = 0; i < kMaterialCount; ++i)
            s += std::string(i == 0 ? "" : i == kMaterialCount - 1 ? " or " : ", ") + kMaterialNames[i];
        return s;
    }();
    return list.c_str();
}
// A material by name (exact, like scripts), plus the few other Roblox names we draw
// with one of ours. False if there's no such material.
inline bool materialFromName(const std::string& s, Material& out) {
    for (int i = 0; i < kMaterialCount; ++i)
        if (s == kMaterialNames[i]) { out = (Material)i; return true; }
    if (s == "CorrodedMetal")                { out = Material::Metal;    return true; }
    if (s == "WoodPlanks")                   { out = Material::Wood;     return true; }
    if (s == "Asphalt" || s == "Basalt" || s == "Limestone" || s == "Pavement" || s == "Sandstone" || s == "Rock")
                                             { out = Material::Concrete; return true; }
    if (s == "Pebble")                       { out = Material::Cobblestone; return true; }
    if (s == "LeafyGrass")                   { out = Material::Grass;    return true; }
    if (s == "Salt")                         { out = Material::Sand;     return true; }
    if (s == "Glacier")                      { out = Material::Ice;      return true; }
    return false;
}
// How heavy (1 = Plastic, floats half under in water), grippy (0-2) and bouncy (0-1)
// each material is by default; a part's Density / Friction / Elasticity override it.
// In proportion to Roblox's defaults, but on our scale (Metal is 3, not 7.85).
struct MaterialPhysics { float density, friction, elasticity; };
inline MaterialPhysics materialPhysics(Material m) {
    switch (m) {
        case Material::Metal:         return {3.0f, 0.35f, 0.15f};
        case Material::Wood:          return {0.7f, 0.50f, 0.25f};
        case Material::Glass:         return {2.5f, 0.25f, 0.20f};
        case Material::Concrete:      return {2.4f, 0.70f, 0.10f};
        case Material::Ice:           return {0.9f, 0.03f, 0.10f};
        case Material::Neon:          return {1.0f, 0.40f, 0.20f};
        case Material::Sand:          return {1.6f, 0.60f, 0.05f};
        case Material::Grass:         return {1.3f, 0.55f, 0.10f};
        case Material::Brick:         return {1.9f, 0.80f, 0.15f};
        case Material::Fabric:        return {0.9f, 0.45f, 0.05f};
        case Material::Slate:         return {2.7f, 0.55f, 0.20f};
        case Material::Granite:       return {2.7f, 0.55f, 0.20f};
        case Material::Marble:        return {2.6f, 0.30f, 0.17f};
        case Material::Cobblestone:   return {2.7f, 0.65f, 0.17f};
        case Material::Foil:          return {2.7f, 0.40f, 0.25f};
        case Material::DiamondPlate:  return {3.0f, 0.55f, 0.25f};
        case Material::SmoothPlastic: return {1.0f, 0.40f, 0.30f};
        default:                      return {1.0f, 0.50f, 0.30f};   // Plastic
    }
}
// The look the part shader draws (uMaterial): the first seven are the material's own
// number; 7-9 are water, liquid and terrain, so the newer ones start at 10.
inline int materialShaderId(Material m) { return (int)m < 7 ? (int)m : (int)m + 3; }

// A custom value on an object, like Roblox Attributes (Properties panel >
// Attributes, or part:SetAttribute("Coins", 5) in a script).
// Tool.Grip's default turn (see SceneNode::gripRot): none, like Roblox's. The Handle's
// long side (+Y) points the way the hand's front faces, so with the arm held out a
// sword points straight up, the classic way.
inline const glm::mat3 kDefaultGripRot{1.0f};

struct Attribute {
    enum Type { Bool, Number, String, Vector3, Color3 };
    std::string name;
    Type        type = Number;
    bool        b = false;
    double      n = 0.0;
    std::string s;
    glm::vec3   v{0.0f};   // Vector3 / Color3
};

struct Transform {
    glm::vec3 position = {0.0f, 0.0f, 0.0f};
    glm::vec3 rotation = {0.0f, 0.0f, 0.0f}; // Euler angles in degrees (XYZ order)
    glm::vec3 scale    = {1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const;
};

class SceneNode {
public:
    explicit SceneNode(std::string name, NodeKind kind = NodeKind::Part);

    uint64_t              id;               // unique, stable across undo / save
    std::string           name;
    NodeKind              kind = NodeKind::Part;
    Transform             transform;
    PrimitiveType         primitiveType = PrimitiveType::None;
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<EditMesh> editMesh;    // primitiveType == Mesh: the shape's points and faces
    glm::vec3             color    = {0.65f, 0.65f, 0.80f};
    bool                  selected = false;
    bool                  visible  = true;
    bool                  internal = false;  // hidden from the outliner / picking

    // Appearance
    float    transparency = 0.0f;            // 0 = opaque, 1 = invisible
    // Extra see-through-ness only this screen uses (Roblox's LocalTransparencyModifier):
    // your own character fades as the camera comes close. Never saved or sent.
    float    localTransparency = 0.0f;
    float    shownTransparency() const { return 1.0f - (1.0f - transparency) * (1.0f - localTransparency); }
    Material material      = Material::Plastic;

    // Behaviour
    bool     anchored   = true;              // false = falls with gravity in Play
    bool     canCollide = true;              // false = things pass through it
    bool     castShadow = true;
    // Solid modeling (Roblox's Negate / Union): a negated part is shown see-through pink and
    // cut out of the other parts by Union; it remembers its old look for Un-negate. A union
    // remembers the parts it was made from (JSON) so Separate can give them back.
    bool        negated = false;
    glm::vec3   negColor{1.0f};
    float       negTransparency = 0.0f;
    bool        negCollide = true;
    std::string unionSource;

    // Script (kind == Script); an Animation's keyframes (JSON, see Animation.h)
    std::string source;
    bool        enabled = true;           // scripts and lights can be switched off
    bool        isModule = false;         // ModuleScript: only runs when require()d
    bool        isLocal = false;          // LocalScript: runs on each player's computer, not the game's server
    bool        remoteFunction = false;   // (kind == Remote) a RemoteFunction, else a RemoteEvent
    PromptProps prompt;                   // (kind == Prompt)
    HighlightProps highlight;             // (kind == Highlight)
    EffectProps effect;                   // (kind == Trail / Beam)

    // Anything
    std::vector<Attribute>   attributes;
    std::vector<std::string> tags;        // CollectionService-style tags
    bool        locked = false;           // can't be picked in the Viewport (Roblox "Locked")
    const Attribute* findAttribute(const std::string& n) const {
        for (auto& a : attributes) if (a.name == n) return &a;
        return nullptr;
    }

    // Light (kind == Light) — uses `color` for its colour.
    LightType   lightType  = LightType::Point;
    float       brightness = 2.0f;
    float       range      = 14.0f;
    float       spotAngle  = 60.0f;       // cone width in degrees (spot lights)

    // Sound (kind == Sound)
    std::string soundId  = "coin";         // built-in name or a file in the games folder
    float       volume   = 0.6f;
    float       pitch    = 1.0f;
    bool        looped   = false;
    bool        autoplay = false;          // start playing when the game starts
    int         audioHandle = 0;           // runtime only

    // Physics (parts). Negative = use the material's value.
    float       density    = -1.0f;
    float       friction   = -1.0f;
    float       elasticity = -1.0f;        // bounciness 0..1

    // Constraint (kind == Constraint). ref0 / ref1 are Attachments (or, for a
    // Weld, the two Parts).
    ConstraintType constraintType = ConstraintType::Rope;
    uint64_t    ref0 = 0, ref1 = 0;
    float       length      = -1.0f;       // rope / rod / spring length (-1 = as placed)
    float       stiffness   = 200.0f;      // spring
    float       damping     = 5.0f;        // spring
    float       motorSpeed  = 0.0f;        // hinge motor, radians / second
    float       motorTorque = 0.0f;        // hinge motor strength (0 = no motor)
    float       thickness   = 0.1f;        // how thick the rope / rod looks

    // Decal (kind == Decal): which picture, on which side. `color` tints it
    // (white = as it is), `transparency` fades it.
    std::string texture;                   // "gb:<id>", a file in the games folder, or a path
    std::string tshirt;                    // a character's torso: a T-shirt picture flat on its front ("gb:<id>")
    Face        face = Face::Front;

    // Game UI (kind == Gui). `visible` is Visible; `enabled` is a ScreenGui's Enabled.
    GuiProps    gui;

    // Value (kind == Value): its type and contents (the name inside isn't used).
    Attribute   value;
    bool        intValue = false;          // an IntValue (Number, whole numbers only)

    // Tool (kind == Tool). `enabled` is Tool.Enabled.
    std::string toolTip;                   // shown when you hover its hotbar slot
    bool        canBeDropped = true;       // Backspace drops it
    bool        starterTool  = false;      // everyone gets one when they spawn (like Roblox's StarterPack)
    glm::vec3   gripPos{0.0f};             // the point on the Handle (Handle's own space) that sits in the hand
    // Tool.Grip's turn, exactly like Roblox's (columns = GripRight, GripUp, GripForward):
    // the Handle sits at RightArm * RightGrip.C0 * Grip:Inverse(). The default (none)
    // holds a Handle with its long side (+Y) pointing up when the arm is held out.
    glm::mat3   gripRot = kDefaultGripRot;

    // Real liquid (see Liquid.h). A FluidSystem is a kind of liquid: `color`, how thick
    // it is and how much it sticks together. A FluidEmitter pours it out: its
    // transform's position and scale are its Position and Size; `enabled` turns it on and off.
    float       viscosity      = 0.015f;   // FluidSystem: 0 = runny like water, 1 = thick like honey
    float       surfaceTension = 0.0f;     // FluidSystem: 0..1, how much drops pull together (beads, strands)
    float       fluidRate      = 500.0f;   // FluidEmitter: drops per second
    glm::vec3   fluidVelocity{0.0f, -10.0f, 0.0f};   // FluidEmitter: how fast (and which way) it pours
    uint64_t    fluidSystem    = 0;        // FluidEmitter: its FluidSystem (0 = plain water)

    // Mover (kind == Mover): BodyVelocity, AlignPosition... `enabled` is Enabled
    // (constraint movers); ref0 / ref1 are Attachment0 / Attachment1.
    MoverProps  mover;

    // Runtime-only physics state (not saved).
    glm::vec3   angularVelocity = {0.0f, 0.0f, 0.0f};
    float       sleepTime = 0.0f;
    bool        jointReady = false;        // weld: rest pose captured
    glm::vec3   jointA{0.0f}, jointB{0.0f};
    glm::vec4   jointRot{0.0f, 0.0f, 0.0f, 1.0f};

    // Runtime-only physics state (not saved).
    glm::vec3   velocity = {0.0f, 0.0f, 0.0f};

    // While Studio's Animation Editor shows this part posed: where it really
    // is (that's what gets saved, copied and undone). Runtime only.
    std::shared_ptr<Transform> restPose;

    SceneNode*                              parent = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children;

    bool isPart()   const { return kind == NodeKind::Part && mesh != nullptr; }
    bool isScript() const { return kind == NodeKind::Script; }
    bool isLight()  const { return kind == NodeKind::Light; }
    bool isSound()  const { return kind == NodeKind::Sound; }
    bool isTool()   const { return kind == NodeKind::Tool; }
    bool isValue()  const { return kind == NodeKind::Value; }
    bool isDecal()  const { return kind == NodeKind::Decal; }
    bool isAnimation() const { return kind == NodeKind::Animation; }
    bool isGui() const { return kind == NodeKind::Gui; }
    // A Frame / label / button / picture (not a ScreenGui, UICorner or UIStroke).
    bool isGuiObject() const {
        return kind == NodeKind::Gui && !isGuiLayer(gui.type) && !isGuiModifier(gui.type);
    }
    bool isGuiButton() const { return kind == NodeKind::Gui && (gui.type == GuiType::TextButton || gui.type == GuiType::ImageButton); }
    // "IntValue", "StringValue"... (kind == Value)
    const char* valueClass() const {
        switch (value.type) {
            case Attribute::Bool:    return "BoolValue";
            case Attribute::String:  return "StringValue";
            case Attribute::Vector3: return "Vector3Value";
            case Attribute::Color3:  return "Color3Value";
            default:                 return intValue ? "IntValue" : "NumberValue";
        }
    }
    std::string valueText() const;   // for showing it (the leaderboard, Properties)
    bool isAttachment() const { return kind == NodeKind::Attachment; }
    bool isConstraint() const { return kind == NodeKind::Constraint; }
    bool isMover() const { return kind == NodeKind::Mover; }
    bool isRemote() const { return kind == NodeKind::Remote; }
    bool isPrompt() const { return kind == NodeKind::Prompt; }
    bool isHighlight() const { return kind == NodeKind::Highlight; }
    bool isEffect() const { return kind == NodeKind::Trail || kind == NodeKind::Beam; }
    bool hasForceField() const {
        for (auto& c : children) if (c->kind == NodeKind::ForceField) return true;
        return false;
    }

    SceneNode*                 addChild(std::unique_ptr<SceneNode> child);
    void                       removeChild(SceneNode* child);
    std::unique_ptr<SceneNode> detachChild(SceneNode* child);  // keeps it alive
    SceneNode*                 findChild(const std::string& name, bool recursive = false) const;
    bool                       isAncestorOf(const SceneNode* other) const;
    glm::mat4                  worldMatrix() const;
    std::string                fullName() const;   // e.g. "Workspace.Door.Script"

    // Make sure freshly created nodes never reuse an id loaded from a file.
    static void     reserveId(uint64_t used);
    static uint64_t newId();
};
