#include "StudioRelay.h"
#include "StudioMcp.h"
#include "AiTools.h"
#include "ExternalMcpProtocol.h"
#include "Editor.h"
#include "../online/OnlineClient.h"
#include "../core/Account.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "Version.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <fstream>

using nlohmann::json;

namespace {
constexpr double kPingEvery = 20;      // the server drops a quiet Studio after 90 s (kHostSilence)
constexpr double kHelloWait = 15;
constexpr double kMaxBackoff = 60;
constexpr int    kHelloId = 1;

// The open game's name, for the app ("Obby Rush"): its file name, or "" if it was never saved.
std::string gameName(const Editor& editor) {
    return editor.filePath().empty() ? std::string() : std::filesystem::path(editor.filePath()).stem().string();
}
json redactRelayArguments(const json& value) {
    if (value.is_array()) {
        json out = json::array();
        for (const auto& item : value) out.push_back(redactRelayArguments(item));
        return out;
    }
    if (!value.is_object()) return value;
    json out = json::object();
    for (auto it = value.begin(); it != value.end(); ++it) {
        std::string key = it.key(), lower = key;
        for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
        const bool secret = lower.find("token") != std::string::npos ||
            lower.find("secret") != std::string::npos || lower.find("password") != std::string::npos ||
            lower.find("authorization") != std::string::npos || lower.find("apikey") != std::string::npos ||
            lower.find("api_key") != std::string::npos || lower.find("verifier") != std::string::npos ||
            lower.find("private") != std::string::npos || lower.find("credential") != std::string::npos ||
            lower.find("cookie") != std::string::npos;
        out[key] = secret ? json("[REDACTED]") : redactRelayArguments(it.value());
    }
    return out;
}

std::string connectedToolFingerprint(const std::string& app, const std::string& toolName) {
    for (const auto& tool : AiTools::mcpTools()) {
        if (tool.is_object() && tool.value("name", std::string()) == toolName)
            return McpHttpProtocol::definitionFingerprint(
                "connected-app:" + app, "https://gutsandbolts.net/mcp", tool);
    }
    return {};
}

void appendRelayAudit(const std::string& app, const std::string& tool,
                      const std::string& fingerprint, const json& arguments,
                      const std::string& outcome) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    std::ofstream file(Paths::file("assistant-mcp-activity.jsonl"), std::ios::app);
    if (file) file << json{{"timestampMs", ms}, {"server", "connected-app-relay"},
        {"app", app}, {"tool", tool}, {"toolFingerprint", fingerprint},
        {"arguments", redactRelayArguments(arguments)}, {"outcome", outcome}}.dump() << "\n";
}

json relayRpcError(const json& id, const std::string& message) {
    return {{"jsonrpc", "2.0"}, {"id", id},
            {"error", {{"code", -32003}, {"message", message}}}};
}

} // namespace

double StudioRelay::now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void StudioRelay::setEnabled(bool on) {
    if (on == m_enabled) return;
    m_enabled = on;
    m_replaced = false;
    m_backoff = 2;
    m_nextTry = 0;
    if (on) { m_state = State::Connecting; m_status = "Connecting..."; Log::system("Connected apps can use Studio's AI tools (through gutsandbolts.net)."); }
    else { drop(State::Off, "Off", false); Log::system("Connected apps can't use Studio any more."); }
}

void StudioRelay::drop(State state, const std::string& why, bool retry) {
    if (m_dialCancel) m_dialCancel->store(true);
    m_dialCancel.reset();
    if (m_pendingApproval) {
        const json params = m_pendingApproval->message.value("params", json::object());
        appendRelayAudit(m_pendingApproval->app, m_pendingApproval->tool,
                         m_pendingApproval->fingerprint,
                         params.value("arguments", json::object()), "connection-dropped-before-resolution");
    }
    m_conn.reset();
    m_pendingApproval.reset();
    m_appToolFingerprints.clear();
    m_ready = false;
    m_state = state;
    m_status = why;
    if (retry) {
        m_nextTry = now() + m_backoff;
        m_backoff = std::min(kMaxBackoff, m_backoff * 2);
    }
}

void StudioRelay::update(Editor& editor, StudioMcp& mcp) {
    if (m_pendingApproval && now() >= m_pendingApproval->expiresAt) {
        PendingApproval expired = std::move(*m_pendingApproval);
        m_pendingApproval.reset();
        if (m_conn) {
            const json timeout = relayRpcError(expired.message.value("id", json()),
                "The Studio approval request expired. Ask the connected app to retry.");
            m_conn->send(json{{"t", "mcp.reply"}, {"id", expired.relayId}, {"reply", timeout}}.dump());
        }
        const json params = expired.message.value("params", json::object());
        appendRelayAudit(expired.app, expired.tool, expired.fingerprint,
                         params.value("arguments", json::object()), "approval-expired");
    }
    if (!m_enabled || m_replaced) return;
    // A dial in progress (connectTo waits, so it runs off the main thread).
    if (m_dialing.valid()) {
        if (m_dialing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto d = m_dialing.get();
        m_dialCancel.reset();
        if (!d->conn) { drop(State::Failed, "Couldn't reach Guts&Bolts (" + d->error + "). Trying again soon.", true); return; }
        m_conn = std::move(d->conn);
        m_account = Account::id();
        m_game = gameName(editor);
        json hello = Online::signedRequest("mcp.serve", {{"studio", GB_VERSION}, {"game", m_game}});
        hello["id"] = kHelloId;
        m_conn->send(hello.dump());
        m_helloAt = now();
        m_status = "Connecting...";
    }
    // Only for a signed-up account, and only the one that's logged in now.
    if (!Online::online() || Online::isGuest()) {
        if (m_conn) drop(State::Waiting, "", false);
        m_state = State::Waiting;
        m_status = "Log in to Guts&Bolts (not as a guest) to let connected apps in.";
        return;
    }
    if (m_conn && m_account != Account::id()) { drop(State::Connecting, "You logged in as someone else. Reconnecting...", false); return; }
    if (!m_conn) {
        if (now() < m_nextTry) return;
        std::string host;
        int port = 0;
        if (!Online::serverHostPort(host, port) || !Net::isWebAddress(host)) {
            drop(State::Failed, "Connected apps only work with the gutsandbolts.net server.", true);
            return;
        }
        m_state = State::Connecting;
        m_status = "Connecting...";
        m_dialCancel = std::make_shared<std::atomic_bool>(false);
        const auto cancel = m_dialCancel;
        m_dialing = std::async(std::launch::async, [host, port, cancel]() {
            auto d = std::make_shared<Dialed>();
            d->conn = Net::Connection::connectTo(host, port, d->error, 8000, cancel.get());
            return d;
        });
        return;
    }
    const bool alive = m_conn->poll();
    std::string msg;
    while (m_conn && m_conn->pop(msg)) onMessage(editor, mcp, msg);
    if (!m_conn) return;   // (onMessage dropped it)
    if (!alive) { drop(State::Connecting, "Lost the connection. Reconnecting...", true); return; }
    const double t = now();
    if (!m_ready && t - m_helloAt > kHelloWait) { drop(State::Failed, "Guts&Bolts didn't answer. Trying again soon.", true); return; }
    if (m_ready && gameName(editor) != m_game) {   // another game was opened or saved under a new name
        m_game = gameName(editor);
        m_conn->send(json{{"t", "mcp.status"}, {"game", m_game}}.dump());
    }
    if (m_ready && t - m_lastPing > kPingEvery) { m_conn->send(json{{"t", "ping"}}.dump()); m_lastPing = t; }
    m_conn->poll();   // send what's queued now
}

void StudioRelay::onMessage(Editor& editor, StudioMcp& mcp, const std::string& text) {
    json m = json::parse(text, nullptr, false);
    if (!m.is_object()) return;
    if (!m_ready) {
        if (!m.contains("id") || m["id"] != kHelloId) return;
        if (!m.value("ok", false)) { drop(State::Failed, m.value("error", std::string("Guts&Bolts said no.")), true); return; }
        m_ready = true;
        m_backoff = 2;
        m_lastPing = now();
        m_state = State::Online;
        m_status = "On: apps you've connected on gutsandbolts.net can use Studio while it's open.";
        return;
    }
    const std::string kind = m.value("t", "");
    if (kind == "mcp.replaced") {
        drop(State::Failed, "Studio was opened on another computer, so that one serves your apps now.", false);
        m_replaced = true;
        return;
    }
    if (kind != "mcp.call" || !m.contains("message") || !m["message"].is_object()) return;
    const std::string app = m.contains("app") && m["app"].is_string() ? m["app"].get<std::string>() : std::string("App");
    bool canChange = false;
    if (m.contains("scopes") && m["scopes"].is_array())
        for (const auto& scope : m["scopes"]) if (scope == "studio.write") canChange = true;
    const json& message = m["message"];
    const std::string method = message.value("method", std::string());

    if (method == "tools/list") {
        bool anyReply = false;
        const std::string reply = mcp.handleApp(editor, message.dump(), anyReply, app, canChange);
        if (anyReply) {
            json parsed = json::parse(reply, nullptr, false);
            auto& fingerprints = m_appToolFingerprints[app];
            fingerprints.clear();
            json tools = json::array();
            if (parsed.is_object() && parsed.contains("result") && parsed["result"].is_object())
                tools = parsed["result"].value("tools", json::array());
            if (tools.is_array()) for (const auto& tool : tools) {
                if (!tool.is_object() || !tool.contains("name") || !tool["name"].is_string()) continue;
                const std::string name = tool["name"].get<std::string>();
                const json meta = tool.value("_meta", json::object());
                const std::string fingerprint = meta.is_object()
                    ? meta.value("io.gutsandbolts/tool-fingerprint", std::string()) : std::string();
                if (!name.empty() && !fingerprint.empty()) fingerprints[name] = fingerprint;
            }
            m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", parsed}}.dump());
        } else {
            m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", nullptr}}.dump());
        }
        return;
    }

    if (method == "tools/call") {
        const json params = message.value("params", json::object());
        if (!params.is_object()) {
            const json error = relayRpcError(message.value("id", json()), "Tool parameters must be an object.");
            m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", error}}.dump());
            return;
        }
        const std::string tool = params.value("name", std::string());
        const auto appTools = m_appToolFingerprints.find(app);
        const std::string pinned = appTools == m_appToolFingerprints.end() ||
            !appTools->second.contains(tool) ? std::string() : appTools->second.at(tool);
        const std::string current = connectedToolFingerprint(app, tool);
        if (tool.empty() || pinned.empty() || current.empty() || current != pinned) {
            const json error = relayRpcError(message.value("id", json()),
                "The connected-app tool definition is missing or changed. Refresh tools/list before retrying.");
            m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", error}}.dump());
            appendRelayAudit(app, tool, current, params.value("arguments", json::object()), "definition-mismatch");
            return;
        }
        if (m_pendingApproval) {
            const json error = relayRpcError(message.value("id", json()),
                "Studio already has a connected-app action awaiting user approval.");
            m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", error}}.dump());
            return;
        }
        const json arguments = params.value("arguments", json::object());
        if (!arguments.is_object()) {
            const json error = relayRpcError(message.value("id", json()), "Tool arguments must be an object.");
            m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", error}}.dump());
            return;
        }
        PendingApproval pending;
        pending.relayId = m["id"];
        pending.message = message;
        pending.app = app;
        pending.tool = tool;
        pending.fingerprint = pinned;
        pending.arguments = redactRelayArguments(arguments).dump(2);
        pending.expiresAt = now() + 110.0;
        pending.canChange = canChange;
        m_pendingApproval = std::move(pending);
        appendRelayAudit(app, tool, pinned, arguments, "approval-requested");
        return;
    }

    // Discovery methods remain scope-checked by StudioMcp; tools/call is always
    // gated by a fresh Studio-side approval above.
    bool anyReply = false;
    const std::string reply = mcp.handleApp(editor, message.dump(), anyReply, app, canChange);
    json parsed = anyReply ? json::parse(reply, nullptr, false) : json();
    if (!parsed.is_discarded() && parsed.is_object())
        m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", parsed}}.dump());
    else
        m_conn->send(json{{"t", "mcp.reply"}, {"id", m["id"]}, {"reply", nullptr}}.dump());
}

const std::string& StudioRelay::pendingApp() const {
    static const std::string empty;
    return m_pendingApproval ? m_pendingApproval->app : empty;
}

const std::string& StudioRelay::pendingTool() const {
    static const std::string empty;
    return m_pendingApproval ? m_pendingApproval->tool : empty;
}

const std::string& StudioRelay::pendingArguments() const {
    static const std::string empty;
    return m_pendingApproval ? m_pendingApproval->arguments : empty;
}

void StudioRelay::resolveApproval(bool approve, Editor& editor, StudioMcp& mcp) {
    if (!m_pendingApproval) return;
    PendingApproval pending = std::move(*m_pendingApproval);
    m_pendingApproval.reset();
    if (!m_conn || m_state != State::Online) return;

    const json params = pending.message.value("params", json::object());
    const json arguments = params.value("arguments", json::object());
    if (!approve) {
        const json denied = relayRpcError(pending.message.value("id", json()),
            "The user denied this connected-app action in Studio.");
        m_conn->send(json{{"t", "mcp.reply"}, {"id", pending.relayId}, {"reply", denied}}.dump());
        appendRelayAudit(pending.app, pending.tool, pending.fingerprint, arguments, "denied-by-user");
        return;
    }

    const std::string current = connectedToolFingerprint(pending.app, pending.tool);
    if (current.empty() || current != pending.fingerprint || !appMay(pending.tool, pending.canChange)) {
        const json stale = relayRpcError(pending.message.value("id", json()),
            "The tool definition or connected-app permission changed while approval was pending. Refresh and request approval again.");
        m_conn->send(json{{"t", "mcp.reply"}, {"id", pending.relayId}, {"reply", stale}}.dump());
        appendRelayAudit(pending.app, pending.tool, pending.fingerprint, arguments, "approval-invalidated");
        return;
    }

    bool anyReply = false;
    const std::string reply = mcp.handleApp(editor, pending.message.dump(), anyReply,
                                             pending.app, pending.canChange);
    json parsed = anyReply ? json::parse(reply, nullptr, false) : json();
    if (!parsed.is_discarded() && parsed.is_object()) {
        m_conn->send(json{{"t", "mcp.reply"}, {"id", pending.relayId}, {"reply", parsed}}.dump());
        ++m_calls;
        m_lastTool = pending.tool;
        m_lastApp = pending.app;
        appendRelayAudit(pending.app, pending.tool, pending.fingerprint, arguments, "approved-and-executed");
    } else {
        const json failed = relayRpcError(pending.message.value("id", json()),
            "Studio could not produce a valid response for the approved action.");
        m_conn->send(json{{"t", "mcp.reply"}, {"id", pending.relayId}, {"reply", failed}}.dump());
        appendRelayAudit(pending.app, pending.tool, pending.fingerprint, arguments, "approved-execution-error");
    }
}

