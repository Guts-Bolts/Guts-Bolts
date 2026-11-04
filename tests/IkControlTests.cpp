// IKControl on the normal character: an arm reaches for a part, the head looks at it,
// Weight blends it in, and it's saved like anything else (scene/IkControls.cpp).
//
//   IkControlTests <a game file with a player>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    const bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    std::string out = ok ? "" : "ERROR " + err;
    for (const Log::Entry& e : lines) {
        if (e.level == Log::Level::Error) out += "ERROR " + e.text;
        else if (e.text.rfind("> ", 0) != 0) out += e.text;
    }
    return out;
}

void play(GameSession& session, float seconds) {
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

glm::vec3 at(const SceneNode* n, glm::vec3 local) { return glm::vec3(n->worldMatrix() * glm::vec4(local, 1.0f)); }

// Degrees between where a limb points (shoulder -> hand) and the way to a spot.
float armMiss(const SceneNode* arm, const glm::vec3& spot) {
    const glm::vec3 shoulder = at(arm, {0, 0.5f, 0}), hand = at(arm, {0, -0.5f, 0});
    const float c = glm::dot(glm::normalize(hand - shoulder), glm::normalize(spot - shoulder));
    return glm::degrees(std::acos(std::clamp(c, -1.0f, 1.0f)));
}
float headMiss(const SceneNode* head, const glm::vec3& spot) {
    const glm::vec3 front = -glm::normalize(glm::vec3(head->worldMatrix()[2]));
    const float c = glm::dot(front, glm::normalize(spot - glm::vec3(head->worldMatrix()[3])));
    return glm::degrees(std::acos(std::clamp(c, -1.0f, 1.0f)));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: IkControlTests <game file>\n"; return 2; }
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
    if (!me || !me->root()) { std::cerr << "no player\n"; return 1; }
    play(session, 1.0f);   // (land on the ground)
    SceneNode* arm = me->root()->findChild("Right Arm");
    SceneNode* head = me->root()->findChild("Head");
    if (!arm || !head) { std::cerr << "no arm / head\n"; return 1; }

    // A ball up and to the side of the character.
    const glm::vec3 c(me->root()->worldMatrix()[3]);
    std::string out = run(session, R"(
        local char = game:GetService("Players").LocalPlayer.Character
        local ball = Instance.new("Part") ball.Name = "Ball" ball.Anchored = true ball.CanCollide = false
        ball.Size = Vector3.new(1, 1, 1) ball.Position = char.HumanoidRootPart.Position + Vector3.new(6, 6, -4)
        ball.Parent = workspace
        local ik = Instance.new("IKControl")
        ik.Name = "Reach"
        ik.Type = Enum.IKControlType.Position
        ik.EndEffector = char["Right Arm"]
        ik.ChainRoot = char["Right Arm"]
        ik.Target = ball
        ik.Parent = char.Humanoid
        print(ik.ClassName, ik.Type, ik.Target.Name, ik.Weight, ik.Parent == char)
    )");
    check(out == "IKControl Position Ball 1.0 true", "Instance.new(\"IKControl\") and its properties: " + out);
    SceneNode* ball = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == "Ball") ball = n; });
    if (!ball) { std::cerr << "no ball\n"; return 1; }
    const glm::vec3 spot(ball->worldMatrix()[3]);

    const float before = armMiss(arm, spot);
    play(session, 0.5f);
    const float after = armMiss(arm, spot);
    check(before > 30.0f && after < 3.0f, "the right arm reaches for the ball (" + std::to_string(before) + " -> " + std::to_string(after) + " degrees off)");
    const glm::vec3 shoulder = at(arm, {0, 0.5f, 0});
    play(session, 0.2f);
    check(glm::length(at(arm, {0, 0.5f, 0}) - shoulder) < 0.05f, "it turns at the shoulder (the shoulder stays put)");

    run(session, R"(game:GetService("Players").LocalPlayer.Character.Reach.Weight = 0.5)");
    play(session, 0.5f);
    const float half = armMiss(arm, spot);
    check(half > 10.0f && half < before - 10.0f, "Weight 0.5: halfway between the animation and the ball (" + std::to_string(half) + " degrees off)");

    run(session, R"(game:GetService("Players").LocalPlayer.Character.Reach.Enabled = false)");
    play(session, 0.5f);
    check(armMiss(arm, spot) > 30.0f, "Enabled = false: back to the animation");

    // The head looks at the ball.
    out = run(session, R"(
        local char = game:GetService("Players").LocalPlayer.Character
        local ik = Instance.new("IKControl")
        ik.Type = Enum.IKControlType.LookAt
        ik.EndEffector = char.Head
        ik.ChainRoot = char.Head
        ik.Target = workspace.Ball
        ik.Parent = char
    )");
    const float lookBefore = headMiss(head, spot);
    play(session, 0.5f);
    const float lookAfter = headMiss(head, spot);
    check(out.empty() && lookAfter < 3.0f && lookBefore > 20.0f,
          "LookAt: the head turns to look at the ball (" + std::to_string(lookBefore) + " -> " + std::to_string(lookAfter) + " degrees off) " + out);

    out = run(session, R"(
        local ik = Instance.new("IKControl")
        local ok = pcall(function() ik.Type = "Sideways" end)
        print(ok)
    )");
    check(out == "false", "a Type that doesn't exist is an error");

    // Saved and loaded with the game.
    SceneNode* reach = me->root()->findChild("Reach");
    check(reach && reach->isIkControl(), "the IKControl is in the character");
    if (reach) {
        auto copy = Serializer::nodeFromString(Serializer::nodeToString(*reach), false);
        check(copy && copy->isIkControl() && copy->ik.type == IkType::Position && copy->ik.target == ball->id &&
                  copy->ik.weight == 0.5f && !copy->enabled,
              "it's saved with its settings");
    }

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
