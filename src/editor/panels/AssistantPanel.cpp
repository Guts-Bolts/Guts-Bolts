#include "AssistantPanel.h"
#include "../Editor.h"
#include "../AiTools.h"
#include "../../net/Socket.h"
#include "../../online/Protocol.h"   // base64
#include "../../core/Paths.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>

using nlohmann::json;

namespace {
constexpr int kMaxRounds = 30;
const char* kClaudeModels[] = {"claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-4-5"};
const char* kClaudeModelNames[] = {"Claude Opus 5.5 (best)", "Claude Sonnet 5.5 (faster)", "Claude Haiku 4.5 (fastest)"};

std::string exePath() { return Paths::sibling("GutsAndBolts").string(); }

json ollamaTools(const json& external = json::array()) {
    json tools = json::array();
    for (const auto& tool : AiTools::list()) {
        tools.push_back({
            {"type", "function"},
            {"function", {
                {"name", tool.value("name", std::string())},
                {"description", tool.value("description", std::string())},
                {"parameters", tool.value("input_schema", json::object())}
            }}
        });
    }
    for (const auto& tool : external) {
        tools.push_back({{"type", "function"}, {"function", {
            {"name", tool.value("qualifiedName", std::string())},
            {"description", tool.value("description", std::string())},
            {"parameters", tool.value("inputSchema", json::object())}
        }}});
    }
    return tools;
}

struct McpEndpoint { std::string host, path; int port = 443; bool tls = true; };
bool parseMcpEndpoint(const std::string& url, McpEndpoint& out, std::string& error) {
    const bool https = url.rfind("https://", 0) == 0;
    const bool http = url.rfind("http://", 0) == 0;
    if (!https && !http) { error = "Use an http:// or https:// MCP endpoint."; return false; }
    const size_t start = https ? 8 : 7;
    const size_t slash = url.find('/', start);
    std::string authority = url.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
    out.path = slash == std::string::npos ? "/mcp" : url.substr(slash);
    if (authority.empty() || authority.find('@') != std::string::npos) { error = "The MCP endpoint address is invalid."; return false; }
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        out.host = authority.substr(0, colon);
        try { out.port = std::stoi(authority.substr(colon + 1)); } catch (...) { error = "The MCP endpoint port is invalid."; return false; }
    } else { out.host = authority; out.port = https ? 443 : 80; }
    if (out.host.empty() || out.port < 1 || out.port > 65535) { error = "The MCP endpoint host or port is invalid."; return false; }
    if (!https && out.host != "localhost" && out.host != "127.0.0.1" && out.host != "::1") {
        error = "For security, non-local MCP endpoints must use HTTPS."; return false;
    }
    out.tls = https;
    return true;
}
std::string responseHeader(const std::string& headers, const std::string& wanted) {
    std::string low = headers, key = wanted;
    for (char& ch : low) ch = (char)std::tolower((unsigned char)ch);
    for (char& ch : key) ch = (char)std::tolower((unsigned char)ch);
    size_t at = low.find("\n" + key + ":");
    if (at == std::string::npos) return {};
    at += key.size() + 2;
    size_t end = headers.find("\r", at);
    std::string value = headers.substr(at, end == std::string::npos ? std::string::npos : end - at);
    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
    return value;
}
bool mcpPost(const McpEndpoint& endpoint, const std::string& token, const std::string& session,
             const json& request, json& response, std::string& error, std::string* sessionOut = nullptr,
             const std::string& protocolVersion = "2025-06-18") {
    std::string headers = "Accept: application/json, text/event-stream\r\nMCP-Protocol-Version: " + protocolVersion + "\r\n";
    if (!token.empty()) headers += "Authorization: Bearer " + token + "\r\n";
    if (!session.empty()) headers += "Mcp-Session-Id: " + session + "\r\n";
    std::string body, responseHeaders;
    int status = 0;
    const std::string host = endpoint.tls ? "wss://" + endpoint.host : endpoint.host;
    if (!Net::httpPost(host, endpoint.port, endpoint.path, request.dump(), body, error, 12000, headers, &status, &responseHeaders)) return false;
    if (sessionOut) *sessionOut = responseHeader(responseHeaders, "Mcp-Session-Id");
    if (status == 202 || body.empty()) { response = json::object(); return true; }
    response = json::parse(body, nullptr, false);
    if (!response.is_object()) {
        size_t at = 0;
        while ((at = body.find("data:", at)) != std::string::npos) {
            at += 5;
            while (at < body.size() && body[at] == ' ') ++at;
            size_t end = body.find('\n', at);
            json event = json::parse(body.substr(at, end == std::string::npos ? std::string::npos : end - at), nullptr, false);
            if (event.is_object()) { response = std::move(event); break; }
            if (end == std::string::npos) break;
            at = end + 1;
        }
    }
    if (!response.is_object()) { error = "The MCP server returned an invalid JSON-RPC response."; return false; }
    if (response.contains("error")) { error = response["error"].value("message", "The MCP server returned an error."); return false; }
    return status >= 200 && status < 300;
}
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
    m_provider = j.value("provider", m_provider);
    m_ollamaHost = j.value("ollamaHost", m_ollamaHost);
    m_ollamaPort = j.value("ollamaPort", m_ollamaPort);
    m_ollamaModel = j.value("ollamaModel", m_ollamaModel);
    if (j.contains("mcpServers") && j["mcpServers"].is_array()) {
        for (const auto& item : j["mcpServers"]) {
            if (!item.is_object()) continue;
            McpServer server;
            server.name = item.value("name", std::string());
            server.url = item.value("url", std::string());
            server.token = item.value("token", std::string());
            server.enabled = item.value("enabled", true);
            server.sessionId = item.value("sessionId", std::string());
            server.protocolVersion = item.value("protocolVersion", std::string("2025-06-18"));
            server.tools = item.value("tools", json::array());
            if (!server.name.empty() && !server.url.empty()) m_externalMcp.push_back(std::move(server));
        }
    }
    m_mcpWanted = j.value("mcp", false);
    m_mcpPort = j.value("mcpPort", (int)StudioMcp::kDefaultPort);
    m_appsWanted = j.value("connectedApps", false);
}

void AssistantPanel::save() const {
    std::ofstream f(Paths::file("assistant.json"));
    f << json{{"provider", m_provider}, {"apiKey", m_apiKey}, {"model", m_model},
              {"ollamaHost", m_ollamaHost}, {"ollamaPort", m_ollamaPort}, {"ollamaModel", m_ollamaModel},
              {"mcp", m_mcpWanted}, {"mcpPort", m_mcpPort}, {"connectedApps", m_appsWanted},
              {"mcpServers", [&]() { json a = json::array(); for (const auto& s : m_externalMcp)
                  a.push_back({{"name", s.name}, {"url", s.url}, {"token", s.token}, {"enabled", s.enabled},
                               {"sessionId", s.sessionId}, {"protocolVersion", s.protocolVersion}, {"tools", s.tools}}); return a; }()}}.dump(2);
}

json AssistantPanel::externalMcpTools() const {
    json result = json::array();
    for (const auto& server : m_externalMcp) {
        if (!server.enabled || !server.tools.is_array()) continue;
        std::string prefix = server.name;
        for (char& ch : prefix) if (!std::isalnum((unsigned char)ch)) ch = '_';
        for (const auto& tool : server.tools) {
            if (!tool.is_object()) continue;
            const std::string name = tool.value("name", std::string());
            if (name.empty()) continue;
            json schema = tool.value("inputSchema", json::object());
            if (!schema.is_object()) schema = json::object();
            result.push_back({{"qualifiedName", "mcp__" + prefix + "__" + name},
                              {"description", "[" + server.name + "] " + tool.value("description", std::string("External MCP tool"))},
                              {"inputSchema", schema}});
        }
    }
    return result;
}

void AssistantPanel::refreshMcpServers() {
    m_mcpStatus.clear();
    for (auto& server : m_externalMcp) {
        McpEndpoint endpoint;
        std::string error;
        if (!parseMcpEndpoint(server.url, endpoint, error)) { m_mcpStatus = server.name + ": " + error; continue; }
        json response;
        std::string newSession;
        json initialize = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
                           {"params", {{"protocolVersion", "2025-06-18"}, {"capabilities", json::object()},
                                       {"clientInfo", {{"name", "fyxa"}, {"version", "0.1"}}}}}};
        if (!mcpPost(endpoint, server.token, "", initialize, response, error, &newSession)) {
            m_mcpStatus = server.name + ": " + error;
            server.tools = json::array();
            continue;
        }
        server.sessionId = newSession;
        server.protocolVersion = response.value("result", json::object()).value("protocolVersion", "2025-06-18");
        json initialized = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
        json ignored;
        mcpPost(endpoint, server.token, server.sessionId, initialized, ignored, error, nullptr, server.protocolVersion);
        server.tools = json::array();
        std::string cursor;
        for (int page = 0; page < 20; ++page) {
            json params = json::object();
            if (!cursor.empty()) params["cursor"] = cursor;
            json list = {{"jsonrpc", "2.0"}, {"id", 2 + page}, {"method", "tools/list"}, {"params", params}};
            if (!mcpPost(endpoint, server.token, server.sessionId, list, response, error, nullptr, server.protocolVersion)) {
                m_mcpStatus = server.name + ": " + error;
                server.tools = json::array();
                break;
            }
            const json pageTools = response.value("result", json::object()).value("tools", json::array());
            if (pageTools.is_array()) for (const auto& tool : pageTools) server.tools.push_back(tool);
            cursor = response.value("result", json::object()).value("nextCursor", std::string());
            if (cursor.empty()) break;
        }
    }
    save();
    if (m_mcpStatus.empty()) {
        size_t count = 0;
        for (const auto& server : m_externalMcp) if (server.enabled && server.tools.is_array()) count += server.tools.size();
        m_mcpStatus = "MCP discovery complete. " + std::to_string(count) + " tool(s) available.";
    }
}

bool AssistantPanel::callExternalMcpTool(const std::string& qualifiedName, const json& args,
                                         std::string& result, std::string& error) const {
    if (qualifiedName.rfind("mcp__", 0) != 0) { error = "Unknown external MCP tool."; return false; }
    const size_t split = qualifiedName.find("__", 5);
    if (split == std::string::npos) { error = "The external MCP tool name is malformed."; return false; }
    const std::string serverKey = qualifiedName.substr(5, split - 5);
    const std::string toolName = qualifiedName.substr(split + 2);
    for (const auto& server : m_externalMcp) {
        std::string key = server.name;
        for (char& ch : key) if (!std::isalnum((unsigned char)ch)) ch = '_';
        if (key != serverKey || !server.enabled) continue;
        McpEndpoint endpoint;
        if (!parseMcpEndpoint(server.url, endpoint, error)) return false;
        json response;
        json request = {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
                        {"params", {{"name", toolName}, {"arguments", args}}}};
        if (!mcpPost(endpoint, server.token, server.sessionId, request, response, error, nullptr, server.protocolVersion)) return false;
        const json rpcResult = response.value("result", json::object());
        if (rpcResult.value("isError", false)) { error = "The MCP tool reported an error."; }
        const json content = rpcResult.value("content", json::array());
        result.clear();
        if (content.is_array()) for (const auto& part : content) {
            if (part.value("type", std::string()) == "text") {
                if (!result.empty()) result += "\n";
                result += part.value("text", std::string());
            }
        }
        if (result.empty() && rpcResult.contains("structuredContent")) result = rpcResult["structuredContent"].dump(2);
        if (result.empty()) result = "The MCP tool returned no text content.";
        return error.empty();
    }
    error = "The configured MCP server is disabled or no longer exists. Refresh the server list in Fyxa settings.";
    return false;
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
    const std::string payload = [&]() {
        if (m_provider == "ollama") {
            json messages = json::array({{{"role", "system"}, {"content", AiTools::guide()}}});
            for (const auto& message : m_messages) messages.push_back(message);
            return json{
                {"model", m_ollamaModel},
                {"stream", false},
                {"messages", messages},
                {"tools", ollamaTools(externalMcpTools())}
            }.dump();
        }

        json tools = AiTools::list();
        for (const auto& ext : externalMcpTools())
            tools.push_back({{"name", ext.value("qualifiedName", std::string())},
                             {"description", ext.value("description", std::string())},
                             {"input_schema", ext.value("inputSchema", json::object())}});
        tools.back()["cache_control"] = {{"type", "ephemeral"}};
        json body = {
            {"model", m_model},
            {"max_tokens", 16000},
            {"system", json::array({{{"type", "text"}, {"text", AiTools::guide()}, {"cache_control", {{"type", "ephemeral"}}}}})},
            {"tools", tools},
            {"messages", m_messages},
        };
        std::string headers = "x-api-key: " + m_apiKey + "\r\nanthropic-version: 2023-06-01\r\n";
        if (m_model != "claude-haiku-4-5") {
            body["fallbacks"] = "default";
            headers += "anthropic-beta: server-side-fallback-2026-07-01\r\n";
        }
        return body.dump();
    }();

    const bool ollama = m_provider == "ollama";
    const std::string host = ollama ? m_ollamaHost : "wss://api.anthropic.com";
    const int port = ollama ? m_ollamaPort : 443;
    const std::string path = ollama ? "/api/chat" : "/v1/messages";
    const std::string headers = ollama ? std::string() :
        "x-api-key: " + m_apiKey + "\r\nanthropic-version: 2023-06-01\r\n" +
        (m_model != "claude-haiku-4-5" ? "anthropic-beta: server-side-fallback-2026-07-01\r\n" : "");
    m_pending = std::async(std::launch::async, [payload, host, port, path, headers]() {
        Reply r;
        Net::httpPost(host, port, path, payload, r.body, r.error, 300000, headers, &r.status);
        return r;
    });
    m_scrollDown = true;
}

void AssistantPanel::handleReply(int status, const std::string& body, const std::string& error) {
    if (m_provider == "ollama") handleOllamaReply(status, body, error);
    else handleClaudeReply(status, body, error);
}

void AssistantPanel::handleClaudeReply(int status, const std::string& body, const std::string& error) {
    m_scrollDown = true;
    json j = json::parse(body, nullptr, false);
    if (status != 200 || !j.is_object() || !j.contains("content")) {
        std::string why = j.is_object() && j.contains("error") ? j["error"].value("message", std::string()) : error;
        if (status == 401) why = "Your Anthropic API key wasn't accepted. Check it in Fyxa settings.";
        m_lines.push_back({Line::Error, "Couldn't reach Claude" + (status ? " (" + std::to_string(status) + ")" : std::string()) +
                                            (why.empty() ? "." : ": " + why)});
        while (!m_messages.empty() && m_messages.back().value("role", "") == "user") m_messages.erase(m_messages.size() - 1);
        return;
    }

    m_messages.push_back({{"role", "assistant"}, {"content", j["content"]}});
    for (const auto& b : j["content"])
        if (b.value("type", "") == "text" && !b.value("text", "").empty())
            m_lines.push_back({Line::Assistant, b["text"].get<std::string>()});

    const std::string stop = j.value("stop_reason", "");
    if (stop == "refusal") { m_lines.push_back({Line::Note, "The assistant declined that request."}); return; }
    if (stop == "max_tokens") { m_lines.push_back({Line::Note, "That answer hit the length limit. Say \"continue\" for more."}); return; }
    if (stop != "tool_use") return;

    json results = json::array();
    for (const auto& b : j["content"]) {
        if (b.value("type", "") != "tool_use") continue;
        std::string name = b.value("name", "");
        AiToolResult r;
        if (name.rfind("mcp__", 0) == 0) {
            std::string externalError;
            if (callExternalMcpTool(name, b.contains("input") && b["input"].is_object() ? b["input"] : json::object(), r.text, externalError))
                r.error = false;
            else { r.error = true; r.text = externalError; }
        } else {
            r = m_editor.runAiToolFrom("Fyxa", name, b.contains("input") && b["input"].is_object() ? b["input"] : json::object());
        }
        std::string shown = name;
        if (b.contains("input") && b["input"].contains("object") && b["input"]["object"].is_string())
            shown += " " + b["input"]["object"].get<std::string>();
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

void AssistantPanel::handleOllamaReply(int status, const std::string& body, const std::string& error) {
    m_scrollDown = true;
    json j = json::parse(body, nullptr, false);
    if (status != 200 || !j.is_object() || !j.contains("message") || !j["message"].is_object()) {
        std::string why = j.is_object() ? j.value("error", error) : error;
        m_lines.push_back({Line::Error, "Couldn't reach the Ollama server" +
            (status ? " (" + std::to_string(status) + ")" : std::string()) + (why.empty() ? "." : ": " + why)});
        while (!m_messages.empty() && m_messages.back().value("role", "") == "user") m_messages.erase(m_messages.size() - 1);
        return;
    }

    const json message = j["message"];
    m_messages.push_back(message);
    const std::string answer = message.value("content", std::string());
    if (!answer.empty()) m_lines.push_back({Line::Assistant, answer});

    const json calls = message.value("tool_calls", json::array());
    if (!calls.is_array() || calls.empty()) return;

    for (const auto& call : calls) {
        const json fn = call.value("function", json::object());
        const std::string name = fn.value("name", std::string());
        const json args = fn.value("arguments", json::object());
        if (name.empty()) continue;
        AiToolResult result;
        if (name.rfind("mcp__", 0) == 0) {
            std::string externalError;
            if (callExternalMcpTool(name, args.is_object() ? args : json::object(), result.text, externalError))
                result.error = false;
            else { result.error = true; result.text = externalError; }
        } else {
            result = m_editor.runAiToolFrom("Fyxa", name, args.is_object() ? args : json::object());
        }
        std::string shown = name;
        if (args.contains("object") && args["object"].is_string()) shown += " " + args["object"].get<std::string>();
        m_lines.push_back({result.error ? Line::Error : Line::Tool, shown + (result.error ? ": " + result.text : "")});
        json toolMessage = {{"role", "tool"}, {"name", name}, {"content", result.text}};
        if (!result.png.empty()) toolMessage["images"] = json::array({Online::base64Encode(result.png)});
        m_messages.push_back(std::move(toolMessage));
    }

    if (m_stopAsked) { m_lines.push_back({Line::Note, "Stopped."}); return; }
    if (++m_rounds >= kMaxRounds) { m_lines.push_back({Line::Note, "That's a lot of steps; paused here. Say \"continue\" to keep going."}); return; }
    requestNext();
}

void AssistantPanel::render(bool* open) {
    if (!ImGui::Begin("Fyxa", open)) { ImGui::End(); return; }
    const bool busy = m_pending.valid();

    if (ImGui::CollapsingHeader("Assistant settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* providers[] = {"Ollama (local)", "Claude (Anthropic)"};
        int provider = m_provider == "claude" ? 1 : 0;
        if (ImGui::Combo("AI provider", &provider, providers, 2)) {
            m_provider = provider == 0 ? "ollama" : "claude";
            m_messages = json::array();
            m_lines.clear();
            save();
        }

        if (m_provider == "ollama") {
            ImGui::TextWrapped("Fyxa connects directly to an Ollama server. The default address is local to this computer; install and start Ollama, then pull a model that supports tool calling.");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("Server host", "127.0.0.1", &m_ollamaHost)) save();
            ImGui::SetNextItemWidth(120);
            if (ImGui::InputInt("Server port", &m_ollamaPort)) { m_ollamaPort = std::clamp(m_ollamaPort, 1, 65535); save(); }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("Model", "qwen3:8b", &m_ollamaModel)) save();
            ImGui::TextDisabled("API: http://%s:%d/api/chat", m_ollamaHost.c_str(), m_ollamaPort);
        } else {
            ImGui::TextWrapped("Use your own Anthropic API key. It is stored in this computer's local assistant settings.");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextWithHint("API key", "Anthropic API key (sk-ant-...)", &m_apiKey, ImGuiInputTextFlags_Password)) save();
            int mi = 1;
            for (int i = 0; i < 3; ++i) if (m_model == kClaudeModels[i]) mi = i;
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("Claude model", &mi, kClaudeModelNames, 3)) { m_model = kClaudeModels[mi]; save(); }
        }
    }

    if (ImGui::CollapsingHeader("Connect MCP servers to Fyxa")) {
        ImGui::TextWrapped("Connect external MCP servers so Fyxa can discover and use their tools. Remote endpoints must use HTTPS. Bearer tokens are saved in this computer's local assistant settings.");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##newmcpname", "Server name (e.g. github)", &m_newMcpName);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##newmcpurl", "https://example.com/mcp or http://127.0.0.1:3000/mcp", &m_newMcpUrl);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##newmcptoken", "Bearer token (optional)", &m_newMcpToken, ImGuiInputTextFlags_Password);
        if (ImGui::Button("Add MCP server") && !m_newMcpName.empty() && !m_newMcpUrl.empty()) {
            bool duplicate = false;
            for (const auto& server : m_externalMcp) if (server.name == m_newMcpName) duplicate = true;
            if (duplicate) m_mcpStatus = "A server with that name already exists.";
            else {
                McpServer server; server.name = m_newMcpName; server.url = m_newMcpUrl; server.token = m_newMcpToken;
                m_externalMcp.push_back(std::move(server)); save();
                m_newMcpName.clear(); m_newMcpUrl.clear(); m_newMcpToken.clear();
                refreshMcpServers();
            }
        }
        if (!m_mcpStatus.empty()) ImGui::TextWrapped("%s", m_mcpStatus.c_str());
        for (size_t i = 0; i < m_externalMcp.size(); ++i) {
            auto& server = m_externalMcp[i];
            ImGui::PushID((int)i);
            ImGui::Separator();
            bool enabled = server.enabled;
            if (ImGui::Checkbox(server.name.c_str(), &enabled)) { server.enabled = enabled; save(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh tools")) refreshMcpServers();
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove")) { m_externalMcp.erase(m_externalMcp.begin() + (long)i); save(); ImGui::PopID(); break; }
            ImGui::TextDisabled("%s", server.url.c_str());
            ImGui::TextDisabled("%d discovered tool(s)", server.tools.is_array() ? (int)server.tools.size() : 0);
            ImGui::PopID();
        }
        if (ImGui::Button("Refresh all MCP tools")) refreshMcpServers();
    }

    if (ImGui::CollapsingHeader("Connect AI apps to Studio (MCP)")) {
        ImGui::TextWrapped("This exposes Studio's tools to other AI apps on this computer. It is an MCP server, not an outbound MCP client.");
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

    if (ImGui::CollapsingHeader("Connected apps (online)")) {
        ImGui::TextWrapped("Let apps connected to your Guts&Bolts account use Studio's tools while Studio is open. They remain subject to the permissions granted to each app.");
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

    const float footer = ImGui::GetFrameHeightWithSpacing() * 3.2f;
    ImGui::BeginChild("##chat", ImVec2(0, -footer), ImGuiChildFlags_Borders);
    if (m_lines.empty())
        ImGui::TextWrapped("I'm Fyxa, your Guts&Bolts assistant. Ask me to build a level, write scripts, debug a game, or explain the engine. I can use Studio's native tools, and changes remain available in AI Activity for review and undo.");
    for (const Line& line : m_lines) {
        ImGui::PushTextWrapPos(0.0f);
        switch (line.kind) {
            case Line::You:       ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "You:"); ImGui::TextUnformatted(line.text.c_str()); break;
            case Line::Assistant: ImGui::TextColored(ImVec4(0.95f, 0.7f, 0.45f, 1), "Fyxa:"); ImGui::TextUnformatted(line.text.c_str()); break;
            case Line::Tool:      ImGui::TextDisabled("  > %s", line.text.c_str()); break;
            case Line::Note:      ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s", line.text.c_str()); break;
            case Line::Error:     ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "%s", line.text.c_str()); break;
        }
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    }
    if (busy) ImGui::TextDisabled("Working%s", std::string((size_t)((int)(ImGui::GetTime() * 3) % 4), '.').c_str());
    if (m_scrollDown) { ImGui::SetScrollHereY(1.0f); m_scrollDown = false; }
    ImGui::EndChild();

    const bool configured = m_provider == "ollama" ? (!m_ollamaHost.empty() && !m_ollamaModel.empty() && m_ollamaPort > 0)
                                                    : !m_apiKey.empty();
    ImGui::BeginDisabled(!configured);
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
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Everything Fyxa did and changed. Undo all of it in one click.");
    if (!configured) ImGui::TextDisabled(m_provider == "ollama" ? "Configure the Ollama server and model in Assistant settings." :
                                                                  "Add your Anthropic API key in Assistant settings.");
    ImGui::End();
}
