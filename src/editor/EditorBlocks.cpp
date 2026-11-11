// Visual Scripting ("Blocks"): make a Script by connecting blocks on a board, like
// "When touched" -> "If it's a player" -> "Give points". The blocks are kept in the script's
// code (VisualScript.h), and the Lua they make is shown next to them to learn from.
#include "Editor.h"
#include "VisualScript.h"
#include "panels/ScriptEditorPanel.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <imnodes.h>
#include <misc/cpp/imgui_stdlib.h>

using nlohmann::json;

namespace {

constexpr float kNodeW = 230.0f;               // how wide a block is (inside its edges)
constexpr float kLabelW = 76.0f;               // a field's name...
constexpr float kValueW = kNodeW - kLabelW;    // ...and the box you type in

// The board's pins and lines are numbered for imnodes: block 7's "comes in" pin is
// 7*16, and its ways out ("next", "then"...) are 7*16+1, 7*16+2...
int pinIn(int node) { return node * 16; }
int pinOut(int node, int i) { return node * 16 + 1 + i; }
int pinNode(int pin) { return pin / 16; }
int pinSlot(int pin) { return pin % 16; }   // 0 = comes in, 1.. = way out number+1

// imnodes keeps its state (where you've scrolled to, what's selected) here. Made the
// first time the window shows and kept until Studio closes.
ImNodesEditorContext* board() {
    static ImNodesEditorContext* ctx = nullptr;
    if (!ctx) {
        ImNodes::CreateContext();
        ctx = ImNodes::EditorContextCreate();
        ImNodes::EditorContextSet(ctx);
        ImNodes::StyleColorsDark();
        ImNodesStyle& st = ImNodes::GetStyle();
        st.GridSpacing = 32.0f;
        st.NodeCornerRounding = 6.0f;
        st.NodePadding = ImVec2(8, 6);
        st.LinkThickness = 3.0f;
        st.PinCircleRadius = 6.0f;
        st.PinHoverRadius = 12.0f;
        st.Colors[ImNodesCol_GridBackground] = IM_COL32(32, 33, 36, 255);
        st.Colors[ImNodesCol_GridLine] = IM_COL32(44, 45, 50, 255);
        st.Colors[ImNodesCol_NodeBackground] = IM_COL32(52, 54, 60, 245);
        st.Colors[ImNodesCol_NodeBackgroundHovered] = IM_COL32(58, 60, 67, 245);
        st.Colors[ImNodesCol_NodeBackgroundSelected] = IM_COL32(62, 64, 72, 245);
        st.Colors[ImNodesCol_NodeOutline] = IM_COL32(20, 20, 22, 255);
        st.Colors[ImNodesCol_Pin] = IM_COL32(230, 230, 235, 255);
        st.Colors[ImNodesCol_PinHovered] = IM_COL32(255, 210, 90, 255);
        st.Colors[ImNodesCol_Link] = IM_COL32(235, 235, 240, 255);
        st.Colors[ImNodesCol_LinkHovered] = IM_COL32(255, 210, 90, 255);
        st.Colors[ImNodesCol_LinkSelected] = IM_COL32(255, 210, 90, 255);
        // Drag the board with the right (or middle) mouse button, like before.
        ImNodes::GetIO().AltMouseButton = ImGuiMouseButton_Right;
    }
    ImNodes::EditorContextSet(ctx);
    return ctx;
}

ImU32 lighter(ImU32 c, int add) {
    auto ch = [&](int shift) { return (ImU32)std::min(255, (int)((c >> shift) & 0xFF) + add) << shift; };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | (c & IM_COL32_A_MASK);
}

ImU32 categoryColor(const std::string& c) {
    if (c == "Events") return IM_COL32(196, 140, 30, 255);
    if (c == "Actions") return IM_COL32(52, 120, 200, 255);
    if (c == "Players") return IM_COL32(60, 160, 90, 255);
    return IM_COL32(150, 80, 180, 255);   // Control
}

json* nodeById(json& g, int id) {
    for (auto& n : g["nodes"]) if (n.value("id", 0) == id) return &n;
    return nullptr;
}

int nextId(const json& g) {
    int top = 0;
    for (const auto& n : g["nodes"]) top = std::max(top, n.value("id", 0));
    return top + 1;
}

json newNode(const VisualScript::Block& b, int id, float x, float y) {
    json f = json::object();
    for (const auto& field : b.fields) f[field.key] = field.def;
    return {{"id", id}, {"type", b.type}, {"x", std::round(x)}, {"y", std::round(y)}, {"f", f}};
}

void unlinkFrom(json& g, int from, const std::string& pin) {
    auto& l = g["links"];
    l.erase(std::remove_if(l.begin(), l.end(), [&](const json& e) {
        return e.value("from", 0) == from && e.value("pin", std::string("next")) == pin; }), l.end());
}

void removeNode(json& g, int id) {
    auto& n = g["nodes"];
    n.erase(std::remove_if(n.begin(), n.end(), [&](const json& e) { return e.value("id", 0) == id; }), n.end());
    auto& l = g["links"];
    l.erase(std::remove_if(l.begin(), l.end(), [&](const json& e) {
        return e.value("from", 0) == id || e.value("to", 0) == id; }), l.end());
}

// Shows a field and changes it; true if it changed. Laid out inside a block on
// the board: the label on the left, the box to type in on the right.
bool fieldUi(const VisualScript::Field& f, std::string& v) {
    using VisualScript::Field;
    bool changed = false;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", f.label);
    ImGui::SameLine(kLabelW);
    ImGui::SetNextItemWidth(kValueW);
    switch (f.kind) {
    case Field::Number:
        changed = ImGui::InputText("##v", &v, ImGuiInputTextFlags_CharsDecimal);
        break;
    case Field::Color: {
        float c[3] = {1, 0, 0};
        int r = 255, gr = 0, b = 0;
        if (std::sscanf(v.c_str(), "%d , %d , %d", &r, &gr, &b) == 3) { c[0] = r / 255.0f; c[1] = gr / 255.0f; c[2] = b / 255.0f; }
        if (ImGui::ColorEdit3("##v", c, ImGuiColorEditFlags_DisplayRGB)) {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%d, %d, %d", (int)std::lround(c[0] * 255), (int)std::lround(c[1] * 255), (int)std::lround(c[2] * 255));
            v = buf;
            changed = true;
        }
        break;
    }
    case Field::Target: {
        const char* keys[] = {"this", "other", "character", "path:"};
        int at = v == "this" ? 0 : v == "other" ? 1 : v == "character" ? 2 : 3;
        if (ImGui::BeginCombo("##v", VisualScript::targetLabel(v))) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(VisualScript::targetLabel(keys[i]), i == at)) {
                    v = i == 3 ? (at == 3 ? v : std::string("path:Workspace.Part")) : keys[i];
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (v.rfind("path:", 0) == 0) {
            std::string path = v.substr(5);
            ImGui::Indent(kLabelW);
            ImGui::SetNextItemWidth(kValueW);
            if (ImGui::InputTextWithHint("##path", "Workspace.Door", &path)) { v = "path:" + path; changed = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where it is in the Explorer, with dots: Workspace.House.Door");
            ImGui::Unindent(kLabelW);
        }
        break;
    }
    default:
        changed = ImGui::InputText("##v", &v);
        break;
    }
    return changed;
}

// What a way out is called on a block.
const char* pinLabel(const std::string& pin) {
    static const std::pair<const char*, const char*> names[] = {
        {"next", "Then"},
        {"then", "If yes"},
        {"else", "If no"},
        {"do", "Do"}};
    for (const auto& n : names) if (pin == n.first) return n.second;
    return "Then";
}

// "Workspace.Door.BlockScript"
std::string where(const SceneNode* n) {
    std::string out;
    for (; n; n = n->parent) if (!n->name.empty()) out = out.empty() ? n->name : n->name + "." + out;
    return out;
}

} // namespace

SceneNode* Editor::newBlockScript() {
    SceneNode* parent = m_scene->selected();
    if (parent && !parent->isPart()) parent = nullptr;
    auto s = std::make_unique<SceneNode>("BlockScript", NodeKind::Script);
    json g = {{"nodes", json::array()}, {"links", json::array()}};
    const VisualScript::Block* start = VisualScript::find(parent ? "touched" : "start");
    g["nodes"].push_back(newNode(*start, 1, 40, 40));
    std::vector<std::string> problems;
    s->source = VisualScript::scriptSource(g, problems);
    SceneNode* made = m_scene->insert(std::move(s), parent);
    m_blocksScript = made->id;
    m_showBlocks = true;
    return made;
}

void Editor::openBlocks(SceneNode* script) {
    m_blocksScript = script ? script->id : 0;
    m_blocksWritten.clear();
    m_showBlocks = true;
    ImGui::SetWindowFocus("Visual Scripting");
}

void Editor::renderBlocks() {
    if (!m_showBlocks) return;
    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Visual Scripting", &m_showBlocks)) { ImGui::End(); return; }

    // --- Which script ---
    std::vector<SceneNode*> scripts;
    m_scene->forEach([&](SceneNode* n) { if (n->isScript() && VisualScript::isBlocks(n->source)) scripts.push_back(n); });
    SceneNode* script = m_blocksScript ? m_scene->findById(m_blocksScript) : nullptr;
    if (script && (!script->isScript() || !VisualScript::isBlocks(script->source))) script = nullptr;
    if (!script && !scripts.empty()) script = scripts.front();
    m_blocksScript = script ? script->id : 0;

    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button("New block script")) { script = newBlockScript(); m_blocksWritten.clear(); }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Makes a Script that you build with blocks.\nSelect a part first to put it in that part.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260);
    std::string shown = script ? where(script) : std::string("(none yet)");
    if (ImGui::BeginCombo("Script", shown.c_str())) {
        for (SceneNode* s : scripts)
            if (ImGui::Selectable(where(s).c_str(), s == script)) { script = s; m_blocksScript = s->id; m_blocksWritten.clear(); }
        ImGui::EndCombo();
    }
    if (script) {
        ImGui::SameLine();
        if (ImGui::Button("Show the code")) openScript(script);
    }
    if (!script) {
        ImGui::Spacing();
        ImGui::TextWrapped("Make scripts without typing code: connect blocks like \"When touched\", \"If it's a player\" and \"Give points\". "
                           "The blocks turn into real Lua code you can read (and learn from).");
        ImGui::TextWrapped("Press \"New block script\" to start. Select a part first to make blocks for that part.");
        ImGui::End();
        return;
    }

    // Load the blocks again when the code changed somewhere else (undo, the AI, Team Create...).
    if (script->source != m_blocksWritten) {
        bool edited = false;
        m_blocksGraph = VisualScript::graphOf(script->source, &edited);
        if (!m_blocksGraph.is_object()) m_blocksGraph = json::object();
        if (!m_blocksGraph.contains("nodes") || !m_blocksGraph["nodes"].is_array()) m_blocksGraph["nodes"] = json::array();
        if (!m_blocksGraph.contains("links") || !m_blocksGraph["links"].is_array()) m_blocksGraph["links"] = json::array();
        m_blocksEdited = edited;
        m_blocksWritten = script->source;
        m_blocksProblems.clear();
        m_blocksLua = VisualScript::toLua(m_blocksGraph, m_blocksProblems);
    }
    json& g = m_blocksGraph;
    bool changed = false;     // write the script at the end of this frame

    if (m_blocksEdited) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        ImGui::TextWrapped("This script's code was changed by hand. Changing the blocks will write over those changes.");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_playing);
        if (ImGui::SmallButton("Keep the code, stop using blocks")) {
            const size_t at = script->source.find("\n\n", script->source.find("]==]"));
            if (at != std::string::npos) script->source = script->source.substr(at + 2);
            openScript(script);
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }
        ImGui::EndDisabled();
    }

    // --- Left: the blocks you can add ---
    const float paletteW = 215.0f;
    ImGui::BeginChild("##palette", ImVec2(paletteW, 0), ImGuiChildFlags_Borders);
    const char* cats[] = {"Events", "Actions", "Players", "Control"};
    for (const char* cat : cats) {
        ImGui::SeparatorText(cat);
        for (const auto& b : VisualScript::blocks()) {
            if (std::string(b.category) != cat) continue;
            ImVec4 col = ImGui::ColorConvertU32ToFloat4(categoryColor(cat));
            col.w = 0.55f;
            ImGui::PushStyleColor(ImGuiCol_Button, col);
            ImGui::BeginDisabled(m_playing);
            if (ImGui::Button(b.title, ImVec2(-1, 0))) {
                const int id = nextId(g);
                const float off = (float)((id * 24) % 160);
                g["nodes"].push_back(newNode(b, id, -m_blocksPan.x + 60 + off, -m_blocksPan.y + 60 + off));
                // Hook it under the selected block when there's a free "next".
                if (json* sel = nodeById(g, m_blocksSelected)) {
                    bool free = true;
                    for (const auto& l : g["links"])
                        if (l.value("from", 0) == m_blocksSelected && l.value("pin", std::string()) == "next") free = false;
                    const auto* sb = VisualScript::find(sel->value("type", std::string()));
                    if (free && !b.event && sb && std::find_if(sb->outs.begin(), sb->outs.end(), [](const char* o) { return std::string(o) == "next"; }) != sb->outs.end()) {
                        g["links"].push_back({{"from", m_blocksSelected}, {"pin", "next"}, {"to", id}});
                        g["nodes"].back()["x"] = (*sel)["x"];
                        g["nodes"].back()["y"] = sel->value("y", 0.0f) + 40.0f + 30.0f * (float)sb->fields.size() + 22.0f * (float)sb->outs.size();
                    }
                }
                if (!b.event) { m_blocksSelected = id; m_blocksSelectNext = id; }
                changed = true;
            }
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", b.tip);
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- Right: problems and the code it makes ---
    const float codeW = std::max(240.0f, ImGui::GetContentRegionAvail().x * 0.3f);
    const float boardW = ImGui::GetContentRegionAvail().x - codeW - ImGui::GetStyle().ItemSpacing.x;

    // --- Middle: the board (imnodes) ---
    ImGui::BeginChild("##board", ImVec2(boardW, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    const ImVec2 boardMin = ImGui::GetCursorScreenPos();
    board();
    ImNodes::BeginNodeEditor();

    // Each way out, so lines can find their pins: (block, "then") -> pin number.
    std::map<std::pair<int, std::string>, int> outPin;
    std::map<int, std::string> pinName;       // pin number -> "next", "then"...
    for (auto& n : g["nodes"]) {
        const int id = n.value("id", 0);
        const auto* b = VisualScript::find(n.value("type", std::string()));
        if (!b || id <= 0) continue;
        ImGui::PushID(id);
        // Where it is comes from the script; dragging moves it (read back below).
        ImNodes::SetNodeGridSpacePos(id, ImVec2(n.value("x", 0.0f), n.value("y", 0.0f)));
        ImNodes::SetNodeDraggable(id, !m_playing);
        const ImU32 col = categoryColor(b->category);
        ImNodes::PushColorStyle(ImNodesCol_TitleBar, col);
        ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, lighter(col, 20));
        ImNodes::PushColorStyle(ImNodesCol_TitleBarSelected, lighter(col, 40));
        ImNodes::BeginNode(id);

        // Title (with the "comes in" pin on its left; events start a chain, so they have none).
        ImNodes::BeginNodeTitleBar();
        if (!b->event) {
            ImNodes::BeginInputAttribute(pinIn(id));
            ImGui::TextUnformatted(b->title);
            ImNodes::EndInputAttribute();
        } else {
            ImGui::Text("> %s", b->title);
        }
        ImNodes::EndNodeTitleBar();
        ImGui::Dummy(ImVec2(kNodeW, 0));   // keeps every block the same width

        // Fields
        ImGui::BeginDisabled(m_playing);
        if (!n["f"].is_object()) n["f"] = json::object();
        for (const auto& f : b->fields) {
            ImGui::PushID(f.key);
            std::string v = n["f"].value(f.key, std::string(f.def));
            if (fieldUi(f, v)) { n["f"][f.key] = v; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndDisabled();

        // Ways out ("next", "then", "else"...), one per row on the right.
        for (size_t i = 0; i < b->outs.size(); ++i) {
            const int pin = pinOut(id, (int)i);
            outPin[{id, b->outs[i]}] = pin;
            pinName[pin] = b->outs[i];
            ImNodes::BeginOutputAttribute(pin);
            const char* label = pinLabel(b->outs[i]);
            ImGui::Indent(kNodeW - ImGui::CalcTextSize(label).x);
            ImGui::TextUnformatted(label);
            ImGui::Unindent(kNodeW - ImGui::CalcTextSize(label).x);
            ImNodes::EndOutputAttribute();
        }
        ImNodes::EndNode();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImNodes::PopColorStyle();
        ImGui::PopID();
    }

    // Lines between blocks (numbered by where they are in the script's list).
    for (size_t i = 0; i < g["links"].size(); ++i) {
        const auto& l = g["links"][i];
        const int to = l.value("to", 0);
        const std::string pin = l.value("pin", std::string("next"));
        auto out = outPin.find({l.value("from", 0), pin});
        const json* target = nodeById(g, to);
        const auto* tb = target ? VisualScript::find(target->value("type", std::string())) : nullptr;
        if (out == outPin.end() || !tb || tb->event) continue;
        const bool no = pin == "else";
        if (no) ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(230, 120, 110, 255));
        ImNodes::Link((int)i, out->second, pinIn(to));
        if (no) ImNodes::PopColorStyle();
    }

    // A block added from the list on the left: select it on the board too.
    if (m_blocksSelectNext && nodeById(g, m_blocksSelectNext)) {
        ImNodes::ClearNodeSelection();
        ImNodes::SelectNode(m_blocksSelectNext);
    }
    m_blocksSelectNext = 0;
    if (!g["nodes"].empty()) ImNodes::MiniMap(0.18f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();
    m_blocksPan = ImNodes::EditorContextGetPanning();

    // Blocks dragged around: keep the new places (written once the mouse lets go).
    if (!m_playing)
        for (auto& n : g["nodes"]) {
            const int id = n.value("id", 0);
            if (!VisualScript::find(n.value("type", std::string())) || id <= 0) continue;
            const ImVec2 at = ImNodes::GetNodeGridSpacePos(id);
            if (std::fabs(at.x - n.value("x", 0.0f)) > 0.01f || std::fabs(at.y - n.value("y", 0.0f)) > 0.01f) {
                n["x"] = at.x;
                n["y"] = at.y;
                m_blocksMoving = true;
            }
        }
    if (m_blocksMoving && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        for (auto& n : g["nodes"]) { n["x"] = std::round(n.value("x", 0.0f)); n["y"] = std::round(n.value("y", 0.0f)); }
        m_blocksMoving = false;
        changed = true;
    }

    // What's selected (the list on the left hooks new blocks under it).
    const int selCount = ImNodes::NumSelectedNodes();
    std::vector<int> selected(selCount);
    if (selCount) ImNodes::GetSelectedNodes(selected.data());
    if (selCount == 1) m_blocksSelected = selected[0];
    else if (selCount == 0 && ImNodes::IsEditorHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_blocksSelected = 0;

    // A line was drawn: from a way out to a block's "comes in" pin (either way round).
    int a = 0, z = 0;
    if (ImNodes::IsLinkCreated(&a, &z) && !m_playing) {
        if (pinSlot(a) == 0) std::swap(a, z);
        if (pinSlot(a) != 0 && pinSlot(z) == 0 && pinNode(a) != pinNode(z) && pinName.count(a)) {
            unlinkFrom(g, pinNode(a), pinName[a]);
            g["links"].push_back({{"from", pinNode(a)}, {"pin", pinName[a]}, {"to", pinNode(z)}});
            changed = true;
        }
    }
    // ...or let go over a block instead of its pin: connect to that block.
    int dropped = 0, overNode = 0;
    if (ImNodes::IsLinkDropped(&dropped, false) && !m_playing && pinSlot(dropped) != 0 && pinName.count(dropped) &&
        ImNodes::IsNodeHovered(&overNode) && overNode != pinNode(dropped)) {
        const json* target = nodeById(g, overNode);
        const auto* tb = target ? VisualScript::find(target->value("type", std::string())) : nullptr;
        if (tb && !tb->event) {
            unlinkFrom(g, pinNode(dropped), pinName[dropped]);
            g["links"].push_back({{"from", pinNode(dropped)}, {"pin", pinName[dropped]}, {"to", overNode}});
            changed = true;
        }
    }

    // Right-click: on a way out cuts its line, on a line removes it, on a block offers
    // to remove it, and on the empty board (without dragging) adds a block there.
    int hoverPin = 0, hoverLink = 0, hoverNode = 0;
    const bool onPin = ImNodes::IsPinHovered(&hoverPin);
    const bool onLink = !onPin && ImNodes::IsLinkHovered(&hoverLink);
    const bool onNode = !onPin && !onLink && ImNodes::IsNodeHovered(&hoverNode);
    if (onPin && pinSlot(hoverPin) != 0) ImGui::SetTooltip("Drag from here to the block that comes next.\nRight-click to cut the line.");
    else if (onLink) ImGui::SetTooltip("Right-click to cut this line.");
    else if (onNode && !ImGui::IsAnyItemHovered())
        if (const json* hn = nodeById(g, hoverNode))
            if (const auto* hb = VisualScript::find(hn->value("type", std::string())))
                ImGui::SetTooltip("%s\n\nDrag to move. Right-click to remove.", hb->tip);
    const bool rightClick = ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImNodes::IsEditorHovered() &&
                            ImGui::GetIO().MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.0f;
    if (rightClick && !m_playing) {
        if (onPin && pinSlot(hoverPin) != 0 && pinName.count(hoverPin)) {
            unlinkFrom(g, pinNode(hoverPin), pinName[hoverPin]);
            changed = true;
        } else if (onLink && hoverLink >= 0 && hoverLink < (int)g["links"].size()) {
            g["links"].erase(g["links"].begin() + hoverLink);
            changed = true;
        } else if (onNode) {
            m_blocksMenuNode = hoverNode;
            ImGui::OpenPopup("##nodemenu");
        } else if (!onPin) {
            const ImVec2 m = ImGui::GetMousePos();
            m_blocksMenuAt = ImVec2(m.x - boardMin.x - m_blocksPan.x, m.y - boardMin.y - m_blocksPan.y);
            ImGui::OpenPopup("##addblock");
        }
    }
    int remove = 0;
    if (ImGui::BeginPopup("##nodemenu")) {
        if (ImGui::MenuItem("Remove block")) remove = m_blocksMenuNode;
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##addblock")) {
        ImGui::TextDisabled("Add a block");
        for (const char* cat : cats)
            if (ImGui::BeginMenu(cat)) {
                for (const auto& b : VisualScript::blocks())
                    if (std::string(b.category) == cat && ImGui::MenuItem(b.title)) {
                        g["nodes"].push_back(newNode(b, nextId(g), m_blocksMenuAt.x, m_blocksMenuAt.y));
                        changed = true;
                    }
                ImGui::EndMenu();
            }
        ImGui::EndPopup();
    }
    // Delete removes the selected blocks (when the board has focus and you're not typing).
    if (!m_playing && selCount && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        for (int id : selected) removeNode(g, id);
        ImNodes::ClearNodeSelection();
        m_blocksSelected = 0;
        changed = true;
    }
    if (remove) {
        removeNode(g, remove);
        ImNodes::ClearNodeSelection();
        if (m_blocksSelected == remove) m_blocksSelected = 0;
        changed = true;
    }
    if (g["nodes"].empty())
        ImGui::GetWindowDrawList()->AddText(ImVec2(boardMin.x + 20, boardMin.y + 20), IM_COL32(170, 170, 175, 255),
                                            "Add an event block from the left (like \"When touched\") to start.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##code", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (!m_blocksProblems.empty()) {
        ImGui::SeparatorText("Problems");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.5f, 1.0f));
        for (const auto& p : m_blocksProblems) ImGui::TextWrapped("%s", p.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::SeparatorText("The code it makes");
    ImGui::TextDisabled("This is real Lua: read it to learn how the blocks work.");
    ImGui::InputTextMultiline("##lua", &m_blocksLua, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndChild();

    if (changed && !m_playing && !m_blocksMoving) {
        m_blocksProblems.clear();
        script->source = VisualScript::scriptSource(g, m_blocksProblems);
        m_blocksWritten = script->source;
        m_blocksEdited = false;
        std::vector<std::string> again;
        m_blocksLua = VisualScript::toLua(g, again);
    }
    ImGui::End();
}
