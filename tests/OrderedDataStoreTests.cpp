// OrderedDataStore from a script, played offline (saved on this computer): GetSortedAsync
// gives the numbers back sorted, a page at a time, and only numbers can be saved.
// (The servers' side, data.sorted, is in ServerOrderedDataTests and worker/test/leaderboards.test.mjs.)
//
//   OrderedDataStoreTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <cstdlib>
#include <filesystem>
#include <imgui.h>
#include <iostream>
#include <string>
#include <chrono>

namespace {
int failures = 0;
void setEnv(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Run some Lua; false if it didn't compile or hit an error while running (errors only get logged).
bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: OrderedDataStoreTests <game file>\n"; return 2; }
    // Save files go to a folder of our own, not the real account's.
    const auto dir = std::filesystem::temp_directory_path() /
        ("gb-ordered-ds-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    setEnv("XDG_CONFIG_HOME", dir.string());
    setEnv("GB_ACCOUNT_DIR", (dir / "account").string());

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
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    check(run(session,
              "local DS = game:GetService('DataStoreService') "
              "local coins = DS:GetOrderedDataStore('TestCoins') "
              "coins:SetAsync('a', 10) coins:SetAsync('b', 30) coins:SetAsync('c', 20) coins:SetAsync('e', 5) "
              "assert(coins:IncrementAsync('a', 1) == 11) "
              "local pages = coins:GetSortedAsync(false, 2) "
              "local p = pages:GetCurrentPage() "
              "assert(#p == 2 and p[1].key == 'b' and p[1].value == 30 and p[2].key == 'c', 'first page') "
              "assert(pages.IsFinished == false, 'more pages') "
              "pages:AdvanceToNextPageAsync() p = pages:GetCurrentPage() "
              "assert(#p == 2 and p[1].key == 'a' and p[1].value == 11 and p[2].key == 'e', 'second page') "
              "assert(pages.IsFinished == true, 'no more pages') "
              "assert(not pcall(function() pages:AdvanceToNextPageAsync() end), 'past the end is an error') "
              "local up = coins:GetSortedAsync(true, 10, 10, 25):GetCurrentPage() "
              "assert(#up == 2 and up[1].key == 'a' and up[2].key == 'c', 'smallest first, between 10 and 25')", err),
          "sorted pages from a script " + err);
    check(run(session,
              "local coins = game:GetService('DataStoreService'):GetOrderedDataStore('TestCoins') "
              "assert(not pcall(function() coins:SetAsync('x', 'words') end)) "
              "assert(coins:RemoveAsync('b') == 30) "
              "assert(coins:GetSortedAsync(false, 1):GetCurrentPage()[1].key == 'c')", err),
          "only numbers, and removed keys leave the board " + err);

    session.stop();
    ImGui::DestroyContext();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::cout << (failures ? "ordered data store tests FAILED\n" : "ordered data store tests passed\n");
    return failures ? 1 : 0;
}
