// MemoryStoreService: the C++ MemoryStore (the same rules as worker/memorystore.js) and the
// Lua side of it, played offline (kept in this program).
//
//   MemoryStoreTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "online/MemoryStore.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Run some Lua; false if it didn't compile or hit an error while running (errors only get logged).
bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: MemoryStoreTests <game file>\n"; return 2; }

    // The store itself.
    {
        MemoryStore m;
        for (auto [key, score] : {std::pair<const char*, int>{"ann", 30}, {"ben", 50}, {"cat", 10}, {"dan", 50}})
            m.run("mem.set", {{"map", "Scores"}, {"sorted", true}, {"key", key}, {"value", score}, {"sortKey", score}, {"expiration", 600}}, 0);
        json top = m.run("mem.range", {{"map", "Scores"}, {"sorted", true}, {"direction", "Descending"}, {"count", 3}}, 1);
        check(top["items"].size() == 3 && top["items"][0]["key"] == "dan" && top["items"][1]["key"] == "ben" && top["items"][2]["key"] == "ann",
              "sorted maps come back in order (ties by key)");
        json low = m.run("mem.range", {{"map", "Scores"}, {"sorted", true}, {"count", 10}, {"lower", {{"sortKey", 10}}}}, 1);
        check(low["items"].size() == 3 && low["items"][0]["key"] == "ann", "a lower bound leaves its items out");
        check(m.run("mem.get", {{"map", "Scores"}, {"key", "ann"}}, 1)["found"] == false, "hash maps and sorted maps are different");
        check(m.run("mem.set", {{"map", "M"}, {"key", "k"}, {"value", 1}}, 0)["ok"] == false, "an expiration is needed");
        check(m.run("mem.set", {{"map", "M"}, {"key", "k"}, {"value", 1}, {"expiration", 10}}, 100)["created"] == true, "SetAsync says it's new");
        check(m.run("mem.get", {{"map", "M"}, {"key", "k"}}, 111)["found"] == false, "values go away when they expire");

        MemoryStore q;
        for (auto [value, priority] : {std::pair<const char*, int>{"a", 0}, {"b", 5}, {"c", 0}})
            q.run("mem.qadd", {{"queue", "Match"}, {"value", value}, {"priority", priority}, {"expiration", 600}}, 0);
        json r1 = q.run("mem.qread", {{"queue", "Match"}, {"count", 2}, {"invisible", 30}}, 0);
        check(r1["items"] == json::array({"b", "a"}), "queues: highest priority first");
        check(q.run("mem.qread", {{"queue", "Match"}, {"count", 2}, {"all", true}}, 1)["items"].empty(), "all or nothing");
        q.run("mem.qremove", {{"queue", "Match"}, {"id", r1["id"]}}, 2);
        check(q.run("mem.qread", {{"queue", "Match"}, {"count", 5}}, 40)["items"] == json::array({"c"}), "unremoved items come back");

        MemoryStore full;
        const std::string big(30 * 1024, 'x');
        full.run("mem.set", {{"map", "M"}, {"key", "a"}, {"value", big}, {"expiration", 60}}, 0);
        full.run("mem.set", {{"map", "M"}, {"key", "b"}, {"value", big}, {"expiration", 60}}, 0);
        check(full.run("mem.set", {{"map", "M"}, {"key", "c"}, {"value", big}, {"expiration", 60}}, 0)["ok"] == false, "64 KB with no players");
        full.setPlayers(40);
        check(full.run("mem.set", {{"map", "M"}, {"key", "c"}, {"value", big}, {"expiration", 60}}, 0)["ok"] == true, "+1 KB per player");
    }

    // Scripts.
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

    check(run(session,
              "local MS = game:GetService('MemoryStoreService') "
              "local map = MS:GetSortedMap('Scores') "
              "assert(map:SetAsync('ann', { score = 30 }, 600, 30) == true) "
              "assert(map:SetAsync('ann', { score = 31 }, 600, 31) == false) "
              "map:SetAsync('ben', { score = 50 }, 600, 50) "
              "local v, s = map:GetAsync('ann') assert(v.score == 31 and s == 31) "
              "local top = map:GetRangeAsync(Enum.SortDirection.Descending, 10) "
              "assert(#top == 2 and top[1].key == 'ben' and top[2].value.score == 31) "
              "local nv, ns = map:UpdateAsync('ann', function(old, sk) return { score = old.score + 1 }, sk + 1 end, 600) "
              "assert(nv.score == 32 and ns == 32) "
              "map:RemoveAsync('ann') assert(map:GetAsync('ann') == nil)", err),
          "sorted maps from a script " + err);
    check(run(session,
              "local h = game:GetService('MemoryStoreService'):GetHashMap('Coins') "
              "h:SetAsync('pot', 5, 60) "
              "assert(h:UpdateAsync('pot', function(v) return (v or 0) + 1 end, 60) == 6) "
              "assert(h:GetAsync('pot') == 6) "
              "assert(not pcall(function() h:SetAsync('pot', 1) end))", err),
          "hash maps from a script; no expiration is an error " + err);
    check(run(session,
              "local q = game:GetService('MemoryStoreService'):GetQueue('Match', 30) "
              "q:AddAsync('a', 60) q:AddAsync('b', 60, 5) "
              "local items, id = q:ReadAsync(2, true, 0) "
              "assert(#items == 2 and items[1] == 'b' and id) "
              "q:RemoveAsync(id) "
              "local none = q:ReadAsync(1, false, 0) assert(#none == 0)", err),
          "queues from a script " + err);

    session.stop();
    ImGui::DestroyContext();
    std::cout << (failures ? "memory store tests FAILED\n" : "memory store tests passed\n");
    return failures ? 1 : 0;
}
