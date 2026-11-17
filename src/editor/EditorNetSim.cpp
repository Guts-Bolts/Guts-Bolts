// Network simulator (Test menu > Network simulator...): like Roblox's Network Simulator, but
// built into the multiplayer test and with physics numbers next to the network ones.
//
// Pick a connection (Wired, Wi-Fi, 4G, 3G, Bad signal, or your own numbers), start "Test with
// N players", and every player window's connection behaves like that one. It can change while
// the test runs. Each window reports, once a second:
//   - world updates a second   the server's tick as it reaches the player (it sends 20)
//   - arrives late             how old the world is when it gets there
//   - others behind            how far in the past other players are shown (to keep them smooth)
//   - KB in / out              what the game costs that connection
//   - resent                   lost packets that had to go again
//   - moved back               the server's anti-cheat put the player back (lag can look like cheating)
//   - prediction misses        moving parts more than a stud from where their last update said
//                              they were heading (fast physics looks jumpy)
//   - ownership changes        parts this player's computer moves (a car it drives) changing hands
// and the window says what that means in plain words. The same is the network_sim AI tool
// (AiToolsRun.cpp), so Fyxa can slow the connection, run the test and read what went wrong.
// The simulator itself is src/net/NetSim.*.
#include "Editor.h"
#include "MultiTest.h"
#include "../core/Log.h"
#include "../net/NetSim.h"

#include <imgui.h>

#include <algorithm>
#include <string>

using json = nlohmann::json;

// What a test's numbers mean, in plain words (the window and the AI tool both use this).
json Editor::networkAdvice(const json& players, const NetSim::Conditions& c) {
    json out = json::array();
    for (const json& p : players) {
        if (!p.value("fresh", false)) continue;
        const std::string who = p.value("player", std::string("A player"));
        const double age = p.value("ageMs", 0.0), worlds = p.value("worldPerSec", 0.0);
        if (age > 250)
            out.push_back(who + ": the world arrives about " + std::to_string((int)age) + " ms late. Things the server moves "
                          "(doors, balls, other people's cars) will look behind. Let each player's own computer move what they "
                          "control (network ownership, SetNetworkOwner) and don't wait for the server before showing a hit.");
        if (worlds > 0 && worlds < 15)
            out.push_back(who + ": only " + std::to_string((int)worlds) + " of the server's 20 updates a second get through. "
                          "The connection is too slow for this much: fewer moving parts, or anchor the ones that have stopped.");
        if (c.kbps > 0 && p.value("kbIn", 0.0) * 8.0 > c.kbps * 0.8)
            out.push_back(who + ": the game uses most of this connection (" + std::to_string((int)p.value("kbIn", 0.0)) +
                          " KB a second). Big changes (terrain, many parts at once) will make everything else wait.");
        if (p.value("waiting", 0) > 40)
            out.push_back(who + ": messages are piling up (" + std::to_string(p.value("waiting", 0)) + " waiting). "
                          "The game sends more than this connection can carry.");
        if (p.value("misses", 0) > 5)
            out.push_back(who + ": moving parts often end up far from where they were heading (worst " +
                          std::to_string((int)p.value("worstMiss", 0.0)) + " studs). Fast physics looks jumpy here: slow it "
                          "down, anchor what doesn't need physics, or give the nearest player ownership.");
        if (p.value("moveBacks", 0) > 0)
            out.push_back(who + ": the server moved them back " + std::to_string(p.value("moveBacks", 0)) +
                          " time(s). With this much lag an honest player can look like they're teleporting.");
    }
    return out;
}

json Editor::networkSim(const json& args) {
    MultiTest& mt = multiTest();
    const json a = args.is_object() ? args : json::object();
    const std::string action = a.value("action", std::string("status"));
    if (action == "set") {
        NetSim::Conditions c;
        const std::string name = a.value("preset", std::string());
        if (!name.empty()) {
            if (!NetSim::preset(name, c)) return {{"error", "There's no connection called " + name + "."}};
        } else {
            c.name = "Custom";
            c.latencyMs = a.value("latency_ms", 0);
            c.jitterMs = a.value("jitter_ms", 0);
            c.lossPercent = a.value("loss_percent", 0.0f);
            c.kbps = a.value("kbps", 0);
        }
        mt.setNetwork(c);
        m_netPreset = (int)NetSim::presets().size();   // (the window shows "Your own numbers"...)
        for (size_t i = 0; i < NetSim::presets().size(); ++i)
            if (NetSim::presets()[i].name == mt.network().name) m_netPreset = (int)i;   // (...unless it's a preset)
        if (m_netPreset == (int)NetSim::presets().size()) m_netCustom = mt.network();
    } else if (action != "status") {
        return {{"error", "action is set or status."}};
    }
    const NetSim::Conditions& c = mt.network();
    const json players = mt.networkStats();
    json r = {{"conditions", {{"name", c.name}, {"latency_ms", c.latencyMs}, {"jitter_ms", c.jitterMs},
                              {"loss_percent", c.lossPercent}, {"kbps", c.kbps}}},
              {"test_running", mt.running()}, {"players", players}, {"advice", networkAdvice(players, c)}};
    if (!mt.running())
        r["note"] = "No multiplayer test is running: these conditions are used by the next one (multiplayer_test start).";
    else if (players.empty())
        r["note"] = "The players haven't joined yet: ask again in a few seconds.";
    return r;
}

void Editor::renderNetSim() {
    if (!m_showNetSim) return;
    ImGui::SetNextWindowSize(ImVec2(760, 620), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(80, 90), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Network simulator", &m_showNetSim)) { ImGui::End(); return; }
    MultiTest& mt = multiTest();
    ImGui::TextWrapped("Make the players in a multiplayer test play over a worse connection: lag, wobble, lost packets and "
                       "a speed limit. Change it any time; the test keeps running. It slows the network, not the computer.");
    ImGui::Spacing();

    const auto& presets = NetSim::presets();
    const int custom = (int)presets.size();
    bool changed = false;
    if (m_netPreset < 0 || m_netPreset > custom) m_netPreset = 0;
    for (int i = 0; i < (int)presets.size(); ++i) {
        if (ImGui::RadioButton(presets[i].name, m_netPreset == i)) { m_netPreset = i; changed = true; }
        ImGui::SameLine(130);
        if (presets[i].c.on())
            ImGui::TextDisabled("%d ms lag, +/-%d, %.1f%% lost%s  %s", presets[i].c.latencyMs, presets[i].c.jitterMs,
                                presets[i].c.lossPercent,
                                presets[i].c.kbps ? (", " + std::to_string(presets[i].c.kbps) + " kbps").c_str() : "",
                                presets[i].about);
        else
            ImGui::TextDisabled("%s", presets[i].about);
    }
    if (ImGui::RadioButton("Your own numbers", m_netPreset == custom)) { m_netPreset = custom; changed = true; }
    if (m_netPreset == custom) {
        ImGui::Indent();
        ImGui::SetNextItemWidth(220);
        changed |= ImGui::SliderInt("Lag (ms, there and back)", &m_netCustom.latencyMs, 0, 2000);
        ImGui::SetNextItemWidth(220);
        changed |= ImGui::SliderInt("Wobble (ms)", &m_netCustom.jitterMs, 0, 500);
        ImGui::SetNextItemWidth(220);
        changed |= ImGui::SliderFloat("Lost packets (%)", &m_netCustom.lossPercent, 0.0f, 20.0f, "%.1f");
        ImGui::SetNextItemWidth(220);
        changed |= ImGui::SliderInt("Speed (kbps, 0 = no limit)", &m_netCustom.kbps, 0, 20000);
        ImGui::Unindent();
    }
    if (changed) {
        NetSim::Conditions c = m_netPreset == custom ? m_netCustom : presets[m_netPreset].c;
        if (m_netPreset == custom) c.name = "Custom";
        mt.setNetwork(c);
    }

    ImGui::Spacing();
    if (!mt.running()) {
        ImGui::TextColored(ImVec4(1, 0.78f, 0.35f, 1), "No multiplayer test is running.");
        ImGui::SameLine();
        if (ImGui::Button("Start a test with 2 players")) {
            std::string err;
            startMultiTest(2, err);
        }
        ImGui::TextDisabled("(Test menu > Players works too. Playing alone has no network to slow down.)");
        ImGui::End();
        return;
    }

    const json players = mt.networkStats();
    ImGui::SeparatorText("What each player sees");
    if (players.empty()) ImGui::TextDisabled("Waiting for the players to join...");
    else if (ImGui::BeginTable("netsim", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        const char* heads[] = {"Player", "Updates/s", "Arrives late", "Others behind", "KB in/out", "Resent", "Moved back",
                               "Prediction misses", "Ownership"};
        for (const char* h : heads) ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const json& p : players) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(p.value("player", std::string("?")).c_str());
            if (!p.value("fresh", false)) {
                ImGui::TableNextColumn();
                ImGui::TextDisabled("joining...");
                continue;
            }
            const double age = p.value("ageMs", 0.0), worlds = p.value("worldPerSec", 0.0);
            const ImVec4 good(0.35f, 0.85f, 0.45f, 1), meh(0.95f, 0.75f, 0.25f, 1), bad(1, 0.4f, 0.35f, 1);
            ImGui::TableNextColumn();
            ImGui::TextColored(worlds >= 18 ? good : worlds >= 12 ? meh : bad, "%.0f", worlds);
            ImGui::TableNextColumn();
            ImGui::TextColored(age < 120 ? good : age < 250 ? meh : bad, "%.0f ms", age);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f ms", p.value("othersBehindMs", 0.0));
            ImGui::TableNextColumn();
            ImGui::Text("%.1f / %.1f", p.value("kbIn", 0.0), p.value("kbOut", 0.0));
            ImGui::TableNextColumn();
            ImGui::Text("%d", p.value("resent", 0));
            ImGui::TableNextColumn();
            ImGui::TextColored(p.value("moveBacks", 0) ? bad : good, "%d", p.value("moveBacks", 0));
            ImGui::TableNextColumn();
            ImGui::TextColored(p.value("misses", 0) > 5 ? bad : p.value("misses", 0) ? meh : good, "%d", p.value("misses", 0));
            ImGui::TableNextColumn();
            ImGui::Text("%d (%d now)", p.value("ownerChanges", 0), p.value("owned", 0));
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("The server sends 20 world updates a second. Numbers are for the last second (moved back and "
                        "ownership: the whole test).");
    const json advice = networkAdvice(players, mt.network());
    ImGui::SeparatorText("What it means");
    if (advice.empty() && !players.empty())
        ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.45f, 1), "Nothing to worry about on this connection.");
    for (const json& w : advice) { ImGui::Bullet(); ImGui::TextWrapped("%s", w.get<std::string>().c_str()); }
    ImGui::End();
}
