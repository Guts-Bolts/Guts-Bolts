#pragma once
#include <future>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../StudioMcp.h"
#include "../StudioRelay.h"

class Editor;

// Studio's Assistant tab (like Roblox Studio's Assistant). Two things:
//  * Chat with Claude, using your own Anthropic API key. It can see and change
//    your game with Studio's AI tools (AiTools.h): build, script, playtest,
//    take screenshots. Everything it does can be undone.
//  * Switch on MCP so AI apps on this computer (Claude Desktop, Claude Code,
//    other MCP tools) can use the same tools, and/or let apps you connected
//    to your account (like Lyreto) use them online (StudioRelay).
class AssistantPanel {
public:
    explicit AssistantPanel(Editor& editor);
    ~AssistantPanel();
    void update();                   // every frame (MCP requests, the chat's replies)
    void render(bool* open);

private:
    struct Line { enum Kind { You, Claude, Tool, Note, Error } kind; std::string text; };
    void load();
    void save() const;
    void send(const std::string& text);
    void requestNext();              // ask Claude to carry on with the conversation so far
    void handleReply(int status, const std::string& body, const std::string& error);

    Editor&     m_editor;
    StudioMcp   m_mcp;
    bool        m_mcpWanted = false;
    int         m_mcpPort = StudioMcp::kDefaultPort;
    std::string m_mcpError;
    StudioRelay m_relay;             // "Allow connected apps to use AI tools" (gutsandbolts.net/mcp)
    bool        m_appsWanted = false;   // saved as "connectedApps" in assistant.json

    std::string m_apiKey, m_model = "claude-opus-5-5", m_input;
    nlohmann::json m_messages = nlohmann::json::array();   // the conversation, as Claude sees it
    std::vector<Line> m_lines;       // the conversation, as shown
    struct Reply { int status = 0; std::string body, error; };
    std::future<Reply> m_pending;
    int         m_rounds = 0;        // tool rounds in this answer (stops runaway loops)
    bool        m_stopAsked = false;
    bool        m_scrollDown = false;
};
