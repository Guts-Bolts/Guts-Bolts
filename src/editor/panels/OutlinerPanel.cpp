#include "OutlinerPanel.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"
#include "../../scene/Player.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include "../Icons.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

OutlinerPanel::OutlinerPanel(Scene* scene, NodeFn openScript, NodeFn addScriptTo)
    : m_scene(scene), m_openScript(std::move(openScript)), m_addScriptTo(std::move(addScriptTo)) {}

bool OutlinerPanel::isOpen(const SceneNode* node) const {
    auto it = m_open.find(node->id);
    if (it != m_open.end()) return it->second;
    // Keep the character folded by default — it has lots of parts.
    return !m_scene->isCharacterPart(node);
}

void OutlinerPanel::expand(const std::vector<SceneNode*>& nodes) {
    std::vector<SceneNode*> stack(nodes.begin(), nodes.end());
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        m_open[n->id] = true;
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

void OutlinerPanel::collapse(const std::vector<SceneNode*>& nodes) {
    std::vector<SceneNode*> stack(nodes.begin(), nodes.end());
    while (!stack.empty()) {
        SceneNode* n = stack.back(); stack.pop_back();
        m_open[n->id] = false;
        for (auto& c : n->children) stack.push_back(c.get());
    }
}

void OutlinerPanel::collapseAll() {
    if (SceneNode* root = m_scene->root()) {
        collapse({root});
        m_open[root->id] = true;   // keep the Workspace itself open
    }
}

void OutlinerPanel::reveal(SceneNode* node) {
    if (!node) return;
    for (SceneNode* p = node->parent; p; p = p->parent) m_open[p->id] = true;
    m_scrollTo = node->id;
}

void OutlinerPanel::beginRename(SceneNode* node) {
    if (!node || m_scene->isProtected(node) || m_scene->isCharacterPart(node)) return;
    reveal(node);
    m_renaming = node->id;
    m_renameText = node->name;
    m_renameFocus = true;
}

void OutlinerPanel::drawNode(SceneNode* node) {
    if (node->internal) return;   // hidden helper geometry (e.g. the face)
    if (!m_filter.empty() && node->parent && !anyMatch(node)) return;

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth;

    bool hasVisibleKids = false;
    for (auto& c : node->children) if (!c->internal) { hasVisibleKids = true; break; }
    if (!hasVisibleKids) flags |= ImGuiTreeNodeFlags_Leaf;
    if (node->selected)  flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(node);

    // Roblox-style: a class icon, then the name (dimmed if hidden / disabled).
    ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    if (!node->visible || (node->isScript() && !node->enabled)) col.w = 0.5f;
    if (!m_filter.empty() && !matches(node)) col.w = 0.45f;          // only here because something inside matches
    ImGui::PushStyleColor(ImGuiCol_Text, col);

    bool renaming = m_renaming == node->id;
    const bool filtering = !m_filter.empty();
    if (hasVisibleKids) ImGui::SetNextItemOpen(filtering ? true : isOpen(node), ImGuiCond_Always);
    const float iconW = ImGui::GetTextLineHeight() + 4;
    bool open = ImGui::TreeNodeEx("##node", flags, "%*s%s", (int)(iconW / ImGui::CalcTextSize(" ").x) + 1, "",
                                  renaming ? "" : node->name.c_str());
    {
        // Icon drawn over the gap left before the name.
        ImVec2 mn = ImGui::GetItemRectMin();
        float h = ImGui::GetTextLineHeight();
        float x = mn.x + ImGui::GetTreeNodeToLabelSpacing() + h * 0.5f;
        Icons::draw(ImGui::GetWindowDrawList(), ImVec2(x, mn.y + ImGui::GetStyle().FramePadding.y + h * 0.5f), h,
                    Icons::forNode(*node));
        if (node->locked)
            Icons::draw(ImGui::GetWindowDrawList(), ImVec2(x + h * 0.45f, mn.y + h * 0.9f), h * 0.55f, Icons::Id::Lock);
    }
    ImGui::PopStyleColor();
    if (!filtering) {
    if (hasVisibleKids) m_open[node->id] = open;
    }
    if (m_scrollTo == node->id) { ImGui::SetScrollHereY(0.4f); m_scrollTo = 0; }

    // A "+" at the end of the row (like Roblox) opens Insert Object for it.
    bool rowHovered = ImGui::IsItemHovered();
    ImVec2 rowMax = ImGui::GetItemRectMax(), rowMin = ImGui::GetItemRectMin();
    if ((rowHovered || node->selected) && onInsert && !m_scene->isCharacterPart(node) && !node->isScript()) {
        float h = rowMax.y - rowMin.y;
        ImVec2 c(rowMax.x - h * 0.5f - 2, (rowMin.y + rowMax.y) * 0.5f);
        ImVec2 mouse = ImGui::GetMousePos();
        bool over = mouse.x > c.x - h * 0.5f && mouse.x < c.x + h * 0.5f && mouse.y > rowMin.y && mouse.y < rowMax.y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (over) dl->AddRectFilled(ImVec2(c.x - h * 0.45f, rowMin.y + 1), ImVec2(c.x + h * 0.45f, rowMax.y - 1), IM_COL32(90, 90, 90, 255), 3);
        Icons::draw(dl, c, h * 0.8f, Icons::Id::Insert);
        if (over && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { onInsert(node); ImGui::PopID(); return; }
    }

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl)       m_scene->toggleSelection(node);
        else if (io.KeyShift) m_scene->addToSelection(node);
        else                  m_scene->select(node);
        m_lastSelected = m_scene->selected();
    }
    if (node->isScript() && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        m_openScript(node);

    // Drag an object (or everything selected) onto another one to move it inside.
    bool locked = m_scene->isProtected(node) || m_scene->isCharacterPart(node);
    if (!locked && !renaming && ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("GB_NODE", &node, sizeof(SceneNode*));
        size_t n = node->selected ? m_scene->selectionRoots().size() : 1;
        if (n > 1) ImGui::Text("Move %zu objects", n);
        else       ImGui::Text("Move %s", node->name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GB_NODE")) {
            SceneNode* dragged = *static_cast<SceneNode* const*>(p->Data);
            m_dragNodes.clear();
            if (dragged->selected) m_dragNodes = m_scene->selectionRoots();
            else                   m_dragNodes.push_back(dragged);
            m_dropTarget = node;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem()) {
        if (!node->selected) m_scene->select(node);
        if (node->isScript() && ImGui::MenuItem("Edit Script")) m_openScript(node);
        if (node->isPart() && ImGui::MenuItem("Add Script inside")) m_addScriptTo(node);
        if (ImGui::MenuItem("Rename", "F2", false, !locked)) beginRename(node);
        if (ImGui::MenuItem(node->visible ? "Hide" : "Show", "H"))
            node->visible = !node->visible;
        if (node->parent && node->parent != m_scene->root() && !locked &&
            ImGui::MenuItem("Move to Workspace")) {
            m_dragNodes = {node};
            m_dropTarget = m_scene->root();
        }
        if (!locked && !node->isScript() && onInsertNamed && ImGui::BeginMenu("Insert Object")) {
            insertMenu(node);
            ImGui::EndMenu();
        }
        if (contextMenuExtras) contextMenuExtras();
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, !m_scene->isProtected(node)))
            m_pendingDelete = node;
        ImGui::EndPopup();
    }

    if (renaming) {
        ImGui::SameLine();
        if (m_renameFocus) { ImGui::SetKeyboardFocusHere(); m_renameFocus = false; }
        ImGui::SetNextItemWidth(-1);
        bool done = ImGui::InputText("##rename", &m_renameText,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) m_renaming = 0;
        else if (done || ImGui::IsItemDeactivated()) {
            if (!m_renameText.empty()) node->name = m_renameText;
            m_renaming = 0;
        }
    }

    if (open) {
        // The character lives under StarterPlayer while you build (see render()).
        const bool hideCharacter = node == m_scene->root() && !(playing && playing());
        // The services have their own rows below the Workspace (but a search looks in them too).
        const bool hideServices = node == m_scene->root() && !filtering;
        for (auto& child : node->children) {
            if (hideCharacter && m_scene->isCharacterRoot(child->id)) continue;
            if (hideServices && m_scene->isServiceFolder(child.get())) continue;
            drawNode(child.get());
        }
        ImGui::TreePop();
    }

    ImGui::PopID();
}

// Insert Object, like Roblox's: everything Studio can make, in groups.
void OutlinerPanel::insertMenu(SceneNode* parent) {
    struct Group { const char* title; std::vector<const char*> items; };
    static const Group groups[] = {
        {"Parts", {"Part", "Sphere", "Cylinder", "MeshPart", "TrussPart", "SpawnLocation", "Seat"}},
        {"Scripts", {"Script", "LocalScript", "ModuleScript", "RemoteEvent", "RemoteFunction"}},
        {"Characters & Tools", {"Tool", "Rig", "Animation", "Team", "ProximityPrompt"}},
        {"Containers", {"Model", "Folder"}},
        {"Effects & Lights", {"PointLight", "SpotLight", "Sound", "ForceField", "Decal", "Highlight", "Trail", "Beam"}},
        {"Water", {"Water", "WaterSource", "FluidVolume", "FluidSystem", "FluidEmitter"}},
        {"Constraints", {"Attachment", "HingeConstraint", "BallSocketConstraint", "PrismaticConstraint", "CylindricalConstraint",
                          "RopeConstraint", "RodConstraint", "SpringConstraint", "WeldConstraint", "LinearVelocity", "AlignPosition", "AlignOrientation", "AngularVelocity", "VectorForce", "Torque"}},
        {"Body Movers (classic)", {"BodyVelocity", "BodyPosition", "BodyGyro", "BodyAngularVelocity", "BodyThrust", "BodyForce"}},
        {"User Interface", {"ScreenGui", "BillboardGui", "SurfaceGui", "Frame", "TextLabel", "TextButton", "ImageLabel", "ImageButton", "TextBox", "ScrollingFrame",
                            "UICorner", "UIStroke", "UIShadow", "UIBlur", "UIListLayout", "UIGridLayout", "UIPadding"}},
        {"Values", {"IntValue", "NumberValue", "StringValue", "BoolValue"}},
    };
    for (const Group& g : groups) {
        ImGui::TextDisabled("%s", g.title);
        for (const char* what : g.items)
            if (ImGui::MenuItem(what)) {
                const std::string w = what;
                onInsertNamed(w, parent);
            }
    }
    ImGui::Separator();
    if (onInsert && ImGui::MenuItem("More (search)...")) onInsert(parent);
}

// A container service (ReplicatedStorage, ServerScriptService, StarterPack...): a row of
// its own like Roblox's Explorer, holding what's inside its folder. The folder is made
// the first time you put something in it (Insert Object, or drag things onto the row).
void OutlinerPanel::folderService(const char* name, int icon, const char* tip) {
    SceneNode* f = m_scene->serviceFolder(name, false);
    bool kids = false;
    if (f) for (auto& c : f->children) if (!c->internal) { kids = true; break; }
    ImGui::PushID(name);
    const bool open = serviceRow(name, icon, kids, f && f->selected);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && f) m_scene->select(f);
    if (ImGui::IsItemHovered() && tip) ImGui::SetTooltip("%s", tip);
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("GB_NODE")) {
            SceneNode* dragged = *static_cast<SceneNode* const*>(p->Data);
            m_dragNodes.clear();
            if (dragged->selected) m_dragNodes = m_scene->selectionRoots();
            else                   m_dragNodes.push_back(dragged);
            m_dropTarget = m_scene->serviceFolder(name, true);
        }
        ImGui::EndDragDropTarget();
    }
    if (onInsertNamed && ImGui::BeginPopupContextItem("##svc")) {
        if (ImGui::BeginMenu("Insert Object")) { insertMenu(m_scene->serviceFolder(name, true)); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }
    if (open) {
        if (f) for (auto& c : f->children) drawNode(c.get());
        ImGui::TreePop();
    }
    ImGui::PopID();
}

bool OutlinerPanel::serviceRow(const char* name, int icon, bool hasKids, bool selected) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasKids) flags |= ImGuiTreeNodeFlags_Leaf;
    if (selected) flags |= ImGuiTreeNodeFlags_Selected;
    const float iconW = ImGui::GetTextLineHeight() + 4;
    bool open = ImGui::TreeNodeEx(name, flags, "%*s%s", (int)(iconW / ImGui::CalcTextSize(" ").x) + 1, "", name);
    ImVec2 mn = ImGui::GetItemRectMin();
    float h = ImGui::GetTextLineHeight();
    float x = mn.x + ImGui::GetTreeNodeToLabelSpacing() + h * 0.5f;
    Icons::draw(ImGui::GetWindowDrawList(), ImVec2(x, mn.y + ImGui::GetStyle().FramePadding.y + h * 0.5f), h, (Icons::Id)icon);
    return open;
}

void OutlinerPanel::render() {
    // Something picked in the Viewport: open the folders so it's visible here.
    SceneNode* sel = m_scene->selected();
    if (sel != m_lastSelected) {
        if (sel && !m_scene->isCharacterPart(sel)) reveal(sel);
        m_lastSelected = sel;
    }

    ImGui::Begin("Explorer");
    // Search, Roblox style: a name, c:Class (or is:), tag:Name, Property=value
    // (also > < >= <= ~=), and "or" between searches.
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - (m_filter.empty() ? 0 : 26));
    ImGui::InputTextWithHint("##filter", "Filter workspace (Ctrl+Shift+X)", &m_filter);
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_X, false))
        ImGui::SetKeyboardFocusHere(-1);
    if (ImGui::IsItemHovered() && m_filter.empty())
        ImGui::SetTooltip("Examples:  door    c:Script    tag:Enemy    Anchored=false    Transparency>0.5    c:Part or c:Model");
    if (!m_filter.empty()) {
        ImGui::SameLine(0, 2);
        if (ImGui::Button("x", ImVec2(22, 0))) m_filter.clear();
    }
    ImGui::BeginChild("##tree");

    if (SceneNode* root = m_scene->root())
        drawNode(root);

    // The other services, in Roblox's order. Lighting: the sky, sun and fog settings.
    // StarterPlayer: the character everyone spawns as (it goes into the Workspace when
    // the game runs). The rest hold what you put in them.
    if (m_filter.empty()) {
        using Id = Icons::Id;
        {
            const bool live = playing && playing();
            std::vector<std::string> who;
            if (live) {
                if (Player* p = m_scene->player()) if (SceneNode* r = m_scene->findById(p->rootId())) who.push_back(r->name);
                for (auto& rc : m_scene->remotes()) who.push_back(rc.name);
            }
            if (serviceRow("Players", (int)Id::Player, !who.empty(), false)) {
                for (const std::string& n : who) { ImGui::TreeNodeEx(n.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen, "%s", n.c_str()); }
                ImGui::TreePop();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everyone in the game (while it runs).");
        }
        if (serviceRow("Lighting", (int)Icons::Id::Lighting, false, false)) ImGui::TreePop();
        if (ImGui::IsItemClicked() && onLighting) onLighting();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sky, sun, fog and colours (opens the Lighting settings)");
        SceneNode* character = nullptr;
        for (auto& c : m_scene->root()->children)
            if (m_scene->isCharacterRoot(c->id)) character = c.get();
        const bool inWorkspace = playing && playing();
        folderService("ReplicatedFirst", (int)Id::Folder, "Loads first on every player's computer (loading screens).");
        folderService("ReplicatedStorage", (int)Id::Folder, "Things scripts copy into the game (on the server and every player's computer).");
        folderService("ServerScriptService", (int)Id::Script, "Scripts that run the game. Nobody sees them.");
        folderService("ServerStorage", (int)Id::Folder, "Things only server scripts use (maps, prizes). Not in the world until a script clones them.");
        folderService("StarterGui", (int)Id::ScreenGui, "The game's on-screen UI (ScreenGuis). Everyone gets a copy.");
        folderService("StarterPack", (int)Id::Tool, "Tools everyone spawns with.");
        if (serviceRow("StarterPlayer", (int)Icons::Id::Player, character && !inWorkspace, false)) {
            if (character && !inWorkspace) drawNode(character);
            ImGui::TreePop();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(inWorkspace ? "The character is in the Workspace while the game runs."
                                          : "The character everyone spawns as. Dress it, add scripts to it (its Animate script plays its animations).");
        folderService("Teams", (int)Id::Team, "Teams players can be on. Insert a Team here.");
        folderService("SoundService", (int)Id::Sound, "Sounds for the whole game (music, sound effects scripts play).");
        folderService("Chat", (int)Id::Output, "Chat settings and scripts.");
        folderService("TextChatService", (int)Id::Output, "Text chat settings and scripts.");
        if (m_showAllServices) {
            // The services scripts reach with game:GetService(...). Nothing to put in them here.
            static const char* hidden[] = {"RunService", "UserInputService", "ContextActionService", "TweenService", "Debris",
                "CollectionService", "HttpService", "DataStoreService", "MemoryStoreService", "MessagingService", "MarketplaceService",
                "BadgeService", "GamePassService", "PathfindingService", "PhysicsService", "TeleportService", "SocialService",
                "GroupService", "InsertService", "ContentProvider", "PolicyService", "LocalizationService", "TextService",
                "GuiService", "HapticService", "ProximityPromptService", "AssetService", "AnalyticsService", "LogService",
                "ScriptContext", "Stats", "TestService", "MaterialService", "VoiceChatService", "ChangeHistoryService", "Selection"};
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            for (const char* n : hidden) {
                if (serviceRow(n, (int)Id::Settings, false, false)) ImGui::TreePop();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scripts use it with game:GetService(\"%s\").", n);
            }
            ImGui::PopStyleColor();
        }
    }

    // Right-click empty space: Insert Object into the Workspace.
    if (onInsertNamed && ImGui::BeginPopupContextWindow("##explorerEmpty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("Insert Object")) { insertMenu(nullptr); ImGui::EndMenu(); }
        ImGui::MenuItem("Show All Services", nullptr, &m_showAllServices);
        ImGui::EndPopup();
    }

    // Click on empty space to clear the selection.
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_scene->deselect();

    ImGui::EndChild();
    ImGui::End();

    // Deferred so we never change the tree while drawing it.
    if (m_pendingDelete) {
        if (m_pendingDelete->selected) {
            for (SceneNode* n : m_scene->selectionRoots())
                if (!m_scene->isProtected(n)) m_scene->removeNode(n);
        } else {
            m_scene->removeNode(m_pendingDelete);
        }
        m_pendingDelete = nullptr;
    }
    if (!m_dragNodes.empty() && m_dropTarget) {
        if (!m_scene->isCharacterPart(m_dropTarget) || m_dropTarget == m_scene->root())
            for (SceneNode* n : m_dragNodes)
                if (!m_scene->isProtected(n) && !m_scene->isCharacterPart(n)) m_scene->reparent(n, m_dropTarget);
        m_open[m_dropTarget->id] = true;
        m_dragNodes.clear();
        m_dropTarget = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Explorer search
// ---------------------------------------------------------------------------

namespace {
std::string low(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

std::string className(const SceneNode& n) {
    switch (n.kind) {
        case NodeKind::Model:      return n.parent ? "Model" : "Workspace";
        case NodeKind::Script:     return n.isModule ? "ModuleScript" : n.isLocal ? "LocalScript" : "Script";
        case NodeKind::Remote:     return n.remoteFunction ? "RemoteFunction" : "RemoteEvent";
        case NodeKind::Prompt:     return "ProximityPrompt";
        case NodeKind::Highlight:  return "Highlight";
        case NodeKind::Trail:      return "Trail";
        case NodeKind::Beam:       return "Beam";
        case NodeKind::Light:      return n.lightType == LightType::Spot ? "SpotLight" : "PointLight";
        case NodeKind::Sound:      return "Sound";
        case NodeKind::Attachment: return "Attachment";
        case NodeKind::ForceField: return "ForceField";
        case NodeKind::Tool:       return "Tool";
        case NodeKind::Value:      return n.valueClass();
        case NodeKind::Decal:      return "Decal";
        case NodeKind::Animation:  return "Animation";
        case NodeKind::FluidSystem:  return "FluidSystem";
        case NodeKind::FluidEmitter: return "FluidEmitter";
        case NodeKind::Mover:        return kMoverClassNames[(int)n.mover.type];
        case NodeKind::Gui:        return kGuiClassNames[(int)n.gui.type];
        case NodeKind::Constraint: return std::string(kConstraintNames[(int)n.constraintType]) + "Constraint";
        default: return n.name == "SpawnLocation" ? "SpawnLocation" : "Part";
    }
}

// A property as text, for Name=value searches.
bool property(const SceneNode& n, const std::string& key, std::string& out) {
    auto f = [](float v) { std::ostringstream o; o << v; return o.str(); };
    std::string k = low(key);
    if (k == "name") out = n.name;
    else if (k == "anchored") out = n.anchored ? "true" : "false";
    else if (k == "cancollide") out = n.canCollide ? "true" : "false";
    else if (k == "locked") out = n.locked ? "true" : "false";
    else if (k == "visible") out = n.visible ? "true" : "false";
    else if (k == "enabled") out = n.enabled ? "true" : "false";
    else if (k == "transparency") out = f(n.transparency);
    else if (k == "material") out = kMaterialNames[(int)n.material];
    else if (k == "position.x") out = f(n.transform.position.x);
    else if (k == "position.y") out = f(n.transform.position.y);
    else if (k == "position.z") out = f(n.transform.position.z);
    else if (k == "size.x") out = f(n.transform.scale.x);
    else if (k == "size.y") out = f(n.transform.scale.y);
    else if (k == "size.z") out = f(n.transform.scale.z);
    else if (const Attribute* a = n.findAttribute(key)) {
        switch (a->type) {
            case Attribute::Bool: out = a->b ? "true" : "false"; break;
            case Attribute::Number: out = f((float)a->n); break;
            case Attribute::String: out = a->s; break;
            default: return false;
        }
    } else return false;
    return true;
}

bool termMatches(const SceneNode& n, const std::string& term) {
    std::string t = low(term);
    if (t.rfind("c:", 0) == 0 || t.rfind("classname:", 0) == 0 || t.rfind("is:", 0) == 0) {
        std::string want = t.substr(t.find(':') + 1);
        std::string cls = low(className(n));
        if (t.rfind("is:", 0) == 0 && want == "basepart") return n.isPart();
        return cls == want || (want == "part" && n.isPart());
    }
    if (t.rfind("tag:", 0) == 0) {
        std::string want = t.substr(4);
        for (auto& tag : n.tags) if (low(tag) == want) return true;
        return false;
    }
    if (t.rfind("name:", 0) == 0) return low(n.name).find(t.substr(5)) != std::string::npos;
    static const char* ops[] = {">=", "<=", "~=", "=", ">", "<"};
    for (const char* op : ops) {
        size_t at = t.find(op);
        if (at == std::string::npos || at == 0) continue;
        std::string key = term.substr(0, at), want = low(term.substr(at + std::strlen(op)));
        std::string have;
        if (!property(n, key, have)) return false;
        have = low(have);
        std::string o = op;
        if (o == "=") return have == want || have.find(want) != std::string::npos;
        if (o == "~=") return have != want;
        double a = std::atof(have.c_str()), b = std::atof(want.c_str());
        return o == ">" ? a > b : o == "<" ? a < b : o == ">=" ? a >= b : a <= b;
    }
    return low(n.name).find(t) != std::string::npos;
}
} // namespace

bool OutlinerPanel::matches(const SceneNode* node) const {
    // "a b" = both must match; "a or b" = either.
    std::istringstream in(m_filter);
    std::string word;
    bool anyGroup = false, groupOk = true;
    while (in >> word) {
        if (low(word) == "or") { if (groupOk) return true; groupOk = true; anyGroup = false; continue; }
        anyGroup = true;
        if (!termMatches(*node, word)) groupOk = false;
    }
    return anyGroup && groupOk;
}

bool OutlinerPanel::anyMatch(const SceneNode* node) const {
    if (matches(node)) return true;
    for (auto& c : node->children) if (!c->internal && anyMatch(c.get())) return true;
    return false;
}
