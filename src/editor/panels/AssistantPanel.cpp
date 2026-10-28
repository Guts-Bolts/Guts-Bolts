#include "AssistantPanel.h"
#include "../Editor.h"
#include "../AiTools.h"
#include "../../net/Socket.h"
#include "../../online/Protocol.h"   // base64
#include "../../core/Paths.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <chrono>
#include <fstream>
#include <thread>

using nlohmann::json;

namespace {
constexpr int kMaxRounds = 30;
const char* kModels[] = {"claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-4-5"};
const char* kModelNames[] = {"Claude Opus 5.5 (best)", "Claude Sonnet 5.5 (faster)", "Claude Haiku 4.5 (fastest)"};

// Where Studio lives, for the Claude Desktop set-up snippet.
std::string exePath() { return Paths::sibling("GutsAndBolts").string(); }
} // namespace

AssistantPanel::AssistantPanel(Editor& editor) : m_editor(editor) {
    load();
    if (m_mcpWanted && !m_mcp.start(m_mcpPort, m_mcpError)) m_mcpWanted = false;
    m_relay.setEnabled(m_appsWanted);
}

AssistantPanel::~AssistantPanel() {
    m_mcp.stop();
    if (m_pending.valid()) m_pending.wait();
}

void AssistantPanel::load() {
    std::ifstream f(Paths::file("assistant.json"));
    json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    m_apiKey = j.value("apiKey", std::string());
    m_model = j.value("model", m_model);
    m_mcpWanted = j.value("mcp", false);
    m_mcpPort = j.value("mcpPort", (int)StudioMcp::kDefaultPort);
    m_appsWanted = j.value("connectedApps", false);
}

void AssistantPanel::save() const {
    std::ofstream f(Paths::file("assistant.json"));
    f << json{{"apiKey", m_apiKey}, {"model", m_model}, {"mcp", m_mcpWanted}, {"mcpPort", m_mcpPort},
              {"connectedApps", m_appsWanted}}.dump(2);
}

void AssistantPanel::update() {
    m_mcp.update(m_editor);
    m_relay.update(m_editor, m_mcp);
    if (!m_pending.valid()) return;
    if (m_pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    Reply r = m_pending.get();
    handleReply(r.status, r.body, r.error);
}

void AssistantPanel::send(const std::string& text) {
    m_lines.push_back({Line::You, text});
    m_messages.push_back({{"role", "user"}, {"content", text}});
    m_rounds = 0;
    m_stopAsked = false;
    requestNext();
}

void AssistantPanel::requestNext() {
    // The guide and the tool list are the same every time: let the API cache them
    // (much cheaper and faster from the second message on).
    json tools = AiTools::list();
    tools.back()["cache_control"] = {{"type", "ephemeral"}};
    json body = {
        {"model", m_model},
        {"max_tokens", 16000},
        {"system", json::array({{{"type", "text"}, {"text", AiTools::guide()}, {"cache_control", {{"type", "ephemeral"}}}}})},
        {"tools", tools},
        {"messages", m_messages},
    };
    std::string headers = "x-api-key: " + m_apiKey + "\r\nanthropic-version: 2023-06-01\r\n";
    // If a request is declined, the API retries it on another model instead of just stopping.
    if (m_model != "claude-haiku-4-5") {
        body["fallbacks"] = "default";
        headers += "anthropic-beta: server-side-fallback-2026-07-01\r\n";
    }
    std::string payload = body.dump();
    m_pending = std::async(std::launch::async, [payload, headers]() {
        Reply r;
        Net::httpPost("wss://api.anthropic.com", 443, "/v1/messages", payload, r.body, r.error, 300000, headers, &r.status);
        return r;
    });
    m_scrollDown = true;
}

void AssistantPanel::handleReply(int status, const std::string& body, const std::string& error) {
    m_scrollDown = true;
    json j = json::parse(body, nullptr, false);
    if (status != 200 || !j.is_object() || !j.contains("content")) {
        std::string why = j.is_object() && j.contains("error") ? j["error"].value("message", std::string()) : error;
        if (status == 401) why = "Your API key wasn't accepted. Check it in the settings above.";
        m_lines.push_back({Line::Error, "Couldn't reach Claude" + (status ? " (" + std::to_string(status) + ")" : std::string()) +
                                            (why.empty() ? "." : ": " + why)});
        // Take back the message it didn't answer, so the conversation stays valid.
        while (!m_messages.empty() && m_messages.back().value("role", "") == "user") m_messages.erase(m_messages.size() - 1);
        return;
    }
    // Keep everything it said, exactly (thinking blocks included), for the next turn.
    m_messages.push_back({{"role", "assistant"}, {"content", j["content"]}});
    for (const auto& b : j["content"])
        if (b.value("type", "") == "text" && !b.value("text", "").empty()) m_lines.push_back({Line::Claude, b["text"].get<std::string>()});

    const std::string stop = j.value("stop_reason", "");
    if (stop == "refusal") { m_lines.push_back({Line::Note, "Claude declined that request."}); return; }
    if (stop == "max_tokens") { m_lines.push_back({Line::Note, "That answer hit the length limit. Say \"continue\" for more."}); return; }
    if (stop != "tool_use") return;

    // Run every tool it asked for, then send all the results back together.
    json results = json::array();
    for (const auto& b : j["content"]) {
        if (b.value("type", "") != "tool_use") continue;
        std::string name = b.value("name", "");
        AiToolResult r = m_editor.runAiToolFrom("Assistant", name, b.contains("input") && b["input"].is_object() ? b["input"] : json::object());
        std::string shown = name;
        if (b.contains("input") && b["input"].contains("object")) shown += " " + b["input"]["object"].get<std::string>();
        m_lines.push_back({r.error ? Line::Error : Line::Tool, shown + (r.error ? ": " + r.text : "")});
        json content = json::array({{{"type", "text"}, {"text", r.text}}});
        if (!r.png.empty())
            content.push_back({{"type", "image"}, {"source", {{"type", "base64"}, {"media_type", "image/png"},
                                                              {"data", Online::base64Encode(r.png)}}}});
        json result = {{"type", "tool_result"}, {"tool_use_id", b.value("id", "")}, {"content", content}};
        if (r.error) result["is_error"] = true;
        results.push_back(result);
    }
    m_messages.push_back({{"role", "user"}, {"content", results}});
    if (m_stopAsked) { m_lines.push_back({Line::Note, "Stopped."}); return; }
    if (++m_rounds >= kMaxRounds) { m_lines.push_back({Line::Note, "That's a lot of steps; paused here. Say \"continue\" to keep going."}); return; }
    requestNext();
}

void AssistantPanel::render(bool* open) {
    if (!ImGui::Begin("Assistant", open)) { ImGui::End(); return; }
    const bool busy = m_pending.valid();

    // --- Connect AI apps (MCP) ---
    if (ImGui::CollapsingHeader("Connect AI apps (MCP)")) {
        ImGui::TextWrapped("Let AI apps on this computer (Claude Desktop, Claude Code and other MCP tools) look at and "
                           "build your game. Only programs on this computer can connect.");
        bool on = m_mcp.running();
        if (ImGui::Checkbox("Let AI apps use Studio (MCP)", &on)) {
            m_mcpError.clear();
            if (on) { if (!m_mcp.start(m_mcpPort, m_mcpError)) on = false; }
            else m_mcp.stop();
            m_mcpWanted = on;
            save();
        }
        if (!m_mcpError.empty()) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", m_mcpError.c_str());
        if (m_mcp.running()) {
            ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1), "On at http://127.0.0.1:%d/mcp", m_mcp.port());
            if (m_mcp.callsServed())
                ImGui::TextDisabled("%d tool call(s) so far (last: %s)", m_mcp.callsServed(), m_mcp.lastTool().c_str());
            const std::string url = "http://127.0.0.1:" + std::to_string(m_mcp.port()) + "/mcp";
            const std::string code = "claude mcp add --transport http guts-and-bolts " + url;
            json desktop = {{"mcpServers", {{"guts-and-bolts", {{"command", exePath()}, {"args", {"--mcp"}}}}}}};
            const std::string desk = desktop.dump(2);
            ImGui::SeparatorText("Claude Code");
            ImGui::TextWrapped("%s", code.c_str());
            if (ImGui::SmallButton("Copy##cc")) ImGui::SetClipboardText(code.c_str());
            ImGui::SeparatorText("Claude Desktop (and other apps that start a program)");
            ImGui::TextWrapped("Add this to the app's MCP settings (claude_desktop_config.json):");
            ImGui::InputTextMultiline("##desk", const_cast<char*>(desk.c_str()), desk.size() + 1, ImVec2(-1, 110), ImGuiInputTextFlags_ReadOnly);
            if (ImGui::SmallButton("Copy##cd")) ImGui::SetClipboardText(desk.c_str());
        }
    }

    // --- Connected apps (through gutsandbolts.net: no port opened here) ---
    if (ImGui::CollapsingHeader("Connected apps (online)")) {
        ImGui::TextWrapped("Let apps you connected to your Guts&Bolts account (like Lyreto) use Studio's AI tools while Studio is "
                           "open. They can only do what you allowed when you connected them, and they can never publish. "
                           "Disconnect them on gutsandbolts.net in Settings > Connected apps.");
        if (ImGui::Checkbox("Allow connected apps to use AI tools", &m_appsWanted)) {
            m_relay.setEnabled(m_appsWanted);
            save();
        }
        if (m_appsWanted) {
            const StudioRelay::State st = m_relay.state();
            const ImVec4 color = st == StudioRelay::State::Online ? ImVec4(0.45f, 0.9f, 0.5f, 1)
                               : st == StudioRelay::State::Failed ? ImVec4(1, 0.45f, 0.4f, 1) : ImVec4(0.9f, 0.8f, 0.4f, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", m_relay.statusText().c_str());
            ImGui::PopStyleColor();
            if (m_relay.callsServed())
                ImGui::TextDisabled("%d tool call(s) from apps (last: %s, by %s)", m_relay.callsServed(), m_relay.lastTool().c_str(),
                                    m_relay.lastApp().c_str());
        }
    }

    // --- Chat ---
    if (ImGui::CollapsingHeader("Chat settings", m_apiKey.empty() ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
        ImGui::TextWrapped("The Assistant uses Claude with your own Anthropic API key (from console.anthropic.com). "
                           "It's kept on this computer only.");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##key", "Anthropic API key (sk-ant-...)", &m_apiKey, ImGuiInputTextFlags_Password)) save();
        int mi = 0;
        for (int i = 0; i < 3; ++i) if (m_model == kModels[i]) mi = i;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##model", &mi, kModelNames, 3)) { m_model = kModels[mi]; save(); }
    }

    const float footer = ImGui::GetFrameHeightWithSpacing() * 3.2f;
    ImGui::BeginChild("##chat", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    if (m_lines.empty())
        ImGui::TextWrapped("Ask for anything: \"make an obby with 10 jumps and a finish line\", \"why doesn't my door "
                           "script work?\", \"add a zombie that chases players\". The Assistant can build, write scripts, "
                           "playtest and look at the result. Ctrl+Z undoes anything it does.");
    for (const Line& l : m_lines) {
        ImGui::PushTextWrapPos(0.0f);
        switch (l.kind) {
            case Line::You:    ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "You:"); ImGui::TextUnformatted(l.text.c_str()); break;
            case Line::Claude: ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.45f, 1), "Assistant:"); ImGui::TextUnformatted(l.text.c_str()); break;
            case Line::Tool:   ImGui::TextDisabled("  > %s", l.text.c_str()); break;
            case Line::Note:   ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", l.text.c_str()); break;
            case Line::Error:  ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", l.text.c_str()); break;
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (busy) ImGui::TextDisabled("Working%s", std::string((size_t)((int)(ImGui::GetTime() * 3) % 4), '.').c_str());
    if (m_scrollDown) { ImGui::SetScrollHereY(1.0f); m_scrollDown = false; }
    ImGui::EndChild();

    ImGui::BeginDisabled(m_apiKey.empty());
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputTextMultiline("##ask", &m_input, ImVec2(-1, ImGui::GetFrameHeight() * 1.9f),
                                           ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_EnterReturnsTrue);
    if (busy) {
        if (ImGui::Button("Stop")) m_stopAsked = true;
    } else if ((enter || ImGui::Button("Send (Enter)")) && !m_input.empty()) {
        send(m_input);
        m_input.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("New chat") && !busy) { m_messages = json::array(); m_lines.clear(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(m_editor.aiHasChanges() ? "Review its changes" : "AI Activity")) m_editor.showAiActivity();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything the AI did and changed. Undo all of it in one click.");
    if (m_apiKey.empty()) ImGui::TextDisabled("Add your API key in Chat settings to start.");
    ImGui::End();
}
