// The realism update: real recorded sounds (with takes), injuries and limping, stamina,
// blinking and glancing, bullets that drop / drift / go through / ricochet, impact and
// footstep sounds by material, spreading fire, footprints and tyre tracks, car grip and
// suspension, wet bodies, sound that knows the room, light rays, eyes adjusting and
// motion blur.
//
//   RealismTests <a game file with a baseplate>
#include "core/Audio.h"
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Acoustics.h"
#include "scene/Ballistics.h"
#include "scene/FireSpread.h"
#include "scene/Impacts.h"
#include "scene/Particles.h"
#include "scene/Physics.h"
#include "scene/Realism.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"
#include "scene/Terrain.h"
#include "scene/Tracks.h"
#include "scene/Vehicles.h"

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

SceneNode* addPart(Scene& scene, const std::string& name, glm::vec3 at, glm::vec3 size, Material m = Material::Plastic) {
    SceneNode* p = scene.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    p->transform.position = at;
    p->transform.scale = size;
    p->material = m;
    return p;
}

// A flat world for the pure bullet maths: one slab of `m` across x = wallX, `thick` thick.
struct Slab { float x0, x1; Material m; };
Ballistics::Result shoot(const std::vector<Slab>& slabs, glm::vec3 from, glm::vec3 vel, glm::vec3 wind = glm::vec3(0.0f),
                         float floorY = -1e9f, Material floorMat = Material::Concrete) {
    Ballistics::Shot s;
    s.origin = from;
    s.velocity = vel;
    s.wind = wind;
    s.damage = 30.0f;
    auto cast = [&](const glm::vec3& o, const glm::vec3& d, float len, Ballistics::Hit& h) {
        float best = len + 1.0f;
        for (size_t i = 0; i < slabs.size(); ++i) {
            const Slab& sl = slabs[i];
            if (std::abs(d.x) < 1e-6f) continue;
            const float t = ((d.x > 0 ? sl.x0 : sl.x1) - o.x) / d.x;
            if (t >= 0.0f && t <= len && t < best) {
                best = t; h.part = i + 1; h.material = sl.m; h.normal = glm::vec3(d.x > 0 ? -1.0f : 1.0f, 0, 0);
            }
        }
        if (d.y < 0.0f) {
            const float t = (floorY - o.y) / d.y;
            if (t >= 0.0f && t <= len && t < best) { best = t; h.part = 99; h.material = floorMat; h.normal = glm::vec3(0, 1, 0); }
        }
        if (best > len) return false;
        h.point = o + d * best;
        return true;
    };
    auto thick = [&](const Ballistics::Hit& h, const glm::vec3&) {
        if (h.part == 99) return 1e9f;
        const Slab& sl = slabs[h.part - 1];
        return sl.x1 - sl.x0;
    };
    return Ballistics::trace(s, cast, thick);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: RealismTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // --- Real sounds: every recording is named <sound>_<take> and grouped by sound ---
    {
        check(Audio::takeBase("hit_metal_2") == "hit_metal", "\"hit_metal_2\" is a take of \"hit_metal\"");
        check(Audio::takeBase("fire_loop") == "fire_loop", "a name without a number is its own sound");
        const auto names = Audio::realSoundNames();
        auto has = [&](const char* n) { return std::find(names.begin(), names.end(), n) != names.end(); };
        check(has("hit_metal") && has("hit_wood") && has("hit_glass") && has("hit_flesh") && has("hit_stone"),
              "recorded hits for metal, wood, glass, flesh and stone");
        check(has("step_snow") && has("step_mud") && has("step_metal") && has("step_wet") && has("step_wood"),
              "recorded footsteps for snow, mud, metal, wet feet and wood");
        check(has("break_glass") && has("fire_loop") && has("breath_tired") && has("whiz") && has("splash"),
              "breaking glass, fire, panting, bullet whizz and splash");
        const auto& all = Audio::builtinNames();
        check(std::find(all.begin(), all.end(), "hit_metal") != all.end() && std::find(all.begin(), all.end(), "jump") != all.end(),
              "the recorded sounds join the built-in list (Sound.SoundId = \"hit_metal\")");
    }

    // --- Injuries ---
    {
        using namespace Realism;
        check(limbOf("Left Leg") == LeftLeg && limbOf("RightLowerLeg") == RightLeg && limbOf("LeftHand") == LeftArm &&
              limbOf("UpperTorso") == Torso && limbOf("Head") == None, "body parts map to limbs (R6 and R15 names)");
        Injuries inj;
        inj.add(LeftLeg, 30.0f);
        check(inj.limp(LeftLeg) > 0.5f && inj.limpingLeg() == LeftLeg, "a bullet in the leg makes it limp");
        check(inj.speedScale() < 0.8f && inj.speedScale() > 0.55f, "limping walks slower (but not stopped)");
        inj.add(RightArm, 20.0f);
        float hold = 0.0f;
        check(inj.holdingArm(&hold) == LeftArm && hold > 0.3f, "the left hand holds a hurt right arm");
        for (int i = 0; i < 200; ++i) inj.heal(0.5f);
        check(!inj.any(), "injuries heal by themselves");
    }

    // --- Stamina ---
    {
        Realism::Stamina st;
        for (int i = 0; i < 60 * 45; ++i) st.step(1.0f / 60.0f, 1.0f, false, false);
        check(st.value < 0.05f && st.winded, "running flat out for 45 s wears you out");
        check(st.speedScale() < 0.7f && st.jumpScale() < 0.75f && st.breathless() > 0.9f, "worn out: slower, lower jumps, panting");
        for (int i = 0; i < 60 * 10; ++i) st.step(1.0f / 60.0f, 0.0f, false, false);
        check(st.value > 0.9f && !st.winded && st.speedScale() > 0.99f, "resting gets your breath back");
        Realism::Stamina walk;
        for (int i = 0; i < 60 * 60; ++i) walk.step(1.0f / 60.0f, 0.5f, false, false);
        check(walk.value > 0.99f, "walking gently doesn't tire you");
    }

    // --- Blinking and glancing ---
    {
        int shut = 0, frames = 0;
        float most = 0.0f;
        for (double t = 0.0; t < 60.0; t += 1.0 / 120.0, ++frames) {
            const float b = Realism::blinkAt(t, 42);
            if (b > 0.5f) ++shut;
            most = std::max(most, b);
        }
        const float shutFraction = (float)shut / frames;
        check(most > 0.99f && shutFraction > 0.01f && shutFraction < 0.06f, "eyes blink now and then, and are open most of the time");
        bool differ = false;
        for (double t = 0.0; t < 10.0; t += 0.01) if (Realism::blinkAt(t, 1) != Realism::blinkAt(t, 2)) differ = true;
        check(differ, "different characters blink at different times");
        Realism::Gaze g;
        g.aim(glm::vec3(0, 5, 0), glm::vec3(0, 0, -1), glm::vec3(-5, 5, -5), true);
        for (int i = 0; i < 120; ++i) g.step(1.0f / 60.0f);
        check(g.yaw > 40.0f && g.yaw <= Realism::Gaze::kMaxYaw, "the head turns toward someone on the left");
        g.aim(glm::vec3(0, 5, 0), glm::vec3(0, 0, -1), glm::vec3(0, 5, 10), true);
        check(g.wantYaw == 0.0f, "but doesn't twist round to look behind");
    }

    // --- Bullets (the maths) ---
    {
        using Ballistics::Hit;
        auto r = shoot({{20, 20.8f, Material::Wood}, {40, 41, Material::Metal}}, glm::vec3(0, 5, 0), glm::vec3(900, 0, 0));
        check(r.hits.size() == 2 && r.hits[0].what == Hit::Pierced && r.hits[1].what == Hit::Stopped,
              "a bullet goes through a wooden plank and stops in a steel plate");
        check(r.hits.size() == 2 && r.hits[1].damage < r.hits[0].damage, "it does less harm after going through something");
        r = shoot({{20, 22, Material::Concrete}}, glm::vec3(0, 5, 0), glm::vec3(900, 0, 0));
        check(r.hits.size() == 1 && r.hits[0].what == Hit::Stopped, "concrete stops it");
        r = shoot({{20, 25, Material::Wood}}, glm::vec3(0, 5, 0), glm::vec3(900, 0, 0));
        check(r.hits.size() == 1 && r.hits[0].what == Hit::Stopped, "a thick wooden beam stops it");
        r = shoot({{20, 20.3f, Material::Glass}, {30, 30.3f, Material::Glass}}, glm::vec3(0, 5, 0), glm::vec3(900, 0, 0));
        check(r.hits.size() >= 2 && r.hits[0].what == Hit::Pierced && r.hits[1].what == Hit::Pierced, "it goes through two windows");
        // Drop: 300 studs at 600 a second is half a second: about 7.5 studs down.
        r = shoot({{300, 301, Material::Metal}}, glm::vec3(0, 50, 0), glm::vec3(600, 0, 0));
        const float drop = r.hits.empty() ? 0.0f : 50.0f - r.hits[0].point.y;
        check(drop > 5.0f && drop < 10.0f, "bullets drop over a long shot (" + std::to_string(drop) + " studs)");
        // Wind pushes it sideways.
        Ballistics::Result calm = shoot({{300, 301, Material::Metal}}, glm::vec3(0, 50, 0), glm::vec3(600, 0, 0));
        Ballistics::Result windy = shoot({{300, 301, Material::Metal}}, glm::vec3(0, 50, 0), glm::vec3(600, 0, 0), glm::vec3(0, 0, 40));
        const float drift = windy.hits.empty() || calm.hits.empty() ? 0.0f : windy.hits[0].point.z - calm.hits[0].point.z;
        check(drift > 0.3f, "a side wind pushes the bullet along (" + std::to_string(drift) + " studs)");
        // Skimming a steel floor: it bounces off.
        r = shoot({}, glm::vec3(0, 2, 0), glm::vec3(900, -60, 0), glm::vec3(0.0f), 0.0f, Material::Metal);
        check(!r.hits.empty() && r.hits[0].what == Hit::Ricochet, "a bullet skimming steel ricochets");
        r = shoot({}, glm::vec3(0, 20, 0), glm::vec3(300, -600, 0), glm::vec3(0.0f), 0.0f, Material::Metal);
        check(!r.hits.empty() && r.hits[0].what == Hit::Stopped, "straight in, it doesn't");
        r = shoot({}, glm::vec3(0, 2, 0), glm::vec3(900, -60, 0), glm::vec3(0.0f), 0.0f, Material::Grass);
        check(!r.hits.empty() && r.hits[0].what != Hit::Ricochet, "soft ground doesn't bounce bullets");
    }

    // --- Materials ---
    {
        using namespace Realism;
        check(std::string(hitSound(feelOf(Material::DiamondPlate))) == "hit_metal" &&
              std::string(hitSound(feelOf(Material::Cobblestone))) == "hit_stone" &&
              std::string(breakSound(feelOf(Material::Glass))) == "break_glass", "each material has its own hit and break sound");
        check(std::string(stepSound(TerrainMaterial::Snow)) == "step_snow" && std::string(stepSound(Material::Wood)) == "step_wood" &&
              std::string(stepSound(Material::Metal)) == "step_metal", "footsteps sound of what's underfoot");
        check(hitVolume(2.0f, 10.0f) == 0.0f && hitVolume(30.0f, 50.0f) > hitVolume(8.0f, 50.0f), "harder hits are louder (and soft touches silent)");
        check(takesPrints(TerrainMaterial::Snow) && takesPrints(TerrainMaterial::Mud) && !takesPrints(TerrainMaterial::Rock),
              "snow and mud keep footprints, rock doesn't");
        check(flammability(Material::Wood) > 0.0f && flammability(Material::Metal) == 0.0f && flammability(Material::Concrete) == 0.0f,
              "wood burns, metal and concrete don't");
    }

    // --- Fire spreading (the maths) ---
    {
        check(FireSpread::catchRate(1.0f, false) > FireSpread::catchRate(1.0f, true), "rain slows fire down");
        check(FireSpread::catchRate(0.0f, false) == 0.0f, "metal never catches");
        check(FireSpread::charAfter(5.0f, 1.0f) < 0.5f && FireSpread::charAfter(40.0f, 1.0f) >= 1.0f, "wood chars over about half a minute");
    }

    // --- Sound that knows the room (the maths) ---
    {
        check(Acoustics::occlusion(0, 0.0f) == 0.0f, "nothing in the way: clear");
        check(Acoustics::occlusion(1, 0.5f) > 0.3f && Acoustics::occlusion(1, 0.5f) < Acoustics::occlusion(2, 1.0f),
              "one wall muffles it, two walls more");
        const Acoustics::Room open = Acoustics::roomFrom(4, 18, 30.0f);
        const Acoustics::Room cupboard = Acoustics::roomFrom(18, 18, 3.0f);
        const Acoustics::Room hall = Acoustics::roomFrom(18, 18, 40.0f);
        check(open.wet == 0.0f, "out in the open: no echo");
        check(cupboard.wet > 0.5f && hall.decay > cupboard.decay * 3.0f, "a hall echoes far longer than a cupboard");
        check(Acoustics::roomRays().size() == 18, "18 rays feel out the room");
    }

    // --- Tyres ---
    {
        // A wheel of radius 1 rolling at 10 studs a second along +x spins about -z... rolling cleanly.
        check(Tracks::slip(glm::vec3(10, 0, 0), glm::vec3(0, 0, -10), glm::vec3(0, -1, 0)) < 0.01f, "a rolling wheel doesn't slip");
        check(Tracks::slip(glm::vec3(10, 0, 0), glm::vec3(0.0f), glm::vec3(0, -1, 0)) > Tracks::kSkidSlip, "a locked wheel skids");
        check(Tracks::slip(glm::vec3(0, 0, 9), glm::vec3(0.0f), glm::vec3(0, -1, 0)) > Tracks::kSkidSlip, "sliding sideways skids");
    }

    // --- In a game ---
    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    // Settings save and load.
    {
        WorldSettings& w = scene.world();
        check(w.injuries && !w.stamina && !w.fireSpread && w.acoustics && w.impactSounds,
              "starting settings: injuries, sound by material and room acoustics on; stamina and spreading fire off");
        w.stamina = true; w.fireSpread = true; w.acoustics = false;
        scene.environment().motionBlur = 0.5f;
        scene.environment().lightRays = 0.8f;
        scene.environment().autoExposure = false;
        const std::string text = Serializer::settingsToString(scene);
        Scene back;
        Serializer::settingsFromString(back, text);
        check(back.world().stamina && back.world().fireSpread && !back.world().acoustics, "workspace.Stamina / FireSpread / Acoustics are saved");
        check(back.environment().motionBlur == 0.5f && back.environment().lightRays == 0.8f && !back.environment().autoExposure,
              "Lighting.MotionBlur / LightRays / AutoExposure are saved");
        scene.environment() = Environment{};
        w.stamina = false; w.fireSpread = false; w.acoustics = true;
    }

    // Scripts can see and change them.
    {
        const bool ok = run(session,
            "assert(workspace.Injuries == true and workspace.Stamina == false)\n"
            "workspace.FireSpread = true\n"
            "game.Lighting.MotionBlur = 0.25\n"
            "assert(game.Lighting.MotionBlur == 0.25 and game.Lighting.AutoExposure == true)\n", err);
        check(ok, "workspace and Lighting settings from scripts" + (ok ? std::string() : ": " + err));
        check(scene.world().fireSpread && scene.environment().motionBlur == 0.25f, "...and they take effect");
        scene.world().fireSpread = false;
    }

    // workspace:FireBullet through a plank into a steel wall, hurting a dummy on the way.
    {
        SceneNode* plank = addPart(scene, "Plank", glm::vec3(10, 5, 300), glm::vec3(0.6f, 6, 6), Material::Wood);
        SceneNode* steel = addPart(scene, "Steel", glm::vec3(30, 5, 300), glm::vec3(1, 6, 6), Material::Metal);
        (void)plank; (void)steel;
        const bool ok = run(session,
            "local r = workspace:FireBullet(Vector3.new(0, 5, 300), Vector3.new(900, 0, 0))\n"
            "assert(#r.Hits == 2, 'hits: ' .. #r.Hits)\n"
            "assert(r.Hits[1].Instance.Name == 'Plank' and r.Hits[1].Pierced, 'through the plank')\n"
            "assert(r.Hits[2].Instance.Name == 'Steel' and not r.Hits[2].Pierced, 'stopped by the steel')\n"
            "assert(r.Hits[1].Material == 'Wood')\n"
            "assert(r.Time > 0 and #r.Path >= 2)\n"
            "local r2 = workspace:FireBullet(Vector3.new(0, 5, 300), Vector3.new(900, 0, 0), {Pierce = false})\n"
            "assert(#r2.Hits == 1 and r2.Hits[1].Instance.Name == 'Plank', 'Pierce = false stops at the plank')\n"
            "local r3 = workspace:FireBullet(Vector3.new(0, 5, 300), Vector3.new(900, 0, 0), {Ignore = {workspace.Plank}})\n"
            "assert(r3.Hits[1].Instance.Name == 'Steel', 'Ignore skips the plank')\n", err);
        check(ok, "workspace:FireBullet goes through wood, stops in steel, Pierce and Ignore work" + (ok ? std::string() : ": " + err));
        addPart(scene, "Pane", glm::vec3(10, 5, 340), glm::vec3(0.3f, 6, 6), Material::Glass);
        const bool glassOk = run(session,
            "local r = workspace:FireBullet(Vector3.new(0, 5, 340), Vector3.new(900, 0, 0))\n"
            "assert(#r.Hits >= 1 and r.Hits[1].Instance.Name == 'Pane' and r.Hits[1].Pierced, 'hits: ' .. #r.Hits)\n", err);
        check(glassOk, "a bullet goes through a window" + (glassOk ? std::string() : ": " + err));
        int holes = 0;
        for (const Particle& p : scene.particles().items()) if (p.kind == Particle::Splat && p.mark) ++holes;
        check(holes >= 2, "bullet holes are left behind (" + std::to_string(holes) + ")");
    }

    // A loose crate dropped on the floor: a knock to hear.
    {
        addPart(scene, "Floor", glm::vec3(60, 0, 60), glm::vec3(20, 1, 20), Material::Concrete);
        SceneNode* crate = addPart(scene, "Crate", glm::vec3(60, 8, 60), glm::vec3(2, 2, 2), Material::Wood);
        crate->anchored = false;
        Physics phys;
        std::vector<uint64_t> fallen;
        bool heard = false;
        for (int i = 0; i < 120 && !heard; ++i) {
            phys.gather(scene);
            phys.stepParts(scene, 1.0f / 60.0f, fallen);
            if (!phys.knocks.empty()) { Impacts::play(scene, phys.knocks, phys.smashes); heard = Impacts::lastCount() > 0; }
        }
        check(heard, "a crate landing on the floor makes a knock");
    }

    // Fire spreads from a torch to a wooden wall touching it, and chars it.
    {
        scene.world().fireSpread = true;
        SceneNode* torch = addPart(scene, "Torch", glm::vec3(-60, 3, -60), glm::vec3(1, 4, 1), Material::Metal);
        auto fire = std::make_unique<SceneNode>("Fire", NodeKind::Particles);
        fire->particle = particleDefaults(ParticleType::Fire);
        torch->addChild(std::move(fire));
        SceneNode* wall = addPart(scene, "WoodWall", glm::vec3(-58.5f, 3, -60), glm::vec3(2, 6, 6), Material::Wood);
        SceneNode* far = addPart(scene, "FarWood", glm::vec3(-40, 3, -60), glm::vec3(2, 6, 6), Material::Wood);
        const glm::vec3 before = wall->color;
        FireSpread::System fs;
        bool caught = false;
        for (int i = 0; i < 60 * 40 && !caught; ++i) {
            fs.update(scene, 1.0f / 60.0f, true, glm::vec3(0.0f));
            for (auto& c : wall->children) if (c->isParticles() && c->name == FireSpread::kFireName) caught = true;
        }
        check(caught, "fire spreads from a torch to the wooden wall touching it");
        for (int i = 0; i < 60 * 5; ++i) fs.update(scene, 1.0f / 60.0f, true, glm::vec3(0.0f));
        SceneNode* stillThere = scene.findById(wall->id);
        check(!stillThere || glm::length(stillThere->color) < glm::length(before) * 0.95f, "burning chars it darker");
        bool farCaught = false;
        for (auto& c : far->children) if (c->isParticles()) farCaught = true;
        check(!farCaught, "wood far away doesn't catch");
        fs.clear();
        scene.world().fireSpread = false;
    }

    // Marks: footprints and tyre tracks in the snow are marks, not blood.
    {
        ParticleSystem& fx = scene.particles();
        const size_t before = fx.items().size();
        fx.mark(glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), glm::vec3(0.36f, 0.02f, 0.78f), glm::vec3(0.6f), 2, 0.8f, 60.0f);
        check(fx.items().size() == before + 1 && fx.items().back().mark == 1 && fx.items().back().look == 2, "a footprint in the snow");
        check(fx.wetBloodAt(glm::vec3(0, 0, 0), 1.0f) == 0.0f, "walking through it doesn't give you bloody shoes");
    }

    session.stop();
    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " failure(s)\n";
    return failures ? 1 : 0;
}
