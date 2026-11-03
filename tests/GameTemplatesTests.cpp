// Game templates (File > New from Template): each one builds, saves and loads, and runs for
// a few seconds with a player in it and no script errors.
//
//   GameTemplatesTests [seconds]
#include "core/Log.h"
#include "editor/GameTemplates.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <cstdlib>
#include <algorithm>
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
int countNamed(Scene& scene, const std::string& name) {
    int n = 0;
    scene.forEach([&](SceneNode* x) { if (x->name == name) ++n; });
    return n;
}
} // namespace

int main(int argc, char** argv) {
    const double seconds = argc > 1 ? std::max(0.5, std::atof(argv[1])) : 4.0;
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int fw = 0, fh = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);

    check(GameTemplates::list().size() == 4, "there are four templates");
    for (const GameTemplates::Info& info : GameTemplates::list()) {
        const std::string name = info.name;
        std::vector<Log::Entry> lines;
        Log::setCapture(&lines);
        Scene scene;
        scene.buildDefault();
        GameTemplates::build(scene, info.kind);
        check(scene.root()->findChild("Cube") == nullptr && scene.root()->findChild("WelcomeScript") == nullptr,
              name + ": the sample cube and welcome script are gone");
        check(scene.info().title != "My Game", name + ": it has its own title (" + scene.info().title + ")");
        check(countNamed(scene, "SpawnLocation") >= 1, name + ": there's somewhere to spawn");
        if (info.kind == GameTemplates::Kind::Obby) check(countNamed(scene, "Checkpoint") >= 1, name + ": it has checkpoints");
        if (info.kind == GameTemplates::Kind::Tycoon) check(countNamed(scene, "Dropper1") == 1 && countNamed(scene, "Collector") == 1, name + ": droppers and a collector");
        if (info.kind == GameTemplates::Kind::SwordFight) check(countNamed(scene, "Sword") == 1, name + ": a sword for everyone");
        if (info.kind == GameTemplates::Kind::Racing) check(countNamed(scene, "Car") == 3 && countNamed(scene, "FinishLine") == 1, name + ": cars and a finish line");

        // It saves and loads back.
        const std::string saved = Serializer::saveScene(scene);
        Scene again;
        std::string err;
        check(Serializer::loadScene(again, saved, &err) && countNamed(again, "SpawnLocation") == countNamed(scene, "SpawnLocation"),
              name + ": it saves and loads back the same" + (err.empty() ? "" : " (" + err + ")"));

        // Play it for a bit. In the tycoon, walk into the gate and check that Cash comes in.
        const bool tycoon = info.kind == GameTemplates::Kind::Tycoon;
        if (tycoon) {
            auto watch = std::make_unique<SceneNode>("TestWatch", NodeKind::Script);
            watch->source = R"(task.wait(1)
local p = game:GetService("Players"):GetPlayers()[1]
while true do
    task.wait(1)
    local c = p and p:FindFirstChild("leaderstats") and p.leaderstats:FindFirstChild("Cash")
    if c and c.Value > 0 then print("TEST cash " .. c.Value) break end
end)";
            scene.insert(std::move(watch), nullptr);
        }
        {
            GameSession session(&scene);
            session.start();
            const int steps = (int)((tycoon ? std::max(seconds, 14.0) : seconds) * 60.0);
            for (int i = 0; i < steps; ++i) {
                if (tycoon && i == 30)
                    if (SceneNode* gate = scene.root()->findChild("ClaimGate", true))
                        if (Player* p = scene.player()) p->moveTo(gate->transform.position);
                ImGui::NewFrame();
                session.update(1.0f / 60.0f, 0.0f, false);
                ImGui::EndFrame();
            }
            session.stop();
        }
        if (tycoon) {
            bool claimed = false, paid = false;
            for (const Log::Entry& e : lines) {
                if (e.text.find("owns the tycoon now") != std::string::npos) claimed = true;
                if (e.text.find("TEST cash") != std::string::npos) paid = true;
            }
            check(claimed, name + ": walking into the gate claims it");
            check(paid, name + ": ore rolls into the collector and pays Cash");
        }
        Log::setCapture(nullptr);
        std::string errors;
        for (const Log::Entry& e : lines) if (e.level == Log::Level::Error) errors += "\n      " + e.text;
        check(errors.empty(), name + ": plays with no script errors" + errors);
    }
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
