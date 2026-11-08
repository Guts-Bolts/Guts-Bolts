#pragma once
#include <future>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include "../net/Socket.h"

class Editor;
class StudioMcp;

// "Allow connected apps to use AI tools" (Assistant tab). Studio dials OUT to the Guts&Bolts
// server's /ws, says mcp.serve (a normal signed request, so the server knows whose Studio it is),
// and then answers the MCP calls that apps you connected with "Log in with Guts&Bolts" (like
// Lyreto) send to https://gutsandbolts.net/mcp. The answers come from the same code as the local
// MCP server (StudioMcp), on the main thread, but only with the tools the app was allowed and
// never publishing. No port is opened on this computer. The server side: worker/mcp-relay.js.
// (Based on the plan the Lyreto team sent.)
class StudioRelay {
public:
    enum class State { Off, Waiting, Connecting, Online, Failed };
    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }
    void update(Editor& editor, StudioMcp& mcp);    // every frame
    State state() const { return m_state; }
    const std::string& statusText() const { return m_status; }
    int  callsServed() const { return m_calls; }
    const std::string& lastTool() const { return m_lastTool; }
    const std::string& lastApp() const { return m_lastApp; }
    bool approvalPending() const { return m_pendingApproval.has_value(); }
    const std::string& pendingApp() const;
    const std::string& pendingTool() const;
    const std::string& pendingArguments() const;
    void resolveApproval(bool approve, Editor& editor, StudioMcp& mcp);

private:
    struct Dialed { std::unique_ptr<Net::Connection> conn; std::string error; };
    struct PendingApproval {
        nlohmann::json relayId;
        nlohmann::json message;
        std::string app, tool, fingerprint, arguments;
        double expiresAt = 0;
        bool canChange = false;
    };
    void drop(State state, const std::string& why, bool retry);
    void onMessage(Editor& editor, StudioMcp& mcp, const std::string& text);
    static double now();

    bool        m_enabled = false;
    bool        m_ready = false;       // the server said mcp.ready
    bool        m_replaced = false;    // another computer took over: wait until it's switched off and on
    State       m_state = State::Off;
    std::string m_status = "Off";
    std::string m_account;             // whose Studio this connection serves (a new login starts again)
    std::string m_game;                // the open game's name, as last told to the server
    std::unique_ptr<Net::Connection> m_conn;
    std::future<std::shared_ptr<Dialed>> m_dialing;
    std::shared_ptr<std::atomic_bool> m_dialCancel;
    std::optional<PendingApproval> m_pendingApproval;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_appToolFingerprints;
    double      m_nextTry = 0, m_backoff = 2, m_lastPing = 0, m_helloAt = 0;
    int         m_calls = 0;
    std::string m_lastTool, m_lastApp;
};
