#include "StudioRelay.h"
#include "StudioMcp.h"
#include "Editor.h"
#include "../online/OnlineClient.h"
#include "../core/Account.h"
#include "../core/Log.h"
#include "Version.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <nlohmann/json.hpp>

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
    m_conn.reset();
    m_ready = false;
    m_state = state;
    m_status = why;
    if (retry) {
        m_nextTry = now() + m_backoff;
        m_backoff = std::min(kMaxBackoff, m_backoff * 2);
    }
}

void StudioRelay::update(Editor& editor, StudioMcp& mcp) {
    if (!m_enabled || m_replaced) return;
    // A dial in progress (connectTo waits, so it runs off the main thread).
    if (m_dialing.valid()) {
        if (m_dialing.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto d = m_dialing.get();
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
        m_dialing = std::async(std::launch::async, [host, port]() {
            auto d = std::make_shared<Dialed>();
            d->conn = Net::Connection::connectTo(host, port, d->error, 8000);
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
        for (const auto& s : m["scopes"]) if (s == "studio.write") canChange = true;
    bool anyReply = false;
    // The same handler as http://127.0.0.1:44755/mcp, in "app" mode: only the app tools, never publishing.
    const std::string reply = mcp.handleApp(editor, m["message"].dump(), anyReply, app, canChange);
    json out = {{"t", "mcp.reply"}, {"id", m.contains("id") ? m["id"] : json()}, {"reply", nullptr}};
    if (anyReply) {
        json r = json::parse(reply, nullptr, false);
        if (!r.is_discarded()) out["reply"] = r;
    }
    const json& message = m["message"];
    if (message.value("method", std::string()) == "tools/call" && message.contains("params") && message["params"].is_object()) {
        ++m_calls;
        m_lastTool = message["params"].value("name", std::string());
        m_lastApp = app;
    }
    m_conn->send(out.dump());
}
