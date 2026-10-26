// The Level Generator window: makes a dungeon or an obby from a seed, checks it (rules,
// then a robot looks for a way from the spawn to the goal) and puts it in the game.
#include "Editor.h"
#include "LevelGen.h"
#include "panels/ViewportPanel.h"
#include "../scene/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>
#include <imgui.h>

using nlohmann::json;

namespace {

// Somewhere next to everything already in the game, so the level doesn't land inside it.
glm::vec3 besideEverything(Scene& scene, const LevelGen::Level& level, SceneNode* skip) {
    float maxX = -1e9f;
    bool any = false;
    scene.forEach([&](SceneNode* n) {
        if (!n->isPart() || n->internal) return;
        for (SceneNode* p = n; p; p = p->parent) if (p == skip) return;
        const glm::vec3 c = glm::vec3(n->worldMatrix()[3]);
        maxX = std::max(maxX, c.x + std::max({n->transform.scale.x, n->transform.scale.y, n->transform.scale.z}) / 2);
        any = true;
    });
    float minX = 0.0f;
    for (const auto& p : level.pieces) minX = std::min(minX, p.pos.x - p.size.x / 2);
    return glm::vec3(any ? std::ceil(maxX + 30.0f - minX) : 0.0f, 0.0f, 0.0f);
}

} // namespace

json Editor::generateLevel(const LevelGen::Settings& settings, int tries, bool buildIt, const glm::vec3* at, SceneNode* replace,
                           SceneNode** made) {
    if (made) *made = nullptr;
    tries = std::clamp(tries, 1, 50);
    json tried = json::array();
    LevelGen::Settings s = settings;
    LevelGen::Level good;
    bool found = false;
    for (int i = 0; i < tries && !found; ++i) {
        s.seed = settings.seed + (uint32_t)i;
        LevelGen::Level level = LevelGen::make(s);
        const bool rules = LevelGen::check(level);
        LevelGen::Route route;
        if (rules) route = LevelGen::walk(level);
        else route.status = "NotTested";
        if (rules && !route.ok) level.problems.push_back("The robot found no way from the spawn to the goal (" + route.status + ").");
        tried.push_back({{"seed", s.seed}, {"placed", level.placed}, {"problems", level.problems},
                         {"route", {{"status", route.status}, {"length", std::round(route.length * 10) / 10}, {"jumps", route.jumps}}},
                         {"ok", level.problems.empty()}});
        if (level.problems.empty() || i + 1 == tries) { good = level; found = level.problems.empty(); }
    }
    json d = {{"tried", tried}, {"ok", found}, {"seed", good.settings.seed}};
    if (!buildIt) return d;
    if (replace) m_scene->removeNode(replace);
    const glm::vec3 where = at ? *at : besideEverything(*m_scene, good, nullptr);
    SceneNode* model = LevelGen::build(*m_scene, good, where, std::string(LevelGen::kStyleNames[(int)good.settings.style]) + "Level");
    if (made) *made = model;
    d["at"] = {where.x, where.y, where.z};
    d["spawn"] = {where.x + good.spawn.x, where.y + good.spawn.y, where.z + good.spawn.z};
    d["goal"] = {where.x + good.goal.x, where.y + good.goal.y, where.z + good.goal.z};
    d["parts"] = (int)good.pieces.size();
    return d;
}

void Editor::renderLevelGenerator() {
    if (!m_showLevelGen) return;
    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Level Generator", &m_showLevelGen)) { ImGui::End(); return; }
    LevelGen::Settings& s = m_levelGen;
    ImGui::TextWrapped("Makes a level from a few rules. The same seed always makes the same level, so you can share a good one by its number.");
    ImGui::Spacing();
    int style = (int)s.style;
    if (ImGui::Combo("Kind", &style, LevelGen::kStyleNames, 2)) s.style = (LevelGen::Style)style;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Dungeon: rooms joined by corridors, with crates and coins.\nObby: platforms to jump along, with lava underneath.");
    int seed = (int)s.seed;
    ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - 70);
    if (ImGui::InputInt("##seed", &seed)) s.seed = (uint32_t)std::max(0, seed);
    ImGui::SameLine(0, 4);
    if (ImGui::Button("Random")) s.seed = (uint32_t)(ImGui::GetTime() * 1000.0) % 100000u;
    ImGui::SameLine();
    ImGui::TextUnformatted("Seed");
    const bool obby = s.style == LevelGen::Style::Obby;
    ImGui::SliderInt(obby ? "Platforms" : "Rooms", &s.count, 2, obby ? 60 : 30);
    if (!obby) {
        ImGui::DragIntRange2("Room size", &s.roomMin, &s.roomMax, 0.2f, 12, 60, "%d", "%d");
        ImGui::SliderFloat("Wall height", &s.wallHeight, 4.0f, 20.0f, "%.0f");
        ImGui::Checkbox("Crates", &s.props);
        ImGui::SameLine();
    }
    ImGui::Checkbox("Coins", &s.coins);
    ImGui::ColorEdit3(obby ? "Start colour" : "Floor colour", &s.floorColor.x, ImGuiColorEditFlags_NoInputs);
    if (!obby) { ImGui::SameLine(); ImGui::ColorEdit3("Wall colour", &s.wallColor.x, ImGuiColorEditFlags_NoInputs); }

    SceneNode* last = m_levelGenLast ? m_scene->findById(m_levelGenLast) : nullptr;
    ImGui::BeginDisabled(!last);
    ImGui::Checkbox("Replace the last one", &m_levelGenReplace);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Take away the level made last time before making the new one.");

    ImGui::Spacing();
    ImGui::BeginDisabled(m_playing);
    auto run = [&](int tries) {
        SceneNode* made = nullptr;
        m_levelGenResult = generateLevel(s, tries, true, nullptr, (m_levelGenReplace && last) ? last : nullptr, &made);
        if (made) {
            m_levelGenLast = made->id;
            m_scene->select(made);
            m_viewport->focusSelected();   // zoom over to look at it
            s.seed = m_levelGenResult.value("seed", s.seed);
        }
    };
    static bool testMake = std::getenv("GB_TEST_LEVELGEN") && std::string(std::getenv("GB_TEST_LEVELGEN")) == "make";   // (tests: press it once)
    if (ImGui::Button("Make it") || std::exchange(testMake, false)) run(1);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Makes the level with this seed, even if a check fails.");
    ImGui::SameLine();
    if (ImGui::Button("Find one that works")) run(20);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tries this seed and the next ones (up to 20) and keeps the first that passes every check.");
    ImGui::SameLine();
    if (ImGui::Button("Test 10 seeds")) m_levelGenResult = generateLevel(s, 10, false, nullptr, nullptr, nullptr), m_levelGenBrowse = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Checks the next 10 seeds without making anything: pick one from the list.");
    ImGui::EndDisabled();

    if (m_levelGenResult.is_object()) {
        ImGui::SeparatorText("Checks");
        const json& tried = m_levelGenResult["tried"];
        if (m_levelGenBrowse) {
            if (ImGui::BeginTable("##seeds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Seed");
                ImGui::TableSetupColumn("Works");
                ImGui::TableSetupColumn("Route");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (const auto& t : tried) {
                    ImGui::TableNextRow();
                    ImGui::PushID(t.value("seed", 0));
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", t.value("seed", 0));
                    ImGui::TableNextColumn();
                    const bool ok = t.value("ok", false);
                    ImGui::TextColored(ok ? ImVec4(0.4f, 0.9f, 0.5f, 1) : ImVec4(1, 0.5f, 0.4f, 1), "%s", ok ? "Yes" : "No");
                    if (!ok && ImGui::IsItemHovered() && !t["problems"].empty())
                        ImGui::SetTooltip("%s", t["problems"][0].get<std::string>().c_str());
                    ImGui::TableNextColumn();
                    const json& r = t["route"];
                    if (r.value("status", std::string()) == "Success")
                        ImGui::Text("%.0f studs, %d jumps", r.value("length", 0.0), r.value("jumps", 0));
                    else
                        ImGui::TextDisabled("-");
                    ImGui::TableNextColumn();
                    ImGui::BeginDisabled(m_playing);
                    if (ImGui::SmallButton("Make it")) {
                        s.seed = (uint32_t)t.value("seed", 0);
                        m_levelGenBrowse = false;
                        run(1);
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        } else if (!tried.empty()) {
            const json& t = tried.back();
            if (tried.size() > 1) ImGui::TextDisabled("Tried %d seeds.", (int)tried.size());
            ImGui::Text("Seed %d", t.value("seed", 0));
            const json& r = t["route"];
            const bool walked = r.value("status", std::string()) == "Success";
            if (t["problems"].empty()) {
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Everything fits, and nothing is inside anything else.");
            }
            for (const auto& p : t["problems"]) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", p.get<std::string>().c_str());
            if (walked)
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "A robot walked from the spawn to the goal: %.0f studs, %d jumps.",
                                   r.value("length", 0.0), r.value("jumps", 0));
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("The level goes next to your game, in a Model. Press Play and walk to the green goal.");
    ImGui::End();
}
