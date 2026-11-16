// Stress test (Test menu > Stress test...): like Roblox's Performance Simulator, but it also
// pushes the physics. On a private copy of the game (yours is never touched) it:
//   - adds up roughly how much memory the game needs on a device, and compares that with what
//     the device has left after its own system and the apps open in the background (light,
//     normal or heavy). With StreamingEnabled on, only the part of the map near the spawn counts.
//   - drops a pile of extra loose crates (0 to 3000) into a walled pit next to the map and
//     times the physics, step by step, then guesses the time on the device (rough: phones
//     are several times slower than a computer) and how many loose parts would still fit.
//   - times raycasts (how many fit in a millisecond on the device), and works out how much
//     the moving parts cost the network for each joined player.
// It ends with plain warnings: what is too much and what to do.
//
// The same check is the stress_test AI tool (AiToolsRun.cpp), so Fyxa and connected apps can
// test a game the way a person would, then fix it.
#include "Editor.h"
#include "../core/Log.h"
#include "../net/Streaming.h"
#include "../renderer/Mesh.h"
#include "../renderer/MeshLibrary.h"
#include "../renderer/Textures.h"
#include "../scene/Physics.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <set>

using json = nlohmann::json;

namespace {

struct Device {
    const char* key;
    const char* name;
    const char* plain;  // the name in a sentence
    int   ramMb;        // all of its memory
    int   systemMb;     // what its own system always keeps
    int   backgroundMb[3];   // other apps left open: light, normal, heavy
    float slower;       // rough: how many times slower than this computer at physics
    int   fps;          // the frame rate it should keep
};
const Device kDevices[] = {
    {"cheap_phone", "Cheap phone (2 GB)", "cheap phone", 2048, 900, {0, 250, 500}, 6.0f, 30},
    {"mid_phone", "Mid-range phone (4 GB)", "mid-range phone", 4096, 1400, {0, 600, 1200}, 3.0f, 60},
    {"old_laptop", "Old laptop (8 GB)", "old laptop", 8192, 2500, {0, 1500, 3000}, 2.5f, 30},
};
constexpr int kDeviceCount = (int)(sizeof(kDevices) / sizeof(kDevices[0]));
const char* const kBackground[] = {"light", "normal", "heavy"};

constexpr double kEngineMb    = 220.0;           // the Player program itself, before any game
constexpr double kPartBytes   = 2048.0;          // one part (its settings, its place in the physics)
constexpr double kScriptBytes = 64.0 * 1024.0;   // one script's share of Lua
constexpr double kSoundBytes  = 1.7 * 1024 * 1024;   // one sound, unpacked (about 10 seconds)
constexpr double kPictureGuess = 512.0 * 512.0;  // pixels, for a picture that isn't loaded yet
constexpr double kNetBytesPerPart = 160.0;       // one moving part in one network update
constexpr double kNetUpdates  = 20.0;            // network updates a second (NetGame kTickRate)
constexpr float  kStep        = 1.0f / 60.0f;    // a physics step
constexpr double kPhysicsShare = 0.3;            // physics may use this much of a frame
constexpr int    kMaxCrates   = 3000;

double ms(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - a).count();
}
double round1(double v) { return std::round(v * 10.0) / 10.0; }
double round2(double v) { return std::round(v * 100.0) / 100.0; }
const char* verdict(double have, double limit) { return have <= limit * 0.75 ? "ok" : have <= limit ? "close" : "over"; }

bool loose(Scene& scene, const SceneNode* n) {   // (as PhysicsReplay: the parts the physics moves)
    if (!n->isPart() || n->internal || n->anchored || n->inViewportFrame() || scene.isCharacterPart(n)) return false;
    for (const SceneNode* p = n; p; p = p->parent) {
        if (!p->visible) return false;
        if (p != n && p->isPart() && !p->anchored) return false;
    }
    return true;
}

glm::vec3 spawnOf(Scene& scene) {
    glm::vec3 at(0.0f);
    bool found = false;
    scene.forEach([&](SceneNode* n) {
        if (!found && n->isSpawnLocation()) { at = n->transform.position; found = true; }
    });
    return at;
}

// Roughly how much memory the game needs, piece by piece. `keep` says which parts a player near
// the spawn has (all of them without streaming).
struct Memory {
    double pictures = 0, meshes = 0, sounds = 0, parts = 0, scripts = 0;
    int pictureCount = 0, meshCount = 0, soundCount = 0, partCount = 0, scriptCount = 0;
    double totalMb() const { return kEngineMb + (pictures + meshes + sounds + parts + scripts) / (1024.0 * 1024.0); }
};
Memory memoryOf(Scene& scene, const std::function<bool(const SceneNode*)>& keep) {
    Memory m;
    std::set<std::string> pictures, sounds;
    std::set<const Mesh*> meshes;
    std::map<std::string, const SceneNode*> variants;
    if (SceneNode* ms = scene.serviceFolder("MaterialService", false))
        for (const auto& c : ms->children)
            if (c->isMaterialVariant()) variants.emplace(c->name, c.get());
    scene.forEach([&](SceneNode* n) {
        if (n->internal || !keep(n)) return;
        if (n->isPart()) {
            ++m.partCount;
            meshes.insert(n->mesh.get());
            if (!n->materialVariant.empty()) {
                auto v = variants.find(n->materialVariant);
                if (v != variants.end())
                    for (const std::string* p : {&v->second->variant.colorMap, &v->second->variant.normalMap, &v->second->variant.ormMap,
                                                 &v->second->variant.roughnessMap, &v->second->variant.metalnessMap})
                        if (!p->empty()) pictures.insert(*p);
            }
        }
        if (!n->texture.empty()) pictures.insert(n->texture);
        if (!n->tshirt.empty()) pictures.insert(n->tshirt);
        if (!n->gui.image.empty()) pictures.insert(n->gui.image);
        if (n->isSound() && !n->soundId.empty()) sounds.insert(n->soundId);
        if (n->isScript()) ++m.scriptCount;
    });
    for (const std::string& id : pictures) {
        int w = 0, h = 0;
        const double pixels = Textures::size(id, w, h) && w > 0 && h > 0 ? (double)w * h : kPictureGuess;
        m.pictures += pixels * 4.0 * 1.34;   // (RGBA, plus the smaller copies for far away)
    }
    for (const Mesh* mesh : meshes) {
        if (!mesh) continue;
        // On the graphics card and once more in memory (physics and picking use the corners).
        m.meshes += 2.0 * std::max<double>((double)mesh->bytes(), mesh->indexCount() * 40.0);
    }
    m.pictureCount = (int)pictures.size();
    m.meshCount = (int)meshes.size();
    m.soundCount = (int)sounds.size();
    m.sounds = sounds.size() * kSoundBytes;
    m.parts = m.partCount * kPartBytes;
    m.scripts = m.scriptCount * kScriptBytes;
    return m;
}

// A walled pit next to the map, with `crates` loose crates stacked in it.
void addCratePit(Scene& world, int crates) {
    glm::vec3 lo(1e30f), hi(-1e30f);
    bool any = false;
    world.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->internal) return;
        const AABB b = Physics::worldBounds(n);
        lo = glm::min(lo, b.min);
        hi = glm::max(hi, b.max);
        any = true;
    });
    if (!any) { lo = glm::vec3(0.0f); hi = glm::vec3(0.0f); }
    const int side = std::max(2, (int)std::ceil(std::sqrt((double)std::min(crates, 100))));   // crates a row
    const float gap = 1.25f, inner = side * gap + 1.0f;
    const glm::vec3 c(hi.x + inner * 0.5f + 20.0f, std::max(lo.y, -50.0f), (lo.z + hi.z) * 0.5f);
    auto block = [&](const std::string& name, glm::vec3 pos, glm::vec3 size, bool anchored) {
        SceneNode* n = world.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
        n->transform.position = pos;
        n->transform.scale = size;
        n->anchored = anchored;
        return n;
    };
    const int layers = (crates + side * side - 1) / std::max(1, side * side);
    const float wallH = std::max(4.0f, layers * gap + 2.0f);
    block("StressFloor", c, glm::vec3(inner + 2.0f, 1.0f, inner + 2.0f), true);
    block("StressWall", c + glm::vec3(inner * 0.5f + 0.5f, wallH * 0.5f, 0.0f), glm::vec3(1.0f, wallH, inner + 2.0f), true);
    block("StressWall", c - glm::vec3(inner * 0.5f + 0.5f, -wallH * 0.5f, 0.0f), glm::vec3(1.0f, wallH, inner + 2.0f), true);
    block("StressWall", c + glm::vec3(0.0f, wallH * 0.5f, inner * 0.5f + 0.5f), glm::vec3(inner + 2.0f, wallH, 1.0f), true);
    block("StressWall", c - glm::vec3(0.0f, -wallH * 0.5f, inner * 0.5f + 0.5f), glm::vec3(inner + 2.0f, wallH, 1.0f), true);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> jitter(-0.1f, 0.1f);
    for (int i = 0; i < crates; ++i) {
        const int layer = i / (side * side), k = i % (side * side);
        const glm::vec3 at = c + glm::vec3((k % side - (side - 1) * 0.5f) * gap + jitter(rng), 1.5f + layer * gap,
                                           (k / side - (side - 1) * 0.5f) * gap + jitter(rng));
        block("StressCrate", at, glm::vec3(1.0f), false);
    }
}

struct PhysicsRun { double avgMs = 0, worstMs = 0; int loose = 0, moving = 0; };
PhysicsRun simulate(Scene& world, float seconds) {
    PhysicsRun r;
    world.forEach([&](SceneNode* n) { if (loose(world, n)) ++r.loose; });
    Physics physics;
    const int steps = std::max(1, (int)std::lround(seconds / kStep));
    double total = 0.0;
    for (int i = 0; i < steps; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        physics.gather(world);
        std::vector<uint64_t> fallen;
        physics.stepParts(world, kStep, fallen);
        const double t = ms(t0);
        total += t;
        r.worstMs = std::max(r.worstMs, t);
        for (uint64_t id : fallen)
            if (SceneNode* n = world.findById(id)) world.removeNode(n);
    }
    r.avgMs = total / steps;
    world.forEach([&](SceneNode* n) {
        if (loose(world, n) && (glm::length(n->velocity) > 0.05f || glm::length(n->angularVelocity) > 0.05f)) ++r.moving;
    });
    return r;
}

} // namespace

int Editor::stressDeviceIndex(const std::string& key) {
    for (int i = 0; i < kDeviceCount; ++i)
        if (key == kDevices[i].key) return i;
    return -1;
}

json Editor::stressTest(int device, int background, int crates, float seconds) {
    const Device& d = kDevices[std::clamp(device, 0, kDeviceCount - 1)];
    background = std::clamp(background, 0, 2);
    crates = std::clamp(crates, 0, kMaxCrates);
    seconds = std::clamp(seconds, 0.5f, 5.0f);
    const auto started = std::chrono::steady_clock::now();
    json warnings = json::array();

    // Memory, on the game as it is.
    Scene world;
    if (!Serializer::loadScene(world, Serializer::saveScene(*m_scene)))
        return {{"error", "The game couldn't be copied for the test."}};
    const WorldSettings& ws = world.world();
    std::set<const SceneNode*> streamedOut;
    if (ws.streaming) {
        const glm::vec3 spawn = spawnOf(world);
        for (const Streaming::Piece& p : Streaming::pieces(world, ws.streamRadius))
            if (!Streaming::wanted(p, spawn, ws.streamRadius, false)) streamedOut.insert(p.node);
    }
    auto keep = [&](const SceneNode* n) {
        for (const SceneNode* p = n; p; p = p->parent)
            if (streamedOut.count(p)) return false;
        return true;
    };
    const Memory mem = memoryOf(world, keep);
    const double budgetMb = d.ramMb - d.systemMb - d.backgroundMb[background];
    const double useMb = mem.totalMb();
    const std::string memVerdict = verdict(useMb, budgetMb);
    json memRows = json::array({
        json{{"what", "Guts&Bolts itself"}, {"mb", round1(kEngineMb)}},
        json{{"what", "Pictures"}, {"count", mem.pictureCount}, {"mb", round1(mem.pictures / 1048576.0)}},
        json{{"what", "Meshes"}, {"count", mem.meshCount}, {"mb", round1(mem.meshes / 1048576.0)}},
        json{{"what", "Sounds"}, {"count", mem.soundCount}, {"mb", round1(mem.sounds / 1048576.0)}},
        json{{"what", "Parts"}, {"count", mem.partCount}, {"mb", round1(mem.parts / 1048576.0)}},
        json{{"what", "Scripts"}, {"count", mem.scriptCount}, {"mb", round1(mem.scripts / 1048576.0)}},
    });
    json memory = {{"estimate_mb", round1(useMb)}, {"budget_mb", budgetMb}, {"device_ram_mb", d.ramMb},
                   {"background", kBackground[background]}, {"verdict", memVerdict}, {"rows", memRows},
                   {"streaming", ws.streaming}};
    if (ws.streaming) memory["streamed_out_pieces"] = (int)streamedOut.size();
    if (memVerdict != "ok") {
        std::string fix = "The game needs about " + std::to_string((int)useMb) + " MB but a " + d.plain + " with " +
                          kBackground[background] + " apps in the background has about " + std::to_string((int)budgetMb) +
                          " MB left. ";
        double biggest = std::max({mem.pictures, mem.meshes, mem.sounds, mem.parts});
        fix += biggest == mem.pictures ? "Pictures take the most: use smaller ones (512 x 512 is plenty for most) and reuse the same ones."
             : biggest == mem.meshes   ? "Meshes take the most: use simpler meshes, and the same mesh many times instead of many different ones."
             : biggest == mem.sounds   ? "Sounds take the most: use fewer, shorter sounds."
                                       : "Parts take the most: join small parts into meshes and delete what players never see.";
        if (!ws.streaming) fix += " Turning on StreamingEnabled (Workspace) loads only the map near each player.";
        memory["fix"] = fix;
        warnings.push_back(fix);
    }

    // Physics: the game alone, then with the crates.
    const PhysicsRun base = simulate(world, std::min(seconds, 1.0f));
    PhysicsRun with = base;
    if (crates > 0) {
        Scene pile;
        Serializer::loadScene(pile, Serializer::saveScene(*m_scene));
        addCratePit(pile, crates);
        with = simulate(pile, seconds);
    }
    const double frameBudget = 1000.0 / d.fps * kPhysicsShare;   // ms for physics in one frame on the device
    const double onDevice = with.avgMs * d.slower;
    const std::string physVerdict = verdict(onDevice, frameBudget);
    const double tickBudget = 1000.0 / 60.0 * 0.5;                // the host computer: half of a 60 a second tick
    const std::string tickVerdict = verdict(with.avgMs, tickBudget);
    json physics = {{"crates", crates}, {"seconds", seconds}, {"loose_parts", with.loose},
                    {"game_alone_ms", round2(base.avgMs)}, {"with_crates_ms", round2(with.avgMs)}, {"worst_step_ms", round2(with.worstMs)},
                    {"on_device_ms", round2(onDevice)}, {"device_budget_ms", round2(frameBudget)}, {"verdict", physVerdict},
                    {"server_tick_ms", round2(with.avgMs)}, {"server_budget_ms", round2(tickBudget)}, {"server_verdict", tickVerdict}};
    if (crates > 0 && with.loose > base.loose && with.avgMs > base.avgMs) {
        const double perPart = (with.avgMs - base.avgMs) / (with.loose - base.loose);
        const double room = frameBudget / d.slower - base.avgMs;
        physics["most_loose_parts"] = std::max(0, base.loose + (int)(room / std::max(perPart, 1e-6)));
    }
    if (physVerdict != "ok") {
        const std::string w = "Physics with " + std::to_string(with.loose) + " loose parts would take about " + std::to_string((int)std::round(onDevice)) +
                              " ms a frame on a " + std::string(d.plain) + " (it has about " + std::to_string((int)std::round(frameBudget)) +
                              " ms for physics). Anchor parts that never move, weld groups of small parts into one, and clean up "
                              "old debris (Debris:AddItem).";
        physics["fix"] = w;
        warnings.push_back(w);
    }
    if (tickVerdict != "ok")
        warnings.push_back("The host would spend " + std::to_string((int)std::round(with.avgMs)) + " ms of each 16 ms tick on physics: "
                           "the server will fall behind and everyone will lag. Fewer loose parts at once.");

    // Queries: raycasts straight down over the map.
    glm::vec3 lo(1e30f), hi(-1e30f);
    world.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->internal) return;
        const AABB b = Physics::worldBounds(n);
        lo = glm::min(lo, b.min);
        hi = glm::max(hi, b.max);
    });
    if (lo.x > hi.x) { lo = glm::vec3(-50.0f); hi = glm::vec3(50.0f); }
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> ux(lo.x, hi.x), uz(lo.z, hi.z);
    constexpr int kRays = 500;
    int hits = 0;
    const auto r0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kRays; ++i) {
        float dist = 0.0f;
        if (Physics::raycast(world, glm::vec3(ux(rng), hi.y + 5.0f, uz(rng)), glm::vec3(0, -1, 0), &dist)) ++hits;
    }
    const double rayUs = ms(r0) * 1000.0 / kRays;
    const int perMs = (int)(1000.0 / std::max(rayUs * d.slower, 0.01));
    json queries = {{"raycasts", kRays}, {"hit", hits}, {"microseconds_each", round2(rayUs)}, {"per_ms_on_device", perMs}};
    if (perMs < 20) {
        const std::string w = "Raycasts are slow in this map: only about " + std::to_string(perMs) + " fit in a millisecond on a " +
                              d.plain + ". Use RaycastParams to skip what you don't need, and don't cast hundreds every frame.";
        queries["fix"] = w;
        warnings.push_back(w);
    }

    // Network: what the moving parts cost each joined player.
    const double kbps = with.moving * kNetBytesPerPart * kNetUpdates / 1024.0;
    const std::string netVerdict = kbps <= 30 ? "ok" : kbps <= 60 ? "close" : "over";
    json network = {{"moving_parts", with.moving}, {"kb_per_second_per_player", round1(kbps)}, {"verdict", netVerdict}};
    if (netVerdict != "ok") {
        const std::string w = std::to_string(with.moving) + " parts were still moving at the end: sending them to every player is about " +
                              std::to_string((int)kbps) + " KB a second each, too much for phone data. Let piles settle, "
                              "or remove parts once they stop mattering.";
        network["fix"] = w;
        warnings.push_back(w);
    }

    return {{"device", d.name}, {"memory", memory}, {"physics", physics}, {"queries", queries}, {"network", network},
            {"warnings", warnings}, {"took_ms", (int)ms(started)},
            {"note", "Done on a copy: your game didn't change. The device numbers are careful guesses (a real phone is several "
                     "times slower than this computer); play it on a real phone before a big launch."}};
}

void Editor::renderStressTest() {
    if (!m_showStressTest) return;
    ImGui::SetNextWindowSize(ImVec2(540, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Stress test", &m_showStressTest)) { ImGui::End(); return; }
    ImGui::TextWrapped("Pretend to be a weaker device: check the memory your game needs, then drop a pile of crates in and time "
                       "the physics. It runs on a copy, so your game doesn't change.");
    ImGui::Spacing();
    for (int i = 0; i < kDeviceCount; ++i)
        if (ImGui::RadioButton(kDevices[i].name, m_stressDevice == i)) m_stressDevice = i;
    ImGui::SetNextItemWidth(160);
    ImGui::Combo("Apps open in the background", &m_stressBackground, "Light\0Normal\0Heavy\0");
    ImGui::SetNextItemWidth(160);
    ImGui::SliderInt("Extra crates", &m_stressCrates, 0, kMaxCrates);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Seconds", &m_stressSeconds, 0.5f, 5.0f, "%.1f");
    if (ImGui::Button("Run the stress test")) {
        m_stressResult = stressTest(m_stressDevice, m_stressBackground, m_stressCrates, m_stressSeconds);
        Log::system("Stress test done in " + std::to_string(m_stressResult.value("took_ms", 0)) + " ms.");
    }
    const json& r = m_stressResult;
    if (r.is_object() && r.contains("memory")) {
        auto color = [](const std::string& v) {
            return v == "ok" ? ImVec4(0.35f, 0.85f, 0.45f, 1) : v == "close" ? ImVec4(0.95f, 0.75f, 0.25f, 1) : ImVec4(1, 0.4f, 0.35f, 1);
        };
        // Coloured text that wraps at the window's edge.
        auto wrapped = [](const ImVec4& c, const char* fmt, auto... args) {
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextWrapped(fmt, args...);
            ImGui::PopStyleColor();
        };
        const json& m = r["memory"];
        ImGui::SeparatorText("Memory");
        wrapped(color(m.value("verdict", "")), "Needs about %.0f MB. The device has %.0f MB free with %s apps in the background.",
                m.value("estimate_mb", 0.0), m.value("budget_mb", 0.0), m.value("background", "").c_str());
        for (const json& row : m["rows"])
            ImGui::BulletText("%s: %.1f MB%s", row.value("what", "").c_str(), row.value("mb", 0.0),
                              row.contains("count") ? (" (" + std::to_string(row.value("count", 0)) + ")").c_str() : "");
        if (m.value("streaming", false)) ImGui::TextDisabled("Streaming is on: only the map near the spawn is counted.");
        const json& p = r["physics"];
        ImGui::SeparatorText("Physics");
        ImGui::Text("%d loose parts. On this computer: %.2f ms a step (worst %.2f).", p.value("loose_parts", 0),
                    p.value("with_crates_ms", 0.0), p.value("worst_step_ms", 0.0));
        wrapped(color(p.value("verdict", "")), "On the device: about %.1f ms (it has %.1f ms for physics).",
                           p.value("on_device_ms", 0.0), p.value("device_budget_ms", 0.0));
        wrapped(color(p.value("server_verdict", "")), "Server tick: %.2f ms of %.1f ms.", p.value("server_tick_ms", 0.0),
                           p.value("server_budget_ms", 0.0));
        if (p.contains("most_loose_parts")) ImGui::Text("Loose parts that would still fit: about %d.", p.value("most_loose_parts", 0));
        const json& q = r["queries"];
        ImGui::SeparatorText("Raycasts and network");
        ImGui::Text("A raycast: %.2f microseconds here, about %d per ms on the device.", q.value("microseconds_each", 0.0),
                    q.value("per_ms_on_device", 0));
        const json& n = r["network"];
        wrapped(color(n.value("verdict", "")), "%d parts still moving: about %.1f KB a second for each player.",
                           n.value("moving_parts", 0), n.value("kb_per_second_per_player", 0.0));
        ImGui::SeparatorText("What to fix");
        if (r["warnings"].empty()) wrapped(color("ok"), "Nothing: it fits this device.");
        for (const json& w : r["warnings"]) { ImGui::Bullet(); ImGui::TextWrapped("%s", w.get<std::string>().c_str()); }
        ImGui::Spacing();
        ImGui::TextDisabled("Device numbers are careful guesses. Try a real phone before a big launch.");
    } else if (r.is_object() && r.contains("error")) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%s", r.value("error", "").c_str());
    }
    ImGui::End();
}
