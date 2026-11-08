#pragma once
#include <future>
#include <utility>
#include <deque>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../StudioMcp.h"
#include "../StudioRelay.h"

class Editor;

// Fyxa is the Guts & Bolts platform-wide AI agent. This panel is its Studio surface;
// platform capabilities are added through permission-checked providers. It can use
// a cloud provider or local Ollama while Studio edits continue through native AI tools.
class AssistantPanel {
public:
    explicit AssistantPanel(Editor& editor);
    ~AssistantPanel();
    void update();
    void render(bool* open);

private:
    struct Line { enum Kind { You, Assistant, Tool, Note, Error } kind; std::string text; };
    void load();
    void save() const;
    void send(const std::string& text);
    void requestNext();
    void handleReply(int status, const std::string& body, const std::string& error);
    void handleClaudeReply(int status, const std::string& body, const std::string& error);
    void handleOllamaReply(int status, const std::string& body, const std::string& error);
    void refreshMcpServers();
    void authorizeMcpServer(const std::string& name);
    nlohmann::json externalMcpTools() const;
    std::string externalMcpFingerprint(const std::string& qualifiedName) const;
    struct ExternalCall { bool claude = false; std::string name, id, definitionFingerprint; nlohmann::json args; };
    struct ExternalCompletion { ExternalCall call; std::string text, error; };
    void startNextExternalCall();
    void finishExternalCalls();
    std::string recordToolEvidence();

    Editor&     m_editor;
    StudioMcp   m_mcp;
    bool        m_mcpWanted = false;
    int         m_mcpPort = StudioMcp::kDefaultPort;
    std::string m_mcpError;
    StudioRelay m_relay;
    bool        m_appsWanted = false;

    std::string m_provider = "ollama";
    std::string m_apiKey;
    std::string m_credentialStatus;
    std::string m_model = "claude-sonnet-5-5";
    std::string m_ollamaHost = "127.0.0.1";
    int         m_ollamaPort = 11434;
    std::string m_ollamaModel = "qwen3:8b";
    struct McpServer {
        std::string name, url, token, credentialKey, sessionId, protocolVersion = "2026-07-28";
        std::string oauthClientId, oauthRefreshToken;
        bool enabled = false;
        nlohmann::json tools = nlohmann::json::array();
    };
    struct McpRefreshResult { std::vector<McpServer> servers; std::string status; };
    struct McpOAuthResult { std::string serverName, accessToken, refreshToken, issuer, error; };
    std::future<McpOAuthResult> m_oauthPending;
    std::string m_oauthServerName;
    std::vector<McpServer> m_externalMcp;
    std::future<McpRefreshResult> m_mcpRefreshPending;
    std::string m_newMcpName, m_newMcpUrl, m_newMcpToken, m_newMcpClientId;
    std::string m_mcpStatus;
    std::string m_input;
    nlohmann::json m_messages = nlohmann::json::array();
    std::vector<Line> m_lines;
    struct Reply { int status = 0; std::string body, error; };
    std::future<Reply> m_pending;
    std::future<ExternalCompletion> m_externalPending;
    std::deque<ExternalCall> m_externalQueue;
    nlohmann::json m_deferredClaudeResults = nlohmann::json::array();
    nlohmann::json m_deferredOllamaMessages = nlohmann::json::array();
    bool m_deferredExternalClaude = false;
    bool m_deferredExternalOllama = false;
    bool m_externalApprovalPending = false;
    bool m_externalApprovalGranted = false;
    int         m_rounds = 0;
    bool        m_stopAsked = false;
    bool        m_scrollDown = false;
    std::string m_activeTaskId;
};
