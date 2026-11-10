// The big bodies, gore and lighting update: bones in bundles, jiggle and cloth on
// accessories, liquid blood, dents that spring back and tear, clean cuts, bullets
// that go through, gore settings per character, breaking things, a faster physics
// broadphase, lamp shadows, bouncy light and lip sync.
//
//   BodiesGoreLightTests <a game file with a baseplate>
#include "core/Audio.h"
#include "core/Log.h"
#include "game/GameSession.h"
#include "online/Protocol.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "renderer/SceneRenderer.h"
#include "scene/BounceLight.h"
#include "scene/BundleRig.h"
#include "scene/Cloth.h"
#include "scene/EditMesh.h"
#include "scene/Gore.h"
#include "scene/LipSync.h"
#include "scene/Physics.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Shatter.h"
#include "scene/Skin.h"
#include "scene/SoftTissue.h"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
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

SceneNode* addPart(Scene& scene, const std::string& name, glm::vec3 at, glm::vec3 size, SceneNode* in = nullptr) {
    SceneNode* p = scene.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    if (in) p = scene.insert(scene.detach(p), in);
    p->transform.position = at;
    p->transform.scale = size;
    return p;
}

void setAttr(SceneNode* n, const std::string& name, Attribute a) {
    a.name = name;
    for (Attribute& old : n->attributes) if (old.name == name) { old = a; return; }
    n->attributes.push_back(a);
}
Attribute num(double v) { Attribute a; a.type = Attribute::Number; a.n = v; return a; }
Attribute yes() { Attribute a; a.type = Attribute::Bool; a.b = true; return a; }
Attribute str(const std::string& s) { Attribute a; a.type = Attribute::String; a.s = s; return a; }

int bonesIn(const SceneNode& n) {
    int c = 0;
    for (auto& k : n.children) { if (k->isBone()) ++c; c += bonesIn(*k); }
    return c;
}

// A simple arm-shaped bundle part: a box of 8 corners.
nlohmann::json boxPart() {
    using nlohmann::json;
    json v = json::array();
    for (int i = 0; i < 8; ++i) { v.push_back(i & 1 ? 0.5 : -0.5); v.push_back(i & 2 ? 0.5 : -0.5); v.push_back(i & 4 ? 0.5 : -0.5); }
    json f = json::array({json::array({0, 1, 3, 2}), json::array({4, 6, 7, 5}), json::array({0, 4, 5, 1}),
                          json::array({2, 3, 7, 6}), json::array({0, 2, 6, 4}), json::array({1, 5, 7, 3})});
    return {{"v", v}, {"f", f}, {"size", {1, 2, 1}}, {"pos", {0, 0, 0}}};
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: BodiesGoreLightTests <game file>\n"; return 2; }
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
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    // --- 1, 4: bones and bending in bundles (the server checks them) ---
    {
        using nlohmann::json;
        json body = {{"Left Arm", boxPart()}};
        body["Left Arm"]["bones"] = json::array({{{"name", "Upper"}, {"parent", -1}, {"pos", {0, 1, 0}}, {"rot", {0, 0, 180}}},
                                                 {{"name", "Floppy"}, {"parent", 0}, {"pos", {0, 1, 0}}, {"rot", {0, 0, 0}},
                                                  {"type", "Jiggle"}, {"stiffness", 0.5}, {"damping", 0.3}}});
        json out;
        check(Online::bundleBody(body, out).empty() && out["Left Arm"]["bones"].size() == 2, "a bundle part with 2 bones is allowed");
        body["Left Arm"]["bones"][1]["parent"] = 5;
        check(!Online::bundleBody(body, out).empty(), "a bone can't hang off one that comes after it");
        body["Left Arm"]["bones"][1]["parent"] = 0;
        body["Left Arm"]["bones"][1]["type"] = "IK";
        check(!Online::bundleBody(body, out).empty(), "only Normal and Jiggle bones in bundles");
        body["Left Arm"]["bones"][1]["type"] = "Jiggle";
        json tooMany = json::array();
        for (int i = 0; i <= Online::kBodyMostBones; ++i)
            tooMany.push_back({{"name", "B" + std::to_string(i)}, {"parent", -1}, {"pos", {0, 0, 0}}, {"rot", {0, 0, 0}}});
        json big = body;
        big["Left Arm"]["bones"] = tooMany;
        check(!Online::bundleBody(big, out).empty(), "too many bones is turned down");
        json weights = json::array();
        for (int i = 0; i < 8; ++i) weights.push_back(json::array({i < 4 ? 0 : 1, 1.0}));
        body["Left Arm"]["w"] = weights;
        check(Online::bundleBody(body, out).empty() && out["Left Arm"].contains("w"), "painted weights (one per corner) are kept");
        body["Left Arm"]["w"].erase(0);
        check(!Online::bundleBody(body, out).empty(), "weights must match the corners");

        json bend = {{"Head", boxPart()}};
        bend["Head"]["bend"] = true;
        check(!Online::bundleBody(bend, out).empty(), "a head can't bend in the middle");
        json leg = {{"Right Leg", boxPart()}};
        leg["Right Leg"]["bend"] = true;
        check(Online::bundleBody(leg, out).empty() && out["Right Leg"].value("bend", false), "a leg can bend at the knee");

        // Worn: the part gets the bones (and bends).
        SceneNode* arm = addPart(scene, "Left Arm", {40, 5, 0}, {1, 2, 1});
        MeshEdit::attach(*arm, MeshEdit::fromPrimitive(PrimitiveType::Cube));
        check(BundleRig::apply(*arm, out["Right Leg"]) && bonesIn(*arm) == 2, "bend on: an upper and a lower bone go in");
        json saved;
        BundleRig::toJson(*arm, saved);
        check(saved.value("bend", false) || saved.contains("bones"), "and they're saved with the part");
        BundleRig::clear(*arm);
        check(bonesIn(*arm) == 0, "taking the bundle off takes its bones out");
        json withBones;
        Online::bundleBody({{"Left Arm", [&] { json p = boxPart(); p["bones"] = body["Left Arm"]["bones"]; return p; }()}}, withBones);
        check(BundleRig::apply(*arm, withBones["Left Arm"]) && bonesIn(*arm) == 2, "a bundle's own bones go in");
    }

    // --- 2: jiggle on accessories ---
    {
        auto model = std::make_unique<SceneNode>("Bunny Ears", NodeKind::Model);
        SceneNode* hat = scene.insert(std::move(model), nullptr);
        SceneNode* ears = addPart(scene, "Ears", {0, 30, 0}, {0.4f, 2.0f, 0.4f}, hat);
        MeshEdit::attach(*ears, MeshEdit::fromPrimitive(PrimitiveType::Cube));
        setAttr(hat, "Jiggle", yes());
        check(Cloth::wants(*ears, "Jiggle"), "Jiggle on the accessory's model counts for its parts");
        check(Cloth::rigJiggle(*ears) && bonesIn(*ears) >= 2, "a jiggly accessory gets springy bones by itself");
    }

    // --- 3: cloth ---
    {
        SceneNode* cape = addPart(scene, "Cape", {0, 40, 10}, {2.0f, 3.0f, 0.1f});
        auto edit = MeshEdit::fromPrimitive(PrimitiveType::Cube);
        MeshEdit::attach(*cape, edit);
        for (int i = 0; i < 90; ++i) Cloth::step(*cape, 1.0f / 60.0f, 98.1f, {0, 0, 40}, {});
        const float sagged = Cloth::sag(*cape);
        check(sagged > 0.1f, "wind blows a cape out from where it's pinned (moved " + std::to_string(sagged) + ")");
        Cloth::reset(*cape);
        check(Cloth::sag(*cape) < sagged, "and goes back when reset");
    }

    // --- 5: liquid blood ---
    {
        ParticleSystem& ps = scene.particles();
        int got = 0;
        ps.setLiquidSink([&](GoreKind, const glm::vec3&, const glm::vec3&) { ++got; });
        ps.spray(GoreKind::Blood, {0, 10, 0}, {0, 1, 0}, 12, 4.0f);
        check(got > 0 && ps.liquidSent() > 0, "with Liquid Blood on, some blood drops become real liquid");
        ps.setLiquidSink(nullptr);
        const int before = got;
        ps.spray(GoreKind::Blood, {0, 10, 0}, {0, 1, 0}, 12, 4.0f);
        check(got == before, "and none when it's off");
    }

    // --- 6: dents spring back, and tear ---
    {
        SceneNode* torso = addPart(scene, "Torso", {60, 5, 0}, {2, 2, 1});
        SoftTissue::dent(*torso, {60.9f, 5.9f, 0.5f}, {0, 0, 1}, {0, 0, -120}, true);
        const float deep = SoftTissue::depth(*torso);
        check(deep > 0.01f, "a hit pushes a dent in");
        for (int i = 0; i < 120; ++i) SoftTissue::update(scene, 1.0f / 30.0f);
        check(SoftTissue::depth(*torso) < deep * 0.3f, "and it springs back out");
        SceneNode* head = addPart(scene, "Head", {64, 5, 0}, {1, 1, 1});
        check(SoftTissue::feelOf(*head).depth < SoftTissue::feelOf(*torso).depth, "a head takes smaller dents than a torso");
        bool tore = false;
        int hits = 0;
        for (; hits < 60 && !tore; ++hits) tore = SoftTissue::dent(*head, {64.45f, 5.45f, 0.5f}, {0, 0, 1}, {0, 0, -400}, true);
        check(tore, "hit the same spot hard enough and it tears (" + std::to_string(hits) + " hits, " + std::to_string(SoftTissue::depth(*head)) + " deep)");
    }

    // --- 7: clean cuts ---
    {
        SceneNode* arm = addPart(scene, "Right Arm", {80, 5, 0}, {1, 2, 1});
        SceneNode* cap = Gore::cutEnd(scene, arm, {0, 0.5f, 0}, 1.0f, GoreKind::Blood, {0.5f, 0.02f, 0.03f});
        int caps = 0;
        for (auto& c : arm->children) if (c->name.rfind("CutEnd", 0) == 0) ++caps;
        check(cap && caps == 2, "a cut-off arm gets a flesh cap and a bone in the middle");
        check(cap && !cap->canCollide && cap->internal, "the caps don't get in the way and aren't saved");
        SceneNode* robot = addPart(scene, "Right Leg", {84, 5, 0}, {1, 2, 1});
        SceneNode* plate = Gore::cutEnd(scene, robot, {0, -0.5f, 0}, 1.0f, GoreKind::Oil, {0, 0, 0});
        check(plate && plate->material == Material::Metal, "a robot gets a metal plate with wires instead");
        Gore::removeCutEnds(scene, scene.root());
        caps = 0;
        for (auto& c : arm->children) if (c->name.rfind("CutEnd", 0) == 0) ++caps;
        check(caps == 0, "respawning takes the caps away");
    }

    // --- 8: bullets that go through ---
    {
        SceneNode* chest = addPart(scene, "Chest", {100, 5, 0}, {2, 2, 1});
        glm::vec3 out;
        check(Gore::exitPoint(*chest, {100, 5, 0.5f}, {0, 0, -1}, out) && std::abs(out.z + 0.5f) < 0.01f,
              "a shot in the front comes out of the back");
        check(Gore::kThroughSpeed > 0.0f, "only fast hits go right through");
    }

    // --- 9, 10: gore for any shape, and settings per character ---
    {
        auto m = std::make_unique<SceneNode>("Robot", NodeKind::Model);
        SceneNode* robot = scene.insert(std::move(m), nullptr);
        SceneNode* part = addPart(scene, "Torso", {120, 5, 0}, {2, 2, 1}, robot);
        setAttr(robot, "Gore", str("Oil"));
        Attribute green; green.type = Attribute::Color3; green.v = {0.1f, 0.9f, 0.2f};
        setAttr(robot, "BloodColor", green);
        setAttr(robot, "BloodAmount", num(2.0));
        scene.world().gore = GoreLevel::Blood;
        const Gore::Look look = Gore::lookFor(scene, part);
        check(look.kind == GoreKind::Oil, "Gore = \"Oil\" on a character makes it leak oil");
        check(std::abs(look.color.g - 0.9f) < 1e-3f, "BloodColor sets its colour");
        check(look.amount > scene.world().bloodAmount, "BloodAmount makes more of it");
        {
            Gore::Scope scope(scene, part);
            check(scene.particles().bloodColor().g > 0.8f, "while it bleeds, the blood is its colour");
        }
        check(scene.particles().bloodColor().g < 0.5f, "and goes back to the game's own colour after");
        setAttr(robot, "Gore", str("Off"));
        check(!Gore::lookFor(scene, part).on, "Gore = \"Off\" on a character: it doesn't bleed");
    }

    // --- 14: breaking things ---
    {
        const auto boxes = Shatter::split({0.5f, 0, 0}, 12, 7);
        float volume = 0.0f, nearHit = 1e9f, farHit = 0.0f;
        for (const Shatter::Box& b : boxes) {
            const float v = b.size.x * b.size.y * b.size.z;
            volume += v;
            if (b.center.x > 0.25f) nearHit = std::min(nearHit, v);
            if (b.center.x < -0.25f) farHit = std::max(farHit, v);
        }
        check(boxes.size() == 12, "a part splits into the pieces asked for");
        check(std::abs(volume - 1.0f) < 1e-3f, "the pieces fill the part exactly");
        check(nearHit < farHit, "pieces near the hit are smaller");
        SceneNode* wall = addPart(scene, "Wall", {140, 5, 0}, {6, 6, 1});
        const uint64_t id = wall->id;
        const auto pieces = Shatter::breakPart(scene, wall, {140, 5, 0.5f}, 1.0f);
        check(pieces.size() >= 4 && !scene.findById(id), "breaking a wall swaps it for loose pieces");
        check(!pieces.empty() && !pieces[0]->anchored, "and they fall");
        SceneNode* crate = addPart(scene, "Crate", {150, 5, 0}, {2, 2, 2});
        crate->tags.push_back("Breakable");
        check(Shatter::breakable(*crate) && Shatter::breakSpeed(*crate) == 45.0f, "the Breakable tag makes it break by itself");
    }

    // --- 13: many loose parts (the faster broadphase still finds every touch) ---
    {
        Physics phys;
        SceneNode* ground = addPart(scene, "PileGround", {210, -0.5f, 10}, {60, 1, 60});
        ground->anchored = true;
        std::vector<SceneNode*> crates;
        for (int i = 0; i < 120; ++i) {
            SceneNode* c = addPart(scene, "Pile", {200.0f + (i % 10) * 2.2f, 1.2f + (i / 10) * 2.2f, 10.0f}, {2, 2, 2});
            c->anchored = false;
            crates.push_back(c);
        }
        std::vector<uint64_t> fallen;
        for (int i = 0; i < 180; ++i) { phys.gather(scene); phys.stepParts(scene, 1.0f / 60.0f, fallen); }
        float lowest = 1e9f;
        for (SceneNode* c : crates) lowest = std::min(lowest, c->transform.position.y);
        check(lowest > 0.0f, "120 crates in a pile stay on the ground (none fall through)");
        for (SceneNode* c : crates) scene.removeNode(c);
        scene.removeNode(ground);
    }

    // --- 11: lamp shadows (the shader works the same sides out) ---
    {
        const glm::vec3 lamp(3, 4, 5);
        const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 20.0f);
        const glm::vec3 below = lamp + glm::vec3(0.5f, -6.0f, 1.0f);
        glm::vec4 c = proj * SceneRenderer::lampFaceView(lamp, 3) * glm::vec4(below, 1.0f);
        check(c.w > 0.0f && std::abs(c.x / c.w) < 1.0f && std::abs(c.y / c.w) < 1.0f, "a spot under a lamp is in its downward side");
        // The shader's own sum: u = dot(v, R) / dot(v, F) with R = cross(U, -F).
        const glm::vec3 v = below - lamp, F(0, -1, 0), U(0, 0, 1), R = glm::cross(U, -F);
        check(std::abs(c.x / c.w - glm::dot(v, R) / glm::dot(v, F)) < 1e-4f &&
              std::abs(c.y / c.w - glm::dot(v, U) / glm::dot(v, F)) < 1e-4f, "and lands where the shader looks for it");
        SceneNode* l = scene.insert(std::make_unique<SceneNode>("Lamp", NodeKind::Light), nullptr);
        l->lightShadows = true;
        auto copy = Serializer::nodeFromString(Serializer::nodeToString(*l), true);
        check(copy && copy->lightShadows, "a lamp's Shadows is saved");
        Environment back;
        Environment env;
        env.bounceLight = 1.25f;
        Serializer::environmentFromString(back, Serializer::environmentToString(env));
        check(back.bounceLight == 1.25f, "and Bouncy Light is saved with the lighting");
    }

    // --- 12: bouncy light ---
    {
        Environment env;
        env.sunElevation = 60.0f;
        env.bounceLight = 1.0f;
        SceneNode* floor = addPart(scene, "RedFloor", {300, 50, 0}, {40, 1, 40});
        floor->color = {0.9f, 0.05f, 0.05f};
        const auto points = BounceLight::gather(scene, env, {300, 50, 0}, {});
        bool red = !points.empty();
        for (const auto& p : points) red = red && p.color.r > p.color.g * 4.0f;
        check(red, "sun on a red floor bounces red light");
        env.bounceLight = 0.0f;
        check(BounceLight::gather(scene, env, {300, 50, 0}, {}).empty(), "Bouncy Light 0 = none");
        scene.removeNode(floor);
    }

    // --- 15: lip sync ---
    {
        auto m = std::make_unique<SceneNode>("Talker", NodeKind::Model);
        SceneNode* talker = scene.insert(std::move(m), nullptr);
        SceneNode* head = addPart(scene, "Head", {400, 5, 0}, {2, 1, 1}, talker);
        auto jaw = std::make_unique<SceneNode>("Jaw", NodeKind::Attachment);
        jaw->bone = true;
        SceneNode* j = scene.insert(std::move(jaw), head);
        check(LipSync::target(scene, *talker, nullptr) == 0.0f, "a quiet character's mouth is shut");
        setAttr(talker, "MouthOpen", num(1.0));
        for (int i = 0; i < 30; ++i) LipSync::update(scene, 1.0f / 60.0f, nullptr);
        check(LipSync::mouth(head->id) > 0.9f, "MouthOpen = 1 opens it wide");
        check(std::abs(j->transform.rotation.x - LipSync::kJawDegrees * LipSync::mouth(head->id)) < 0.5f, "and a Jaw bone turns down with it");
        talker->attributes.clear();
        for (int i = 0; i < 60; ++i) LipSync::update(scene, 1.0f / 60.0f, [](const std::string& who) { return who == "Talker" ? 0.6f : 0.0f; });
        check(LipSync::mouth(head->id) > 0.5f, "a player talking on voice chat moves their mouth");
        const float hum[4] = {0.5f, -0.5f, 0.5f, -0.5f};
        check(std::abs(Audio::loudnessOf(hum, 4) - 0.5f) < 1e-4f, "loudness is the sound's RMS");
    }

    // --- Lua: the new settings ---
    {
        check(run(session, "workspace.LiquidBlood = true; workspace.SoftTissue = false; workspace.GlobalWind = Vector3.new(5, 0, 0)", err) &&
              scene.world().liquidBlood && !scene.world().softTissue && scene.world().globalWind.x == 5.0f,
              "scripts can turn on liquid blood, soft tissue and wind (" + err + ")");
        check(run(session, "game.Lighting.BounceLight = 1.5", err) && scene.environment().bounceLight == 1.5f,
              "and Lighting.BounceLight (" + err + ")");
        check(run(session, "local l = Instance.new('PointLight'); l.Shadows = true; assert(l.Shadows == true)", err),
              "and a light's Shadows (" + err + ")");
        check(run(session, "local p = Instance.new('Part'); p.Size = Vector3.new(4, 4, 4); p.Position = Vector3.new(500, 10, 0); "
                           "p.Parent = workspace; local bits = p:Shatter(p.Position, 1); assert(#bits >= 4, #bits)", err),
              "and part:Shatter() breaks a part into pieces (" + err + ")");
    }

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
