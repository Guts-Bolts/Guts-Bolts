#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Socket.h"
#include "../core/Log.h"
#include "../game/PlayerEntry.h"
#include <nlohmann/json.hpp>
#include "../scene/Player.h"   // CharacterPose
#include <nlohmann/json.hpp>

class Scene;
class GameSession;

// Chat messages and the speech bubbles above characters' heads.
struct ChatLog {
    struct Line {
        std::string from, text;
        bool system = false; bool admin = false; bool verified = false;
        bool whisper = false; std::string to;   // a private message (/w name message): only the two see it
        // Translation: the language it was said in ("" = not known), and once it's been put
        // into yours, what was really said (text is then the translation).
        std::string lang, original;
        bool translated = false, asked = false;
        uint64_t id = 0;
    };
    std::vector<Line> lines;
    std::unordered_map<std::string, std::pair<std::string, float>> bubbles;   // name -> text, seconds left
    bool translate = true;   // put lines said in other languages into yours (not on a dedicated server: nobody reads it)

    // `lang`: the language the sender writes in (see myLanguage).
    void add(const std::string& from, const std::string& text, bool system = false, bool admin = false,
             bool verified = false, const std::string& lang = "");
    // Private messages get no speech bubble (everyone would see it).
    void addWhisper(const std::string& from, const std::string& to, const std::string& text, bool admin = false,
                    bool verified = false, const std::string& lang = "");
    void update(float dt);
    // The language to send with what you say ("" when it can't be told), and a sender's one checked.
    static std::string myLanguage();
    static std::string cleanLanguage(const std::string& lang);

    // "/w name message" or "/whisper name message" (like Roblox). True if `text` is
    // a whisper command; `to` and `message` may be empty if it's incomplete.
    static bool parseWhisper(const std::string& text, std::string& to, std::string& message);
    static constexpr const char* kWhisperHelp = "To whisper to someone: /w PlayerName your message";

private:
    void translateNew();   // ask the server to translate lines said in another language (ChatTranslate.cpp)
    uint64_t m_nextId = 1;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);   // (translations that come back after the log is gone are dropped)
};

inline constexpr int kDefaultPort = 7777;

// Makes other players move smoothly. Their poses arrive about 20 times a second,
// and not evenly (the internet bunches them up). Showing each one as it arrives
// looks jumpy, so instead we keep the last second of poses, each stamped with the
// time the sender took it, and show the player a tenth of a second in the past,
// gliding between the two poses around that moment. If updates stop for a moment,
// the player keeps going the way they were (briefly), then waits.
class PoseBuffer {
public:
    // `sentAt`: the sender's clock when it took the pose; `now`: our clock.
    void push(double sentAt, double now, const CharacterPose& pose);
    // The pose to show right now. False until the first pose has arrived.
    bool sample(double now, CharacterPose& out) const;
    // Seconds behind: two updates' worth, more on a bumpy connection (up to a third of a second).
    static constexpr double kDelay = 0.1, kMaxDelay = 0.35;
    double delay() const;

private:
    struct Snap { double t; CharacterPose pose; };
    std::vector<Snap> m_snaps;              // oldest first
    double m_offset = 0.0;                  // our clock minus theirs (smallest seen, plus a slow drift)
    bool   m_haveOffset = false;
    double m_jitter = 0.0;                  // how late messages have been running (recently)
    // The delay in use eases towards what's wanted (a sudden change would make them jump).
    mutable double m_lag = 0.0, m_lastSample = -1.0;   // how far behind our clock we show them
};

// Hosting a multiplayer game. The host runs the scripts and physics for the
// world; every joined player simulates their own character and sends its pose.
class NetServer {
public:
    NetServer(Scene* scene, GameSession* session);
    ~NetServer();

    bool start(int port, std::string& error);          // local network: players connect to us directly
    // Through a Guts&Bolts server instead: players reach us via the server, so
    // nobody sees anybody's IP address. `hostRequest` is a signed "relay.host".
    bool startRelay(const std::string& server, int port, const std::string& hostRequest, std::string& error);
    void stop();
    void update(float dt);
    void say(const std::string& text);                 // the host's own chat
    void announce(const std::string& text);            // a grey system line for everyone ("X earned a badge!")
    std::vector<PlayerEntry> players() const;
    ChatLog& chat() { return m_chat; }
    int port() const { return m_port; }
    bool relayed() const { return (bool)m_control; }
    bool relayReady() const { return !m_sessionId.empty(); }
    const std::string& relayCode() const { return m_code; }      // private servers: what friends type in
    const std::string& relayError() const { return m_relayError; }
    // A game server machine (GutsAndBoltsGameServer): nobody plays here, so there's
    // no host in the player list, and the game goes on whoever leaves.
    void setDedicated(bool on) { m_dedicated = on; m_chat.translate = !on; }
    int  playerCount() const;                          // people who've joined (not counting the host)
    // The dev console's server side (F9) is only for the game's owner (their account ID).
    void setOwner(const std::string& accountId) { m_owner = accountId; }
    bool iAmOwner() const;
    void devCommand(const std::string& code);   // the owner (us) runs Lua on the server
    // MarketplaceService:PromptGamePassPurchase for a joined player: their app shows the
    // Buy window and tells us how it went. False if that player isn't one of ours.
    bool promptPass(int scriptUserId, const std::string& pass);
    // TeleportService sent this joined player to another game (`from` = this game's ID).
    bool teleport(const std::string& name, const std::string& place, const nlohmann::json& data, const std::string& from);
    // Voice chat: does this game allow it (Game Settings / workspace.VoiceChatEnabled)?
    bool voiceAllowed() const;

private:
    struct Client;
    struct NodeState;
    void handle(Client& c, const std::string& msg);
    Client* findClient(const std::string& name);                        // a joined player, by name (any capitals)
    static bool sameName(const std::string& a, const std::string& b);
    void addClient(std::unique_ptr<Net::Connection> conn);
    void updateRelay(float dt);
    void sendTick();
    void dropClient(size_t index, const char* reason);
    void broadcast(const std::string& msg, const Client* except = nullptr);
    std::string worldMessage(bool full);

    void sendDevLog();
    void updateVoice(double now);
    void passVoice(const std::string& name, uint64_t rootId, const std::string& data, const Client* from);
    int         m_voiceSent = -1;   // voiceAllowed() as last told to everyone
    std::string m_owner;
    bool        m_dedicated = false;
    std::set<std::string> m_guestAccounts;   // joiners the server told us have no account (can't chat)
    Scene*       m_scene;
    GameSession* m_session;
    Net::Listener m_listener;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::unordered_map<uint64_t, std::string> m_sent;   // node id -> last replicated state
    std::string m_lastEnv, m_lastGui;
    std::string m_lastCam;   // the game camera setting last sent (Workspace.Orthographic)
    std::string m_lastGroups;   // the collision groups last sent (PhysicsService)
    uint64_t    m_terrainSent = 0;    // the terrain version everyone has (0 = not checked yet)
    double      m_terrainAt = 0.0;    // when it was last sent
    std::unordered_map<uint64_t, PoseBuffer> m_poses;   // joined players' characters, shown smoothly
    ChatLog     m_chat;
    float       m_tick = 0.0f;
    int         m_nextId = 1;
    int         m_port = kDefaultPort;
    // Relay (see server/ServerRelay.cpp)
    std::unique_ptr<Net::Connection> m_control;
    std::string m_relayServer, m_sessionId, m_code, m_relayError;
    int         m_relayPort = 0;
    double      m_pingAt = 0.0;   // steady-clock seconds
};

// Joining someone else's game.
class NetClient {
public:
    enum class State { Idle, Connecting, Joined, Failed };

    NetClient(Scene* scene, GameSession* session);
    ~NetClient();

    bool connect(const std::string& host, int port);
    // Through a Guts&Bolts server: `joinRequest` is a signed "relay.join".
    bool connectRelay(const std::string& server, int port, const std::string& joinRequest);
    void disconnect();
    void update(float dt);
    void say(const std::string& text);
    void reportTouch(uint64_t partId, const std::string& limb);
    void reportClick(uint64_t partId);

    State state() const { return m_state; }
    const std::string& error() const { return m_error; }
    const std::string& gameTitle() const { return m_title; }
    const std::vector<PlayerEntry>& players() const { return m_players; }
    ChatLog& chat() { return m_chat; }
    // Through a Guts&Bolts server: which server this is and which game it plays.
    const std::string& relaySession() const { return m_relaySession; }
    const std::string& relayGame() const { return m_relayGame; }
    // We were in the game and then the host went away (closed it, crashed, or lost internet).
    bool hostLeft() const { return m_hostGone; }
    // The dev console: the host said we own this game, so we see its server log and can run commands.
    bool devOwner() const { return m_devOwner; }
    const std::vector<Log::Entry>& serverLog() const { return m_serverLog; }
    void devCommand(const std::string& code);
    // Game passes: the host's game asked us to show a Buy window ("" = none waiting);
    // passDone tells it how it went.
    std::string takePassPrompt() { return std::move(m_passPrompt); }
    void passDone(const std::string& pass, bool bought);
    // The server said this server's host left: 0 = no, 1 = wait for the new one, 2 = you host it.
    int movingTurn() const { return m_movingTurn; }
    // TeleportService: what we bring when joining ({"data", "from"}), and where the
    // server's scripts sent us ({"place", "data", "from"}; null = nowhere).
    void setJoinData(const nlohmann::json& joinData) { m_joinData = joinData; }
    nlohmann::json takeTeleport() { nlohmann::json t = std::move(m_teleport); m_teleport = nullptr; return t; }
    // Voice chat: the host's game allows it.
    bool voiceAllowed() const { return m_voiceAllowed; }

private:
    void handle(const std::string& msg);

    Scene*       m_scene;
    GameSession* m_session;
    std::unique_ptr<Net::Connection> m_conn;
    State        m_state = State::Idle;
    std::string  m_error, m_title;
    uint64_t     m_myServerRoot = 0;     // our character on the host (hidden here)
    std::map<uint64_t, std::string> m_toolSeen;   // our tools (copies of the host's), as the host last sent them
    void         showMyTools(const nlohmann::json& m);
    void         showHeldTool(SceneNode* root, const nlohmann::json& ch);
    std::map<uint64_t, std::string> m_charNames;
    std::map<uint64_t, std::vector<uint64_t>> m_charFx;   // Highlights / Trails / Beams we put on each character (as the host said)
    std::unordered_map<uint64_t, PoseBuffer> m_poses;   // other players' characters, shown smoothly
    std::vector<PlayerEntry> m_players;
    std::string  m_nonce;                 // we ask the host to sign this, to prove who it is
    uint64_t     m_hostRoot = 0;
    bool         m_hostAdmin = false;
    bool         m_hostVerified = false;
    ChatLog      m_chat;
    float        m_tick = 0.0f;
    bool         m_wasDead = false;
    std::string  m_relaySession, m_relayGame;
    bool         m_devOwner = false;
    std::string  m_passPrompt;
    std::vector<Log::Entry> m_serverLog;
    bool         m_hostGone = false;
    int          m_movingTurn = 0;
    nlohmann::json m_joinData, m_teleport;
    bool         m_voiceAllowed = false;
    int          m_voiceTold = -1;    // whether we told the host we're listening (-1 = not yet)
};
