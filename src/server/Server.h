#pragma once
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Net { class Connection; class Listener; }

// The Guts&Bolts server: one program that keeps everyone's accounts, Bolts,
// badges and uploads (hats, shirts, pants, audio, plugins and games) in a
// folder, and answers the apps' signed requests (see online/Protocol.h).
//
// Everything is saved as plain JSON files in the data folder, so backing the
// server up is just copying that folder.
class GbServer {
public:
    struct Options {
        int         port = 7780;
        std::filesystem::path data = "server_data";
        std::string official;          // the staff account's ID ("" = the one built in)
        std::string name = "Guts&Bolts";
    };

    explicit GbServer(Options opts);
    ~GbServer();
    bool start(std::string& error);
    void runForever();                 // until stop() / Ctrl+C
    void step(int waitMs);             // one round of accept / read / answer
    void stop() { m_running = false; }

    // Handle one already-parsed request (also used by the tests). Returns the reply.
    nlohmann::json handle(const nlohmann::json& request);

private:
    struct Entry { long long amount = 0; std::string reason; long long time = 0; std::string ref; };
    struct User {
        std::string id, name = "Player";
        long long   created = 0, lastSeen = 0;
        std::vector<Entry> ledger;
        std::map<std::string, std::string> grants;   // badge key -> signature
        std::set<std::string> owned;                 // asset ids
        std::string uploadDay; int uploadsToday = 0;
        std::string playDay;   long long playEarned = 0, lastPlay = 0;
        bool        banned = false;
        std::string banReason, banNote;   // Online::kBanReasons key, and staff's note
        long long   bannedAt = 0, bannedUntil = 0;   // bannedUntil 0 = for good
        nlohmann::json warnings = nlohmann::json::array();   // staff warnings: {id, reason, note, at, seen}
        std::set<std::string> friends, friendIn, friendOut;   // friends; requests to me; requests I sent
        std::set<std::string> following, followers;           // one-way follows (no asking)
        // Privacy: who sees you online / what you play, and who can join you there:
        // "everyone", "friends" or "nobody" (worker/server.js privacyOf).
        std::string privacyStatus = "everyone", privacyJoin = "everyone";
        std::vector<std::string> gear;   // equipped gear (worker/server.js me.gear)
        std::vector<std::string> emotes; // the emote menu's slots ("" = empty; worker/server.js me.emotes)
        std::string animPack;            // the animation pack they wear ("" = the normal animations)
        // Signing up: a username and user number (both never reused), plus the
        // password-locked backup of their key so they can log in on other devices.
        std::string username;
        long long   userId = 0;                  // 0 = hasn't signed up
        std::string pwSalt, pwHash, keyBlob;
        std::string totpSecret, totpPending;    // authenticator app (base32), on / being set up
        long long   totpLast = -1;              // the newest 30-second step used (no reusing a code)     // pwHash = hash of the login token (we never see the password)
        nlohmann::json avatar;                   // colours (0-255), hat, hatColor, wearing, updated; null = never set
        nlohmann::json gameBadges = nlohmann::json::array();   // [badge id, game id, when] earned in games
        // ServerSocial.cpp: saved outfits, favourite and recently played games, private messages.
        nlohmann::json outfits = nlohmann::json::array();   // {id, name, avatar, created}, newest first
        std::vector<std::string> favorites, recent;          // game ids, newest first
        nlohmann::json inbox = nlohmann::json::array();     // {id, from, subject, body, at, read}, newest first
        nlohmann::json notes = nlohmann::json::array();     // the bell: {id, kind, text, about, at, read}, newest first
        nlohmann::json receipts = nlohmann::json::array();  // developer products bought: {id, product, game, price, at, granted}
        nlohmann::json privateServers = nlohmann::json::object();   // game id -> when their paid private server runs out
        nlohmann::json sent = nlohmann::json::array();      // {id, to, subject, body, at}
        std::string messageDay; int messagesToday = 0;
        std::string privacyMessages = "everyone";            // who can send you messages
        std::string privacyInventory = "everyone";           // who can see what you own (and trade with you)
        std::string blurb;                                   // "About me"
        nlohmann::json posts = nlohmann::json::array();     // "Right now I'm..." {text, at}, newest first
        std::string statusDay; int statusesToday = 0;
        // ServerSafety.cpp: people you blocked, and how many reports you sent today.
        std::vector<std::string> blocked;
        std::string reportDay; int reportsToday = 0;
        // ServerItems.cpp / ServerAccounts.cpp (worker/server.js has the same):
        std::vector<std::string> pastNames;   // usernames they had before (reserved for them)
        std::vector<std::string> played;      // games they've played (game.vote needs it), oldest first, at most 500
        std::vector<std::string> usedModels;  // Library models they've inserted (model.vote needs it), oldest first
        long long   publicWeek = 0;           // the week (unix time / 7 days) publicThisWeek counts
        int         publicThisWeek = 0;       // models made public that week (Verified: no limit)
    };
    struct Asset {
        std::string id, kind, name, description, creator;
        long long   price = 0, created = 0, sales = 0, plays = 0;
        size_t      size = 0;
        long long   thumb = 0;                   // when its picture was last set (0 = none)
        long long   num = 0;                     // its number, counting up like Roblox's asset IDs
        nlohmann::json meta = nlohmann::json::object();
        nlohmann::json badges = nlohmann::json::array();   // games: badges its creator made
        nlohmann::json comments = nlohmann::json::array(); // games: comments under it, newest first (ServerSocial.cpp)
        nlohmann::json days = nlohmann::json::object();    // creator stats: "YYYY-MM-DD" -> {plays, sales, bolts}, last 60 days
        long long   icon = 0;                    // games: when its icon was last set (0 = none)
        long long   likes = 0, dislikes = 0;     // games and Library models: thumbs up / down
        nlohmann::json votes = nlohmann::json::object();   // games and models: account id -> 1 or -1
        // Limiteds (ServerItems.cpp): a fixed stock of numbered copies, which people resell and trade.
        bool        limited = false;
        long long   stock = 0;
        nlohmann::json copies = nlohmann::json::array();   // {serial, owner, price} (price 0 = not for sale)
        // Games: screenshots on its page (ServerItems.cpp), in order: {id, at, type}. Files "shot-<game>-<id>".
        nlohmann::json shots = nlohmann::json::array();
        // Games: version history, {v, at, by, size, restored?}, oldest first (the last is live). Empty for a
        // game published before history existed: its file counts as version 1. (worker/server.js a.versions)
        nlohmann::json versions = nlohmann::json::array();
    };

    struct Post { std::string by, text; long long time = 0; };
    struct Group {
        std::string id, name, description, owner;
        long long   created = 0;
        int         color = 0x3A7BD5;                   // emblem colour (0xRRGGBB)
        bool        open = true;                         // anyone can join (false: ask first)
        std::map<std::string, std::string> members;      // account id -> rank id ("Owner", "Admin", "Member" or a custom "r-...")
        nlohmann::json ranks = nlohmann::json::array();  // [{id, name, level, perms}] (empty = the default three)
        std::vector<Entry> ledger;                       // the group's Bolts (game sales in, payouts out)
        std::set<std::string> requests;                  // waiting to join
        Post        shout;
        std::vector<Post> wall;                          // newest last, at most 200
    };

    // A connection to the server. Most just send requests; the relay turns some
    // into a host's control line or into one end of a pipe between a player and a host.
    struct Client {
        enum class Mode { Request, HostControl, PendingJoin, Pipe, Pool };   // Pool: a game server machine
        std::unique_ptr<Net::Connection> conn;
        Mode        mode = Mode::Request;
        long long   lastActive = 0, since = 0;
        std::string account, session, ticket;
        Client*     peer = nullptr;      // the other end of a pipe
        bool        closing = false;     // drop once everything queued has been sent
    };
    // A game server running on someone's computer, reached only through us, so
    // players never learn each other's IP addresses.
    struct Session {
        std::string id, game, title, host, code;   // code: private servers only
        bool        priv = false;
        int         max = 12;
        long long   created = 0;
        bool        dedicated = false;   // run by a game server machine: nobody plays on it, so it stays when anyone leaves
        Client*     control = nullptr;
        std::set<Client*> players;               // the player end of each pipe
    };

    // Requests
    nlohmann::json op(const std::string& name, User& me, const nlohmann::json& args);
    nlohmann::json groupOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerGroups.cpp
    nlohmann::json accountOp(const std::string& name, User& me, const nlohmann::json& args);  // ServerAccounts.cpp
    // ServerItems.cpp: editing items, Limiteds (resale and trades), game votes and icons,
    // model access and the update log. (worker/server.js has the same.)
    nlohmann::json itemOp(const std::string& name, User& me, const nlohmann::json& args);
    nlohmann::json copiesOf(const User& u) const;          // every Limited copy someone holds
    void           syncOwned(User& u, const Asset& a);     // `owned` follows the copies they hold
    nlohmann::json limitedJson(const Asset& a) const;      // what the catalog shows about a Limited (null if it isn't one)
    int            publicModelsLeft(const User& u) const;  // -1 = no limit
    void           countPublicModel(User& u);
    static std::string publicModelLimitText();
    void saveTrades();
    void loadTrades();
    // Past trades (ServerItems.cpp, worker/server.js has the same): a trade is over, so it goes in
    // both people's Past trades; open trades nobody answered in 30 days expire.
    void finishTrade(nlohmann::json& tr, const std::string& status, long long now);
    void keepPastTrade(const nlohmann::json& tr, bool onlyIfNew = false);
    void expireTrades(long long now);
    void savePastTrades();
    void loadPastTrades();
    void saveUpdates();
    void loadUpdates();
    void  claimOfficial(User& u);                  // the staff account is user 1, "Guts"
    User* findUsername(const std::string& username);
    // An account key, "@username", or a name as shown in games.
    User* findPerson(const std::string& s);
    // The Guts account (#1) follows every signed-up player (like Builderman did).
    void gutsFollows(User& u);
    User* findUserId(long long userId);
    void  saveIds();
    void  loadIds();
    nlohmann::json friendOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerFriends.cpp
    nlohmann::json socialOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerSocial.cpp
    // ServerForum.cpp: boards of threads ({id, board, title, by, at, last, lastBy, views, pinned, locked, posts}).
    nlohmann::json forumOp(const std::string& name, User& me, const nlohmann::json& args);
    const nlohmann::json* forumPost(const std::string& threadId, const std::string& postId, std::string* title) const;
    void saveForum();
    void loadForum();
    // ServerSafety.cpp: blocking people and reporting things to staff.
    nlohmann::json safetyOp(const std::string& name, User& me, const nlohmann::json& args);
    bool blocks(const User& a, const User& b) const;   // has either of them blocked the other?
    // Inventory on profiles (ServerFriends.cpp, worker/server.js inventoryOf): the catalog
    // items someone owns, in one group (all, accessories, clothing, faces, gear, emotes, limited).
    bool seesInventory(const User& viewer, const User& u) const;
    nlohmann::json inventoryOf(const User& u, const std::string& cat) const;
    nlohmann::json reportsJson(const std::string& status) const;   // for the Staff page
    nlohmann::json closeReport(const User& staff, const std::string& id, const std::string& outcome);
    void saveReports();
    void loadReports();
    // ServerSupport.cpp: support tickets (like "I forgot my password"), for staff to answer.
    nlohmann::json supportOp(const std::string& name, User& me, const nlohmann::json& args);
    nlohmann::json ticketJson(const nlohmann::json& x, bool forStaff) const;
    nlohmann::json ticketsJson(const std::string& status) const;   // for the Staff page
    nlohmann::json ticketAdminOp(const std::string& name, const User& staff, const nlohmann::json& args);
    void saveTickets();
    void loadTickets();
    // The staff action log (stafflog.json): what staff did, newest last. about: who it was done to.
    void staffDid(const User& staff, const std::string& action, const std::string& text, const std::string& about = "");
    nlohmann::json staffLogJson(const std::string& who) const;
    void loadStaffLog();
    void rememberPlayed(User& me, const std::string& gameId);                                 // ServerSocial.cpp
    nlohmann::json playerBadgesOf(const User& u) const;                                       // ServerSocial.cpp
    static nlohmann::json allPlayerBadges();                                                  // ServerSocial.cpp
    static constexpr size_t kMaxRecentGames = 30;
    nlohmann::json serverOp(const std::string& name, User& me, const nlohmann::json& args);   // ServerRelay.cpp
    // ServerData.cpp: DataStores (what games save between visits), one file per game.
    nlohmann::json dataOp(const std::string& name, User& me, const nlohmann::json& args);
    nlohmann::json& gameData(const std::string& game);
    void saveGameData(const std::string& game);
    std::map<std::string, nlohmann::json> m_gameData;   // game id -> {store: {key: value}}, loaded when first used
    nlohmann::json checkRequest(const nlohmann::json& req, User*& me);   // null = fine, else the failure reply
    void relayRequest(Client& c, const nlohmann::json& req);            // relay.host / relay.join
    void relayAccept(Client& c, const std::string& ticket);
    void relayStep(long long now);
    void dropClients(long long now);
    nlohmann::json sessionJson(const Session& s) const;
    const Session* sessionOf(const std::string& userId) const;          // the game they're in right now
    // What `viewer` may know about where `u` is: {"online", "playing": {game, title, session or null}}.
    nlohmann::json presence(const User& viewer, const User& u) const;
    bool isOnline(const User& u) const;
    nlohmann::json publicGroup(const Group& g) const;
    nlohmann::json badgesOf(const User& u) const;         // badge keys that check out
    std::vector<const Group*> groupsOf(const std::string& userId) const;
    // Group ranks, funds and games (ServerGroups.cpp; worker/server.js has the same).
    nlohmann::json ranksOf(const Group& g) const;
    nlohmann::json rankIn(const Group& g, const std::string& userId) const;   // null = not in it
    bool      groupCan(const Group& g, const User& u, const std::string& perm) const;
    long long groupFunds(const Group& g) const;
    void      groupAdd(Group& g, long long amount, const std::string& reason, const std::string& ref);
    Group*    groupFor(const Asset& a);   // the group a game (or its pass or product) belongs to
    void      paySeller(Asset& a, User& buyer, long long share, const std::string& ref);   // to the group, or the creator
    nlohmann::json meJson(const User& u) const;
    // Someone's emote slots: kEmoteSlots item ids ("" = empty), only emotes they still own.
    nlohmann::json emoteSlots(const User& u) const;
    nlohmann::json publicUser(const User& u) const;
    // me: who's looking (adds their vote on a game, like worker/server.js publicAsset(a, me)).
    nlohmann::json publicAsset(const Asset& a, const User* me = nullptr) const;
    // Private models and animations: only their creator (and staff) see them.
    bool canSee(const Asset& a, const User& me) const;
    // Paid private servers (ServerRelay.cpp; worker/server.js has the same).
    long long   privateUntil(const User& u, const std::string& gameId) const;   // 0 = they don't have one
    std::string privateBlocked(const User& u, const std::string& gameId) const; // why they can't start one ("" = they can)
    nlohmann::json uploadsToReview() const;   // new decals, sounds and T-shirts waiting for staff

    // Accounts
    User&     user(const std::string& id);          // makes a new account (with the welcome gift) if needed
    User*     findUser(const std::string& id);
    void      notify(User* u, const std::string& kind, const std::string& text, const std::string& about);   // the bell
    long long balance(const User& u) const;
    bool      hasRef(const User& u, const std::string& ref) const;
    void      add(User& u, long long amount, const std::string& reason, const std::string& ref);
    bool      isOfficial(const User& u) const;
    bool      isStaff(const User& u) const;         // official, or has the Staff badge
    bool      isVerified(const User& u) const;      // staff always are

    // Files
    void load();
    void addStarterEmotes();  // the starter emotes and animation packs (assets/emotes), as the staff account's
    void addExampleGames();   // the games folder's example games, as the staff account's
    void saveUsers();
    void saveAssets();
    void tally(Asset* a, const std::string& field, long long n = 1);   // creator stats (worker/server.js has the same)
    void saveGroups();
    void loadGroups();
    std::filesystem::path blobPath(const std::string& assetId) const;
    // Version history (games): every publish is kept, the newest kMaxVersions (and at most
    // kMaxVersionBytes of them per game). Files are files/ver-<game>-<v>. (worker/server.js has the same.)
    static constexpr size_t    kMaxVersions = 50;
    static constexpr long long kMaxVersionBytes = 100LL << 20;
    std::filesystem::path versionPath(const std::string& assetId, long long v) const;
    nlohmann::json versionsOf(const Asset& a) const;   // a.versions, or just "version 1" for older games
    // Keep `data` as the newest version (old: the file it replaces, to make version 1 of an older game).
    long long addVersion(Asset& a, const std::string& data, const std::string& by, const std::string* old, long long restored = 0);
    nlohmann::json versionsJson(const Asset& a) const;  // for the website, newest first

    Options m_opts;
    std::unique_ptr<Net::Listener> m_listener;
    std::vector<std::unique_ptr<Client>> m_clients;
    std::map<std::string, User>  m_users;
    std::map<std::string, Asset> m_assets;
    // Assets have plain numbers counting up (1, 2, 3...), like Roblox's asset IDs. New ones use the
    // number as their ID; older ones ("decal-1a2b...") answer to their number too. (worker/server.js: AssetMap)
    std::map<std::string, std::string> m_assetNums;   // number -> id
    long long m_nextAssetNum = 1;
    std::map<std::string, Asset>::iterator       findAsset(const std::string& id);
    std::map<std::string, Asset>::const_iterator findAsset(const std::string& id) const;
    void numberAssets();
    std::map<std::string, Group> m_groups;
    // Trade offers between players (Limiteds), trades.json: {id, from, to, give, get, status, created, updated}, newest last.
    nlohmann::json m_trades = nlohmann::json::array();
    // Finished trades (pasttrades.json): {id, from, to, fromName, toName, give, get, status, created, finished},
    // with the names as they were then, newest last.
    nlohmann::json m_pastTrades = nlohmann::json::array();
    // Updates staff posted (the website's Updates page; the built-in ones are in worker/updates.js), updates.json, newest first.
    nlohmann::json m_updates = nlohmann::json::array();
    nlohmann::json m_forum = nlohmann::json::object();   // thread id -> thread (forum.json)
    long long      m_forumSaved = 0;
    // What players reported (ServerSafety.cpp): {id, from, kind, target, about, reason, note, copy, at, status, ...}, newest last.
    nlohmann::json m_reports = nlohmann::json::array();
    nlohmann::json m_staffLog = nlohmann::json::array();
    nlohmann::json m_tickets = nlohmann::json::array();   // support tickets (ServerSupport.cpp), newest last
    std::map<std::string, Session> m_sessions;
    // A server whose host left: its players get a while to move to a new one, which one of them hosts.
    struct Moved {
        std::string game, title, code, newId;
        bool        priv = false;
        int         max = 12;
        std::vector<std::string> members;   // accounts, longest-playing first
        size_t      heir = 0;               // whose turn it is to host
        long long   heirSince = 0, until = 0;
    };
    std::map<std::string, Moved> m_moved;   // old session id ->
    // Game server machines (GutsAndBoltsGameServer), by their control line.
    struct Pool { std::string account; int slots = 4; std::map<std::string, long long> starting; };   // game -> when asked
    std::map<Client*, Pool> m_pools;
    bool startOnPool(const Asset& game);
    int  headcount(const Session& s) const { return (int)s.players.size() + (s.dedicated ? 0 : 1); }
    long long m_nextUserId = 2;                      // 1 is Guts (the staff account)
    std::set<std::string> m_takenNames;              // every username ever used (lower case)
    std::map<std::string, std::vector<long long>> m_failedLogins;   // username -> times of wrong passwords
    std::map<std::string, long long> m_lastPost;     // account -> when they last wrote on a wall
    std::map<std::string, long long> m_seenNonces;   // account+nonce -> when, to stop replays
    bool m_running = false;
};
