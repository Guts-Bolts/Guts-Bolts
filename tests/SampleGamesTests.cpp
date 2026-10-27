// Loads every game in games/ and runs it for a few seconds the way a game server
// does (no window, nobody playing), so an engine change that breaks the sample
// games, or makes their scripts error, fails the build instead of reaching players.
//
//   SampleGamesTests <games folder> [seconds]
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

// The same pretend screen GutsAndBoltsGameServer uses (scripts can ask about the UI).
void setUpImGui() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
}

// Run one game; returns the problems found (empty = it's fine).
std::vector<std::string> runGame(const std::filesystem::path& file, double seconds) {
    std::vector<std::string> problems;
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    {
        Scene scene;
        GameSession session(&scene);
        std::string err;
        if (!Serializer::loadGameFile(scene, file.string(), &err)) {
            Log::setCapture(nullptr);
            return {"couldn't load: " + err};
        }
        if (Player* p = scene.player()) {   // nobody plays, like on a game server
            if (SceneNode* r = p->root()) scene.removeNode(r);
            p->setRootId(0);
        }
        session.setRunOnly(true);
        session.scripts().setNoLocalPlayer(true);
        session.scripts().setPlayerName("Server");
        session.start();
        constexpr float kStep = 1.0f / 60.0f;
        for (int i = 0; i < (int)(seconds * 60.0); ++i) {
            ImGui::NewFrame();
            session.update(kStep, 0.0f, false);
            ImGui::EndFrame();
        }
        session.stop();
    }
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) problems.push_back(e.text);
    return problems;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: SampleGamesTests <games folder> [seconds]\n"; return 2; }
    const std::filesystem::path folder = argv[1];
    const double seconds = argc > 2 ? std::max(0.1, std::atof(argv[2])) : 5.0;

    std::vector<std::filesystem::path> games;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
        if (entry.path().extension() == ".gbscene") games.push_back(entry.path());
    std::sort(games.begin(), games.end());
    if (games.empty()) { std::cerr << "No .gbscene files in " << folder.string() << "\n"; return 1; }

    Mesh::headless = true;
    setUpImGui();
    int failed = 0;
    for (const auto& game : games) {
        const auto problems = runGame(game, seconds);
        const std::string name = game.filename().string();
        if (problems.empty()) {
            std::cout << "ok    " << name << "\n";
            continue;
        }
        ++failed;
        std::cout << "FAIL  " << name << "\n";
        for (const auto& p : problems) std::cout << "      " << p << "\n";
    }
    std::cout << (games.size() - failed) << " of " << games.size() << " sample games ran without errors\n";
    ImGui::DestroyContext();
    return failed ? 1 : 0;
}
