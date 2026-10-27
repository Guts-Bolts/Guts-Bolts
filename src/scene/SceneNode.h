#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <glm/glm.hpp>
#include "../renderer/Mesh.h"
#include "../core/SoundFx.h"

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
//   ClickDetector — inside a part or model: players can click it (or tap it) to
//                make something happen (buttons, levers, "click to buy" signs)
//   Animation  — keyframes that pose a rig's parts (made in Studio's Animation
//                Editor, played by scripts: humanoid:LoadAnimation(anim):Play())
//   SoundGroup — a volume knob (and effects) for many sounds at once: each Sound whose
//                SoundGroup is this one plays at Volume x the group's Volume (a group
//                inside another group multiplies again). Usually in SoundService.
//   SoundEffect — ReverbSoundEffect, EchoSoundEffect... inside a Sound (changes that
//                sound) or a SoundGroup (changes all of its sounds together)
//   PostEffect — a screen effect in Lighting (or the Camera): ColorCorrectionEffect,
//                SunRaysEffect, DepthOfFieldEffect, BloomEffect, BlurEffect
//   Sky        — in Lighting: skybox pictures, the sun and moon, how many stars
//   MaterialVariant — in MaterialService: your own material, made from pictures (colour,
//                bumps, shininess, metal) on top of a built-in one. Parts use it by name.
//   Particles  — a ParticleEmitter, Fire, Smoke or Sparkles inside a part (or an
//                Attachment): little pictures that fly out of it (see ParticleProps)
enum class NodeKind { Part, Model, Script, Light, ForceField, Sound, Attachment, Constraint, Tool, Value, Decal, Animation, Gui,
                      FluidSystem, FluidEmitter, Mover, Remote, Prompt, Highlight, Trail, Beam, ClickDetector, Particles, Camera,
                      PostEffect, Sky, SoundGroup, SoundEffect, MaterialVariant };

// A sound effect's settings (kind == SoundEffect; `enabled` is Enabled). Which numbers mean
// what depends on the type: SoundFx::params(type) names them (DecayTime, Delay, Level...).
struct SoundFxProps {
    SoundFx::Type type = SoundFx::Type::Reverb;
    int           priority = 0;                     // Priority: higher ones are applied first
    float         v[SoundFx::kMostParams] = {1.5f, 1.0f, 1.0f, 0.0f, 0.0f};
};
inline SoundFxProps soundFxDefaults(SoundFx::Type t) {
    SoundFxProps p;
    p.type = t;
    const SoundFx::Settings d = SoundFx::defaults(t);
    for (int i = 0; i < SoundFx::kMostParams; ++i) p.v[i] = d.v[i];
    return p;
}

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

// A ClickDetector's settings (kind == ClickDetector).
struct ClickProps {
    float       range = 16.0f;   // MaxActivationDistance (Roblox's default 32 studs; 0 = can't be clicked)
    std::string cursor;          // CursorIcon: a picture for the mouse pointer ("" = the hand)
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

// Screen effects (kind == PostEffect; `enabled` switches it off). They work when they're
// right inside Lighting or the Camera, and several stack up. Defaults are Roblox's.
enum class PostEffectType { ColorCorrection, SunRays, DepthOfField, Bloom, Blur };
inline constexpr int kPostEffectCount = 5;
inline const char* const kPostEffectClassNames[kPostEffectCount] = {
    "ColorCorrectionEffect", "SunRaysEffect", "DepthOfFieldEffect", "BloomEffect", "BlurEffect"};
struct PostEffectProps {
    PostEffectType type = PostEffectType::ColorCorrection;
    // ColorCorrectionEffect
    float     brightness = 0.0f;           // Brightness: added to every colour (-1..1)
    float     contrast = 0.0f;             // Contrast (-1..1, 0 = as it is)
    float     saturation = 0.0f;           // Saturation (-1 = grey .. 1 = twice as colourful)
    glm::vec3 tint{1.0f};                  // TintColor (multiplies)
    // SunRaysEffect (Intensity, Spread) / BloomEffect (Intensity, Size, Threshold) / BlurEffect (Size)
    float     intensity = 0.25f;
    float     spread = 1.0f;               // how far the rays reach across the screen (0..1)
    float     size = 24.0f;                // pixels: how far the glow / blur spreads (0..56)
    float     threshold = 0.95f;           // BloomEffect: how bright before it glows
    // DepthOfFieldEffect (distances in studs)
    float     farIntensity = 0.75f;        // how blurry far things get (0..1)
    float     focusDistance = 0.05f;       // where it's sharpest (0..200)
    float     inFocusRadius = 10.0f;       // how far either side of that stays sharp (0..50)
    float     nearIntensity = 0.75f;       // how blurry things closer than the focus get (0..1)
};
inline PostEffectProps postEffectDefaults(PostEffectType t) {
    PostEffectProps p;
    p.type = t;
    if (t == PostEffectType::Bloom) p.intensity = 0.4f;
    return p;
}

// Keep each setting in the range it can have (scripts, files and other players can send anything).
inline void clampPostEffect(PostEffectProps& p) {
    auto c = [](float& v, float lo, float hi) { v = (v == v) ? (v < lo ? lo : v > hi ? hi : v) : lo; };
    c(p.brightness, -1.0f, 1.0f); c(p.contrast, -1.0f, 1.0f); c(p.saturation, -1.0f, 1.0f);
    c(p.tint.r, 0.0f, 1.0f); c(p.tint.g, 0.0f, 1.0f); c(p.tint.b, 0.0f, 1.0f);
    c(p.intensity, 0.0f, 1.0f); c(p.spread, 0.0f, 1.0f); c(p.size, 0.0f, 56.0f); c(p.threshold, 0.0f, 4.0f);
    c(p.farIntensity, 0.0f, 1.0f); c(p.focusDistance, 0.0f, 200.0f); c(p.inFocusRadius, 0.0f, 50.0f);
    c(p.nearIntensity, 0.0f, 1.0f);
}

// A Sky (kind == Sky), right inside Lighting. Pictures are ids like a Decal's ("gb:<id>",
// a file in the games folder...); "" = the engine's own sky / sun / moon. The skybox is
// used when all six of its pictures load.
struct SkyProps {
    std::string bk, dn, ft, lf, rt, up;    // SkyboxBk, SkyboxDn, SkyboxFt, SkyboxLf, SkyboxRt, SkyboxUp
    std::string sun, moon;                 // SunTextureId, MoonTextureId
    int         starCount = 3000;          // StarCount (0..5000)
    bool        celestial = true;          // CelestialBodiesShown: the sun and moon
    float       sunSize = 21.0f;           // SunAngularSize (degrees)
    float       moonSize = 11.0f;          // MoonAngularSize (degrees)
};
inline void clampSky(SkyProps& s) {
    if (s.starCount < 0) s.starCount = 0;
    if (s.starCount > 5000) s.starCount = 5000;
    auto c = [](float& v) { v = (v == v) ? (v < 0.0f ? 0.0f : v > 60.0f ? 60.0f : v) : 0.0f; };
    c(s.sunSize); c(s.moonSize);
    for (std::string* t : {&s.bk, &s.dn, &s.ft, &s.lf, &s.rt, &s.up, &s.sun, &s.moon})
        if (t->size() > 500) t->resize(500);
}

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
// UIGradient colours the object it's in (fading along a line), UIScale makes it and
// everything inside it bigger or smaller, UIAspectRatioConstraint keeps its shape.
// A ViewportFrame shows the parts and models inside it as a little 3D picture, seen
// from its CurrentCamera (a Camera object, kind == Camera).
enum class GuiType { ScreenGui, Frame, TextLabel, TextButton, ImageLabel, ImageButton, UICorner, UIStroke, UIShadow, UIBlur,
                     TextBox, ScrollingFrame, UIListLayout, UIGridLayout, UIPadding, BillboardGui, SurfaceGui,
                     UIGradient, UIScale, UIAspectRatioConstraint, ViewportFrame };
inline constexpr int kGuiTypeCount = 21;
inline const char* const kGuiClassNames[kGuiTypeCount] = {"ScreenGui", "Frame", "TextLabel", "TextButton", "ImageLabel",
                                                          "ImageButton", "UICorner", "UIStroke", "UIShadow", "UIBlur",
                                                          "TextBox", "ScrollingFrame", "UIListLayout", "UIGridLayout", "UIPadding",
                                                          "BillboardGui", "SurfaceGui", "UIGradient", "UIScale",
                                                          "UIAspectRatioConstraint", "ViewportFrame"};
inline bool isGuiModifier(GuiType t) {
    return t == GuiType::UICorner || t == GuiType::UIStroke || t == GuiType::UIShadow || t == GuiType::UIBlur ||
           t == GuiType::UIListLayout || t == GuiType::UIGridLayout || t == GuiType::UIPadding ||
           t == GuiType::UIGradient || t == GuiType::UIScale || t == GuiType::UIAspectRatioConstraint;
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
    // UIGradient: Color and Transparency along the line (Rotation degrees clockwise from
    // left -> right), moved by Offset (a fraction of the object's size). `enabled` is Enabled.
    std::vector<ColorKey>  gradColor{{0.0f, glm::vec3(1.0f)}, {1.0f, glm::vec3(1.0f)}};
    std::vector<NumberKey> gradTransparency{{0.0f, 0.0f}, {1.0f, 0.0f}};
    float     gradRotation = 0.0f;
    glm::vec2 gradOffset{0.0f};
    float     scale = 1.0f;                         // UIScale.Scale
    // UIAspectRatioConstraint: AspectRatio (width / height), AspectType (0 FitWithinMaxSize,
    // 1 ScaleWithParentSize) and DominantAxis (0 Width, 1 Height).
    float     aspectRatio = 1.0f;
    int       aspectType = 0, dominantAxis = 0;
    // ViewportFrame: CurrentCamera (a Camera's id; 0 = none: it frames what's inside by
    // itself), Ambient, LightColor and LightDirection. ImageColor3 / ImageTransparency
    // tint the picture.
    uint64_t  camera = 0;
    glm::vec3 ambient{200.0f / 255.0f}, lightColor{140.0f / 255.0f}, lightDir{-1.0f, -1.0f, -1.0f};
    // Where it was last drawn (AbsolutePosition / AbsoluteSize). Runtime only.
    glm::vec2 absPos{0.0f}, absSize{0.0f};
    float     absScale = 1.0f;                      // every UIScale above it multiplied (pixels are this much bigger). Runtime only.
    glm::vec2 contentSize{0.0f};                    // layouts: AbsoluteContentSize. Runtime only.
};

// Sides of a part, in Roblox's NormalId order.
enum class Face { Right, Top, Back, Left, Bottom, Front };
inline const char* const kFaceNames[6] = {"Right", "Top", "Back", "Left", "Bottom", "Front"};

// Particle effects (kind == Particles; `enabled` switches them off). A ParticleEmitter
// is the fully adjustable one; Fire, Smoke and Sparkles are the classic ready-made ones
// with just a few settings. Every computer makes its own particles from these settings
// (they're only for looks): the particles are never saved or sent, the settings are.
enum class ParticleType { ParticleEmitter, Fire, Smoke, Sparkles };
inline constexpr int kParticleTypeCount = 4;
inline const char* const kParticleClassNames[kParticleTypeCount] = {"ParticleEmitter", "Fire", "Smoke", "Sparkles"};
// Built-in particle pictures (a ParticleEmitter's Texture can also be "gb:<id>" or a file).
inline constexpr int kParticleLookCount = 4;
inline const char* const kParticleLooks[kParticleLookCount] = {"", "smoke", "fire", "sparkle"};   // "" = a soft round glow
// The most particles there can be at once (all emitters together), and from one emitter,
// so a huge Rate or Emit(100000) can't freeze the game. New ones wait until some have faded.
inline constexpr int kMaxSprites = 6000;
inline constexpr int kMaxSpritesPerEmitter = 1500;
struct ParticleProps {
    ParticleType type = ParticleType::ParticleEmitter;
    // ParticleEmitter (Roblox's names and defaults). Ranges are (min, max): each particle
    // gets a random value between them (NumberRange).
    float     rate = 20.0f;                         // Rate: particles a second
    glm::vec2 lifetime{5.0f, 10.0f};                // Lifetime (seconds)
    glm::vec2 speed{5.0f, 5.0f};                    // Speed (studs / second)
    glm::vec2 spread{0.0f, 0.0f};                   // SpreadAngle (degrees, X and Y: a Vector2)
    glm::vec2 rotation{0.0f, 0.0f};                 // Rotation: its starting turn (degrees)
    glm::vec2 rotSpeed{0.0f, 0.0f};                 // RotSpeed (degrees / second)
    std::vector<ColorKey>  color{{0.0f, glm::vec3(1.0f)}, {1.0f, glm::vec3(1.0f)}};   // Color over its life
    std::vector<NumberKey> size{{0.0f, 1.0f}, {1.0f, 1.0f}};                          // Size over its life (studs)
    std::vector<NumberKey> transparency{{0.0f, 0.0f}, {1.0f, 0.0f}};                  // Transparency over its life
    std::string texture;                            // a built-in look (kParticleLooks) or a picture
    float     lightEmission = 0.0f;                 // 1 = glows (adds its light to what's behind)
    glm::vec3 acceleration{0.0f};                   // Acceleration (studs / second², world space)
    float     drag = 0.0f;                          // Drag: how quickly they slow down
    Face      direction = Face::Top;                // EmissionDirection
    bool      locked = false;                       // LockedToPart: they move with the part
    // Fire / Smoke / Sparkles
    glm::vec3 color1{1.0f};                         // Fire.Color, Smoke.Color, Sparkles.SparkleColor
    glm::vec3 color2{1.0f};                         // Fire.SecondaryColor
    float     fxSize = 5.0f;                        // Fire.Size / Smoke.Size
    float     rise = 9.0f;                          // Fire.Heat / Smoke.RiseVelocity
    float     opacity = 0.5f;                       // Smoke.Opacity
    // emitter:Emit(n) and :Clear(). Running counts, sent to other players so theirs do
    // it too (not saved); `pending...` is what this computer hasn't done yet.
    int       emits = 0, clears = 0;
    int       pendingEmit = 0;
    bool      pendingClear = false;
};
// Roblox's starting values for each one.
inline ParticleProps particleDefaults(ParticleType t) {
    ParticleProps p;
    p.type = t;
    switch (t) {
        case ParticleType::Fire:
            p.color1 = glm::vec3(236, 139, 70) / 255.0f; p.color2 = glm::vec3(139, 80, 55) / 255.0f;
            p.fxSize = 5.0f; p.rise = 9.0f; break;
        case ParticleType::Smoke:
            p.color1 = glm::vec3(1.0f); p.fxSize = 1.0f; p.rise = 1.0f; p.opacity = 0.5f; break;
        case ParticleType::Sparkles:
            p.color1 = glm::vec3(144, 25, 255) / 255.0f; break;
        default: break;
    }
    return p;
}

enum class ConstraintType { Rope, Rod, Spring, Weld, Hinge, BallSocket, Prismatic, Cylindrical };
inline constexpr int kConstraintTypeCount = 8;
inline const char* const kConstraintNames[kConstraintTypeCount] = {"Rope", "Rod", "Spring", "Weld", "Hinge",
                                                                   "BallSocket", "Prismatic", "Cylindrical"};
inline const char* const kConstraintClassNames[kConstraintTypeCount] = {
    "RopeConstraint", "RodConstraint", "SpringConstraint", "WeldConstraint", "HingeConstraint",
    "BallSocketConstraint", "PrismaticConstraint", "CylindricalConstraint"};
// The joints whose settings live in JointProps (below).
inline bool usesJointProps(ConstraintType t) { return (int)t >= (int)ConstraintType::BallSocket; }
// Prismatic and cylindrical joints slide along Attachment0's axis.
inline bool jointSlides(ConstraintType t) { return t == ConstraintType::Prismatic || t == ConstraintType::Cylindrical; }

// ActuatorType / AngularActuatorType: nothing, a motor (runs at a speed) or a
// servo (goes to a target and stops there).
enum class Actuator { None, Motor, Servo };
inline const char* const kActuatorNames[3] = {"None", "Motor", "Servo"};

// Settings of the ball-socket, prismatic (slider) and cylindrical (slide + spin)
// joints. Roblox's property names in brackets; angles in degrees, like Roblox.
struct JointProps {
    // BallSocket: how far Attachment1's axis may lean away from Attachment0's [LimitsEnabled,
    // UpperAngle], and how far it may twist around it [TwistLimitsEnabled, TwistLowerAngle,
    // TwistUpperAngle]. Prismatic / Cylindrical: LimitsEnabled turns on LowerLimit / UpperLimit.
    bool  limitsEnabled      = false;
    float upperAngle         = 45.0f;   // BallSocket: the cone; Cylindrical: the furthest turn one way
    bool  twistLimitsEnabled = false;
    float twistLowerAngle    = -45.0f;
    float twistUpperAngle    = 45.0f;
    float restitution        = 0.0f;    // 0..1: how much it bounces off a limit [Restitution]
    // Prismatic / Cylindrical: sliding along Attachment0's axis (its X, Roblox's Axis).
    float lowerLimit = 0.0f, upperLimit = 5.0f;   // studs [LowerLimit, UpperLimit]
    Actuator actuator = Actuator::None;           // [ActuatorType]
    float motorMaxForce  = 0.0f;                  // [MotorMaxForce]
    float velocity       = 0.0f;                  // motor: studs / second [Velocity]
    float speed          = 0.0f;                  // servo: studs / second [Speed]
    float servoMaxForce  = 0.0f;                  // [ServoMaxForce]
    float targetPosition = 0.0f;                  // servo: studs [TargetPosition]
    // Cylindrical: spinning around the axis (tilted InclinationAngle degrees from X towards Y).
    Actuator angularActuator = Actuator::None;    // [AngularActuatorType]
    float angularVelocity  = 0.0f;                // motor: radians / second [AngularVelocity]
    float motorMaxTorque   = 0.0f;                // [MotorMaxTorque]
    float angularSpeed     = 0.0f;                // servo: radians / second [AngularSpeed]
    float targetAngle      = 0.0f;                // servo: degrees [TargetAngle]
    float servoMaxTorque   = 0.0f;                // [ServoMaxTorque]
    bool  angularLimitsEnabled = false;           // [AngularLimitsEnabled]: LowerAngle..UpperAngle
    float lowerAngle       = -45.0f;              // [LowerAngle]
    float inclinationAngle = 0.0f;                // [InclinationAngle]
    // Where it is now (worked out by the physics, not saved): [CurrentPosition]
    // studs along the axis, [CurrentAngle] degrees around it.
    float currentPosition = 0.0f, currentAngle = 0.0f;
};

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
        case Material::Grass:         return {1.2f, 0.55f, 0.10f};
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

// A MaterialVariant (kind == MaterialVariant), in MaterialService, like Roblox's. A part
// whose Material is this one's BaseMaterial and whose MaterialVariant is this one's name
// is drawn with these pictures (ids like a Decal's: "gb:<id>" or a file; "" = none). It
// still weighs, grips and bounces like the BaseMaterial.
struct MaterialVariantProps {
    Material    base = Material::Plastic;                       // BaseMaterial
    std::string colorMap, normalMap, roughnessMap, metalnessMap; // ColorMap, NormalMap, RoughnessMap, MetalnessMap
    float       studsPerTile = 10.0f;    // StudsPerTile: how big one copy of the pictures is (Roblox studs)
    // Ours, for when there's no picture for it: 0 smooth .. 1 rough, 0 not metal .. 1 metal,
    // and how much it glows (0 = not at all; the part's colour times its ColorMap).
    float       roughness = 0.5f, metalness = 0.0f, glow = 0.0f;
    // Made with the Material Graph (nodes): the graph, as JSON ("" = made from pictures).
    // The pictures above are what it last baked into.
    std::string graph;
};
inline void clampVariant(MaterialVariantProps& v) {
    auto c = [](float& f, float lo, float hi) { f = (f == f) ? (f < lo ? lo : f > hi ? hi : f) : lo; };
    c(v.studsPerTile, 0.1f, 1000.0f);
    c(v.roughness, 0.0f, 1.0f);
    c(v.metalness, 0.0f, 1.0f);
    c(v.glow, 0.0f, 10.0f);
    for (std::string* t : {&v.colorMap, &v.normalMap, &v.roughnessMap, &v.metalnessMap})
        if (t->size() > 500) t->resize(500);
    if (v.graph.size() > 200000) v.graph.clear();
}

// A custom value on an object, like Roblox Attributes (Properties panel >
// Attributes, or part:SetAttribute("Coins", 5) in a script).
// Tool.Grip's default turn (see SceneNode::gripRot): none, like Roblox's. The Handle's
// long side (+Y) points the way the hand's front faces, so with the arm held out a
// sword points straight up, the classic way.
inline const glm::mat3 kDefaultGripRot{1.0f};
// A SpawnLocation's TeamColor to start with: Roblox's default, Medium stone grey.
inline const glm::vec3 kDefaultTeamColor{0.639f, 0.635f, 0.647f};

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
    // An Attachment that's a Bone (Roblox's Bone): a joint of an armature inside a
    // MeshPart. Turning it bends the mesh around it (see Skin.h). Unlike other
    // children of a part, its position is in studs from the part's middle, and
    // the part's size doesn't stretch it.
    bool                  bone = false;
    bool                  robloxEmbeddedMeshBone = false; // generated from FileMesh, not an authored Roblox Bone instance
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
    std::string materialVariant;             // a MaterialVariant's name in MaterialService ("" = none)

    // Behaviour
    bool     anchored   = true;              // false = falls with gravity in Play
    bool     canCollide = true;              // false = things pass through it
    // Which collision group it's in (PhysicsService; see CollisionGroups.h). A group
    // that isn't registered acts like "Default".
    std::string collisionGroup = "Default";
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
    ClickProps  click;                    // (kind == ClickDetector)
    HighlightProps highlight;             // (kind == Highlight)
    EffectProps effect;                   // (kind == Trail / Beam)
    PostEffectProps post;                 // (kind == PostEffect)
    SkyProps    sky;                      // (kind == Sky)
    MaterialVariantProps variant;         // (kind == MaterialVariant)
    ParticleProps particle;               // (kind == Particles)

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
    uint64_t    soundGroup = 0;            // SoundGroup: the group it plays through (0 = none)
    int         audioHandle = 0;           // runtime only
    // SoundGroup (kind == SoundGroup) uses `volume` (Roblox's default 0.5).
    SoundFxProps soundFx;                  // (kind == SoundEffect)

    // Physics (parts). Negative = use the material's value. Together these are Roblox's
    // CustomPhysicalProperties (PhysicalProperties.new(density, friction, elasticity,
    // frictionWeight, elasticityWeight)); nil there = all three negative.
    float       density    = -1.0f;
    float       friction   = -1.0f;
    float       elasticity = -1.0f;        // bounciness 0..1
    // How much this part's friction / bounciness counts when it touches another part
    // (1 and 1 = the usual mix; bigger = this part's value wins).
    float       frictionWeight   = 1.0f;
    float       elasticityWeight = 1.0f;
    bool hasCustomPhysics() const { return density >= 0.0f || friction >= 0.0f || elasticity >= 0.0f; }

    // Model.PrimaryPart: the part inside it that GetPivot / PivotTo / SetPrimaryPartCFrame
    // go by (0 = none).
    uint64_t    primaryPart = 0;

    // SpawnLocation (a part called SpawnLocation, or tagged "SpawnLocation"), like Roblox's:
    // a Neutral one is for anybody; otherwise only players on the team with this TeamColor
    // spawn there. AllowTeamChangeOnTouch: touching it puts you on that team.
    bool        spawnNeutral = true;
    glm::vec3   teamColor = kDefaultTeamColor;
    bool        allowTeamChange = false;

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
    JointProps  joint;                     // ball socket / prismatic / cylindrical settings
    float       breakForce  = 0.0f;        // BreakForce: pulled harder than this, it snaps (0 = never)
    bool        broken      = false;       // it snapped (and switched itself off) in this play session
    float       jointForce  = 0.0f;        // (play: how hard it's being pulled right now, for Studio's physics view)

    // Decal (kind == Decal): which picture, on which side. `color` tints it
    // (white = as it is), `transparency` fades it.
    std::string texture;                   // "gb:<id>", a file in the games folder, or a path
    std::string tshirt;                    // a character's torso: a T-shirt picture flat on its front ("gb:<id>")
    Face        face = Face::Front;

    // Game UI (kind == Gui). `visible` is Visible; `enabled` is a ScreenGui's Enabled.
    GuiProps    gui;
    // Camera (kind == Camera): a ViewportFrame's CurrentCamera. Its transform is its
    // CFrame (it looks along its -Z); FieldOfView is degrees, top to bottom.
    float       fieldOfView = 70.0f;

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
    bool isSoundGroup()  const { return kind == NodeKind::SoundGroup; }
    bool isSoundEffect() const { return kind == NodeKind::SoundEffect; }
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
    // A ViewportFrame: the parts inside it are only a picture, not in the world
    // (not drawn there, no physics, rays and explosions don't find them).
    bool isViewportFrame() const { return kind == NodeKind::Gui && gui.type == GuiType::ViewportFrame; }
    bool inViewportFrame() const {
        for (const SceneNode* p = parent; p; p = p->parent) if (p->isViewportFrame()) return true;
        return false;
    }
    bool isCamera() const { return kind == NodeKind::Camera; }
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
    bool isBone() const { return kind == NodeKind::Attachment && bone; }
    bool isConstraint() const { return kind == NodeKind::Constraint; }
    bool isMover() const { return kind == NodeKind::Mover; }
    bool isRemote() const { return kind == NodeKind::Remote; }
    bool isPrompt() const { return kind == NodeKind::Prompt; }
    bool isHighlight() const { return kind == NodeKind::Highlight; }
    bool isClickDetector() const { return kind == NodeKind::ClickDetector; }
    bool isEffect() const { return kind == NodeKind::Trail || kind == NodeKind::Beam; }
    bool isPostEffect() const { return kind == NodeKind::PostEffect; }
    bool isSky() const { return kind == NodeKind::Sky; }
    bool isMaterialVariant() const { return kind == NodeKind::MaterialVariant; }
    // A force zone: a part tagged "ForceZone". Loose parts inside it are pushed (attributes
    // Mode = Wind / Push / Pull, Strength in studs/s^2, Direction for Wind).
    bool isForceZone() const {
        return isPart() && std::find(tags.begin(), tags.end(), "ForceZone") != tags.end();
    }
    bool isSpawnLocation() const {
        return isPart() && (name == "SpawnLocation" || std::find(tags.begin(), tags.end(), "SpawnLocation") != tags.end());
    }
    bool isParticles() const { return kind == NodeKind::Particles; }
    const char* particleClass() const { return kParticleClassNames[(int)particle.type]; }
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
    // The space this node's transform is in (its parent's world matrix; for a bone
    // in a part, the part without its size).
    glm::mat4                  parentFrame() const;
    std::string                fullName() const;   // e.g. "Workspace.Door.Script"

    // Make sure freshly created nodes never reuse an id loaded from a file.
    static void     reserveId(uint64_t used);
    static uint64_t newId();
};
