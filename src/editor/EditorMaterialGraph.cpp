// The Material Graph window: make a material from nodes (noise, bricks, cells...) wired
// together, instead of from photos. Each box shows its picture; Apply bakes the Output
// into the material's ColorMap / NormalMap / RoughnessMap / MetalnessMap
// (src/scene/MaterialGraph.h does the work).
#include "Editor.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../renderer/Textures.h"
#include "../scene/MaterialGraph.h"
#include "../scene/Scene.h"
#include <stb_image_write.h>   // (its code is in renderer/Textures.cpp)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <filesystem>
#include <functional>
#include <imgui.h>

using MaterialGraph::Graph;
using MaterialGraph::Node;
using MaterialGraph::Type;

struct Editor::GraphUi {
    bool        show = false;
    uint64_t    variant = 0;              // the MaterialVariant it's for
    Graph       graph;
    std::string saved;                    // the graph as JSON, as last put in the variant
    std::string previewed;                // ...and as the previews were last made from
    ImVec2      scroll{0.0f, 0.0f};
    int         selected = 0;
    int         linkFrom = 0;             // dragging a wire out of this node's output
    int         size = 512;               // the pictures Apply makes
    ImVec2      addAt{0.0f, 0.0f};        // where the next added box goes
};

namespace {

constexpr float kNodeWidth = 176.0f, kPreview = 72.0f;

// Picture file names: the material's name, but only letters and digits.
std::string fileSafe(const std::string& s) {
    std::string out;
    for (char ch : s) out += std::isalnum((unsigned char)ch) ? ch : '_';
    return out.empty() ? std::string("Material") : out.substr(0, 40);
}

} // namespace

// Bake a graph into a MaterialVariant's pictures (PNG files in the games folder).
bool Editor::applyMaterialGraph(SceneNode* v, const MaterialGraph::Graph& graph, int size) {
    const MaterialGraph::Baked b = MaterialGraph::bake(graph, size);
    std::error_code ec;
    const std::filesystem::path folder = Paths::gamesFolder() / "Material Graphs";
    std::filesystem::create_directories(folder, ec);
    const std::string key = std::to_string(std::hash<std::string>{}(MaterialGraph::toJson(graph).dump()) % 100000000);
    auto save = [&](bool has, const MaterialGraph::Image& img, const char* what, std::string& into) {
        if (!has) { into.clear(); return true; }
        const std::string path = (folder / (fileSafe(v->name) + "-" + key + "-" + what + ".png")).string();
        const std::vector<unsigned char> px = MaterialGraph::rgba(img);
        if (!stbi_write_png(path.c_str(), img.size, img.size, 4, px.data(), img.size * 4)) return false;
        Textures::forget(path);
        into = path;
        return true;
    };
    MaterialVariantProps& p = v->variant;
    const bool ok = save(b.hasColor, b.color, "color", p.colorMap) && save(b.hasNormal, b.normal, "normal", p.normalMap) &&
                    save(b.hasRoughness, b.roughness, "roughness", p.roughnessMap) &&
                    save(b.hasMetalness, b.metalness, "metalness", p.metalnessMap);
    if (ok) Log::info("\"" + v->name + "\" made from its graph: its pictures are in " + folder.string());
    else Log::warn("Couldn't save the material's pictures in " + folder.string());
    return ok;
}

void Editor::openMaterialGraph(uint64_t variant) {
    if (!m_graphUi) m_graphUi = std::make_shared<GraphUi>();
    GraphUi& ui = *m_graphUi;
    ui.show = true;
    if (ui.variant == variant) return;
    ui.variant = variant;
    ui.saved.clear();          // (read from the variant below)
    ui.previewed.clear();
    ui.selected = ui.linkFrom = 0;
    ui.scroll = ImVec2(0.0f, 0.0f);
}

void Editor::renderMaterialGraph() {
    if (!m_graphUi || !m_graphUi->show) return;
    GraphUi& ui = *m_graphUi;
    SceneNode* v = m_scene->findById(ui.variant);
    if (!v || !v->isMaterialVariant()) { ui.show = false; return; }

    // The graph lives in the variant (so it's saved, and Undo works): read it again if
    // it changed there (Undo, someone in Team Create...).
    if (v->variant.graph != ui.saved || ui.graph.nodes.empty()) {
        Graph g;
        if (v->variant.graph.empty() || !MaterialGraph::fromJson(nlohmann::json::parse(v->variant.graph, nullptr, false), g))
            g = MaterialGraph::starter();
        ui.graph = std::move(g);
        ui.saved = v->variant.graph;
        if (!ui.graph.find(ui.selected)) ui.selected = 0;
    }

    ImGui::SetNextWindowSize(ImVec2(980, 600), ImGuiCond_FirstUseEver);
    const std::string title = "Material Graph: " + v->name + "###MaterialGraph";
    if (!ImGui::Begin(title.c_str(), &ui.show)) { ImGui::End(); return; }

    bool changed = false;
    ImGui::BeginDisabled(m_playing);
    // --- Top: apply it to the material ---
    if (ImGui::Button("Apply to material")) applyMaterialGraph(v, ui.graph, ui.size);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Make the Output into the material's pictures (colours, bumps, rough and metal spots).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    const int sizes[] = {256, 512, 1024};
    const char* sizeNames[] = {"256 px", "512 px", "1024 px"};
    int si = ui.size == 256 ? 0 : ui.size == 1024 ? 2 : 1;
    if (ImGui::Combo("##size", &si, sizeNames, 3)) ui.size = sizes[si];
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("How big the pictures are (bigger: sharper, but slower to make)");
    ImGui::SameLine();
    if (ImGui::Button("Start from...")) ImGui::OpenPopup("##examples");
    if (ImGui::BeginPopup("##examples")) {
        ImGui::TextDisabled("Throws away these boxes and starts again from:");
        if (ImGui::MenuItem("A simple one")) { ui.graph = MaterialGraph::starter(); ui.selected = 0; changed = true; }
        for (const std::string& e : MaterialGraph::examples())
            if (ImGui::MenuItem(e.c_str())) { ui.graph = MaterialGraph::example(e); ui.selected = 0; changed = true; }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", "Right-click to add a box. Drag from a box's right dot to another's left dot to wire them. Middle-drag to look around.");
    ImGui::EndDisabled();

    // --- The canvas ---
    ImGui::BeginChild("##graphcanvas", ImVec2(0, 0), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 corner = ImGui::GetCursorScreenPos();
    const ImVec2 area = ImGui::GetContentRegionAvail();
    const ImVec2 origin(corner.x + ui.scroll.x, corner.y + ui.scroll.y);
    const bool canvasHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    // A grid behind.
    const ImU32 gridCol = ImGui::GetColorU32(ImGuiCol_Border, 0.35f);
    for (float x = std::fmod(ui.scroll.x, 32.0f); x < area.x; x += 32.0f)
        dl->AddLine(ImVec2(corner.x + x, corner.y), ImVec2(corner.x + x, corner.y + area.y), gridCol);
    for (float y = std::fmod(ui.scroll.y, 32.0f); y < area.y; y += 32.0f)
        dl->AddLine(ImVec2(corner.x, corner.y + y), ImVec2(corner.x + area.x, corner.y + y), gridCol);
    if (canvasHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        ui.scroll.x += ImGui::GetIO().MouseDelta.x;
        ui.scroll.y += ImGui::GetIO().MouseDelta.y;
    }

    // Previews: every box's picture, made again when the graph changes.
    const std::string json = MaterialGraph::toJson(ui.graph).dump();
    if (json != ui.previewed) {
        for (const Node& n : ui.graph.nodes) {
            const MaterialGraph::Image img = MaterialGraph::evaluate(ui.graph, n.id, 64);
            const std::vector<unsigned char> px = MaterialGraph::rgba(img);
            Textures::put("matgraph:" + std::to_string(n.id), px.data(), img.size, img.size);
        }
        ui.previewed = json;
    }

    // Boxes (drawn first into the top layer so their sizes are known for the wires).
    struct Socket { int node, input; ImVec2 at; };
    std::vector<Socket> inputs;
    std::map<int, ImVec2> outputs;
    int removeNode = 0;
    dl->ChannelsSplit(3);   // 0 wires, 1 box backgrounds, 2 box insides
    ImGui::BeginDisabled(m_playing);
    for (Node& n : ui.graph.nodes) {
        const MaterialGraph::TypeInfo& ti = MaterialGraph::info(n.type);
        ImGui::PushID(n.id);
        dl->ChannelsSetCurrent(2);
        const ImVec2 at(origin.x + n.pos.x, origin.y + n.pos.y);
        ImGui::SetCursorScreenPos(ImVec2(at.x + 8, at.y + 4));
        ImGui::BeginGroup();
        ImGui::PushItemWidth(kNodeWidth - 16 - 70);
        ImGui::TextUnformatted(ti.name);
        if (ImGui::IsItemHovered() && ti.help) ImGui::SetTooltip("%s", ti.help);
        ImGui::Dummy(ImVec2(kNodeWidth - 16, 2));
        if (const unsigned tex = Textures::get("matgraph:" + std::to_string(n.id)))
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(kPreview, kPreview), ImVec2(0, 1), ImVec2(1, 0));
        else ImGui::Dummy(ImVec2(kPreview, kPreview));
        // Inputs, with their dots on the left edge.
        for (int i = 0; i < ti.inputs; ++i) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetTextLineHeight();
            inputs.push_back({n.id, i, ImVec2(at.x, p.y + h * 0.5f)});
            ImGui::TextUnformatted(ti.inputNames[i]);
        }
        // Settings.
        for (int i = 0; i < ti.params; ++i) {
            const MaterialGraph::Param& pr = ti.param[i];
            if (n.type == Type::Mix && n.in[2] >= 0) continue;   // (Amount comes from the wire)
            if (pr.whole) {
                int val = (int)n.p[i];
                if (ImGui::DragInt(pr.name, &val, 0.1f, (int)pr.min, (int)pr.max)) { n.p[i] = (float)std::clamp(val, (int)pr.min, (int)pr.max); changed = true; }
            } else if (ImGui::DragFloat(pr.name, &n.p[i], (pr.max - pr.min) / 300.0f, pr.min, pr.max, pr.max <= 0.2f ? "%.3f" : "%.2f")) {
                n.p[i] = std::clamp(n.p[i], pr.min, pr.max);
                changed = true;
            }
            if (ImGui::IsItemHovered() && pr.tip && *pr.tip) ImGui::SetTooltip("%s", pr.tip);
        }
        for (int i = 0; i < ti.colors; ++i)
            if (ImGui::ColorEdit3(ti.colorNames[i], &n.c[i].x, ImGuiColorEditFlags_NoInputs)) changed = true;
        ImGui::PopItemWidth();
        ImGui::EndGroup();
        const ImVec2 inside = ImGui::GetItemRectMax();
        const ImVec2 lo = at, hi(at.x + kNodeWidth, inside.y + 6);
        if (n.type != Type::Output) outputs[n.id] = ImVec2(hi.x, at.y + 12 + ImGui::GetTextLineHeight());

        // The box behind, and dragging it by its title.
        dl->ChannelsSetCurrent(1);
        const bool sel = ui.selected == n.id;
        dl->AddRectFilled(lo, hi, ImGui::GetColorU32(ImGuiCol_PopupBg), 6.0f);
        dl->AddRectFilled(lo, ImVec2(hi.x, lo.y + ImGui::GetTextLineHeight() + 8), ImGui::GetColorU32(n.type == Type::Output ? ImGuiCol_ButtonActive : ImGuiCol_TitleBgActive), 6.0f, ImDrawFlags_RoundCornersTop);
        dl->AddRect(lo, hi, ImGui::GetColorU32(sel ? ImGuiCol_NavHighlight : ImGuiCol_Border), 6.0f, 0, sel ? 2.0f : 1.0f);
        ImGui::SetCursorScreenPos(lo);
        ImGui::InvisibleButton("##title", ImVec2(kNodeWidth, ImGui::GetTextLineHeight() + 8));
        if (ImGui::IsItemActivated()) ui.selected = n.id;
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            n.pos.x += ImGui::GetIO().MouseDelta.x;
            n.pos.y += ImGui::GetIO().MouseDelta.y;
        }
        if (ImGui::IsItemDeactivated() && ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).x != 0.0f) changed = true;
        if (ImGui::BeginPopupContextItem("##nodemenu")) {
            if (n.type == Type::Output) ImGui::TextDisabled("Every graph has one Output.");
            else if (ImGui::MenuItem("Delete")) removeNode = n.id;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }

    // Dots, and wiring.
    dl->ChannelsSetCurrent(2);
    const float dotR = 5.0f;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto near = [&](ImVec2 a, ImVec2 b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) < 100.0f; };
    for (const Socket& s : inputs) {
        const Node* n = ui.graph.find(s.node);
        const bool wired = n && n->in[s.input] >= 0;
        dl->AddCircleFilled(s.at, dotR, ImGui::GetColorU32(wired ? ImGuiCol_CheckMark : ImGuiCol_FrameBgHovered));
        // Pull a wire off an input: it comes away in your hand.
        if (!m_playing && wired && ui.linkFrom == 0 && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && near(mouse, s.at)) {
            ui.linkFrom = n->in[s.input];
            ui.graph.find(s.node)->in[s.input] = -1;
            changed = true;
        }
    }
    for (auto& [id, at] : outputs) {
        dl->AddCircleFilled(at, dotR, ImGui::GetColorU32(ImGuiCol_CheckMark));
        if (!m_playing && ui.linkFrom == 0 && canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && near(mouse, at))
            ui.linkFrom = id;
    }
    // The wires.
    dl->ChannelsSetCurrent(0);
    auto wire = [&](ImVec2 a, ImVec2 b, ImU32 col) {
        const float dx = std::max(40.0f, std::abs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, col, 2.5f);
    };
    for (const Socket& s : inputs) {
        const Node* n = ui.graph.find(s.node);
        if (!n || n->in[s.input] < 0) continue;
        auto from = outputs.find(n->in[s.input]);
        if (from != outputs.end()) wire(from->second, s.at, ImGui::GetColorU32(ImGuiCol_PlotLines));
    }
    if (ui.linkFrom) {
        if (auto from = outputs.find(ui.linkFrom); from != outputs.end()) wire(from->second, mouse, ImGui::GetColorU32(ImGuiCol_PlotLinesHovered));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            for (const Socket& s : inputs)
                if (near(mouse, s.at)) { if (ui.graph.connect(ui.linkFrom, s.node, s.input)) changed = true; break; }
            ui.linkFrom = 0;
        }
    }
    dl->ChannelsMerge();
    ImGui::EndDisabled();

    // Adding boxes (right-click on the empty canvas), and deleting the picked one.
    if (!m_playing && canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsAnyItemHovered() &&
        ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0.0f) {
        ui.addAt = ImVec2(mouse.x - origin.x, mouse.y - origin.y);
        ImGui::OpenPopup("##addnode");
    }
    if (ImGui::BeginPopup("##addnode")) {
        ImGui::TextDisabled("Add a box");
        for (int t = 1; t < (int)Type::Count; ++t) {
            const MaterialGraph::TypeInfo& ti = MaterialGraph::info((Type)t);
            if (ImGui::MenuItem(ti.name)) {
                ui.selected = ui.graph.add((Type)t, glm::vec2(ui.addAt.x, ui.addAt.y)).id;
                changed = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ti.help);
        }
        ImGui::EndPopup();
    }
    if (!m_playing && canvasHovered && ui.selected && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Delete))
        removeNode = ui.selected;
    if (removeNode) {
        ui.graph.remove(removeNode);
        Textures::forget("matgraph:" + std::to_string(removeNode));
        if (ui.selected == removeNode) ui.selected = 0;
        changed = true;
    }
    ImGui::EndChild();
    ImGui::End();

    // Keep the variant's copy up to date (once a drag is finished: one undo step each).
    if (!ImGui::IsAnyItemActive() && !ui.linkFrom) {
        const std::string now = MaterialGraph::toJson(ui.graph).dump();
        if (now != ui.saved && (changed || ui.saved != now)) {
            v->variant.graph = now;
            ui.saved = now;
        }
    }
}
