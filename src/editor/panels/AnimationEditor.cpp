#include "AnimationEditor.h"
#include "../../core/FileDialog.h"
#include "../../core/Paths.h"
#include "../../online/OnlineClient.h"
#include "../../online/Protocol.h"
#include "../../scene/Scene.h"
#include "../../scene/SceneNode.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {

// The rig's parts, and the bones of armatures inside them (Skin.h).
void allParts(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if ((c->kind == NodeKind::Part && c->mesh) || c->isBone()) out.push_back(c.get());
        if (c->kind == NodeKind::Part || c->kind == NodeKind::Model || c->isBone()) allParts(c.get(), out);
    }
}

void findAnimations(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children) {
        if (c->isAnimation()) out.push_back(c.get());
        findAnimations(c.get(), out);
    }
}

bool moved(const Transform& a, const Transform& b) {
    return glm::length(a.position - b.position) > 1e-4f || glm::length(a.rotation - b.rotation) > 1e-3f;
}

const ImU32 kKeyCol    = IM_COL32(240, 190, 60, 255);
const ImU32 kKeySelCol = IM_COL32(90, 180, 255, 255);
const ImU32 kHeadCol   = IM_COL32(230, 70, 70, 255);

void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddQuadFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), col);
    dl->AddQuad(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y), IM_COL32(30, 30, 30, 255));
}

} // namespace

AnimationEditor::AnimationEditor(Scene* scene, EditorState* state) : m_scene(scene), m_state(state) {}

SceneNode* AnimationEditor::rig() {
    SceneNode* r = m_rig ? m_scene->findById(m_rig) : nullptr;
    if (m_rig && !r) {   // deleted (or undone away)
        m_rig = 0;
        m_state->animRig = 0;
        m_rest.clear();
        m_applied.clear();
        m_playing = false;
    }
    return r;
}

SceneNode* AnimationEditor::animation() {
    SceneNode* r = rig();
    if (!r) return nullptr;
    SceneNode* a = m_anim ? m_scene->findById(m_anim) : nullptr;
    if (a && a->isAnimation() && r->isAncestorOf(a)) return a;
    std::vector<SceneNode*> all;
    findAnimations(r, all);
    m_anim = all.empty() ? 0 : all.front()->id;
    return all.empty() ? nullptr : all.front();
}

Anim::Clip AnimationEditor::clip() {
    SceneNode* a = animation();
    return a ? Anim::parse(a->source) : Anim::Clip();
}

void AnimationEditor::save(const Anim::Clip& c) {
    if (SceneNode* a = animation()) a->source = Anim::dump(c);
}

void AnimationEditor::ensureRest() {
    SceneNode* r = rig();
    if (!r) return;
    std::vector<SceneNode*> parts;
    allParts(r, parts);
    for (SceneNode* n : parts) {
        if (!n->restPose) {
            // New (or freshly loaded by undo): where it is now is where it really is.
            n->restPose = std::make_shared<Transform>(n->transform);
            m_applied.erase(n->id);
        }
        Transform t = *n->restPose;
        t.scale = n->transform.scale;   // size isn't animated: keep the real one
        m_rest[n->id] = t;
    }
}

void AnimationEditor::editRig(SceneNode* node) {
    close();
    SceneNode* r = node && node->kind == NodeKind::Model && node->parent ? node : Anim::rigOf(node);
    if (!r) return;
    m_rig = r->id;
    m_state->animRig = m_rig;
    m_rest.clear();
    m_applied.clear();
    ensureRest();
    m_anim = 0;
    animation();
    m_time = 0.0f;
    m_hasSel = false;
    m_state->tool = GizmoTool::Rotate;   // posing is mostly turning
    m_state->gizmoLocal = true;
}

void AnimationEditor::close() {
    if (SceneNode* r = m_rig ? m_scene->findById(m_rig) : nullptr) {
        std::vector<SceneNode*> parts;
        allParts(r, parts);
        for (SceneNode* n : parts) {
            if (n->restPose) {
                n->transform.position = n->restPose->position;
                n->transform.rotation = n->restPose->rotation;
                n->restPose.reset();
            }
        }
    }
    m_rig = 0;
    m_state->animRig = 0;
    m_rest.clear();
    m_applied.clear();
    m_playing = false;
    m_dragging = false;
}

float AnimationEditor::snap(float t) const {
    t = std::max(0.0f, t);
    return m_fps > 0 ? std::round(t * (float)m_fps) / (float)m_fps : t;
}

void AnimationEditor::setTime(float t) { m_time = std::max(0.0f, t); }

void AnimationEditor::keyPart(Anim::Clip& c, SceneNode* part) {
    SceneNode* r = rig();
    if (!r || !part) return;
    std::map<std::string, Anim::Sample> others;
    Anim::sample(c, m_time, others);
    others.erase(part->name);
    Anim::Pose p;
    if (!Anim::poseFromTransform(r, m_rest, others, part, part->transform, p)) return;
    Anim::Keyframe& k = c.addKey(snap(m_time));
    auto old = k.poses.find(part->name);
    if (old != k.poses.end()) { p.easing = old->second.easing; p.dir = old->second.dir; }
    k.poses[part->name] = p;
}

void AnimationEditor::update(float dt, bool gizmoInUse) {
    SceneNode* r = rig();
    if (!r) return;
    m_state->animRig = m_rig;
    ensureRest();
    SceneNode* a = animation();
    if (!a) {
        Anim::restore(r, m_rest);
        m_applied.clear();
        return;
    }
    Anim::Clip c = clip();
    if (gizmoInUse) m_playing = false;
    if (m_playing) {
        float len = c.length();
        m_time += dt;
        if (len <= 0.0f) m_time = 0.0f;
        else if (m_time > len) {
            if (c.loop) m_time = std::fmod(m_time, len);
            else { m_time = len; m_playing = false; }
        }
    }

    // Someone turned or moved one of the rig's parts (Rotate / Move tools,
    // Properties): that's a pose at the playhead.
    SceneNode* sel = m_scene->selected();
    bool selInRig = sel && (sel->isPart() || sel->isBone()) && r->isAncestorOf(sel);
    if (selInRig && !m_playing) {
        auto it = m_applied.find(sel->id);
        if (it != m_applied.end() && moved(it->second, sel->transform)) {
            keyPart(c, sel);
            save(c);
            m_hasSel = true;
            m_selTime = snap(m_time);
            m_selPart = sel->name;
        }
    }

    // Show the pose at the playhead.
    Transform keep = selInRig ? sel->transform : Transform();
    std::map<std::string, Anim::Sample> s;
    Anim::sample(c, m_time, s);
    Anim::restore(r, m_rest);
    Anim::applyPoses(r, m_rest, s);
    // (While dragging, the part stays exactly where the tool puts it.)
    if (gizmoInUse && selInRig) { sel->transform.position = keep.position; sel->transform.rotation = keep.rotation; }
    m_applied.clear();
    std::vector<SceneNode*> parts;
    allParts(r, parts);
    for (SceneNode* n : parts) m_applied[n->id] = n->transform;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

void AnimationEditor::render(bool* open) {
    ImGui::SetNextWindowSize(ImVec2(980, 320), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Animation Editor", open)) { ImGui::End(); return; }
    SceneNode* r = rig();
    if (!r) {
        ImGui::TextWrapped("Pick something to animate: select a Model made of parts (like a Rig: MODEL tab > Rig, "
                           "or insert \"Rig\" with Ctrl+I), then press Animate.");
        SceneNode* sel = m_scene->selected();
        SceneNode* cand = sel ? (sel->kind == NodeKind::Model && sel->parent ? sel : Anim::rigOf(sel)) : nullptr;
        if (cand && Anim::rigParts(cand).empty()) cand = nullptr;
        ImGui::BeginDisabled(!cand);
        std::string label = cand ? "Animate \"" + cand->name + "\"" : std::string("Animate (select a model)");
        if (ImGui::Button(label.c_str())) editRig(cand);
        ImGui::EndDisabled();
        ImGui::End();
        return;
    }

    // Space plays / pauses while this window is in use (like Roblox's).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Space, false) && animation()) {
        const float len = clip().length();
        if (!m_playing && len > 0.0f && m_time >= len - 1e-4f) m_time = 0.0f;
        m_playing = !m_playing;
    }
    drawDialogs();
    SceneNode* a = animation();
    if (!a) {
        ImGui::Text("Rig: %s", r->name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Done")) { close(); ImGui::End(); return; }
        ImGui::TextDisabled("No animation in this rig yet.");
        if (ImGui::Button("Create New")) createAnimation("Animation", Anim::emptyClipText());
        ImGui::SameLine();
        if (ImGui::Button("Import from the Library...")) { m_openImport = true; m_importLoaded = "?"; }
        ImGui::End();
        return;
    }

    Anim::Clip c = clip();
    drawToolbar(c);
    if (m_doneRequested) { m_doneRequested = false; close(); ImGui::End(); return; }
    ImGui::Separator();
    float poseW = 230.0f;
    ImGui::BeginChild("##timeline", ImVec2(ImGui::GetContentRegionAvail().x - poseW, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawTimeline(c);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##pose", ImVec2(0, 0), true);
    drawPosePanel(c);
    ImGui::EndChild();
    ImGui::End();
}

void AnimationEditor::drawToolbar(Anim::Clip& c) {
    // Roblox's bar: "..." (the file menu) and the animation's name, then the play
    // buttons, then "time / length".
    SceneNode* a = animation();
    float len = c.length();
    if (ImGui::Button("...", ImVec2(30, 0))) ImGui::OpenPopup("##animFile");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Load, Save, Import, Export (Publish to Guts&Bolts), Create New, Priority");
    drawFileMenu(c);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##animName", &a->name);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The animation's name (rig: %s)", rig()->name.c_str());
    ImGui::SameLine(0, 14);
    auto icon = [](const char* label, const char* tip, bool on = false) {
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        const bool hit = ImGui::Button(label, ImVec2(std::max(30.0f, ImGui::CalcTextSize(label).x + 14.0f), 0));
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine(0, 2);
        return hit;
    };
    // The keyframe before / after the playhead.
    float prevKey = 0.0f, nextKey = len;
    for (const Anim::Keyframe& k : c.keys) {
        if (k.time < m_time - 1e-4f) prevKey = k.time;
        if (k.time > m_time + 1e-4f && k.time < nextKey) nextKey = k.time;
    }
    if (icon("|<", "To the start")) { setTime(0.0f); m_playing = false; }
    if (icon("<", "To the keyframe before")) { setTime(prevKey); m_playing = false; }
    if (icon(m_playing ? "||" : ">", m_playing ? "Pause (Space)" : "Play (Space)", m_playing)) {
        if (!m_playing && len > 0.0f && m_time >= len - 1e-4f) m_time = 0.0f;
        m_playing = !m_playing;
    }
    if (icon(">>", "To the keyframe after")) { setTime(nextKey); m_playing = false; }
    if (icon(">|", "To the end")) { setTime(len); m_playing = false; }
    if (icon("Loop", c.loop ? "Looping: it starts again at the end (click to stop)" : "Play it once (click to loop)", c.loop)) {
        c.loop = !c.loop;
        save(c);
    }
    ImGui::SameLine(0, 12);
    auto clock = [](float t) {
        const int f = (int)std::lround(t * 60.0f);
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d:%02d", f / 60, f % 60);
        return std::string(buf);
    };
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s / %s", clock(m_time).c_str(), clock(len).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Where the playhead is / how long the animation is (seconds:frames, 60 a second)");
    ImGui::SameLine(0, 12);
    ImGui::TextDisabled("%s", Anim::kPriorityNames[(int)c.priority]);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Its priority (change it in the ... menu)");
    ImGui::SameLine(0, 18);
    ImGui::SetNextItemWidth(70);
    ImGui::DragFloat("Show (s)", &m_view, 0.05f, 0.5f, 120.0f, "%.1f");
    ImGui::SameLine();
    static const char* snaps[] = {"Off", "1/60 s", "1/30 s", "1/10 s"};
    int snapIdx = m_fps == 60 ? 1 : m_fps == 30 ? 2 : m_fps == 10 ? 3 : 0;
    ImGui::SetNextItemWidth(80);
    if (ImGui::Combo("Snap", &snapIdx, snaps, 4)) m_fps = snapIdx == 1 ? 60 : snapIdx == 2 ? 30 : snapIdx == 3 ? 10 : 0;
    ImGui::SameLine(0, 18);
    if (ImGui::SmallButton("Done")) { m_doneRequested = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stop animating this rig");
    if (ImGui::GetTime() - m_messageAt < 5.0) {
        ImGui::SameLine(0, 14);
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1), "%s", m_message.c_str());
    }

    SceneNode* r = rig();
    SceneNode* sel = m_scene->selected();
    bool selInRig = r && sel && (sel->isPart() || sel->isBone()) && r->isAncestorOf(sel);
    if (ImGui::Button("+ Keyframe")) {
        // Key everything the animation already moves (or every part, the first time).
        std::vector<SceneNode*> parts = Anim::rigParts(r);
        std::map<std::string, bool> used;
        for (const auto& k : c.keys) for (const auto& [n, p] : k.poses) used[n] = true;
        for (SceneNode* p : parts)
            if (used.empty() || used.count(p->name)) keyPart(c, p);
        save(c);
        m_hasSel = true; m_selTime = snap(m_time); m_selPart.clear();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A keyframe here with every part as it is now");
    ImGui::SameLine();
    ImGui::BeginDisabled(!selInRig);
    if (ImGui::Button("Key part")) { keyPart(c, sel); save(c); m_hasSel = true; m_selTime = snap(m_time); m_selPart = sel->name; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Key just the selected part here (turning or moving it does this too)");
    ImGui::SameLine();
    if (ImGui::Button("Reset part")) {
        Anim::Keyframe& k = c.addKey(snap(m_time));
        k.poses[sel->name] = Anim::Pose();
        save(c);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Put the selected part back how it was built, at this time");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!m_hasSel);
    if (ImGui::Button("Delete")) {
        if (Anim::Keyframe* k = c.keyAt(m_selTime)) {
            if (m_selPart.empty()) k->poses.clear();
            else k->poses.erase(m_selPart);
        }
        c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& k) { return k.poses.empty(); }),
                     c.keys.end());
        save(c);
        m_hasSel = false;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete the picked keyframe (Delete key)");
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        m_clipboard.clear();
        if (Anim::Keyframe* k = c.keyAt(m_selTime)) {
            if (m_selPart.empty()) m_clipboard = k->poses;
            else if (k->poses.count(m_selPart)) m_clipboard[m_selPart] = k->poses[m_selPart];
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_clipboard.empty());
    if (ImGui::Button("Paste")) {
        Anim::Keyframe& k = c.addKey(snap(m_time));
        for (const auto& [n, p] : m_clipboard) k.poses[n] = p;
        save(c);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste the copied poses at the playhead");
    ImGui::EndDisabled();
    ImGui::SameLine(0, 16);
    ImGui::TextDisabled("Pick a part, turn it with Rotate (Ctrl+3) or move it (Ctrl+2): that keys it at the playhead.");
}

void AnimationEditor::drawTimeline(Anim::Clip& c) {
    SceneNode* r = rig();
    if (!r) return;
    std::vector<SceneNode*> parts = Anim::rigParts(r);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float labelW = 150.0f, rulerH = 22.0f, rowH = 20.0f;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float width = std::max(200.0f, ImGui::GetContentRegionAvail().x - labelW - 10.0f);
    int rows = (int)parts.size() + 1;   // "Keyframes" (all) + one per part
    float height = rulerH + rows * rowH;
    m_view = std::max(m_view, c.length() + 0.25f);
    const float x0 = origin.x + labelW;
    auto toX = [&](float t) { return x0 + t / m_view * width; };
    auto toT = [&](float x) { return (x - x0) / width * m_view; };

    // Labels (click a part's name to pick it).
    SceneNode* sel = m_scene->selected();
    for (int i = 0; i < rows; ++i) {
        float y = origin.y + rulerH + i * rowH;
        if (i % 2 == 0) dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(x0 + width, y + rowH), IM_COL32(255, 255, 255, 10));
        ImGui::SetCursorScreenPos(ImVec2(origin.x + 4, y + 2));
        if (i == 0) { ImGui::TextDisabled("All keyframes"); continue; }
        SceneNode* p = parts[i - 1];
        ImGui::PushID((void*)p);
        if (ImGui::Selectable(p->name.c_str(), p == sel, 0, ImVec2(labelW - 8, rowH - 4))) m_scene->select(p);
        ImGui::PopID();
    }

    // Ruler.
    dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x0 + width, origin.y + rulerH), IM_COL32(40, 40, 44, 255));
    float step = m_view <= 2.0f ? 0.1f : m_view <= 8.0f ? 0.5f : m_view <= 30.0f ? 1.0f : 5.0f;
    for (int i = 0; i * step <= m_view + 1e-4f; ++i) {
        float t = i * step, x = toX(t);
        bool major = std::fmod(t + 1e-4f, step * 5.0f) < 2e-3f;
        dl->AddLine(ImVec2(x, origin.y + (major ? 6.0f : 14.0f)), ImVec2(x, origin.y + rulerH), IM_COL32(150, 150, 160, 255));
        if (major) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.1f", t);
            dl->AddText(ImVec2(x + 2, origin.y), IM_COL32(190, 190, 200, 255), buf);
        }
        dl->AddLine(ImVec2(x, origin.y + rulerH), ImVec2(x, origin.y + height), IM_COL32(255, 255, 255, major ? 18 : 8));
    }
    // The end of the animation.
    if (c.length() > 0.0f)
        dl->AddLine(ImVec2(toX(c.length()), origin.y), ImVec2(toX(c.length()), origin.y + height), IM_COL32(120, 200, 120, 160), 2.0f);

    // Keyframes: one diamond per pose, and one on the top row per keyframe.
    struct Hit { float time; std::string part; ImVec2 at; };
    std::vector<Hit> hits;
    for (const Anim::Keyframe& k : c.keys) {
        float x = toX(k.time);
        ImVec2 top(x, origin.y + rulerH + rowH * 0.5f);
        bool pickedAll = m_hasSel && m_selPart.empty() && std::fabs(m_selTime - k.time) < 1e-3f;
        diamond(dl, top, 6.0f, pickedAll ? kKeySelCol : kKeyCol);
        if (!k.name.empty()) dl->AddText(ImVec2(x + 8, top.y - 7), IM_COL32(255, 220, 120, 255), k.name.c_str());
        hits.push_back({k.time, "", top});
        for (size_t i = 0; i < parts.size(); ++i) {
            auto it = k.poses.find(parts[i]->name);
            if (it == k.poses.end()) continue;
            ImVec2 at(x, origin.y + rulerH + (i + 1) * rowH + rowH * 0.5f);
            bool picked = m_hasSel && m_selPart == parts[i]->name && std::fabs(m_selTime - k.time) < 1e-3f;
            diamond(dl, at, 5.0f, picked ? kKeySelCol : it->second.easing == Anim::Easing::Constant ? IM_COL32(200, 200, 200, 255) : kKeyCol);
            hits.push_back({k.time, parts[i]->name, at});
        }
    }
    // The playhead.
    float hx = toX(m_time);
    dl->AddLine(ImVec2(hx, origin.y), ImVec2(hx, origin.y + height), kHeadCol, 2.0f);
    dl->AddTriangleFilled(ImVec2(hx - 6, origin.y), ImVec2(hx + 6, origin.y), ImVec2(hx, origin.y + 8), kHeadCol);

    // One invisible button over the tracks, so dragging counts as one edit (one undo step).
    ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
    ImGui::InvisibleButton("##tracks", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool hovered = ImGui::IsItemHovered();
    ImVec2 m = ImGui::GetMousePos();
    auto hitAt = [&]() -> const Hit* {
        const Hit* best = nullptr;
        float bestD = 8.0f;
        for (const Hit& h : hits) {
            float d = std::max(std::fabs(h.at.x - m.x), std::fabs(h.at.y - m.y));
            if (d < bestD) { bestD = d; best = &h; }
        }
        return best;
    };
    static bool scrubbing = false;
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const Hit* h = m.y > origin.y + rulerH ? hitAt() : nullptr;
        if (h) {
            m_hasSel = true; m_selTime = h->time; m_selPart = h->part;
            m_dragging = true; m_dragFrom = h->time;
            setTime(h->time);
            m_playing = false;
            if (!h->part.empty()) if (SceneNode* p = Anim::findPart(r, h->part)) m_scene->select(p);
        } else {
            scrubbing = true;
            if (m.y > origin.y + rulerH) m_hasSel = false;
        }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float t = snap(toT(m.x));
        if (scrubbing) { setTime(t); m_playing = false; }
        else if (m_dragging && std::fabs(t - m_selTime) > 1e-4f && !c.keyAt(t, 1e-4f)) {
            // Move the picked keyframe (or just that part's pose) to t.
            Anim::Keyframe* from = c.keyAt(m_selTime);
            if (from) {
                if (m_selPart.empty()) {
                    from->time = t;
                    c.sort();
                } else if (from->poses.count(m_selPart)) {
                    Anim::Pose p = from->poses[m_selPart];
                    from->poses.erase(m_selPart);
                    c.addKey(t).poses[m_selPart] = p;
                    c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(),
                                                [](const Anim::Keyframe& k) { return k.poses.empty(); }), c.keys.end());
                }
                m_selTime = t;
                setTime(t);
                save(c);
            }
        } else if (m_dragging && std::fabs(t - m_selTime) > 1e-4f && c.keyAt(t, 1e-4f) && !m_selPart.empty()) {
            // Onto another keyframe: the pose joins it.
            Anim::Keyframe* from = c.keyAt(m_selTime);
            if (from && from->poses.count(m_selPart)) {
                Anim::Pose p = from->poses[m_selPart];
                from->poses.erase(m_selPart);
                c.keyAt(t, 1e-4f)->poses[m_selPart] = p;
                c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(),
                                            [](const Anim::Keyframe& k) { return k.poses.empty(); }), c.keys.end());
                m_selTime = t;
                setTime(t);
                save(c);
            }
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) { scrubbing = false; m_dragging = false; }

    // Right-click a keyframe: easing, name, delete.
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (const Hit* h = hitAt()) {
            m_hasSel = true; m_selTime = h->time; m_selPart = h->part;
            Anim::Keyframe* k = c.keyAt(h->time);
            m_newName = k ? k->name : std::string();
            ImGui::OpenPopup("##keymenu");
        }
    }
    if (ImGui::BeginPopup("##keymenu")) {
        Anim::Keyframe* k = c.keyAt(m_selTime);
        if (!k) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        ImGui::TextDisabled("%s at %.2f s", m_selPart.empty() ? "Keyframe" : m_selPart.c_str(), k->time);
        auto forPicked = [&](const std::function<void(Anim::Pose&)>& fn) {
            if (m_selPart.empty()) for (auto& [n, p] : k->poses) fn(p);
            else if (k->poses.count(m_selPart)) fn(k->poses[m_selPart]);
        };
        Anim::Pose* one = !m_selPart.empty() && k->poses.count(m_selPart) ? &k->poses[m_selPart] : nullptr;
        if (ImGui::BeginMenu("Easing style")) {
            for (int i = 0; i < Anim::kEasingCount; ++i)
                if (ImGui::MenuItem(Anim::kEasingNames[i], nullptr, one && (int)one->easing == i)) {
                    forPicked([&](Anim::Pose& p) { p.easing = (Anim::Easing)i; });
                    save(c);
                }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Easing direction")) {
            for (int i = 0; i < 3; ++i)
                if (ImGui::MenuItem(Anim::kEaseDirNames[i], nullptr, one && (int)one->dir == i)) {
                    forPicked([&](Anim::Pose& p) { p.dir = (Anim::EaseDir)i; });
                    save(c);
                }
            ImGui::EndMenu();
        }
        if (m_selPart.empty()) {
            ImGui::SetNextItemWidth(160);
            if (ImGui::InputTextWithHint("##kname", "Name (for KeyframeReached)", &m_newName, ImGuiInputTextFlags_EnterReturnsTrue)) {
                k->name = m_newName;
                save(c);
                ImGui::CloseCurrentPopup();
            }
        }
        if (ImGui::MenuItem("Copy")) {
            m_clipboard.clear();
            if (m_selPart.empty()) m_clipboard = k->poses;
            else if (k->poses.count(m_selPart)) m_clipboard[m_selPart] = k->poses[m_selPart];
        }
        if (ImGui::MenuItem("Delete")) {
            if (m_selPart.empty()) k->poses.clear(); else k->poses.erase(m_selPart);
            c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& kk) { return kk.poses.empty(); }),
                         c.keys.end());
            save(c);
            m_hasSel = false;
        }
        ImGui::EndPopup();
    }

    // Delete key removes the picked keyframe.
    if (m_hasSel && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (Anim::Keyframe* k = c.keyAt(m_selTime)) {
            if (m_selPart.empty()) k->poses.clear(); else k->poses.erase(m_selPart);
            c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& kk) { return kk.poses.empty(); }),
                         c.keys.end());
            save(c);
        }
        m_hasSel = false;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + 4));
    ImGui::Dummy(ImVec2(labelW + width, 1));
}

void AnimationEditor::drawPosePanel(Anim::Clip& c) {
    SceneNode* r = rig();
    SceneNode* sel = m_scene->selected();
    if (!r || !sel || !(sel->isPart() || sel->isBone()) || !r->isAncestorOf(sel)) {
        ImGui::TextWrapped("Click a part of the rig (in the 3D view or the list) to pose it.");
        ImGui::Spacing();
        ImGui::TextDisabled("Keys: Delete removes the picked keyframe.\nRight-click a keyframe for easing and names.");
        return;
    }
    ImGui::Text("%s", sel->name.c_str());
    float t = snap(m_time);
    Anim::Keyframe* k = c.keyAt(t);
    Anim::Pose* p = k && k->poses.count(sel->name) ? &k->poses[sel->name] : nullptr;
    Anim::Pose shown;
    if (p) shown = *p;
    else {
        std::map<std::string, Anim::Sample> s;
        Anim::sample(c, m_time, s);
        if (auto it = s.find(sel->name); it != s.end()) { shown.pos = it->second.pos; shown.rot = Anim::quatToEuler(it->second.rot); }
    }
    ImGui::TextDisabled(p ? "Keyed at %.2f s" : "Not keyed at %.2f s (in between)", t);
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    ImGui::TextUnformatted("Turn (degrees)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::DragFloat3("##rot", &shown.rot.x, 0.5f, -360.0f, 360.0f, "%.1f");
    ImGui::TextUnformatted("Move (studs)");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::DragFloat3("##pos", &shown.pos.x, 0.01f, -50.0f, 50.0f, "%.2f");
    int e = (int)shown.easing, d = (int)shown.dir;
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::Combo("Style", &e, Anim::kEasingNames, Anim::kEasingCount);
    ImGui::SetNextItemWidth(110);
    changed |= ImGui::Combo("Direction", &d, Anim::kEaseDirNames, 3);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetTooltip("How it gets from this keyframe to the next one");
    if (changed) {
        shown.easing = (Anim::Easing)e;
        shown.dir = (Anim::EaseDir)d;
        c.addKey(t).poses[sel->name] = shown;
        save(c);
        m_hasSel = true; m_selTime = t; m_selPart = sel->name;
    }
    if (p && ImGui::Button("Remove this key")) {
        k->poses.erase(sel->name);
        c.keys.erase(std::remove_if(c.keys.begin(), c.keys.end(), [](const Anim::Keyframe& kk) { return kk.poses.empty(); }),
                     c.keys.end());
        save(c);
    }
}

// ---------------------------------------------------------------------------
// The "..." menu, like Roblox's: Load, Save, Save As, Import, Export, Create New,
// Set Animation Priority. Export > Publish puts it on Guts&Bolts, public (other
// creators can use it) or private; Import > From the Library brings one back.
// ---------------------------------------------------------------------------

SceneNode* AnimationEditor::createAnimation(const std::string& name, const std::string& text) {
    SceneNode* r = rig();
    if (!r) return nullptr;
    auto n = std::make_unique<SceneNode>(name.empty() ? "Animation" : name, NodeKind::Animation);
    n->source = text;
    SceneNode* made = m_scene->insert(std::move(n), r);
    m_anim = made->id;
    m_time = 0.0f;
    m_hasSel = false;
    m_playing = false;
    return made;
}

std::string AnimationEditor::fileText(const std::string& name) {
    nlohmann::json clipJson = nlohmann::json::parse(animation() ? animation()->source : Anim::emptyClipText(), nullptr, false);
    if (!clipJson.is_object()) clipJson = nlohmann::json::parse(Anim::emptyClipText());
    return nlohmann::json{{"format", "gbanim"}, {"version", 1}, {"name", name}, {"clip", clipJson}}.dump();
}

void AnimationEditor::importText(const std::string& text, const std::string& fallbackName) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    std::string clipText, name = fallbackName;
    if (j.is_object() && j.value("format", std::string()) == "gbanim" && j.contains("clip")) {
        clipText = j["clip"].dump();
        name = j.value("name", fallbackName);
    } else if (j.is_object() && j.contains("keys")) {
        clipText = text;   // just the keyframes (an Animation object's text)
    }
    if (clipText.empty()) { m_message = "That isn't a Guts&Bolts animation."; m_messageAt = ImGui::GetTime(); return; }
    createAnimation(name, Anim::dump(Anim::parse(clipText)));
    m_message = "Imported \"" + name + "\".";
    m_messageAt = ImGui::GetTime();
}

void AnimationEditor::drawFileMenu(Anim::Clip& c) {
    if (!ImGui::BeginPopup("##animFile")) return;
    SceneNode* r = rig();
    SceneNode* a = animation();
    std::vector<SceneNode*> anims;
    if (r) findAnimations(r, anims);
    if (ImGui::BeginMenu("Load")) {
        for (SceneNode* n : anims) {
            ImGui::PushID((void*)n);
            if (ImGui::MenuItem(n->name.c_str(), nullptr, n == a)) { m_anim = n->id; m_time = 0.0f; m_hasSel = false; m_playing = false; }
            ImGui::PopID();
        }
        if (anims.empty()) ImGui::TextDisabled("(none in this rig)");
        ImGui::Separator();
        if (ImGui::MenuItem("From the Library...")) { m_openImport = true; m_importLoaded = "?"; }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Save")) {
        // Every change is already kept in the Animation object inside the rig (saved with the game).
        m_message = "Saved \"" + a->name + "\" in " + r->name + " (save the game to keep it).";
        m_messageAt = ImGui::GetTime();
    }
    if (ImGui::MenuItem("Save As...")) { m_openSaveAs = true; m_saveAsName = a->name + " Copy"; }
    if (ImGui::BeginMenu("Import")) {
        if (ImGui::MenuItem("From the Library (Guts&Bolts)...")) { m_openImport = true; m_importLoaded = "?"; }
        if (ImGui::MenuItem("From a File (.gbanim)...", nullptr, false, FileDialog::available())) {
            const std::string path = FileDialog::openAny("Import animation", "Guts&Bolts animation", "*.gbanim");
            if (!path.empty()) {
                std::ifstream f(path, std::ios::binary);
                std::stringstream buf;
                buf << f.rdbuf();
                importText(buf.str(), std::filesystem::path(path).stem().string());
            }
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Export")) {
        if (ImGui::MenuItem("Publish to Guts&Bolts...")) { m_openPublish = true; m_pubName = a->name; m_pubDesc.clear(); }
        if (ImGui::MenuItem("To a File (.gbanim)")) {
            std::error_code ec;
            const std::filesystem::path dir = Paths::appFolder() / "animations";
            std::filesystem::create_directories(dir, ec);
            std::string safe;
            for (char ch : a->name) safe += std::isalnum((unsigned char)ch) || ch == ' ' || ch == '-' ? ch : '_';
            const std::filesystem::path file = dir / ((safe.empty() ? std::string("Animation") : safe) + ".gbanim");
            std::ofstream(file, std::ios::binary) << fileText(a->name);
            m_message = "Exported to " + file.string();
            m_messageAt = ImGui::GetTime();
        }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Create New"))
        createAnimation(anims.empty() ? "Animation" : "Animation" + std::to_string(anims.size() + 1), Anim::emptyClipText());
    if (ImGui::BeginMenu("Set Animation Priority")) {
        for (int i = 0; i < 4; ++i)
            if (ImGui::MenuItem(Anim::kPriorityNames[i], nullptr, (int)c.priority == i)) { c.priority = (Anim::Priority)i; save(c); }
        ImGui::Separator();
        ImGui::TextDisabled("When two animations move the same part,\\nthe higher one wins: Core < Idle < Movement < Action.");
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
}

void AnimationEditor::drawDialogs() {
    // Save As: a copy under a new name (in the same rig).
    if (m_openSaveAs) { ImGui::OpenPopup("Save Animation As"); m_openSaveAs = false; }
    if (ImGui::BeginPopupModal("Save Animation As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputText("Name", &m_saveAsName, ImGuiInputTextFlags_EnterReturnsTrue);
        if ((ImGui::Button("Save", ImVec2(100, 0)) || enter) && !m_saveAsName.empty()) {
            const std::string text = animation() ? animation()->source : Anim::emptyClipText();
            createAnimation(m_saveAsName, text);
            m_message = "Saved as \"" + m_saveAsName + "\".";
            m_messageAt = ImGui::GetTime();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Publish to Guts&Bolts: public (other creators can find and use it) or private (only you).
    if (m_openPublish) { ImGui::OpenPopup("Publish Animation"); m_openPublish = false; }
    ImGui::SetNextWindowSize(ImVec2(460, 0));
    if (ImGui::BeginPopupModal("Publish Animation", nullptr, ImGuiWindowFlags_NoResize)) {
        if (!Online::online()) {
            ImGui::TextWrapped("Log in to Guts&Bolts first (File > Log In) to publish animations.");
        } else {
            ImGui::TextWrapped("Put this animation on Guts&Bolts. Public animations show in the Library so other creators "
                               "can use them in their games; private ones are only for you.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-90);
            ImGui::InputText("Name", &m_pubName);
            ImGui::InputTextMultiline("Description", &m_pubDesc, ImVec2(-90, 50));
            if (ImGui::RadioButton("Public: anyone can find and use it", m_pubPublic)) m_pubPublic = true;
            if (ImGui::RadioButton("Private: only you", !m_pubPublic)) m_pubPublic = false;
            ImGui::Spacing();
            ImGui::BeginDisabled(m_busy || m_pubName.empty());
            if (ImGui::Button("Publish", ImVec2(120, 28))) {
                m_busy = true;
                m_message = "Publishing...";
                m_messageAt = ImGui::GetTime();
                nlohmann::json args = {{"kind", "animation"}, {"name", m_pubName}, {"description", m_pubDesc},
                                       {"access", m_pubPublic ? "public" : "private"},
                                       {"data", Online::base64Encode(fileText(m_pubName))}};
                Online::request("upload", args, [this](const nlohmann::json& res) {
                    m_busy = false;
                    m_messageAt = ImGui::GetTime();
                    if (!res.value("ok", false)) { m_message = res.value("error", std::string("Couldn't publish it.")); return; }
                    const nlohmann::json& as = res.contains("asset") ? res["asset"] : nlohmann::json::object();
                    const long long num = as.value("num", 0LL);
                    m_message = "Published! Its ID is " + (num ? std::to_string(num) : as.value("id", std::string("?"))) + ".";
                });
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Close", ImVec2(100, 28))) ImGui::CloseCurrentPopup();
        if (!m_message.empty() && ImGui::GetTime() - m_messageAt < 30.0) ImGui::TextWrapped("%s", m_message.c_str());
        ImGui::EndPopup();
    }

    // Import from the Library: search public animations (and your own), or paste an ID.
    if (m_openImport) { ImGui::OpenPopup("Import Animation"); m_openImport = false; }
    ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Import Animation", nullptr)) {
        if (!Online::online()) {
            ImGui::TextWrapped("Log in to Guts&Bolts first (File > Log In) to get animations from the Library.");
        } else {
            auto fetch = [this](const std::string& id, const std::string& name) {
                m_busy = true;
                Online::request("get", {{"id", id}}, [this, name](const nlohmann::json& res) {
                    m_busy = false;
                    std::string bytes;
                    if (!res.value("ok", false) || !Online::base64Decode(res.value("data", std::string()), bytes)) {
                        m_message = res.value("error", std::string("Couldn't get that animation."));
                        m_messageAt = ImGui::GetTime();
                        return;
                    }
                    importText(bytes, name);
                });
            };
            ImGui::SetNextItemWidth(200);
            const bool go = ImGui::InputTextWithHint("##animId", "Got an ID? (like 123)", &m_importId, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            ImGui::BeginDisabled(m_busy || m_importId.empty());
            if (ImGui::Button("Import ID") || (go && !m_importId.empty())) {
                std::string id = m_importId;
                if (id.rfind("gb:", 0) == 0) id = id.substr(3);
                // A number: look its real ID up first (asset.info takes either).
                Online::request("asset.info", {{"id", id}}, [this, fetch](const nlohmann::json& res) {
                    if (!res.value("ok", false) || !res.contains("asset") || res["asset"].value("kind", std::string()) != "animation") {
                        m_message = res.value("error", std::string("That ID isn't an animation."));
                        m_messageAt = ImGui::GetTime();
                        return;
                    }
                    fetch(res["asset"].value("id", std::string()), res["asset"].value("name", std::string("Animation")));
                });
            }
            ImGui::EndDisabled();
            ImGui::SetNextItemWidth(200);
            const bool search = ImGui::InputTextWithHint("##animQ", "Search the Library", &m_importQuery, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Search") || search) m_importLoaded = "?";
            if (m_importLoaded != m_importQuery) {
                m_importLoaded = m_importQuery;
                m_importList.clear();
                const std::string want = m_importQuery;
                Online::request("list", {{"kind", "animation"}, {"query", want}, {"sort", "popular"}, {"limit", 100}},
                                [this, want](const nlohmann::json& res) {
                    if (m_importLoaded != want || !res.value("ok", false)) return;
                    m_importList.clear();
                    for (const nlohmann::json& x : res.value("assets", nlohmann::json::array()))
                        m_importList.push_back({x.value("id", std::string()),
                                                x.value("name", std::string()) + "  -  by " + x.value("creatorName", std::string("?")) +
                                                    (x.value("access", std::string()) == "private" ? "  (private)" : "")});
                });
            }
            ImGui::BeginChild("##animList", ImVec2(0, -40), true);
            if (m_importList.empty()) ImGui::TextDisabled("No animations here yet.");
            for (const auto& [id, label] : m_importList) {
                ImGui::PushID(id.c_str());
                if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups) && !m_busy)
                    fetch(id, label.substr(0, label.find("  -  ")));
                ImGui::PopID();
            }
            ImGui::EndChild();
        }
        if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
        if (!m_message.empty() && ImGui::GetTime() - m_messageAt < 10.0) { ImGui::SameLine(); ImGui::TextWrapped("%s", m_message.c_str()); }
        ImGui::EndPopup();
    }
}
