// game.ServerLowMemoryWarning and Stats:GetTotalMemoryUsageMb (src/scripting/ServerMemory.cpp):
// the warning fires when memory is high, at most once a minute, and only on the server.
//
//   ServerMemoryTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scripting/ServerMemory.h"

#include <imgui.h>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}
void seconds(GameSession& session, int n) {
    for (int i = 0; i < n * 30; ++i) {   // (the game steps at most 1/30 of a second a frame)
        ImGui::NewFrame();
        session.update(1.0f / 30.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: ServerMemoryTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    check(ServerMemory::usedMb() > 1.0, "this program's memory can be measured (" + std::to_string((int)ServerMemory::usedMb()) + " MB)");
    check(ServerMemory::limitMb() >= 64.0, "there's a limit (" + std::to_string((int)ServerMemory::limitMb()) + " MB)");

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);   // like a game server machine
    session.start();

    bool ok = run(session,
        "local Stats = game:GetService('Stats')\n"
        "assert(Stats:GetTotalMemoryUsageMb() > 0, 'memory used')\n"
        "assert(Stats:GetServerMemoryLimitMb() >= 64, 'memory limit')\n"
        "_G.warnings, _G.lastUsed, _G.lastLimit = 0, 0, 0\n"
        "game.ServerLowMemoryWarning:Connect(function(used, limit)\n"
        "    _G.warnings = _G.warnings + 1\n"
        "    _G.lastUsed, _G.lastLimit = used, limit\n"
        "end)\n", err);
    check(ok, "scripts connect to game.ServerLowMemoryWarning and read Stats" + (ok ? std::string() : ": " + err));

    ServerMemory::pretend(300, 1000);
    seconds(session, 10);
    ok = run(session, "assert(_G.warnings == 0, tostring(_G.warnings))", err);
    check(ok, "30% of the limit: no warning" + (ok ? std::string() : ": " + err));

    ServerMemory::pretend(800, 1000);
    seconds(session, 6);
    ok = run(session, "assert(_G.warnings == 1, tostring(_G.warnings)); assert(_G.lastUsed == 800 and _G.lastLimit == 1000)", err);
    check(ok, "80% of the limit: one warning, with the numbers" + (ok ? std::string() : ": " + err));

    seconds(session, 30);
    ok = run(session, "assert(_G.warnings == 1, tostring(_G.warnings))", err);
    check(ok, "still high 30 seconds later: no second warning yet" + (ok ? std::string() : ": " + err));

    seconds(session, 40);
    ok = run(session, "assert(_G.warnings == 2, tostring(_G.warnings))", err);
    check(ok, "still high after a minute: a second warning" + (ok ? std::string() : ": " + err));

    ok = run(session, "assert(game:GetService('Stats'):GetTotalMemoryUsageMb() == 800)", err);
    check(ok, "Stats:GetTotalMemoryUsageMb says the same number" + (ok ? std::string() : ": " + err));
    session.stop();

    // A joined player's computer isn't the server: no warning there.
    {
        Scene clientScene;
        GameSession client(&clientScene);
        Serializer::loadGameFile(clientScene, argv[1], &err);
        client.setRunOnly(true);
        client.setRole(GameSession::Role::Client);
        client.start();
        run(client, "_G.warnings = 0; game.ServerLowMemoryWarning:Connect(function() _G.warnings = _G.warnings + 1 end)", err);
        seconds(client, 70);
        ok = run(client, "assert(_G.warnings == 0, tostring(_G.warnings))", err);
        check(ok, "a player's computer in someone else's game never gets it" + (ok ? std::string() : ": " + err));
        client.stop();
    }
    ServerMemory::pretend(0, 0);

    std::cout << (failures ? "\nFAILED: " + std::to_string(failures) + "\n" : "\nAll server memory checks passed.\n");
    return failures ? 1 : 0;
}
