#include "StudioMcp.h"
#include "Editor.h"
#include "AiTools.h"
#include "../online/Protocol.h"   // base64
#include "../core/Log.h"
#include "Version.h"

#include <cstdio>
#include <iostream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {
const char* kProtocolVersions[] = {"2025-06-18", "2025-03-26", "2024-11-05"};

json rpcError(const json& id, int code, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}};
}

// The tools apps connected through gutsandbolts.net may use (the server has the same lists:
// worker/mcp-policy.js). Read ones need studio.read; the rest studio.write. Anything else, like
// publish_game or compare_scenes (it opens files by path), is refused here even if the server let it by.
const char* kAppReadTools[] = {"get_engine_info", "get_game_tree", "find_objects", "get_object", "read_script", "get_output", "get_errors", "diagnose_object", "analyze_scene_performance", "analyze_script_dependencies", "validate_scene", "find_path", "get_runtime_state", "screenshot"};
const char* kAppWriteTools[] = {"insert_object", "delete_object", "set_property", "find_and_change", "select", "create_script", "edit_script", "visual_script", "generate_level", "run_lua", "playtest", "multiplayer_test", "physics_replay", "lua_debugger", "create_physical_object", "spawn_explosion", "bake_navmesh", "walk_character_to", "script_history", "ai_activity", "checkpoint", "undo", "redo"};

bool appMay(const std::string& tool, bool canChange) {
    for (const char* t : kAppReadTools) if (tool == t) return true;
    if (canChange) for (const char* t : kAppWriteTools) if (tool == t) return true;
    return false;
}
} // namespace

bool StudioMcp::start(int port, std::string& error) {
    m_port = port;
    if (!m_http.open(port, error)) return false;
    Log::system("AI tools (MCP) are on at http://127.0.0.1:" + std::to_string(port) + "/mcp");
    return true;
}

void StudioMcp::stop() {
    if (m_http.isOpen()) Log::system("AI tools (MCP) are off.");
    m_http.close();
}

void StudioMcp::update(Editor& editor) {
    if (!m_http.isOpen()) return;
    m_http.poll();
    Net::HttpServer::Request req;
    while (m_http.next(req)) {
        // Web pages must not be able to reach Studio (only local apps).
        size_t o = req.headers.find("\norigin:");
        if (o != std::string::npos) {
            std::string origin = req.headers.substr(o + 8, req.headers.find('\r', o + 1) - o - 8);
            if (origin.find("127.0.0.1") == std::string::npos && origin.find("localhost") == std::string::npos) {
                m_http.respond(req.id, 403, "text/plain", "Only apps on this computer can use Studio's AI tools.");
                continue;
            }
        }
        if (req.path.rfind("/mcp", 0) != 0) { m_http.respond(req.id, 404, "text/plain", "Try /mcp"); continue; }
        if (req.method == "GET") { m_http.respond(req.id, 405, "text/plain", "Send JSON-RPC with POST."); continue; }
        if (req.method == "DELETE") { m_http.respond(req.id, 200, "", ""); continue; }
        if (req.method != "POST") { m_http.respond(req.id, 405, "text/plain", ""); continue; }
        bool anyReply = false;
        std::string reply = handle(editor, req.body, anyReply);
        if (anyReply) m_http.respond(req.id, 200, "application/json", reply);
        else m_http.respond(req.id, 202, "", "");
    }
    m_http.poll();   // start sending the answers now
}

std::string StudioMcp::handle(Editor& editor, const std::string& body, bool& anyReply, const std::string& app, bool canChange) {
    const bool fromApp = !app.empty();
    json in = json::parse(body, nullptr, false);
    if (in.is_discarded()) { anyReply = true; return rpcError(nullptr, -32700, "That wasn't valid JSON.").dump(); }
    auto one = [&](const json& m) -> json {
        if (!m.is_object() || !m.contains("method")) return rpcError(m.value("id", json()), -32600, "Not a JSON-RPC request.");
        const std::string method = m.value("method", "");
        const bool notification = !m.contains("id");
        const json id = notification ? json() : m["id"];
        if (notification) return json();   // notifications/initialized etc.: nothing to say
        const json params = m.value("params", json::object());
        if (method == "initialize") {
            std::string want = params.value("protocolVersion", std::string(kProtocolVersions[0]));
            std::string use = kProtocolVersions[0];
            for (const char* v : kProtocolVersions) if (want == v) use = v;
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {
                {"protocolVersion", use},
                {"capabilities", {{"tools", {{"listChanged", false}}}, {"resources", {{"listChanged", false}}}, {"prompts", {{"listChanged", false}}}}},
                {"serverInfo", {{"name", "guts-and-bolts-studio"}, {"version", GB_VERSION}}},
                {"instructions", AiTools::guide()}}}};
        }
        if (method == "ping") return {{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}};
        if (method == "tools/list") {
            json tools = AiTools::mcpTools();
            if (fromApp) {
                json kept = json::array();
                for (const auto& t : tools) if (appMay(t.value("name", std::string()), canChange)) kept.push_back(t);
                tools = kept;
            }
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"tools", tools}}}};
        }
        // Resources: the engine reference (gutsbolts://concepts ...) and live views of the game.
        if (method == "resources/list")
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"resources", AiTools::resources()}}}};
        if (method == "resources/templates/list")
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"resourceTemplates", json::array()}}}};
        if (method == "resources/read") {
            const std::string uri = params.value("uri", "");
            const std::string page = uri.rfind("gutsbolts://", 0) == 0 ? uri.substr(12) : std::string();
            std::string text, mime = "text/markdown";
            if (page == "guide") text = AiTools::guide();
            else if (page == "capabilities") { text = AiTools::capabilities().dump(2); mime = "application/json"; }
            else if (page == "classes") { text = editor.runAiTool("get_engine_info", {{"topic", "classes"}}).text; mime = "application/json"; }
            else if (page == "scene/tree") { text = editor.runAiTool("get_game_tree", {{"depth", 6}}).text; mime = "application/json"; }
            else if (page == "runtime") { text = editor.runAiTool("get_runtime_state", json::object()).text; mime = "application/json"; }
            else if (page == "errors") { text = editor.runAiTool("get_errors", json::object()).text; mime = "application/json"; }
            else if (!page.empty()) text = AiTools::engineDoc(page);
            if (text.empty()) return rpcError(id, -32002, "There's no resource \"" + uri + "\". See resources/list.");
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"contents", json::array({{{"uri", uri}, {"mimeType", mime}, {"text", text}}})}}}};
        }
        if (method == "prompts/list")
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"prompts", AiTools::prompts()}}}};
        if (method == "prompts/get") {
            json p = AiTools::prompt(params.value("name", ""), params.value("arguments", json::object()));
            if (p.is_null()) return rpcError(id, -32602, "There's no prompt called \"" + params.value("name", "") + "\".");
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", p}};
        }
        if (method == "tools/call") {
            std::string name = params.value("name", "");
            json args = params.value("arguments", json::object());
            if (fromApp && !appMay(name, canChange))
                return rpcError(id, -32003, "\"" + name + "\" can't be used by " + app + ". Publishing and files stay in Studio, done by you.");
            AiToolResult r = editor.runAiToolFrom(fromApp ? "Connected app (" + app + ")" : std::string("AI app"), name,
                                                  args.is_object() ? args : json::object());
            ++m_calls;
            m_lastTool = name;
            json content = json::array({{{"type", "text"}, {"text", r.text}}});
            if (!r.png.empty())
                content.push_back({{"type", "image"}, {"data", Online::base64Encode(r.png)}, {"mimeType", "image/png"}});
            json result = {{"content", content}, {"isError", r.error}};
            if (r.data.is_object()) result["structuredContent"] = r.data;   // the same JSON, for apps that read it directly
            return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
        }
        return rpcError(id, -32601, "Studio doesn't know \"" + method + "\".");
    };
    // (A message with the wrong types, like a number for a tool's name, gets an error, not a crash.)
    auto safe = [&](const json& m) -> json {
        try { return one(m); }
        catch (const std::exception&) { return rpcError(m.is_object() && m.contains("id") ? m["id"] : json(), -32602, "Bad parameters."); }
    };
    if (in.is_array()) {
        json out = json::array();
        for (const auto& m : in) { json r = safe(m); if (!r.is_null()) out.push_back(r); }
        anyReply = !out.empty();
        return out.dump();
    }
    json r = safe(in);
    anyReply = !r.is_null();
    return r.dump();
}

int StudioMcp::runStdioBridge(int port) {
    // MCP over stdio: one JSON message per line in, one per line out.
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty() || line == "\r") continue;
        std::string reply, error;
        int status = 0;
        bool ok = Net::httpPost("127.0.0.1", port, "/mcp", line, reply, error, 300000, "", &status);
        if (!ok && status == 0) {
            json m = json::parse(line, nullptr, false);
            if (m.is_object() && m.contains("id"))
                reply = rpcError(m["id"], -32000, "Guts and Bolts Studio isn't reachable. Open Studio, go to the Assistant tab "
                                                  "and turn on \"Let AI apps use Studio (MCP)\".").dump();
            else reply.clear();
        }
        if (status == 202) reply.clear();
        if (!reply.empty()) { std::fwrite(reply.data(), 1, reply.size(), stdout); std::fputc('\n', stdout); std::fflush(stdout); }
    }
    return 0;
}
