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
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

using nlohmann::json;

namespace {

constexpr float kNodeW = 230.0f;

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

// Shows a field and changes it; true if it changed.
// (left / right: the block's edges on screen)
bool fieldUi(const VisualScript::Field& f, std::string& v, float left, float right) {
    using VisualScript::Field;
    bool changed = false;
    const float valueX = left + 84;
    ImGui::SetCursorScreenPos(ImVec2(left + 8, ImGui::GetCursorScreenPos().y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", f.label);
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(valueX, ImGui::GetCursorScreenPos().y));
    ImGui::SetNextItemWidth(right - valueX);
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
            ImGui::SetCursorScreenPos(ImVec2(valueX, ImGui::GetCursorScreenPos().y));
            ImGui::SetNextItemWidth(right - valueX);
            if (ImGui::InputTextWithHint("##path", "Workspace.Door", &path)) { v = "path:" + path; changed = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where it is in the Explorer, with dots: Workspace.House.Door");
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
                if (!b.event) m_blocksSelected = id;
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

    // --- Middle: the board ---
    ImGui::BeginChild("##board", ImVec2(boardW, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 boardMin = ImGui::GetCursorScreenPos();
    const ImVec2 boardSize = ImGui::GetContentRegionAvail();
    const ImVec2 origin(boardMin.x + m_blocksPan.x, boardMin.y + m_blocksPan.y);
    dl->AddRectFilled(boardMin, ImVec2(boardMin.x + boardSize.x, boardMin.y + boardSize.y), IM_COL32(32, 33, 36, 255));
    for (float x = std::fmod(m_blocksPan.x, 32.0f); x < boardSize.x; x += 32.0f)
        dl->AddLine(ImVec2(boardMin.x + x, boardMin.y), ImVec2(boardMin.x + x, boardMin.y + boardSize.y), IM_COL32(44, 45, 50, 255));
    for (float y = std::fmod(m_blocksPan.y, 32.0f); y < boardSize.y; y += 32.0f)
        dl->AddLine(ImVec2(boardMin.x, boardMin.y + y), ImVec2(boardMin.x + boardSize.x, boardMin.y + y), IM_COL32(44, 45, 50, 255));

    // Background: drag it (any mouse button but left) to look around; right-click to add.
    ImGui::SetCursorScreenPos(boardMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##boardbg", boardSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        m_blocksPan.x += ImGui::GetIO().MouseDelta.x;
        m_blocksPan.y += ImGui::GetIO().MouseDelta.y;
        m_blocksPanned = true;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) m_blocksSelected = 0;
    if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        if (!m_blocksPanned && !m_playing) {
            m_blocksMenuAt = ImVec2(ImGui::GetMousePos().x - origin.x, ImGui::GetMousePos().y - origin.y);
            ImGui::OpenPopup("##addblock");
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_blocksPanned = false;
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

    struct PinAt { int node; std::string pin; ImVec2 at; };
    std::vector<PinAt> outs;
    std::map<int, ImVec2> ins;
    int remove = 0;
    bool released = false;

    ImDrawListSplitter split;
    split.Split(dl, 2);
    for (auto& n : g["nodes"]) {
        const int id = n.value("id", 0);
        const auto* b = VisualScript::find(n.value("type", std::string()));
        if (!b) continue;
        ImGui::PushID(id);
        const ImVec2 p(origin.x + n.value("x", 0.0f), origin.y + n.value("y", 0.0f));
        const float titleH = ImGui::GetFrameHeight() + 4;

        split.SetCurrentChannel(dl, 1);
        // Title: drag to move, right-click to remove.
        ImGui::SetCursorScreenPos(p);
        ImGui::InvisibleButton("##title", ImVec2(kNodeW, titleH));
        if (ImGui::IsItemActivated()) m_blocksSelected = id;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f) && !m_playing) {
            n["x"] = n.value("x", 0.0f) + ImGui::GetIO().MouseDelta.x;
            n["y"] = n.value("y", 0.0f) + ImGui::GetIO().MouseDelta.y;
            m_blocksMoving = true;
        }
        if (ImGui::IsItemDeactivated() && m_blocksMoving) {
            n["x"] = std::round(n.value("x", 0.0f));
            n["y"] = std::round(n.value("y", 0.0f));
            m_blocksMoving = false;
            released = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n\nDrag to move. Right-click to remove.", b->tip);
        if (ImGui::BeginPopupContextItem("##nodemenu")) {
            ImGui::BeginDisabled(m_playing);
            if (ImGui::MenuItem("Remove block")) remove = id;
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        dl->AddText(ImVec2(p.x + 22, p.y + 4), IM_COL32(255, 255, 255, 255), b->title);

        // Fields
        ImGui::SetCursorScreenPos(ImVec2(p.x + 8, p.y + titleH + 4));
        ImGui::BeginDisabled(m_playing);
        if (!n["f"].is_object()) n["f"] = json::object();
        for (const auto& f : b->fields) {
            ImGui::PushID(f.key);
            std::string v = n["f"].value(f.key, std::string(f.def));
            if (fieldUi(f, v, p.x, p.x + kNodeW - 8)) { n["f"][f.key] = v; changed = true; }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        float y = b->fields.empty() ? p.y + titleH + 4 : ImGui::GetCursorScreenPos().y + 2;

        // Ways out ("next", "then", "else"...), one per row on the right.
        for (const char* o : b->outs) {
            const ImVec2 pin(p.x + kNodeW, y + 9);
            const char* label = pinLabel(o);
            const ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(pin.x - 14 - ts.x, y), IM_COL32(200, 200, 205, 255), label);
            outs.push_back({id, o, pin});
            ImGui::SetCursorScreenPos(ImVec2(pin.x - 8, pin.y - 8));
            ImGui::PushID(o);
            ImGui::InvisibleButton("##out", ImVec2(16, 16));
            if (ImGui::IsItemActivated() && !m_playing) { m_blocksDragFrom = id; m_blocksDragPin = o; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag from here to the block that comes next.\nRight-click to cut the line.");
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !m_playing) { unlinkFrom(g, id, o); changed = true; }
            ImGui::PopID();
            y += 22;
        }
        const float h = y - p.y + 4;

        split.SetCurrentChannel(dl, 0);
        const ImVec2 q(p.x + kNodeW, p.y + h);
        const bool sel = m_blocksSelected == id;
        dl->AddRectFilled(p, q, IM_COL32(52, 54, 60, 245), 6.0f);
        dl->AddRectFilled(p, ImVec2(q.x, p.y + titleH), categoryColor(b->category), 6.0f, ImDrawFlags_RoundCornersTop);
        dl->AddRect(p, q, sel ? IM_COL32(255, 255, 255, 230) : IM_COL32(20, 20, 22, 255), 6.0f, 0, sel ? 2.0f : 1.0f);
        if (!b->event) {
            const ImVec2 in(p.x, p.y + titleH * 0.5f);
            ins[id] = in;
            dl->AddCircleFilled(in, 6, IM_COL32(230, 230, 235, 255));
            dl->AddCircle(in, 6, IM_COL32(20, 20, 22, 255));
        } else {
            dl->AddTriangleFilled(ImVec2(p.x + 8, p.y + 6), ImVec2(p.x + 8, p.y + titleH - 6), ImVec2(p.x + 16, p.y + titleH * 0.5f),
                                  IM_COL32(255, 255, 255, 220));
        }
        for (const auto& o : outs)
            if (o.node == id) {
                dl->AddCircleFilled(o.at, 6, IM_COL32(230, 230, 235, 255));
                dl->AddCircle(o.at, 6, IM_COL32(20, 20, 22, 255));
            }
        ImGui::PopID();
    }

    // Lines between blocks
    split.SetCurrentChannel(dl, 1);
    auto curve = [&](ImVec2 a, ImVec2 b, ImU32 col) {
        const float dx = std::max(40.0f, std::fabs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, 3.0f);
    };
    for (const auto& l : g["links"]) {
        const int from = l.value("from", 0), to = l.value("to", 0);
        const std::string pin = l.value("pin", std::string("next"));
        auto in = ins.find(to);
        if (in == ins.end()) continue;
        for (const auto& o : outs)
            if (o.node == from && o.pin == pin) curve(o.at, in->second, pin == "else" ? IM_COL32(230, 120, 110, 255) : IM_COL32(235, 235, 240, 255));
    }
    if (m_blocksDragFrom) {
        for (const auto& o : outs)
            if (o.node == m_blocksDragFrom && o.pin == m_blocksDragPin) curve(o.at, ImGui::GetMousePos(), IM_COL32(255, 210, 90, 255));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // Dropped on a block: connect to it (the nearest one under the mouse).
            const ImVec2 m = ImGui::GetMousePos();
            int best = 0;
            float bestD = 1e9f;
            for (auto& n : g["nodes"]) {
                const int id = n.value("id", 0);
                auto in = ins.find(id);
                if (in == ins.end() || id == m_blocksDragFrom) continue;
                const ImVec2 p(origin.x + n.value("x", 0.0f), origin.y + n.value("y", 0.0f));
                const bool over = m.x >= p.x - 16 && m.x <= p.x + kNodeW && m.y >= p.y - 16 && m.y <= p.y + 60;
                const float d = std::hypot(m.x - in->second.x, m.y - in->second.y);
                if ((over || d < 24) && d < bestD) { best = id; bestD = d; }
            }
            if (best) {
                unlinkFrom(g, m_blocksDragFrom, m_blocksDragPin);
                g["links"].push_back({{"from", m_blocksDragFrom}, {"pin", m_blocksDragPin}, {"to", best}});
                changed = true;
            }
            m_blocksDragFrom = 0;
        }
    }
    split.Merge(dl);
    if (remove) { removeNode(g, remove); changed = true; }
    if (released) changed = true;
    if (g["nodes"].empty())
        dl->AddText(ImVec2(boardMin.x + 20, boardMin.y + 20), IM_COL32(170, 170, 175, 255),
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
