// Natural disasters a game spawns (Disasters.h, game:GetService("Disasters")):
// lightning, tornadoes, tsunamis and earthquakes.
//
//   DisastersTests <a game file with a baseplate>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Disasters.h"
#include "scene/Player.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Storms.h"

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

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

SceneNode* named(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (!found && n->name == name) found = n; });
    return found;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: DisastersTests <game file>\n"; return 2; }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = {1280, 720};
    Mesh::headless = true;
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // ---- The numbers on their own ------------------------------------------------
    {
        Disasters::Kind k;
        check(Disasters::kindFromName("tornado", k) && k == Disasters::Kind::Tornado, "kind names are read in any capitals");
        check(!Disasters::kindFromName("volcano", k), "an unknown kind is refused");

        Disasters::Disaster q = Disasters::defaults(Disasters::Kind::Earthquake);
        q.strength = 7.0f;
        q.fade = 1.0f;
        q.age = 0.3f;
        const float early = Disasters::quakeLevel(q);
        q.age = 4.0f;
        const float later = Disasters::quakeLevel(q);
        check(later > early && later > 0.3f, "an earthquake builds up (the P wave, then the S wave)");
        Disasters::Disaster small = q;
        small.strength = 3.0f;
        check(Disasters::quakeLevel(small) < later * 0.6f, "a magnitude 3 shakes much less than a 7");

        Disasters::Disaster t = Disasters::defaults(Disasters::Kind::Tornado);
        Disasters::Disaster big = t;
        big.strength = 5.0f;
        check(Disasters::tornadoCore(big) > Disasters::tornadoCore(t) && Disasters::tornadoCarry(big) > Disasters::tornadoCarry(t),
              "a stronger tornado is wider and carries people higher");

        Disasters::Disaster w = Disasters::defaults(Disasters::Kind::Tsunami);
        w.pos = {0.0f, 0.0f, 0.0f};
        w.dir = {0.0f, 0.0f, 1.0f};
        w.height = 20.0f;
        w.fade = 1.0f;
        check(Disasters::waterTop(w, {0.0f, 0.0f, -5.0f}) > 10.0f, "just behind a tsunami's front the water is tall");
        check(Disasters::waterTop(w, {0.0f, 0.0f, 40.0f}) < 0.0f, "in front of it there's no water yet");
    }

    // ---- In a running game ---------------------------------------------------------
    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.start();
    auto step = [&](int frames) { for (int i = 0; i < frames; ++i) session.update(1.0f / 60.0f, 0.0f, false); };
    step(5);

    Player* me = scene.player();
    if (!me || !me->root()) { std::cerr << "no player\n"; return 1; }
    step(60);   // (land on the ground)
    for (int i = 0; i < 40 && me->root()->hasForceField(); ++i) step(30);   // (the spawn ForceField wears off)
    check(run(session, R"(
        _G.hits, _G.killed, _G.ended, _G.started = 0, 0, 0, 0
        local D = game:GetService("Disasters")
        D.Hit:Connect(function() _G.hits = _G.hits + 1 end)
        D.Killed:Connect(function() _G.killed = _G.killed + 1 end)
        D.Ended:Connect(function() _G.ended = _G.ended + 1 end)
        D.Started:Connect(function() _G.started = _G.started + 1 end)
    )", err), "a script can reach the Disasters service: " + err);
    // Lightning: damage on the player, a kill on the NPC.
    check(run(session, "game:GetService('Disasters'):Lightning(game:GetService('Players').LocalPlayer, {Damage = 40, Ragdoll = false})", err),
          "lightning strikes a player: " + err);
    step(10);
    check(std::abs(me->humanoid().health - 60.0f) < 0.5f, "Damage = 40 leaves them on 60 health (" + std::to_string(me->humanoid().health) + ")");
    check(run(session, "game:GetService('Disasters'):Lightning(game:GetService('Players').LocalPlayer, {Kill = true})", err),
          "lightning with Kill: " + err);
    step(10);
    check(me->isDead() || me->humanoid().health <= 0.0f, "Kill = true kills whoever it hits");
    check(run(session, "assert(_G.hits >= 2 and _G.killed >= 1, 'hits ' .. _G.hits .. ' killed ' .. _G.killed)", err),
          "the Hit and Killed events reach scripts: " + err);
    step(120);
    check(scene.disasters().list.empty(), "a lightning strike is over in a moment");

    // A tornado: settings, moving it, sending it to everyone, stopping it.
    check(run(session, R"(
        local D = game:GetService("Disasters")
        _G.t = D:Tornado(Vector3.new(200, 0, 200), {Strength = 3, RotationSpeed = 2, ThrowForce = 1.5, ExplodeOnImpact = true, Duration = 0})
        assert(_G.t.Kind == "Tornado", "kind " .. tostring(_G.t.Kind))
        assert(_G.t.ExplodeOnImpact == true and _G.t.RotationSpeed == 2, "settings read back")
        _G.t.Velocity = Vector3.new(10, 0, 0)
        assert(#D:GetActive() == 1, "GetActive")
    )", err), "a tornado starts with its settings: " + err);
    step(60);
    const Disasters::Disaster* t = scene.disasters().list.empty() ? nullptr : &scene.disasters().list.front();
    check(t && t->kind == Disasters::Kind::Tornado && t->pos.x > 205.0f, "it drifts along at its Velocity");
    check(t && glm::length(Disasters::wind(scene, t->pos + glm::vec3(Disasters::tornadoCore(*t), 10.0f, 0.0f))) > 20.0f,
          "it blows hard around its core");
    check(t && glm::length(Disasters::characterPush(scene, t->pos + glm::vec3(Disasters::tornadoCore(*t) * 0.6f, 4.0f, 0.0f), glm::vec3(0.0f))) > 30.0f,
          "and pushes a character standing in it");
    check(Disasters::near(scene, t ? t->pos + glm::vec3(5.0f, 10.0f, 0.0f) : glm::vec3(0.0f)), "the anti-cheat knows people near it may be flung about");

    // What every player's computer gets.
    {
        const std::string text = Disasters::encode(scene.disasters());
        Scene other;
        Disasters::decode(other, text);
        check(other.disasters().list.size() == 1 && other.disasters().list[0].kind == Disasters::Kind::Tornado
              && std::abs(other.disasters().list[0].pos.x - t->pos.x) < 0.1f && other.disasters().list[0].spin == 2.0f,
              "a player's computer gets the same tornado at the same place");
        Disasters::decode(other, "");
        check(other.disasters().list.empty(), "and drops it when the host's list is empty");
    }

    check(run(session, "_G.t:MoveTo(Vector3.new(-50, 0, 10)) _G.t.Velocity = Vector3.new(0, 0, 0)", err), "MoveTo: " + err);
    step(2);
    check(!scene.disasters().list.empty() && std::abs(scene.disasters().list[0].pos.x + 50.0f) < 1.0f, "MoveTo puts it somewhere else");
    check(run(session, "_G.t:Stop()", err), "Stop: " + err);
    step(400);
    check(scene.disasters().list.empty(), "Stop makes it fade away");
    check(run(session, "assert(_G.ended >= 1 and _G.started >= 3, 'ended ' .. _G.ended .. ' started ' .. _G.started) assert(not _G.t.Active)", err),
          "Started and Ended fire, and the handle says it's gone: " + err);

    // An earthquake shakes the ground and the camera.
    check(run(session, "_G.q = game:GetService('Disasters'):Earthquake(7, {Duration = 10, LoseFooting = false})", err), "Earthquake(7): " + err);
    step(240);
    const float shake = Disasters::quakeAt(scene, {0.0f, 0.0f, 0.0f});
    check(shake > 0.3f, "the ground shakes");
    check(Disasters::cameraShake(scene, {0.0f, 5.0f, 0.0f}) > 0.0f, "and so do cameras");
    check(glm::length(Storms::groundJolt(scene, {0.0f, 0.0f, 0.0f})) > 0.0f, "loose parts get jolted");
    check(run(session, "assert(game:GetService('Disasters'):GetShakeAt(Vector3.new(0, 0, 0)) > 0)", err), "GetShakeAt: " + err);
    check(run(session, "game:GetService('Disasters'):StopAll()", err), "StopAll: " + err);
    step(300);
    check(scene.disasters().list.empty() && Disasters::quakeAt(scene, {0.0f, 0.0f, 0.0f}) == 0.0f, "StopAll calms everything down");

    // A Studio spawner part.
    check(run(session, R"(
        local p = Instance.new("Part")
        p.Name = "MySpawner" p.Anchored = true p.Position = Vector3.new(0, 1, 80)
        p:SetAttribute("Disaster", "Tsunami")
        p:SetAttribute("Height", 22)
        p:SetAttribute("Speed", 40)
        p.Parent = workspace
        _G.w = game:GetService("Disasters"):SpawnFromPart(p)
        assert(_G.w and _G.w.Kind == "Tsunami", "spawned from a part")
    )", err), "SpawnFromPart reads the part's attributes: " + err);
    step(2);
    check(!scene.disasters().list.empty() && std::abs(scene.disasters().list[0].height - 22.0f) < 0.01f
          && std::abs(scene.disasters().list[0].speed - 40.0f) < 0.01f, "with the Height and Speed set on it");
    check(run(session, "game:GetService('Disasters'):StopAll()", err), "StopAll: " + err);

    // Bad input gets a clear error instead of a crash.
    check(!run(session, "game:GetService('Disasters'):Spawn('Volcano', Vector3.new())", err), "an unknown kind is an error");
    check(!run(session, "game:GetService('Disasters'):Tornado(Vector3.new(), {Strength = 'big'})", err), "a wrong setting type is an error");

    session.stop();
    std::cout << (failures ? "FAILED: " : "all passed") << (failures ? std::to_string(failures) : "") << "\n";
    return failures ? 1 : 0;
}
