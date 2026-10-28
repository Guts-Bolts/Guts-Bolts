// UV mapping in Studio: the UV Editor window (where each face of a MeshPart
// sits on its picture, like Blender's UV Editor) and the U menu for laying the
// mesh out flat. The maths is in scene/MeshUV.cpp.
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include "../scene/EditMesh.h"
#include "../scene/PlayerModel.h"
#include "../renderer/Textures.h"
#include "../core/FileDialog.h"
#include "../core/Paths.h"
#include "../core/Log.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <stb_image_write.h>   // (its code is in renderer/Textures.cpp)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {
const ImU32 kUVPicked = IM_COL32(255, 150, 40, 255);
const ImU32 kUVWire   = IM_COL32(235, 235, 240, 230);
const ImU32 kUVFill   = IM_COL32(120, 160, 255, 40);

bool inStep(const EditMesh& m) {
    if (m.uvs.size() != m.faces.size()) return false;
    for (size_t i = 0; i < m.faces.size(); ++i) if (m.uvs[i].size() != m.faces[i].size()) return false;
    return true;
}

bool inside(glm::vec2 p, const std::vector<glm::vec2>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
            p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}
} // namespace

SceneNode* Editor::uvNode() {
    SceneNode* n = m_state.mode == StudioMode::Modeling ? m_scene->findById(m_state.modeling.node) : m_scene->selected();
    return n && n->isPart() && n->editMesh && !m_scene->isCharacterPart(n) && canEdit(n) ? n : nullptr;
}

void Editor::unwrapMesh(int how) {
    SceneNode* n = uvNode();
    if (!n) { Log::warn("UV: pick a MeshPart first (or go into Modeling mode with Tab)."); return; }
    MeshEdit::Selection sel;
    if (m_state.mode == StudioMode::Modeling) sel = m_state.modeling.sel;
    sel.resize(n->editMesh->verts.size(), 0);
    // Project From View: from the mesh's own points to the screen, as the 3D view shows them now.
    glm::mat4 view = m_viewport->cameraViewProj() * n->worldMatrix();
    EditMesh work = *n->editMesh;
    if (!MeshEdit::unwrap(work, sel, (MeshEdit::Unwrap)how, n->transform.scale, view)) return;
    MeshEdit::own(*n) = std::move(work);
    MeshEdit::refresh(*n);
    int faces = (int)MeshEdit::selectedFaces(*n->editMesh, sel).size();
    if (faces == 0) faces = (int)n->editMesh->faces.size();
    Log::info(std::string(MeshEdit::kUnwrapNames[how]) + ": laid out " + std::to_string(faces) + " faces. See them in the UV Editor.");
    m_uv.show = true;
}

void Editor::markSeams(bool on) {
    SceneNode* n = m_scene->findById(m_state.modeling.node);
    if (m_state.mode != StudioMode::Modeling || !n || !n->editMesh) return;
    MeshEdit::Selection sel = m_state.modeling.sel;
    sel.resize(n->editMesh->verts.size(), 0);
    EditMesh work = *n->editMesh;
    if (!MeshEdit::markSeams(work, sel, on)) {
        if (on) Log::warn("Mark Seam: pick some edges first (2 picks edges). Unwrap cuts the mesh open along seams.");
        return;
    }
    MeshEdit::own(*n) = std::move(work);
}

// Clothing Template: lay the mesh out like a body part of the default character, so a
// shirt or pants picture fits it (for body parts made for bundles).
void Editor::clothingUnwrap(const std::string& rigPart) {
    SceneNode* n = uvNode();
    if (!n) { Log::warn("UV: pick a MeshPart first (or go into Modeling mode with Tab)."); return; }
    EditMesh work = *n->editMesh;
    if (!PlayerModel::clothingUVs(work, rigPart)) {
        Log::warn("Clothing Template: shirts and pants cover the Torso, arms and legs (not the Head).");
        return;
    }
    MeshEdit::own(*n) = std::move(work);
    MeshEdit::refresh(*n);
    m_scene->markDirty();
    // Show the template behind it: legs wear pants, the rest a shirt.
    m_uv.clothing = rigPart.find("Leg") != std::string::npos ? 2 : 1;
    m_uv.show = true;
    Log::info("Clothing Template: laid " + n->name + " out as the " + rigPart + " of the shirt / pants template.");
}

static const char* const kClothedParts[] = {"Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg"};

// The U menu (U in Modeling mode, or the ribbon's Unwrap button).
static void unwrapMenuItems(Editor& ed, bool modeling, void (Editor::*unwrap)(int), void (Editor::*seams)(bool),
                            void (Editor::*clothing)(const std::string&), const std::string& partName) {
    for (int i = 0; i < MeshEdit::kUnwrapCount; ++i) {
        if (i == (int)MeshEdit::Unwrap::Reset) ImGui::Separator();
        if (ImGui::MenuItem(MeshEdit::kUnwrapNames[i])) (ed.*unwrap)(i);
        if (ImGui::IsItemHovered()) {
            static const char* tips[MeshEdit::kUnwrapCount] = {
                "Lay the faces out flat, cut open along the seams (red edges), keeping their shapes",
                "Cut the mesh into pieces wherever it bends a lot and lay each one flat",
                "Like a box: each face goes on the side it faces most",
                "Wrap the picture around it like a label on a can",
                "Wrap the picture around it like a map on a globe",
                "Just as the faces look from the camera right now",
                "Every face fills the whole picture"};
            ImGui::SetTooltip("%s", tips[i]);
        }
    }
    ImGui::Separator();
    // A part named like a body part ("Left Arm") gets its own spot straight away.
    const char* const* named = std::find(std::begin(kClothedParts), std::end(kClothedParts), partName);
    if (named != std::end(kClothedParts)) {
        if (ImGui::MenuItem("Clothing Template")) (ed.*clothing)(partName);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Lay it out like the %s of the shirt / pants template, so clothing fits it", partName.c_str());
    } else if (ImGui::BeginMenu("Clothing Template")) {
        for (const char* part : kClothedParts)
            if (ImGui::MenuItem(part)) (ed.*clothing)(part);
        ImGui::EndMenu();
    }
    if (modeling) {
        ImGui::Separator();
        if (ImGui::MenuItem("Mark Seam")) (ed.*seams)(true);
        if (ImGui::MenuItem("Clear Seam")) (ed.*seams)(false);
    }
}

void Editor::renderUVEditor() {
    if (m_uv.openUnwrap) { ImGui::OpenPopup("##uvunwrap"); m_uv.openUnwrap = false; }
    if (ImGui::BeginPopup("##uvunwrap")) {
        ImGui::TextDisabled("UV Unwrap");
        ImGui::Separator();
        SceneNode* un = uvNode();
        unwrapMenuItems(*this, m_state.mode == StudioMode::Modeling, &Editor::unwrapMesh, &Editor::markSeams,
                        &Editor::clothingUnwrap, un ? un->name : std::string());
        ImGui::EndPopup();
    }
    m_uv.hovered = false;
    if (!m_uv.show || m_playing) { m_uv.modal = 0; return; }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    // First time: over the Toolbox, leaving the 3D view clear.
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 8, vp->WorkPos.y + 150), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UV Editor", &m_uv.show, ImGuiWindowFlags_MenuBar)) { ImGui::End(); return; }

    SceneNode* n = uvNode();
    if (!n) {
        m_uv.modal = 0;
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Pick a MeshPart (or a part, then Tab for Modeling mode) to see how its picture wraps onto it.");
        ImGui::Spacing();
        ImGui::TextDisabled("In Modeling mode: mark seams on edges, press U and pick Unwrap to lay the mesh out flat, "
                            "then give the part a Texture in Properties.");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    // The layout to show: the mesh's own, or the box mapping it's drawn with until it gets one.
    static EditMesh boxed;
    const EditMesh* shown = n->editMesh.get();
    if (!inStep(*shown)) { boxed = *shown; MeshEdit::ensureUVs(boxed); shown = &boxed; }
    auto editable = [&]() -> EditMesh& {
        EditMesh& m = MeshEdit::own(*n);
        MeshEdit::ensureUVs(m);
        return m;
    };
    // Which faces show: the ones picked in Modeling mode, or all.
    std::vector<int> faces;
    if (m_state.mode == StudioMode::Modeling) {
        MeshEdit::Selection s = m_state.modeling.sel;
        s.resize(shown->verts.size(), 0);
        faces = MeshEdit::selectedFaces(*shown, s);
    }
    if (faces.empty()) for (size_t f = 0; f < shown->faces.size(); ++f) faces.push_back((int)f);
    // Keep the UV picks in step with the mesh.
    bool fresh = m_uv.node != n->id || m_uv.sel.size() != shown->faces.size();
    for (size_t f = 0; !fresh && f < shown->faces.size(); ++f) fresh = m_uv.sel[f].size() != shown->faces[f].size();
    if (fresh) {
        m_uv.node = n->id;
        m_uv.modal = 0;
        m_uv.sel.assign(shown->faces.size(), {});
        for (size_t f = 0; f < shown->faces.size(); ++f) m_uv.sel[f].assign(shown->faces[f].size(), 0);
    }
    auto pickedCount = [&] {
        int c = 0;
        for (int f : faces) for (char s : m_uv.sel[f]) c += s;
        return c;
    };
    auto selectAll = [&](bool on) { for (int f : faces) std::fill(m_uv.sel[f].begin(), m_uv.sel[f].end(), on ? 1 : 0); };
    // Pick a corner and every corner stuck to it (same point of the mesh, same spot on the picture).
    auto pickSticky = [&](int f, int c, char on) {
        uint32_t v = shown->faces[f][c];
        glm::vec2 at = shown->uvs[f][c];
        for (int g : faces)
            for (size_t k = 0; k < shown->faces[g].size(); ++k)
                if (shown->faces[g][k] == v && glm::length(shown->uvs[g][k] - at) < 1e-5f) m_uv.sel[g][k] = on;
    };
    auto pickIsland = [&](int f) {
        for (auto& island : MeshEdit::uvIslands(*shown, faces))
            if (std::find(island.begin(), island.end(), f) != island.end())
                for (int g : island) std::fill(m_uv.sel[g].begin(), m_uv.sel[g].end(), 1);
    };
    auto commit = [&](EditMesh& m) { (void)m; MeshEdit::refresh(*n); };

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Checker Picture", nullptr, &m_uv.checker);
            if (ImGui::MenuItem("Shirt Template", nullptr, m_uv.clothing == 1)) m_uv.clothing = m_uv.clothing == 1 ? 0 : 1;
            if (ImGui::MenuItem("Pants Template", nullptr, m_uv.clothing == 2)) m_uv.clothing = m_uv.clothing == 2 ? 0 : 2;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show a clothing template behind the layout when the part has no Texture");
            if (ImGui::MenuItem("Frame All", "Home")) { m_uv.zoom = 0.9f; m_uv.pan = {0.5f, 0.5f}; }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Select")) {
            if (ImGui::MenuItem("All", "A")) selectAll(true);
            if (ImGui::MenuItem("None", "Alt+A")) selectAll(false);
            if (ImGui::MenuItem("Linked", "Ctrl+L")) {
                std::vector<int> picked;
                for (int f : faces) if (std::count(m_uv.sel[f].begin(), m_uv.sel[f].end(), 1)) picked.push_back(f);
                for (int f : picked) pickIsland(f);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UV")) {
            unwrapMenuItems(*this, m_state.mode == StudioMode::Modeling, &Editor::unwrapMesh, &Editor::markSeams,
                            &Editor::clothingUnwrap, n->name);
            ImGui::Separator();
            if (ImGui::MenuItem("Pack Islands")) {
                EditMesh& m = editable();
                MeshEdit::packIslands(m, faces, n->transform.scale);
                commit(m);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Spread the pieces out so none overlap, filling the picture");
            bool any = pickedCount() > 0;
            auto eachPicked = [&](auto fn) {
                EditMesh& m = editable();
                glm::vec2 lo(1e30f), hi(-1e30f);
                for (int f : faces) for (size_t c = 0; c < m.uvs[f].size(); ++c)
                    if (m_uv.sel[f][c]) { lo = glm::min(lo, m.uvs[f][c]); hi = glm::max(hi, m.uvs[f][c]); }
                glm::vec2 mid = (lo + hi) * 0.5f;
                for (int f : faces) for (size_t c = 0; c < m.uvs[f].size(); ++c)
                    if (m_uv.sel[f][c]) m.uvs[f][c] = mid + fn(m.uvs[f][c] - mid);
                commit(m);
            };
            if (ImGui::MenuItem("Flip Horizontally", nullptr, false, any)) eachPicked([](glm::vec2 d) { return glm::vec2(-d.x, d.y); });
            if (ImGui::MenuItem("Flip Vertically", nullptr, false, any)) eachPicked([](glm::vec2 d) { return glm::vec2(d.x, -d.y); });
            if (ImGui::MenuItem("Rotate 90", nullptr, false, any)) eachPicked([](glm::vec2 d) { return glm::vec2(-d.y, d.x); });
            ImGui::Separator();
            if (ImGui::MenuItem("Export UV Layout...")) {
                // A picture of the layout to paint over in a drawing program, saved in the games folder.
                std::filesystem::path dir = Paths::gamesFolder() / "uv layouts";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::string name = n->name;
                for (char& ch : name) if (!std::isalnum((unsigned char)ch) && ch != '-' && ch != '_') ch = '_';
                std::filesystem::path file = dir / (name + " UV.png");
                const int size = 1024;
                std::vector<unsigned char> px = MeshEdit::layoutPicture(*shown, size);
                if (stbi_write_png(file.string().c_str(), size, size, 4, px.data(), size * 4))
                    Log::info("Saved the UV layout to " + file.string() + " - paint over it, then use it as the part's Texture.");
                else
                    Log::warn("Couldn't save the UV layout to " + file.string());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    // The part's picture (also in Properties).
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##uvtex", "Texture: a picture to wrap onto it", &n->texture);
    if (ImGui::IsItemDeactivatedAfterEdit()) m_scene->markDirty();
    if (FileDialog::available()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
            std::string path = FileDialog::openImage("Pick a picture to wrap onto the part");
            if (!path.empty()) { n->texture = Paths::relativeToGames(path); m_scene->markDirty(); }
        }
    }

    // --- The canvas ---
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= ImGui::GetTextLineHeightWithSpacing();
    if (avail.x < 50 || avail.y < 50) { ImGui::End(); return; }
    ImVec2 lo = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##uvcanvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    m_uv.hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) || m_uv.modal != 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 hi(lo.x + avail.x, lo.y + avail.y);
    dl->PushClipRect(lo, hi, true);
    dl->AddRectFilled(lo, hi, IM_COL32(40, 42, 48, 255));
    const glm::vec2 center(lo.x + avail.x * 0.5f, lo.y + avail.y * 0.5f);
    const float scale = std::min(avail.x, avail.y) * m_uv.zoom;
    auto toScreen = [&](glm::vec2 t) { return ImVec2(center.x + (t.x - m_uv.pan.x) * scale, center.y - (t.y - m_uv.pan.y) * scale); };
    auto toUV = [&](ImVec2 p) { return glm::vec2((p.x - center.x) / scale + m_uv.pan.x, -(p.y - center.y) / scale + m_uv.pan.y); };
    ImGuiIO& io = ImGui::GetIO();

    // Behind: the picture (pictures load bottom row first, so v = 1 is its top), or a checker.
    ImVec2 p0 = toScreen({0, 1}), p1 = toScreen({1, 0});
    unsigned tex = !n->texture.empty() ? Textures::get(n->texture)
                 : m_uv.clothing ? Textures::get(m_uv.clothing == 2 ? Textures::kPantsTemplatePicture : Textures::kShirtTemplatePicture)
                 : 0;
    if (tex) {
        dl->AddImage((ImTextureID)(intptr_t)tex, p0, p1, ImVec2(0, 1), ImVec2(1, 0));
    } else if (m_uv.checker) {
        const int cells = 8;
        for (int y = 0; y < cells; ++y)
            for (int x = 0; x < cells; ++x) {
                ImVec2 a = toScreen({(float)x / cells, (float)(y + 1) / cells}), b = toScreen({(float)(x + 1) / cells, (float)y / cells});
                dl->AddRectFilled(a, b, (x + y) % 2 ? IM_COL32(90, 92, 100, 255) : IM_COL32(130, 132, 140, 255));
            }
    } else {
        dl->AddRectFilled(p0, p1, IM_COL32(70, 72, 80, 255));
    }
    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 90));

    // The faces.
    for (int f : faces) {
        const auto& uv = shown->uvs[f];
        std::vector<ImVec2> pts;
        for (auto& t : uv) pts.push_back(toScreen(t));
        for (size_t k = 1; k + 1 < pts.size(); ++k) dl->AddTriangleFilled(pts[0], pts[k], pts[k + 1], kUVFill);
    }
    for (int f : faces) {
        const auto& uv = shown->uvs[f];
        for (size_t c = 0; c < uv.size(); ++c) {
            size_t d = (c + 1) % uv.size();
            bool picked = m_uv.sel[f][c] && m_uv.sel[f][d];
            bool seam = !shown->seams.empty() && MeshEdit::isSeam(*shown, shown->faces[f][c], shown->faces[f][d]);
            dl->AddLine(toScreen(uv[c]), toScreen(uv[d]), picked ? kUVPicked : seam ? IM_COL32(235, 40, 40, 255) : kUVWire,
                        picked ? 2.0f : 1.2f);
        }
    }
    for (int f : faces)
        for (size_t c = 0; c < shown->uvs[f].size(); ++c) {
            ImVec2 p = toScreen(shown->uvs[f][c]);
            bool picked = m_uv.sel[f][c];
            dl->AddRectFilled(ImVec2(p.x - 2.5f, p.y - 2.5f), ImVec2(p.x + 2.5f, p.y + 2.5f), picked ? kUVPicked : IM_COL32(20, 20, 24, 255));
        }

    // Pan (middle drag) and zoom (wheel, toward the mouse).
    if (hovered && m_uv.modal == 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            m_uv.pan -= glm::vec2(io.MouseDelta.x, -io.MouseDelta.y) / scale;
        }
        if (io.MouseWheel != 0.0f) {
            glm::vec2 before = toUV(io.MousePos);
            m_uv.zoom = std::clamp(m_uv.zoom * std::pow(1.15f, io.MouseWheel), 0.1f, 40.0f);
            float s2 = std::min(avail.x, avail.y) * m_uv.zoom;
            glm::vec2 after((io.MousePos.x - center.x) / s2 + m_uv.pan.x, -(io.MousePos.y - center.y) / s2 + m_uv.pan.y);
            m_uv.pan += before - after;
        }
    }

    // G / R / S: move, turn or size the picked UVs with the mouse (click or Enter to keep, Esc or right-click to cancel).
    auto startModal = [&](int what) {
        if (pickedCount() == 0) return;
        EditMesh& m = editable();
        m_uv.before = m.uvs;
        m_uv.modal = what;
        m_uv.axis = -1;
        m_uv.startMouse = {io.MousePos.x, io.MousePos.y};
        glm::vec2 sum(0.0f);
        int count = 0;
        for (int f : faces) for (size_t c = 0; c < m.uvs[f].size(); ++c) if (m_uv.sel[f][c]) { sum += m.uvs[f][c]; ++count; }
        m_uv.pivot = sum / (float)count;
    };
    auto keyed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    if (m_uv.modal) {
        EditMesh& m = MeshEdit::own(*n);
        if (m.uvs.size() != m_uv.before.size()) { m_uv.modal = 0; }
        else {
            if (keyed(ImGuiKey_X)) m_uv.axis = m_uv.axis == 0 ? -1 : 0;
            if (keyed(ImGuiKey_Y)) m_uv.axis = m_uv.axis == 1 ? -1 : 1;
            glm::vec2 a = toUV(ImVec2(m_uv.startMouse.x, m_uv.startMouse.y)), b = toUV(io.MousePos);
            glm::vec2 move = b - a;
            float turn = std::atan2(b.y - m_uv.pivot.y, b.x - m_uv.pivot.x) - std::atan2(a.y - m_uv.pivot.y, a.x - m_uv.pivot.x);
            float da = glm::length(a - m_uv.pivot), size = da > 1e-6f ? glm::length(b - m_uv.pivot) / da : 1.0f;
            if (io.KeyCtrl) {   // snap
                move = glm::round(move * 32.0f) / 32.0f;
                turn = std::round(turn / glm::radians(15.0f)) * glm::radians(15.0f);
                size = std::round(size * 10.0f) / 10.0f;
            }
            if (m_uv.axis == 0) move.y = 0.0f;
            if (m_uv.axis == 1) move.x = 0.0f;
            glm::vec2 sz(size);
            if (m_uv.axis == 0) sz.y = 1.0f;
            if (m_uv.axis == 1) sz.x = 1.0f;
            float cs = std::cos(turn), sn = std::sin(turn);
            for (int f : faces)
                for (size_t c = 0; c < m.uvs[f].size() && c < m_uv.before[f].size(); ++c) {
                    if (!m_uv.sel[f][c]) continue;
                    glm::vec2 d = m_uv.before[f][c] - m_uv.pivot;
                    if (m_uv.modal == 1) m.uvs[f][c] = m_uv.before[f][c] + move;
                    else if (m_uv.modal == 2) m.uvs[f][c] = m_uv.pivot + glm::vec2(d.x * cs - d.y * sn, d.x * sn + d.y * cs);
                    else m.uvs[f][c] = m_uv.pivot + d * sz;
                }
            MeshEdit::refresh(*n);
            const char* what = m_uv.modal == 1 ? "Move" : m_uv.modal == 2 ? "Rotate" : "Scale";
            char head[160];
            if (m_uv.modal == 1) std::snprintf(head, sizeof head, "%s  U %.3f  V %.3f", what, move.x, move.y);
            else if (m_uv.modal == 2) std::snprintf(head, sizeof head, "%s  %.1f deg", what, glm::degrees(turn));
            else std::snprintf(head, sizeof head, "%s  %.2f", what, size);
            std::string text = std::string(head) + (m_uv.axis == 0 ? "  (only U)" : m_uv.axis == 1 ? "  (only V)" : "") +
                               "   X / Y lock, Ctrl snaps, click to keep, Esc cancels";
            dl->AddText(ImVec2(lo.x + 8, lo.y + 6), IM_COL32(255, 255, 255, 255), text.c_str());
            bool keep = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || keyed(ImGuiKey_Enter) || keyed(ImGuiKey_KeypadEnter);
            bool cancel = ImGui::IsMouseClicked(ImGuiMouseButton_Right) || keyed(ImGuiKey_Escape);
            if (cancel) { m.uvs = m_uv.before; MeshEdit::refresh(*n); }
            if (keep || cancel) { m_uv.modal = 0; m_uv.before.clear(); }
        }
    } else if (hovered || ImGui::IsWindowFocused()) {
        // Picking: click a corner (Shift adds), click inside a face for the whole face, drag for a box.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_uv.boxing = true;
            m_uv.boxStart = {io.MousePos.x, io.MousePos.y};
        }
        if (m_uv.boxing && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 a(m_uv.boxStart.x, m_uv.boxStart.y), b = io.MousePos;
            if (std::abs(a.x - b.x) + std::abs(a.y - b.y) > 4.0f) {
                ImVec2 bl(std::min(a.x, b.x), std::min(a.y, b.y)), bh(std::max(a.x, b.x), std::max(a.y, b.y));
                dl->AddRectFilled(bl, bh, IM_COL32(255, 255, 255, 25));
                dl->AddRect(bl, bh, IM_COL32(255, 255, 255, 170));
            }
        }
        if (m_uv.boxing && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_uv.boxing = false;
            ImVec2 a(m_uv.boxStart.x, m_uv.boxStart.y), b = io.MousePos;
            bool add = io.KeyShift, sub = io.KeyCtrl;
            if (std::abs(a.x - b.x) + std::abs(a.y - b.y) > 4.0f) {
                if (!add && !sub) selectAll(false);
                ImVec2 bl(std::min(a.x, b.x), std::min(a.y, b.y)), bh(std::max(a.x, b.x), std::max(a.y, b.y));
                for (int f : faces)
                    for (size_t c = 0; c < shown->uvs[f].size(); ++c) {
                        ImVec2 p = toScreen(shown->uvs[f][c]);
                        if (p.x >= bl.x && p.x <= bh.x && p.y >= bl.y && p.y <= bh.y) m_uv.sel[f][c] = sub ? 0 : 1;
                    }
            } else {
                int bestF = -1, bestC = -1;
                float best = 9.0f * 9.0f;
                for (int f : faces)
                    for (size_t c = 0; c < shown->uvs[f].size(); ++c) {
                        ImVec2 p = toScreen(shown->uvs[f][c]);
                        float d = (p.x - b.x) * (p.x - b.x) + (p.y - b.y) * (p.y - b.y);
                        if (d < best) { best = d; bestF = f; bestC = (int)c; }
                    }
                if (bestF >= 0) {
                    char was = m_uv.sel[bestF][bestC];
                    if (!add) selectAll(false);
                    pickSticky(bestF, bestC, add ? !was : 1);
                } else {
                    glm::vec2 at = toUV(b);
                    int hit = -1;
                    for (int f : faces) if (inside(at, shown->uvs[f])) hit = f;
                    if (!add) selectAll(false);
                    if (hit >= 0) for (size_t c = 0; c < shown->uvs[hit].size(); ++c) pickSticky(hit, (int)c, 1);
                }
            }
        }
        if (hovered && !io.WantTextInput) {
            if (!io.KeyCtrl && !io.KeyAlt && keyed(ImGuiKey_A)) selectAll(true);
            if (io.KeyAlt && keyed(ImGuiKey_A)) selectAll(false);
            if (!io.KeyCtrl && !io.KeyAlt && keyed(ImGuiKey_L)) {
                // The piece under the mouse.
                glm::vec2 at = toUV(io.MousePos);
                int hit = -1;
                for (int f : faces) if (inside(at, shown->uvs[f])) hit = f;
                if (hit < 0) {
                    float best = 1e30f;
                    for (int f : faces)
                        for (auto& t : shown->uvs[f]) if (glm::length(t - at) < best) { best = glm::length(t - at); hit = f; }
                }
                if (hit >= 0) pickIsland(hit);
            }
            if (io.KeyCtrl && keyed(ImGuiKey_L)) {
                std::vector<int> picked;
                for (int f : faces) if (std::count(m_uv.sel[f].begin(), m_uv.sel[f].end(), 1)) picked.push_back(f);
                for (int f : picked) pickIsland(f);
            }
            if (!io.KeyCtrl && !io.KeyAlt) {
                if (keyed(ImGuiKey_G)) startModal(1);
                if (keyed(ImGuiKey_R)) startModal(2);
                if (keyed(ImGuiKey_S)) startModal(3);
                if (keyed(ImGuiKey_U)) m_uv.openUnwrap = true;
                if (keyed(ImGuiKey_Home)) { m_uv.zoom = 0.9f; m_uv.pan = {0.5f, 0.5f}; }
            }
        }
    }
    dl->PopClipRect();
    ImGui::TextDisabled("Click / drag to pick, G move, R turn, S size, L piece, U unwrap, wheel zoom, middle-drag pan");
    ImGui::End();
}
