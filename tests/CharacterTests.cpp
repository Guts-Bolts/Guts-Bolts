// The character: held down as a ragdoll (PlatformStand / the Ragdoll state) until let go,
// knocked off seats, and swimming lying along the way it goes.
//
//   CharacterTests <a game file with a baseplate and a player>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

void play(GameSession& session, float seconds) {
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

// How upright a body part is (1 = standing, 0 = lying flat).
float upright(const SceneNode* n) { return n ? std::abs(glm::normalize(glm::vec3(n->worldMatrix()[1])).y) : -1.0f; }

SceneNode* addPart(GameSession& session, Scene& scene, const std::string& name, const glm::vec3& at, const glm::vec3& size, bool collide) {
    // (Made by a script, like a game would, so the physics knows about it.)
    char code[512];
    std::snprintf(code, sizeof(code),
                  "local p = Instance.new('Part') p.Name = '%s' p.Anchored = true p.CanCollide = %s "
                  "p.Size = Vector3.new(%g, %g, %g) p.Position = Vector3.new(%g, %g, %g) p.Parent = workspace",
                  name.c_str(), collide ? "true" : "false", size.x, size.y, size.z, at.x, at.y, at.z);
    std::string err;
    session.scripts().runCommand(code, err);
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == name && n->isPart()) found = n; });
    return found;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: CharacterTests <game file>\n"; return 2; }
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
    Player* p = scene.player();
    if (!p || !p->root()) { std::cerr << "no player in that game\n"; return 1; }
    // A quiet floor of our own, away from the game's obstacles.
    addPart(session, scene, "Floor", glm::vec3(500.0f, 99.0f, 500.0f), glm::vec3(120.0f, 2.0f, 120.0f), true);
    p->root()->transform.position = glm::vec3(500.0f, 100.2f, 500.0f);
    play(session, 1.5f);
    check(upright(p->root()->findChild("Torso")) > 0.9f, "the character starts standing");

    // PlatformStand: a floppy ragdoll that stays down until it's turned off.
    p->humanoid().platformStand = true;
    play(session, 10.0f);
    check(std::string(p->stateName()) == "PlatformStanding", std::string("the state is PlatformStanding (") + p->stateName() + ")");
    check(upright(p->root()->findChild("Torso")) < 0.5f, "PlatformStand: the body lies down");
    p->humanoid().platformStand = false;
    play(session, 2.0f);
    check(upright(p->root()->findChild("Torso")) > 0.9f, "PlatformStand off: back up");

    // ChangeState(Ragdoll) the same, until GettingUp.
    p->setHeldState("Ragdoll");
    play(session, 9.0f);
    check(std::string(p->stateName()) == "Ragdoll" && upright(p->root()->findChild("Torso")) < 0.5f, "the Ragdoll state holds the body down");
    p->setHeldState("");
    play(session, 2.0f);
    check(upright(p->root()->findChild("Torso")) > 0.9f && !p->tripped(), "GettingUp: back up");

    // Seats: a blast knocks you off.
    const glm::vec3 feet = p->root()->transform.position;
    SceneNode* seat = addPart(session, scene, "Seat", feet + glm::vec3(0.0f, 0.5f, 3.0f), glm::vec3(2.0f, 1.0f, 2.0f), true);
    p->sit(seat);
    play(session, 0.5f);
    check(p->sitting(), "sitting on a seat");
    p->hurt(5.0f, 0.0f, glm::vec3(12.0f, 4.0f, 0.0f));
    play(session, 0.1f);
    check(!p->sitting(), "a blast knocks you off the seat");
    play(session, 4.0f);
    // A seat tipping right over throws you out.
    p->sit(seat);
    play(session, 0.3f);
    seat->transform.rotation.z = 120.0f;
    play(session, 0.2f);
    check(!p->sitting(), "a seat tipped over throws you out");
    scene.removeNode(seat);
    play(session, 8.0f);
    check(!p->isDead() && !p->tripped(), "back up after all that");

    // Swimming: flat out swimming along, upright treading water.
    const glm::vec3 at(500.0f, 100.0f, 500.0f);
    addPart(session, scene, "Water", at + glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(200.0f, 12.0f, 200.0f), false);
    p->scriptMove(glm::vec3(0.0f, 0.0f, 1.0f));
    play(session, 5.0f);
    check(p->swimming(), "swimming in the water");
    const float along = p->root()->transform.rotation.x;
    check(along > 45.0f, "swimming along: the body lies flat (" + std::to_string(along) + " degrees)");
    p->scriptMove(glm::vec3(0.0f));
    play(session, 3.0f);
    const float still = p->root()->transform.rotation.x;
    check(std::abs(still) < 20.0f, "treading water: upright again (" + std::to_string(still) + " degrees)");

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "character tests FAILED\n" : "character tests passed\n");
    return failures ? 1 : 0;
}
