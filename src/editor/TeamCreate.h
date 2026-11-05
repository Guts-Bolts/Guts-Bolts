#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include "../net/Socket.h"

class Scene;

inline constexpr int kTeamCreatePort = 7778;

// Team Create: several people editing the same game at the same time.
// One Studio hosts; others join. Every change anyone makes (objects, scripts,
// lighting, rules) is sent to everyone else, object by object.
class TeamCreate {
public:
    struct Member {
        int         id = 0;
        std::string name;
        glm::vec3   color{1.0f};
        uint64_t    selected = 0;       // what they have selected
        glm::vec3   cursor{0.0f};
        bool        cursorActive = false;
    };
    struct ChatLine { std::string from, text; bool system = false; };

    explicit TeamCreate(Scene* scene);
    ~TeamCreate();

    bool host(int port, std::string& error);
    bool join(const std::string& address, std::string& error);   // then wait for active()
    bool hostCloud(std::string& error);                           // authenticated, internet-hosted session
    bool joinCloud(const std::string& code, std::string& error);  // eight-character invite code
    bool cloud() const { return m_cloud; }
    const std::string& inviteCode() const { return m_cloudCode; }
    const std::string& chatId() const { return m_cloudChat; }
    void leave();

    bool active() const  { return m_hosting || m_joined || (m_cloud && m_cloudReady); }
    bool hosting() const { return m_hosting || (m_cloud && m_cloudOwner); }
    bool waiting() const { return (m_server && !m_joined) || (m_cloud && !m_cloudReady); } // connecting, not yet in

    // Every frame. While `canApply` is false (playtesting) incoming edits wait.
    void update(bool canApply);
    // After the editor records a local change: send it to the others.
    void localChanged();
    void setSelection(uint64_t id);
    void setCursor(const glm::vec3& world, bool active);
    void say(const std::string& text);

    // True once after other people's edits were applied (the editor resets undo).
    bool consumeRemoteEdit() { bool r = m_remoteEdit; m_remoteEdit = false; return r; }

    const std::vector<Member>& members() const { return m_members; }
    int  myId() const { return m_myId; }
    std::vector<ChatLine>& chat() { return m_chat; }
    const std::string& error() const { return m_error; }

private:
    struct Client {
        std::unique_ptr<Net::Connection> conn;
        int         id = 0;
        std::string name;
        bool        joined = false;
    };

    void handle(const std::string& text, Client* from);
    std::string diffLocal();                 // "" if nothing changed
    void applyOps(const std::string& ops);
    void snapshotSynced();
    void send(const std::string& msg, Client* except = nullptr);   // host: to clients; client: to host
    void addSystem(const std::string& text);
    Member* member(int id);
    static glm::vec3 colorFor(int id);
    void updateCloud(bool canApply);
    void cloudMembers(const nlohmann::json& members);
    void cloudPush(const std::string& kind, const std::string& payload);

    Scene*        m_scene;
    Net::Listener m_listener;
    std::vector<std::unique_ptr<Client>> m_clients;     // host side
    std::unique_ptr<Net::Connection>     m_server;      // client side
    bool          m_hosting = false, m_joined = false;
    int           m_myId = 0, m_nextId = 1;
    std::vector<Member> m_members;
    std::unordered_map<uint64_t, std::string> m_synced;   // id -> "parent|index|object"
    std::string   m_syncedSettings;
    std::vector<std::string> m_pending;                   // edits received while playtesting
    bool          m_remoteEdit = false;
    uint64_t      m_selection = 0, m_sentSelection = ~0ull;
    std::vector<ChatLine> m_chat;
    std::string   m_error;
    bool          m_cloud = false, m_cloudReady = false, m_cloudOwner = false;
    bool          m_cloudPollPending = false, m_cloudPushPending = false, m_cloudPresencePending = false;
    std::string   m_cloudSession, m_cloudCode, m_cloudChat, m_cloudAccount;
    uint64_t      m_cloudSeq = 0;
    glm::vec3     m_cursor{0.0f};
    bool          m_cursorActive = false;
    double        m_cloudNextPoll = 0.0, m_cloudNextPresence = 0.0;
    std::vector<std::pair<std::string, std::string>> m_cloudPushQueue;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};
