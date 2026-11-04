// Loading big maps in pieces (Roblox's StreamingEnabled): which pieces of the map a
// joined player gets, the setting itself, and the smaller scene sent when they join
// (net/Streaming.cpp; NetGame sends the pieces).
//
//   StreamingTests <a game file with a player>
#include "core/Log.h"
#include "game/GameSession.h"
#include "net/Streaming.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <algorithm>
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

SceneNode* find(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == name) found = n; });
    return found;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: StreamingTests <game file>\n"; return 2; }
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

    // --- The setting, from scripts ---
    std::string out = run(session, R"(
        print(workspace.StreamingEnabled, workspace.StreamingTargetRadius)
        workspace.StreamingEnabled = true
        workspace.StreamingTargetRadius = 100
        print(workspace.StreamingEnabled, workspace.StreamingTargetRadius)
        workspace.StreamingTargetRadius = 1
        print(workspace.StreamingTargetRadius)
        workspace.StreamingTargetRadius = 100
    )");
    check(out == "false 512.0true 100.032.0", "workspace.StreamingEnabled / StreamingTargetRadius (at least 32): " + out);

    // A map: a block near the middle, a part and a two-part tower far away, and a big
    // "City" group spread across the whole map (its insides become pieces).
    out = run(session, R"(
        local function part(name, pos, parent)
            local p = Instance.new('Part') p.Name = name p.Anchored = true p.Size = Vector3.new(4, 4, 4)
            p.Position = pos p.Parent = parent or workspace
            return p
        end
        part('NearBlock', Vector3.new(20, 5, 0))
        part('FarPart', Vector3.new(400, 5, 0))
        local tower = Instance.new('Model') tower.Name = 'Tower' tower.Parent = workspace
        part('TowerBase', Vector3.new(0, 5, 500), tower)
        part('TowerTop', Vector3.new(0, 9, 500), tower)
        local city = Instance.new('Model') city.Name = 'City' city.Parent = workspace
        part('CityNear', Vector3.new(-30, 5, 0), city)
        part('CityFar', Vector3.new(-600, 5, 0), city)
        part('EdgePart', Vector3.new(0, 5, -110))
    )");
    check(out.empty(), "built the map " + out);

    const float R = scene.world().streamRadius;
    std::vector<Streaming::Piece> pieces = Streaming::pieces(scene, R);
    auto pieceOf = [&](const std::string& name) -> const Streaming::Piece* {
        for (const Streaming::Piece& p : pieces) if (p.node->name == name) return &p;
        return nullptr;
    };
    check(pieceOf("NearBlock") && pieceOf("FarPart") && pieceOf("Tower"), "parts and Models in the Workspace are pieces");
    check(!pieceOf("TowerBase"), "a Model comes and goes as one piece (not part by part)");
    check(!pieceOf("City") && pieceOf("CityNear") && pieceOf("CityFar"), "a group bigger than the circle is opened up");
    bool characterIn = false;
    for (const Streaming::Piece& p : pieces) characterIn = characterIn || scene.isCharacterPart(p.node) || scene.isServiceFolder(p.node);
    check(!characterIn, "characters and services are never pieces");

    const glm::vec3 at(0.0f, 5.0f, 0.0f);
    auto wants = [&](const std::string& name, bool have) { const Streaming::Piece* p = pieceOf(name); return p && Streaming::wanted(*p, at, R, have); };
    check(wants("NearBlock", false) && wants("CityNear", false), "near pieces are sent");
    check(!wants("FarPart", false) && !wants("Tower", false) && !wants("CityFar", false), "far pieces aren't");
    check(!wants("EdgePart", false) && wants("EdgePart", true), "just past the edge: not sent yet, but kept if they have it (no flickering)");

    // --- The scene a joining player gets ---
    std::unordered_set<uint64_t> leave;
    for (const Streaming::Piece& p : pieces) if (!Streaming::wanted(p, at, R, false)) leave.insert(p.node->id);
    const std::string full = Streaming::sceneWithout(scene, {});
    const std::string small = Streaming::sceneWithout(scene, leave);
    check(small.size() < full.size(), "it's smaller (" + std::to_string(small.size()) + " vs " + std::to_string(full.size()) + " bytes)");
    Scene joined;
    check(Serializer::loadScene(joined, small, &err), "and still loads " + err);
    check(find(joined, "NearBlock") && find(joined, "CityNear") && find(joined, "City"), "with the near pieces (and the group around them)");
    check(!find(joined, "FarPart") && !find(joined, "TowerTop") && !find(joined, "CityFar"), "without the far ones");
    check(joined.world().streaming && joined.world().streamRadius == 100.0f, "the setting is saved with the game");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
