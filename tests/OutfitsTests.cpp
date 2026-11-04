// Outfits from scripts (Roblox's HumanoidDescription): humanoid:GetAppliedDescription(),
// changing it and humanoid:ApplyDescription(desc) recolour and dress the character, and
// the look is remembered so the host can send it to everyone (scene/Outfits.cpp).
//
//   OutfitsTests <a game file with a baseplate>
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
    if (argc < 2) { std::cerr << "usage: OutfitsTests <game file>\n"; return 2; }
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
    Player* me = scene.player();
    check(me && me->root(), "there's a character");
    if (!me || !me->root()) return 1;

    std::string out = run(session, R"(
        local d = Instance.new("HumanoidDescription")
        print(d.ClassName, d:IsA("HumanoidDescription"), d.Shirt, typeof(d.TorsoColor))
    )");
    check(out == "HumanoidDescription true 0 Color3", "Instance.new(\"HumanoidDescription\"): " + out);

    out = run(session, R"(
        local hum = game:GetService("Players").LocalPlayer.Character.Humanoid
        local d = hum:GetAppliedDescription()
        d.TorsoColor = Color3.new(1, 0, 0)
        d.LeftLegColor = Color3.new(0, 0, 1)
        d.Shirt = 123
        d.Pants = "gb:456"
        d.HatAccessory = "789,1011"
        hum:ApplyDescription(d)
        local now = hum:GetAppliedDescription()
        print(now.Shirt, now.Pants, now.HatAccessory, now.TorsoColor.R == 1, now.LeftLegColor.B == 1)
    )");
    check(out == "123 456 789 true true", "ApplyDescription then GetAppliedDescription: " + out);

    SceneNode* r = me->root();
    SceneNode* torso = r->findChild("Torso");
    SceneNode* arm = r->findChild("Left Arm");
    SceneNode* leg = r->findChild("Left Leg");
    check(torso && torso->color.r > 0.99f && torso->color.g < 0.01f, "the torso is red now");
    check(arm && arm->texture == "gb:123", "the shirt is on the arms");
    check(leg && leg->texture == "gb:456", "the pants are on the legs");
    check(scene.looks().count(r->id) && scene.looks()[r->id].version >= 1, "the look is remembered (for multiplayer)");
    check(scene.looks()[r->id].look["accessories"].value("hat", std::string()) == "gb:789", "the first hat in a list is worn");

    const int before = scene.looks()[r->id].version;
    out = run(session, R"(
        local hum = game:GetService("Players").LocalPlayer.Character.Humanoid
        local d = hum:GetAppliedDescription()
        d.Shirt = 0
        d.HatAccessory = ""
        hum:ApplyDescription(d)
        print(hum:GetAppliedDescription().Shirt)
    )");
    check(out == "0", "Shirt = 0 takes it off: " + out);
    check(arm && arm->texture.empty(), "the arms have no shirt");
    check(scene.looks()[r->id].version > before, "the look's version goes up (everyone is sent it again)");
    check(!scene.looks()[r->id].look["accessories"].contains("hat"), "HatAccessory = \"\" takes the hat off");

    out = run(session, R"(
        local hum = game:GetService("Players").LocalPlayer.Character.Humanoid
        local ok, e = pcall(function() hum:ApplyDescription({ Shirt = 5 }) end)
        print(ok)
        local d = hum:GetAppliedDescription()
        d.Face = "../../secret"
        hum:ApplyDescription(d)
        print(hum:GetAppliedDescription().Face)
    )");
    check(out == "false0", "only a real HumanoidDescription, and only real ids: " + out);

    // A rig that isn't anyone's character (an NPC): dressed the same way.
    SceneNode* npc = Player::buildRig(scene, "Dummy", glm::vec3(10, 0, 0));
    Player::dressRig(scene, npc, {{"torso", {0.0, 1.0, 0.0}}, {"shirtImage", "gb:42"}, {"faceImage", "C:/not/a/server/id.png"}});
    SceneNode* nt = npc->findChild("Torso");
    check(nt && nt->color.g > 0.99f && nt->texture == "gb:42", "dressRig dresses an NPC");
    check(Player::lookOf(scene, npc).value("faceImage", std::string("?")).empty(), "a file on someone's computer is never used");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
