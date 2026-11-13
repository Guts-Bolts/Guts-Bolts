// Experiments and analytics in games: ExperimentService and AnalyticsService (offline here,
// so no experiment is running and nothing is sent), and the version sum the website uses.
//
//   ExperimentsTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"
#include "scripting/Experiments.h"

#include <imgui.h>
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
    err.clear();
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err = e.text; }
    return ok;
}

void test(GameSession& session, const std::string& what, const std::string& code) {
    std::string err;
    check(run(session, code, err), what + (err.empty() ? "" : "  (" + err + ")"));
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: ExperimentsTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // The same sum as worker/experiments.js variantOf (FNV-1a of "x:1" is 0x87017982).
    check(Experiments::variantOf("x", 1, 1000000007) == (int)(0x87017982u % 1000000007u), "the version sum matches the website's");
    int counts[3] = {0, 0, 0};
    for (int p = 1; p <= 3000; ++p) counts[Experiments::variantOf("5f0e2a9c1b7d", p, 3)]++;
    check(counts[0] > 850 && counts[1] > 850 && counts[2] > 850, "players are spread over the versions");

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.start();

    test(session, "the services are there", R"(
        local ES = game:GetService("ExperimentService")
        local AS = game:GetService("AnalyticsService")
        assert(ES and ES.ClassName == "ExperimentService")
        assert(AS and AS.ClassName == "AnalyticsService")
    )");

    test(session, "no experiment running: no version, and nothing waits to be sent", R"(
        local ES = game:GetService("ExperimentService")
        assert(ES:GetVariantAsync(42, "Shop") == nil)
        assert(#ES:GetExperimentsAsync() == 0)
        local AS = game:GetService("AnalyticsService")
        AS:LogCustomEvent(42, "BoughtSword", 1)
        AS:LogEconomyEvent(42, Enum.AnalyticsEconomyFlowType.Sink, "Coins", 50, 10, "Shop")
        AS:LogFunnelStepEvent(42, "Tutorial", nil, 3, "Jumped")
        AS:LogOnboardingFunnelStepEvent(42, 2)
        AS:LogProgressionEvent(42)
        assert(__gb_analyticsQueued() == 0)
    )");

    test(session, "SetTestVariant picks a version while testing", R"(
        local ES = game:GetService("ExperimentService")
        ES:SetTestVariant("Shop", "New")
        assert(ES:GetVariantAsync(42, "Shop") == "New")
        ES:SetTestVariant("Shop", nil)
        assert(ES:GetVariantAsync(42, "Shop") == nil)
    )");

    test(session, "bad values are errors", R"(
        local AS = game:GetService("AnalyticsService")
        assert(not pcall(function() AS:LogCustomEvent(42, "") end))
        assert(not pcall(function() AS:LogCustomEvent(42, "x", "lots") end))
        assert(not pcall(function() AS:LogEconomyEvent(42, "Sideways", "Coins", 5) end))
        assert(not pcall(function() AS:LogFunnelStepEvent(42, "Tutorial", nil, "three") end))
        assert(not pcall(function() game:GetService("ExperimentService"):GetVariantAsync(42, 5) end))
    )");

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
