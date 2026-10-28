// Things that tripped up a tester writing scripts: the autocomplete's "how to use it"
// help names real functions (with the right upper / lower case), a name typed with the
// wrong case says which one was meant, and ContextActionService hears mouse buttons.
//
//   ScriptingHelpTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scripting/ScriptEngine.h"
#include "scripting/ScriptHelp.h"

#include <imgui.h>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Run some Lua; everything it logged comes back joined up (errors start with "ERROR ").
std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    if (!session.scripts().runCommand(code, err)) lines.push_back({Log::Level::Error, err});
    Log::setCapture(nullptr);
    std::string out;
    for (const Log::Entry& e : lines)
        out += (e.level == Log::Level::Error ? "ERROR " : e.level == Log::Level::Warn ? "WARN " : "") + e.text + "\n";
    return out;
}

void frames(GameSession& session, int n) {
    for (int i = 0; i < n; ++i) {
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: ScriptingHelpTests <game file>\n"; return 2; }
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

    // --- The help: every name you can type on its own is really there, spelled like that ---
    int missing = 0;
    for (const ScriptHelp::Entry& e : ScriptHelp::all()) {
        if (!e.global || std::string(e.name) == "script") continue;   // (a command has no script)
        const std::string out = run(session, std::string("if ") + e.name + " == nil then error('missing') end");
        if (!out.empty()) { std::cout << "      " << e.name << ": " << out; ++missing; }
    }
    check(missing == 0, "every function the autocomplete explains exists with that exact name");
    int noUsage = 0;
    for (const ScriptHelp::Entry& e : ScriptHelp::all())
        if (!*e.usage || !*e.about) ++noUsage;
    check(noUsage == 0, "every help entry says how to use it and what it does");
    const ScriptHelp::Entry* m3 = ScriptHelp::find("getmouse3d");
    check(m3 && std::string(m3->usage).find("-> Vector3, BasePart?, Vector3") != std::string::npos,
          "getmouse3d's help shows what it gives back");
    const ScriptHelp::Entry* ray = ScriptHelp::find("workspace:Raycast");
    check(ray && std::string(ray->name) == "Raycast", "a method's help is found from obj:Method");
    check(ScriptHelp::rightCase("getMouse3D") == "getmouse3d", "getMouse3D is spotted as getmouse3d with the wrong case");
    check(ScriptHelp::rightCase("Task.Wait") == "task.wait", "Task.Wait is spotted as task.wait");
    check(ScriptHelp::rightCase("getmouse3d").empty() && ScriptHelp::rightCase("nothingLikeThis").empty(),
          "right names and unknown names aren't 'fixed'");

    // --- A wrong-case name in a script says which one was meant ---
    std::string out = run(session, "getMouse3D()");
    check(out.find("did you mean getmouse3d?") != std::string::npos, "calling getMouse3D() says: did you mean getmouse3d?");
    out = run(session, "nothingLikeThis()");
    check(out.find("ERROR") != std::string::npos && out.find("did you mean") == std::string::npos,
          "an unknown name doesn't get a made-up suggestion");

    // --- ContextActionService hears mouse buttons ---
    out = run(session, R"(
        _G.heard = {}
        local function rec(name, state, input)
            table.insert(_G.heard, name .. ":" .. state .. ":" .. tostring(input.UserInputType))
            return Enum.ContextActionResult.Sink
        end
        local CAS = game:GetService("ContextActionService")
        CAS:BindAction("Aim", rec, false, Enum.UserInputType.MouseButton2)
        CAS:BindAction("Wheel", rec, false, Enum.UserInputType.MouseButton3)
        CAS:BindAction("Shout", rec, false, Enum.UserInputType.MouseButton1Click)
    )");
    check(out.find("WARN") == std::string::npos && out.find("ERROR") == std::string::npos,
          "binding MouseButton2, MouseButton3 and MouseButton1Click gives no warning");
    check(out.find("GUI button's event") != std::string::npos, "MouseButton1Click explains it means MouseButton1");
    ScriptEngine& s = session.scripts();
    const bool sunk2 = s.mouseButton(2, true, glm::vec2(10.0f, 10.0f));
    s.mouseButton(2, false, glm::vec2(10.0f, 10.0f));
    s.mouseButton(3, true, glm::vec2(10.0f, 10.0f));
    s.mouseButton(3, false, glm::vec2(10.0f, 10.0f));
    s.pointerDown(true, glm::vec2(10.0f, 10.0f));
    s.pointerDown(false, glm::vec2(10.0f, 10.0f));
    frames(session, 2);
    out = run(session, "print(table.concat(_G.heard, ' '))");
    check(sunk2, "an action that sinks MouseButton2 says so");
    check(out.find("Aim:Begin:MouseButton2 Aim:End:MouseButton2") != std::string::npos, "the right button: Begin then End");
    check(out.find("Wheel:Begin:MouseButton3 Wheel:End:MouseButton3") != std::string::npos, "the middle button: Begin then End");
    check(out.find("Shout:Begin:MouseButton1 Shout:End:MouseButton1") != std::string::npos,
          "a MouseButton1Click binding hears the left button");
    if (failures) std::cout << "      heard: " << out;

    // A right button held down ends when it's let go (noticed each frame).
    run(session, "_G.heard = {}");
    s.mouseButton(2, true, glm::vec2(0.0f));
    frames(session, 2);   // (ImGui's right button isn't down here)
    out = run(session, "print(table.concat(_G.heard, ' '))");
    check(out.find("Aim:Begin:MouseButton2 Aim:End:MouseButton2") != std::string::npos, "letting go of the right button ends it");

    // UserInputService and the old Mouse hear the right button too.
    out = run(session, R"(
        game:GetService("ContextActionService"):UnbindAllActions()
        _G.uis, _G.b2 = nil, false
        game:GetService("UserInputService").InputBegan:Connect(function(input) _G.uis = tostring(input.UserInputType) end)
    )");
    s.mouseButton(2, true, glm::vec2(0.0f));
    s.mouseButton(2, false, glm::vec2(0.0f));
    frames(session, 2);
    out = run(session, "print(_G.uis)");
    check(out.find("MouseButton2") != std::string::npos, "UserInputService.InputBegan hears MouseButton2");

    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
