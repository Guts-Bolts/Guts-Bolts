#pragma once
#include "../game/GameGui.h"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "../renderer/Camera.h"
#include "../renderer/Framebuffer.h"
#include "../scene/Scene.h"

namespace Catalog { struct Item; }
#include "../game/Catalog.h"
#include "../game/PlayerEntry.h"
#include "../game/TouchControls.h"
#include "../core/Vr.h"
#include <imgui.h>
#include <nlohmann/json.hpp>

class AppWindow;
class NetServer;
class NetClient;
struct ChatLog;
class SceneRenderer;
class GameSession;

struct PlayerOptions {
    std::string game;              // join this game file straight away
    std::string page;              // start on "avatar" / "settings"
    std::string join;              // --join host[:port]
    bool        host = false;      // --host (with a game file)
    std::string say;               // --say <text>  (tests: chat once joined)
    std::string devCommand;        // --dev-command <lua>  (tests: the dev console's server command bar)
    bool        guest = false;     // --guest  (tests: press "Play as Guest")
    std::string screenshot;        // --screenshot <out.ppm> (tests)
    int         frames = 120;
    std::string holdKey;
    bool        createStaff = false;   // --create-staff-account
    std::string padTest;               // --pad-test "40-200:LStickUp=1 60-62:FaceDown" (tests: a fake controller)
    std::string touchTest;             // --touch-test stick|jump|look (tests: fake fingers)
    bool        testItems = false;     // --test-make-items (tests, staff only)
    std::string testGrantFor;          // --test-grant <account id> (tests, staff only: print a badge code)
    std::string testRedeem;            // --test-redeem <code> (tests)
    std::string testBoltsFor;          // --test-bolts-code <account id> (tests, staff only: print a 500 Bolts code)
    std::string testRedeemBolts;       // --test-redeem-bolts <code> (tests)
    std::string testBuy;               // --test-buy "<item name>" (tests: buy and wear it)
    std::string onlineTest;            // --online-test "op {json}|op {json}" (tests: talk to the server, print replies)
    std::string onlineGame;            // --online-game ID: press Play on a published game once online
    bool        onlinePlay = false;    // --online-play (with a game): press Play once online (public server)
    bool        privateServer = false; // --private-server (with a game): start a private server once online
    std::string testAccessory, testFace;   // --test-accessory file.json / --test-face face.png (tests)
    std::string testClothes;           // --test-clothes shirt.png,pants.png: wear these pictures (tests)
    std::string joinCode;              // --join-code <code>: join a private server once online
    std::string testSignup, testLogin; // --test-signup / --test-login "user:password" once online
    float       cameraYaw = -1000.0f;  // --camera-yaw <degrees> (tests: look from another side)
    std::vector<std::string> testOps;  // --test-op 'op {json}' (tests: send requests in order once online, print the answers)
    std::string testRename;            // --test-rename "New name" (tests: rename your first game on the Create page)
    std::string testPublish;           // --test-publish file.gbscene (tests: publish a game file to the server)
    int         createTab = 0;         // --create-tab N (tests: which Create tab to open)
    std::string testTools;             // --test-tools "print 1 click 2 drop" (tests: one step every 25 frames in a game)
    std::string testClick;             // --test-click "x,y" (tests: click there 3 times, 0..1 of the window)
    std::string testType;              // --test-type "text" (tests: type it at frame 90, then press Enter)
    float       testWheel = 0.0f;      // --test-wheel N (tests: turn the mouse wheel N notches at frame 70)
    std::string launchUrl;             // gutsandbolts://play/<game>?guest=boy (the website's Play button)
    // Studio's "Test with N players": --studio-test host:port --name Player2 joins Studio's
    // local test server straight away as that name; the window closes when the test ends.
    std::string studioTest;
    std::string studioInputTrace;   // --studio-input-trace <path>: sampled gameplay input for a Studio test bundle
    bool        vr = false;            // --vr: show the game in a VR headset once it starts
    std::string vrTestHands;           // --vr-test-hands "lx,ly,lz,rx,ry,rz": pretend controllers there (metres from the head, tests)
    std::string testName;
};

// Guts&BoltsPlayer: the platform app. Browse the games on this computer,
// dress up your avatar, and play — no editor in sight.
class PlayerApp {
public:
    explicit PlayerApp(PlayerOptions opts);
    ~PlayerApp();
    void run();
    void frame();

private:
    enum class Page { Home, Games, Avatar, GameInfo, Game, Catalog, Staff, Bolts, Create, People, Profile, Groups, Group, Friends, Login, Messages, Forum };
    // How a game is started: alone, or as the host of a server.
    enum class HostMode { Solo, Lan, Public, Private };
    using Starter = std::function<void(HostMode)>;   // loads the game (downloading it if needed) and starts it

    struct GameCard {
        std::filesystem::path        path;
        GameInfo                     info;
        std::unique_ptr<Framebuffer> thumb;
        bool                         broken = false;
        bool                         gore = false;       // blood / oil on
        bool                         ragdoll = false;
    };

    void frame(float dt);
    void drawTopBar(ImVec2 pos, float width);
    void drawHome();
    void drawGames();
    void drawGameInfo();
    void drawRow(const char* title, const std::vector<int>& games, const char* seeAll);
    bool drawTile(int index);
    void drawAvatar(float dt);
    void drawGame(float dt);
    // The classic loading screen: the game's name, who made it and a spinner.
    void drawLoading(ImVec2 pos, ImVec2 size, float alpha, const char* status);
    // From pressing Play until the game shows: the connecting screen over everything.
    bool        m_connectScreen = false;
    GameGui::Input m_guiInput;                     // the game's own UI: which button is pointed at / pressed
    std::string m_loadingGameId;                   // server game id (for its icon), or ""
    std::string m_loadingIcon;                     // the icon file, once downloaded
    std::string m_loadingTitle, m_loadingAuthor;   // the game's name and creator on the server
    void startLoadingScreen(const std::string& gameId, const std::string& title);
    void drawConnectScreen();
    void drawPauseMenu();
    std::vector<PlayerEntry> currentPlayers() const;   // everyone in the game (or just you, offline)
    // Voice chat works here: a multiplayer game that allows it, and we turned it on.
    bool voiceHere() const;
    void updateVoice(bool acceptInput);
    void askForMicrophone();   // phones ask "Allow Guts&Bolts to record audio?" (once)
    void drawVoiceHud(ImDrawList* dl, ImVec2 min, ImVec2 max, bool touch);
    void drawCatalog();
    void drawItemDialog();
    void drawStaff();
    void drawBolts();

    // Online (a Guts&Bolts server) — PlayerOnline.cpp
    void drawServerButton(ImVec2 at);
    void drawNoServer();
    void fetchAvatarParts();                   // download worn accessories / faces, then put them on
    std::set<std::string> m_avatarFetching;    // asset ids already asked for
    void drawServerCards(const std::string& gameKey, const std::string& title);   // game page: who's playing where   // not connected: "Connecting..." / "Can't reach Guts&Bolts"
    // Automated tests and Studio's multiplayer test may play offline.
    bool testMode() const { return (!m_opts.screenshot.empty() && m_opts.page != "noserver") || studioTest(); }
    bool studioTest() const { return !m_opts.studioTest.empty(); }
    void drawOnlineCatalog();
    void drawWardrobe();
    void drawModeration();                          // the ban screen and staff warnings (PlayerOnline.cpp)                            // Avatar page: what you own, click to wear (PlayerOnline.cpp)
    void wardrobeToggle(const Catalog::Item& it);
    bool itemOn(const Catalog::Item& it) const;   // worn, or (gear) equipped
    void toggleGear(const std::string& id, bool on);
    // Emotes (into the first empty emote slot) and animation packs, on or off (worker/server.js
    // emotes.equip / animpack.equip).
    void toggleMove(const Catalog::Item& it, bool on);
    // Your equipped emotes and pack (Online::me()) -> Emotes::mine(), downloading their files.
    // Every frame; only does something when they change.
    void syncEmotes();
    std::string m_emotesSynced;
    void drawOnlineItemDialog();
    void drawCreate();
    void drawMyGames();
    void myGameRow(const std::string& key, const std::string& title, const std::string& sub, unsigned thumbTex,
                   const std::string& cardId, const std::filesystem::path& path, const std::string& publishedId,
                   const std::function<void()>& play);
    void drawUploadForm(const std::string& kind);
    void drawMyUploads(const std::string& kind);
    static bool renameGameFile(const std::filesystem::path& path, const std::string& title, std::string& error);
    void renameGame(const std::filesystem::path& path, const std::string& publishedId, const std::string& title);
    // Put a game file from this computer on the server (so it's on the website for everyone), with
    // `picture` (a .png) for its card, and remember in the file that it's published.
    void publishGameFile(const std::filesystem::path& path, const std::string& picture,
                         const std::function<void(bool ok, const std::string& message)>& done);
    void openInStudio(const std::filesystem::path& path);
    void drawOnlineGames();
    void drawOnlineGameDialog();
    void drawOnlineBolts();
    void drawOnlineStaff();
    void refreshOnline(const std::string& what);   // "catalog", "games", "mine", "history"
    void onlinePlayTick(float dt);

    // People and groups — PlayerSocial.cpp
    void drawPeople();
    void drawProfile();
    // Item pictures: real renders of a mannequin wearing each item (ItemRenders.cpp).
    struct ItemRender {
        std::unique_ptr<Catalog::Item> item;
        std::unique_ptr<Framebuffer>   fb;
        bool   done = false;
        double lastUsed = 0, retryAt = 0, firstTry = 0;
        // A saved outfit instead of one item (PlayerMail.cpp): the whole look on the mannequin.
        bool   outfit = false;
        BodyColors colors;
        HatStyle   hat = HatStyle::None;
        glm::vec3  hatTint{-1.0f};
        std::vector<Catalog::Item> wearing;
    };
    std::map<std::string, ItemRender> m_itemRenders;
    void itemPicture(ImDrawList* dl, ImVec2 c, float s, const Catalog::Item& it);
    void updateItemRenders();
    bool renderItem(ItemRender& e);
    static bool dressPlayer(Player& p, BodyColors bc, HatStyle hat, glm::vec3 hatTint, const std::vector<Catalog::Item>& items);
    // PlayerMail.cpp: messages, saved outfits, favourite and recently played games.
    void drawMessages();
    void openNewMessage(const std::string& to, const std::string& subject = "");
    // The bell (notifications), left of the account box. PlayerMail.cpp.
    void drawBell(ImVec2 center);
    nlohmann::json m_notes = nlohmann::json::array();
    bool           m_notesLoaded = false;
    int  unreadMessages() const;
    void drawOutfits();
    void outfitPicture(ImDrawList* dl, ImVec2 c, float s, const nlohmann::json& outfit);
    void refreshMyGames();
    void drawFeed();
    // Create > Library: public models, decals, audio and plugins, each with its own page (PlayerLibrary.cpp)
    static constexpr int kLibraryTab = 8;
    static constexpr int kStatsTab = 9;   // Create > Stats: how your games and items are doing
    void     drawStats();
    nlohmann::json m_stats;
    double         m_statsAt = -100.0;
    void     drawLibrary();
    void     drawAsset();
    void     openAsset(const std::string& id);   // "gb:decal-..." or just "decal-..."
    void     stopAssetSound();
    unsigned libraryPicture(const nlohmann::json& asset, int& w, int& h);
    void setFavorite(const std::string& id, bool on);
    nlohmann::json m_messages = nlohmann::json::array();
    std::string    m_msgBox = "inbox", m_msgTo, m_msgSubject, m_msgBody, m_msgStatus;
    double         m_messagesAt = -100.0;
    nlohmann::json m_outfits = nlohmann::json::array();
    double         m_outfitsAt = -100.0, m_restageUntil = 0.0, m_restageAt = 0.0;
    std::string    m_outfitName, m_outfitMsg, m_renameOutfitId, m_renameOutfitName;
    nlohmann::json m_recentGames = nlohmann::json::array(), m_favGames = nlohmann::json::array();
    nlohmann::json m_openGame = nlohmann::json::object();   // the game in the online game popup
    // Comments under the open game (PlayerOnline.cpp drawComments; the website's game page has the same).
    void           drawComments(const std::string& gameId);
    nlohmann::json m_comments = nlohmann::json::array();
    std::string    m_commentsGame, m_commentText, m_commentMsg;
    bool           m_commentsMore = false, m_commentsOff = false;
    long long      m_commentCount = 0;
    double         m_myGamesAt = -100.0;
    // Someone's friends / following / followers (a popup over their profile).
    void openPeople(const std::string& user, const std::string& which, int page = 0);
    void drawPeopleDialog();
    std::string    m_peopleUser, m_peopleWhich;
    int            m_peoplePage = 0;
    long long      m_peopleTotal = 0;
    nlohmann::json m_people = nlohmann::json::array();
    std::string    m_peopleMsg;
    // Someone's inventory (a popup over their profile): what they own from the catalog.
    void openInventory(const std::string& user, const std::string& cat, int page = 0);
    void drawInventoryDialog();
    void drawItemTiles(const nlohmann::json& items, int perRow);   // item pictures in a grid (+ Limited numbers)
    std::string    m_invUser, m_invCat, m_invMsg;   // m_invCat empty = closed
    int            m_invPage = 0;
    long long      m_invTotal = 0;
    nlohmann::json m_inv = nlohmann::json::array(), m_invCounts = nlohmann::json::object();
    void drawGroups();
    void drawGroup();
    void openProfile(const std::string& accountId);
    void openGroup(const std::string& groupId);
    bool needsServer(const char* what);   // "you need a server" note; true if offline
    void drawAccount();
    void drawNotice();
    void padCamera(float dt);   // a controller's right stick / D-pad
    // VR headsets (PlayerVr.cpp): start / stop, the controllers, the view from your head.
    void startVr();
    void stopVr();
    void vrControls(bool acceptInput);
    void vrTrack();                    // your real head and hands -> the character's head and arms
    void vrFollow(Player& player);
    void vrShowBody(Player& player);   // in VR you see your own body (all but the head)
    void vrRender(const Vr::View eyes[2]);
    void vrMirror(ImVec2 size);
    void vrPointer(bool menus);        // the floating menu screen: where it is, and the right hand pointing at it
    void vrOverlay();                  // on top of everything: the pointer, and the keyboard when you're typing
    int  vrKeyAt(ImVec2 p) const;      // the headset keyboard's key there (0 = none)
    void vrPressKey(int code);
    void vrAfterUi();                  // the menus and buttons onto that screen, then the headset's picture is done
    // Reporting and blocking — PlayerSafety.cpp
    bool canReport() const;   // signed up and online (guests can't report or block)
    void openReport(const std::string& kind, const std::string& id, const std::string& name, const std::string& blockUser = "");
    void drawReportDialog();
    void setBlocked(const std::string& user, bool on);
    void drawBlockedList();   // Avatar > Your account
    void drawReportsBox();    // the Staff page
    void drawUploadsBox();    // the Staff page: new uploads waiting for a check
    nlohmann::json m_uploads = nlohmann::json::array();
    void           drawStaffLog();   // PlayerSafety.cpp: what staff did lately (the website's Staff page has the same)
    nlohmann::json m_staffLog = nlohmann::json::array();
    double         m_staffLogAt = -100.0;
    double         m_uploadsAt = -100.0;
    std::string    m_reportKind, m_reportId, m_reportName, m_reportBlockUser, m_reportNote, m_reportMsg;
    int            m_reportReason = 0;
    bool           m_reportWanted = false, m_reportAlsoBlock = false, m_reportsClosed = false;
    nlohmann::json m_blocked = nlohmann::json::array(), m_reports = nlohmann::json::array();
    double         m_blockedAt = -100.0, m_reportsAt = -100.0;
    bool           m_noticePlain = false;   // the notice is just a message (no "Copy my account ID")
    void updateTouch(ImVec2 min, ImVec2 max, bool acceptInput);

    // Sign up / log in — PlayerLogin.cpp
    bool needsLogin() const;
    void drawLogin();
    void signUp(const std::string& username, const std::string& password);
    void logIn(const std::string& username, const std::string& password);
    void logOut();

    // Friends and servers — PlayerFriends.cpp
    void drawFriends();
    void refreshFriends();
    void friendButton(const std::string& accountId, const std::string& status);   // Add / Accept / Friends
    // Play: a public server. `tries`: asking again while a game server machine starts one.
    void playGame(const std::string& gameKey, const std::string& title, Starter start, int tries = 0);
    // Your own private server: a game server machine runs it if one is free (so it stays up after
    // you leave), otherwise you host it. `code`: the one it's starting, while we wait for it.
    void playPrivate(const std::string& gameKey, const std::string& title, Starter start, const std::string& code = "", int tries = 0);
    std::string    m_waitKey, m_waitTitle;   // a game server machine is starting this game: ask again at m_waitAt
    std::string    m_waitCode;               // (a private server: its code)
    bool           m_waitPrivate = false;
    Starter        m_waitStart;
    double         m_waitAt = 0.0;
    int            m_waitTries = 0;
    void openServers(const std::string& gameKey, const std::string& title, Starter start);
    void drawServersDialog();
    void joinRelay(const std::string& session, const std::string& code, const std::string& title);
    Starter localStarter(const std::filesystem::path& path);
    Starter onlineStarter(const std::string& assetId);
    void giveGear(const std::string& gameKey);   // your equipped catalog gear, in games that allow it

    void refreshGames();
    void joinGame(const std::filesystem::path& path, HostMode mode = HostMode::Solo, const std::string& gameKey = "");
    void joinServer(const std::string& address);
    void drawChat(ImVec2 min, ImVec2 max);
    void drawJoinDialog();
    ChatLog& chat();
    void sendChat(const std::string& text);
    void leaveGame();
    void buildAvatarStage();
    // The 3D avatar on someone's profile (their colours, hat and clothes).
    void buildProfileStage(const nlohmann::json& avatar, const nlohmann::json& wearing);

    PlayerOptions                  m_opts;
    std::unique_ptr<AppWindow>     m_window;
    std::unique_ptr<SceneRenderer> m_renderer;
    std::unique_ptr<Scene>         m_scene;        // the game being played
    std::unique_ptr<GameSession>   m_session;
    std::unique_ptr<Scene>         m_avatarScene;  // preview on the Avatar page
    std::unique_ptr<Scene>         m_profileScene; // the avatar on the Profile page
    std::string                    m_profileSceneFor;
    double                         m_profileRetryAt = 0;   // rebuild once their clothes have downloaded
    Camera                         m_profileCam;
    Framebuffer                    m_profileView;
    // "Choose Your Character" (guests): a boy and a girl to play as.
    bool                           m_charPickOpen = false;
    std::unique_ptr<Scene>         m_charScene[2];
    Framebuffer                    m_charView[2];
    Camera                         m_charCam;
    void drawCharacterPicker();
    void applyGuestLook(int which);    // 0 = boy (black cap), 1 = girl (ponytail)
    // A gutsandbolts:// link to act on once we're online (the website's Play button).
    std::string                    m_linkGame, m_linkGuest, m_linkServer;
    bool                           m_linkEdit = false;   // the link was "Edit in Studio"
    double                         m_linkPollAt = 0.0;
    void takeLink(const std::string& url);
    void followLink();

    Page        m_page = Page::Home;
    std::vector<GameCard> m_games;
    std::string m_search;
    std::string m_status;                          // "Couldn't load ..." etc.
    std::string m_currentTitle;
    std::string m_currentAuthor;                   // for the loading screen
    float       m_loadingT = 0.0f;                 // seconds of loading screen left (fades out)

    Camera      m_camera;
    Framebuffer m_view;
    Framebuffer m_vrEyes[2];     // what each eye sees, in VR
    bool        m_vrOn = false;
    bool        m_vrAligned = false;   // turned so the headset starts looking where the character faces
    bool        m_vrTurned = false;    // the right stick is pushed over (one snap turn per push)
    float       m_vrYaw = 0.0f;        // degrees: how far the snap turns (and lining up) turned you
    bool        m_vrBodyAligned = false;
    float       m_vrScale = 1.45f;     // studs per real metre (from the character's eye height)
    float       m_vrBodyYaw = 0.0f;    // degrees: the way the body faces (it follows the head, a little behind)
    std::string m_vrStatus;            // why VR didn't start / stopped (shown in the menu)
    Framebuffer m_vrPanel;             // the menus and buttons, for the floating screen in the headset
    glm::mat4   m_vrPanelPose{1.0f};   // where that screen is (headset space, metres)
    bool        m_vrPanelPlaced = false;
    bool        m_vrPanelMoving = false;   // gliding back in front of you (you looked far away)
    bool        m_vrWasPaused = false;
    bool        m_vrPointing = false;  // the right hand points at the screen (its trigger clicks it)
    bool        m_vrPointerDown = false;
    bool        m_vrOnGameUi = false;  // the pointer is on the game's own buttons or the hotbar (this frame)
    bool        m_vrOverUi = false;    // ...or on any button (last frame): the trigger clicks it, not your tool
    bool        m_vrMenus = false;     // a menu (or the games list) is open: the pointer clicks it
    bool        m_vrOnKeyboard = false;   // the pointer is on the headset keyboard
    bool        m_vrShift = false;
    bool        m_vrAutoStarted = false;  // Quest: VR started by itself when the app opened
    std::vector<std::pair<ImVec4, int>> m_vrKeys;   // the headset keyboard's keys (window pixels, what they type)
    ImVec2      m_vrPointer{0, 0};     // where on the screen (window pixels)
    Camera      m_avatarCam;
    Framebuffer m_avatarView;
    Framebuffer m_bannerView;
    int         m_selected = -1;                   // game shown on the GameInfo page
    std::string m_category = "all";                // filter on the Games page

    std::unique_ptr<NetServer>     m_server;       // we are hosting
    std::unique_ptr<NetClient>     m_client;       // we joined someone
    std::unique_ptr<ChatLog>       m_soloChat;     // chat when playing alone
    std::string m_chatInput;
    bool        m_chatOpen = false;
    bool        m_talkOn = false;     // voice chat: the Talk button (phones) is switched on
    bool        m_micAsked = false;   // asked the phone for the microphone already
    // Developer Console (PlayerDevConsole.cpp): F9 or /devconsole.
    bool        m_devConsole = false, m_devFollow = true;
    int         m_devTab = 0;
    std::string m_devFilter, m_devCommand;
    std::string m_gameOwner;   // the account that made the game we're hosting
    bool        m_iMadeThis = false;   // the game we're in is ours (or a file on this computer)
    // TeleportService: a script sent us to another game.
    std::string    m_playingKey;                    // the published game we're in ("" = a file or joined)
    nlohmann::json m_teleportData;                  // what we bring there: {"data", "from"} (null = not teleporting)
    bool           m_teleporting = false;           // looking the game up
    void teleportMe(const std::string& place, const nlohmann::json& data, const std::string& from);
    void drawDevConsole();
    // Photo mode (PlayerPhoto.cpp): F2 or the menu's button hides the HUD and lets the
    // camera fly on its own; Enter saves the 3D view as a picture.
    bool        m_photo = false;
    Camera      m_photoCam;
    glm::vec3   m_photoMove{0.0f};      // the phone's arrow buttons held down (forward, right, up)
    bool        m_photoOnButton = false;   // the pointer is on a button (so a drag doesn't turn the camera)
    bool        m_photoHideBar = false;    // H: not even the bar
    float       m_photoFlash = 0.0f;
    std::string m_photoMsg;
    double      m_photoMsgUntil = 0.0;
    bool canTakePhotos() const;
    void startPhoto();
    void photoCamera(float dt, bool hovered);
    void takePhoto();
    void drawPhotoMode(ImDrawList* dl, ImVec2 pos, ImVec2 max);
    // Game passes: the Buy window a game's script asked for (PlayerPasses.cpp).
    std::string    m_passPrompt, m_passMsg;   // the pass being offered ("" = none)
    nlohmann::json m_passInfo;
    bool           m_passForHost = false, m_passBusy = false, m_passOwned = false;
    void updatePassPrompts();
    void openPassPrompt(const std::string& pass, bool forHost);
    void finishPassPrompt(bool bought);
    void drawPassPrompt();
    std::string    m_passesKey;               // the game whose passes we have (its page)
    nlohmann::json m_passes = nlohmann::json::array();
    void drawGamePasses(const std::string& gameKey);
    bool devServerAccess() const;
    void runDevCommand(const std::string& code);   // the server command bar's Enter
    // The leaderboard: folded away or not (Tab), and the little menu you get by
    // clicking someone's name (Add Friend / Follow).
    bool        m_listOpen = true;
    std::string m_listMenu;          // whose menu is open ("" = none)
    ImVec2      m_listMenuAt{0, 0};
    nlohmann::json m_listRel;        // where you stand with them (friends.relation)
    std::string m_listMsg;           // "Friend request sent!" and such
    void        drawPlayerMenu();
    bool        m_showJoin = false;
    std::string m_joinAddress;

    // Catalog
    std::vector<Catalog::Item> m_items;
    int         m_itemType = -1;                   // filter; -1 = everything
    int         m_openItem = -1;                   // item shown in the item dialog
    std::string m_catalogMsg;

    // Account / staff
    std::string m_notice;                          // popup message
    std::string m_nameEdit, m_nameError;
    std::string m_redeemCode, m_redeemMsg;
    int         m_grantBadge = 1;
    std::string m_grantTo, m_grantCode, m_grantError;

    // Bolts
    std::string m_boltsCode, m_boltsMsg;           // redeem box on the Bolts page
    int         m_giveBolts = 100;                 // staff: amount for a Bolts code
    std::string m_giveBoltsTo, m_giveBoltsCode, m_giveBoltsError;
    std::string m_boltsToast;                      // "+5 Bolts for playing!"
    double      m_boltsToastUntil = 0.0;
    std::string m_buyMsg;                          // catalog item dialog

    // Online
    nlohmann::json m_onlineItems = nlohmann::json::array();   // server hats / shirts / pants
    nlohmann::json m_wardrobe = nlohmann::json::array();      // the Avatar page: items you own
    double      m_wardrobeAt = -100.0;                         // when it was last asked for
    int         m_avatarTab = 0;                               // 0 Wardrobe, 1 Body
    int         m_wardrobeKind = 0;                            // which kind of item it shows
    nlohmann::json m_onlineGames = nlohmann::json::array();
    nlohmann::json m_featuredGames = nlohmann::json::array();   // the staff's picks
    nlohmann::json m_myCreations = nlohmann::json::array();
    nlohmann::json m_onlineHistory = nlohmann::json::array();
    nlohmann::json m_foundUsers = nlohmann::json::array();
    int            m_openOnlineItem = -1, m_openOnlineGame = -1;
    std::string    m_onlineMsg, m_createMsg, m_staffMsg, m_findQuery;
    std::string    m_banTarget, m_banTargetName, m_banNote;   // the "Ban account" popup
    int            m_banReason = 0;                           // index into Online::kBanReasons
    int            m_banDays = 4;                             // index into the ban lengths (last = forever)
    bool           m_warnMode = false;                        // the popup sends a warning, not a ban
    std::string    m_warnAcking;                              // the warning we said "I understand" to
    std::string    m_appealText, m_appealMsg;                 // the ban screen's appeal (appeal.send)
    bool           m_appealBusy = false;
    std::string    m_deleteName, m_deletePassword, m_deleteMsg;   // Your account > Delete my account (account.delete)
    bool           m_deleteBusy = false;
    int            m_createKind = 0, m_createPrice = 0, m_giveServerBolts = 100;
    std::string    m_createName, m_createDesc, m_createPath;
    std::string    m_renameKey, m_renameText;      // My Games row being renamed
    bool           m_renameFocus = false;
    bool           m_gamesDirty = false;           // a game was renamed: re-read the games list
    std::set<std::string> m_askedDownloads;        // decal pictures already asked for
    std::string    m_testLastId;                   // --online-test: id of the last upload ("$LAST")
    double         m_avatarPushAt = 0.0;           // save the avatar on the server at this time (0 = nothing to save)
    bool           m_busy = false;                 // waiting on the server
    float          m_onlinePlaySeconds = 0.0f;
    std::string    m_loaded;                       // lists already fetched this visit ("catalog games ...")

    // People and groups
    std::string    m_peopleQuery, m_profileId, m_groupId, m_groupQuery, m_socialMsg;
    std::string    m_statusEdit, m_blurbEdit, m_feedPost;   // classic "Right now I'm..." and About me
    nlohmann::json m_feed = nlohmann::json::array();
    std::string    m_libKind = "model", m_libQuery, m_libLoaded, m_libIdInput, m_assetId, m_assetMsg;
    nlohmann::json m_libList = nlohmann::json::array(), m_asset;
    int            m_assetSound = 0;     // the Listen button's sound
    double         m_copiedAt = -100.0;
    double         m_feedAt = -1000.0;
    nlohmann::json m_peopleResults = nlohmann::json::array();
    nlohmann::json m_profile = nlohmann::json::object();
    nlohmann::json m_groupList = nlohmann::json::array();
    nlohmann::json m_myGroups = nlohmann::json::array();
    nlohmann::json m_group = nlohmann::json::object();
    int            m_groupsTab = 0;                // My groups / Browse / Create
    int            m_groupTab = 0;                 // a group page: Wall / Members / Admin
    std::string    m_newGroupName, m_newGroupDesc, m_wallInput, m_shoutInput, m_editDesc;
    glm::vec3      m_newGroupColor{0.23f, 0.48f, 0.84f};
    bool           m_newGroupOpen = true, m_editingGroup = false;
    std::map<std::string, nlohmann::json> m_rankEdits;   // the owner's rank forms, by rank id ("" = a new one)
    nlohmann::json m_groupMyGames = nlohmann::json::array();   // your games, for "Add one of your games"
    int            m_groupAddGame = 0, m_payTo = 0, m_payAmount = 0;

    // The Forum (PlayerForum.cpp)
    void drawForum();
    void drawForumThread();
    void openForum(const std::string& board, const std::string& thread = "", int page = 0);
    nlohmann::json m_forum = nlohmann::json::object();   // what the server sent for the page we're on
    std::string    m_forumBoard, m_forumThread, m_forumTitle, m_forumText, m_forumReply, m_forumMsg;
    int            m_forumPage = 0;
    bool           m_forumWriting = false;                 // the New thread form is open

    // Sign up / log in
    int            m_loginTab = 0;                 // Sign Up / Log In
    bool           m_signupAgree = false;          // "I'm 18+ and agree to the Terms" (sign up)
    bool           m_showTerms = false;            // the Terms of Service window
    std::string    m_loginUser, m_loginPass, m_loginPass2, m_loginMsg;
    std::string    m_loginCode;                    // two-step verification: the code from your email
    bool           m_loginNeedCode = false;
    bool           m_loginAppCode = false;         // ...from an authenticator app (not email)
    nlohmann::json m_nameCheck = nlohmann::json::object();   // is the typed username free?
    double         m_nameCheckAt = 0.0;

    // Friends and servers
    nlohmann::json m_friends = nlohmann::json::object();   // friends.list reply
    double         m_friendsAt = -100.0;           // when we last asked
    int            m_friendsTab = 0;               // Friends / Requests / Add friends
    std::string    m_friendQuery, m_friendMsg;
    nlohmann::json m_friendSearch = nlohmann::json::array();
    bool           m_serversOpen = false;
    std::string    m_serversKey, m_serversTitle, m_serversMsg, m_codeInput;
    Starter        m_serversStart;
    nlohmann::json m_serverList = nlohmann::json::array();
    nlohmann::json m_serverPrivate = nlohmann::json::object();   // servers.list: {privatePrice, privateUntil, privateNeedsBuy}
    // The game page's server cards (Roblox-style): whose game, when fetched, which page.
    nlohmann::json m_gameServers = nlohmann::json::array();
    std::string    m_gameServersKey;
    double         m_gameServersAt = -100.0;
    int            m_gameServersPage = 0;
    std::string    m_playMsg;                      // "Finding a server..."
    bool           m_joinedOnce = false;           // fetched the game's sounds after joining
    // The host left: everyone moves to a new server that one of the players hosts (PlayerFriends.cpp).
    std::string    m_moveFrom, m_moveGame, m_moveTitle;   // the server they left, its game
    double         m_moveUntil = 0.0, m_moveRetryAt = 0.0;
    std::string    m_continues;                    // hosting: the old server we're taking over
    std::string    m_tookOver;                     // ...and while the server says yes or no to that
    double         m_tookOverUntil = 0.0;
    // The world as it was when the host left (handoffSnapshot): if we're the one who takes
    // over, we carry on in it instead of starting the game over. Everyone stays in the game
    // (no loading screen) while the new host gets going; m_handoff shows the note about it.
    std::string    m_carry, m_carryGame;
    bool           m_handoff = false;
    void startMove(const std::string& session, const std::string& game, const std::string& title);
    void moveStep();
    bool moving() const { return !m_moveFrom.empty(); }
    bool           m_autoStarted = false;          // test options that wait for the server
    bool           m_autoServers = false;

    TouchControls m_touch;
    ImVec2      m_chatMin{0, 0}, m_chatMax{0, 0};  // where the chat box was last frame
    size_t      m_chatSeen = 0;                    // phones: chat pops up briefly for new messages
    double      m_chatShowUntil = 0.0;
    void        touchScroll();

    bool        m_paused = false;
    bool        m_showSettings = false;
    bool        m_shiftLock = false;   // Roblox Shift Lock (Shift toggles it)
    bool        m_padClick = false;    // a controller's RT held (it clicks ClickDetectors)
    int         m_menuTab = 0;         // in-game menu: 0 Players, 1 Settings, 2 Help
    int         m_menuConfirm = 0;     // 1 = "Reset character?", 2 = "Leave game?"
    int         m_frame = 0;
};
