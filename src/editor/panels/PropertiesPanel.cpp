#include "PropertiesPanel.h"
#include "../MaterialUi.h"
#include "../BoneUi.h"
#include "../../core/I18n.h"
#include "../../online/Protocol.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../renderer/MeshLibrary.h"
#include "../../core/Audio.h"
#include "../../scene/SoundMix.h"
#include "../../core/FileDialog.h"
#include "../../core/Paths.h"
#include "../../renderer/Textures.h"
#include "../../scene/EditMesh.h"
#include "../../scene/Serializer.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <nlohmann/json.hpp>
#include <misc/cpp/imgui_stdlib.h>

PropertiesPanel::PropertiesPanel(Scene* scene, std::function<void(SceneNode*)> openScript)
    : m_scene(scene), m_openScript(std::move(openScript)) {}

// The class name scripts see (part.ClassName).
const char* PropertiesPanel::className(const SceneNode* node) const {
    return node == m_scene->root()          ? "Workspace"
           : node->kind == NodeKind::Script   ? (node->isModule ? "ModuleScript" : node->isLocal ? "LocalScript" : "Script")
           : node->kind == NodeKind::Remote   ? (node->remoteFunction ? "RemoteFunction" : "RemoteEvent")
           : node->kind == NodeKind::Prompt   ? "ProximityPrompt"
           : node->kind == NodeKind::Highlight ? "Highlight"
           : node->kind == NodeKind::PostEffect ? kPostEffectClassNames[(int)node->post.type]
           : node->kind == NodeKind::Sky      ? "Sky"
           : node->kind == NodeKind::MaterialVariant ? "MaterialVariant"
           : node->kind == NodeKind::ClickDetector ? "ClickDetector"
           : node->kind == NodeKind::Trail    ? "Trail"
           : node->kind == NodeKind::Beam     ? "Beam"
           : node->kind == NodeKind::Particles ? node->particleClass()
           : node->kind == NodeKind::Light    ? (node->lightType == LightType::Spot ? "SpotLight" : "PointLight")
           : node->kind == NodeKind::Sound    ? "Sound"
           : node->kind == NodeKind::SoundGroup ? "SoundGroup"
           : node->kind == NodeKind::SoundEffect ? SoundFx::kClassNames[(int)node->soundFx.type]
           : node->kind == NodeKind::Attachment ? (node->bone ? "Bone" : "Attachment")
           : node->kind == NodeKind::Constraint ? "Constraint"
           : node->kind == NodeKind::ForceField ? "ForceField"
           : node->kind == NodeKind::Tool       ? "Tool"
           : node->kind == NodeKind::Value      ? node->valueClass()
           : node->kind == NodeKind::Decal      ? "Decal"
           : node->kind == NodeKind::Animation  ? "Animation"
           : node->kind == NodeKind::FluidSystem  ? "FluidSystem"
           : node->kind == NodeKind::FluidEmitter ? "FluidEmitter"
           : node->kind == NodeKind::Mover        ? kMoverClassNames[(int)node->mover.type]
           : node->kind == NodeKind::Gui        ? kGuiClassNames[(int)node->gui.type]
           : node->kind == NodeKind::Camera     ? "Camera"
           : node->kind == NodeKind::Model    ? "Model" : "Part";
}

void PropertiesPanel::render() {
    ImGui::Begin("Properties");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##propertyFilter", "Search properties...", &m_propertyFilter);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search common properties by name, like Position, Anchored, Color or Transparency.");

    SceneNode* node = m_scene->selected();
    if (!node) {
        ImGui::TextDisabled("No object selected.");
        ImGui::TextDisabled("Pick one in the Explorer or Viewport.");
        ImGui::End();
        return;
    }
    // Several picked: what you change here is copied to every other one of the same kind.
    const std::vector<SceneNode*>& sel = m_scene->selection();
    const char* cls = className(node);
    std::string before;
    if (sel.size() > 1) {
        int same = 0;
        for (SceneNode* s : sel) same += std::strcmp(className(s), cls) == 0;
        before = Serializer::nodeShallowToString(*node);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.0f, 1.0f));
        if (same == (int)sel.size()) ImGui::TextWrapped("%d selected: a change here changes all of them.", same);
        else ImGui::TextWrapped("%d selected: a change here changes the %d that are the same kind as this one.", (int)sel.size(), same);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Only the boxes you change are copied; everything else keeps its own value.\n"
                              "For more (find by tag or value, do maths on numbers) use Edit > Find and Change.");
    }
    // Properties keep the names scripts use (part.Transparency), in every language.
    I18n::Pause keep;
    ImGui::Separator();
    ImGui::Text("%s", node->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", cls);

    // Searching switches to a compact two-column inspector, similar to Roblox Studio's
    // Properties panel. Normal browsing keeps the complete class-specific property editor.
    if (!m_propertyFilter.empty()) {
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char ch) { return (char)std::tolower(ch); });
            return s;
        };
        const std::string query = lower(m_propertyFilter);
        auto matches = [&](const char* label) { return lower(label).find(query) != std::string::npos; };
        bool any = false;
        if (ImGui::BeginTable("##filteredProperties", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 112.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            auto row = [&](const char* label, auto&& draw) {
                if (!matches(label)) return;
                any = true;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1.0f);
                draw();
            };
            row("Name", [&] {
                ImGui::BeginDisabled(m_scene->isProtected(node));
                ImGui::InputText("##Name", &node->name);
                ImGui::EndDisabled();
            });
            row("ClassName", [&] { ImGui::TextUnformatted(cls); });
            row("Position", [&] { ImGui::DragFloat3("##Position", &node->transform.position.x, 0.05f, 0.0f, 0.0f, "%.3f"); });
            row("Rotation", [&] { ImGui::DragFloat3("##Rotation", &node->transform.rotation.x, 0.5f, 0.0f, 0.0f, "%.2f"); });
            row("Size", [&] { ImGui::DragFloat3("##Size", &node->transform.scale.x, 0.05f, 0.001f, 1000.0f, "%.3f"); });
            row("Visible", [&] { ImGui::Checkbox("##Visible", &node->visible); });
            row("Enabled", [&] { ImGui::Checkbox("##Enabled", &node->enabled); });
            if (node->isPart()) {
                row("Anchored", [&] { ImGui::Checkbox("##Anchored", &node->anchored); });
                row("CanCollide", [&] { ImGui::Checkbox("##CanCollide", &node->canCollide); });
                row("CastShadow", [&] { ImGui::Checkbox("##CastShadow", &node->castShadow); });
                row("Color", [&] { ImGui::ColorEdit3("##Color", &node->color.x, ImGuiColorEditFlags_NoLabel); });
                row("Transparency", [&] { ImGui::SliderFloat("##Transparency", &node->transparency, 0.0f, 1.0f, "%.2f"); });
                row("Texture", [&] { ImGui::InputText("##Texture", &node->texture); });
            }
            ImGui::EndTable();
        }
        if (!any) {
            ImGui::Spacing();
            ImGui::TextDisabled("No quick-search match. Clear the search to browse every property.");
        }
        if (node != m_scene->root()) renderAttributes(node);
        if (!before.empty() && m_scene->selected() == node) copyToSelection(node, before);
        ImGui::End();
        return;
    }

    renderProperties(node);
    if (node != m_scene->root()) renderAttributes(node);
    if (!before.empty() && m_scene->selected() == node) copyToSelection(node, before);   // (a button may have swapped it)
    ImGui::End();
}

// What changed on `node` this frame (compared with `before`) goes to the other selected
// objects of its kind too. Only the changed values are copied, so their own positions,
// names and so on stay as they are. It's all in one frame, so one Ctrl+Z undoes it.
void PropertiesPanel::copyToSelection(SceneNode* node, const std::string& before) {
    const std::string after = Serializer::nodeShallowToString(*node);
    if (after == before) return;
    const nlohmann::json a = nlohmann::json::parse(before, nullptr, false), b = nlohmann::json::parse(after, nullptr, false);
    if (a.is_discarded() || b.is_discarded()) return;
    nlohmann::json ops = nlohmann::json::array();
    for (const auto& op : nlohmann::json::diff(a, b)) {
        const std::string path = op.value("path", "");
        if (path == "/id" || path.rfind("/id/", 0) == 0) continue;   // every object keeps its own id
        ops.push_back(op);
    }
    if (ops.empty()) return;
    const char* cls = className(node);
    for (SceneNode* other : std::vector<SceneNode*>(m_scene->selection())) {
        if (other == node || other == m_scene->root() || std::strcmp(className(other), cls) != 0) continue;
        nlohmann::json o = nlohmann::json::parse(Serializer::nodeShallowToString(*other), nullptr, false);
        if (o.is_discarded()) continue;
        for (const auto& op : ops) {
            try { o = o.patch(nlohmann::json::array({op})); } catch (...) {}   // (a value it doesn't have: skip)
        }
        Serializer::applyNodeShallow(*other, o.dump());
    }
}

// Game UI objects: sizes and places as UDim2 (a fraction of the parent + pixels).
namespace {
bool editUDim2(const char* label, UDim2& u, float scaleStep = 0.005f) {
    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    changed |= ImGui::DragFloat2("X (scale, pixels)", &u.xs, 1.0f, 0, 0, "%.3f");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    changed |= ImGui::DragFloat2("Y (scale, pixels)", &u.ys, 1.0f, 0, 0, "%.3f");
    (void)scaleStep;
    ImGui::PopID();
    return changed;
}
} // namespace

void PropertiesPanel::renderGui(SceneNode* node) {
    GuiProps& g = node->gui;
    const bool noSwitch = g.type == GuiType::UICorner || g.type == GuiType::UIPadding || isGuiLayout(g.type) ||
                          g.type == GuiType::UIScale || g.type == GuiType::UIAspectRatioConstraint;   // (always on)
    const bool hasEnabled = isGuiLayer(g.type) || (isGuiModifier(g.type) && !noSwitch);
    if (!noSwitch)
        ImGui::Checkbox(hasEnabled ? "Enabled" : "Visible", hasEnabled ? &node->enabled : &node->visible);
    static const char* const kH[] = {"Left", "Center", "Right"};
    static const char* const kV[] = {"Top", "Center", "Bottom"};
    if (isGuiLayout(g.type)) {
        const bool grid = g.type == GuiType::UIGridLayout;
        ImGui::SeparatorText(grid ? "UIGridLayout" : "UIListLayout");
        const char* dirs[] = {"Vertical", "Horizontal"};
        ImGui::Combo("FillDirection", &g.fill, dirs, 2);
        ImGui::Combo("HorizontalAlignment", &g.hAlign, kH, 3);
        ImGui::Combo("VerticalAlignment", &g.vAlign, kV, 3);
        int sort = g.sortByName ? 1 : 0;
        const char* sorts[] = {"LayoutOrder", "Name"};
        if (ImGui::Combo("SortOrder", &sort, sorts, 2)) g.sortByName = sort == 1;
        if (grid) {
            editUDim2("CellSize", g.cellSize);
            editUDim2("CellPadding", g.padding);
            ImGui::InputInt("FillDirectionMaxCells", &g.maxCells);
            if (g.maxCells < 0) g.maxCells = 0;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How many in a row (0 = as many as fit).");
        } else {
            ImGui::DragFloat2("Padding (scale, pixels)", &g.padding.xs, 0.5f, 0.0f, 0.0f, "%.2f");
            g.padding.ys = g.padding.xs; g.padding.yo = g.padding.xo;
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(grid ? "Puts everything next to it in a grid of same-size cells (their Position and Size are ignored)."
                                 : "Lines up everything next to it, one after another (their Position is ignored). "
                                   "Lower LayoutOrder comes first.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UIPadding) {
        ImGui::SeparatorText("UIPadding");
        static const char* const kSides[4] = {"PaddingLeft", "PaddingTop", "PaddingRight", "PaddingBottom"};
        for (int i = 0; i < 4; ++i) {
            float v[2] = {g.padScale[i], g.padPx[i]};
            if (ImGui::DragFloat2(kSides[i], v, 0.5f, 0.0f, 0.0f, "%.2f")) { g.padScale[i] = v[0]; g.padPx[i] = v[1]; }
        }
        ImGui::TextDisabled("Keeps what's inside away from the edges (scale, pixels).");
        return;
    }
    if (g.type == GuiType::BillboardGui || g.type == GuiType::SurfaceGui) {
        const bool bb = g.type == GuiType::BillboardGui;
        ImGui::SeparatorText(bb ? "BillboardGui" : "SurfaceGui");
        if (bb) {
            editUDim2("Size", g.size);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pixels stay the same size on screen; scale is studs (grows as you get closer).");
            ImGui::DragFloat3("StudsOffset", &g.studsOffset.x, 0.1f, -100.0f, 100.0f, "%.1f");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves it from the part's middle: right, up, towards the camera.");
            ImGui::DragFloat3("StudsOffsetWorldSpace", &g.worldOffset.x, 0.1f, -100.0f, 100.0f, "%.1f");
        } else {
            ImGui::Combo("Face", &g.face, kFaceNames, 6);
            int mode = g.perStud ? 1 : 0;
            const char* modes[] = {"FixedSize", "PixelsPerStud"};
            if (ImGui::Combo("SizingMode", &mode, modes, 2)) g.perStud = mode == 1;
            if (g.perStud) ImGui::DragFloat("PixelsPerStud", &g.pixelsPerStud, 0.5f, 1.0f, 1000.0f, "%.0f");
            else ImGui::DragFloat2("CanvasSize", &g.surfaceCanvas.x, 1.0f, 1.0f, 10000.0f, "%.0f");
        }
        ImGui::Checkbox("AlwaysOnTop", &g.alwaysOnTop);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show it even when something's in front of it.");
        ImGui::DragFloat("MaxDistance", &g.maxDistance, 1.0f, 0.0f, 100000.0f, g.maxDistance > 0 ? "%.0f" : "no limit");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(bb ? "Put it inside a part (or set Adornee in a script) and it floats over it, facing you: "
                                 "great for name signs and health bars. Insert labels and buttons into it."
                               : "Put it inside a part and it's painted on one side of it (Face): signs, screens and "
                                 "buttons on walls. Insert labels and buttons into it.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::ScreenGui) {
        ImGui::InputInt("DisplayOrder", &g.displayOrder);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("ScreenGuis with a higher number are drawn on top.");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("A ScreenGui is a layer of UI over the game. Insert Frames, TextLabels, TextButtons and "
                            "ImageLabels inside it. Click them in the viewport to pick them, drag to move, drag the "
                            "blue corner to resize.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UICorner) {
        ImGui::SeparatorText("UICorner");
        ImGui::DragFloat2("CornerRadius (scale, pixels)", &g.corner.xs, 0.5f, 0.0f, 500.0f, "%.2f");
        ImGui::TextDisabled("Rounds the corners of the object it's in.");
        // Each corner on its own: tick one to give it its own roundness (0 = a sharp corner).
        static const char* const kNames[4] = {"TopLeft", "TopRight", "BottomRight", "BottomLeft"};
        ImGui::SeparatorText("Corners");
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            bool own = g.corners[i] >= 0;
            if (ImGui::Checkbox("##own", &own)) {
                g.corners[i] = own ? g.corner.xo : -1.0f;
                g.cornerScales[i] = own ? g.corner.xs : 0.0f;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Round this corner its own way (unticked: the same as CornerRadius).");
            ImGui::SameLine();
            ImGui::BeginDisabled(!own);
            float v[2] = {own ? g.cornerScales[i] : g.corner.xs, own ? g.corners[i] : g.corner.xo};
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            if (ImGui::DragFloat2(kNames[i], v, 0.5f, 0.0f, 500.0f, "%.2f") && own) {
                g.cornerScales[i] = std::max(0.0f, v[0]);
                g.corners[i] = std::max(0.0f, v[1]);
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        return;
    }
    if (g.type == GuiType::UIShadow) {
        ImGui::SeparatorText("UIShadow");
        ImGui::ColorEdit3("Color", &g.bg.x);
        ImGui::SliderFloat("Transparency", &g.bgTransparency, 0.0f, 1.0f);
        ImGui::DragFloat2("Offset", &g.shadowOffset.x, 0.5f, -200.0f, 200.0f, "%.1f");
        ImGui::DragFloat("Blur", &g.shadowBlur, 0.5f, 0.0f, 100.0f, "%.1f");
        ImGui::DragFloat("Spread", &g.shadowSpread, 0.5f, -100.0f, 100.0f, "%.1f");
        ImGui::TextDisabled("A soft drop shadow under the object it's in.");
        return;
    }
    if (g.type == GuiType::UIBlur) {
        ImGui::SeparatorText("UIBlur");
        ImGui::DragFloat("Size", &g.blurSize, 0.5f, 0.0f, 100.0f, "%.1f");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Blurs the world behind the object it's in (frosted glass). Make its "
                            "BackgroundTransparency 0.3 - 0.7 to tint the glass.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UIGradient) {
        ImGui::SeparatorText("UIGradient");
        // Color / Transparency: the two ends here (scripts can give it more keypoints).
        const bool many = g.gradColor.size() > 2 || g.gradTransparency.size() > 2;
        glm::vec3 c0 = g.gradColor.front().c, c1 = g.gradColor.back().c;
        bool cEdit = ImGui::ColorEdit3("Color (start)", &c0.x);
        cEdit |= ImGui::ColorEdit3("Color (end)", &c1.x);
        if (cEdit) g.gradColor = {{0.0f, c0}, {1.0f, c1}};
        float t0 = g.gradTransparency.front().v, t1 = g.gradTransparency.back().v;
        bool tEdit = ImGui::SliderFloat("Transparency (start)", &t0, 0.0f, 1.0f);
        tEdit |= ImGui::SliderFloat("Transparency (end)", &t1, 0.0f, 1.0f);
        if (tEdit) g.gradTransparency = {{0.0f, t0}, {1.0f, t1}};
        if (many) ImGui::TextDisabled("(it has more colours, set by a script: editing here keeps just the two ends)");
        ImGui::DragFloat("Rotation", &g.gradRotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = left to right, 90 = top to bottom.");
        ImGui::DragFloat2("Offset", &g.gradOffset.x, 0.01f, -2.0f, 2.0f, "%.2f");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Colours the object it's in (its background, text and picture) with a fade "
                            "from one colour to the next.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UIScale) {
        ImGui::SeparatorText("UIScale");
        ImGui::DragFloat("Scale", &g.scale, 0.01f, 0.0f, 10.0f, "%.2f");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Makes the object it's in, and everything inside it, bigger (2 = twice as big) or smaller. "
                            "It grows around its AnchorPoint.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UIAspectRatioConstraint) {
        ImGui::SeparatorText("UIAspectRatioConstraint");
        if (ImGui::DragFloat("AspectRatio", &g.aspectRatio, 0.01f, 0.01f, 100.0f, "%.2f")) g.aspectRatio = std::max(0.01f, g.aspectRatio);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Width / height: 1 = square, 2 = twice as wide as tall.");
        const char* types[] = {"FitWithinMaxSize", "ScaleWithParentSize"};
        ImGui::Combo("AspectType", &g.aspectType, types, 2);
        const char* axes[] = {"Width", "Height"};
        ImGui::BeginDisabled(g.aspectType == 0);
        ImGui::Combo("DominantAxis", &g.dominantAxis, axes, 2);
        ImGui::EndDisabled();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Keeps the object it's in the same shape on every screen. FitWithinMaxSize: as big as fits "
                            "in its Size. ScaleWithParentSize: DominantAxis keeps its size and the other side follows.");
        ImGui::PopTextWrapPos();
        return;
    }
    if (g.type == GuiType::UIStroke) {
        ImGui::SeparatorText("UIStroke");
        ImGui::ColorEdit3("Color", &g.borderColor.x);
        ImGui::DragFloat("Thickness", &g.thickness, 0.1f, 0.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("Transparency", &g.bgTransparency, 0.0f, 1.0f);
        ImGui::TextDisabled("An outline around the object it's in.");
        return;
    }

    ImGui::SeparatorText("Layout");
    editUDim2("Position", g.pos);
    editUDim2("Size", g.size);
    ImGui::DragFloat2("AnchorPoint", &g.anchor.x, 0.01f, 0.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Which point of it sits at Position: 0,0 = top-left, 0.5,0.5 = middle.");
    ImGui::InputInt("ZIndex", &g.zIndex);
    ImGui::InputInt("LayoutOrder", &g.layoutOrder);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("With a UIListLayout or UIGridLayout: lower numbers come first.");
    ImGui::Checkbox("ClipsDescendants", &g.clips);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Hide the parts of things inside it that stick out.");
    ImGui::TextDisabled("Scale 1 = the whole of the parent; pixels are added on top.");

    ImGui::SeparatorText("Background");
    ImGui::ColorEdit3("BackgroundColor3", &g.bg.x);
    ImGui::SliderFloat("BackgroundTransparency", &g.bgTransparency, 0.0f, 1.0f);
    ImGui::ColorEdit3("BorderColor3", &g.borderColor.x);
    ImGui::SliderInt("BorderSizePixel", &g.border, 0, 10);

    if (g.type == GuiType::ScrollingFrame) {
        ImGui::SeparatorText("Scrolling");
        editUDim2("CanvasSize", g.canvasSize);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big the inside is. Scale 2 on Y = twice as tall as the frame.");
        const char* axes[] = {"None", "X", "Y", "XY"};
        ImGui::Combo("AutomaticCanvasSize", &g.autoCanvas, axes, 4);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grow the canvas to fit what's inside.");
        int dir = g.scrollDir - 1;
        if (ImGui::Combo("ScrollingDirection", &dir, axes + 1, 3)) g.scrollDir = dir + 1;
        ImGui::DragFloat2("CanvasPosition", &g.canvasPos.x, 1.0f, 0.0f, 100000.0f, "%.0f");
        ImGui::SliderInt("ScrollBarThickness", &g.scrollBar, 0, 30);
        ImGui::ColorEdit3("ScrollBarImageColor3", &g.scrollColor.x);
        ImGui::Checkbox("ScrollingEnabled", &g.scrolling);
    }
    if (guiHasText(g.type)) {
        ImGui::SeparatorText("Text");
        ImGui::InputTextMultiline("Text", &g.text, ImVec2(-1, ImGui::GetTextLineHeight() * 3));
        ImGui::ColorEdit3("TextColor3", &g.textColor.x);
        ImGui::DragFloat("TextSize", &g.textSize, 0.5f, 4.0f, 200.0f, "%.0f");
        ImGui::Checkbox("TextScaled", &g.textScaled);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make the text as big as fits in the box.");
        ImGui::SameLine();
        ImGui::Checkbox("TextWrapped", &g.textWrapped);
        ImGui::SameLine();
        ImGui::Checkbox("Bold", &g.bold);
        const char* xs[] = {"Left", "Center", "Right"};
        const char* ys[] = {"Top", "Center", "Bottom"};
        ImGui::Combo("TextXAlignment", &g.xAlign, xs, 3);
        ImGui::Combo("TextYAlignment", &g.yAlign, ys, 3);
        ImGui::SliderFloat("TextTransparency", &g.textTransparency, 0.0f, 1.0f);
        ImGui::ColorEdit3("TextStrokeColor3", &g.strokeColor.x);
        ImGui::SliderFloat("TextStrokeTransparency", &g.strokeTransparency, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0 = a solid outline around the letters, 1 = none.");
    }
    if (g.type == GuiType::TextBox) {
        ImGui::SeparatorText("TextBox");
        ImGui::InputText("PlaceholderText", &g.placeholder);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shown greyed out while nothing's typed.");
        ImGui::ColorEdit3("PlaceholderColor3", &g.placeholderColor.x);
        ImGui::Checkbox("ClearTextOnFocus", &g.clearOnFocus);
        ImGui::SameLine();
        ImGui::Checkbox("TextEditable", &g.editable);
        ImGui::SameLine();
        ImGui::Checkbox("MultiLine", &g.multiLine);
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("In a script: box.FocusLost:Connect(function(enterPressed) print(box.Text) end)");
        ImGui::PopTextWrapPos();
    }
    if (g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) {
        ImGui::SeparatorText("Image");
        ImGui::InputText("Image", &g.image);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A picture: a .png / .jpg in the games folder, a full path,\nor gb:<id> for one uploaded on the Create page.");
        if (FileDialog::available()) {
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string path = FileDialog::openImage("Pick a picture");
                if (!path.empty()) { g.image = Paths::relativeToGames(path); m_scene->markDirty(); }
            }
        }
        ImGui::ColorEdit3("ImageColor3", &g.imageColor.x);
        ImGui::SliderFloat("ImageTransparency", &g.imageTransparency, 0.0f, 1.0f);
    }
    if (g.type == GuiType::ViewportFrame) {
        ImGui::SeparatorText("ViewportFrame");
        // CurrentCamera: one of the Cameras inside it (or none: it frames what's inside by itself).
        std::vector<SceneNode*> cams;
        for (auto& c : node->children) if (c->isCamera()) cams.push_back(c.get());
        SceneNode* cur = g.camera ? m_scene->findById(g.camera) : nullptr;
        if (ImGui::BeginCombo("CurrentCamera", cur ? cur->name.c_str() : "(none: fit what's inside)")) {
            if (ImGui::Selectable("(none: fit what's inside)", !cur)) g.camera = 0;
            for (SceneNode* c : cams)
                if (ImGui::Selectable((c->name + "##" + std::to_string(c->id)).c_str(), c == cur)) g.camera = c->id;
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Insert a Camera into the frame to pick it here.");
        ImGui::ColorEdit3("Ambient", &g.ambient.x);
        ImGui::ColorEdit3("LightColor", &g.lightColor.x);
        ImGui::DragFloat3("LightDirection", &g.lightDir.x, 0.02f, -1.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The way the light shines: -1, -1, -1 = down from the top right.");
        ImGui::ColorEdit3("ImageColor3", &g.imageColor.x);
        ImGui::SliderFloat("ImageTransparency", &g.imageTransparency, 0.0f, 1.0f);
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Shows the parts and models inside it as a little 3D picture (an item in a shop, a "
                            "character preview). They aren't in the world: no physics, nobody bumps into them.");
        ImGui::PopTextWrapPos();
    }
    if (node->isGuiButton()) {
        ImGui::SeparatorText("Button");
        ImGui::Checkbox("AutoButtonColor", &g.autoButtonColor);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Get darker when the mouse is over it or presses it.");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("In a script: button.MouseButton1Click:Connect(function() ... end)");
        ImGui::PopTextWrapPos();
    }
}

void PropertiesPanel::renderProperties(SceneNode* node) {
    // --- Name ---
    ImGui::BeginDisabled(m_scene->isProtected(node));
    ImGui::InputText("Name", &node->name);
    ImGui::EndDisabled();

    // --- Scripts: just the code ---
    if (node->isScript()) {
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::Spacing();
        if (ImGui::Button("Edit Script", ImVec2(-1, 36)) && m_openScript) m_openScript(node);
        ImGui::Spacing();
        ImGui::TextDisabled("Runs when you press Play.");
        ImGui::TextDisabled("Inside the code, 'script.Parent' is the");
        ImGui::TextDisabled("object this script is inside of.");
        return;
    }

    if (node->isValue()) {
        ImGui::SeparatorText("Value");
        switch (node->value.type) {
            case Attribute::Bool:   ImGui::Checkbox("Value", &node->value.b); break;
            case Attribute::String: ImGui::InputText("Value", &node->value.s); break;
            case Attribute::Vector3: ImGui::DragFloat3("Value", &node->value.v.x, 0.1f); break;
            case Attribute::Color3: ImGui::ColorEdit3("Value", &node->value.v.x); break;
            default:
                if (node->intValue) {
                    long long v = (long long)node->value.n;
                    if (ImGui::InputScalar("Value", ImGuiDataType_S64, &v)) node->value.n = (double)v;
                } else {
                    ImGui::InputDouble("Value", &node->value.n);
                }
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Scripts read and change it with .Value (and hear about changes with .Changed). "
                            "Put IntValues in a folder called leaderstats inside a player to show them on the leaderboard.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isRemote()) {
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(node->remoteFunction
            ? "A LocalScript asks the server with :InvokeServer(...), and the server's OnServerInvoke function answers."
            : "LocalScripts call :FireServer(...) and the server hears it on OnServerEvent; "
              "the server calls :FireClient(player, ...) or :FireAllClients(...) and players hear it on OnClientEvent.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isPrompt()) {
        PromptProps& p = node->prompt;
        ImGui::SeparatorText("ProximityPrompt");
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::InputText("ActionText", &p.action);
        ImGui::InputText("ObjectText", &p.object);
        static const char* keys[] = {"E", "F", "Q", "R", "G", "T", "X", "Z", "C", "V", "B", "One", "Two", "Three", "Space", "Return"};
        if (ImGui::BeginCombo("Key", p.key.c_str())) {
            for (const char* k : keys)
                if (ImGui::Selectable(k, p.key == k)) p.key = k;
            ImGui::EndCombo();
        }
        ImGui::DragFloat("HoldDuration", &p.hold, 0.05f, 0.0f, 60.0f, "%.2f s");
        ImGui::DragFloat("MaxActivationDistance", &p.range, 0.1f, 0.0f, 500.0f, "%.1f");
        ImGui::Checkbox("RequiresLineOfSight", &p.lineOfSight);
        ImGui::Checkbox("ClickablePrompt", &p.clickable);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Shows when a player walks up to the part it's in. Pressing the key (or clicking it) "
                            "fires its Triggered event, with the player who pressed it.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isClickDetector()) {
        ClickProps& c = node->click;
        ImGui::SeparatorText("ClickDetector");
        ImGui::DragFloat("MaxActivationDistance", &c.range, 0.1f, 0.0f, 500.0f, "%.1f");
        ImGui::InputText("CursorIcon", &c.cursor);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Players can click (or tap) the part or model it's in when they're this close. "
                            "Fires MouseClick, RightMouseClick, MouseHoverEnter and MouseHoverLeave, with the player.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isHighlight()) {
        HighlightProps& h = node->highlight;
        ImGui::SeparatorText("Highlight");
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::ColorEdit3("FillColor", &h.fill.x);
        ImGui::SliderFloat("FillTransparency", &h.fillTransparency, 0.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("OutlineColor", &h.outline.x);
        ImGui::SliderFloat("OutlineTransparency", &h.outlineTransparency, 0.0f, 1.0f, "%.2f");
        int mode = h.onTop ? 0 : 1;
        if (ImGui::Combo("DepthMode", &mode, "AlwaysOnTop\0Occluded\0")) h.onTop = mode == 0;
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(h.onTop ? "Colours the part or model it's in, with an outline round it, seen even through walls."
                                    : "Colours the part or model it's in, with an outline round it, only where you can see it.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isPostEffect()) {
        PostEffectProps& p = node->post;
        ImGui::SeparatorText(kPostEffectClassNames[(int)p.type]);
        ImGui::Checkbox("Enabled", &node->enabled);
        const char* about = "";
        switch (p.type) {
            case PostEffectType::ColorCorrection:
                ImGui::SliderFloat("Brightness", &p.brightness, -1.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Contrast", &p.contrast, -1.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Saturation", &p.saturation, -1.0f, 1.0f, "%.2f");
                ImGui::ColorEdit3("TintColor", &p.tint.x);
                about = "Changes the colours of everything on screen. Saturation -1 = black and white.";
                break;
            case PostEffectType::SunRays:
                ImGui::SliderFloat("Intensity", &p.intensity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Spread", &p.spread, 0.0f, 1.0f, "%.2f");
                about = "Rays of light from the sun, past whatever is in front of it. Off on Low graphics.";
                break;
            case PostEffectType::DepthOfField:
                ImGui::SliderFloat("FarIntensity", &p.farIntensity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("FocusDistance", &p.focusDistance, 0.0f, 200.0f, "%.1f studs");
                ImGui::SliderFloat("InFocusRadius", &p.inFocusRadius, 0.0f, 50.0f, "%.1f studs");
                ImGui::SliderFloat("NearIntensity", &p.nearIntensity, 0.0f, 1.0f, "%.2f");
                about = "Blurs things nearer and further than FocusDistance, like a camera lens. Off on Low graphics.";
                break;
            case PostEffectType::Bloom:
                ImGui::SliderFloat("Intensity", &p.intensity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Size", &p.size, 0.0f, 56.0f, "%.0f");
                ImGui::SliderFloat("Threshold", &p.threshold, 0.0f, 4.0f, "%.2f");
                about = "Bright things glow. Takes over from the bloom in the Lighting settings.";
                break;
            case PostEffectType::Blur:
                ImGui::SliderFloat("Size", &p.size, 0.0f, 56.0f, "%.0f");
                about = "Blurs the whole screen (menus, being dizzy...).";
                break;
        }
        clampPostEffect(p);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s Works right inside Lighting (or the Camera, from a script). Several add up.", about);
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isMaterialVariant()) {
        ImGui::SeparatorText("MaterialVariant");
        MaterialUi::edit(node->variant);
        if (ImGui::Button("Open in the Material Editor", ImVec2(-1, 0)) && m_openMaterials) m_openMaterials(node->id);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Put it in MaterialService. A part shows it when its Material is the BaseMaterial and its MaterialVariant is this one's name.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isSky()) {
        SkyProps& k = node->sky;
        ImGui::SeparatorText("Sky");
        const std::pair<const char*, std::string*> pics[] = {
            {"SkyboxBk", &k.bk}, {"SkyboxDn", &k.dn}, {"SkyboxFt", &k.ft}, {"SkyboxLf", &k.lf},
            {"SkyboxRt", &k.rt}, {"SkyboxUp", &k.up}, {"SunTextureId", &k.sun}, {"MoonTextureId", &k.moon}};
        for (auto& [label, value] : pics) ImGui::InputTextWithHint(label, "(the engine's own)", value);
        ImGui::Checkbox("CelestialBodiesShown", &k.celestial);
        ImGui::SliderInt("StarCount", &k.starCount, 0, 5000);
        ImGui::SliderFloat("SunAngularSize", &k.sunSize, 0.0f, 60.0f, "%.1f");
        ImGui::SliderFloat("MoonAngularSize", &k.moonSize, 0.0f, 60.0f, "%.1f");
        clampSky(k);
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Put it right inside Lighting. Pictures are like a Decal's (an uploaded picture or a file in the games "
                            "folder); the skybox shows when all six load, else the Lighting settings' sky stays.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isEffect()) {
        EffectProps& e = node->effect;
        const bool beam = node->kind == NodeKind::Beam;
        ImGui::SeparatorText(beam ? "Beam" : "Trail");
        ImGui::Checkbox("Enabled", &node->enabled);
        // Its two Attachments: pick from every Attachment in the game.
        std::vector<SceneNode*> atts;
        m_scene->forEach([&](SceneNode* n) { if (n->isAttachment()) atts.push_back(n); });
        auto label = [](const SceneNode* a) { return a ? (a->parent ? a->parent->name + "." + a->name : a->name) : std::string("(none)"); };
        auto pick = [&](const char* name, uint64_t& id) {
            SceneNode* cur = id ? m_scene->findById(id) : nullptr;
            if (ImGui::BeginCombo(name, label(cur).c_str())) {
                if (ImGui::Selectable("(none)", !cur)) id = 0;
                for (SceneNode* a : atts) {
                    ImGui::PushID(a);
                    if (ImGui::Selectable(label(a).c_str(), a == cur)) id = a->id;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
        };
        pick("Attachment0", e.a0);
        pick("Attachment1", e.a1);
        // Colour and see-through at each end (scripts can use more keypoints).
        glm::vec3 c0 = e.color.front().c, c1 = e.color.back().c;
        bool colEdit = ImGui::ColorEdit3("Color (start)", &c0.x);
        colEdit |= ImGui::ColorEdit3("Color (end)", &c1.x);
        if (colEdit) e.color = {{0.0f, c0}, {1.0f, c1}};
        float t0 = e.transparency.front().v, t1 = e.transparency.back().v;
        bool trEdit = ImGui::SliderFloat("Transparency (start)", &t0, 0.0f, 1.0f, "%.2f");
        trEdit |= ImGui::SliderFloat("Transparency (end)", &t1, 0.0f, 1.0f, "%.2f");
        if (trEdit) e.transparency = {{0.0f, t0}, {1.0f, t1}};
        ImGui::SliderFloat("LightEmission", &e.lightEmission, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("FaceCamera", &e.faceCamera);
        ImGui::InputText("Texture", &e.texture);
        if (beam) {
            ImGui::DragFloat("Width0", &e.width0, 0.02f, 0.0f, 1000.0f, "%.2f");
            ImGui::DragFloat("Width1", &e.width1, 0.02f, 0.0f, 1000.0f, "%.2f");
            ImGui::DragFloat("CurveSize0", &e.curve0, 0.05f, -1000.0f, 1000.0f, "%.2f");
            ImGui::DragFloat("CurveSize1", &e.curve1, 0.05f, -1000.0f, 1000.0f, "%.2f");
            ImGui::DragInt("Segments", &e.segments, 0.2f, 1, 1000);
            int mode = e.textureWrap ? 1 : 0;
            if (ImGui::Combo("TextureMode", &mode, "Stretch\0Wrap\0")) e.textureWrap = mode == 1;
            ImGui::DragFloat("TextureLength", &e.textureLength, 0.02f, 0.001f, 10000.0f, "%.2f");
            ImGui::DragFloat("TextureSpeed", &e.textureSpeed, 0.02f, -1000.0f, 1000.0f, "%.2f");
        } else {
            ImGui::DragFloat("Lifetime", &e.lifetime, 0.02f, 0.0f, 20.0f, "%.2f s");
            ImGui::DragFloat("MinLength", &e.minLength, 0.01f, 0.0f, 1000.0f, "%.2f");
            ImGui::DragFloat("MaxLength", &e.maxLength, 0.1f, 0.0f, 10000.0f, e.maxLength > 0.0f ? "%.1f" : "no limit");
            float w0 = e.widthScale.front().v, w1 = e.widthScale.back().v;
            bool wEdit = ImGui::DragFloat("WidthScale (new)", &w0, 0.01f, 0.0f, 100.0f, "%.2f");
            wEdit |= ImGui::DragFloat("WidthScale (old)", &w1, 0.01f, 0.0f, 100.0f, "%.2f");
            if (wEdit) e.widthScale = {{0.0f, w0}, {1.0f, w1}};
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(beam ? "A ribbon between its two Attachments. CurveSize bends it along each Attachment's X axis."
                                 : "Leaves a ribbon behind between its two Attachments as they move, fading over Lifetime seconds.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isParticles()) {
        ParticleProps& p = node->particle;
        ImGui::SeparatorText(node->particleClass());
        ImGui::Checkbox("Enabled", &node->enabled);
        auto note = [](const char* text) {
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("%s", text);
            ImGui::PopTextWrapPos();
        };
        switch (p.type) {
            case ParticleType::Fire:
                ImGui::ColorEdit3("Color", &p.color1.x);
                ImGui::ColorEdit3("SecondaryColor", &p.color2.x);
                ImGui::SliderFloat("Size", &p.fxSize, 2.0f, 30.0f, "%.1f");
                ImGui::SliderFloat("Heat", &p.rise, -25.0f, 25.0f, "%.1f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("How fast the flames rise (below 0: they fall).");
                note("Flames rising from the middle of the part it's in. They fade from Color to SecondaryColor.");
                return;
            case ParticleType::Smoke:
                ImGui::ColorEdit3("Color", &p.color1.x);
                ImGui::SliderFloat("Opacity", &p.opacity, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("RiseVelocity", &p.rise, -25.0f, 25.0f, "%.1f");
                ImGui::SliderFloat("Size", &p.fxSize, 0.1f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                note("Puffs of smoke drifting up from the part it's in, growing as they go.");
                return;
            case ParticleType::Sparkles:
                ImGui::ColorEdit3("SparkleColor", &p.color1.x);
                note("Twinkling sparkles all round the part it's in.");
                return;
            default: break;
        }
        auto range = [](const char* name, glm::vec2& r, float speed, float lo, float hi, const char* fmt) {
            if (ImGui::DragFloat2(name, &r.x, speed, lo, hi, fmt)) { r = glm::clamp(r, lo, hi); if (r.y < r.x) r.y = r.x; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Min, Max: each particle gets a random value between them.");
        };
        ImGui::DragFloat("Rate", &p.rate, 0.5f, 0.0f, 100000.0f, "%.1f / s");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Particles a second. At most %d from one emitter are out at once.", kMaxSpritesPerEmitter);
        range("Lifetime", p.lifetime, 0.05f, 0.0f, 1000.0f, "%.2f s");
        range("Speed", p.speed, 0.1f, -10000.0f, 10000.0f, "%.1f");
        ImGui::DragFloat2("SpreadAngle", &p.spread.x, 0.5f, -360.0f, 360.0f, "%.0f deg");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How far (degrees) they can veer from EmissionDirection, each way.");
        int dir = (int)p.direction;
        if (ImGui::Combo("EmissionDirection", &dir, kFaceNames, 6)) p.direction = (Face)dir;
        // Over each particle's life: start and end (scripts can use more keypoints).
        glm::vec3 c0 = p.color.front().c, c1 = p.color.back().c;
        bool edit = ImGui::ColorEdit3("Color (start)", &c0.x);
        edit |= ImGui::ColorEdit3("Color (end)", &c1.x);
        if (edit) p.color = {{0.0f, c0}, {1.0f, c1}};
        float s0 = p.size.front().v, s1 = p.size.back().v;
        edit = ImGui::DragFloat("Size (start)", &s0, 0.02f, 0.0f, 1000.0f, "%.2f");
        edit |= ImGui::DragFloat("Size (end)", &s1, 0.02f, 0.0f, 1000.0f, "%.2f");
        if (edit) p.size = {{0.0f, std::max(0.0f, s0)}, {1.0f, std::max(0.0f, s1)}};
        float t0 = p.transparency.front().v, t1 = p.transparency.back().v;
        edit = ImGui::SliderFloat("Transparency (start)", &t0, 0.0f, 1.0f, "%.2f");
        edit |= ImGui::SliderFloat("Transparency (end)", &t1, 0.0f, 1.0f, "%.2f");
        if (edit) p.transparency = {{0.0f, t0}, {1.0f, t1}};
        ImGui::SliderFloat("LightEmission", &p.lightEmission, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat3("Acceleration", &p.acceleration.x, 0.1f, -10000.0f, 10000.0f, "%.1f");
        ImGui::DragFloat("Drag", &p.drag, 0.02f, -100.0f, 100.0f, "%.2f");
        range("Rotation", p.rotation, 1.0f, -36000.0f, 36000.0f, "%.0f deg");
        range("RotSpeed", p.rotSpeed, 1.0f, -36000.0f, 36000.0f, "%.0f deg/s");
        ImGui::Checkbox("LockedToPart", &p.locked);
        // The picture: a built-in one, or your own.
        static const char* looks[] = {"Glow", "Smoke", "Fire", "Sparkle"};
        int look = -1;
        for (int i = 0; i < kParticleLookCount; ++i) if (p.texture == kParticleLooks[i]) look = i;
        if (ImGui::BeginCombo("Texture", look >= 0 ? looks[look] : "(a picture)")) {
            for (int i = 0; i < kParticleLookCount; ++i)
                if (ImGui::Selectable(looks[i], look == i)) p.texture = kParticleLooks[i];
            ImGui::EndCombo();
        }
        ImGui::InputText("Texture picture", &p.texture);
        if (ImGui::IsItemDeactivatedAfterEdit()) p.texture = particleTexture(Online::assetRef(p.texture));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Your own picture: a .png in the games folder, a full path, or gb:<id>.\nEmpty = the soft glow.");
        if (ImGui::Button("Emit 20")) { p.pendingEmit += 20; }
        ImGui::SameLine();
        if (ImGui::Button("Clear")) p.pendingClear = true;
        note("Particles fly out of the part it's in (from anywhere inside it), or from an Attachment's point. "
             "Scripts: emitter:Emit(n) sends out n at once, emitter:Clear() removes them.");
        return;
    }

    if (node->isGui()) { renderGui(node); return; }

    if (node->isCamera()) {
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.05f);
        ImGui::DragFloat3("Orientation", &node->transform.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f");
        ImGui::SliderFloat("FieldOfView", &node->fieldOfView, 1.0f, 120.0f, "%.0f deg");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("A camera for a ViewportFrame (its CurrentCamera): it looks along its front (-Z). "
                            "Turn it with Orientation, or set its CFrame in a script.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isDecal()) {
        ImGui::SeparatorText("Decal");
        ImGui::InputText("Texture", &node->texture);
        if (ImGui::IsItemDeactivatedAfterEdit()) node->texture = Online::assetRef(node->texture);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A picture: a .png / .jpg in the games folder, a full path,\nor gb:<id> for one uploaded on the Create page.");
        if (FileDialog::available()) {
            ImGui::SameLine();
            if (ImGui::Button("Browse...")) {
                std::string path = FileDialog::openImage("Pick a picture for the decal");
                if (!path.empty()) { node->texture = Paths::relativeToGames(path); m_scene->markDirty(); }
            }
        }
        int face = (int)node->face;
        if (ImGui::Combo("Face", &face, kFaceNames, 6)) node->face = (Face)face;
        ImGui::ColorEdit3("Color3", &node->color.x);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tints the picture. White shows it as it is.");
        ImGui::SliderFloat("Transparency", &node->transparency, 0.0f, 1.0f);
        if (unsigned tex = Textures::get(node->texture)) {
            int w = 0, h = 0;
            Textures::size(node->texture, w, h);
            float pw = std::min(ImGui::GetContentRegionAvail().x, 160.0f);
            float ph = w > 0 ? pw * (float)h / (float)w : pw;
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(pw, ph), ImVec2(0, 1), ImVec2(1, 0));
        } else if (!node->texture.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.5f, 0.3f, 1), "Can't find that picture yet.");
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Put a Decal inside a part: the picture is stuck on the side you pick in Face, "
                            "stretched to fill it.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isTool()) {
        ImGui::SeparatorText("Tool");
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::InputText("ToolTip", &node->toolTip);
        ImGui::Checkbox("CanBeDropped", &node->canBeDropped);
        ImGui::Checkbox("In StarterPack", &node->starterTool);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everyone gets this tool in their backpack when they spawn.");
        ImGui::DragFloat3("GripPos", &node->gripPos.x, 0.02f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where on the Handle the hand holds it (in the Handle's own space).");
        {   // Grip's turn, shown as its three axes like Roblox's GripForward / GripRight / GripUp
            glm::vec3 axes[3] = {node->gripRot[2], node->gripRot[0], node->gripRot[1]};
            const char* names[3] = {"GripForward", "GripRight", "GripUp"};
            for (int i = 0; i < 3; ++i)
                if (ImGui::DragFloat3(names[i], &axes[i].x, 0.01f, -1.0f, 1.0f) && glm::length(axes[i]) > 1e-4f)
                    node->gripRot[i == 0 ? 2 : i == 1 ? 0 : 1] = glm::normalize(axes[i]);
            if (ImGui::SmallButton("Reset grip turn")) node->gripRot = kDefaultGripRot;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Back to holding the Handle's long side (Y) straight out of the fist.");
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Put a part called Handle inside: that's what the character holds (its long side, Y, "
                            "points forward out of the hand). Players pick tools up by touching them, and press "
                            "1-9 to equip. Scripts inside get tool.Activated when the player clicks.");
        ImGui::PopTextWrapPos();
        return;
    }

    // --- Real liquid ---
    if (node->kind == NodeKind::FluidSystem) {
        ImGui::ColorEdit3("Color", &node->color.x);
        ImGui::SliderFloat("Viscosity", &node->viscosity, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How thick it is: 0 runs like water, 1 oozes like honey.");
        ImGui::SliderFloat("SurfaceTension", &node->surfaceTension, 0.0f, 1.0f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much it sticks to itself: higher makes beads and strands.");
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("A kind of liquid. FluidEmitters pour it out (set their FluidSystem to this).");
        ImGui::PopTextWrapPos();
        return;
    }
    if (node->kind == NodeKind::FluidEmitter) {
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.1f);
        ImGui::DragFloat3("Size", &node->transform.scale.x, 0.05f, 0.1f, 100.0f);
        ImGui::DragFloat("Rate", &node->fluidRate, 5.0f, 0.0f, 100000.0f, "%.0f drops/s");
        ImGui::DragFloat3("Velocity", &node->fluidVelocity.x, 0.1f);
        std::string current = "Water (default)";
        std::vector<SceneNode*> systems;
        m_scene->forEach([&](SceneNode* n) { if (n->kind == NodeKind::FluidSystem) systems.push_back(n); });
        for (SceneNode* sys : systems) if (sys->id == node->fluidSystem) current = sys->name;
        if (ImGui::BeginCombo("FluidSystem", current.c_str())) {
            if (ImGui::Selectable("Water (default)", node->fluidSystem == 0)) node->fluidSystem = 0;
            for (SceneNode* sys : systems) {
                ImGui::PushID((int)sys->id);
                if (ImGui::Selectable(sys->name.c_str(), node->fluidSystem == sys->id)) node->fluidSystem = sys->id;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Pours real liquid while the game runs: Rate drops a second, out of a box "
                            "Size big, moving at Velocity.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isSound()) {
        if (ImGui::BeginCombo("Sound", node->soundId.c_str())) {
            for (const auto& name : Audio::builtinNames())
                if (ImGui::Selectable(name.c_str(), node->soundId == name)) node->soundId = name;
            ImGui::EndCombo();
        }
        ImGui::InputText("File", &node->soundId);
        if (ImGui::IsItemDeactivatedAfterEdit()) node->soundId = Online::assetRef(node->soundId);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A built-in name, or a .wav / .mp3 / .ogg / .flac file in the games folder\n(e.g. music/theme.mp3)");
        ImGui::SliderFloat("Volume", &node->volume, 0.0f, 2.0f);
        ImGui::SliderFloat("Pitch", &node->pitch, 0.25f, 3.0f);
        ImGui::Checkbox("Looped", &node->looped);
        ImGui::SameLine();
        ImGui::Checkbox("Autoplay", &node->autoplay);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Starts playing when the game starts (great for music)");
        // SoundGroup: pick from every SoundGroup in the game (usually in SoundService).
        {
            std::vector<SceneNode*> groups;
            m_scene->forEach([&](SceneNode* n) { if (n->isSoundGroup()) groups.push_back(n); });
            SceneNode* now = SoundMix::groupOf(*m_scene, node);
            if (ImGui::BeginCombo("SoundGroup", now ? now->name.c_str() : "(none)")) {
                if (ImGui::Selectable("(none)", !now)) node->soundGroup = 0;
                for (SceneNode* g : groups) {
                    ImGui::PushID((int)g->id);
                    if (ImGui::Selectable(g->fullName().c_str(), now == g)) node->soundGroup = g->id;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Plays through this group: its Volume multiplies this sound's, and its effects change it too.");
            if (now) ImGui::TextDisabled("Plays at %.2f (Volume x the groups' Volume)", SoundMix::volume(*m_scene, node));
        }
        static int preview = 0;
        if (ImGui::Button(Audio::isPlaying(preview) ? "Stop preview" : "Preview")) {
            if (Audio::isPlaying(preview)) Audio::stop(preview);
            else {
                preview = Audio::play(node->soundId, SoundMix::volume(*m_scene, node), node->pitch, false);
                SoundMix::apply(*m_scene, node, preview);   // with its effects (and its group's)
            }
        }
        ImGui::TextDisabled("Scripts: script.Parent:Play()  /  Sounds.Play(\"coin\")");
        return;
    }

    if (node->isSoundGroup()) {
        ImGui::SeparatorText("SoundGroup");
        ImGui::SliderFloat("Volume", &node->volume, 0.0f, 2.0f, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every sound in the group plays at its own Volume times this (0-10; Ctrl+click to type)");
        node->volume = std::clamp(node->volume, 0.0f, 10.0f);
        int sounds = 0;
        m_scene->forEach([&](SceneNode* n) { if (n->isSound() && n->soundGroup == node->id) ++sounds; });
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%d sound%s use%s this group. Set a Sound's SoundGroup in its Properties; put sound effects "
                            "(Insert Object > Sound) inside the group to change all of them together. A SoundGroup inside "
                            "another one is part of it (the Volumes multiply).", sounds, sounds == 1 ? "" : "s", sounds == 1 ? "s" : "");
        ImGui::PopTextWrapPos();
        return;
    }
    if (node->isSoundEffect()) {
        SoundFxProps& f = node->soundFx;
        ImGui::SeparatorText(SoundFx::kClassNames[(int)f.type]);
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::InputInt("Priority", &f.priority);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Effects with a higher Priority are applied first");
        const auto& params = SoundFx::params(f.type);
        for (size_t i = 0; i < params.size(); ++i) {
            const SoundFx::Param& p = params[i];
            ImGui::SliderFloat(p.name, &f.v[i], p.lo, p.hi, "%.3g");
            f.v[i] = std::clamp(f.v[i], p.lo, p.hi);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
        }
        ImGui::PushTextWrapPos(0);
        const SceneNode* in = node->parent;
        if (!in || (!in->isSound() && !in->isSoundGroup()))
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Put it inside a Sound or SoundGroup: that's what it changes.");
        else
            ImGui::TextDisabled(in->isSound() ? "Changes the sound it's in." : "Changes every sound in the group it's in, mixed together.");
        ImGui::PopTextWrapPos();
        return;
    }

    if (node->isBone()) {   // a joint of an armature (see the Armature window)
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.01f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Studs from the part's middle (or from the bone it's in)");
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        BoneUi::edit(*m_scene, *node);
        return;
    }
    if (node->isAttachment()) {
        ImGui::DragFloat3("Offset", &node->transform.position.x, 0.01f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where on the part (in the part's own space)");
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        ImGui::TextDisabled("Its red (X) direction is the hinge axis.");
        return;
    }
    if (node->isMover()) {
        MoverProps& m = node->mover;
        const MoverType t = m.type;
        const bool classic = isBodyMover(t);
        ImGui::Checkbox("Enabled", &node->enabled);
        if (!classic) {
            SceneNode* a0 = m_scene->findById(node->ref0);
            SceneNode* a1 = m_scene->findById(node->ref1);
            auto owner = [](SceneNode* r) { return r ? (r->parent ? r->parent->name + "." + r->name : r->name) : std::string("(none)"); };
            ImGui::TextDisabled("Attachment0: %s", owner(a0).c_str());
            if (t == MoverType::AlignPosition || t == MoverType::AlignOrientation)
                ImGui::TextDisabled("Attachment1: %s", owner(a1).c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set these from a script: mover.Attachment0 = part.Attachment");
        }
        auto huge = [](float& v, const char* label) {   // (0 = no limit: shown as "huge")
            ImGui::DragFloat(label, &v, 10.0f, 0.0f, 1e30f, v >= 1e29f ? "huge" : "%.0f", ImGuiSliderFlags_Logarithmic);
        };
        switch (t) {
            case MoverType::BodyVelocity:
                ImGui::DragFloat3("Velocity", &m.value.x, 0.1f);
                ImGui::DragFloat3("MaxForce", &m.maxAxes.x, 10.0f, 0.0f, 1e30f, "%.0f");
                break;
            case MoverType::BodyPosition:
                ImGui::DragFloat3("Position", &m.value.x, 0.1f);
                ImGui::DragFloat3("MaxForce", &m.maxAxes.x, 10.0f, 0.0f, 1e30f, "%.0f");
                ImGui::DragFloat("P", &m.p, 10.0f, 0.0f, 1e7f);
                ImGui::DragFloat("D", &m.d, 5.0f, 0.0f, 1e7f);
                break;
            case MoverType::BodyGyro:
                ImGui::DragFloat3("Facing (rotation)", &m.rotation.x, 0.5f);
                ImGui::DragFloat3("MaxTorque", &m.maxAxes.x, 100.0f, 0.0f, 1e30f, "%.0f");
                ImGui::DragFloat("P", &m.p, 10.0f, 0.0f, 1e7f);
                ImGui::DragFloat("D", &m.d, 5.0f, 0.0f, 1e7f);
                break;
            case MoverType::BodyAngularVelocity:
                ImGui::DragFloat3("AngularVelocity", &m.value.x, 0.05f);
                ImGui::DragFloat3("MaxTorque", &m.maxAxes.x, 10.0f, 0.0f, 1e30f, "%.0f");
                break;
            case MoverType::BodyThrust:
                ImGui::DragFloat3("Force", &m.value.x, 1.0f);
                ImGui::DragFloat3("Location", &m.location.x, 0.05f);
                break;
            case MoverType::BodyForce:
            case MoverType::VectorForce:
                ImGui::DragFloat3("Force", &m.value.x, 1.0f);
                if (t == MoverType::VectorForce) ImGui::Checkbox("ApplyAtCenterOfMass", &m.atCenterOfMass);
                break;
            case MoverType::Torque:
                ImGui::DragFloat3("Torque", &m.value.x, 1.0f);
                break;
            case MoverType::LinearVelocity:
                ImGui::DragFloat3("VectorVelocity", &m.value.x, 0.1f);
                huge(m.maxForce, "MaxForce");
                break;
            case MoverType::AngularVelocity:
                ImGui::DragFloat3("AngularVelocity", &m.value.x, 0.05f);
                huge(m.maxForce, "MaxTorque");
                break;
            case MoverType::AlignPosition:
            case MoverType::AlignOrientation:
                if (t == MoverType::AlignPosition) ImGui::DragFloat3("Position", &m.value.x, 0.1f);
                else ImGui::DragFloat3("Facing (rotation)", &m.rotation.x, 0.5f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Used when there's no Attachment1");
                huge(m.maxForce, t == MoverType::AlignPosition ? "MaxForce" : "MaxTorque");
                ImGui::DragFloat("Responsiveness", &m.responsiveness, 0.5f, 0.0f, 200.0f);
                ImGui::DragFloat(t == MoverType::AlignPosition ? "MaxVelocity" : "MaxAngularVelocity", &m.maxVelocity, 0.5f, 0.0f, 1e6f,
                                 m.maxVelocity <= 0 ? "no limit" : "%.1f");
                ImGui::Checkbox("RigidityEnabled", &m.rigid);
                break;
        }
        if (t == MoverType::LinearVelocity || t == MoverType::AngularVelocity || t == MoverType::VectorForce || t == MoverType::Torque) {
            int rel = m.relativeToAttachment ? 1 : 0;
            const char* items[] = {"World", "Attachment0"};
            if (ImGui::Combo("RelativeTo", &rel, items, 2)) m.relativeToAttachment = rel == 1;
        }
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled(classic ? "A classic BodyMover: it pushes the part it's inside (and anything welded to it)."
                                    : "A mover constraint: it pushes the part its Attachment0 is in (and anything welded to it).");
        ImGui::PopTextWrapPos();
        return;
    }
    if (node->isConstraint()) {
        int t = (int)node->constraintType;
        if (ImGui::Combo("Type", &t, kConstraintNames, kConstraintTypeCount)) { node->constraintType = (ConstraintType)t; node->jointReady = false; }
        ImGui::Checkbox("Enabled", &node->enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Visible", &node->visible);
        SceneNode* r0 = m_scene->findById(node->ref0);
        SceneNode* r1 = m_scene->findById(node->ref1);
        auto owner = [](SceneNode* r) { return r ? (r->isAttachment() && r->parent ? r->parent->name : r->name) : std::string("(missing)"); };
        ImGui::TextDisabled("Connects %s  <->  %s", owner(r0).c_str(), owner(r1).c_str());
        ConstraintType ct = node->constraintType;
        if (ct != ConstraintType::Weld) {
            // Its two Attachments: pick from every Attachment in the game.
            std::vector<SceneNode*> atts;
            m_scene->forEach([&](SceneNode* n) { if (n->isAttachment()) atts.push_back(n); });
            auto label = [](const SceneNode* a) { return a ? (a->parent ? a->parent->name + "." + a->name : a->name) : std::string("(none)"); };
            auto pick = [&](const char* name, uint64_t& id) {
                SceneNode* cur = id ? m_scene->findById(id) : nullptr;
                if (ImGui::BeginCombo(name, label(cur).c_str())) {
                    if (ImGui::Selectable("(none)", !cur)) { id = 0; node->jointReady = false; }
                    for (SceneNode* a : atts) {
                        ImGui::PushID(a);
                        if (ImGui::Selectable(label(a).c_str(), a == cur)) { id = a->id; node->jointReady = false; }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            };
            pick("Attachment0", node->ref0);
            pick("Attachment1", node->ref1);
        }
        if (ct == ConstraintType::Rope || ct == ConstraintType::Rod || ct == ConstraintType::Spring) {
            bool autoLen = node->length < 0.0f;
            if (ImGui::Checkbox("Length as placed", &autoLen)) node->length = autoLen ? -1.0f : 4.0f;
            if (!autoLen) ImGui::DragFloat("Length", &node->length, 0.05f, 0.0f, 500.0f);
        }
        if (ct == ConstraintType::Spring) {
            ImGui::DragFloat("Stiffness", &node->stiffness, 1.0f, 0.0f, 100000.0f);
            ImGui::DragFloat("Damping", &node->damping, 0.1f, 0.0f, 10000.0f);
        }
        if (ct == ConstraintType::Hinge) {
            bool motor = node->motorTorque > 0.0f;
            if (ImGui::Checkbox("Motor", &motor)) node->motorTorque = motor ? 3000.0f : 0.0f;
            if (motor) {
                ImGui::DragFloat("Speed", &node->motorSpeed, 0.05f, -100.0f, 100.0f, "%.2f rad/s");
                ImGui::DragFloat("Strength", &node->motorTorque, 10.0f, 0.0f, 1e7f);
            }
        }
        if (usesJointProps(ct)) {
            JointProps& p = node->joint;
            auto actuator = [](const char* label, Actuator& a) {
                int v = (int)a;
                if (ImGui::Combo(label, &v, kActuatorNames, 3)) a = (Actuator)v;
            };
            ImGui::PushTextWrapPos(0);
            if (ct == ConstraintType::BallSocket) {
                ImGui::TextDisabled("Holds the two attachments together and lets them turn any way, like a shoulder.");
                ImGui::Checkbox("LimitsEnabled", &p.limitsEnabled);
                if (p.limitsEnabled) ImGui::DragFloat("UpperAngle", &p.upperAngle, 0.5f, 0.0f, 180.0f, "%.1f deg");
                ImGui::Checkbox("TwistLimitsEnabled", &p.twistLimitsEnabled);
                if (p.twistLimitsEnabled) {
                    ImGui::DragFloat("TwistLowerAngle", &p.twistLowerAngle, 0.5f, -180.0f, 180.0f, "%.1f deg");
                    ImGui::DragFloat("TwistUpperAngle", &p.twistUpperAngle, 0.5f, -180.0f, 180.0f, "%.1f deg");
                }
                if (p.limitsEnabled || p.twistLimitsEnabled) ImGui::SliderFloat("Restitution", &p.restitution, 0.0f, 1.0f);
            } else {
                ImGui::TextDisabled(ct == ConstraintType::Prismatic
                    ? "Lets Attachment1 slide along Attachment0's axis (its X), like a drawer or a lift. It can't turn."
                    : "Lets Attachment1 slide along Attachment0's axis and spin around it, like a piston or a drill.");
                ImGui::Checkbox("LimitsEnabled", &p.limitsEnabled);
                if (p.limitsEnabled) {
                    ImGui::DragFloat("LowerLimit", &p.lowerLimit, 0.05f, -1000.0f, 1000.0f, "%.2f studs");
                    ImGui::DragFloat("UpperLimit", &p.upperLimit, 0.05f, -1000.0f, 1000.0f, "%.2f studs");
                    ImGui::SliderFloat("Restitution", &p.restitution, 0.0f, 1.0f);
                }
                actuator("ActuatorType", p.actuator);
                if (p.actuator == Actuator::Motor) {
                    ImGui::DragFloat("Velocity", &p.velocity, 0.05f, -1000.0f, 1000.0f, "%.2f studs/s");
                    ImGui::DragFloat("MotorMaxForce", &p.motorMaxForce, 10.0f, 0.0f, 1e9f);
                } else if (p.actuator == Actuator::Servo) {
                    ImGui::DragFloat("TargetPosition", &p.targetPosition, 0.05f, -1000.0f, 1000.0f, "%.2f studs");
                    ImGui::DragFloat("Speed", &p.speed, 0.05f, 0.0f, 1000.0f, "%.2f studs/s");
                    ImGui::DragFloat("ServoMaxForce", &p.servoMaxForce, 10.0f, 0.0f, 1e9f);
                }
                if (ct == ConstraintType::Cylindrical) {
                    ImGui::SeparatorText("Spinning");
                    ImGui::DragFloat("InclinationAngle", &p.inclinationAngle, 0.5f, -180.0f, 180.0f, "%.1f deg");
                    ImGui::Checkbox("AngularLimitsEnabled", &p.angularLimitsEnabled);
                    if (p.angularLimitsEnabled) {
                        ImGui::DragFloat("LowerAngle", &p.lowerAngle, 0.5f, -180.0f, 180.0f, "%.1f deg");
                        ImGui::DragFloat("UpperAngle", &p.upperAngle, 0.5f, -180.0f, 180.0f, "%.1f deg");
                    }
                    actuator("AngularActuatorType", p.angularActuator);
                    if (p.angularActuator == Actuator::Motor) {
                        ImGui::DragFloat("AngularVelocity", &p.angularVelocity, 0.05f, -100.0f, 100.0f, "%.2f rad/s");
                        ImGui::DragFloat("MotorMaxTorque", &p.motorMaxTorque, 10.0f, 0.0f, 1e9f);
                    } else if (p.angularActuator == Actuator::Servo) {
                        ImGui::DragFloat("TargetAngle", &p.targetAngle, 0.5f, -180.0f, 180.0f, "%.1f deg");
                        ImGui::DragFloat("AngularSpeed", &p.angularSpeed, 0.05f, 0.0f, 100.0f, "%.2f rad/s");
                        ImGui::DragFloat("ServoMaxTorque", &p.servoMaxTorque, 10.0f, 0.0f, 1e9f);
                    }
                }
            }
            ImGui::PopTextWrapPos();
        }
        if (ct != ConstraintType::Weld) {
            ImGui::ColorEdit3("Color", &node->color.x);
            ImGui::SliderFloat("Thickness", &node->thickness, 0.02f, 1.0f);
        }
        // Breakable joints: pulled harder than BreakForce, it snaps and switches itself off.
        bool breakable = node->breakForce > 0.0f;
        if (ImGui::Checkbox("Breakable", &breakable)) node->breakForce = breakable ? 2000.0f : 0.0f;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", I18n::tr("Snaps when it's pulled too hard (BreakForce). Turn on Physics View (MODEL tab) "
                                             "while testing to see how hard each joint is pulled."));
        if (breakable) {
            ImGui::DragFloat("BreakForce", &node->breakForce, 10.0f, 1.0f, 1e9f, "%.0f");
            if (node->jointForce > 0.0f) ImGui::TextDisabled("%s %.0f", I18n::tr("Pulled now:"), node->jointForce);
        }
        if (node->broken && !node->enabled) ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "%s", I18n::tr("Broken"));
        return;
    }

    if (node->isLight()) {
        ImGui::Checkbox("Enabled", &node->enabled);
        int t = (int)node->lightType;
        const char* types[] = {"Point", "Spot"};
        if (ImGui::Combo("Type", &t, types, 2)) node->lightType = (LightType)t;
        ImGui::ColorEdit3("Color", &node->color.x);
        ImGui::SliderFloat("Brightness", &node->brightness, 0.0f, 20.0f);
        ImGui::SliderFloat("Range", &node->range, 1.0f, 100.0f);
        if (node->lightType == LightType::Spot) {
            ImGui::SliderFloat("Angle", &node->spotAngle, 5.0f, 170.0f, "%.0f\xc2\xb0");
            ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
            ImGui::TextDisabled("Spot lights point down; rotate to aim.");
        }
        ImGui::DragFloat3("Offset", &node->transform.position.x, 0.05f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Position relative to the part it's inside");
        return;
    }

    if (node == m_scene->root()) {
        ImGui::Separator();
        ImGui::TextDisabled("The Workspace holds everything in your game.");
        ImGui::DragFloat("Gravity", &m_scene->world().gravity, 0.1f, 0.0f, 200.0f);
        return;
    }

    ImGui::Checkbox("Visible", &node->visible);
    ImGui::Separator();

    // --- Transform ---
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &node->transform.position.x, 0.05f);
        ImGui::DragFloat3("Rotation", &node->transform.rotation.x, 0.5f);
        ImGui::DragFloat3("Size",     &node->transform.scale.x,    0.05f, 0.001f, 1000.0f);

        if (ImGui::Button("Reset Transform")) {
            node->transform.position = {0, 0, 0};
            node->transform.rotation = {0, 0, 0};
            node->transform.scale    = {1, 1, 1};
        }
    }

    // Model.PrimaryPart: the part GetPivot / PivotTo / SetPrimaryPartCFrame go by.
    if (node->kind == NodeKind::Model && node != m_scene->root()) {
        std::vector<SceneNode*> parts;
        std::vector<SceneNode*> stack{node};
        while (!stack.empty() && parts.size() < 500) {
            SceneNode* n = stack.back();
            stack.pop_back();
            for (auto& c : n->children) {
                if (c->internal) continue;
                if (c->isPart()) parts.push_back(c.get());
                stack.push_back(c.get());
            }
        }
        if (!parts.empty()) {
            SceneNode* cur = node->primaryPart ? m_scene->findById(node->primaryPart) : nullptr;
            if (cur && !node->isAncestorOf(cur)) cur = nullptr;
            if (ImGui::BeginCombo("PrimaryPart", cur ? cur->name.c_str() : "(none)")) {
                if (ImGui::Selectable("(none)", !cur)) node->primaryPart = 0;
                for (SceneNode* p : parts) {
                    ImGui::PushID((void*)p);
                    if (ImGui::Selectable(p->name.c_str(), p == cur)) node->primaryPart = p->id;
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The part model:GetPivot() / PivotTo() / SetPrimaryPartCFrame() go by");
        }
    }

    if (node->kind != NodeKind::Part) {
        return;
    }

    // --- Appearance ---
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", &node->color.x);

        if (node->primitiveType == PrimitiveType::Mesh && node->editMesh) {
            // A MeshPart: its shape was built in Modeling mode.
            ImGui::TextDisabled("Custom mesh: %d corners, %d faces", (int)node->editMesh->verts.size(),
                                (int)node->editMesh->faces.size());
            bool smooth = node->editMesh->smooth;
            if (ImGui::Checkbox("Smooth shading", &smooth)) { MeshEdit::own(*node).smooth = smooth; MeshEdit::refresh(*node); }
            if (ImGui::Button("Edit Mesh (Tab)", ImVec2(-1, 0)) && m_editMesh) m_editMesh(node);
            // A picture wrapped onto it by its UV layout (the UV Editor shows where each face sits).
            ImGui::InputText("Texture", &node->texture);
            if (ImGui::IsItemDeactivatedAfterEdit()) node->texture = Online::assetRef(node->texture);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("A picture wrapped onto the mesh: a .png / .jpg in the games folder, a full path,\n"
                                  "or gb:<id> for one uploaded on the Create page. The UV Editor shows how it wraps.");
            if (FileDialog::available()) {
                ImGui::SameLine();
                if (ImGui::Button("Browse...##meshtex")) {
                    std::string path = FileDialog::openImage("Pick a picture to wrap onto the mesh");
                    if (!path.empty()) { node->texture = Paths::relativeToGames(path); m_scene->markDirty(); }
                }
            }
            if (ImGui::Button("UV Editor", ImVec2(-1, 0)) && m_openUVEditor) m_openUVEditor();
        } else {
            const char* shapes[] = { "Cube", "Sphere", "Plane", "Cylinder" };
            int shape = (int)node->primitiveType - 1;
            if (ImGui::Combo("Shape", &shape, shapes, IM_ARRAYSIZE(shapes))) {
                node->primitiveType = (PrimitiveType)(shape + 1);
                node->mesh = MeshLibrary::get(node->primitiveType);
            }
            if (ImGui::Button("Reshape in Modeling mode (Tab)", ImVec2(-1, 0)) && m_editMesh) m_editMesh(node);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turns this part into a MeshPart you can reshape");
        }

        int mat = (int)node->material;
        if (ImGui::Combo("Material", &mat, kMaterialNames, kMaterialCount))
            node->material = (Material)mat;
        // MaterialVariant: one of the game's own materials (MaterialService). Picking one
        // also sets Material to its BaseMaterial, which it needs to show.
        {
            std::vector<SceneNode*> vars;
            if (SceneNode* ms = m_scene->serviceFolder("MaterialService", false))
                for (auto& c : ms->children) if (c->isMaterialVariant()) vars.push_back(c.get());
            SceneNode* on = nullptr;
            for (SceneNode* v : vars) if (v->name == node->materialVariant) { on = v; break; }
            std::string shown = node->materialVariant.empty() ? std::string("(none)")
                              : !on ? node->materialVariant + " (not in MaterialService)"
                              : on->variant.base != node->material ? node->materialVariant + " (needs Material " + kMaterialNames[(int)on->variant.base] + ")"
                              : node->materialVariant;
            if (ImGui::BeginCombo("MaterialVariant", shown.c_str())) {
                if (ImGui::Selectable("(none)", node->materialVariant.empty())) node->materialVariant.clear();
                for (SceneNode* v : vars) {
                    ImGui::PushID(v);
                    if (ImGui::Selectable(v->name.c_str(), v == on)) { node->materialVariant = v->name; node->material = v->variant.base; }
                    ImGui::PopID();
                }
                ImGui::Separator();
                if (ImGui::Selectable("Material Editor...") && m_openMaterials) m_openMaterials(on ? on->id : 0);
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your own materials, made in the Material Editor (VIEW > Materials)");
        }

        ImGui::SliderFloat("Transparency", &node->transparency, 0.0f, 1.0f);
    }

    // --- Behavior ---
    if (ImGui::CollapsingHeader("Behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Anchored",    &node->anchored);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Unanchored parts fall with gravity in Play mode");
        ImGui::Checkbox("Can Collide", &node->canCollide);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Turn off to let things pass through");
        ImGui::Checkbox("Cast Shadow", &node->castShadow);
        // Collision Group (PhysicsService): which registered group it's in.
        const CollisionGroups& groups = m_scene->world().collisionGroups;
        const bool known = groups.find(node->collisionGroup) >= 0;
        std::string shown = known ? node->collisionGroup : node->collisionGroup + " (not registered: acts as Default)";
        if (ImGui::BeginCombo("Collision Group", shown.c_str())) {
            for (int i : groups.slots()) {
                const bool on = groups.names[i] == node->collisionGroup;
                if (ImGui::Selectable(groups.names[i].c_str(), on)) {
                    const std::string g = groups.names[i];
                    for (SceneNode* s : m_scene->selection())   // (every selected part)
                        if (s->isPart()) s->collisionGroup = g;
                    node->collisionGroup = g;
                }
                if (on) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Parts in groups that don't collide pass through each other");
        if (ImGui::SmallButton("Edit Collision Groups...") && m_editCollisionGroups) m_editCollisionGroups();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collision Groups: add groups and choose which ones collide");
    }

    // --- Physics (how it behaves when unanchored) ---
    if (ImGui::CollapsingHeader("Physics")) {
        auto prop = [&](const char* label, float& v, float def, float mn, float mx, const char* tip) {
            bool custom = v >= 0.0f;
            ImGui::PushID(label);
            if (ImGui::Checkbox("##custom", &custom)) v = custom ? def : -1.0f;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tick to override the material's value");
            ImGui::SameLine();
            ImGui::BeginDisabled(!custom);
            float shown = custom ? v : def;
            if (ImGui::SliderFloat(label, &shown, mn, mx) && custom) v = shown;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
            ImGui::PopID();
        };
        prop("Density", node->density, 1.0f, 0.05f, 10.0f, "Heavier parts are harder to push. Water is 1.3: lighter parts float, heavier ones sink");
        prop("Friction", node->friction, 0.5f, 0.0f, 2.0f, "0 = slides like ice");
        prop("Bounciness", node->elasticity, 0.3f, 0.0f, 1.0f, "1 = bounces like a rubber ball");
        if (node->hasCustomPhysics()) {   // (CustomPhysicalProperties' FrictionWeight / ElasticityWeight)
            ImGui::SliderFloat("Friction weight", &node->frictionWeight, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much this part's friction counts when it touches another part (1 = an even mix)");
            ImGui::SliderFloat("Bounciness weight", &node->elasticityWeight, 0.0f, 100.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("How much this part's bounciness counts when it touches another part (1 = the usual)");
        }
    }

    // --- SpawnLocation: who spawns here ---
    if (node->isSpawnLocation() && ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Neutral", &node->spawnNeutral);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Anybody can spawn here. Untick to make it one team's spawn (by TeamColor)");
        ImGui::BeginDisabled(node->spawnNeutral);
        ImGui::ColorEdit3("TeamColor", &node->teamColor.x);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Players on the Team with this TeamColor spawn here");
        ImGui::Checkbox("AllowTeamChangeOnTouch", &node->allowTeamChange);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Touching it puts you on its team");
        ImGui::EndDisabled();
    }

    if (!m_scene->isCharacterPart(node)) {
        ImGui::Spacing();
        if (ImGui::Button("Add Script inside", ImVec2(-1, 0))) {
            auto s = std::make_unique<SceneNode>("Script", NodeKind::Script);
            s->source = "local part = script.Parent\n\nprint(\"Hello from \" .. part.Name)\n";
            SceneNode* raw = m_scene->insert(std::move(s), node);
            m_scene->select(raw);
            if (m_openScript) m_openScript(raw);
        }
    }
}

// Roblox-style Attributes (your own named values) and Tags, for any object.
void PropertiesPanel::renderAttributes(SceneNode* node) {
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Attributes", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        for (size_t i = 0; i < node->attributes.size(); ++i) {
            Attribute& a = node->attributes[i];
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x")) remove = (int)i;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this attribute");
            ImGui::SameLine();
            switch (a.type) {
                case Attribute::Bool:    ImGui::Checkbox(a.name.c_str(), &a.b); break;
                case Attribute::Number:  ImGui::InputDouble(a.name.c_str(), &a.n, 1.0, 10.0, "%g"); break;
                case Attribute::String:  ImGui::InputText(a.name.c_str(), &a.s); break;
                case Attribute::Vector3: ImGui::DragFloat3(a.name.c_str(), &a.v.x, 0.05f); break;
                case Attribute::Color3:  ImGui::ColorEdit3(a.name.c_str(), &a.v.x); break;
            }
            ImGui::PopID();
        }
        if (remove >= 0) node->attributes.erase(node->attributes.begin() + remove);
        if (node->attributes.empty()) ImGui::TextDisabled("Scripts read these with obj:GetAttribute(\"Name\").");

        if (ImGui::Button("+ Add Attribute", ImVec2(-1, 0))) {
            m_newAttrName.clear();
            m_newAttrError.clear();
            ImGui::OpenPopup("Add Attribute");
        }
        if (ImGui::BeginPopup("Add Attribute")) {
            static const char* kTypes[] = {"Boolean", "Number", "String", "Vector3", "Color3"};
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            bool enter = ImGui::InputTextWithHint("Name", "e.g. Damage", &m_newAttrName,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Combo("Type", &m_newAttrType, kTypes, 5);
            if (!m_newAttrError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", m_newAttrError.c_str());
            if (ImGui::Button("Add") || enter) {
                bool ok = !m_newAttrName.empty() && m_newAttrName.size() <= 100 && m_newAttrName.rfind("RBX", 0) != 0;
                for (char c : m_newAttrName) if (!std::isalnum((unsigned char)c) && c != '_') ok = false;
                if (!ok) m_newAttrError = "Use letters, numbers and _ only.";
                else if (node->findAttribute(m_newAttrName)) m_newAttrError = "There's already one called that.";
                else {
                    Attribute a;
                    a.name = m_newAttrName;
                    a.type = (Attribute::Type)m_newAttrType;
                    if (a.type == Attribute::Color3) a.v = glm::vec3(1.0f);
                    node->attributes.push_back(a);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)) {
        int remove = -1;
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        for (size_t i = 0; i < node->tags.size(); ++i) {
            ImGui::PushID((int)i);
            std::string chip = node->tags[i] + "  x";
            float w = ImGui::CalcTextSize(chip.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0) {
                ImGui::SameLine();
                if (ImGui::GetCursorScreenPos().x + w > right) ImGui::NewLine();
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.44f, 0.72f, 1.0f));
            if (ImGui::SmallButton(chip.c_str())) remove = (int)i;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to remove this tag");
            ImGui::PopID();
        }
        if (remove >= 0) node->tags.erase(node->tags.begin() + remove);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##newtag", "+ Add tag (press Enter)", &m_newTag, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (!m_newTag.empty() && std::find(node->tags.begin(), node->tags.end(), m_newTag) == node->tags.end())
                node->tags.push_back(m_newTag);
            m_newTag.clear();
            ImGui::SetKeyboardFocusHere(-1);
        }
        if (node->tags.empty()) ImGui::TextDisabled("Find tagged things with CollectionService:GetTagged(\"Tag\").");
    }
}
