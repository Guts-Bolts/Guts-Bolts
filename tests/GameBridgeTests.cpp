// The scripting side of bridging games to other sites (like Lyreto): Players.PlayerChatted and
// player.Chatted, TextChatService:DisplaySystemMessage, game.GameId, and HttpService's Secrets
// and web requests (which always go through the Guts&Bolts server, so offline they say why).
//
//   GameBridgeTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

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
    err.clear();
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}
void test(GameSession& session, const std::string& what, const std::string& code) {
    std::string err;
    check(run(session, code, err), what + (err.empty() ? "" : "  (" + err + ")"));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: GameBridgeTests <game file>\n"; return 2; }
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
    scene.info().publishedId = "";
    session.setRunOnly(true);
    session.scripts().setPlayerName("Tester");
    session.start();

    test(session, "game.GameId is 0 before the game is published", R"(
        assert(game.GameId == 0, "GameId was " .. tostring(game.GameId))
        assert(game.PlaceId == 0)
    )");
    session.scripts().setOnlineData(false, "4321");
    test(session, "game.GameId is the published game's number", R"(
        assert(game.GameId == 4321, "GameId was " .. tostring(game.GameId))
        assert(type(game.GameId) == "number")
    )");

    test(session, "listening to chat", R"(
        _G.heard, _G.mine = {}, {}
        game:GetService("Players").PlayerChatted:Connect(function(player, message) table.insert(_G.heard, player.Name .. ": " .. message) end)
        game.Players.LocalPlayer.Chatted:Connect(function(message) table.insert(_G.mine, message) end)
    )");
    session.scripts().fireChatted("Tester", "hello there");
    session.scripts().fireChatted("Nobody", "not in this game");
    session.update(1.0f / 60.0f, 0.0f, false);
    test(session, "Players.PlayerChatted and player.Chatted hear what was said", R"(
        assert(#_G.heard == 1 and _G.heard[1] == "Tester: hello there", "heard " .. #_G.heard .. ": " .. tostring(_G.heard[1]))
        assert(#_G.mine == 1 and _G.mine[1] == "hello there")
    )");

    test(session, "DisplaySystemMessage queues a line for the chat", R"(
        game:GetService("TextChatService"):DisplaySystemMessage("The round starts!")
    )");
    auto lines = session.scripts().takeSystemChat();
    check(lines.size() == 1 && lines[0] == "The round starts!", "the app gets the system line");
    test(session, "at most 20 system lines every 10 seconds", R"(
        for i = 2, 20 do game:GetService("TextChatService"):DisplaySystemMessage("line " .. i) end
        local ok, err = pcall(function() game:GetService("TextChatService"):DisplaySystemMessage("one too many") end)
        assert(not ok and tostring(err):find("too many"), tostring(err))
    )");
    check(session.scripts().takeSystemChat().size() == 19, "19 more lines made it");

    test(session, "a Secret is a stand-in that can't be read or joined to text", R"(
        local HttpService = game:GetService("HttpService")
        local s = HttpService:GetSecret("LyretoKey")
        assert(tostring(s) == "Secret")
        assert(tostring(s:AddPrefix("Bearer ")) == "Secret")
        assert(getmetatable(s) == "Secret", "the metatable is hidden")
        assert(not pcall(function() return "Bearer " .. s end), "joining it to text is an error")
        assert(not pcall(function() return HttpService:GetSecret("bad name!") end))
        local n = 0 for _ in pairs(s) do n = n + 1 end
        assert(n == 0, "nothing inside it to look at")
    )");

    test(session, "web requests offline say why instead of reaching the site", R"(
        local HttpService = game:GetService("HttpService")
        local ok, err = pcall(function() return HttpService:GetAsync("https://lyreto.com/") end)
        assert(not ok and tostring(err):find("connected"), tostring(err))
        ok, err = pcall(function() return HttpService:RequestAsync({ Url = 5 }) end)
        assert(not ok and tostring(err):find("Url must be text"), tostring(err))
        ok, err = pcall(function() return HttpService:RequestAsync({ Url = "https://lyreto.com/", Headers = { A = {} } }) end)
        assert(not ok and tostring(err):find("text or a Secret"), tostring(err))
    )");
    session.scripts().setOnlineData(false, "");
    test(session, "an unpublished game is told to publish first", R"(
        local ok, err = pcall(function() return game:GetService("HttpService"):PostAsync("https://lyreto.com/", "{}") end)
        assert(not ok and tostring(err):find("published game"), tostring(err))
    )");

    session.stop();
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
