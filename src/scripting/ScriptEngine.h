#pragma once
#include <set>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <unordered_set>
#include <vector>

struct lua_State;
class Scene;
class SceneNode;
class Physics;
enum class SignalKind : int;

// Things scripts can show on screen during Play (see the Gui table in Lua).
struct GuiState {
    std::map<std::string, std::string> labels;   // Gui.Label(key, text)
    std::string message;                         // Gui.Message(text, seconds)
    float       messageTime = 0.0f;
};

// Runs the Lua code in every Script object while the game is in Play mode.
//
// Each script body — and every event callback — runs in its own coroutine, so
// wait() can pause it without freezing the rest of the game. A per-resume
// time limit catches accidental infinite loops (e.g. `while true do end`).
class ScriptEngine {
public:
    explicit ScriptEngine(Scene* scene);
    ~ScriptEngine();

    void start(bool runScripts = true);   // create a fresh Lua world (and run all Scripts)
    // Studio's Command Bar: run some Lua right now (the engine must be started).
    bool runCommand(const std::string& code, std::string& error);
    // Studio plugins: run a whole file, and later call functions it handed us (registry refs).
    bool runChunk(const std::string& code, const std::string& chunkName, std::string& error);
    bool callRef(int ref, std::string& error);
    struct lua_State* lua() { return m_L; }
    // The game's physics world (PathfindingService looks at it). Null outside a game.
    void setPhysics(const Physics* p) { m_physics = p; }
    const Physics* physics() const { return m_physics; }
    void stop();                  // tear everything down
    bool running() const { return m_L != nullptr; }

    // Called once per frame in Play mode.
    void update(float dt);
    void fireTouched(uint64_t partId, uint64_t otherId);
    void fireClicked(uint64_t partId);
    void fireDied(uint64_t characterRootId);
    void fireMoveToFinished(uint64_t characterRootId, bool reached);
    void fireAttributeChanged(uint64_t id, const std::string& name);
    void fireTag(bool added, uint64_t id, const std::string& tag);
    void fireTool(SignalKind kind, uint64_t toolId);   // Activated / Deactivated / Equipped / Unequipped
    void fireValueChanged(uint64_t valueId);            // an IntValue etc. changed (.Changed)
    void fireGui(SignalKind kind, uint64_t id);         // game UI: GuiClick / GuiEnter / GuiLeave / GuiFocused
    void fireFocusLost(uint64_t id, bool enter);        // textBox.FocusLost(enterPressed)
    // A ProximityPrompt event (SignalKind::Prompt*), by the player with this UserId (0 = ours).
    void firePrompt(SignalKind kind, uint64_t promptId, int userId = 0);
    void firePropertyChanged(uint64_t id, const char* property);   // obj.Changed(property) (a TextBox's Text...)
    void fireAnimationEvents();                         // AnimationTracks: Stopped, KeyframeReached...
    // DataStoreService's saved data for this game (a file in the player's account folder).
    const nlohmann::json& saveData();
    void setSaveData(const std::string& store, const std::string& key, const nlohmann::json& value);
    // DataStores on the Guts&Bolts server, so every server of a game sees the same data. Used when
    // the app turns it on (playing or hosting a published game; never in Studio) and the server
    // trusts this account with the game's data (game server machines, staff, the creator).
    // Otherwise they're the file above, as before.
    void setOnlineData(bool on, const std::string& gameId) { m_onlineData = on; m_dataGame = gameId; }
    // Script calls (they wait in Lua until done(ticket) is true): "get" or "inc".
    int  dataStart(const std::string& kind, const std::string& store, const std::string& key, double delta);
    bool dataDone(int ticket, nlohmann::json& value);
    void dataSet(const std::string& store, const std::string& key, const nlohmann::json& value);
    // MessagingService: messages between the running servers of this game (each one gets every
    // message, the sender too). Online - through the Guts&Bolts server - when setOnlineData is on
    // and the server says we're running a server of the game; otherwise on this computer (a
    // folder that Studio's play test, its multiplayer test server and any other local server of
    // the same game share). Limits like Roblox's: topics 1-80 letters, messages 1 KB as JSON,
    // 150 + 60 per player sends a minute, 20 + 8 per player subscriptions.
    static constexpr size_t kMsgTopic = 80, kMsgSize = 1024;
    // A send (the Lua side checked the topic and size): ticket, or 0 with `error` (too many).
    int  msgPublish(const std::string& topic, const nlohmann::json& data, int players, std::string& error);
    bool msgDone(int ticket, std::string& error);   // finished? (error "" = sent)
    void msgSubscribe(const std::string& topic, bool on);
    struct MsgIn { std::string topic; nlohmann::json data; double sent = 0.0; };
    std::vector<MsgIn> takeMessages() { std::vector<MsgIn> out; out.swap(m_msgIn); return out; }
    // require(ID): a model from the Library, fetched once per game. 0 = still getting it,
    // 1 = ready (module = its MainModule), 2 = couldn't (error says why).
    int  libraryModule(const std::string& assetId, uint64_t& module, std::string& error);
    // TeleportService: players a script sent to another game. The app moves them (only the
    // Player does; elsewhere, like Studio's play test, scripts just get a note in Output).
    struct Teleport { std::string player, place; nlohmann::json data; };
    void setTeleportsWork(bool on) { m_teleportsWork = on; }
    bool teleportsWork() const { return m_teleportsWork; }
    void queueTeleport(Teleport t) { m_teleports.push_back(std::move(t)); }
    std::vector<Teleport> takeTeleports() { return std::move(m_teleports); }
    // What a player brought along when they were teleported here: {"data": ..., "from": "123"}
    // (player:GetJoinData(), TeleportService:GetLocalPlayerTeleportData()). Kept across start().
    void setJoinData(const std::string& player, const nlohmann::json& joinData) { m_joinData[player] = joinData; }
    void clearJoinData() { m_joinData.clear(); }
    const nlohmann::json* joinData(const std::string& player) const {
        auto it = m_joinData.find(player);
        return it == m_joinData.end() ? nullptr : &it->second;
    }
    // BadgeService:AwardBadge calls waiting to be sent to the server: (player name, badge id).
    // The app sends them (only the host of a published game's server can award).
    std::vector<std::pair<std::string, std::string>> takeBadgeAwards() { return std::move(m_badgeAwards); }
    // Badges we know a player has (awarded this session, or looked up by the app).
    void markBadge(const std::string& player, const std::string& badge) { m_knownBadges.insert(player + "\n" + badge); }
    bool knowsBadge(const std::string& player, const std::string& badge) const { return m_knownBadges.count(player + "\n" + badge) > 0; }
    void queueBadge(const std::string& player, const std::string& badge) { m_badgeAwards.push_back({player, badge}); }
    // Game passes (MarketplaceService). The engine asks the server who owns which by
    // itself; a purchase prompt needs the app: it takes them, shows the player a Buy
    // window (or sends it to that player's computer) and says how it went.
    void lookUpPasses(int userId);                       // ask the server (once per player)
    std::string onlineGameId() const;
    void setPlayerAccount(int userId, const std::string& accountId) { m_playerAccounts[userId] = accountId; }
    bool passesReady(int userId) const { return m_passReady.count(userId) > 0; }
    bool ownsPass(int userId, const std::string& pass) const;
    void queuePassPrompt(int userId, const std::string& pass) { m_passPrompts.push_back({userId, pass}); }
    std::vector<std::pair<int, std::string>> takePassPrompts() { return std::move(m_passPrompts); }
    void passPromptDone(int userId, const std::string& pass, bool bought);   // fires PromptGamePassPurchaseFinished
    struct PassResult { int userId; std::string pass; bool bought; };
    std::vector<PassResult> takePassResults() { return std::move(m_passResults); }
    // Developer products: the prompt is a pass prompt for "product:<id>". Purchases are
    // receipts on the server; ProcessReceipt hands each out, then the server is told.
    void checkReceipts(int userId);                                   // fetch a player's waiting receipts
    void receiptDone(int userId, const std::string& receipt, bool granted);
    struct Receipt { int userId; std::string id, product; long long price; };
    std::vector<Receipt> takeReceipts() { return std::move(m_receipts); }
    // Players' objects (outside the world) and what's in their leaderstats folder.
    uint64_t playerNode(const std::string& name);
    std::vector<std::pair<std::string, std::string>> leaderstats(const std::string& playerName);
    // Start the scripts inside `root` that haven't run yet (objects that just
    // arrived in the world, like a cloned tool).
    void runScriptsIn(SceneNode* root);
    // Multiplayer: other players joining / leaving (Players.PlayerAdded etc.).
    void addPlayer(const std::string& name, uint64_t characterRootId, int userId, uint64_t backpackId = 0);
    void removePlayer(const std::string& name);

    // Which scripts run here. LocalScripts run on each player's own computer:
    //   All    - playing by yourself, or hosting from the app (you're a player too)
    //   Server - a game server machine: Scripts only (nobody plays on it)
    //   Client - you joined someone's game: only LocalScripts (the host runs the Scripts)
    enum class RunMode { All, Server, Client };
    void    setRunMode(RunMode m) { m_runMode = m; }
    RunMode runMode() const { return m_runMode; }
    bool    wantsScript(const SceneNode* script) const;
    void    setLocalUserId(int id) { m_localUserId = id; }   // our player's UserId (the server picks it)

    // RemoteEvents / RemoteFunctions between computers (ScriptRemotes.cpp). What scripts
    // want sent, for the app to pass on: `to` = a player's UserId, 0 = every joined
    // player, -1 = the server.
    struct RemoteOut { int to; nlohmann::json msg; };
    std::vector<RemoteOut> takeRemoteOut() { return std::move(m_remoteOut); }
    // A message from the other side ("remote", "invoke" or "invoked"); on the server,
    // `fromUserId` is the player who sent it.
    void remoteIn(const nlohmann::json& msg, int fromUserId = 0);
    // On a joined player's computer our own character has different ids from the
    // server's copy of it: these swap them (unset: ids are the same everywhere).
    std::function<uint64_t(uint64_t)> idToServer, idFromServer;

    GuiState& gui() { return m_gui; }
    void setPlayerName(const std::string& n) { m_playerName = n; }
    // A game server machine: nobody plays here, so there's no LocalPlayer (only people who join).
    void setNoLocalPlayer(bool on) { m_noLocalPlayer = on; }
    bool noLocalPlayer() const { return m_noLocalPlayer; }
    // While set, events only reach Scripts, not LocalScripts: on the host, a joined
    // player's clicks and typing are theirs, not the host's.
    void setJoinerEvent(bool on) { m_joinerEvent = on; }
    // Hosting or joined (remote messages to other computers are kept for sending).
    void setNetworked(bool on) { m_networked = on; if (!on) m_remoteOut.clear(); }
    const std::string& playerName() const { return m_playerName; }

    // Compile without running — used by the script editor for live error checks.
    static bool checkSyntax(const std::string& source, std::string& error, int& line);

    // ---- used by the Lua bindings --------------------------------------------
    Scene*     scene() { return m_scene; }
    double     time() const { return m_time; }
    SceneNode* resolve(uint64_t id);                 // scene or detached (Parent = nil)
    SceneNode* adopt(std::unique_ptr<SceneNode> n);  // new object with Parent = nil
    bool       setParent(SceneNode* node, SceneNode* newParent, std::string& err);
    bool       destroy(SceneNode* node, std::string& err);
    int        connect(SignalKind kind, uint64_t id, int fnRef, bool once);
    int        connectWaiter(SignalKind kind, uint64_t id, lua_State* thread);
    void       disconnect(int index);
    bool       connected(int index) const;
    void       resume(lua_State* co, int ref, int nargs, lua_State* from = nullptr);
    void       schedule(lua_State* co, int ref, double delay, int startArgs);
    void       checkTimeout(lua_State* L);
    float&     clockTime();
    void       applyClockTime();
    bool       isKeyDown(const std::string& key) const;
    // Stop (or restart) every script in this part of the tree, like
    // Destroy() / Disabled in Roblox.
    void       stopScripts(SceneNode* root);
    void       setScriptEnabled(SceneNode* script, bool on);

private:
    void openRemotes();                       // the Lua side of RemoteEvents (ScriptRemotes.cpp)
    RunMode m_runMode = RunMode::All;
    bool    m_joinerEvent = false;
    bool    m_networked = false;
    int     m_localUserId = 1;
    std::vector<RemoteOut> m_remoteOut;
    struct InvokeIn { uint64_t remote; int from; int call; nlohmann::json args; };
    std::vector<InvokeIn> m_invokesIn;        // RemoteFunction calls waiting for the server's scripts
    std::map<int, nlohmann::json> m_invokeResults;   // our InvokeServer calls the server has answered
    int     m_nextCall = 1;
    friend struct RemoteLua;
    std::unordered_set<uint64_t> m_started;   // scripts that have run (so nothing runs twice)
    uint64_t m_playersRoot = 0;               // "Players": one object per player (detached)
    nlohmann::json m_saveData;                // loaded on first use
    std::vector<std::pair<std::string, std::string>> m_badgeAwards;
    std::set<std::string> m_knownBadges;      // "player\nbadge"
    std::map<int, std::set<std::string>> m_passOwned;   // user number -> pass ids and numbers they own
    std::set<int> m_passReady, m_passAsked;
    std::map<int, std::string> m_playerAccounts;   // script UserId -> account (the local player is 1: us)
    std::vector<std::pair<int, std::string>> m_passPrompts;
    std::vector<PassResult> m_passResults;
    std::vector<Receipt> m_receipts;
    std::set<std::string> m_receiptsBusy;   // handed to ProcessReceipt and not answered yet
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);   // replies after the engine is gone are ignored
    bool     m_saveLoaded = false;
    // Online DataStores (see setOnlineData).
    enum class DataMode { Unknown, Asking, Local, Online };
    struct DataJob { int ticket = 0; std::string kind, store, key; double delta = 0.0; nlohmann::json value; };
    struct DataCached { nlohmann::json value; double at = 0.0; int writing = 0; };
    void dataDecide();                  // local file or the server?
    void dataRun(const DataJob& job);
    bool        m_onlineData = false;
    std::string m_dataGame;
    DataMode    m_dataMode = DataMode::Unknown;
    int         m_dataNext = 1, m_dataGen = 0;
    std::vector<DataJob> m_dataQueue;              // waiting for dataDecide
    std::map<int, nlohmann::json> m_dataDone;      // ticket -> value
    bool m_teleportsWork = false;
    std::vector<Teleport> m_teleports;
    std::map<std::string, nlohmann::json> m_joinData;
    struct LibraryModule { int state = 0; uint64_t module = 0; std::string error; };
    std::map<std::string, LibraryModule> m_libraryModules;   // require(ID): asset ID -> what we got
    std::map<std::string, DataCached> m_dataCache; // "store\nkey" -> what we last saw or wrote
    // MessagingService (see msgPublish).
    enum class MsgMode { Unknown, Asking, Local, Online };
    struct MsgOut { int ticket = 0; std::string topic; nlohmann::json data; };
    void msgStep();                                  // every frame: ask for / read new messages
    void msgSync();                                  // online: find out where the game's messages are up to
    void msgSend(const MsgOut& m);
    std::filesystem::path msgFolder() const;
    MsgMode     m_msgMode = MsgMode::Unknown;
    std::map<std::string, double> m_msgTopics;       // subscribed topic -> since when (unix seconds)
    std::vector<MsgIn>  m_msgIn;                     // arrived, for the scripts
    std::vector<MsgOut> m_msgQueue;                  // waiting while we ask the server
    std::map<int, std::string> m_msgDone;            // ticket -> error ("" = sent)
    std::vector<double> m_msgSentAt;                 // the rate limit
    std::set<std::string> m_msgSeen;                 // local message files already read
    std::string m_msgToken;                          // this engine's part of local file names
    long long   m_msgAfter = -1, m_msgCount = 0;
    int         m_msgNext = 1;
    bool        m_msgPolling = false;
    double      m_msgNextPoll = 0.0, m_msgPruned = 0.0;
    std::filesystem::path saveFile() const;
    struct Waiting {
        int        ref;
        lua_State* co;
        double     wakeAt;
        double     since;
        int        startArgs;   // >= 0: not started yet (task.delay), else a wait()
        uint64_t   owner = 0;   // the Script that started this thread
    };
    struct Connection {
        SignalKind kind;
        uint64_t   id;
        int        fnRef     = -2;       // LUA_NOREF
        int        threadRef = -2;       // for :Wait()
        lua_State* waiter    = nullptr;
        bool       once      = false;
        bool       alive     = true;
        uint64_t   owner     = 0;
    };

    void openLibraries();
    void runScript(SceneNode* script);
    void fire(SignalKind kind, uint64_t id, const std::function<int(lua_State*)>& pushArgs);
    void reportError(lua_State* co);

    Scene*     m_scene;
    const Physics* m_physics = nullptr;
    lua_State* m_L = nullptr;
    double     m_time = 0.0;
    double     m_resumeStart = 0.0;
    int        m_depth = 0;
    uint64_t   m_current = 0;              // the Script whose code is running right now
    std::unordered_set<uint64_t> m_stopped;

    std::vector<Waiting>                    m_waiting;
    std::vector<Connection>                 m_conns;
    std::vector<std::unique_ptr<SceneNode>> m_detached;
    GuiState                                m_gui;
    std::string                             m_playerName = "Player";
    bool                                    m_noLocalPlayer = false;
    bool m_padSeen = false;   // a controller was plugged in (UserInputService.GamepadEnabled)
};
