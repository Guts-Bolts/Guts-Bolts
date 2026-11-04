// The Live Profiler: while it's open it times each part of a frame and each script's code,
// so a slow script stands out at the top (core/FrameProfile.cpp, game/LiveProfiler.cpp).
// Closed, it measures nothing.
//
//   LiveProfilerTests <a game file with a player>
#include "core/FrameProfile.h"
#include "game/GameSession.h"
#include "game/LiveProfiler.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: LiveProfilerTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    // A slow script (lots of sums every frame) and a quick one.
    auto slow = std::make_unique<SceneNode>("SlowScript", NodeKind::Script);
    slow->source = R"(
        game:GetService("RunService").Heartbeat:Connect(function()
            local x = 0
            for i = 1, 200000 do x = x + i end
        end)
    )";
    auto quick = std::make_unique<SceneNode>("QuickScript", NodeKind::Script);
    quick->source = R"(game:GetService("RunService").Heartbeat:Connect(function() local y = 1 + 1 end))";
    scene.insert(std::move(slow), scene.root());
    scene.insert(std::move(quick), scene.root());
    session.start();

    auto frames = [&](int n, bool drawWindow, bool* open) {
        for (int i = 0; i < n; ++i) {
            ImGui::NewFrame();
            session.update(1.0f / 60.0f, 0.0f, false);
            if (drawWindow) LiveProfiler::draw(open);
            ImGui::EndFrame();
            FrameProfile::endFrame(16.7);
        }
    };

    // Closed: nothing is measured.
    bool open = false;
    frames(30, true, &open);
    check(!FrameProfile::live() && FrameProfile::sections().empty() && FrameProfile::scripts().empty() && FrameProfile::frames().empty(),
          "closed: it measures nothing");

    // Open: parts of the frame and scripts.
    open = true;
    frames(60, true, &open);
    const auto sections = FrameProfile::sections();
    const auto scripts = FrameProfile::scripts();
    bool hasScripts = false, hasPhysics = false;
    for (const auto& r : sections) {
        hasScripts = hasScripts || r.name == "game: scripts";
        hasPhysics = hasPhysics || r.name == "game: part physics";
    }
    check(FrameProfile::live() && hasScripts && hasPhysics, "open: it times the parts of a frame (scripts, physics...)");
    check(!scripts.empty() && scripts[0].name.find("SlowScript") != std::string::npos && scripts[0].avg > 0.05f,
          "the slow script is at the top of the scripts (" + (scripts.empty() ? std::string("none") : scripts[0].name + " " +
          std::to_string(scripts[0].avg) + " ms") + ")");
    float quickMs = -1.0f;
    for (const auto& r : scripts) if (r.name.find("QuickScript") != std::string::npos) quickMs = r.avg;
    check(quickMs >= 0.0f && quickMs < scripts[0].avg / 5.0f, "the quick script is listed too, and much lower (" + std::to_string(quickMs) + " ms)");
    check(FrameProfile::frames().size() == 60, "it keeps the frame times for the graph");
    check(scripts[0].max >= scripts[0].avg && scripts[0].now > 0.0f, "Now / Average / Slowest add up");

    char name[64];
    check(std::string(LiveProfiler::niceName("game: part physics", name, sizeof(name))) == "Part physics" &&
              std::string(LiveProfiler::niceName("draw: liquid", name, sizeof(name))) == "Drawing: liquid",
          "parts get plain names");

    // Closing it stops the measuring (and the old numbers stay as they were).
    open = false;
    frames(1, true, &open);
    FrameProfile::clear();
    frames(20, true, &open);
    check(!FrameProfile::live() && FrameProfile::scripts().empty(), "closed again: measuring stops");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
