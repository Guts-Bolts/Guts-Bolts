// DragDetectors: grab a part with the pointer and drag it across the floor, along a line
// (with limits) or spin it round; scripts hear DragStart / DragContinue / DragEnd; a loose
// part can be thrown; too far away, nothing happens (game/DragDetectors.cpp).
//
//   DragDetectorTests <a game file with a player>
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

void frame(GameSession& session) {
    ImGui::NewFrame();
    session.update(1.0f / 60.0f, 0.0f, false);
    ImGui::EndFrame();
}

SceneNode* find(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == name && n->isPart()) found = n; });
    return found;
}
glm::vec3 pos(const SceneNode* n) { return glm::vec3(n->worldMatrix()[3]); }
float numAttr(const SceneNode* n, const char* name) {
    const Attribute* a = n ? n->findAttribute(name) : nullptr;
    return a && a->type == Attribute::Number ? (float)a->n : -999.0f;
}

// A camera above and behind `from`, looking at `at`: the ray through that point.
void rayTo(const glm::vec3& at, glm::vec3& ro, glm::vec3& rd) {
    ro = glm::vec3(at.x, at.y + 12.0f, at.z + 14.0f);
    rd = glm::normalize(at - ro);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: DragDetectorTests <game file>\n"; return 2; }
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
    for (int i = 0; i < 60; ++i) frame(session);
    const glm::vec3 c = me->position();
    const glm::vec3 look(0.0f, -0.6f, -0.8f);

    // A crate next to the player with a DragDetector, and a script counting its events.
    std::string code = R"(
        local c = ...
        local crate = Instance.new("Part") crate.Name = "Crate" crate.Anchored = true crate.Size = Vector3.new(2, 2, 2)
        crate.Position = c + Vector3.new(0, 1, -6) crate.Parent = workspace
        local d = Instance.new("DragDetector") d.Parent = crate
        crate:SetAttribute("Starts", 0) crate:SetAttribute("Moves", 0) crate:SetAttribute("Ends", 0)
        d.DragStart:Connect(function(player) crate:SetAttribute("Starts", crate:GetAttribute("Starts") + 1) crate:SetAttribute("Who", player.Name) end)
        d.DragContinue:Connect(function() crate:SetAttribute("Moves", crate:GetAttribute("Moves") + 1) end)
        d.DragEnd:Connect(function() crate:SetAttribute("Ends", crate:GetAttribute("Ends") + 1) end)
        print(d.ClassName, d:IsA("ClickDetector"), d.DragStyle, d.Axis.Y, d.Enabled)
    )";
    char where[96];
    std::snprintf(where, sizeof(where), "local c = Vector3.new(%f, %f, %f)\n", c.x, c.y - 2.5f, c.z);
    code.replace(code.find("local c = ..."), 13, where);
    std::string out = run(session, code);
    check(out == "DragDetector true TranslatePlane 1.0 true",
          "Instance.new(\"DragDetector\") and its properties: " + out);
    SceneNode* crate = find(scene, "Crate");
    if (!crate) { std::cerr << "no crate\n"; return 1; }

    // --- TranslatePlane: across the floor ---
    const glm::vec3 start = pos(crate), grab = start + glm::vec3(0, 1, 0);
    check(session.dragBegin(crate->id, grab, look), "pressing on it takes hold");
    glm::vec3 ro, rd;
    for (int i = 1; i <= 20; ++i) {
        rayTo(grab + glm::vec3(0.2f * i, 0, 0.1f * i), ro, rd);
        session.dragMove(ro, rd);
        frame(session);
    }
    const glm::vec3 moved = pos(crate) - start;
    check(std::fabs(moved.x - 4.0f) < 0.05f && std::fabs(moved.z - 2.0f) < 0.05f && std::fabs(moved.y) < 0.01f,
          "it slides across the floor with the pointer (" + std::to_string(moved.x) + ", " + std::to_string(moved.y) + ", " +
              std::to_string(moved.z) + ")");
    session.dragEnd();
    frame(session);
    check(numAttr(crate, "Starts") == 1.0f && numAttr(crate, "Moves") >= 20.0f && numAttr(crate, "Ends") == 1.0f,
          "DragStart, DragContinue and DragEnd fire");
    const Attribute* who = crate->findAttribute("Who");
    check(who && !who->s.empty(), "with the player who dragged it");

    // --- TranslateLine: up only, at most 3 studs ---
    run(session, R"(
        local d = workspace.Crate.DragDetector
        d.DragStyle = Enum.DragDetectorDragStyle.TranslateLine
        d.MinDragTranslation = 0
        d.MaxDragTranslation = 3
    )");
    const glm::vec3 lineStart = pos(crate), lineGrab = lineStart + glm::vec3(0, 0, 1);
    session.dragBegin(crate->id, lineGrab, look);
    ro = lineGrab + glm::vec3(2, 6, 14);
    rd = glm::normalize(lineGrab + glm::vec3(5, 10, 0) - ro);   // (well up and to the side)
    session.dragMove(ro, rd);
    session.dragEnd();
    const glm::vec3 up = pos(crate) - lineStart;
    check(std::fabs(up.x) < 0.01f && std::fabs(up.z) < 0.01f && std::fabs(up.y - 3.0f) < 0.05f,
          "TranslateLine: only up and down, stopping at MaxDragTranslation (" + std::to_string(up.y) + ")");

    // --- RotateAxis: spin round, at most 45 degrees ---
    run(session, R"(
        local d = workspace.Crate.DragDetector
        d.DragStyle = Enum.DragDetectorDragStyle.RotateAxis
        d.MaxDragAngle = 45
    )");
    const glm::vec3 mid = pos(crate);
    const glm::vec3 front0 = -glm::normalize(glm::vec3(crate->worldMatrix()[2]));
    session.dragBegin(crate->id, mid + glm::vec3(1, 1, 0), look);
    // Swing the pointer round a quarter turn (from +X towards -Z, seen from above).
    rayTo(mid + glm::vec3(0, 1, -1), ro, rd);
    ro = mid + glm::vec3(0, 10, 0);
    rd = glm::normalize(mid + glm::vec3(0, 1, -1) - ro);
    session.dragMove(ro, rd);
    session.dragEnd();
    const glm::vec3 front1 = -glm::normalize(glm::vec3(crate->worldMatrix()[2]));
    const float turned = glm::degrees(std::acos(std::clamp(glm::dot(front0, front1), -1.0f, 1.0f)));
    check(std::fabs(turned - 45.0f) < 1.0f && glm::length(pos(crate) - mid) < 0.01f,
          "RotateAxis: it spins in place, stopping at MaxDragAngle (" + std::to_string(turned) + " degrees)");

    // --- A loose ball can be thrown ---
    std::snprintf(where, sizeof(where), "local c = Vector3.new(%f, %f, %f)\n", c.x, c.y, c.z);
    code = std::string(where) + R"(
        local ball = Instance.new("Part") ball.Name = "Ball" ball.Shape = Enum.PartType.Ball ball.Size = Vector3.new(1, 1, 1)
        ball.Position = c + Vector3.new(3, 1, -4) ball.Parent = workspace
        local d = Instance.new("DragDetector") d.DragStyle = Enum.DragDetectorDragStyle.TranslateViewPlane d.Parent = ball
    )";
    run(session, code);
    SceneNode* ball = find(scene, "Ball");
    const glm::vec3 b0 = pos(ball);
    check(session.dragBegin(ball->id, b0, look), "a loose ball can be grabbed");
    for (int i = 1; i <= 10; ++i) {
        const glm::vec3 aim = b0 + glm::vec3(0, 0.25f * i, 0);
        ro = aim - look * 10.0f;
        session.dragMove(ro, look);
        frame(session);
    }
    check(pos(ball).y > b0.y + 1.0f, "a loose part is carried (it doesn't fall while held)");
    session.dragEnd();
    check(ball->velocity.y > 3.0f, "and keeps its speed when let go (thrown up at " + std::to_string(ball->velocity.y) + ")");

    // --- Too far away: no drag ---
    run(session, R"(workspace.Crate.Position = workspace.Crate.Position + Vector3.new(200, 0, 0))");
    check(!session.dragBegin(crate->id, pos(crate), look), "too far away to reach: it can't be grabbed");
    run(session, R"(workspace.Crate.Position = workspace.Crate.Position - Vector3.new(200, 0, 0) workspace.Crate.DragDetector.Enabled = false)");
    check(!session.dragBegin(crate->id, pos(crate), look), "Enabled = false: it can't be grabbed");

    // --- Saved with the game ---
    SceneNode* det = crate->findChild("DragDetector");
    auto copy = det ? Serializer::nodeFromString(Serializer::nodeToString(*det), false) : nullptr;
    check(copy && copy->click.drag && copy->click.dragStyle == 3 && copy->click.maxAngle == 45.0f && !copy->enabled,
          "it's saved with its settings");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
