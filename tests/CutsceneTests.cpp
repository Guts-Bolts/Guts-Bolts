// Cutscenes (CutsceneService, made with Studio's Cutscene Maker): the camera flies through the
// shots in order, passes through each one, hands the camera back at the end, and the shot
// markers go invisible when the game starts.
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"

#include <imgui.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
SceneNode* shot(Scene& scene, SceneNode* cut, const char* name, glm::vec3 at, double time) {
    auto p = std::make_unique<SceneNode>(name, NodeKind::Part);
    p->transform.position = at;
    p->transform.scale = {1, 1, 1};
    p->anchored = true;
    Attribute a; a.name = "Time"; a.type = Attribute::Number; a.n = time;
    p->attributes.push_back(a);
    return scene.insert(std::move(p), cut);
}
bool has(const std::vector<Log::Entry>& lines, const std::string& text) {
    for (const Log::Entry& e : lines) if (e.text.find(text) != std::string::npos) return true;
    return false;
}
} // namespace

int main() {
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);

    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    {
        Scene scene;
        auto m = std::make_unique<SceneNode>("Intro", NodeKind::Model);
        Attribute a; a.name = "Cutscene"; a.type = Attribute::Bool; a.b = true;
        m->attributes.push_back(a);
        SceneNode* cut = scene.insert(std::move(m), nullptr);
        // (out of order on purpose: they play by the number in their name)
        shot(scene, cut, "Shot3", {20, 10, 0}, 1.0);
        shot(scene, cut, "Shot1", {0, 10, 0}, 0.0);
        shot(scene, cut, "Shot2", {10, 10, 0}, 1.0);
        auto s = std::make_unique<SceneNode>("Watch", NodeKind::Script);
        s->source = R"(
local cut = workspace.Intro
local cam = workspace.CurrentCamera
CutsceneService.Finished:Connect(function(c, done) print("TEST finished " .. c.Name .. " " .. tostring(done)) end)
task.spawn(function()
    task.wait(0.5)
    print("TEST hidden " .. tostring(cut.Shot2.Transparency == 1 and not cut.Shot2.CanCollide))
    print("TEST type " .. tostring(cam.CameraType))
    task.wait(0.5)
    print(string.format("TEST at1 %.1f", cam.CFrame.Position.X))
    task.wait(1.0)
    print(string.format("TEST at2 %.1f", cam.CFrame.Position.X))
end)
CutsceneService:Play(cut)
print("TEST after " .. tostring(cam.CameraType) .. " " .. tostring(CutsceneService.Playing == nil))
)";
        scene.insert(std::move(s), nullptr);
        GameSession session(&scene);
        session.start();
        for (int i = 0; i < 4 * 60; ++i) {
            ImGui::NewFrame();
            session.update(1.0f / 60.0f, 0.0f, false);
            ImGui::EndFrame();
        }
        session.stop();
    }
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.text.rfind("TEST", 0) == 0 || e.level == Log::Level::Error) std::cout << "      " << e.text << "\n";

    check(has(lines, "TEST hidden true"), "the shot markers go invisible when the game starts");
    check(has(lines, "TEST type Scriptable") || has(lines, "TEST type Enum.CameraType.Scriptable"), "the cutscene takes the camera");
    // Shot2 is at X=10, reached after 1 second; Shot3 (X=20) after 2.
    bool at1 = false, at2 = false;
    for (const Log::Entry& e : lines) {
        if (e.text.rfind("TEST at1 ", 0) == 0) { const double x = std::stod(e.text.substr(9)); at1 = x > 8.5 && x < 11.5; }
        if (e.text.rfind("TEST at2 ", 0) == 0) { const double x = std::stod(e.text.substr(9)); at2 = x > 19.0 && x < 20.5; }
    }
    check(at1, "after 1 second the camera is at the second shot");
    check(at2, "after 2 seconds it's at the last shot");
    check(has(lines, "TEST finished Intro true"), "Finished fires when it's over");
    check(has(lines, "TEST after Custom true") || has(lines, "TEST after Enum.CameraType.Custom true"), "then the camera goes back to normal");
    bool errors = false;
    for (const Log::Entry& e : lines) if (e.level == Log::Level::Error) errors = true;
    check(!errors, "no script errors");
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
