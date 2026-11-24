#include "StandIns.h"

#include <cstring>
#include <limits>

namespace StandIns {
namespace {

Prop B(const char* n, bool v)               { Prop p{n, Type::Bool}; p.n = v ? 1 : 0; return p; }
Prop N(const char* n, double v)             { Prop p{n, Type::Number}; p.n = v; return p; }
Prop S(const char* n, const char* v = "")   { Prop p{n, Type::String}; p.s = v; return p; }
Prop V(const char* n, float x = 0, float y = 0, float z = 0) { Prop p{n, Type::Vector3}; p.x = x; p.y = y; p.z = z; return p; }
Prop V2(const char* n, float x = 0, float y = 0) { Prop p{n, Type::Vector2}; p.x = x; p.y = y; return p; }
Prop C(const char* n, int r, int g, int b)  { Prop p{n, Type::Color3}; p.x = r / 255.0f; p.y = g / 255.0f; p.z = b / 255.0f; return p; }
Prop I(const char* n)                       { return Prop{n, Type::Instance}; }
Prop F(const char* n)                       { return Prop{n, Type::CFrame}; }

const float kInf = std::numeric_limits<float>::infinity();

std::vector<Class> build() {
    std::vector<Prop> handle = {I("Adornee"), B("AlwaysOnTop", false), C("Color3", 13, 105, 172), F("CFrame"),
                                V("SizeRelativeOffset"), N("Transparency", 0), B("Visible", true), N("ZIndex", -1)};
    auto withHandle = [&](std::vector<Prop> extra) { extra.insert(extra.begin(), handle.begin(), handle.end()); return extra; };
    std::vector<Prop> fidelity = {S("RenderFidelity", "Automatic"), S("CollisionFidelity", "Default")};
    auto withFidelity = [&](std::vector<Prop> extra) { extra.insert(extra.end(), fidelity.begin(), fidelity.end()); return extra; };
    std::vector<Prop> operation = withFidelity({B("UsePartColor", false), N("SmoothingAngle", 0)});
    std::vector<Prop> joint = {I("Attachment0"), I("Attachment1"), B("Enabled", true), B("Visible", false), C("Color", 13, 105, 172)};
    auto withJoint = [&](std::vector<Prop> extra) { extra.insert(extra.begin(), joint.begin(), joint.end()); return extra; };

    return {
        // --- values and folders (these work) ---
        {"ObjectValue", Base::Model, true, true, {"ValueBase"}, {I("Value")}},
        {"CFrameValue", Base::Model, true, true, {"ValueBase"}, {F("Value")}},
        {"BrickColorValue", Base::Model, true, true, {"ValueBase"}, {S("Value", "Medium stone grey")}},
        {"RayValue", Base::Model, true, true, {"ValueBase"}, {S("Value", "{0, 0, 0}, {0, 0, 0}")}},
        {"BinaryStringValue", Base::Model, true, true, {"ValueBase"}, {S("Value")}},
        {"Configuration", Base::Model, true, false, {}, {}},
        {"Backpack", Base::Model, true, false, {}, {}},
        {"StarterGear", Base::Model, true, false, {}, {}},
        {"DataStoreOptions", Base::Model, true, false, {}, {B("AllScopes", false)}},
        {"DataStoreSetOptions", Base::Model, true, false, {}, {}},
        {"DataStoreGetOptions", Base::Model, true, false, {}, {B("UseCache", true)}},
        {"DataStoreIncrementOptions", Base::Model, true, false, {}, {}},
        {"GetTextBoundsParams", Base::Model, true, false, {}, {S("Text"), S("Font", "SourceSans"), N("Size", 14), N("Width", 0), B("RichText", false)}},
        {"ExperienceInviteOptions", Base::Model, true, false, {}, {S("PromptMessage"), S("LaunchData"), S("InviteMessageId"), N("InviteUser", 0)}},

        // --- animation holders (LoadAnimation plays on the rig they're in) ---
        {"Animator", Base::Model, true, false, {}, {}},
        {"AnimationController", Base::Model, true, false, {}, {}},

        // --- the sky and the air (these set Lighting) ---
        {"Atmosphere", Base::Model, true, false, {}, {N("Density", 0.395), N("Offset", 0), C("Color", 199, 199, 199),
                                                       C("Decay", 106, 112, 125), N("Glare", 0), N("Haze", 0)}},
        {"Clouds", Base::Model, true, false, {}, {N("Cover", 0.5), N("Density", 0.7), C("Color", 255, 255, 255), B("Enabled", true)}},

        // --- parts ---
        {"MeshPart", Base::Part, true, false, {"BasePart", "TriangleMeshPart", "PVInstance"},
         withFidelity({S("MeshId"), S("TextureID"), B("DoubleSided", false), V("MeshSize", 1, 1, 1)})},
        {"UnionOperation", Base::Part, true, false, {"PartOperation", "TriangleMeshPart", "BasePart", "PVInstance"}, operation},
        {"IntersectOperation", Base::Part, true, false, {"PartOperation", "TriangleMeshPart", "BasePart", "PVInstance"}, operation},
        {"NegateOperation", Base::Part, true, false, {"PartOperation", "TriangleMeshPart", "BasePart", "PVInstance"}, operation},
        {"PartOperation", Base::Part, true, false, {"TriangleMeshPart", "BasePart", "PVInstance"}, operation},
        {"CornerWedgePart", Base::Part, true, false, {"BasePart", "PVInstance"}, {}},

        // --- other things we have under another name ---
        {"UnreliableRemoteEvent", Base::Remote, true, false, {"BaseRemoteEvent"}, {}},
        {"SurfaceLight", Base::SpotLight, true, false, {"Light"}, {S("Face", "Front")}},
        {"Texture", Base::Decal, true, false, {"Decal", "FaceInstance"},
         {N("StudsPerTileU", 2), N("StudsPerTileV", 2), N("OffsetStudsU", 0), N("OffsetStudsV", 0)}},
        {"CanvasGroup", Base::Frame, true, false, {"GuiObject", "GuiBase2d", "GuiBase"},
         {N("GroupTransparency", 0), C("GroupColor3", 255, 255, 255)}},

        // --- a character's look ---
        {"BodyColors", Base::Model, true, false, {}, {C("HeadColor3", 245, 205, 48), C("TorsoColor3", 13, 105, 172),
                                                       C("LeftArmColor3", 245, 205, 48), C("RightArmColor3", 245, 205, 48),
                                                       C("LeftLegColor3", 164, 189, 71), C("RightLegColor3", 164, 189, 71)}},
        {"Shirt", Base::Model, false, false, {"Clothing", "CharacterAppearance"}, {S("ShirtTemplate"), C("Color3", 255, 255, 255)}},
        {"Pants", Base::Model, false, false, {"Clothing", "CharacterAppearance"}, {S("PantsTemplate"), C("Color3", 255, 255, 255)}},
        {"ShirtGraphic", Base::Model, false, false, {"CharacterAppearance"}, {S("Graphic"), C("Color3", 255, 255, 255)}},
        {"CharacterMesh", Base::Model, false, false, {"CharacterAppearance"}, {S("BodyPart", "Head"), N("MeshId", 0), N("BaseTextureId", 0), N("OverlayTextureId", 0)}},
        {"Accessory", Base::Model, false, false, {"Accoutrement"}, {F("AttachmentPoint"), S("AccessoryType", "Unknown")}},
        {"Accoutrement", Base::Model, false, false, {}, {F("AttachmentPoint")}},

        // --- 3D outlines and handles ---
        {"SelectionBox", Base::Model, false, false, {"InstanceAdornment", "PVAdornment", "GuiBase3d", "GuiBase"},
         {I("Adornee"), C("Color3", 13, 105, 172), N("LineThickness", 0.15), C("SurfaceColor3", 13, 105, 172),
          N("SurfaceTransparency", 1), N("Transparency", 0), B("Visible", true)}},
        {"SelectionSphere", Base::Model, false, false, {"PVAdornment", "GuiBase3d", "GuiBase"},
         {I("Adornee"), C("Color3", 13, 105, 172), C("SurfaceColor3", 13, 105, 172), N("SurfaceTransparency", 1),
          N("Transparency", 0), B("Visible", true)}},
        {"BoxHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({V("Size", 1, 1, 1)})},
        {"SphereHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({N("Radius", 1)})},
        {"CylinderHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"},
         withHandle({N("Height", 1), N("Radius", 1), N("InnerRadius", 0), N("Angle", 360)})},
        {"ConeHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({N("Height", 2), N("Radius", 0.5)})},
        {"LineHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({N("Length", 5), N("Thickness", 1)})},
        {"ImageHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({S("Image"), V2("Size", 1, 1)})},
        {"PyramidHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({N("Height", 2), N("Size", 1), N("Sides", 4)})},
        {"WireframeHandleAdornment", Base::Model, false, false, {"HandleAdornment", "PVAdornment", "GuiBase3d", "GuiBase"}, withHandle({N("Thickness", 1)})},
        {"Handles", Base::Model, false, false, {"HandlesBase", "PartAdornment", "GuiBase3d", "GuiBase"},
         {I("Adornee"), C("Color3", 13, 105, 172), S("Style", "Resize"), B("Visible", true)}},
        {"ArcHandles", Base::Model, false, false, {"HandlesBase", "PartAdornment", "GuiBase3d", "GuiBase"},
         {I("Adornee"), C("Color3", 13, 105, 172), B("Visible", true)}},
        {"SurfaceSelection", Base::Model, false, false, {"PartAdornment", "GuiBase3d", "GuiBase"},
         {I("Adornee"), C("Color3", 13, 105, 172), S("TargetSurface", "Right"), N("Transparency", 0), B("Visible", true)}},

        // --- old-style animations (KeyframeSequence) ---
        {"KeyframeSequence", Base::Model, false, false, {"AnimationClip"}, {B("Loop", true), S("Priority", "Action")}},
        {"Keyframe", Base::Model, false, false, {}, {N("Time", 0)}},
        {"Pose", Base::Model, false, false, {"PoseBase"}, {F("CFrame"), N("Weight", 1), S("EasingStyle", "Linear"), S("EasingDirection", "In")}},
        {"NumberPose", Base::Model, false, false, {"PoseBase"}, {N("Value", 0), N("Weight", 1), S("EasingStyle", "Linear"), S("EasingDirection", "In")}},
        {"KeyframeMarker", Base::Model, false, false, {}, {S("Value")}},
        {"CurveAnimation", Base::Model, false, false, {"AnimationClip"}, {B("Loop", true), S("Priority", "Action")}},
        {"FloatCurve", Base::Model, false, false, {}, {}},
        {"RotationCurve", Base::Model, false, false, {}, {}},
        {"Vector3Curve", Base::Model, false, false, {}, {}},
        {"EulerRotationCurve", Base::Model, false, false, {}, {S("RotationOrder", "XYZ")}},
        {"MarkerCurve", Base::Model, false, false, {}, {}},

        // --- NPC talk bubbles ---
        {"Dialog", Base::Model, false, false, {}, {S("InitialPrompt"), S("GoodbyeDialog"), S("Purpose", "Help"), S("Tone", "Neutral"),
                                                   N("ConversationDistance", 25), B("InUse", false), S("BehaviorType", "SinglePlayer"),
                                                   N("TriggerDistance", 0), V("TriggerOffset")}},
        {"DialogChoice", Base::Model, false, false, {}, {S("UserDialog"), S("ResponseDialog"), S("GoodbyeDialog"), B("GoodbyeChoiceActive", true)}},

        // --- game UI helpers ---
        {"UISizeConstraint", Base::Model, false, false, {"UIConstraint", "UIComponent", "UIBase"}, {V2("MinSize", 0, 0), V2("MaxSize", kInf, kInf)}},
        {"UITextSizeConstraint", Base::Model, false, false, {"UIConstraint", "UIComponent", "UIBase"}, {N("MinTextSize", 1), N("MaxTextSize", 100)}},
        {"UIPageLayout", Base::Model, false, false, {"UIGridStyleLayout", "UILayout", "UIComponent", "UIBase"},
         {B("Animated", true), B("Circular", false), S("EasingStyle", "Back"), S("EasingDirection", "Out"), N("TweenTime", 1),
          S("FillDirection", "Horizontal"), S("SortOrder", "LayoutOrder"), B("GamepadInputEnabled", true), B("ScrollWheelInputEnabled", true),
          B("TouchInputEnabled", true), I("CurrentPage")}},
        {"UITableLayout", Base::Model, false, false, {"UIGridStyleLayout", "UILayout", "UIComponent", "UIBase"},
         {B("FillEmptySpaceColumns", false), B("FillEmptySpaceRows", false), S("MajorAxis", "RowMajor"), S("SortOrder", "LayoutOrder"),
          S("FillDirection", "Horizontal")}},
        {"UIFlexItem", Base::Model, false, false, {"UIComponent", "UIBase"},
         {S("FlexMode", "None"), N("GrowRatio", 0), N("ShrinkRatio", 0), S("ItemLineAlignment", "Automatic")}},
        {"UIDragDetector", Base::Model, false, false, {"UIComponent", "UIBase"},
         {B("Enabled", true), S("DragStyle", "TranslatePlane"), S("ResponseStyle", "Offset"), I("BoundingUI")}},

        // --- physics we don't have yet ---
        {"NoCollisionConstraint", Base::Model, false, false, {}, {I("Part0"), I("Part1"), B("Enabled", true)}},
        {"RigidConstraint", Base::Model, false, false, {"Constraint"}, withJoint({B("DestructionEnabled", false), N("DestructionForce", 0), N("DestructionTorque", 0)})},
        {"PlaneConstraint", Base::Model, false, false, {"Constraint"}, joint},
        {"UniversalConstraint", Base::Model, false, false, {"Constraint"}, withJoint({B("LimitsEnabled", false), N("MaxAngle", 45), N("Radius", 0.2)})},
        {"TorsionSpringConstraint", Base::Model, false, false, {"Constraint"},
         withJoint({N("Stiffness", 100), N("Damping", 0.01), N("MaxTorque", kInf), B("LimitsEnabled", false), N("MaxAngle", 45), N("Restitution", 0)})},
        {"LineForce", Base::Model, false, false, {"Constraint"},
         withJoint({N("Magnitude", 1000), B("ApplyAtCenterOfMass", false), B("InverseSquareLaw", false), N("MaxForce", kInf), B("ReactionForceEnabled", false)})},
        {"AnimationConstraint", Base::Model, false, false, {"Constraint"}, withJoint({N("MaxForce", 10000), N("MaxTorque", 10000), F("Transform"), B("IsKinematic", false)})},
        {"VelocityMotor", Base::Model, false, false, {"JointInstance"}, {I("Part0"), I("Part1"), N("MaxVelocity", 0), N("DesiredAngle", 0), I("Hole")}},
        {"BuoyancySensor", Base::Model, false, false, {"SensorBase"}, {B("FullySubmerged", false), B("TouchingSurface", false), S("UpdateType", "OnRead")}},
        {"ControllerPartSensor", Base::Model, false, false, {"SensorBase"}, {I("SensedPart"), V("HitFrame"), V("HitNormal"), N("SearchDistance", 0)}},
        {"FluidForceSensor", Base::Model, false, false, {"SensorBase"}, {V("CenterOfPressure"), V("Force"), V("Torque")}},
        {"AtmosphereSensor", Base::Model, false, false, {"SensorBase"}, {N("AirDensity", 0), V("RelativeWindVelocity")}},

        // --- pathfinding hints ---
        {"PathfindingModifier", Base::Model, false, false, {}, {S("Label"), B("PassThrough", false)}},
        {"PathfindingLink", Base::Model, false, false, {}, {I("Attachment0"), I("Attachment1"), B("IsBidirectional", true), S("Label")}},

        // --- chat, words, haptics, new input ---
        {"TextChannel", Base::Model, false, false, {}, {}},
        {"TextChatCommand", Base::Model, false, false, {}, {S("PrimaryAlias"), S("SecondaryAlias"), B("Enabled", true), B("AutocompleteVisible", true)}},
        {"TextChatMessageProperties", Base::Model, false, false, {}, {S("PrefixText"), S("Text"), S("Translation")}},
        {"BubbleChatMessageProperties", Base::Model, false, false, {}, {C("BackgroundColor3", 250, 250, 250), N("BackgroundTransparency", 0.1),
                                                                         C("TextColor3", 57, 59, 61), N("TextSize", 16)}},
        {"LocalizationTable", Base::Model, false, false, {}, {S("SourceLocaleId", "en-us")}},
        {"HapticEffect", Base::Model, false, false, {}, {S("Type", "GameplayCollision"), B("Looped", false), N("Radius", 0), V("Position")}},
        {"InputContext", Base::Model, false, false, {}, {B("Enabled", true), N("Priority", 1000), B("Sink", false)}},
        {"InputAction", Base::Model, false, false, {}, {B("Enabled", true), S("Type", "Bool")}},
        {"InputBinding", Base::Model, false, false, {}, {S("KeyCode", "Unknown"), S("PrimaryModifier", "Unknown"), S("SecondaryModifier", "Unknown"),
                                                         S("Up", "Unknown"), S("Down", "Unknown"), S("Left", "Unknown"), S("Right", "Unknown"), N("Scale", 1)}},
        {"InputActionLabel", Base::Model, false, false, {}, {}},
        {"CustomLog", Base::Model, false, false, {}, {}},
        {"Path2D", Base::Model, false, false, {"GuiBase"}, {C("Color3", 0, 0, 0), N("Thickness", 1), B("Visible", true), N("ZIndex", 1)}},
    };
}

} // namespace

const std::vector<Class>& all() {
    static const std::vector<Class> classes = build();
    return classes;
}

const Class* find(const std::string& name) {
    for (const Class& c : all())
        if (name == c.name) return &c;
    return nullptr;
}

const Prop* prop(const Class* c, const char* name) {
    if (!c) return nullptr;
    for (const Prop& p : c->props)
        if (std::strcmp(p.name, name) == 0) return &p;
    return nullptr;
}

} // namespace StandIns
