// The "Lights, Sirens, Labels" scripting additions: RichText's markup, private PlayerGuis,
// team:GetPlayers(), BrickColor, typeof, tree events and the respawn settings.
// (tools/test_cops_and_robbers_api.lua checks the same in a real play test, with waiting.)
//
//   CopsAndRobbersApiTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "game/Hud.h"
#include "game/RichText.h"
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

// Run some Lua and give back what it printed ("ERROR ..." if it failed).
std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    const bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    std::string out = ok ? "" : "ERROR " + err;
    for (const Log::Entry& e : lines) {
        if (e.level == Log::Level::Error) out += "ERROR " + e.text;
        else if (e.text.rfind("> ", 0) != 0) out += e.text;   // (not the echo of the command)
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: CopsAndRobbersApiTests <game file>\n"; return 2; }

    // --- RichText markup ---
    {
        auto runs = RichText::parse("You are a <b>Cop</b>!");
        check(runs.size() == 3 && runs[1].text == "Cop" && runs[1].style.bold && !runs[0].style.bold, "<b> makes a bold piece");
        runs = RichText::parse("<font color=\"#ff0000\" size=\"30\">Wanted</font>");
        check(runs.size() == 1 && runs[0].style.hasColor && runs[0].style.color.r > 0.99f && runs[0].style.color.g < 0.01f &&
              runs[0].style.size == 30.0f, "<font color size>");
        runs = RichText::parse("<font color=\"rgb(0,255,0)\">go</font>");
        check(runs.size() == 1 && runs[0].style.color.g > 0.99f, "rgb() colours");
        check(RichText::strip("a<br/>b") == "a\nb", "<br/> is a new line");
        check(RichText::strip("5 &lt; 6 &amp; 7") == "5 < 6 & 7", "&lt; and &amp;");
        check(RichText::strip("<u>under</u> <s>out</s> <i>it</i> <uppercase>up</uppercase>") == "under out it UP", "u, s, i, uppercase");
        check(RichText::strip("<!-- hidden -->shown <bogus>x</bogus>") == "shown x", "comments and unknown tags are left out");
        check(RichText::strip("no closing <b") == "no closing <b", "a broken tag stays as text");
        runs = RichText::parse("<b>a<font color=\"#0000ff\">b</font>c</b>");
        check(runs.size() == 3 && runs[1].style.bold && runs[1].style.hasColor && runs[2].style.bold && !runs[2].style.hasColor,
              "tags inside tags");
    }

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
    session.start();

    std::string out = run(session, R"(
        local me = game:GetService("Players").LocalPlayer
        local teams = Instance.new("Folder") teams.Name = "Teams" teams.Parent = workspace
        local cops = Instance.new("Team") cops.Name = "Cops" cops.TeamColor = BrickColor.new("Bright blue") cops.Parent = teams
        me.Team = cops
        print(#cops:GetPlayers(), cops:GetPlayers()[1] == me, tostring(BrickColor.new("Bright blue")))
    )");
    check(out.find("1 true Bright blue") != std::string::npos, "Instance.new(\"Team\"), BrickColor and team:GetPlayers() (" + out + ")");

    out = run(session, R"(print(typeof(Vector3.new()), typeof(Color3.new()), typeof(workspace), typeof(1), typeof(BrickColor.Red())))");
    check(out.find("Vector3 Color3 Instance number BrickColor") != std::string::npos, "typeof (" + out + ")");

    out = run(session, R"(
        local f = Instance.new("Folder") f.Parent = workspace
        local got = {}
        f.ChildAdded:Connect(function(c) got[#got + 1] = "added " .. c.Name end)
        f.ChildRemoved:Connect(function(c) got[#got + 1] = "removed " .. c.Name end)
        local p = Instance.new("Part") p.Name = "Coin"
        p.Destroying:Connect(function() got[#got + 1] = "destroying" end)
        p.Parent = f
        p:Destroy()
        print(table.concat(got, ","))
    )");
    check(out.find("added Coin,destroying,removed Coin") != std::string::npos, "ChildAdded, Destroying, ChildRemoved (" + out + ")");

    out = run(session, R"(
        local me = game:GetService("Players").LocalPlayer
        local pg = me.PlayerGui
        local s = Instance.new("ScreenGui") s.Name = "Everyone" s.Parent = game:GetService("StarterGui")
        local mine = Instance.new("ScreenGui") mine.Name = "Mine" mine.Parent = pg
        print(pg.ClassName, pg ~= game:GetService("StarterGui"), pg:FindFirstChild("Everyone") == s, pg.Mine == mine,
              game:GetService("StarterGui"):FindFirstChild("Mine") == nil, #pg:GetChildren() >= 2)
    )");
    check(out.find("PlayerGui true true true true true") != std::string::npos, "a private PlayerGui that still finds StarterGui's things (" + out + ")");
    check(session.scripts().localPlayerGui() && session.scripts().localPlayerGui()->findChild("Mine"), "our PlayerGui is what our screen shows");

    out = run(session, R"(
        local P = game:GetService("Players")
        print(P.RespawnTime, P.CharacterAutoLoads)
        P.RespawnTime = 1.5 P.CharacterAutoLoads = false
        print(P.RespawnTime, P.CharacterAutoLoads, P:GetPlayerByUserId(P.LocalPlayer.UserId) == P.LocalPlayer)
    )");
    check(out.find("4 true") != std::string::npos && out.find("1.5 false true") != std::string::npos, "RespawnTime / CharacterAutoLoads (" + out + ")");
    check(session.scripts().respawnTime() == 1.5f && !session.scripts().autoLoads(), "the engine gets the respawn settings");

    out = run(session, R"(local l = Instance.new("TextLabel") l.RichText = true l.Text = "<b>Hi</b> there" print(l.ContentText, l.RichText))");
    check(out.find("Hi there true") != std::string::npos, "TextLabel.RichText and ContentText (" + out + ")");

    // The player list: our team (from the Team test above) and the game's own look.
    {
        PlayerEntry e;
        session.scripts().fillTeam(e, session.scripts().playerName());
        check(e.team == "Cops" && e.teamColor[2] > 0.5f && e.teamColor[0] < 0.3f, "the player list knows our team and its colour (" + e.team + ")");
        std::vector<PlayerEntry> list(4);
        list[0].name = "a"; list[0].team = "Robbers"; list[0].stats = {{"Cash", "5"}};
        list[1].name = "b"; list[1].team = "Cops";    list[1].stats = {{"Cash", "9"}};
        list[2].name = "c";                           list[2].stats = {{"Cash", "50"}};
        list[3].name = "d"; list[3].team = "Robbers"; list[3].stats = {{"Cash", "20"}};
        auto g = Hud::groupPlayers(list, true);
        check(g.size() == 3 && g[0].team == "Robbers" && g[1].team == "Cops" && g[2].team.empty(), "teams in order, Neutral last");
        check(g.size() == 3 && g[0].rows.size() == 2 && g[0].rows[0] == 3 && g[0].rows[1] == 0, "the most Cash first within a team");
        auto flat = Hud::groupPlayers(list, false);
        check(flat.size() == 1 && flat[0].rows[0] == 2, "ShowTeams off: one list, best first");
    }
    out = run(session, R"(
        local sg = game:GetService("StarterGui")
        sg:SetCore("PlayerListStyle", { Title = "Cops & Robbers", BackgroundColor = Color3.new(0, 0, 0.5), ShowTeams = false })
        local st = sg:GetCore("PlayerListStyle")
        print(st.Title, st.ShowTeams, st.Visible, pcall(function() sg:SetCore("PlayerListStyle", { Colour = 1 }) end))
        sg:SetCoreGuiEnabled(Enum.CoreGuiType.PlayerList, false)
        print("listshown", sg:GetCoreGuiEnabled(Enum.CoreGuiType.PlayerList))
    )");
    check(out.find("Cops & Robbers false true false") != std::string::npos && out.find("listshown false") != std::string::npos,
          "SetCore(\"PlayerListStyle\") / GetCore and hiding the list (" + out + ")");
    check(scene.world().playerList.title == "Cops & Robbers" && !scene.world().playerList.teams &&
          scene.world().playerList.background.b == 0.5f, "the style reaches the engine");
    {
        Scene saved;
        Serializer::loadScene(saved, Serializer::saveScene(scene));
        check(saved.world().playerList == scene.world().playerList, "the player list's look is saved with the place");
    }

    out = run(session, R"(game:GetService("Players").LocalPlayer:Kick("Busted"))");
    std::string why;
    check(session.scripts().takeKick(why) && why == "Busted", "player:Kick on ourselves ends the game with the reason");

    session.stop();
    std::cout << (failures ? "SOME CHECKS FAILED\n" : "all good\n");
    return failures ? 1 : 0;
}
