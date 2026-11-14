// "Test like a phone" (Test menu > Test like a phone...): playtest the game with a cheap phone's
// graphics settings, frame-rate cap and screen sharpness, and check the game against what such a
// phone can handle (parts, triangles, lights, particles, sounds, scripts). Studio's own settings
// come back the moment the playtest stops. Your computer is still faster than a real phone, so
// the budgets matter more than the frame rate you see.
//
// The same budget check is the phone_check AI tool (AiToolsRun.cpp), so Fyxa can say what to fix.
#include "Editor.h"
#include "../core/Log.h"
#include "../core/Settings.h"
#include "../renderer/Mesh.h"
#include "../scene/Scene.h"

#include <imgui.h>

using json = nlohmann::json;

namespace {
// What a device can handle. Numbers are rough limits for a smooth 30+ FPS, picked to be careful.
struct PhoneProfile {
    const char* name;
    const char* about;
    int quality;        // GraphicsSettings quality preset
    float renderScale;  // how sharp the picture is
    int fpsCap;
    int parts, triangles, unanchored, lights, emitters, sounds, scripts;
};
const PhoneProfile kProfiles[] = {
    {"Cheap phone", "A 3-4 year old budget Android phone.", GraphicsSettings::VeryLow, 0.5f, 30,
     3000, 300000, 150, 4, 8, 20, 60},
    {"Mid-range phone", "A newer phone or an older iPhone.", GraphicsSettings::Low, 0.7f, 60,
     8000, 800000, 400, 8, 20, 40, 120},
    {"Old laptop", "A laptop with no graphics card.", GraphicsSettings::Low, 0.7f, 30,
     12000, 1500000, 600, 12, 30, 60, 200},
};
constexpr int kProfileCount = (int)(sizeof(kProfiles) / sizeof(kProfiles[0]));

struct Count { int parts = 0, triangles = 0, unanchored = 0, lights = 0, emitters = 0, sounds = 0, scripts = 0; };
Count countScene(Scene& scene) {
    Count c;
    scene.forEach([&](SceneNode* n) {
        if (n->internal) return;
        if (n->isPart()) {
            ++c.parts;
            if (!n->anchored) ++c.unanchored;
            if (n->mesh) c.triangles += n->mesh->indexCount() / 3;
        }
        if (n->isLight() && n->enabled) ++c.lights;
        if (n->isParticles() && n->enabled) ++c.emitters;
        if (n->isSound()) ++c.sounds;
        if (n->isScript()) ++c.scripts;
    });
    return c;
}
// One row: what, how many, the limit, and what to do when it's over.
struct Row { const char* what; int have, limit; const char* fix; };
std::vector<Row> rowsFor(const Count& c, const PhoneProfile& p) {
    return {
        {"Parts", c.parts, p.parts, "Join small parts into one mesh, or delete ones players never see."},
        {"Triangles", c.triangles, p.triangles, "Use simpler meshes (fewer faces), especially ones used many times."},
        {"Unanchored parts", c.unanchored, p.unanchored, "Anchor parts that never need to move: physics costs the most."},
        {"Lights", c.lights, p.lights, "Phones only draw a few lights. Keep the ones that matter, switch off the rest."},
        {"Particle emitters", c.emitters, p.emitters, "Fewer emitters, or lower Rate on each one."},
        {"Sounds", c.sounds, p.sounds, "Reuse one sound instead of copying it into every part."},
        {"Scripts", c.scripts, p.scripts, "Use one script that handles many parts instead of a script in each part."},
    };
}
const char* verdict(int have, int limit) { return have <= limit * 3 / 4 ? "ok" : have <= limit ? "close" : "over"; }
} // namespace

json Editor::phoneCheck(int profile) const {
    const PhoneProfile& p = kProfiles[std::clamp(profile, 0, kProfileCount - 1)];
    const Count c = countScene(*m_scene);
    json rows = json::array();
    int over = 0;
    for (const Row& r : rowsFor(c, p)) {
        const std::string v = verdict(r.have, r.limit);
        if (v == "over") ++over;
        json row = {{"what", r.what}, {"count", r.have}, {"limit", r.limit}, {"verdict", v}};
        if (v != "ok") row["fix"] = r.fix;
        rows.push_back(row);
    }
    return {{"device", p.name}, {"about", p.about}, {"over", over}, {"checks", rows},
            {"note", "Rough limits for a smooth 30+ FPS on that device. Studio's playtest can use the device's graphics settings: "
                     "Test menu > Test like a phone."}};
}

void Editor::startPhoneTest(int profile) {
    if (m_playing) return;
    const PhoneProfile& p = kProfiles[std::clamp(profile, 0, kProfileCount - 1)];
    GraphicsSettings& gs = GraphicsSettings::get();
    m_phoneSaved = std::make_unique<GraphicsSettings>(gs);
    gs.applyPreset(p.quality);
    gs.renderScale = p.renderScale;
    gs.vsync = false;
    gs.fpsCap = p.fpsCap;
    GraphicsSettings::holdSaving(true);   // (these are only for the test: never written to settings.json)
    m_phoneTesting = profile + 1;
    Log::system(std::string("Testing like a ") + p.name + ": its graphics, " + std::to_string(p.fpsCap) +
                " FPS at most. Stop the test to get Studio's own settings back.");
    togglePlay();
}

void Editor::endPhoneTest() {
    if (!m_phoneTesting) return;
    GraphicsSettings::holdSaving(false);
    if (m_phoneSaved) GraphicsSettings::get() = *m_phoneSaved;
    m_phoneSaved.reset();
    m_phoneTesting = 0;
    Log::system("Phone test over: Studio's own graphics settings are back.");
}

void Editor::renderPhoneTest() {
    if (m_phoneTesting && !m_playing) endPhoneTest();   // (the playtest stopped some other way)
    if (!m_showPhoneTest) return;
    ImGui::SetNextWindowSize(ImVec2(470, 470), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Test like a phone", &m_showPhoneTest)) { ImGui::End(); return; }
    ImGui::TextWrapped("Most players are on phones and old computers. Check your game against what they can handle, "
                       "then playtest it with their graphics settings.");
    ImGui::Spacing();
    for (int i = 0; i < kProfileCount; ++i) {
        if (ImGui::RadioButton(kProfiles[i].name, m_phonePick == i)) m_phonePick = i;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", kProfiles[i].about);
    }
    const PhoneProfile& p = kProfiles[m_phonePick];
    const Count c = countScene(*m_scene);
    ImGui::SeparatorText("Budget");
    int over = 0;
    if (ImGui::BeginTable("##phoneBudget", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("In your game", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Phone limit", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableHeadersRow();
        for (const Row& r : rowsFor(c, p)) {
            const std::string v = verdict(r.have, r.limit);
            const ImVec4 col = v == "ok" ? ImVec4(0.35f, 0.85f, 0.45f, 1) : v == "close" ? ImVec4(0.95f, 0.75f, 0.25f, 1) : ImVec4(1, 0.4f, 0.35f, 1);
            if (v == "over") ++over;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.what);
            if (v != "ok" && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.fix);
            ImGui::TableNextColumn(); ImGui::TextColored(col, "%d", r.have);
            ImGui::TableNextColumn(); ImGui::Text("%d", r.limit);
        }
        ImGui::EndTable();
    }
    if (over) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.35f, 1), "%d over the limit. Hover a red row to see what to do.", over);
    } else {
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1), "Everything fits a %s.", p.name);
    }
    ImGui::SeparatorText("Playtest");
    ImGui::TextWrapped("Plays your game with this device's graphics (%s), %d%% sharpness and at most %d FPS. "
                       "Your computer is still faster than a real phone, so a smooth test here isn't a promise.",
                       GraphicsSettings::qualityNames()[GraphicsSettings::menuIndexOf(p.quality)], (int)(p.renderScale * 100), p.fpsCap);
    if (m_phoneTesting) {
        ImGui::TextColored(ImVec4(1, 0.78f, 0.35f, 1), "Testing like a %s now.", kProfiles[m_phoneTesting - 1].name);
        if (ImGui::Button("Stop the test")) togglePlay();
    } else {
        ImGui::BeginDisabled(m_playing);
        if (ImGui::Button("Playtest like this")) startPhoneTest(m_phonePick);
        ImGui::EndDisabled();
        if (m_playing) { ImGui::SameLine(); ImGui::TextDisabled("(stop the playtest first)"); }
    }
    ImGui::End();
}
