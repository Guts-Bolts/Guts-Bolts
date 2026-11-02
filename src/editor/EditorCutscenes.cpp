// Tools > Cutscene Maker: make a camera fly-through without writing the camera code.
// A cutscene is a Model (attribute Cutscene = true) of "shot" parts: fly the Studio camera
// to a spot, press Add Shot, and the shot remembers where the camera is and which way it
// looks. Preview flies the Studio camera through them; in the game, CutsceneService:Play
// (or "Play when the game starts") does the same with the game's camera (ScriptEngine.cpp).
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "../renderer/MeshLibrary.h"
#include "../scene/Scene.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
const Attribute* findAttr(const SceneNode* n, const char* name) {
    for (const Attribute& a : n->attributes) if (a.name == name) return &a;
    return nullptr;
}
double numAttr(const SceneNode* n, const char* name, double fallback) {
    const Attribute* a = findAttr(n, name);
    return a && a->type == Attribute::Number ? a->n : fallback;
}
bool boolAttr(const SceneNode* n, const char* name) {
    const Attribute* a = findAttr(n, name);
    return a && a->type == Attribute::Bool && a->b;
}
void setNum(SceneNode* n, const char* name, double v) {
    for (Attribute& a : n->attributes) if (a.name == name) { a.type = Attribute::Number; a.n = v; return; }
    Attribute a; a.name = name; a.type = Attribute::Number; a.n = v;
    n->attributes.push_back(a);
}
void setBool(SceneNode* n, const char* name, bool v) {
    for (Attribute& a : n->attributes) if (a.name == name) { a.type = Attribute::Bool; a.b = v; return; }
    Attribute a; a.name = name; a.type = Attribute::Bool; a.b = v;
    n->attributes.push_back(a);
}
bool isCutscene(const SceneNode* n) { return n->kind == NodeKind::Model && boolAttr(n, "Cutscene"); }

// The shots in play order: Order attribute, else the number at the end of the name (the same
// rule as CutsceneService in the game).
std::vector<SceneNode*> shotsOf(SceneNode* cut) {
    std::vector<SceneNode*> shots;
    for (auto& c : cut->children) if (c->kind == NodeKind::Part) shots.push_back(c.get());
    auto num = [](const SceneNode* s) {
        size_t i = s->name.size();
        while (i > 0 && std::isdigit((unsigned char)s->name[i - 1])) --i;
        return i < s->name.size() ? std::atoi(s->name.c_str() + i) : INT_MAX;
    };
    std::stable_sort(shots.begin(), shots.end(), [&](const SceneNode* a, const SceneNode* b) {
        const Attribute* oa = findAttr(a, "Order"); const Attribute* ob = findAttr(b, "Order");
        if (oa && ob && oa->n != ob->n) return oa->n < ob->n;
        if (num(a) != num(b)) return num(a) < num(b);
        return a->name < b->name;
    });
    return shots;
}
glm::mat4 shotFrame(const SceneNode* s) {
    glm::mat4 m = s->worldMatrix();
    for (int i = 0; i < 3; ++i) m[i] = glm::vec4(glm::normalize(glm::vec3(m[i])), 0.0f);
    return m;
}
glm::vec3 curve(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, float t) {
    const float t2 = t * t, t3 = t2 * t;
    return 0.5f * (2.0f * p1 + (p2 - p0) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (3.0f * p1 - p0 - 3.0f * p2 + p3) * t3);
}
// Where the camera is `t` seconds into the cutscene. False once it's over.
bool cameraAt(const std::vector<SceneNode*>& shots, double t, float baseFov, glm::mat4& frame, float& fov) {
    if (shots.empty()) return false;
    auto fovOf = [&](const SceneNode* s, float f) { return (float)numAttr(s, "FieldOfView", f); };
    double at = std::max(0.0, numAttr(shots[0], "Hold", 0.0));
    frame = shotFrame(shots[0]);
    fov = fovOf(shots[0], baseFov);
    if (t < at) return true;
    for (size_t i = 1; i < shots.size(); ++i) {
        const SceneNode* a = shots[i - 1]; const SceneNode* b = shots[i];
        const double len = std::max(0.0, numAttr(b, "Time", 2.0)), hold = std::max(0.0, numAttr(b, "Hold", 0.0));
        const float fa = fovOf(a, baseFov), fb = fovOf(b, fa);
        if (t < at + len) {
            float e = (float)((t - at) / len);
            e = e * e * (3.0f - 2.0f * e);
            const glm::mat4 ma = shotFrame(a), mb = shotFrame(b);
            const glm::vec3 p0 = glm::vec3((i >= 2 ? shotFrame(shots[i - 2]) : ma)[3]);
            const glm::vec3 p3 = glm::vec3((i + 1 < shots.size() ? shotFrame(shots[i + 1]) : mb)[3]);
            const glm::quat q = glm::slerp(glm::quat_cast(glm::mat3(ma)), glm::quat_cast(glm::mat3(mb)), e);
            frame = glm::mat4_cast(q);
            frame[3] = glm::vec4(curve(p0, glm::vec3(ma[3]), glm::vec3(mb[3]), p3, e), 1.0f);
            fov = fa + (fb - fa) * e;
            return true;
        }
        at += len;
        frame = shotFrame(b);
        fov = fb;
        if (t < at + hold) return true;
        at += hold;
    }
    return false;
}
// Put a shot where the Studio camera is, looking the same way.
void placeShot(SceneNode* s, const Camera& cam) {
    const glm::vec3 eye = cam.position(), fwd = cam.forward();
    const glm::mat4 m = glm::inverse(glm::lookAt(eye, eye + fwd, std::abs(fwd.y) > 0.99f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0)));
    float z, y, x;
    glm::extractEulerAngleZYX(m, z, y, x);
    s->transform.position = eye;
    s->transform.rotation = glm::degrees(glm::vec3(x, y, z));
}
} // namespace

void Editor::stopCutscenePreview() {
    if (!m_cutPreview) return;
    m_cutPreview = false;
    Camera& cam = m_viewport->camera();
    cam.clearScripted();
    cam.fov = m_cutPreviewFov;
}

void Editor::renderCutscenes() {
    SceneNode* cut = m_cutsceneId ? m_scene->findById(m_cutsceneId) : nullptr;
    if (cut && !isCutscene(cut)) cut = nullptr;
    // The preview runs even while the window is tucked away, until it ends or play starts.
    if (m_cutPreview) {
        Camera& cam = m_viewport->camera();
        glm::mat4 frame; float fov = m_cutPreviewFov;
        if (!cut || m_playing || !m_showCutscenes ||
            !cameraAt(shotsOf(cut), ImGui::GetTime() - m_cutPreviewStart, m_cutPreviewFov, frame, fov)) {
            stopCutscenePreview();
        } else {
            cam.useCFrame = true;
            cam.noInput = true;
            cam.cframe = frame;
            cam.fov = fov;
        }
    }
    if (!m_showCutscenes) return;
    ImGui::SetNextWindowSize(ImVec2(470, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cutscene Maker", &m_showCutscenes)) { ImGui::End(); return; }
    ImGui::TextWrapped("Fly the camera to a spot and press Add Shot. In the game, the camera glides from shot to shot in order.");
    ImGui::Spacing();

    std::vector<SceneNode*> cuts;
    m_scene->forEach([&](SceneNode* n) { if (isCutscene(n)) cuts.push_back(n); });
    if (!cut && !cuts.empty()) { cut = cuts.front(); m_cutsceneId = cut->id; }
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("##cut", cut ? cut->name.c_str() : "(no cutscenes yet)")) {
        for (SceneNode* c : cuts) {
            ImGui::PushID((int)c->id);
            if (ImGui::Selectable(c->name.c_str(), c == cut)) { stopCutscenePreview(); cut = c; m_cutsceneId = c->id; }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button("New cutscene")) {
        std::string name = "Cutscene";
        for (int i = 2; m_scene->root()->findChild(name); ++i) name = "Cutscene" + std::to_string(i);
        cut = m_scene->insert(std::make_unique<SceneNode>(name, NodeKind::Model), nullptr);
        setBool(cut, "Cutscene", true);
        m_cutsceneId = cut->id;
        m_scene->select(cut);
        m_dirty = true;
    }
    ImGui::EndDisabled();

    if (!cut) {
        ImGui::TextDisabled("Make one with New cutscene.");
        ImGui::End();
        return;
    }
    bool onStart = boolAttr(cut, "PlayOnStart");
    if (ImGui::Checkbox("Play when the game starts", &onStart)) { setBool(cut, "PlayOnStart", onStart); m_dirty = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Or play it from a script any time:\nCutsceneService:Play(workspace.%s)", cut->name.c_str());

    std::vector<SceneNode*> shots = shotsOf(cut);
    Camera& cam = m_viewport->camera();
    ImGui::Separator();
    ImGui::BeginDisabled(m_playing || m_cutPreview);
    if (ImGui::Button("Add shot from camera")) {
        int n = (int)shots.size() + 1;
        while (cut->findChild("Shot" + std::to_string(n))) ++n;
        SceneNode* s = m_scene->addNode("Shot" + std::to_string(n), PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
        m_scene->reparent(s, cut);
        s->transform.scale = {1.0f, 0.8f, 1.6f};
        s->color = {1.0f, 0.8f, 0.2f};
        s->material = Material::Neon;
        s->transparency = 0.5f;
        s->anchored = true;
        s->canCollide = false;
        placeShot(s, cam);
        setNum(s, "Time", shots.empty() ? 0.0 : 2.0);
        shots = shotsOf(cut);
        m_dirty = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(shots.size() < 2 || m_playing);
    if (!m_cutPreview) {
        if (ImGui::Button("Preview")) {
            m_cutPreview = true;
            m_cutPreviewStart = ImGui::GetTime();
            m_cutPreviewFov = cam.fov;
        }
    } else if (ImGui::Button("Stop preview")) {
        stopCutscenePreview();
    }
    ImGui::EndDisabled();

    double total = 0.0;
    for (size_t i = 0; i < shots.size(); ++i) total += (i ? numAttr(shots[i], "Time", 2.0) : 0.0) + numAttr(shots[i], "Hold", 0.0);
    ImGui::TextDisabled("%d shots, %.1f seconds long", (int)shots.size(), total);

    if (ImGui::BeginTable("shots", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Shot");
        ImGui::TableSetupColumn("Seconds to get here", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Stay", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableHeadersRow();
        SceneNode* remove = nullptr;
        for (size_t i = 0; i < shots.size(); ++i) {
            SceneNode* s = shots[i];
            ImGui::PushID((int)s->id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(s->name.c_str(), false)) m_scene->select(s);
            ImGui::TableNextColumn();
            if (i == 0) ImGui::TextDisabled("start");
            else {
                float t = (float)numAttr(s, "Time", 2.0);
                ImGui::SetNextItemWidth(-1);
                if (ImGui::DragFloat("##t", &t, 0.05f, 0.0f, 60.0f, "%.1f s")) { setNum(s, "Time", t); m_dirty = true; }
            }
            ImGui::TableNextColumn();
            float h = (float)numAttr(s, "Hold", 0.0);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::DragFloat("##h", &h, 0.05f, 0.0f, 60.0f, "%.1f s")) { setNum(s, "Hold", h); m_dirty = true; }
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(m_cutPreview);
            if (ImGui::SmallButton("Look")) {   // put the Studio camera at this shot
                const glm::mat4 f = shotFrame(s);
                const glm::vec3 eye(f[3]), fwd = -glm::normalize(glm::vec3(f[2]));
                cam.clearScripted();
                cam.pitch = glm::degrees(std::asin(std::clamp(-fwd.y, -1.0f, 1.0f)));
                cam.yaw = glm::degrees(std::atan2(-fwd.z, -fwd.x));
                cam.pivot = eye + fwd * cam.distance;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the camera to this shot");
            ImGui::SameLine();
            if (ImGui::SmallButton("Retake")) { placeShot(s, cam); m_dirty = true; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move this shot to where the camera is now");
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) remove = s;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (remove) { m_scene->deselect(); m_scene->removeNode(remove); m_dirty = true; }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Shots are only markers: they turn invisible when the game runs.");
    ImGui::End();
}
