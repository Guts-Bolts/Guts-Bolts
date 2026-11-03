// Roblox chat-bridge scripts (like Lyreto's Game Bridge module) running as they are: the Luau they're
// written in (generalized for loops, if-expressions), RunService:IsServer/IsStudio, game.JobId,
// TextService:FilterStringAsync, StarterGui:SetCore("ChatMakeSystemMessage") and TextChatService.ChatVersion,
// IsDescendantOf(workspace) for things in the service folders, and where LocalScripts run.
//
//   RobloxChatBridgeTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scripting/Luau.h"

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
bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: RobloxChatBridgeTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // --- The Luau rewrite ---
    {
        std::string a = Luau::toLua("for _, p in Players:GetPlayers() do print(p) end");
        check(has(a, "in __gb_iter(Players:GetPlayers()) do"), "a generalized for loop gets __gb_iter: " + a);
        std::string b = Luau::toLua("for k, v in pairs(t) do end\nfor i = 1, 3 do end\nfor k, v in next, t do end");
        check(!has(b, "__gb_iter"), "pairs(), counting loops and next, t stay as they are: " + b);
        std::string c = Luau::toLua("local color: Color3 = if typeof(x) == \"Color3\" then x else Color3.new(1, 1, 1)\nlocal y = 2");
        check(has(c, "(function() if typeof(x) == \"Color3\" then return x else return Color3.new(1, 1, 1) end end)()"),
              "an if-expression becomes a function call: " + c);
        check(has(c, "\nlocal y = 2"), "...and the next line is left alone");
        std::string d = Luau::toLua("if a then\n  b()\nelse\n  c()\nend");
        check(d == "if a then\n  b()\nelse\n  c()\nend", "an if statement isn't touched: " + d);
        std::string e = Luau::toLua("f(if a then 1 elseif b then 2 else 3, 4)");
        check(has(e, "f((function() if a then return 1 elseif b then return 2 else return 3 end end)(), 4)"),
              "elseif chains and a comma after: " + e);
    }

    Scene scene;
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    scene.info().publishedId = "";
    // A module in ServerScriptService that keeps a LocalScript inside it as a template for players.
    SceneNode* sss = scene.serviceFolder("ServerScriptService", true);
    auto module = std::make_unique<SceneNode>("ChatBridge", NodeKind::Script);
    module->isModule = true;
    module->source = R"(
local M = {}
local tries: { number } = {}
function M.Start(opts: { Key: string }?): boolean
    for _, t in tries do error("not empty") end
    table.insert(tries, os.clock())
    table.clear(tries)
    assert(#tries == 0)
    local where = if script:IsDescendantOf(workspace) then "workspace" else "safe"
    return where == "safe"
end
return M
)";
    auto client = std::make_unique<SceneNode>("ChatBridgeClient", NodeKind::Script);
    client->isLocal = true;
    client->source = "_G.templateRan = true";
    module->addChild(std::move(client));
    sss->addChild(std::move(module));

    GameSession session(&scene);
    session.setRunOnly(true);
    session.scripts().setPlayerName("Tester");
    session.scripts().setOnlineData(false, "4321");
    session.start();

    test(session, "a LocalScript kept in ServerScriptService doesn't run", R"(
        assert(_G.templateRan == nil, "the template ran")
    )");
    test(session, "a Luau module with typed loops and if-expressions loads and runs", R"(
        local m = require(game:GetService("ServerScriptService").ChatBridge)
        assert(m.Start({ Key = "x" }) == true, "the module thinks it's in the Workspace")
    )");
    test(session, "things in the service folders aren't in the Workspace", R"(
        local sss = game:GetService("ServerScriptService")
        assert(not sss.ChatBridge:IsDescendantOf(workspace))
        assert(sss.ChatBridge:IsDescendantOf(sss))
        local p = Instance.new("Part") p.Parent = workspace
        assert(p:IsDescendantOf(workspace))
        p:Destroy()
    )");
    test(session, "RunService knows it's the server, not Studio", R"(
        local RunService = game:GetService("RunService")
        assert(RunService:IsServer() == true)
        assert(RunService:IsStudio() == false)
        assert(type(RunService:IsClient()) == "boolean")
    )");
    test(session, "game.JobId is this server's own ID, the same every time", R"(
        assert(type(game.JobId) == "string" and #game.JobId == 36, "JobId " .. tostring(game.JobId))
        assert(game.JobId == game.JobId)
    )");
    test(session, "TextService:FilterStringAsync filters like the chat", R"(
        local TextService = game:GetService("TextService")
        local r = TextService:FilterStringAsync("hello there", 1, Enum.TextFilterContext.PublicChat)
        assert(r:GetNonChatStringForBroadcastAsync() == "hello there")
        assert(r:GetChatForUserAsync(2) == "hello there")
        assert(r:GetNonChatStringForUserAsync(2) == "hello there")
        assert(typeof(r) == "TextFilterResult")
    )");
    test(session, "TextChatService.ChatVersion says the older chat (so SetCore lines are used)", R"(
        local TextChatService = game:GetService("TextChatService")
        assert(TextChatService.ChatVersion == Enum.ChatVersion.LegacyChatService)
        assert(TextChatService.ChatVersion ~= Enum.ChatVersion.TextChatService)
    )");
    session.scripts().takeSystemChat();
    session.scripts().takeLocalChat();
    test(session, "SetCore ChatMakeSystemMessage shows a line only here", R"(
        game:GetService("StarterGui"):SetCore("ChatMakeSystemMessage", { Text = "[Lyreto] kermit: hi", Color = Color3.new(1, 0, 1) })
    )");
    {
        auto mine = session.scripts().takeLocalChat();
        check(mine.size() == 1 && mine[0] == "[Lyreto] kermit: hi", "the line is for this computer");
        check(session.scripts().takeSystemChat().empty(), "...and isn't sent to everyone");
    }
    test(session, "StarterPlayer has its StarterPlayerScripts", R"(
        local sps = game:GetService("StarterPlayer"):FindFirstChildOfClass("StarterPlayerScripts")
        assert(sps ~= nil and sps.Name == "StarterPlayerScripts")
    )");
    test(session, "a LocalScript put in StarterPlayerScripts waits for the next player", R"(
        local copy = game:GetService("ServerScriptService").ChatBridge.ChatBridgeClient:Clone()
        copy.Parent = game:GetService("StarterPlayer"):FindFirstChildOfClass("StarterPlayerScripts")
        task.wait()
        assert(_G.templateRan == nil, "it ran for players already here")
    )");
    test(session, "typeof names a Secret", R"(
        assert(typeof(game:GetService("HttpService"):GetSecret("LyretoKey")) == "Secret")
    )");
    test(session, "a table without __call in a rewritten loop walks like pairs", R"(
        local seen = 0
        for _, v in __gb_iter({ 1, 2, 3 }) do seen = seen + v end
        assert(seen == 6)
        local n = 0
        for _ in __gb_iter(pairs({ a = 1, b = 2 })) do n = n + 1 end
        assert(n == 2)
    )");

    session.stop();
    session.scripts().setInStudio(true);
    session.start();
    test(session, "in a Studio test: IsStudio, and JobId is empty like Roblox Studio's", R"(
        assert(game:GetService("RunService"):IsStudio() == true)
        assert(game.JobId == "")
    )");
    session.stop();

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
