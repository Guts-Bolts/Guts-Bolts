// Arterial bleeding (Artery.h) and dead bodies that lie still.
//  * Which hits open an artery: the inside of a thigh, the inside of an upper arm, the
//    neck; not the outside of a leg, the top of the head, or a blunt hit.
//  * A bullet through the inner thigh (Humanoid:TakeDamage with a hit result): the
//    blood spurts in pulses, the character staggers about (moves with nobody pushing
//    the keys), collapses and bleeds to death; the body then comes to rest and stays still.
//  * Humanoid:StopBleeding() closes it; workspace.ArterialBleeding = false turns it off;
//    CutArtery opens one from a script; a character's "Arteries" attribute = false never gets one.
//  * A dead ragdoll pushed out from deep inside a part isn't flung across the map.
//
//   ArteryTests <a game file with a baseplate and a player>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Artery.h"
#include "scene/Particles.h"
#include "scene/Physics.h"
#include "scene/Player.h"
#include "scene/Ragdoll.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

int drops(const Scene& scene) {
    int n = 0;
    for (const Particle& p : const_cast<Scene&>(scene).particles().items()) if (p.kind == Particle::Drop) ++n;
    return n;
}

glm::vec3 onPart(const SceneNode* part, const glm::vec3& local) {
    return glm::vec3(part->worldMatrix() * glm::vec4(local, 1.0f));
}

bool run(GameSession& session, const std::string& code) {
    std::string err;
    if (!session.scripts().runCommand(code, err)) { std::cout << "script error: " << err << "\n"; return false; }
    return true;
}

std::string vec(const glm::vec3& v) {
    char b[160];
    std::snprintf(b, sizeof(b), "Vector3.new(%.4f, %.4f, %.4f)", v.x, v.y, v.z);
    return b;
}

// Shoot the player's character: TakeDamage with a hit result, like workspace:FireBullet does.
void shoot(GameSession& session, const char* partName, const glm::vec3& at, const glm::vec3& velocity, float damage) {
    char head[256];
    std::snprintf(head, sizeof(head), "local ch = game:GetService('Players'):GetPlayers()[1].Character "
                                      "local hum = ch:FindFirstChildOfClass('Humanoid') local part = ch:FindFirstChild('%s', true) ", partName);
    run(session, std::string(head) + "hum:TakeDamage(" + std::to_string(damage) + ", {Instance = part, Position = " + vec(at) +
                 ", Normal = Vector3.new(0, 0, 1)}, " + vec(velocity) + ")");
}

void play(GameSession& session, float seconds) {
    for (int i = 0; i < (int)(seconds * 60.0f); ++i) {
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

SceneNode* addPart(Scene& scene, const std::string& name, glm::vec3 at, glm::vec3 size) {
    SceneNode* p = scene.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    p->transform.position = at;
    p->transform.scale = size;
    p->anchored = true;
    return p;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: ArteryTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // --- Which hits are on an artery (a plain R6 body, standing) ---
    {
        Scene s;
        SceneNode* leg = addPart(s, "Left Leg", {-0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f});
        SceneNode* rarm = addPart(s, "Right Arm", {0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f});
        SceneNode* head = addPart(s, "Head", {0.0f, 2.25f, 0.0f}, {1.0f, 0.5f, 0.5f});
        SceneNode* torso = addPart(s, "Torso", {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f});
        check(Artery::at(*leg, onPart(leg, {0.4f, 0.3f, 0.5f})) == std::string("Left Thigh"), "inside of the left thigh: the femoral artery");
        check(Artery::at(*leg, onPart(leg, {-0.4f, 0.3f, 0.5f})) == nullptr, "outside of the thigh: no artery");
        check(Artery::at(*leg, onPart(leg, {0.4f, -0.4f, 0.5f})) == nullptr, "the shin: no artery");
        check(Artery::at(*rarm, onPart(rarm, {-0.4f, 0.1f, 0.5f})) == std::string("Right Arm"), "inside of the right upper arm: brachial");
        check(Artery::at(*head, onPart(head, {0.0f, -0.4f, 0.5f})) == std::string("Neck"), "under the jaw: the neck");
        check(Artery::at(*head, onPart(head, {0.0f, 0.4f, 0.5f})) == nullptr, "top of the head: no artery");
        check(Artery::at(*torso, onPart(torso, {0.0f, 0.45f, 0.5f})) == std::string("Neck"), "base of the neck");
        check(Artery::at(*torso, onPart(torso, {0.3f, -0.2f, 0.5f})) == nullptr, "the belly: no artery");

        Artery::Stagger st;
        st.seed = 0.3f;
        float most = 0.0f;
        for (int i = 0; i < 600; ++i) { st.step(1.0f / 60.0f, 0.8f, {0, 0, 1}); most = std::max(most, glm::length(st.drift)); }
        check(most > 0.2f && most <= 1.01f, "staggering pulls the body about, but not faster than walking");
        check(st.pitch > 15.0f, "bleeding badly: hunched over");
    }

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    scene.world().spawnForceField = 0.0f;
    scene.world().gore = GoreLevel::Blood;
    session.start();
    Player* p = scene.player();
    if (!p || !p->root()) { std::cerr << "no player in that game\n"; return 1; }
    // A quiet floor of our own, away from the game's obstacles.
    run(session, "local f = Instance.new('Part') f.Name = 'ArteryFloor' f.Anchored = true f.Size = Vector3.new(160, 2, 160) "
                 "f.Position = Vector3.new(500, 99, 500) f.Parent = workspace");
    p->root()->transform.position = glm::vec3(500.0f, 100.2f, 500.0f);
    play(session, 1.5f);
    SceneNode* leg = p->root()->findChild("Left Leg", true);
    if (!leg) { std::cerr << "the player has no Left Leg\n"; return 1; }

    // A blunt hit on the same spot: a bruise, no artery.
    shoot(session, "Left Leg", onPart(leg, {0.4f, 0.3f, 0.5f}), {0.0f, 0.0f, -20.0f}, 2.0f);
    check(!p->humanoid().artery.on, "a blunt hit on the thigh doesn't open an artery");
    // Turned off for the game: nothing.
    scene.world().arteries = false;
    shoot(session, "Left Leg", onPart(leg, {0.4f, 0.3f, 0.5f}), {0.0f, 0.0f, -300.0f}, 2.0f);
    check(!p->humanoid().artery.on, "workspace.ArterialBleeding off: no artery");
    scene.world().arteries = true;
    // Not for this character.
    run(session, "game:GetService('Players'):GetPlayers()[1].Character:SetAttribute('Arteries', false)");
    shoot(session, "Left Leg", onPart(leg, {0.4f, 0.3f, 0.5f}), {0.0f, 0.0f, -300.0f}, 2.0f);
    check(!p->humanoid().artery.on, "Arteries = false on the character: no artery");
    run(session, "game:GetService('Players'):GetPlayers()[1].Character:SetAttribute('Arteries', nil)");
    p->humanoid().health = p->humanoid().maxHealth;

    // The real thing: a bullet through the inner thigh.
    shoot(session, "Left Leg", onPart(leg, {0.4f, 0.3f, 0.5f}), {0.0f, 0.0f, -300.0f}, 5.0f);
    check(p->humanoid().artery.on && p->humanoid().artery.name == "Left Thigh", "a bullet through the inner thigh opens the femoral artery");
    run(session, "local h = game:GetService('Players'):GetPlayers()[1].Character:FindFirstChildOfClass('Humanoid') "
                 "_G.arteryBleeding = h.BleedingOut _G.arteryName = h.Artery");
    run(session, "assert(_G.arteryBleeding == true and _G.arteryName == 'Left Thigh', 'Humanoid.BleedingOut / Artery')");

    // Pulses: blood comes and goes with the heartbeat.
    const glm::vec3 start = p->root()->transform.position;
    int rises = 0, last = drops(scene), most = 0;
    for (int i = 0; i < 120; ++i) {
        play(session, 1.0f / 30.0f);
        const int now = drops(scene);
        if (now > last + 4) ++rises;
        most = std::max(most, now);
        last = now;
    }
    check(most > 20, "blood spurts out of the wound");
    check(rises >= 3, "it comes in pulses, beat after beat (" + std::to_string(rises) + " spurts in 4 s)");
    const float health4 = p->humanoid().health;
    check(health4 < p->humanoid().maxHealth * 0.8f && health4 > 0.0f, "the health drains away");
    const float wandered = glm::length(glm::vec2(p->root()->transform.position.x - start.x, p->root()->transform.position.z - start.z));
    check(wandered > 0.5f, "staggering about with nobody at the keys (" + std::to_string(wandered) + " studs)");

    // Down and dead within the time.
    bool collapsed = false;
    for (int i = 0; i < 9 * 10 && !p->isDead(); ++i) {
        play(session, 0.1f);
        if (p->tripped() && !p->isDead()) collapsed = true;
    }
    check(collapsed, "the legs go: down on the floor before dying");
    check(p->isDead(), "bled to death");

    // The body comes to rest and stays there (no twitching, no creeping, no flinging).
    SceneNode* torso = p->root()->findChild("Torso", true);
    play(session, 4.0f);
    const glm::vec3 restAt = torso ? glm::vec3(torso->worldMatrix()[3]) : glm::vec3(0.0f);
    play(session, 1.0f);
    const glm::vec3 later = torso ? glm::vec3(torso->worldMatrix()[3]) : glm::vec3(0.0f);
    check(torso && glm::length(later - restAt) < 0.02f, "a dead body lies still");

    // A script's own artery, and stopping it.
    p->loadCharacter();
    play(session, 1.0f);
    check(!p->humanoid().artery.on && !p->isDead(), "respawned: no artery");
    run(session, "game:GetService('Players'):GetPlayers()[1].Character:FindFirstChildOfClass('Humanoid'):CutArtery('Head', 30)");
    check(p->humanoid().artery.on && p->humanoid().artery.name == "Neck", "CutArtery('Head') opens the neck");
    play(session, 1.0f);
    run(session, "game:GetService('Players'):GetPlayers()[1].Character:FindFirstChildOfClass('Humanoid'):StopBleeding()");
    const float kept = p->humanoid().health;
    play(session, 2.0f);
    check(!p->humanoid().artery.on && p->humanoid().health == kept && !p->isDead(), "StopBleeding: a tourniquet stops it");

    // --- A dead body pushed out from deep inside a part isn't flung ---
    {
        Scene s;
        addPart(s, "Floor", {0.0f, -0.5f, 0.0f}, {60.0f, 1.0f, 60.0f});
        addPart(s, "Block", {0.0f, 1.0f, 1.2f}, {4.0f, 2.0f, 2.0f});   // the legs are deep inside this
        Physics phys;
        phys.gather(s);
        SceneNode* body = s.addNode("Body", PrimitiveType::Cube, nullptr);
        body->kind = NodeKind::Model;
        auto add = [&](const char* n, glm::vec3 at, glm::vec3 size) {
            SceneNode* q = s.addNode(n, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
            q->transform.position = at;
            q->transform.scale = size;
            s.reparent(q, body);
            return q;
        };
        // Lying face down across the edge of the block.
        add("Torso", {0.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 1.0f})->transform.rotation.x = 90.0f;
        add("Head", {0.0f, 0.5f, -0.75f}, {1.0f, 0.5f, 0.5f})->transform.rotation.x = 90.0f;
        add("Left Arm", {-0.75f, 0.5f, 0.0f}, {0.5f, 0.5f, 1.0f})->transform.rotation.x = 90.0f;
        add("Right Arm", {0.75f, 0.5f, 0.0f}, {0.5f, 0.5f, 1.0f})->transform.rotation.x = 90.0f;
        add("Left Leg", {-0.25f, 0.6f, 1.0f}, {0.5f, 0.5f, 1.0f})->transform.rotation.x = 90.0f;
        add("Right Leg", {0.25f, 0.6f, 1.0f}, {0.5f, 0.5f, 1.0f})->transform.rotation.x = 90.0f;
        Ragdoll rd;
        rd.start(s, body, glm::vec3(0.0f), glm::vec3(0.0f), 0.0f);
        float fastest = 0.0f;
        glm::vec3 was = rd.center();
        for (int i = 0; i < 60 * 6; ++i) {
            rd.update(1.0f / 60.0f, s, phys);
            const glm::vec3 c = rd.center();
            if (i > 2) fastest = std::max(fastest, glm::length(c - was) * 60.0f);
            if (std::getenv("ARTERY_DEBUG") && i < 40) std::printf("%d %.2f %.2f %.2f  v=%.1f\n", i, c.x, c.y, c.z, glm::length(c - was) * 60.0f);
            was = c;
        }
        check(fastest < 15.0f, "a body stuck inside a part comes out of it gently (top speed " + std::to_string(fastest) + ")");
        check(glm::length(glm::vec2(rd.center().x, rd.center().z)) < 6.0f, "...and isn't flung away");
        check(rd.asleep(), "...and then lies still");
    }

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
