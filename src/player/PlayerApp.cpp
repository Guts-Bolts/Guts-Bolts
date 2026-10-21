#include "PlayerApp.h"
#include "../core/Language.h"
#include "../core/Pad.h"
#include <map>
#include "SiteUi.h"
#include "LaunchLink.h"
#include "../game/GameGui.h"
#include "../game/EmoteMenu.h"
#include "../scene/Emotes.h"
#include "../renderer/Textures.h"
#include "../core/AppWindow.h"
#include "../game/PlayCamera.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../core/Settings.h"
#include "../game/GameSession.h"
#include "../game/ClickDetectors.h"
#include "../game/Hud.h"
#include "../game/Profile.h"
#include "../game/SettingsWindow.h"
#include "../game/UpdateToast.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Physics.h"
#include "../scene/Serializer.h"
#include "../net/NetGame.h"
#include "../core/Audio.h"
#include "../core/Voice.h"
#include "../core/Account.h"
#include "../game/Badges.h"
#include "../game/Bolts.h"
#include "../online/OnlineClient.h"
#include "../online/AssetCache.h"
#include "../online/Protocol.h"

#ifdef __ANDROID__
#include <SDL.h>   // SDL_AndroidRequestPermission (voice chat's microphone)
#endif
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <ctime>
#include <fstream>
#include <sstream>
#include <cctype>
#include <cmath>
#include <cstdio>

using namespace Site;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Put the camera behind / above whatever the spawn point is.
void frameSpawn(Scene& scene, Camera& cam) {
    glm::vec3 target(0.0f, 1.0f, 0.0f);
    if (SceneNode* s = scene.root()->findChild("SpawnLocation", true))
        target = glm::vec3(s->worldMatrix()[3]) + glm::vec3(0, 1.5f, 0);
    cam.pivot    = target;
    cam.yaw      = 45.0f;
    cam.pitch    = 28.0f;
    cam.distance = 22.0f;
}



} // namespace

PlayerApp::PlayerApp(PlayerOptions opts) : m_opts(std::move(opts)) {
    if (studioTest()) {   // Studio's multiplayer test: smaller windows, so a few fit on the screen
        if (!m_opts.testName.empty()) Online::setTestName(m_opts.testName);
        const std::string title = Online::playerName() + " - Guts&Bolts multiplayer test";
        m_window = std::make_unique<AppWindow>(title.c_str(), 800, 450, "player_layout.ini");
    } else {
        m_window = std::make_unique<AppWindow>("Guts&Bolts Player", 1280, 720, "player_layout.ini");
    }
    m_window->setFixedTimestep(!m_opts.screenshot.empty());
    m_window->setAfterUi([this] { vrAfterUi(); });
    m_renderer = std::make_unique<SceneRenderer>();
    m_scene    = std::make_unique<Scene>();
    m_session  = std::make_unique<GameSession>(m_scene.get());
    m_session->scripts().setTeleportsWork(true);   // (Studio leaves it off: there it only prints a note)
    m_soloChat = std::make_unique<ChatLog>();
    if (!m_opts.touchTest.empty() && m_opts.touchTest.rfind("sdl-", 0) != 0)
        GraphicsSettings::get().touchControls = GraphicsSettings::TouchOn;
    if (m_opts.createStaff) {
        m_notice = Account::createStaffAccount();
        std::string err;
        if (Account::iAmStaff()) Profile::get().rename(Account::kStaffName, err);
    }
    if (m_opts.testItems) {
        std::string msg;
        Catalog::Item a; a.name = "Red Cap"; a.description = "A classic."; a.type = Catalog::Type::Hat;
        a.hat = HatStyle::Cap; a.color = {0.85f, 0.1f, 0.1f}; a.price = 75;
        Catalog::create(a, msg); Log::info(msg);
        Catalog::Item b; b.name = "Bolt Tee"; b.type = Catalog::Type::Shirt; b.color = {0.2f, 0.5f, 0.9f};
        Catalog::create(b, msg); Log::info(msg);
        Catalog::Item c; c.name = "Oil Jeans"; c.type = Catalog::Type::Pants; c.color = {0.15f, 0.15f, 0.2f}; c.price = 250;
        Catalog::create(c, msg); Log::info(msg);
    }
    if (!m_opts.testGrantFor.empty()) {
        std::string err;
        // --test-grant <account id>[:badge key]
        std::string who = m_opts.testGrantFor, key = "tester";
        if (auto c = who.find(':'); c != std::string::npos) { key = who.substr(c + 1); who = who.substr(0, c); }
        Badges::Id bid = Badges::Id::Tester;
        Badges::fromKey(key, bid);
        std::string code = Badges::makeCode(bid, who, err);
        std::printf("BADGECODE %s %s\n", code.c_str(), err.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testRedeem.empty()) {
        std::string msg;
        Badges::redeem(m_opts.testRedeem, msg);
        std::printf("REDEEM %s\n", msg.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testBoltsFor.empty()) {
        std::string err;
        std::string code = Bolts::makeCode(500, m_opts.testBoltsFor, err);
        std::printf("BOLTSCODE %s %s\n", code.c_str(), err.c_str());
        std::fflush(stdout);
    }
    if (!m_opts.testRedeemBolts.empty()) {
        std::string msg;
        Bolts::redeem(m_opts.testRedeemBolts, msg);
        std::printf("REDEEMBOLTS %s (balance %lld)\n", msg.c_str(), Bolts::balance());
        std::fflush(stdout);
    }
    if (!m_opts.onlineTest.empty()) {
        // Each step: "op {json args}". $GRANT:<badge>:<account> in a string becomes a real signed grant.
        std::stringstream ss(m_opts.onlineTest);
        std::string step;
        while (std::getline(ss, step, '|')) {
            size_t sp = step.find(' ');
            std::string op = step.substr(0, sp);
            nlohmann::json args = sp == std::string::npos ? nlohmann::json::object()
                                                          : nlohmann::json::parse(step.substr(sp + 1), nullptr, false);
            if (!args.is_object()) args = nlohmann::json::object();
            if (args.contains("grantFor")) {   // make the signed badge here, with our key
                std::string err;
                Badges::Id bid = Badges::Id::Verified;
                Badges::fromKey(args.value("key", std::string("verified")), bid);
                auto g = Badges::makeGrant(bid, args["grantFor"].get<std::string>(), err);
                args["to"] = args["grantFor"]; args.erase("grantFor");
                args["key"] = g.first; args["sig"] = g.second;
            }
            if (args.contains("id") && args["id"] == "$LAST") args["id"] = m_testLastId;   // the thing uploaded just before
            if (args.contains("fileB64")) {    // upload a file from disk
                std::ifstream f(args["fileB64"].get<std::string>(), std::ios::binary);
                std::stringstream buf; buf << f.rdbuf();
                args["data"] = Online::base64Encode(buf.str());
                args.erase("fileB64");
            }
            Online::request(op, args, [this, op](const nlohmann::json& r) {
                if (r.contains("asset") && r["asset"].is_object()) m_testLastId = r["asset"].value("id", m_testLastId);
                nlohmann::json shown = r;
                if (shown.contains("data")) shown["data"] = "(" + std::to_string(shown["data"].get<std::string>().size()) + " base64 chars)";
                if (shown.contains("me")) shown["me"] = {{"name", r["me"].value("name", "")}, {"bolts", r["me"].value("bolts", 0)},
                                                         {"id", r["me"].value("id", "")},
                                                         {"verified", r["me"].value("verified", false)},
                                                         {"staff", r["me"].value("staff", false)},
                                                         {"username", r["me"].value("username", "")},
                                                         {"userId", r["me"].value("userId", 0)},
                                                         {"hasPassword", r["me"].value("hasPassword", false)}};
                std::printf("ONLINE %s -> %s\n", op.c_str(), shown.dump().substr(0, 600).c_str());
                std::fflush(stdout);
            });
            Online::finishAll();
        }
    }
    buildAvatarStage();
    refreshGames();
    m_items = Catalog::load();
    std::printf("CATALOG %d items, account %s, staff %d\n", (int)m_items.size(), Account::shortId().c_str(),
                (int)Account::iAmStaff());
    for (const Catalog::Item& it : m_items) {
        if (m_opts.testBuy.empty() || it.name != m_opts.testBuy) continue;
        std::string msg;
        bool ok = Catalog::buy(it, msg);
        if (ok) Catalog::wear(it);
        std::printf("BUY %d %s (balance %lld, owns %d)\n", (int)ok, msg.c_str(), Bolts::balance(), (int)Catalog::owns(it));
    }
    std::fflush(stdout);

    if (m_opts.guest) Online::setGuest(true);   // tests: "Play as Guest"
    if (!m_opts.launchUrl.empty()) takeLink(m_opts.launchUrl);
    if (m_opts.screenshot.empty()) LaunchLink::registerScheme();   // the website's Play button opens us (not in tests)
    if (m_opts.page == "avatar") m_page = Page::Avatar;
    if (m_opts.page == "games") m_page = Page::Games;
    if (m_opts.page.rfind("game:", 0) == 0) { m_selected = std::atoi(m_opts.page.c_str() + 5); m_page = Page::GameInfo; }
    if (m_opts.page == "settings") m_showSettings = true;
    if (m_opts.page == "character") m_charPickOpen = true;   // test: the guest "Choose Your Character" box
    if (m_opts.page == "catalog") m_page = Page::Catalog;
    if (m_opts.page.rfind("profile:", 0) == 0) openProfile(m_opts.page.substr(8));   // test: someone's profile ("profile:2")
    if (m_opts.page == "bolts") m_page = Page::Bolts;
    if (m_opts.page == "create") { m_page = Page::Create; m_createKind = m_opts.createTab; }
    if (m_opts.page == "people") m_page = Page::People;
    if (m_opts.page == "groups") m_page = Page::Groups;
    if (m_opts.page == "forum") m_page = Page::Forum;   // (loads once online)
    if (m_opts.page.rfind("forum:", 0) == 0) { m_page = Page::Forum; m_forumThread = m_opts.page.substr(6); m_forumPage = -1; }   // test: a thread
    if (m_opts.page.rfind("board:", 0) == 0) { m_page = Page::Forum; m_forumBoard = m_opts.page.substr(6); }
    if (m_opts.page == "friends") m_page = Page::Friends;
    if (m_opts.page == "messages") m_page = Page::Messages;
    if (m_opts.page == "outfits") { m_page = Page::Avatar; m_avatarTab = 2; }
    if (m_opts.page == "login") { m_page = Page::Login; m_loginTab = 1; }
    if (m_opts.page.rfind("servers:", 0) == 0 && !m_games.empty()) {   // tests: a game's Servers window
        m_selected = std::clamp(std::atoi(m_opts.page.c_str() + 8), 0, (int)m_games.size() - 1);
        m_page = Page::GameInfo;
        m_autoServers = true;
    }
    if (m_opts.page.rfind("game:", 0) == 0 && !m_games.empty()) {   // tests: a game's page
        m_selected = std::clamp(std::atoi(m_opts.page.c_str() + 5), 0, (int)m_games.size() - 1);
        m_page = Page::GameInfo;
    }
    if (m_opts.page.rfind("group:", 0) == 0) { m_groupId = m_opts.page.substr(6); m_page = Page::Group; }
    if (m_opts.page.rfind("profile:", 0) == 0) { m_profileId = m_opts.page.substr(8); m_page = Page::Profile; }
    if (m_opts.page.rfind("item:", 0) == 0) { m_page = Page::Catalog; m_openItem = std::atoi(m_opts.page.c_str() + 5); }
    if (m_opts.page == "library") { m_page = Page::Create; m_createKind = kLibraryTab; }
    if (m_opts.page.rfind("asset:", 0) == 0) { m_page = Page::Create; m_createKind = kLibraryTab; }   // (opened once online, below)
    if (m_opts.page == "staff" && Account::iAmStaff()) m_page = Page::Staff;
    // (--online-play / --private-server / --join-code wait until we're online: see frame())
    // Everyone plays on the main server: a game file (Studio's "Play in Guts&BoltsPlayer")
    // goes into a public server once we're online. Only automated tests play offline / on LAN.
    if (!m_opts.game.empty() && !m_opts.onlinePlay && !m_opts.privateServer) {
        if (testMode()) joinGame(m_opts.game, m_opts.host ? HostMode::Lan : HostMode::Solo);
        else m_opts.onlinePlay = true;
    }
    if (!m_opts.join.empty() && testMode()) joinServer(m_opts.join);
    if (studioTest()) {
        joinServer(m_opts.studioTest);
        if (!m_client) {   // (couldn't even start connecting: nothing to show)
            Log::error("Couldn't join the multiplayer test: " + m_status);
            m_window->close();
        }
    }
}

PlayerApp::~PlayerApp() {
    Vr::stop();   // (while the window's OpenGL is still there)
    m_server.reset();
    m_client.reset();
    if (m_session) m_session->stop();
    m_session.reset();
    m_games.clear();
    m_avatarScene.reset();
    m_scene.reset();
    m_renderer.reset();
    m_view.resize(0, 0);
    for (Framebuffer& e : m_vrEyes) e.resize(0, 0);
    m_avatarView.resize(0, 0);
    m_window.reset();
}

void PlayerApp::run() {
    while (!m_window->shouldClose()) {
        ++m_frame;
        // Test helper: rename your first game from the Create page.
        if (!m_opts.testRename.empty() && m_page == Page::Create && m_frame == 30) {
            for (GameCard& g : m_games)
                if (!g.broken && g.info.author != "Guts and Bolts" && g.path.extension() == Paths::kExtension) {
                    std::printf("RENAME %s -> %s\n", g.info.title.c_str(), m_opts.testRename.c_str());
                    std::fflush(stdout);
                    renameGame(g.path, g.info.publishedId, m_opts.testRename);
                    break;
                }
        }
        // Test helper: publish a game file (like the Publish button on My Games).
        if (!m_opts.testPublish.empty() && m_frame == 60) {
            std::printf("PUBLISH %s (online %d)\n", m_opts.testPublish.c_str(), (int)Online::online());
            std::fflush(stdout);
            publishGameFile(m_opts.testPublish, std::string(), [](bool ok, const std::string& msg) {
                std::printf("PUBLISHED %d %s\n", (int)ok, msg.c_str());
                std::fflush(stdout);
            });
        }
        // Test helper: drive the tool hotbar, one step every 25 frames.
        if (!m_opts.testTools.empty() && m_page == Page::Game && m_frame > 40 && m_frame % 25 == 0) {
            size_t sp = m_opts.testTools.find(' ');
            std::string step = m_opts.testTools.substr(0, sp);
            m_opts.testTools = sp == std::string::npos ? "" : m_opts.testTools.substr(sp + 1);
            if (step.size() == 1 && step[0] >= '1' && step[0] <= '9') m_session->selectToolSlot(step[0] - '1');
            else if (step == "click") m_session->click(0);
            else if (step == "drop") m_session->dropTool();
            else if (step == "print" && m_scene->player()) {
                Player* p = m_scene->player();
                std::string names;
                for (SceneNode* t : p->tools()) names += (names.empty() ? "" : ",") + t->name;
                SceneNode* held = p->equippedTool();
                glm::vec3 at = p->position();
                std::printf("TOOLS held=%s slots=%s at=%.1f,%.1f,%.1f\n", held ? held->name.c_str() : "-", names.c_str(), at.x, at.y, at.z);
                for (const RemoteCharacter& rc : m_scene->remotes())   // (and what everyone else holds)
                    if (SceneNode* r = m_scene->findById(rc.rootId)) {
                        SceneNode* t = Player::heldTool(r);
                        std::printf("OTHER %s held=%s at=%.1f,%.1f,%.1f\n", r->name.c_str(), t ? t->name.c_str() : "-",
                                    r->transform.position.x, r->transform.position.y, r->transform.position.z);
                    }
                std::fflush(stdout);
            }
        }
        float dt = m_window->beginFrame([&] {
            float cx, cy;   // tests: three mouse clicks at a spot (down, then up two frames later)
            if (!m_opts.testClick.empty() && std::sscanf(m_opts.testClick.c_str(), "%f,%f", &cx, &cy) == 2) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(cx * io.DisplaySize.x, cy * io.DisplaySize.y);
                int t = m_frame - 50;
                if (t >= 0 && t < 30 && t % 10 == 0) io.AddMouseButtonEvent(0, true);
                if (t >= 0 && t < 30 && t % 10 == 2) io.AddMouseButtonEvent(0, false);
                // "x,y;x2,y2": then one more click somewhere else (e.g. outside a popup it opened).
                float c2x = 0, c2y = 0;
                size_t semi = m_opts.testClick.find(';');
                if (semi != std::string::npos && std::sscanf(m_opts.testClick.c_str() + semi + 1, "%f,%f", &c2x, &c2y) == 2 && t >= 60) {
                    io.AddMousePosEvent(c2x * io.DisplaySize.x, c2y * io.DisplaySize.y);
                    if (t == 62) io.AddMouseButtonEvent(0, true);
                    if (t == 64) io.AddMouseButtonEvent(0, false);
                }
            }
            if (!m_opts.testType.empty() && m_frame == 90) ImGui::GetIO().AddInputCharactersUTF8(m_opts.testType.c_str());
            if (!m_opts.testType.empty() && (m_frame == 94 || m_frame == 96)) ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, m_frame == 94);
            if (m_opts.testWheel != 0.0f && m_frame == 70) ImGui::GetIO().AddMouseWheelEvent(0.0f, m_opts.testWheel);
            if (!m_opts.holdKey.empty() && m_frame > 3) {
                ImGuiKey k = m_opts.holdKey == "Space" ? ImGuiKey_Space
                           : m_opts.holdKey == "Shift" ? ImGuiKey_LeftShift
                           : (ImGuiKey)(ImGuiKey_A + (m_opts.holdKey[0] - 'A'));
                ImGui::GetIO().AddKeyEvent(k, true);
            }
            // "--pad-test '40-200:LStickUp=1 60-62:FaceDown'": pretend a controller (frames, ImGuiKey_Gamepad* names).
            if (!m_opts.padTest.empty()) {
                std::istringstream parts(m_opts.padTest);
                std::string part;
                std::map<int, float> held;
                while (parts >> part) {
                    int from = 0, to = 0;
                    char name[64] = {};
                    float v = 1.0f;
                    if (std::sscanf(part.c_str(), "%d-%d:%63[A-Za-z0-9]=%f", &from, &to, name, &v) < 3) continue;
                    for (int k = ImGuiKey_GamepadStart; k <= ImGuiKey_GamepadRStickDown; ++k)
                        if (std::string("Gamepad") + name == ImGui::GetKeyName((ImGuiKey)k)) {
                            const bool on = m_frame >= from && m_frame <= to;
                            held[k] = std::max(held[k], on ? v : 0.0f);
                        }
                }
                for (auto& [k, v] : held) Pad::inject(k, v);
                Pad::injectApply();
            }
        });
#ifdef GB_QUEST
        // Quest: the app runs inside the headset, so it's always in VR.
        if (!m_vrOn && !m_vrAutoStarted) { m_vrAutoStarted = true; startVr(); }
#endif
        vrOverlay();   // (VR: the pointer and the headset keyboard, on top of everything)
        // Test helper: real (SDL) touch events, as a phone would send them.
        if (m_opts.touchTest.rfind("sdl-", 0) == 0 && m_frame > 40) {
            float k = std::min(1.0f, (m_frame - 40) / 6.0f);
            if (m_opts.touchTest == "sdl-stick") m_window->injectTouch(7, 0.12f, 0.80f - 0.08f * k, true);
            if (m_opts.touchTest == "sdl-jump")  m_window->injectTouch(8, 0.93f, 0.86f, ((m_frame - 40) / 15) % 2 == 0);
            // "sdl-tap:x,y" taps once; "sdl-swipe:x,y1,y2" drags a finger (0..1 screen coords).
            float a = 0, b = 0, c = 0;
            int t = m_frame - 40;
            if (std::sscanf(m_opts.touchTest.c_str(), "sdl-tap:%f,%f", &a, &b) == 2 && t <= 4)
                m_window->injectTouch(10, a, b, t < 3);
            if (std::sscanf(m_opts.touchTest.c_str(), "sdl-swipe:%f,%f,%f", &a, &b, &c) == 3 && t <= 21)
                m_window->injectTouch(11, a, b + (c - b) * std::min(1.0f, t / 20.0f), t < 21);
            if (m_opts.touchTest == "sdl-both") {
                m_window->injectTouch(7, 0.12f, 0.80f - 0.08f * k, true);
                m_window->injectTouch(9, 0.55f + 0.004f * (m_frame - 40), 0.4f, true);
            }
        }
        frame(dt);
        if (std::getenv("GB_TRACE_POS") && m_scene)   // test: the character's height every frame
            if (Player* p = m_scene->player()) std::printf("TRACE %d %.3f %.3f\n", m_frame, p->position().z, p->position().y);
        if (!m_opts.say.empty() && m_frame == 90 && m_page == Page::Game) sendChat(m_opts.say);
        if (!m_opts.devCommand.empty() && m_frame == 100 && m_page == Page::Game) {
            if (devServerAccess()) runDevCommand(m_opts.devCommand);
            else Log::info("Dev console: the Server tab isn't available to this player");
        }
        bool shoot = !m_opts.screenshot.empty() && m_frame == m_opts.frames;
        if (shoot && m_scene) {   // test output: where everyone's character ended up
            if (Player* p = m_scene->player()) {
                glm::vec3 q = p->position();
                std::printf("POS me %.2f %.2f %.2f\n", q.x, q.y, q.z);
            }
            Liquid& liquid = m_scene->water().liquid();
            const Liquid::Stats st = liquid.stats();
            if (st.count) {   // real liquid: how much, where and how fast
                std::printf("LIQUID %zu drops (%s), from %.1f %.1f %.1f to %.1f %.1f %.1f, fastest %.1f, %.1f ms\n", st.count,
                            liquid.backend(), st.lo.x, st.lo.y, st.lo.z, st.hi.x, st.hi.y, st.hi.z, st.fastest, liquid.lastStepMs());
                if (const char* dump = std::getenv("GB_LIQUID_DUMP"))   // every drop, for looking at in a script
                    if (FILE* f = std::fopen(dump, "w")) {
                        for (const glm::vec4& d : liquid.debugDrops()) std::fprintf(f, "%.2f %.2f %.2f %.2f\n", d.x, d.y, d.z, d.w);
                        std::fclose(f);
                    }
                const float* pr = m_scene->water().liquid().profile();
                std::printf("LIQUID ms: move %.1f, neighbours %.1f, solve %.1f, velocity %.1f, scan %.1f\n", pr[0], pr[1], pr[2], pr[3], pr[4]);
            }
            for (const RemoteCharacter& rc : m_scene->remotes())
                if (SceneNode* n = m_scene->findById(rc.rootId))
                    std::printf("POS %s %.2f %.2f %.2f\n", rc.name.c_str(), n->transform.position.x,
                                n->transform.position.y, n->transform.position.z);
            std::fflush(stdout);
        }
        m_window->endFrame(shoot ? m_opts.screenshot : std::string());
        if (shoot) m_window->close();
    }
}

// ---------------------------------------------------------------------------
// Games list
// ---------------------------------------------------------------------------

void PlayerApp::refreshGames() {
    m_games.clear();
    for (const auto& path : Paths::listGames()) {
        GameCard card;
        card.path = path;
        Scene preview;
        if (!Serializer::loadGameFile(preview, path.string())) {
            card.broken = true;
            card.info.title = path.stem().string();
            card.info.description = "This game file couldn't be read.";
        } else {
            card.info = preview.info();
            card.gore = preview.world().gore != GoreLevel::Off;
            card.ragdoll = preview.world().deathStyle == DeathStyle::Ragdoll;
            if (card.info.title.empty() || card.info.title == "My Game") card.info.title = path.stem().string();
            // Render a little picture of the game for its card.
            card.thumb = std::make_unique<Framebuffer>();
            card.thumb->resize(384, 216);
            Camera cam;
            cam.resize(384, 216);
            frameSpawn(preview, cam);
            m_renderer->render(preview, cam, *card.thumb, false);
        }
        m_games.push_back(std::move(card));
    }
}

void PlayerApp::joinGame(const std::filesystem::path& path, HostMode mode, const std::string& gameKey) {
    std::string err;
    if (!Serializer::loadGameFile(*m_scene, path.string(), &err)) {
        m_status = "Couldn't load " + path.filename().string() + (err.empty() ? "" : ": " + err);
        m_page = Page::Home;
        return;
    }
    // Taking over from a host who left: carry on in the world as it was.
    const bool carried = !m_continues.empty() && !m_carry.empty() && gameKey == m_carryGame &&
                         mode != HostMode::Lan && handoffRestore(*m_scene, m_carry);
    m_carry.clear();
    m_handoff = false;
    if (carried) Log::info("The host left: carrying on in the world as it was.");
    Online::fetchSounds(*m_scene);   // server audio ("gb:" sounds) this game uses
    m_iMadeThis = gameKey.empty() || (!m_gameOwner.empty() && m_gameOwner == Account::id());
    m_playingKey = gameKey;
    Profile& me = Profile::get();
    if (Player* p = m_scene->player()) {
        me.applyTo(*p);
        if (SceneNode* r = p->root()) r->name = Online::playerName();   // like Roblox: the character is named after you
        if (testMode() && !m_opts.testClothes.empty()) {
            // "shirt,pants" or "shirt,pants,tshirt" (picture files).
            std::vector<std::string> part;
            std::stringstream ss(m_opts.testClothes);
            for (std::string x; std::getline(ss, x, ',');) part.push_back(x);
            part.resize(3);
            p->setClothing(part[0], part[1], part[2]);
        }
        if (testMode() && !m_opts.testAccessory.empty()) p->setAccessories({{"hat", m_opts.testAccessory}});
        if (testMode() && !m_opts.testFace.empty()) p->setFace(m_opts.testFace);
    }
    m_session->scripts().setPlayerName(Online::playerName());
    // A published game's DataStores live on the Guts&Bolts server (if it trusts us with them).
    m_session->scripts().setOnlineData(!gameKey.empty(), gameKey);
    *m_soloChat = ChatLog{};

    m_currentTitle = m_scene->info().title;
    m_currentAuthor = m_scene->info().author;
    m_loadingT = carried ? 0.0f : 1.4f;
    if (gameKey != m_loadingGameId) {   // not the icon of the last game
        m_loadingGameId = gameKey; m_loadingIcon.clear(); m_loadingTitle.clear(); m_loadingAuthor.clear();
    }
    if (!m_loadingTitle.empty()) m_currentTitle = m_loadingTitle;    // a published game: its name on the site
    if (!m_loadingAuthor.empty()) m_currentAuthor = m_loadingAuthor;
    m_window->setTitle(m_currentTitle + " - Guts&Bolts Player");
    if (!carried) {   // (carrying on: the camera stays where it was)
        frameSpawn(*m_scene, m_camera);
        m_camera.clearScripted();    // (a script may have had it in the last game)
        m_camera.distance = 12.0f;
        m_camera.pitch = 20.0f;
        m_camera.yaw = 90.0f;        // behind the character, looking the way it faces (-Z)
        if (m_opts.cameraYaw > -999.0f) m_camera.yaw = m_opts.cameraYaw;
    }
    m_paused = false;
    m_status.clear();
    Log::clear();
    if (mode == HostMode::Lan) {
        m_server = std::make_unique<NetServer>(m_scene.get(), m_session.get());
        m_server->setOwner(gameKey.empty() ? Account::id() : m_gameOwner);
        std::string herr;
        if (!m_server->start(kDefaultPort, herr)) {
            m_status = "Couldn't host: " + herr;
            m_server.reset();
        }
    } else if (mode == HostMode::Public || mode == HostMode::Private) {
        // Host through the Guts&Bolts server: players join via the server, never by our address.
        std::string server, herr;
        int port = 0;
        if (Online::online() && Online::serverHostPort(server, port)) {
            m_server = std::make_unique<NetServer>(m_scene.get(), m_session.get());
            m_server->setOwner(gameKey.empty() ? Account::id() : m_gameOwner);   // local file: it's ours
            nlohmann::json args = {{"game", gameKey.empty() ? "local:" + path.stem().string() : gameKey},
                                   {"title", m_currentTitle}, {"private", mode == HostMode::Private}, {"max", 12}};
            if (!m_continues.empty()) args["continues"] = m_continues;   // taking over from a host who left
            m_tookOver = m_continues;
            m_tookOverUntil = ImGui::GetTime() + 20.0;
            m_continues.clear();
            nlohmann::json req = Online::signedRequest("relay.host", args);
            if (!m_server->startRelay(server, port, req.dump(), herr)) {
                m_status = "Couldn't start a server (" + herr + "), so you're playing alone.";
                m_server.reset();
            }
        }
    }
    // Teleported here? Then the game's scripts can read what we brought (player:GetJoinData()).
    ScriptEngine& se = m_session->scripts();
    se.clearJoinData();
    if (m_teleportData.is_object()) se.setJoinData(Online::playerName(), m_teleportData);
    m_teleportData = nullptr;
    m_session->start();
    m_page = Page::Game;
}

void PlayerApp::joinServer(const std::string& address) {
    std::string host = address;
    int port = kDefaultPort;
    size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        host = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
        if (port <= 0) port = kDefaultPort;
    }
    if (host.empty()) host = "127.0.0.1";
    m_client = std::make_unique<NetClient>(m_scene.get(), m_session.get());
    if (!m_client->connect(host, port)) {
        m_status = m_client->error();
        m_client.reset();
        return;
    }
    m_currentTitle = "Joining " + address + "...";
    m_currentAuthor.clear();
    m_loadingT = 0.9f;
    m_joinedOnce = false;
    m_paused = false;
    m_status.clear();
    Log::clear();
    m_page = Page::Game;
}

// Not connected: everything lives on the Guts&Bolts server, so wait for it here.
void PlayerApp::drawNoServer() {
    const Online::Status st = Online::status();
    const bool trying = st == Online::Status::Connecting || st == Online::Status::Off;
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(0, 60));
    auto centered = [&](const char* text, ImVec4 col, float scale) {
        ImGui::SetWindowFontScale(scale);
        ImVec2 ts = ImGui::CalcTextSize(text);
        ImGui::SetCursorPosX(std::max(0.0f, (w - ts.x) * 0.5f));
        ImGui::TextColored(col, "%s", text);
        ImGui::SetWindowFontScale(1.0f);
    };
    if (trying) {
        // A little spinner while we connect.
        ImVec2 c = ImGui::GetCursorScreenPos();
        c.x += w * 0.5f; c.y += 22;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float t = (float)ImGui::GetTime() * 5.0f;
        for (int i = 0; i < 8; ++i) {
            float a = t + i * 0.785f;
            dl->AddCircleFilled(ImVec2(c.x + std::cos(a) * 16, c.y + std::sin(a) * 16), 2.0f + i * 0.4f,
                                IM_COL32(29, 114, 210, 60 + i * 24));
        }
        ImGui::Dummy(ImVec2(0, 50));
        centered("Connecting to Guts&Bolts...", Classic::kInk, 1.4f);
    } else {
        centered("Can't reach Guts&Bolts", ImVec4(0.75f, 0.2f, 0.15f, 1), 1.4f);
        ImGui::Spacing();
        centered("Guts&Bolts needs an internet connection: games, friends and Bolts all live online.",
                 Classic::kInkDim, 1.0f);
        centered("Check your internet, then try again.", Classic::kInkDim, 1.0f);
        ImGui::Spacing();
        ImGui::SetCursorPosX(std::max(0.0f, (w - 160) * 0.5f));
        if (Classic::button("Try again", Classic::kBlue, ImVec2(160, 34))) Online::connect();
    }
}

ChatLog& PlayerApp::chat() {
    if (m_server) return m_server->chat();
    if (m_client) return m_client->chat();
    return *m_soloChat;
}

void PlayerApp::sendChat(const std::string& text) {
    if (text.empty()) return;
    if (text == "/devconsole" || text == "/console") { m_devConsole = true; return; }   // like Roblox
    // "/e dance" and friends: emotes, like old Roblox (they don't go into the chat).
    if (text.rfind("/e ", 0) == 0 || text.rfind("/emote ", 0) == 0) {
        std::string name = text.substr(text.find(' ') + 1);
        while (!name.empty() && name.back() == ' ') name.pop_back();
        Player* p = m_scene->player();
        if (!p || !p->playEmote(name)) {
            std::string help = "Emotes:";
            for (const Emotes::Emote& e : Emotes::builtIn()) help += " /e " + e.name + ",";
            for (const Emotes::Slot& e : Emotes::mine().slots)   // the ones you equipped from the catalog
                if (e.clip) help += " /e " + Emotes::normalize(e.name) + ",";
            chat().add("", help + " /e sit (stand still on the ground first). Or press B for the emote menu.", true);
        }
        return;
    }
    if (Online::isGuest() && Online::online()) { chat().add("", Online::kGuestChatText, true); return; }
    std::string to, msg;
    if (!m_server && !m_client && ChatLog::parseWhisper(text, to, msg)) {   // playing alone
        m_soloChat->add("", to.empty() || msg.empty() ? ChatLog::kWhisperHelp : "There's nobody else here to whisper to.", true);
        return;
    }
    if (m_server)      m_server->say(text);
    else if (m_client) m_client->say(text);
    else               m_soloChat->add(Online::playerName(), text, false, Account::iAmStaff(), Badges::iHave(Badges::Id::Verified));
}

// Off to another game: look it up first (if it's not a game we can play, we stay here),
// then leave and press Play on it, bringing the teleport data along.
void PlayerApp::teleportMe(const std::string& place, const nlohmann::json& data, const std::string& from) {
    if (m_teleporting || place.empty()) return;
    if (!Online::online()) { Log::warn("TeleportService: you need to be connected to Guts&Bolts to go to another game"); return; }
    m_teleporting = true;
    Log::info("Teleporting to game " + place + "...");
    Online::request("asset.info", {{"id", place}}, [this, place, data, from](const nlohmann::json& r) {
        m_teleporting = false;
        if (m_page != Page::Game) return;   // they left by themselves meanwhile
        const nlohmann::json a = r.value("asset", nlohmann::json::object());
        if (!r.value("ok", false)) { Log::warn("TeleportService: " + r.value("error", std::string("couldn't find that game"))); return; }
        if (a.value("kind", std::string()) != "game") { Log::warn("TeleportService: " + place + " isn't a game"); return; }
        const std::string title = a.value("name", std::string("a game"));
        leaveGame();
        m_teleportData = nlohmann::json{{"data", data}, {"from", from}};
        playGame(place, title, onlineStarter(place));
    });
}

void PlayerApp::leaveGame() {
    m_waitKey.clear();   // (stop waiting for a game server machine)
    m_carry.clear();
    m_handoff = false;
    m_playingKey.clear();
    m_server.reset();
    m_client.reset();
    Voice::setActive(false);
    Voice::forgetAll();
    m_talkOn = false;
#ifndef GB_QUEST
    stopVr();   // (Quest: you stay in VR, back at the games list)
#endif
    m_session->stop();
    m_session->setRole(GameSession::Role::Solo);
    m_scene->buildDefault();
    m_paused = false;
    m_page = Page::Home;
    m_window->setTitle("Guts&Bolts Player");
    if (studioTest()) {   // the test server closed (or we left): this window was only for the test
        if (!m_status.empty()) Log::warn(m_status);
        Log::system("Left the multiplayer test.");
        m_window->close();
    }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

// Accessories and faces you wear live on the server: fetch any we don't have yet, then
// put them on (in the game and on the avatar page).
void PlayerApp::fetchAvatarParts() {
    if (!Online::online()) return;
    const Profile& me = Profile::get();
    std::vector<std::string> want;
    for (const auto& [kind, src] : me.accessories) want.push_back(src);
    want.push_back(me.faceImage);
    want.push_back(me.body);
    want.push_back(me.shirtImage);   // (without these the character showed up in no clothes)
    want.push_back(me.pantsImage);
    want.push_back(me.tshirtImage);
    for (const std::string& src : want) {
        if (src.rfind("gb:", 0) != 0) continue;
        std::string id = src.substr(3);
        if (!Paths::downloaded(id).empty() || !m_avatarFetching.insert(id).second) continue;
        Online::download(id, [this](bool ok, const std::filesystem::path&, const nlohmann::json&) {
            if (!ok) return;
            if (Player* p = m_scene->player()) Profile::get().applyTo(*p);
            if (m_avatarScene) if (Player* p = m_avatarScene->player()) Profile::get().applyTo(*p);
        });
    }
}

void PlayerApp::frame(float dt) {
    Online::update();   // replies from the Guts&Bolts server
    if (!m_waitKey.empty() && ImGui::GetTime() >= m_waitAt) {   // a game server machine was starting it: ask again
        const std::string key = std::move(m_waitKey), title = std::move(m_waitTitle);
        m_waitKey.clear();
        if (m_waitPrivate) playPrivate(key, title, std::move(m_waitStart), m_waitCode, m_waitTries);
        else playGame(key, title, std::move(m_waitStart), m_waitTries);
    }
    fetchAvatarParts();
    if (m_autoServers && Online::online()) {
        m_autoServers = false;
        const GameCard& g = m_games[m_selected];
        openServers("local:" + g.path.stem().string(), g.info.title, localStarter(g.path));
    }
    if (Online::online() && m_opts.page.rfind("inventory:", 0) == 0) {   // test: someone's inventory ("inventory:2")
        const std::string id = m_opts.page.substr(10);
        m_opts.page.clear();
        openProfile(id);
        openInventory(id, "all");
    }
    if (Online::online() && m_opts.page.rfind("asset:", 0) == 0 && m_opts.testOps.empty()) {   // test: an asset's page ("asset:gb:decal-...")
        openAsset(m_opts.page.substr(6));
        m_opts.page.clear();
    }
    if (Online::online() && m_opts.page.rfind("online-servers:", 0) == 0) {   // test: an online game's Servers window ("online-servers:8")
        const std::string id = m_opts.page.substr(15);
        m_opts.page.clear();
        openServers(id, "Game " + id, onlineStarter(id));
    }
    if (Online::online() && m_opts.page.rfind("online:", 0) == 0 && m_opts.testOps.empty()) {   // test: an online game's popup ("online:7")
        Online::request("asset.info", {{"id", m_opts.page.substr(7)}}, [this](const nlohmann::json& r) {
            if (r.value("ok", false) && r.contains("asset")) { m_page = Page::Games; m_openGame = r["asset"]; m_openOnlineGame = 0; }
        });
        m_opts.page.clear();
    }
    if (Online::online() && !m_opts.testOps.empty() && !Online::pending()) {   // tests: one request at a time
        std::string t = m_opts.testOps.front();
        m_opts.testOps.erase(m_opts.testOps.begin());
        const size_t sp = t.find(' ');
        const std::string op = t.substr(0, sp);
        nlohmann::json args = sp == std::string::npos ? nlohmann::json::object() : nlohmann::json::parse(t.substr(sp + 1), nullptr, false);
        Online::request(op, args.is_object() ? args : nlohmann::json::object(), [op](const nlohmann::json& r) {
            std::printf("OP %s %s\n", op.c_str(), r.dump().substr(0, 8000).c_str());
            std::fflush(stdout);
        });
    }
    if (Online::online() && (!m_opts.testSignup.empty() || !m_opts.testLogin.empty())) {   // tests
        std::string& t = m_opts.testSignup.empty() ? m_opts.testLogin : m_opts.testSignup;
        bool signup = !m_opts.testSignup.empty();
        size_t colon = t.find(':');
        std::string user = t.substr(0, colon), pass = colon == std::string::npos ? "" : t.substr(colon + 1);
        t.clear();
        if (signup) { m_loginUser = user; signUp(user, pass); }
        else        { m_loginUser = user; m_loginTab = 1; logIn(user, pass); }
    }
#if defined(GB_MOBILE) || defined(__APPLE__)
    if (ImGui::GetTime() >= m_linkPollAt) {   // Android and Mac: a website Play link opened (or re-opened) the app
        m_linkPollAt = ImGui::GetTime() + 0.5;
        if (std::string link = LaunchLink::poll(); !link.empty()) takeLink(link);
    }
#endif
    followLink();
    updateItemRenders();   // item pictures (a couple per frame)
    syncEmotes();          // the emotes and animation pack you equipped, for games
    if (!m_autoStarted && Online::online()) {   // test options that need the server first
        if (!m_opts.onlineGame.empty()) {
            m_autoStarted = true;
            playGame(m_opts.onlineGame, "", onlineStarter(m_opts.onlineGame));
        } else if (m_opts.onlinePlay && !m_opts.game.empty()) {
            m_autoStarted = true;
            std::filesystem::path path = m_opts.game;
            playGame("local:" + path.stem().string(), path.stem().string(), localStarter(path));
        } else if (m_opts.privateServer && !m_opts.game.empty()) {
            m_autoStarted = true;
            std::filesystem::path path = m_opts.game;
            joinGame(path, HostMode::Private);
        } else if (!m_opts.joinCode.empty()) {
            m_autoStarted = true;
            joinRelay("", m_opts.joinCode, "");
        }
    }
    m_window->lockLandscape(m_page == Page::Game);   // phones: games are landscape, the site isn't
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                             ImGuiWindowFlags_NoDocking;
    if (m_page == Page::Game) flags |= ImGuiWindowFlags_NoNav;   // a controller plays the game, not the Menu button
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    // VR: no background (the headset's floating screen shows just the menus and buttons).
    const bool clearHost = m_vrOn && m_page == Page::Game;
    if (clearHost) ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin("##PlayerHost", nullptr, flags);
    if (clearHost) ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);

    Classic::seasonal(GraphicsSettings::get().seasonTheme && Classic::isOctober());
    if (m_page == Page::Game) {
        drawGame(dt);
    } else {
        // Sky background with the site in a column down the middle.
        ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilledMultiColor(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                                    Classic::kSkyTop, Classic::kSkyTop, Classic::kSkyBottom, Classic::kSkyBottom);
        // Phones: thin margins, so the page gets as much of the small screen as it can.
        const bool smallScreen = size.x < 700.0f || size.y < 520.0f;
        const float gutter = smallScreen ? 6.0f : 16.0f;
        float width = std::min(1000.0f, size.x - gutter * 2.0f);
        ImVec2 col(pos.x + (size.x - width) * 0.5f, pos.y + (smallScreen ? 4.0f : 10.0f));
        drawTopBar(col, width);

        float top = ImGui::GetCursorScreenPos().y;
        ImGui::SetCursorScreenPos(ImVec2(col.x, top));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, smallScreen ? ImVec2(10, 8) : ImVec2(18, 14));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0.88f, 0.9f, 0.93f, 1));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.62f, 0.66f, 0.72f, 1));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.5f, 0.56f, 0.66f, 1));
        ImGui::BeginChild("##content", ImVec2(width, pos.y + size.y - top - (smallScreen ? 4 : 10)), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImVec2 cpos = ImGui::GetWindowPos(), csize = ImGui::GetWindowSize();
        Classic::stripes(ImGui::GetWindowDrawList(), cpos, ImVec2(cpos.x + csize.x, cpos.y + csize.y));
        Classic::pushLight();
        // Esc / Android Back: go back to the home page.
        if (m_page != Page::Home && !ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            m_page = Page::Home;
        ImGui::PushTextWrapPos(0.0f);   // long lines wrap at the page edge (small phone screens)
        if (!Online::online() && !testMode()) drawNoServer();   // no offline play
        else if (needsLogin()) drawLogin();   // online but not signed up: that comes first
        else switch (m_page) {
            case Page::Home:     drawHome(); break;
            case Page::Games:    drawGames(); break;
            case Page::Avatar:   drawAvatar(dt); break;
            case Page::GameInfo: drawGameInfo(); break;
            case Page::Catalog:  drawCatalog(); break;
            case Page::Staff:    drawStaff(); break;
            case Page::Bolts:    drawBolts(); break;
            case Page::Create:   drawCreate(); break;
            case Page::People:   drawPeople(); break;
            case Page::Profile:  drawProfile(); break;
            case Page::Groups:   drawGroups(); break;
            case Page::Group:    drawGroup(); break;
            case Page::Forum:    drawForum(); break;
            case Page::Friends:  drawFriends(); break;
            case Page::Login:    drawLogin(); break;
            case Page::Messages: drawMessages(); break;
            default: break;
        }
        ImGui::PopTextWrapPos();
        Classic::popLight();
        ImGui::EndChild();
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    drawCharacterPicker();
    ImGui::End();
    drawConnectScreen();
    if (m_page != Page::Game) touchScroll();
    if (m_showSettings) {   // dressed like the rest of the site: white box, blue title bar
        Classic::pushLight();
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.97f, 0.97f, 0.98f, 1));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(1, 1, 1, 1));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, Classic::kTitle);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.10f, 0.45f, 0.82f, 1));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.86f, 0.91f, 0.98f, 1));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.80f, 0.88f, 0.98f, 1));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, Classic::kBlue);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Classic::kLink);
        SettingsWindow::draw(&m_showSettings);
        ImGui::PopStyleColor(8);
        Classic::popLight();
    }
    // The site's dialogs: white boxes with dark text, like its pages.
    Classic::pushLight();
    drawJoinDialog();
    drawServersDialog();
    drawItemDialog();
    drawOnlineItemDialog();
    drawOnlineGameDialog();
    drawNotice();
    drawReportDialog();
    Classic::popLight();
    if (m_page != Page::Game && UpdateToast::draw("GutsAndBoltsPlayer")) m_window->close();
    drawModeration();   // last: the ban screen covers everything
}

void PlayerApp::drawJoinDialog() {
    if (m_showJoin) { ImGui::OpenPopup("Join on local network"); m_showJoin = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(420), 0));
    if (ImGui::BeginPopupModal("Join on local network", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextWrapped("For people on the same Wi-Fi only. Your friend opens a game, clicks Create a server, "
                           "then Local network, and tells you the address it shows (like 192.168.1.20). "
                           "Online, use your Friends list instead: it never shares anyone's address.");
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        bool enter = ImGui::InputTextWithHint("##addr", "address, e.g. 192.168.1.20 or host:7777",
                                              &m_joinAddress, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        if (bigButton("Join", kGreen, ImVec2(120, 34)) || enter) {
            ImGui::CloseCurrentPopup();
            joinServer(m_joinAddress);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 34))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void PlayerApp::drawTopBar(ImVec2 pos, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Profile& me = Profile::get();

    // --- Banner: your avatar standing in the sky, with the logo ---
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const bool portrait = screen.y > screen.x;
    const bool shortScreen = screen.y < 520.0f;   // a phone on its side: every pixel of height counts
#ifdef GB_MOBILE
    const float bannerH = portrait ? 64.0f : (shortScreen ? 54.0f : 72.0f);   // phones: keep the banner slim
    const float logoSize = portrait ? 24.0f : (shortScreen ? 30.0f : 40.0f);
#else
    const float bannerH = shortScreen ? 60.0f : 118.0f;
    const float logoSize = shortScreen ? 34.0f : 64.0f;
#endif
    const float fb = ImGui::GetIO().DisplayFramebufferScale.x;   // > 1 on phones: draw with every real pixel
    m_bannerView.resize((int)(width * fb), (int)(bannerH * fb));
    Camera cam;
    cam.resize((int)(width * fb), (int)(bannerH * fb));
    cam.fov = 30.0f;
    cam.pivot = {-5.2f, 1.45f, 0.0f};      // look left of the avatar so it stands on the right
    cam.yaw = 90.0f;
    cam.pitch = 3.0f;
    cam.distance = 10.5f;
    m_renderer->render(*m_avatarScene, cam, m_bannerView, false);
    ImVec2 b0 = pos, b1(pos.x + width, pos.y + bannerH);
    dl->AddImageRounded((ImTextureID)(intptr_t)m_bannerView.colorTexture(), b0, b1, ImVec2(0, 1), ImVec2(1, 0),
                        IM_COL32_WHITE, 8.0f, ImDrawFlags_RoundCornersTop);
    Classic::logo(dl, ImVec2(pos.x + 26, pos.y + (bannerH - logoSize) * 0.5f - 4), logoSize, "GUTS&BOLTS");
    if (Classic::gutstober) {   // Gutstober: a pumpkin after the logo and bats flapping across the banner
        const float lw = ImGui::GetFont()->CalcTextSizeA(logoSize, FLT_MAX, 0, "GUTS&BOLTS").x;
        Classic::pumpkin(dl, ImVec2(pos.x + 26 + lw + logoSize * 0.6f, pos.y + bannerH * 0.5f - 2), logoSize * 0.33f);
        const float tt = (float)ImGui::GetTime();
        for (int k = 0; k < 3; ++k) {
            float x = std::fmod(tt * (22.0f + k * 9.0f) + k * 260.0f, width + 80.0f) - 40.0f;
            Classic::bat(dl, ImVec2(pos.x + x, pos.y + 14 + k * 9 + std::sin(tt * 1.7f + k) * 5), 9.0f + k * 2.0f, tt + k);
        }
    }
#ifndef GB_MOBILE
    drawServerButton(ImVec2(pos.x + 26, b1.y - 30));   // online / offline (phones: in the nav bar)
#endif

    // Account box, top-right of the banner: name, your Bolts and a link to the avatar editor.
    const bool guest = Online::online() && Online::isGuest();
    std::string hi = "Hi, " + (guest ? Online::guestName() : me.name);
    ImVec2 ts = ImGui::CalcTextSize(hi.c_str());
    const bool staff = Account::iAmStaff();
    const bool verifiedMe = Badges::iHave(Badges::Id::Verified);
    float badgeW = (staff ? 24.0f : 0.0f) + (verifiedMe ? 20.0f : 0.0f);
    std::string boltsText = Bolts::format(Online::online() ? Online::bolts() : Bolts::balance());
    float boltsW = 18 + 4 + ImGui::CalcTextSize(boltsText.c_str()).x;
    float line2 = boltsW + 14 + ImGui::CalcTextSize(Online::online() && Online::isGuest() ? "Sign up" : "Edit avatar").x;
    float boxW = std::max(ts.x + badgeW, line2) + 24;
    const float boxY = pos.y + std::min(10.0f, std::max(2.0f, (bannerH - 50.0f) * 0.5f));
    ImVec2 a(b1.x - boxW - (shortScreen ? 6 : 10), boxY), c(b1.x - (shortScreen ? 6 : 10), boxY + 50);
    dl->AddRectFilled(a, c, IM_COL32(255, 255, 255, 215), 5.0f);
    dl->AddRect(a, c, IM_COL32(120, 140, 170, 255), 5.0f);
    drawBell(ImVec2(a.x - 22, a.y + 25));
    if (staff) Badges::drawIcon(dl, ImVec2(a.x + 22, a.y + 15), 20.0f, Badges::Id::Administrator);
    float nameX = a.x + 12 + (staff ? 24.0f : 0.0f);
    dl->AddText(ImVec2(nameX, a.y + 7), IM_COL32(30, 30, 40, 255), hi.c_str());
    if (verifiedMe) Badges::drawCheck(dl, ImVec2(nameX + ts.x + 10, a.y + 7 + ImGui::GetFontSize() * 0.5f), 15.0f);
    // Your Bolts (click to open the Bolts page).
    ImGui::SetCursorScreenPos(ImVec2(a.x + 10, a.y + 26));
    if (ImGui::InvisibleButton("##boltsbox", ImVec2(boltsW + 4, 20))) m_page = Page::Bolts;
    bool boltsHover = ImGui::IsItemHovered();
    if (boltsHover) {
        dl->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32(255, 200, 60, 70), 3.0f);
        ImGui::SetTooltip("Your Bolts. Click to earn more!");
    }
    Bolts::drawIcon(dl, ImVec2(a.x + 21, a.y + 36), 18.0f);
    dl->AddText(ImVec2(a.x + 33, a.y + 29), IM_COL32(140, 90, 0, 255), boltsText.c_str());
    ImGui::SetCursorScreenPos(ImVec2(a.x + 12 + boltsW + 14, a.y + 29));
    ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    if (guest) { if (ImGui::SmallButton("Sign up")) { m_page = Page::Login; m_loginTab = 0; } }
    else if (ImGui::SmallButton("Edit avatar")) m_page = Page::Avatar;
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    // --- The blue nav bar (wraps onto a second row on narrow, portrait screens) ---
    const float navH = shortScreen ? 28.0f : 34.0f;
    const float navGap = portrait ? 16.0f : (shortScreen ? 20.0f : 26.0f);
    struct Item { const char* label; int action; };
    // "Friends (2)" when friend requests are waiting.
    static std::string friendsLabel;
    size_t waiting = m_friends.contains("incoming") ? m_friends["incoming"].size() : 0;
    friendsLabel = waiting ? "Friends (" + std::to_string(waiting) + ")" : std::string("Friends");
    static std::string messagesLabel;   // "Messages (2)" when there are unread ones
    const int unread = unreadMessages();
    messagesLabel = unread ? "Messages (" + std::to_string(unread) + ")" : std::string("Messages");
    std::vector<Item> items = {{"Home", 0}, {"Games", 1}, {"Catalog", 6}, {"Bolts", 8}, {"Create", 9}, {"Friends", 3}, {"Messages", 13}, {"People", 11}, {"Groups", 12}, {"Forum", 14}, {"Avatar", 2},
                               {"Develop", 4}, {"Settings", 5}};
    if (Badges::canVerify()) items.push_back({"Staff", 7});
#ifdef GB_MOBILE
    items.push_back({Online::online() ? "Online" : "Offline", 10});
#endif
#ifdef GB_MOBILE
    // No Studio on phones.
    items.erase(std::remove_if(items.begin(), items.end(), [](const Item& i) { return i.action == 4; }), items.end());
#endif
    // Lay the items out in rows first, so the bar knows how tall to be.
    std::vector<ImVec2> at;
    {
        float x = pos.x + (portrait ? 10 : 14), row = 0;
        for (const Item& it : items) {
            float w = ImGui::CalcTextSize(it.action == 3 ? friendsLabel.c_str() : it.action == 13 ? messagesLabel.c_str() : it.label).x;
            if (x + w + 4 > pos.x + width && x > pos.x + 14) { x = pos.x + (portrait ? 10 : 14); row += navH; }
            at.push_back(ImVec2(x, row));
            x += w + navGap;
        }
    }
    float rows = (at.empty() ? 0 : at.back().y) + navH;
    ImVec2 n0(pos.x, b1.y), n1(pos.x + width, b1.y + rows);
    dl->AddRectFilledMultiColor(n0, n1, Classic::kNavTop, Classic::kNavTop, Classic::kNavBottom, Classic::kNavBottom);
    dl->AddLine(ImVec2(n0.x, n1.y - 1), ImVec2(n1.x, n1.y - 1), Classic::kNavLine);
    for (size_t k = 0; k < items.size(); ++k) {
        Item it = items[k];
        if (it.action == 3) it.label = friendsLabel.c_str();
        if (it.action == 13) it.label = messagesLabel.c_str();
        float x = at[k].x;
        float rowY = n0.y + at[k].y;
        ImVec2 sz = ImGui::CalcTextSize(it.label);
        ImVec2 p0(x - navGap * 0.3f, rowY), p1(x + sz.x + navGap * 0.3f, rowY + navH);
        ImGui::SetCursorScreenPos(p0);
        ImGui::PushID(it.action);
        bool clicked = ImGui::InvisibleButton("##nav", ImVec2(p1.x - p0.x, navH));
        ImGui::PopID();
        bool active = (it.action == 0 && m_page == Page::Home) || (it.action == 1 && m_page == Page::Games) ||
                      (it.action == 2 && m_page == Page::Avatar) || (it.action == 6 && m_page == Page::Catalog) ||
                      (it.action == 7 && m_page == Page::Staff) || (it.action == 8 && m_page == Page::Bolts) ||
                      (it.action == 9 && m_page == Page::Create) || (it.action == 3 && m_page == Page::Friends) ||
                      (it.action == 11 && (m_page == Page::People || m_page == Page::Profile)) ||
                      (it.action == 12 && (m_page == Page::Groups || m_page == Page::Group)) ||
                      (it.action == 13 && m_page == Page::Messages) || (it.action == 14 && m_page == Page::Forum);
        if (ImGui::IsItemHovered() || active)
            dl->AddRectFilled(p0, p1, IM_COL32(255, 255, 255, active ? 60 : 35));
        dl->AddText(ImVec2(x + 1, rowY + (navH - sz.y) * 0.5f + 1), IM_COL32(0, 30, 80, 180), it.label);
        dl->AddText(ImVec2(x, rowY + (navH - sz.y) * 0.5f), IM_COL32(255, 255, 255, 255), it.label);
        if (it.action == 13 && unread) dl->AddCircleFilled(ImVec2(x + sz.x + 5, rowY + navH * 0.3f), 3.5f, IM_COL32(230, 30, 30, 255));
        if (clicked) {
            switch (it.action) {
                case 0: m_page = Page::Home; m_loaded.clear(); break;
                case 1: m_page = Page::Games; m_category = "all"; break;
                case 2: m_page = Page::Avatar; break;
                case 3: m_page = Page::Friends; m_friendsAt = -100.0; m_friendMsg.clear(); break;
                case 4:
                    if (!Paths::launch(Paths::sibling("GutsAndBolts")))
                        m_status = "Couldn't find the Guts and Bolts editor next to this app.";
                    break;
                case 5: m_showSettings = true; break;
                case 6: m_page = Page::Catalog; m_items = Catalog::load(); m_loaded.clear(); break;
                case 7: m_page = Page::Staff; break;
                case 8: m_page = Page::Bolts; m_loaded.clear(); break;
                case 9: m_page = Page::Create; m_loaded.clear(); break;
                case 11: m_page = Page::People; m_socialMsg.clear(); m_loaded.clear(); break;
                case 12: m_page = Page::Groups; m_socialMsg.clear(); m_loaded.clear(); break;
                case 14: m_loaded.clear(); openForum(""); break;
                case 13: m_page = Page::Messages; if (m_msgBox == "new") m_msgBox = "inbox"; m_messagesAt = -100.0; m_msgStatus.clear(); break;
                case 10: if (!Online::online()) Online::connect(); break;
            }
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(pos.x, n1.y));
}

bool PlayerApp::drawTile(int index) {
    GameCard& g = m_games[index];
    const float w = 150.0f, h = 112.0f;
    ImGui::PushID(index);
    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##tile", ImVec2(w, h));
    bool hover = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g.thumb)   // crop the 16:9 picture to a 4:3 tile
        dl->AddImage((ImTextureID)(intptr_t)g.thumb->colorTexture(), p, ImVec2(p.x + w, p.y + h),
                     ImVec2(0.125f, 1), ImVec2(0.875f, 0));
    else
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(120, 40, 40, 255));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(160, 165, 175, 255),
                0.0f, 0, hover ? 2.0f : 1.0f);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
    std::string title = g.info.title.size() > 34 ? g.info.title.substr(0, 32) + "..." : g.info.title;
    ImGui::TextColored(Classic::kLink, "%s", title.c_str());
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled("by %s", g.info.author.c_str());
    ImGui::Dummy(ImVec2(w, 0));
    ImGui::EndGroup();
    if (hover && !g.info.description.empty() && !m_window->hasTouchScreen()) {   // no hovering on phones
        ImGui::SetTooltip("%s", g.info.description.c_str());
    }
    ImGui::PopID();
    if (clicked) { m_selected = index; m_page = Page::GameInfo; }
    return clicked;
}

void PlayerApp::drawRow(const char* title, const std::vector<int>& games, const char* seeAll) {
    if (games.empty()) return;
    ImGui::SetWindowFontScale(1.25f);
    ImGui::TextUnformatted(title);
    ImGui::SetWindowFontScale(1.0f);
    if (seeAll) {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 70);
        ImGui::PushID(title);
        if (Classic::button("See All", Classic::kBlue, ImVec2(70, 0))) { m_page = Page::Games; m_category = seeAll; }
        ImGui::PopID();
    }
    float avail = ImGui::GetContentRegionAvail().x;
    int fit = std::max(1, (int)((avail + 14) / (150 + 14)));
    // (The same game can be in several rows: each row gets its own IDs, or hovering
    // it shows "conflicting ID" errors.)
    ImGui::PushID(title);
    for (int i = 0; i < (int)games.size() && i < fit; ++i) {
        if (i > 0) ImGui::SameLine(0, 14);
        drawTile(games[i]);
    }
    ImGui::PopID();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void PlayerApp::drawHome() {
    Profile& me = Profile::get();
    if (!m_status.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_status.c_str());
        ImGui::Spacing();
    }
    // Signed up but no password yet (like an account made before passwords existed):
    // you can't log in anywhere else, including the website, until you set one.
    if (Online::online() && Online::me().value("userId", 0LL) > 0 && !Online::me().value("hasPassword", true)) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 0.95f, 0.8f, 1));
        ImGui::BeginChild("##nopw", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(ImVec4(0.55f, 0.35f, 0.0f, 1), "Your account has no password yet.");
        ImGui::TextWrapped("Set one so you can log in on the website and other devices as @%s.",
                           Online::me().value("username", std::string()).c_str());
        if (Classic::button("Set a password", Classic::kBlue)) {
            m_page = Page::Login;
            m_loginTab = 0;
            m_loginUser = Online::me().value("username", std::string());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    // Online: games people published.
    drawFeed();
    drawOnlineGames();
    // Daily Bolts waiting for you?
    if (Online::online() ? Online::me().value("canDaily", false) : Bolts::canClaimDaily()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        const bool narrow = w < 560;   // phones held upright: the button goes under the text
        float h = narrow ? 84.0f : 46.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 244, 205, 255), 6.0f);
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(225, 180, 60, 255), 6.0f);
        Bolts::drawIcon(dl, ImVec2(p.x + 26, p.y + 23), 30.0f);
        ImGui::SetCursorScreenPos(ImVec2(p.x + 50, p.y + 6));
        ImGui::TextColored(ImVec4(0.45f, 0.3f, 0.0f, 1), "Your daily Bolts are ready!");
        ImGui::SetCursorScreenPos(ImVec2(p.x + 50, p.y + 24));
        if (!narrow) ImGui::TextDisabled("Claim %lld Bolts every day. Spend them in the Catalog.", Bolts::kDaily);
        ImGui::SetCursorScreenPos(narrow ? ImVec2(p.x + 50, p.y + 44) : ImVec2(p.x + w - 130, p.y + 8));
        if (Classic::button(narrow ? ("Claim " + std::to_string(Bolts::kDaily) + " Bolts").c_str() : "Claim", Classic::kPlay,
                            ImVec2(narrow ? 170.0f : 120.0f, 30))) {
            if (Online::online())
                Online::request("bolts.daily", nlohmann::json::object(), [this](const nlohmann::json& r) {
                    m_boltsMsg = r.value("ok", false) ? "You got 25 Bolts! Come back tomorrow for more." : r.value("error", std::string());
                });
            else Bolts::claimDaily(m_boltsMsg);
            m_page = Page::Bolts;
            m_loaded.clear();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + 10));
    }
    if (m_games.empty()) {
        ImGui::TextWrapped("No games yet! Click Develop to open Guts and Bolts Studio, build something and "
                           "save it - it will show up here.");
        ImGui::TextDisabled("Games folder: %s", Paths::gamesFolder().string().c_str());
        return;
    }

    std::vector<int> all, recent, carnage, classic;
    for (int i = 0; i < (int)m_games.size(); ++i) {
        all.push_back(i);
        (m_games[i].gore ? carnage : classic).push_back(i);
    }
    for (const auto& name : me.recent)
        for (int i = 0; i < (int)m_games.size(); ++i)
            if (m_games[i].path.filename().string() == name) recent.push_back(i);

    drawRow("Popular", all, "all");
    drawRow("Recently Played", recent, "recent");
    drawRow("Carnage (ragdolls & gore)", carnage, "carnage");
    drawRow("Classic", classic, "classic");

    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Tip: press Play to jump into a public server, or open a game and click Create a server to play privately with friends.");
    ImGui::PopTextWrapPos();
}

void PlayerApp::drawGames() {
    const char* names[] = {"all", "recent", "carnage", "classic"};
    const char* titles[] = {"All Games", "Recently Played", "Carnage", "Classic"};
    int cur = 0;
    for (int i = 0; i < 4; ++i) if (m_category == names[i]) cur = i;

    ImGui::SetWindowFontScale(1.35f);
    ImGui::TextUnformatted(titles[cur]);
    ImGui::SetWindowFontScale(1.0f);
    const float refreshW = ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::min(170.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::Combo("##cat", &cur, titles, 4)) m_category = names[cur];
    if (!portraitScreen()) ImGui::SameLine();   // phones: search goes on its own line
    ImGui::SetNextItemWidth(std::max(80.0f, std::min(260.0f, ImGui::GetContentRegionAvail().x - refreshW)));
    ImGui::InputTextWithHint("##search", "Search", &m_search);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refreshGames();
    ImGui::Spacing();

    std::vector<int> list;
    std::string q = lower(m_search);
    auto matches = [&](int i) {
        const GameCard& g = m_games[i];
        if (!q.empty() && lower(g.info.title).find(q) == std::string::npos &&
            lower(g.info.author).find(q) == std::string::npos) return false;
        if (cur == 2) return g.gore;
        if (cur == 3) return !g.gore;
        return true;
    };
    if (cur == 1) {
        for (const auto& name : Profile::get().recent)
            for (int i = 0; i < (int)m_games.size(); ++i)
                if (m_games[i].path.filename().string() == name && matches(i)) list.push_back(i);
    } else {
        for (int i = 0; i < (int)m_games.size(); ++i) if (matches(i)) list.push_back(i);
    }
    if (list.empty()) ImGui::TextDisabled("Nothing here yet.");

    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, (int)((avail + 14) / (150 + 14)));
    for (size_t k = 0; k < list.size(); ++k) {
        if (k % perRow != 0) ImGui::SameLine(0, 14);
        else if (k > 0) ImGui::Spacing();
        drawTile(list[k]);
    }
}

void PlayerApp::drawGameInfo() {
    if (m_selected < 0 || m_selected >= (int)m_games.size()) { m_page = Page::Home; return; }
    GameCard& g = m_games[m_selected];

    if (ImGui::SmallButton("< Back")) m_page = Page::Home;
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(g.info.title.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("by %s", g.info.author.c_str());
    ImGui::Spacing();

    const bool tall = portraitScreen();
    float picW = tall ? ImGui::GetContentRegionAvail().x : std::min(560.0f, ImGui::GetContentRegionAvail().x * 0.6f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(picW, picW * 9.0f / 16.0f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g.thumb)
        dl->AddImage((ImTextureID)(intptr_t)g.thumb->colorTexture(), p,
                     ImVec2(p.x + picW, p.y + picW * 9.0f / 16.0f), ImVec2(0, 1), ImVec2(1, 0));
    dl->AddRect(p, ImVec2(p.x + picW, p.y + picW * 9.0f / 16.0f), IM_COL32(140, 150, 165, 255));

    if (tall) ImGui::Spacing();   // phones held upright: buttons go under the picture
    else      ImGui::SameLine(0, 20);
    ImGui::BeginGroup();
    ImGui::BeginDisabled(g.broken);
    const std::string key = "local:" + g.path.stem().string();
    ImGui::BeginDisabled(m_busy);
    if (Classic::button(m_busy ? "Finding a server..." : "Play", Classic::kPlay, ImVec2(220, 56)))
        playGame(key, g.info.title, localStarter(g.path));
    ImGui::EndDisabled();
    ImGui::Spacing();
    if (Classic::button("Create a server", Classic::kBlue, ImVec2(220, 34))) openServers(key, g.info.title, localStarter(g.path));
    ImGui::EndDisabled();
    ImGui::TextDisabled("Play puts you in a public server.");
    ImGui::Spacing();
    ImGui::TextDisabled("Death: %s", g.ragdoll ? "Ragdoll" : "Classic");
    ImGui::TextDisabled("Gore: %s", g.gore ? "Yes" : "No");
    if (g.gore && !GraphicsSettings::get().allowGore)
        ImGui::TextDisabled("(hidden - you turned gore off)");
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::SeparatorText("Description");
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(g.info.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    drawServerCards(key, g.info.title);
}

namespace {
// A round headshot like Roblox's server cards: the avatar's head colour, a face and its hat.
void drawHeadshot(ImDrawList* dl, ImVec2 c, float r, const nlohmann::json& av) {
    auto col = [&](const char* k, ImU32 fallback) {
        if (!av.is_object() || !av.contains(k) || !av[k].is_array() || av[k].size() < 3 || !av[k][0].is_number()) return fallback;
        int R = av[k][0].get<int>(), G = av[k][1].get<int>(), B = av[k][2].get<int>();
        if (R < 0) return fallback;
        return IM_COL32(std::clamp(R, 0, 255), std::clamp(G, 0, 255), std::clamp(B, 0, 255), 255);
    };
    dl->AddCircleFilled(c, r, IM_COL32(200, 207, 217, 255), 32);
    ImU32 skin = col("head", IM_COL32(245, 205, 48, 255));
    float h = r * 0.62f;
    dl->AddRectFilled(ImVec2(c.x - h, c.y - h * 0.75f), ImVec2(c.x + h, c.y + h * 1.05f), skin, h * 0.3f);
    dl->AddCircleFilled(ImVec2(c.x - h * 0.33f, c.y), h * 0.1f, IM_COL32(20, 20, 20, 255));
    dl->AddCircleFilled(ImVec2(c.x + h * 0.33f, c.y), h * 0.1f, IM_COL32(20, 20, 20, 255));
    dl->PathArcTo(ImVec2(c.x, c.y + h * 0.28f), h * 0.35f, 0.35f, 2.8f);
    dl->PathStroke(IM_COL32(20, 20, 20, 255), 0, std::max(1.2f, r * 0.05f));
    int hat = av.is_object() ? av.value("hat", 0) : 0;
    if (hat > 0) {
        ImU32 hc = col("hatColor", IM_COL32(30, 30, 34, 255));
        dl->AddRectFilled(ImVec2(c.x - h * 1.05f, c.y - h * 1.05f), ImVec2(c.x + h * 1.05f, c.y - h * 0.62f), hc, h * 0.3f);
    }
}
} // namespace

void PlayerApp::drawServerCards(const std::string& gameKey, const std::string& title) {
    drawGamePasses(gameKey);
    ImGui::SeparatorText("Servers");
    if (!Online::online()) return;
    if (gameKey != m_gameServersKey || ImGui::GetTime() - m_gameServersAt > 10.0) {   // keep it fresh
        if (gameKey != m_gameServersKey) { m_gameServers = nlohmann::json::array(); m_gameServersPage = 0; }
        m_gameServersKey = gameKey;
        m_gameServersAt = ImGui::GetTime();
        Online::request("servers.list", {{"game", gameKey}}, [this, gameKey](const nlohmann::json& r) {
            if (gameKey == m_gameServersKey && r.value("ok", false)) m_gameServers = r["servers"];
        });
    }
    if (m_gameServers.empty()) { ImGui::TextDisabled("Nobody's playing right now. Be the first!"); return; }

    const float cardW = 200.0f, cardH = 250.0f, gap = 12.0f, face = 50.0f;
    const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cardW + gap)));
    const int perPage = perRow * 2;
    const int pages = std::max(1, ((int)m_gameServers.size() + perPage - 1) / perPage);
    m_gameServersPage = std::clamp(m_gameServersPage, 0, pages - 1);
    const int first = m_gameServersPage * perPage, last = std::min((int)m_gameServers.size(), first + perPage);
    for (int i = first; i < last; ++i) {
        const nlohmann::json& sv = m_gameServers[i];
        if ((i - first) % perRow) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + cardW, p.y + cardH), IM_COL32(40, 43, 50, 255), 6.0f);
        // Faces: up to 5, then "+N".
        const nlohmann::json people = sv.value("people", nlohmann::json::array());
        const int players = sv.value("players", 1), max = sv.value("max", 12);
        int shown = std::min<int>((int)people.size(), 5), more = players - shown;
        for (int k = 0; k < shown + (more > 0 ? 1 : 0); ++k) {
            ImVec2 c(p.x + 14 + face * 0.5f + (k % 2) * (face + 8), p.y + 12 + face * 0.5f + (k / 2) * (face + 8));
            if (k < shown) drawHeadshot(dl, c, face * 0.5f, people[k].value("avatar", nlohmann::json()));
            else {
                dl->AddCircleFilled(c, face * 0.5f, IM_COL32(120, 126, 140, 255), 32);
                std::string t = "+" + std::to_string(more);
                ImVec2 ts = ImGui::CalcTextSize(t.c_str());
                dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), t.c_str());
            }
        }
        char line[64];
        std::snprintf(line, sizeof(line), "%d of %d people max", players, max);
        dl->AddText(ImVec2(p.x + 14, p.y + 186), IM_COL32(235, 237, 242, 255), line);
        std::string id = sv.value("id", std::string());
        std::string shortId = "ID: " + (id.rfind("s-", 0) == 0 ? id.substr(2) : id);
        dl->AddText(ImVec2(p.x + 14, p.y + cardH - 20), IM_COL32(170, 175, 185, 255), shortId.c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + 14, p.y + 208));
        ImGui::BeginDisabled(players >= max);
        if (Classic::button(players >= max ? "Full" : "Join", Classic::kPlay, ImVec2(cardW - 28, 24))) {
            startLoadingScreen(gameKey, title);
            joinRelay(id, "", title);
        }
        ImGui::EndDisabled();
        ImGui::SetCursorScreenPos(p);
        ImGui::Dummy(ImVec2(cardW, cardH));
        ImGui::EndGroup();
        // Names when you point at a card.
        if (ImGui::IsItemHovered() && !people.empty()) {
            std::string names;
            for (const auto& pp : people) names += (names.empty() ? "" : ", ") + pp.value("name", std::string("?"));
            if (more > 0) names += " and " + std::to_string(more) + " more";
            ImGui::SetTooltip("%s", names.c_str());
        }
        ImGui::PopID();
    }
    if (pages > 1) {
        ImGui::Spacing();
        if (ImGui::SmallButton("<<")) m_gameServersPage = 0;
        ImGui::SameLine();
        if (ImGui::SmallButton("<")) --m_gameServersPage;
        ImGui::SameLine();
        ImGui::Text("Page %d of %d", m_gameServersPage + 1, pages);
        ImGui::SameLine();
        if (ImGui::SmallButton(">")) ++m_gameServersPage;
        ImGui::SameLine();
        if (ImGui::SmallButton(">>")) m_gameServersPage = pages - 1;
    }
}

// ---------------------------------------------------------------------------
// Avatar editor
// ---------------------------------------------------------------------------

void PlayerApp::buildAvatarStage() {
    m_avatarScene = std::make_unique<Scene>();
    // Keep only the floor and the character.
    std::vector<SceneNode*> remove;
    for (auto& c : m_avatarScene->root()->children)
        if (c->name != "Baseplate" && !m_avatarScene->isProtected(c.get())) remove.push_back(c.get());
    for (auto* r : remove) m_avatarScene->removeNode(r);
    if (Player* p = m_avatarScene->player()) {
        p->setSpawn({0, 0, 0});
        p->build();
        Profile::get().applyTo(*p);
    }
    Environment& e = m_avatarScene->environment();
    e.fogEnabled = false;
    e.sunAzimuth = 70.0f;
    e.sunElevation = 40.0f;
    m_avatarCam.pivot = {0, 1.4f, 0};
    m_avatarCam.yaw = 90.0f;
    m_avatarCam.pitch = 8.0f;
    m_avatarCam.distance = 6.5f;
    m_avatarCam.fov = 45.0f;
}

void PlayerApp::drawAvatar(float dt) {
    Profile& me = Profile::get();
    Player* p = m_avatarScene->player();
    bool changed = false;

    ImGui::BeginChild("##avatar", ImVec2(0, 0));

    // Left: 3D preview (drag to spin).
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool tall = portraitScreen();
#ifdef GB_MOBILE
    float previewW = tall ? avail.x : std::max(160.0f, avail.x * 0.38f);
#else
    float previewW = tall ? avail.x : std::max(200.0f, avail.x * 0.55f);
#endif
    ImGui::BeginChild("##preview", ImVec2(previewW, tall ? std::min(260.0f, avail.y * 0.4f) : 0.0f), ImGuiChildFlags_Borders);
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x > 1 && size.y > 1) {
        const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
        m_avatarView.resize((int)(size.x * fb), (int)(size.y * fb));
        m_avatarCam.resize((int)(size.x * fb), (int)(size.y * fb));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_avatarCam.yaw += dt * 12.0f;   // slow turntable
        m_renderer->render(*m_avatarScene, m_avatarCam, m_avatarView, false);
        ImGui::Image((ImTextureID)(intptr_t)m_avatarView.colorTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_avatarCam.yaw += ImGui::GetIO().MouseDelta.x * 0.5f;
    }
    ImGui::EndChild();
    if (!tall) ImGui::SameLine(0, 24);   // side by side, or stacked on an upright phone

    // Right: options.
    ImGui::BeginChild("##opts", ImVec2(0, 0));
    ImGui::SetWindowFontScale(1.4f);
    ImGui::Text("Your Avatar");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("This is how you look in every game.");
    ImGui::Spacing();

    const bool signedUp = Online::online() && Online::me().value("userId", 0LL) > 0;
    if (signedUp) {   // online, your name is your username (one of a kind, so nobody can pretend to be you)
        ImGui::Text("Name: %s", me.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(your username)");
    }
    if (!signedUp) {   // playing offline: call yourself what you like
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Display name").x - ImGui::GetStyle().ItemInnerSpacing.x));
        if (!ImGui::IsAnyItemActive() && m_nameEdit != me.name && m_nameError.empty()) m_nameEdit = me.name;
        if (ImGui::InputText("Display name", &m_nameEdit) && m_nameEdit.size() > 20) m_nameEdit.resize(20);
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (me.rename(m_nameEdit, m_nameError)) m_nameError.clear();
            else m_nameEdit = me.name;
        }
    }
    if (!m_nameError.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_nameError.c_str());

    // Two tabs: your wardrobe (what you own: wear it right here, no trip to the
    // catalog) and your body (colours and the classic hats).
    ImGui::Spacing();
    {
        const char* tabNames[] = {"Wardrobe", "Body & Colours", "Outfits"};
        const float tw = std::min(150.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f);
        for (int t = 0; t < 3; ++t) {
            if (t) ImGui::SameLine();
            bool on = m_avatarTab == t;
            if (on ? Classic::button(tabNames[t], Classic::kBlue, ImVec2(tw, 30)) : ImGui::Button(tabNames[t], ImVec2(tw, 30)))
                m_avatarTab = t;
        }
    }
    if (m_avatarTab == 0) {
        drawWardrobe();
    } else if (m_avatarTab == 2) {
        drawOutfits();
    } else {
    ImGui::SeparatorText("Colour sets");
    int i = 0;
    for (const auto& [name, colors] : Player::colorPresets()) {
        // Three per row, shrinking to fit narrow (phone) screens.
        float rowW = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        float bw = std::min(130.0f, (rowW - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f);
        if (ImGui::Button(name, ImVec2(bw, 30))) { me.colors = colors; changed = true; }
        if (++i % 3 != 0) ImGui::SameLine();
    }
    if (i % 3 != 0) ImGui::NewLine();

    ImGui::SeparatorText("Body colours");
    const ImGuiColorEditFlags cf = ImGuiColorEditFlags_NoInputs;
    changed |= ImGui::ColorEdit3("Head", &me.colors.head.x, cf);   ImGui::SameLine(160);
    changed |= ImGui::ColorEdit3("Torso", &me.colors.torso.x, cf);
    changed |= ImGui::ColorEdit3("Left Arm", &me.colors.leftArm.x, cf); ImGui::SameLine(160);
    changed |= ImGui::ColorEdit3("Right Arm", &me.colors.rightArm.x, cf);
    changed |= ImGui::ColorEdit3("Left Leg", &me.colors.leftLeg.x, cf); ImGui::SameLine(160);
    changed |= ImGui::ColorEdit3("Right Leg", &me.colors.rightLeg.x, cf);

    ImGui::SeparatorText("Hat");
    for (int h = 0; h < kHatStyleCount; ++h) {
        bool on = (int)me.hat == h;
        float rowW = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
        float hw = std::min(95.0f, (rowW - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f);
        bool pressed = on ? Classic::button(Player::hatName((HatStyle)h), Classic::kBlue, ImVec2(hw, 30))
                          : ImGui::Button(Player::hatName((HatStyle)h), ImVec2(hw, 30));
        if (pressed) {
            me.hat = (HatStyle)h;
            me.hatColor = glm::vec3(-1.0f);   // back to its normal colours
            changed = true;
        }
        if (h % 4 != 3 && h + 1 < kHatStyleCount) ImGui::SameLine();   // four to a row
    }
    }   // Body & Colours

    drawAccount();

    ImGui::EndChild();
    ImGui::EndChild();

    // Just put on a saved outfit: show it as it comes in from the server (the look, then the clothes).
    if (m_restageUntil > 0.0 && ImGui::GetTime() > m_restageAt && p) {
        m_restageAt = ImGui::GetTime() + 0.5;
        me.applyTo(*p);
        Online::fetchSounds(*m_avatarScene);
        if (ImGui::GetTime() > m_restageUntil) m_restageUntil = 0.0;
    }
    if (changed && p) {
        me.applyTo(*p);
        me.save();
        m_avatarPushAt = ImGui::GetTime() + 1.5;   // then save it on the server too (after you stop clicking)
    }
    if (m_avatarPushAt > 0.0 && ImGui::GetTime() > m_avatarPushAt) {
        m_avatarPushAt = 0.0;
        if (Online::online() && Online::me().value("userId", 0LL) > 0) Online::pushAvatar();
    }
}

// ---------------------------------------------------------------------------
// In game
// ---------------------------------------------------------------------------

void PlayerApp::drawGame(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !m_chatOpen && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                             ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false) ||
                                             (m_paused && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))) {
        if (m_menuConfirm) m_menuConfirm = 0;   // Esc (or B) backs out of "Are you sure?"
        else { m_paused = !m_paused; m_menuTab = 0; }
    }

    // Test helper: "--page menu" / "menu-settings" / "menu-help" / "menu-leave" opens the in-game menu.
    if (m_frame == 60 && m_opts.page.rfind("menu", 0) == 0) {
        m_paused = true;
        m_menuTab = m_opts.page == "menu-settings" ? 1 : m_opts.page == "menu-help" ? 2 : 0;
        m_menuConfirm = m_opts.page == "menu-leave" ? 2 : 0;
    }

    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (size.x < 1 || size.y < 1) return;
    if (m_opts.vr) { m_opts.vr = false; startVr(); }   // (--vr)

    // Networking first (receive the world), then simulate, then (next tick) send.
    if (m_client) {
        m_client->update(dt);
        if (m_client->state() == NetClient::State::Failed) {
            std::string err = m_client->error();
            // The host left (or we're already following everyone): don't kick, move to a new server.
            if (m_client->hostLeft() && !m_client->relaySession().empty()) {
                std::string from = m_client->relaySession(), game = m_client->relayGame();
                m_carry = handoffSnapshot(*m_scene, m_client->otherCharacters());   // (in case we're the one who takes over)
                m_carryGame = game;
                m_client.reset();
                startMove(from, game, m_currentTitle);
            } else if (moving() && m_client->movingTurn()) {
                const bool mine = m_client->movingTurn() == 2;
                m_client.reset();
                if (mine) {   // our turn to host it: same game, everyone follows us
                    m_continues = m_moveFrom;
                    m_moveFrom.clear();
                    onlineStarter(m_moveGame)(HostMode::Public);
                } else m_moveRetryAt = ImGui::GetTime() + 2.0;
            } else {
                m_status = err;
                leaveGame();
                m_status = err;
                return;
            }
        }
        if (m_client && m_client->state() == NetClient::State::Joined && moving()) {   // made it
            m_moveFrom.clear();
            m_carry.clear();
            m_handoff = false;
        }
    }
    // We were going to host the new server, but someone else already does: follow them instead.
    if (!m_tookOver.empty() && ImGui::GetTime() > m_tookOverUntil) m_tookOver.clear();
    if (m_server && !m_tookOver.empty() && !m_server->relayError().empty()) {
        std::string from = m_tookOver;
        m_tookOver.clear();
        m_server.reset();
        startMove(from, m_moveGame, m_currentTitle);
    }
    // Moving to the new server: the world stays on screen (frozen for a moment) instead of a
    // loading screen, with a note at the top (drawn below).
    if (moving() && !m_client) {
        moveStep();
        if (m_page != Page::Game) return;   // (nobody could start it)
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_moveFrom.clear(); leaveGame(); return; }
    }
    if (m_client) {
        if (m_client->state() == NetClient::State::Joined) {
            m_currentTitle = m_client->gameTitle();
            if (!m_joinedOnce) { m_joinedOnce = true; Online::fetchSounds(*m_scene); }   // server audio the host's game uses
        }
    }
    updateVoice(!m_paused && !m_chatOpen && !io.WantTextInput);
    if (m_server) m_server->update(dt);
    if (!m_server && !m_client) m_soloChat->update(dt);

    bool connecting = m_client && m_client->state() != NetClient::State::Joined;
    if (connecting && !m_handoff) {
        drawLoading(pos, size, 1.0f, "Connecting to server");
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) leaveGame();
        return;
    }

    // Chat: "/" or Enter starts typing (not for guests: they sign up to chat).
    const bool guestChat = Online::online() && Online::isGuest();
    if (!m_chatOpen && !guestChat && !io.WantTextInput && !m_paused &&
        (ImGui::IsKeyPressed(ImGuiKey_Slash, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
        m_chatOpen = true;

    // Simulate (the world keeps running while the menu is open, like Roblox).
    bool acceptInput = !m_paused && !m_showSettings && !m_chatOpen;
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    const bool touch = GraphicsSettings::get().touchEnabled();
    // VR: wait for the headset's next picture, read its controllers (they walk you
    // like the touch screen's thumbstick does).
    Vr::View vrEyes[2];
    if (m_vrOn && !Vr::running()) {   // the headset closed us (VR software quit...)
        m_vrOn = false;
        m_window->setHeadsetPaced(false);
        m_vrStatus = Vr::lastError();
        m_session->scripts().setVr({});
    }
    const bool vr = m_vrOn;
    const bool vrDraw = vr && Vr::beginFrame(vrEyes, 0.1f, 1000.0f);
    if (touch) updateTouch(pos, max, acceptInput);
    else if (vr) { vrControls(acceptInput); vrTrack(); }
    else if (!m_opts.vrTestHands.empty()) {   // tests: the VR body without a headset, facing the way the camera looks
        const glm::vec3 f = m_camera.forward();
        m_vrYaw = glm::degrees(std::atan2(-f.x, -f.z));
        m_vrBodyYaw = glm::degrees(std::atan2(f.x, f.z));
        vrTrack();
    }
    else       m_session->setTouchInput(glm::vec2(0.0f), false);
    m_session->update(dt, m_camera.yaw, acceptInput, PlayCamera::swimLook(m_camera));

    // BadgeService:AwardBadge from the game's scripts: the server checks we're this
    // game's host and the player is here, then everyone hears about it.
    for (auto& [who, badge] : m_session->scripts().takeBadgeAwards()) {
        if (!m_server || !Online::online()) continue;
        Online::request("gamebadge.award", {{"badge", badge}, {"to", who}}, [this](const nlohmann::json& r) {
            if (!r.value("ok", false)) { Log::warn("BadgeService: " + r.value("error", std::string())); return; }
            if (r.value("awarded", false) && m_server)
                m_server->announce(r.value("player", std::string()) + " earned the badge \"" + r.value("name", std::string()) + "\"!");
        });
    }

    // TeleportService: players the game's scripts sent to another game. We go ourselves;
    // people who joined us get told to go (their app does the rest).
    for (auto& tp : m_session->scripts().takeTeleports()) {
        if (tp.player == m_session->scripts().playerName()) teleportMe(tp.place, tp.data, m_playingKey);
        else if (m_server && !m_server->teleport(tp.player, tp.place, tp.data, m_playingKey))
            Log::warn("TeleportService: " + tp.player + " isn't in this server");
    }
    if (m_client)
        if (nlohmann::json tp = m_client->takeTeleport(); tp.is_object())
            teleportMe(tp.value("place", std::string()), tp.value("data", nlohmann::json()), tp.value("from", std::string()));

    // Bolts for playing (not while the menu is open). Online, the server keeps count.
    if (!m_paused && Online::online()) onlinePlayTick(dt);
    else if (!m_paused) {
        if (long long got = Bolts::addPlayTime(dt)) {
            m_boltsToast = "+" + Bolts::format(got) + " Bolts for playing!";
            m_boltsToastUntil = ImGui::GetTime() + 4.0;
        }
    }

    // Camera: follow the character's head; right-drag to look around, wheel to
    // zoom, all the way in for first person (where the mouse looks around by itself).
    // Shift toggles Shift Lock (like Roblox): the mouse is locked in the middle
    // and turns the camera, which sits over the right shoulder.
    bool hovered = ImGui::IsWindowHovered();
    const bool firstPerson = PlayCamera::firstPerson(m_camera);
    const bool typing = io.WantTextInput || m_chatOpen;
    if (!GraphicsSettings::get().shiftLockSwitch || touch) m_shiftLock = false;
    else if (acceptInput && !typing && !firstPerson &&
             (ImGui::IsKeyPressed(ImGuiKey_LeftShift, false) || ImGui::IsKeyPressed(ImGuiKey_RightShift, false)))
        m_shiftLock = !m_shiftLock;
    if (acceptInput && hovered && !vr) {   // (in VR your head turns the camera)
        const bool locked = (firstPerson || (m_shiftLock && !m_camera.noInput)) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        if (locked) {
            ImVec2 c = ImGui::GetCursorScreenPos();
            ImVec2 mid(c.x + size.x * 0.5f, c.y + size.y * 0.5f);
            AppWindow::lockMouse(mid.x, mid.y);
            PlayCamera::turn(m_camera, AppWindow::mouseLookX(), AppWindow::mouseLookY());
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            if (m_shiftLock && !firstPerson) {
                // Roblox's Shift Lock cursor: a ring with a dot.
                fg->AddCircle(mid, 11.0f, IM_COL32(0, 0, 0, 120), 24, 4.0f);
                fg->AddCircle(mid, 11.0f, IM_COL32(255, 255, 255, 235), 24, 2.0f);
                fg->AddCircleFilled(mid, 2.5f, IM_COL32(255, 255, 255, 235));
            } else {
                // The pointer is hidden: a little dot in the middle shows what you'd click.
                fg->AddCircleFilled(mid, 3.5f, IM_COL32(0, 0, 0, 160));
                fg->AddCircleFilled(mid, 2.0f, IM_COL32(255, 255, 255, 230));
            }
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            PlayCamera::turn(m_camera, io.MouseDelta.x, io.MouseDelta.y);
        }
        if (!GameGui::overScroller(*m_scene, ImGui::GetMousePos())) PlayCamera::zoom(m_camera, io.MouseWheel, m_scene->cameraOptions());   // (else the wheel scrolls the UI)
    }
    if (acceptInput && Pad::connected() && !vr) padCamera(dt);
    if (Player* p = m_scene->player()) {
        if (vr) vrFollow(*p);
        else PlayCamera::follow(m_camera, *p, dt, m_shiftLock);
        PlayCamera::fade(*m_scene, *p, m_camera);
        if (vr) vrShowBody(*p);
    }

#ifdef GB_MOBILE
    // Phones: render with the real pixels, not the (bigger) UI pixels.
    ImVec2 px(size.x * io.DisplayFramebufferScale.x, size.y * io.DisplayFramebufferScale.y);
#else
    ImVec2 px = size;
#endif
    m_camera.resize((int)px.x, (int)px.y);
    if (vr) {   // each eye for the headset (the screen shows the left one)
        if (vrDraw) vrRender(vrEyes);   // (vrAfterUi finishes the headset's picture, once the menus are drawn)
    } else {
        m_view.resize((int)px.x, (int)px.y);
        m_renderer->render(*m_scene, m_camera, m_view, false);
    }
    if (GameGui::needsBackdrop(*m_scene) && !vr) {   // UIBlur: the world behind the UI, blurred
        m_renderer->makeBackdrop(m_view);
        unsigned bd[SceneRenderer::kBackdropLevels];
        for (int i = 0; i < SceneRenderer::kBackdropLevels; ++i) bd[i] = m_renderer->backdrop(i);
        GameGui::setBackdrop(bd, SceneRenderer::kBackdropLevels);
    } else {
        GameGui::setBackdrop(nullptr, 0);
    }
    Audio::setListener(m_camera.position(), m_camera.forward());
    if (vr) vrMirror(size);
    else ImGui::Image((ImTextureID)(intptr_t)m_view.colorTexture(), size, ImVec2(0, 1), ImVec2(1, 0));

    // Clicking parts (for part.Clicked in scripts). With touch controls, a tap does it.
    ImVec2 tapAt;
    bool tapped = touch && m_touch.tapped(tapAt);
    ImVec2 pointer = tapped ? tapAt : ImGui::GetMousePos();
    const bool emotesOpen = m_scene->coreGui().emotesOpen;   // (while it's open, clicks are the emote menu's)
    const bool onHotbar = emotesOpen || Hud::overHotbar(pos, max, *m_scene, pointer);   // picking a tool isn't swinging it
    // The game's own UI (buttons...) gets the pointer first.
    GameGui::setCamera(m_camera.view(), m_camera.projection(), m_camera.position());   // (UI on parts)
    std::vector<GameGui::Event> guiEvents;
    bool onGuiPrompt = false;
    const bool onGui = GameGui::handle(*m_scene, pos, max, pointer, acceptInput && (hovered || tapped) && !onHotbar,
                                       !touch && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                                       !touch && ImGui::IsMouseReleased(ImGuiMouseButton_Left), tapped, m_guiInput, guiEvents);
    m_vrOnGameUi = vr && (onGui || onHotbar);
    // ProximityPrompts near our character ("E  Open").
    {
        Player* me = m_session->runOnly() ? nullptr : m_scene->player();
        const glm::vec3 at = me && me->root() && !me->isDead() ? me->position() : glm::vec3(0.0f);
        const bool onPrompt = GameGui::prompts(*m_scene, pos, max, me && me->root() && !me->isDead() ? &at : nullptr, pointer,
                                               acceptInput && !onGui && !onHotbar, io.DeltaTime, guiEvents);
        if (onPrompt) onGuiPrompt = true;
    }
    m_session->guiEvents(guiEvents);
    // The part at a point on the screen (not our own character).
    auto partAt = [&](ImVec2 m) -> SceneNode* {
        float nx = (m.x - pos.x) / size.x * 2.0f - 1.0f;
        float ny = 1.0f - (m.y - pos.y) / size.y * 2.0f;
        glm::mat4 inv = glm::inverse(m_camera.projection() * m_camera.view());
        glm::vec4 a = inv * glm::vec4(nx, ny, -1, 1), b = inv * glm::vec4(nx, ny, 1, 1);
        glm::vec3 ro = glm::vec3(a) / a.w, rd = glm::normalize(glm::vec3(b) / b.w - ro);
        const SceneNode* character = m_scene->player() ? m_scene->player()->root() : nullptr;
        return Physics::raycast(*m_scene, ro, rd, nullptr, character);
    };
    const bool toWorld = acceptInput && !onHotbar && !onGui && !onGuiPrompt;
    // ClickDetectors: what the mouse points at (with a controller, the middle of the screen)
    // gets MouseHoverEnter / Leave, and the pointer becomes a hand over it.
    const bool padAim = !touch && Pad::connected() && Pad::inUse();
    const ImVec2 centre((pos.x + max.x) * 0.5f, (pos.y + max.y) * 0.5f);
    SceneNode* under = toWorld && (padAim || (!touch && hovered)) ? partAt(padAim ? centre : pointer) : nullptr;
    std::string cursorIcon;
    const bool onClickable = m_session->hoverPart(under ? under->id : 0, &cursorIcon);
    if (onClickable && !padAim) ClickDetectors::showPointer(pointer, cursorIcon);
    if (toWorld && (tapped || (!touch && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)))) {
        SceneNode* hit = partAt(pointer);
        m_session->click(hit ? hit->id : 0);
    }
    // A right-click (not a right-drag turning the camera): RightMouseClick.
    if (toWorld && !touch && hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 6.0f * 6.0f) {
        SceneNode* hit = partAt(pointer);
        m_session->rightClick(hit ? hit->id : 0);
    }
    // A controller's RT clicks what's in the middle of the screen (when it isn't using a tool).
    {
        const bool trigger = padAim && Pad::rightTrigger() > 0.5f;
        if (trigger && !m_padClick && onClickable && under) m_session->clickDetector(under->id);
        m_padClick = trigger;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float labelsAt = 0.0f;
    if (touch) {
        bool chatShowing = m_chatOpen || ImGui::GetTime() < m_chatShowUntil;
        labelsAt = chatShowing ? (m_chatOpen ? 216.0f : 156.0f) : 58.0f;   // under the chat box when it's up
    }
    GameGui::draw(dl, pos, max, *m_scene, &m_guiInput);
    GameGui::drawPrompts(dl);
    if (padAim && toWorld) ClickDetectors::drawAim(dl, centre, onClickable);
    {
        std::vector<GameGui::Event> typed;   // a TextBox being typed in
        GameGui::textInput(*m_scene, typed);
        m_session->guiEvents(typed);
    }
    Hud::draw(dl, pos, max, *m_scene, m_session->gui(), labelsAt);
    if (int slot = Hud::drawHotbar(dl, pos, max, *m_scene, tapped && onHotbar && !emotesOpen ? &tapAt : nullptr); slot >= 0 && acceptInput && !emotesOpen)
        m_session->selectToolSlot(slot);
    Hud::drawNameTags(dl, pos, max, *m_scene, m_camera.projection() * m_camera.view(), m_camera.position());
    Hud::drawBubbles(dl, pos, max, *m_scene, m_camera.projection() * m_camera.view(), chat().bubbles);
    Hud::drawVoiceTags(dl, pos, max, m_camera.projection() * m_camera.view());
    drawVoiceHud(dl, pos, max, touch);
    // The emote menu: B (or the smiley button on phones) opens a wheel of your emotes.
    EmoteMenu::update(dl, pos, max, *m_scene, acceptInput && !io.WantTextInput && !m_session->runOnly(),
                      tapped ? &tapAt : nullptr, !touch && hovered);
    // Tab (or a controller's Back / Select) folds the leaderboard away (and back), like old Roblox.
    if (acceptInput && !m_chatOpen && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Tab, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false)))
        m_listOpen = !m_listOpen;
    {
        ImVec2 at;
        std::string who = Hud::drawPlayerList(dl, pos, max, currentPlayers(), m_listOpen, &at, tapped ? &tapAt : nullptr);
        if (!who.empty() && who != Online::playerName()) {
            m_listMenu = who;
            m_listMenuAt = at;
            m_listRel = nlohmann::json();
            m_listMsg.clear();
            if (!Online::isGuest())
                Online::request("friends.relation", {{"user", who}}, [this, who](const nlohmann::json& r) {
                    if (m_listMenu == who) m_listRel = r;
                });
        }
    }
    drawPlayerMenu();
    if (m_loadingT <= 0.3f) drawChat(pos, max);   // not over the loading screen
    if (ImGui::IsKeyPressed(ImGuiKey_F9, false)) m_devConsole = !m_devConsole;
    drawDevConsole();
    updatePassPrompts();
    drawPassPrompt();

    // "+5 Bolts for playing!" popup, top middle.
    if (ImGui::GetTime() < m_boltsToastUntil) {
        ImVec2 ts = ImGui::CalcTextSize(m_boltsToast.c_str());
        float w = ts.x + 50, x = pos.x + (size.x - w) * 0.5f, y = pos.y + 16;
        float fade = (float)std::min(1.0, m_boltsToastUntil - ImGui::GetTime());
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + 34), IM_COL32(40, 30, 10, (int)(210 * fade)), 17.0f);
        Bolts::drawIcon(dl, ImVec2(x + 20, y + 17), 22.0f);
        dl->AddText(ImVec2(x + 38, y + (34 - ts.y) * 0.5f), IM_COL32(255, 215, 90, (int)(255 * fade)), m_boltsToast.c_str());
    }

    // Menu button + FPS. (Touch screens get their own buttons instead.)
    if (touch) {
        if (acceptInput) m_touch.draw(dl);
    } else {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 12, max.y - 44));
        if (bigButton("Menu (Esc)", ImVec4(0.1f, 0.1f, 0.12f, 0.8f))) m_paused = true;
    }
    if (GraphicsSettings::get().showFps) {
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.0f FPS", io.Framerate);
        dl->AddText(ImVec2(max.x - 80, max.y - 26), IM_COL32(255, 255, 255, 160), fps);
    }

    if (m_loadingT > 0.0f) {   // the loading screen fades away as the game appears
        m_loadingT -= dt;
        drawLoading(pos, size, std::clamp(m_loadingT / 0.6f, 0.0f, 1.0f), "Starting");
    }
    if (m_handoff) {   // the host left: a note at the top while someone else takes over
        const char* note = "The host left. Someone else is taking over, hang on...";
        const ImVec2 ts = ImGui::CalcTextSize(note);
        const ImVec2 a(pos.x + (size.x - ts.x) * 0.5f - 14, pos.y + 18), b(a.x + ts.x + 28, a.y + ts.y + 14);
        dl->AddRectFilled(a, b, IM_COL32(20, 20, 24, 210), 6.0f);
        dl->AddText(ImVec2(a.x + 14, a.y + 7), IM_COL32(255, 230, 140, 255), note);
    }

    if (m_paused) drawPauseMenu();
}

// The classic connecting screen: the game's icon and name, a spinning circle,
// what's happening, and the Guts&Bolts logo underneath.
void PlayerApp::drawLoading(ImVec2 pos, ImVec2 size, float alpha, const char* status) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    const float base = ImGui::GetFontSize();
    const int a = (int)(255 * alpha);
    auto col = [a](int r, int g, int b, float k = 1.0f) { return IM_COL32(r, g, b, (int)(a * k)); };
    ImVec2 max(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilledMultiColor(pos, max, col(34, 36, 42), col(34, 36, 42), col(14, 15, 18), col(14, 15, 18));

    std::string title = m_currentTitle;
    if (title.rfind("Joining ", 0) == 0) title = title.substr(8);
    if (title.size() > 3 && title.compare(title.size() - 3, 3, "...") == 0) title.resize(title.size() - 3);
    if (title.empty()) title = "Guts&Bolts";

    // Everything is stacked in the middle; shrink it on short (phone) screens.
    const float k = std::clamp(size.y / 560.0f, 0.6f, 1.5f);
    const float iconSize = 96.0f * k, spin = 30.0f * k, logoSize = 30.0f * k;
    const float wrap = std::max(120.0f, size.x - 60.0f);
    float big = base * 1.8f * std::max(0.8f, k);
    const float small = base * std::max(1.0f, k);   // "By ..." and the status line
    ImVec2 ts = font->CalcTextSizeA(big, FLT_MAX, wrap, title.c_str());
    const std::string by = m_currentAuthor.empty() ? std::string() : "By " + m_currentAuthor;
    const float byH = by.empty() ? 0.0f : base * std::max(1.0f, k) + 4;
    const ImVec2 lsz = font->CalcTextSizeA(logoSize, FLT_MAX, 0.0f, "GUTS&BOLTS");
    const float total = iconSize + 14 + ts.y + byH + 34 * k + spin * 2 + 16 + base + 34 * k + lsz.y;
    float y = pos.y + std::max(10.0f, (size.y - total) * 0.5f);
    const float cx = pos.x + size.x * 0.5f;

    // The game's icon (or its first letter on a coloured square).
    ImVec2 i0(cx - iconSize * 0.5f, y), i1(cx + iconSize * 0.5f, y + iconSize);
    unsigned tex = m_loadingIcon.empty() ? 0 : Textures::get(m_loadingIcon);
    dl->AddRectFilled(ImVec2(i0.x - 3, i0.y - 3), ImVec2(i1.x + 3, i1.y + 3), col(255, 255, 255, 0.9f), 14.0f * k);
    if (tex) {
        dl->AddImageRounded((ImTextureID)(intptr_t)tex, i0, i1, ImVec2(0, 1), ImVec2(1, 0), col(255, 255, 255), 12.0f * k);
    } else {
        unsigned h = 2166136261u;
        for (char ch : title) h = (h ^ (unsigned char)ch) * 16777619u;
        ImU32 top = col(40 + (h & 0x5f), 60 + ((h >> 8) & 0x5f), 110 + ((h >> 16) & 0x5f));
        ImU32 bot = col(20 + ((h >> 4) & 0x5f), 30 + ((h >> 12) & 0x5f), 60 + ((h >> 20) & 0x5f));
        dl->AddRectFilledMultiColor(i0, i1, top, top, bot, bot);
        const char letter[2] = {title[0], 0};
        const float ls = iconSize * 0.55f;
        ImVec2 lt = font->CalcTextSizeA(ls, FLT_MAX, 0.0f, letter);
        dl->AddText(font, ls, ImVec2(cx - lt.x * 0.5f + 2, i0.y + (iconSize - lt.y) * 0.5f + 2), col(0, 0, 0, 0.4f), letter);
        dl->AddText(font, ls, ImVec2(cx - lt.x * 0.5f, i0.y + (iconSize - lt.y) * 0.5f), col(255, 255, 255), letter);
    }
    y = i1.y + 14;

    // Its name, and who made it.
    dl->AddText(font, big, ImVec2(cx - ts.x * 0.5f + 2, y + 2), col(0, 0, 0, 0.6f), title.c_str(), nullptr, wrap);
    dl->AddText(font, big, ImVec2(cx - ts.x * 0.5f, y), col(255, 255, 255), title.c_str(), nullptr, wrap);
    y += ts.y;
    if (!by.empty()) {
        ImVec2 bs = font->CalcTextSizeA(small, FLT_MAX, 0.0f, by.c_str());
        dl->AddText(font, small, ImVec2(cx - bs.x * 0.5f, y + 2), col(170, 176, 190), by.c_str());
        y += byH;
    }
    y += 34 * k;

    // The spinning circle: 12 bars, the bright one going round and the rest fading behind it.
    ImVec2 c(cx, y + spin);
    const float t = (float)ImGui::GetTime();
    const int bars = 12;
    const int lead = (int)(t * 12.0f) % bars;
    for (int i = 0; i < bars; ++i) {
        float ang = (float)i / bars * 6.2831853f - 1.5707963f;
        int behind = (lead - i + bars) % bars;
        float bright = std::max(0.15f, 1.0f - behind / 7.0f);
        ImVec2 dir(std::cos(ang), std::sin(ang));
        dl->AddLine(ImVec2(c.x + dir.x * spin * 0.5f, c.y + dir.y * spin * 0.5f),
                    ImVec2(c.x + dir.x * spin, c.y + dir.y * spin), col(255, 255, 255, bright), std::max(3.0f, 5.0f * k));
    }
    y = c.y + spin + 16;

    // What's happening ("Connecting to server...").
    char line[96];
    std::snprintf(line, sizeof(line), "%s%.*s", status, 1 + (int)(t * 3.0f) % 3, "...");
    ImVec2 ls = font->CalcTextSizeA(small, FLT_MAX, 0.0f, status);
    dl->AddText(font, small, ImVec2(cx - ls.x * 0.5f, y), col(220, 224, 232), line);
    y += small + 34 * k;

    // The logo under it all.
    if (alpha > 0.99f) Classic::logo(dl, ImVec2(cx - lsz.x * 0.5f, y), logoSize, "GUTS&BOLTS");
    else dl->AddText(font, logoSize, ImVec2(cx - lsz.x * 0.5f, y), col(222, 34, 28), "GUTS&BOLTS");
}

// Pressing Play: show the connecting screen straight away, and fetch the game's icon for it.
void PlayerApp::startLoadingScreen(const std::string& gameId, const std::string& title) {
    m_connectScreen = true;
    if (!title.empty()) m_currentTitle = title;
    m_currentAuthor.clear();
    for (const auto& g : m_onlineGames)
        if (g.value("id", std::string()) == gameId) m_currentAuthor = g.value("creatorName", std::string());
    if (gameId == m_loadingGameId && !m_loadingTitle.empty()) {   // already asked about this one
        m_currentTitle = m_loadingTitle;
        if (!m_loadingAuthor.empty()) m_currentAuthor = m_loadingAuthor;
        return;
    }
    m_loadingGameId = gameId;
    m_loadingIcon.clear();
    m_loadingTitle.clear();
    m_loadingAuthor.clear();
    if (gameId.empty() || gameId.rfind("local:", 0) == 0 || !Online::online()) return;
    Online::request("icon.get", {{"id", gameId}}, [this, gameId](const nlohmann::json& r) {
        if (!r.value("ok", false) || m_loadingGameId != gameId) return;
        // The name it has on the site (the file inside may still have Studio's name), and who made it.
        m_loadingTitle = r.value("name", std::string());
        m_loadingAuthor = r.value("creatorName", std::string());
        if (!m_loadingTitle.empty()) m_currentTitle = m_loadingTitle;
        if (!m_loadingAuthor.empty()) m_currentAuthor = m_loadingAuthor;
        std::string bytes;
        if (!Online::base64Decode(r.value("data", std::string()), bytes) || bytes.size() < 4) return;
        const bool jpg = (unsigned char)bytes[0] == 0xff && (unsigned char)bytes[1] == 0xd8;
        std::filesystem::path file = Paths::downloadsFolder() / ("icon-" + gameId + "-" + std::to_string(r.value("icon", 0LL)) + (jpg ? ".jpg" : ".png"));
        std::ofstream out(file, std::ios::binary);
        out.write(bytes.data(), (std::streamsize)bytes.size());
        out.close();
        if (m_loadingGameId == gameId) m_loadingIcon = file.string();
    }, 10);
}

// The connecting screen while the server's being asked and the game downloads.
void PlayerApp::drawConnectScreen() {
    if (!m_connectScreen) return;
    if (m_page == Page::Game || (!m_busy && m_playMsg.empty())) { m_connectScreen = false; return; }   // it's here (or it failed)
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##connecting", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(3);
    const bool downloading = m_playMsg.rfind("Download", 0) == 0;
    drawLoading(vp->Pos, vp->Size, 1.0f, downloading ? "Downloading the game" : "Connecting to server");
    ImGui::End();
}

// Under a line that was translated into your language: a small "(translated from Russian)"; hover
// it (or tap it) to see what they really wrote.
static void translatedTag(const ChatLog::Line& l) {
    if (!l.translated) return;
    const char* from = Lang::name(l.lang);
    ImGui::TextDisabled(*from ? "(translated from %s)" : "(translated)", from);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextDisabled("What they wrote:");
        ImGui::TextUnformatted(l.original.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void PlayerApp::drawChat(ImVec2 min, ImVec2 max) {
    ChatLog& log = chat();
    const bool touchUi = GraphicsSettings::get().touchEnabled();
    if (touchUi) {
        // Phones: chat stays out of the way until you tap the chat button, and
        // pops up for a few seconds when someone says something.
        double now = ImGui::GetTime();
        if (log.lines.size() != m_chatSeen) { m_chatSeen = log.lines.size(); m_chatShowUntil = now + 6.0; }
        if (!m_chatOpen && now > m_chatShowUntil) { m_chatMin = m_chatMax = ImVec2(0, 0); return; }
    }
    // Bottom-left normally; top-left on touch screens (the thumbstick lives bottom-left).
    const bool touch = GraphicsSettings::get().touchEnabled();
    const float w = touch ? 340.0f : 420.0f, h = touch ? (m_chatOpen ? 150.0f : 90.0f) : 210.0f;
    ImVec2 p = touch ? ImVec2(min.x + 12, min.y + 70) : ImVec2(min.x + 12, max.y - 60 - h);
    ImGui::SetNextWindowPos(p);
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::SetNextWindowBgAlpha(m_chatOpen ? 0.45f : 0.2f);
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    ImGui::Begin("##chat", nullptr, f);
    m_chatMin = ImGui::GetWindowPos();
    m_chatMax = ImVec2(m_chatMin.x + ImGui::GetWindowWidth(), m_chatMin.y + ImGui::GetWindowHeight());
    float footer = (touch && !m_chatOpen) ? 0.0f : ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##lines", ImVec2(0, -footer), ImGuiChildFlags_None);
    size_t first = log.lines.size() > 40 ? log.lines.size() - 40 : 0;
    for (size_t i = first; i < log.lines.size(); ++i) {
        const auto& l = log.lines[i];
        if (l.system) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1), "%s", l.text.c_str());
            ImGui::PopTextWrapPos();
        } else if (l.whisper) {
            // Private messages: "{To Bob}" for ones you sent, "{From Alice}" for ones you got.
            const bool mine = l.from == Online::playerName();
            const ImVec4 pink(0.95f, 0.6f, 1.0f, 1);
            ImGui::TextColored(pink, "{%s %s}", mine ? "To" : "From", (mine ? l.to : l.from).c_str());
            if (!mine && (l.verified || l.admin)) { ImGui::SameLine(0, 3); Badges::check(ImGui::GetTextLineHeight() * 0.9f); }
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(ImVec4(0.98f, 0.88f, 1.0f, 1), "%s", l.text.c_str());
            ImGui::PopTextWrapPos();
            translatedTag(l);
        } else {
            if (l.admin) {
                Badges::icon(Badges::Id::Administrator, ImGui::GetTextLineHeight());
                ImGui::SameLine(0, 4);
            }
            ImGui::TextColored(l.admin ? ImVec4(1.0f, 0.85f, 0.4f, 1) : ImVec4(0.55f, 0.8f, 1.0f, 1), "%s", l.from.c_str());
            if (l.verified || l.admin) { ImGui::SameLine(0, 3); Badges::check(ImGui::GetTextLineHeight() * 0.9f); }
            ImGui::SameLine(0, 0);
            ImGui::TextColored(l.admin ? ImVec4(1.0f, 0.85f, 0.4f, 1) : ImVec4(0.55f, 0.8f, 1.0f, 1), ":");
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(l.text.c_str());
            ImGui::PopTextWrapPos();
            translatedTag(l);
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 5) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    if (m_chatOpen) {
        if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(touch ? -100 : -60);
        bool enter = ImGui::InputTextWithHint("##say", touch ? "Type a message" : "Type a message and press Enter (Esc to cancel)",
                                              &m_chatInput, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (enter || ImGui::Button("Send", ImVec2(touch ? 56 : -1, 0))) {
            sendChat(m_chatInput);
            m_chatInput.clear();
            m_chatOpen = false;
        }
        if (touch) {   // phones have no Esc key
            ImGui::SameLine();
            if (ImGui::Button("X", ImVec2(-1, 0))) { m_chatOpen = false; m_chatInput.clear(); }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) { m_chatOpen = false; m_chatInput.clear(); }
    } else if (Online::online() && Online::isGuest()) {
        // Greyed out, with the reason, like Roblox's guests.
        ImGui::BeginDisabled();
        static std::string none;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##guestsay", Online::kGuestChatText, &none, ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Guests can't chat. Leave the game and sign up (it's free) to talk to other players.");
    } else {
        if (!touch) ImGui::TextDisabled("Press / to chat");
    }
    ImGui::End();
}

bool PlayerApp::voiceHere() const {
    if (!GraphicsSettings::get().voiceChat || (Online::online() && Online::isGuest())) return false;
    if (m_server) return m_server->voiceAllowed() && m_server->playerCount() > 0;   // (nobody to talk to yet)
    if (m_client) return m_client->state() == NetClient::State::Joined && m_client->voiceAllowed();
    return false;
}

void PlayerApp::askForMicrophone() {
    if (m_micAsked) return;
    m_micAsked = true;
#ifdef __ANDROID__
    // Waits for the answer (and returns at once if it was already allowed).
    if (!SDL_AndroidRequestPermission("android.permission.RECORD_AUDIO"))
        Log::system("Voice chat: no permission for the microphone, so you can listen but not talk.");
#endif
}

// Every frame in a game: open or close the mic, push-to-talk (V) or the Talk button.
void PlayerApp::updateVoice(bool acceptInput) {
    const GraphicsSettings& gs = GraphicsSettings::get();
    const bool here = voiceHere();
    if (here) askForMicrophone();
    Voice::setActive(here);
    if (!here) m_talkOn = false;
    const bool held = here && (m_talkOn || (acceptInput && ImGui::IsKeyDown(ImGuiKey_V)));
    Voice::setTalking(held, here && gs.voiceOpenMic);
    Voice::setVolume(gs.voiceVolume);
    Voice::update(ImGui::GetIO().DeltaTime);
    m_touch.setMic(here && !gs.voiceOpenMic, m_talkOn);
}

// Bottom left, above the Menu button: how to talk, and a light while you are.
void PlayerApp::drawVoiceHud(ImDrawList* dl, ImVec2 min, ImVec2 max, bool touch) {
    (void)min;
    if (!voiceHere()) return;
    const GraphicsSettings& gs = GraphicsSettings::get();
    const bool talking = Voice::sending();
    const char* text = !Voice::micWorks() ? "Listening (no microphone)"
                     : talking            ? "Talking"
                     : gs.voiceOpenMic    ? "Open Mic"
                     : touch              ? "Tap the mic to talk"
                                          : "Hold V to talk";
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float h = 28.0f, w = ts.x + 74.0f;
    // Next to the Menu button (on phones, after the Menu, Chat and mic buttons along the top).
    const float bs = 46.0f * std::clamp(gs.touchSize, 0.5f, 2.0f);
    const ImVec2 a = touch ? ImVec2(min.x + 12 + 3 * (bs + 10), min.y + 12 + (bs - h) * 0.5f) : ImVec2(min.x + 124, max.y - 42);
    const ImVec2 b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, talking ? IM_COL32(30, 110, 50, 200) : IM_COL32(0, 0, 0, 140), h * 0.5f);
    Hud::drawSpeaker(dl, ImVec2(a.x + 18, a.y + h * 0.5f), 15.0f, talking, !Voice::micWorks());
    dl->AddText(ImVec2(a.x + 32, a.y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, 235), text);
    // A little meter: how loud the mic is.
    const float lv = Voice::micWorks() ? Voice::micLevel() : 0.0f;
    const ImVec2 m0(b.x - 34, a.y + 10), m1(b.x - 10, a.y + h - 10);
    dl->AddRectFilled(m0, m1, IM_COL32(255, 255, 255, 50), 3.0f);
    if (lv > 0.01f) dl->AddRectFilled(m0, ImVec2(m0.x + (m1.x - m0.x) * lv, m1.y), IM_COL32(120, 235, 140, 230), 3.0f);
}

std::vector<PlayerEntry> PlayerApp::currentPlayers() const {
    if (m_server || m_client) {
        std::vector<PlayerEntry> list = m_server ? m_server->players() : m_client->players();
        if (voiceHere()) {
            const std::string me = m_session->scripts().playerName();   // (the name the host gave us)
            for (PlayerEntry& e : list)
                e.voice = Voice::muted(e.name) ? 2
                        : (e.name == me || e.name == me + " (host)" ? Voice::sending() : Voice::speaking(e.name)) ? 1 : 0;
        }
        return list;
    }
    return {{Online::playerName(), Account::iAmStaff(), Badges::iHave(Badges::Id::Verified),
             m_session->scripts().leaderstats(Online::playerName()), m_iMadeThis}};
}

// The in-game menu, laid out like Roblox's: tabs along the top (Players,
// Settings, Help) and Reset / Leave / Resume along the bottom, each with its key.
void PlayerApp::drawPauseMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::GetWindowDrawList()->AddRectFilled(vp->WorkPos,
        ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y), IM_COL32(0, 0, 0, 150));

    // Keys, like Roblox: R resets, L leaves (both ask first), Esc resumes.
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_menuConfirm = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_L, false)) m_menuConfirm = 2;
    }

    const float w = fitWidth(640), h = std::min(470.0f, vp->WorkSize.y - 24.0f);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.13f, 0.15f, 0.96f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::IsWindowAppearing()) ImGui::SetWindowFocus();   // (so a controller's D-pad moves through it)

    if (m_menuConfirm != 0) {
        // "Are you sure?" page, like Roblox's.
        const bool reset = m_menuConfirm == 1;
        ImGui::Dummy(ImVec2(0, h * 0.22f));
        ImGui::SetWindowFontScale(1.5f);
        const char* q = reset ? "Are you sure you want to reset your character?" : "Are you sure you want to leave the game?";
        ImVec2 qs = ImGui::CalcTextSize(q);
        ImGui::SetCursorPosX(std::max(16.0f, (w - qs.x) * 0.5f));
        ImGui::TextUnformatted(q);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Dummy(ImVec2(0, 30));
        const float bw = std::min(200.0f, (w - 60) * 0.5f);
        ImGui::SetCursorPosX((w - bw * 2 - 16) * 0.5f);
        bool yes = bigButton(reset ? "Reset" : "Leave", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(bw, 44));
        ImGui::SameLine(0, 16);
        bool no = bigButton(reset ? "Don't Reset" : "Don't Leave",
                            ImVec4(0.3f, 0.3f, 0.35f, 1), ImVec2(bw, 44));
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) yes = true;
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        if (no) m_menuConfirm = 0;
        else if (yes) {
            m_menuConfirm = 0;
            m_paused = false;
            if (reset) { if (Player* p = m_scene->player()) p->kill(); }
            else leaveGame();
        }
        return;
    }

    // Tabs along the top.
    const char* tabs[] = {"Players", "Settings", "Help"};
    const float tw = (w - 32 - 16) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, 8);
        bool on = m_menuTab == i;
        if (bigButton(tabs[i], on ? kAccent : ImVec4(0.2f, 0.21f, 0.24f, 1), ImVec2(tw, 34))) m_menuTab = i;
    }
    ImGui::Spacing();

    const float bottom = 52.0f;
    ImGui::BeginChild("##menuBody", ImVec2(0, -bottom), false);
    if (m_menuTab == 0) {
        ImGui::TextDisabled("%s", m_currentTitle.c_str());
        if (m_server && m_server->relayed()) {
            if (!m_server->relayCode().empty())
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1), "Private server - code: %s", m_server->relayCode().c_str());
            else ImGui::TextDisabled(m_server->relayReady() ? "Public server" : "Starting the server...");
        } else if (m_server) ImGui::TextDisabled("Local network server");
        else if (!m_client) ImGui::TextDisabled("Offline - just you");
        ImGui::Spacing();
        for (const PlayerEntry& e : currentPlayers()) {
            ImGui::PushID(e.name.c_str());
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImVec2 z(a.x + ImGui::GetContentRegionAvail().x, a.y + 40);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, z, IM_COL32(40, 42, 48, 255), 6.0f);
            dl->AddCircleFilled(ImVec2(a.x + 22, a.y + 20), 13, IM_COL32(90, 150, 230, 255));
            float x = a.x + 44;
            dl->AddText(ImVec2(x, a.y + 12), IM_COL32(255, 255, 255, 255), e.name.c_str());
            x += ImGui::CalcTextSize(e.name.c_str()).x + 8;
            if (e.verified) { dl->AddText(ImVec2(x, a.y + 12), IM_COL32(80, 170, 255, 255), "[Verified]"); x += 74; }
            if (e.admin)    dl->AddText(ImVec2(x, a.y + 12), IM_COL32(255, 200, 70, 255), "[Staff]");
            if (e.name == Online::playerName()) {
                const char* you = "(you)";
                dl->AddText(ImVec2(z.x - ImGui::CalcTextSize(you).x - 12, a.y + 12), IM_COL32(170, 170, 180, 255), you);
            }
            ImGui::Dummy(ImVec2(0, 44));
            ImGui::PopID();
        }
    } else if (m_menuTab == 1) {
        // VR: show the game in a headset (SteamVR, Quest Link...) instead.
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("VR headset");
        ImGui::SameLine(190.0f);
#ifdef GB_QUEST
        ImGui::TextUnformatted(m_vrOn ? "Always on (this is the Quest version)" : "Couldn't start");
#else
        if (m_vrOn) { if (bigButton("Stop VR", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(160, 0))) stopVr(); }
        else if (bigButton("Play in VR", kAccent, ImVec2(160, 0))) { startVr(); if (m_vrOn) m_paused = false; }
#endif
        if (!m_vrStatus.empty()) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1), "%s", m_vrStatus.c_str());
        ImGui::Spacing();
        GraphicsSettings& gs = GraphicsSettings::get();
        bool changed = false;
        const float labelW = 190.0f;
        auto row = [&](const char* label) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(labelW);
            ImGui::SetNextItemWidth(-1);
        };
        auto onOff = [&](const char* label, bool& v) {
            row(label);
            ImGui::PushID(label);
            const float bw = (ImGui::GetContentRegionAvail().x - 8) * 0.5f;
            if (bigButton("On", v ? kGreen : ImVec4(0.2f, 0.21f, 0.24f, 1), ImVec2(bw, 0)) && !v) { v = true; changed = true; }
            ImGui::SameLine(0, 8);
            if (bigButton("Off", !v ? ImVec4(0.55f, 0.25f, 0.25f, 1) : ImVec4(0.2f, 0.21f, 0.24f, 1), ImVec2(bw, 0)) && v) { v = false; changed = true; }
            ImGui::PopID();
        };
        ImGui::SeparatorText("Camera");
        onOff("Shift Lock Switch", gs.shiftLockSwitch);
        row("Camera Sensitivity");
        changed |= ImGui::SliderFloat("##sens", &gs.mouseSensitivity, 0.1f, 4.0f, "%.1f");
        onOff("Invert Camera", gs.invertCamera);
        ImGui::SeparatorText("Sound and screen");
        row("Volume");
        int vol = (int)std::lround(gs.volume * 10.0f);
        if (ImGui::SliderInt("##vol", &vol, 0, 10)) { gs.volume = vol / 10.0f; changed = true; }
#ifndef GB_MOBILE
        onOff("Fullscreen", gs.fullscreen);
#endif
        row("Graphics Quality");
        static const char* q[] = {"Low", "Medium", "High", "Ultra", "Custom"};
        int qi = std::clamp(gs.quality, 0, 4);
        if (ImGui::Combo("##quality", &qi, q, 5)) { if (qi < 4) gs.applyPreset(qi); else gs.quality = qi; changed = true; }
        {
            // Render distance, like the old graphics bar: - [][][][][][][][][][] +
            row("Render Distance");
            ImGui::PushID("renderdist");
            const int maxRd = GraphicsSettings::kMaxRenderDistance;
            int rd = std::clamp(gs.renderDistance, 1, maxRd);
            const float h = ImGui::GetFrameHeight();
            if (ImGui::Button("-", ImVec2(h, h)) && rd > 1) rd--;
            ImGui::SameLine(0, 6);
            const float gap = 3.0f;
            const float barW = std::max(40.0f, ImGui::GetContentRegionAvail().x - h - 6.0f);
            const float cell = (barW - gap * (maxRd - 1)) / maxRd;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 at = ImGui::GetCursorScreenPos();
            for (int i = 0; i < maxRd; ++i) {
                ImGui::SetCursorScreenPos(ImVec2(at.x + i * (cell + gap), at.y));
                ImGui::PushID(i);
                if (ImGui::InvisibleButton("seg", ImVec2(cell, h))) rd = i + 1;
                const bool hover = ImGui::IsItemHovered();
                ImGui::PopID();
                const ImU32 col = i < rd ? (i == maxRd - 1 ? IM_COL32(90, 200, 120, 255) : IM_COL32(80, 160, 240, 255))
                                         : (hover ? IM_COL32(80, 84, 95, 255) : IM_COL32(55, 58, 66, 255));
                dl->AddRectFilled(ImVec2(at.x + i * (cell + gap), at.y + 2), ImVec2(at.x + i * (cell + gap) + cell, at.y + h - 2), col, 3.0f);
            }
            ImGui::SetCursorScreenPos(ImVec2(at.x + barW + 6.0f, at.y));
            if (ImGui::Button("+", ImVec2(h, h)) && rd < maxRd) rd++;
            ImGui::PopID();
            if (rd != gs.renderDistance) { gs.renderDistance = rd; gs.quality = GraphicsSettings::Custom; changed = true; }
            ImGui::SetCursorPosX(labelW);
            if (rd >= maxRd) ImGui::TextDisabled("Max: everything is drawn");
            else ImGui::TextDisabled("%d studs (lower = faster)", (int)gs.renderDistanceStuds());
        }
        row("Water Quality");
        static const char* wq[] = {"Low", "Medium", "High", "Ultra"};
        if (ImGui::Combo("##water", &gs.waterQuality, wq, 4)) { gs.quality = GraphicsSettings::Custom; changed = true; }
        row("Graphics API");
        static const int startedApi = gs.graphicsApi;
        changed |= ImGui::Combo("##api", &gs.graphicsApi, GraphicsSettings::apiNames(), 4);
        if (!GraphicsSettings::activeApi().empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Using %s", GraphicsSettings::activeApi().c_str());
            ImGui::PopTextWrapPos();
        }
        if (gs.graphicsApi != startedApi)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "Close and reopen Guts&Bolts to switch.");
        onOff("Show FPS", gs.showFps);
        ImGui::SeparatorText("Voice chat");
        if (Online::online() && Online::isGuest()) {
            ImGui::TextDisabled("Sign up to use voice chat.");
        } else {
            onOff("Voice Chat", gs.voiceChat);
            if (gs.voiceChat) {
                onOff("Open Mic", gs.voiceOpenMic);
                ImGui::SetCursorPosX(labelW);
                ImGui::TextDisabled(gs.voiceOpenMic ? "Sends whenever you make a sound." : "Off: hold V (or tap the mic) to talk.");
                row("Voice Volume");
                int vv = (int)std::lround(gs.voiceVolume * 10.0f);
                if (ImGui::SliderInt("##voicevol", &vv, 0, 20)) { gs.voiceVolume = vv / 10.0f; changed = true; }
            }
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Voices aren't filtered like the chat. Click someone on the player list to mute them. "
                                "Games can turn voice chat off.");
            ImGui::PopTextWrapPos();
        }
        ImGui::SeparatorText("Chat");
        onOff("Translate Chat", gs.translateChat);
        ImGui::SetCursorPosX(labelW);
        ImGui::PushTextWrapPos(0);
        if (const char* me = Lang::name(ChatLog::myLanguage()); *me)
            ImGui::TextDisabled("Messages in other languages show in %s (your computer's language).", me);
        else
            ImGui::TextDisabled("Your computer's language can't be translated to yet, so chat shows as it was written.");
        ImGui::PopTextWrapPos();
        ImGui::SeparatorText("Other");
        onOff("Blood and Gore", gs.allowGore);
        row("Touch Controls");
        static const char* tm[] = {"Automatic", "Always on", "Off"};
        changed |= ImGui::Combo("##touch", &gs.touchControls, tm, 3);
        ImGui::Spacing();
        if (bigButton("Advanced graphics...", ImVec4(0.3f, 0.3f, 0.35f, 1))) m_showSettings = true;
        if (changed) gs.save();
    } else {
        auto key = [](const char* k, const char* what) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1), "%-12s", k);
            ImGui::SameLine(130);
            ImGui::TextUnformatted(what);
        };
        ImGui::SeparatorText("Moving");
        key("W A S D", "Walk");
        key("Space", "Jump (again in the air to double-jump, if the game allows it)");
        key("Shift", "Shift Lock: camera over your shoulder (turn on in Settings)");
        key("C / Ctrl", "Swimming: dive (or swim forward looking down; Space goes up)");
        key("V", "Hold to talk (voice chat, once it's on in Settings)");
        ImGui::SeparatorText("Camera");
        key("Right mouse", "Hold and drag to look around");
        key("Mouse wheel", "Zoom in and out; all the way in is first person");
        ImGui::SeparatorText("Other");
        key("/ or Enter", "Chat  (/w name message whispers to one player)");
        key("1 - 9", "Equip a tool from your hotbar");
        key("Esc", "Open or close this menu");
        ImGui::SeparatorText("Controller");
        key("Left stick", "Walk");
        key("Right stick", "Look around (click it for Shift Lock)");
        key("A / Cross", "Jump");
        key("LT / L2", "Swimming: dive");
        key("RT / R2", "Use the tool you're holding, or click what's in the middle of the screen");
        key("LB / RB", "Last / next tool;  B / Circle puts it away");
        key("X / Square", "Use the prompt you're next to (\"Open\", \"Talk\"...)");
        key("D-pad", "Up and down zoom in and out");
        key("Start", "Open or close this menu (the D-pad and A pick in it)");
        key("Back / Select", "Show or hide the player list");
        ImGui::SeparatorText("VR headset (Settings > Play in VR)");
        key("Left stick", "Walk the way you look");
        key("Right stick", "Turn (left / right)");
        key("A", "Jump");
        key("Right trigger", "Use the tool you're holding");
        key("Your hands", "Your character's arms reach for your controllers, and its head turns with yours");
        key("Menu", "Open or close this menu");
        key("Point + trigger", "Click buttons on the floating screen (point with your right hand)");
        key("Typing", "A keyboard shows on the floating screen: point at a key and pull the trigger");
    }
    ImGui::EndChild();

    // Bottom buttons, like Roblox: [R] Reset Character, [L] Leave Game, [Esc] Resume Game.
    const float bw = (w - 32 - 16) / 3.0f;
    if (bigButton("[R]  Reset Character", ImVec4(0.3f, 0.3f, 0.35f, 1), ImVec2(bw, 40))) m_menuConfirm = 1;
    ImGui::SameLine(0, 8);
    if (bigButton("[L]  Leave Game", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(bw, 40))) m_menuConfirm = 2;
    ImGui::SameLine(0, 8);
    if (bigButton("[Esc]  Resume Game", kGreen, ImVec2(bw, 40))) m_paused = false;
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Touch controls
// ---------------------------------------------------------------------------

// A controller's camera: the right stick turns it, the D-pad zooms (up = in), and
// clicking the right stick switches Shift Lock (when it's allowed).
void PlayerApp::padCamera(float dt) {
    const glm::vec2 rs = Pad::rightStick();
    if (rs.x != 0.0f || rs.y != 0.0f) PlayCamera::turn(m_camera, rs.x * 900.0f * dt, -rs.y * 600.0f * dt);
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true))   PlayCamera::zoom(m_camera, 1.0f, m_scene->cameraOptions());
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true)) PlayCamera::zoom(m_camera, -1.0f, m_scene->cameraOptions());
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR3, false) && GraphicsSettings::get().shiftLockSwitch &&
        !PlayCamera::firstPerson(m_camera))
        m_shiftLock = !m_shiftLock;
}

void PlayerApp::updateTouch(ImVec2 min, ImVec2 max, bool acceptInput) {
    const float scale = GraphicsSettings::get().touchSize;
    m_touch.begin(min, max, scale);
    m_touch.setEmoteShown(!m_session->runOnly() && EmoteMenu::available(*m_scene));
    std::vector<std::pair<ImVec2, ImVec2>> blocked = GameGui::scrollerRects(*m_scene);   // fingers there scroll the UI
    blocked.push_back({m_chatMin, m_chatMax});
    m_touch.setBlocked(std::move(blocked));
    // Scripts' action buttons (ContextActionService), next to the jump button.
    std::vector<TouchControls::ActionButton> buttons;
    for (const ScriptEngine::ActionButton& b : m_session->scripts().actionButtons())
        buttons.push_back({b.name, b.title, b.image, b.hasPosition, b.xScale, b.xOffset, b.yScale, b.yOffset});
    m_touch.setActionButtons(std::move(buttons));
    if (m_window->hasTouchScreen()) {
        // A real touch screen: every finger counts (thumbstick + look + jump at once).
        std::vector<TouchControls::Finger> f;
        for (const TouchPoint& t : m_window->touches()) f.push_back({(int)t.id, ImVec2(t.x, t.y), true});
        m_touch.feed(f, acceptInput);
    } else if (!m_opts.touchTest.empty()) {
        // Test helper: pretend fingers are on the screen.
        std::vector<TouchControls::Finger> f;
        int k = m_frame - 40;
        if (k > 0 && m_opts.touchTest == "stick") {
            ImVec2 base(min.x + 150, max.y - 150);
            float push = std::min(1.0f, k / 6.0f);
            f.push_back({1, ImVec2(base.x + 12 * push, base.y - 70 * push), true});
        }
        if (k > 0 && m_opts.touchTest == "jump" && (k / 15) % 2 == 0)
            f.push_back({2, ImVec2(max.x - 105 * scale, max.y - 105 * scale), true});
        if (k > 0 && m_opts.touchTest == "look")
            f.push_back({3, ImVec2((min.x + max.x) * 0.5f + k * 4.0f, (min.y + max.y) * 0.4f), true});
        m_touch.feed(f);
    } else {
        m_touch.feedMouse(acceptInput && ImGui::IsWindowHovered());
    }
    // Scripts hear the fingers first: action buttons, and Touch for fingers on the game view
    // (an action that sinks one keeps it from turning the camera or tapping).
    ScriptEngine& scripts = m_session->scripts();
    for (const auto& e : m_touch.actionEvents()) scripts.actionButtonInput(e.name, e.down, glm::vec2(e.pos.x - min.x, e.pos.y - min.y));
    for (const auto& e : m_touch.fingerEvents()) {
        static const char* states[] = {"Begin", "Change", "End"};
        if (scripts.touchInput(e.id, states[e.state], glm::vec2(e.pos.x - min.x, e.pos.y - min.y)) && e.state != 2)
            m_touch.sinkFinger(e.id);
    }
    if (!acceptInput) { m_session->setTouchInput(glm::vec2(0.0f), false); return; }
    m_session->setTouchInput(m_touch.move(), m_touch.jump());
    ImVec2 look = m_touch.look();
    if ((look.x != 0.0f || look.y != 0.0f) && !m_camera.noInput) m_camera.orbit(look.x, look.y);
    if (m_touch.zoom() != 0.0f) PlayCamera::zoom(m_camera, m_touch.zoom(), m_scene->cameraOptions());
    if (m_touch.chatPressed()) m_chatOpen = true;
    if (m_touch.menuPressed()) m_paused = true;
    if (m_touch.micPressed()) m_talkOn = !m_talkOn;
    if (m_touch.emotePressed()) EmoteMenu::toggle(*m_scene);
}

// ---------------------------------------------------------------------------
// Account, badges and staff tools
// ---------------------------------------------------------------------------

void PlayerApp::drawAccount() {
    Profile& me = Profile::get();
    ImGui::SeparatorText("Badges");
    auto badges = Badges::verified(Account::id(), me.grants);
    if (badges.empty()) {
        ImGui::TextDisabled("No official badges yet.");
    } else {
        for (size_t i = 0; i < badges.size(); ++i) {
            if (i > 0) ImGui::SameLine(0, 16);
            ImGui::BeginGroup();
            Badges::icon(badges[i], 56.0f);
            ImGui::TextUnformatted(Badges::info(badges[i]).name);
            ImGui::EndGroup();
        }
    }

    ImGui::SeparatorText("Your account");
    if (Online::online() && Online::me().value("userId", 0LL) > 0) {
        const nlohmann::json& om = Online::me();
        ImGui::Text("Logged in as @%s", om.value("username", std::string()).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(user #%lld)", om.value("userId", 0LL));
        if (om.value("hasPassword", false)) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Log out")) ImGui::OpenPopup("##logout");
            if (ImGui::BeginPopup("##logout")) {
                ImGui::TextUnformatted("Log out of this device? You can log back in with your username and password.");
                if (ImGui::Button("Yes, log out")) { ImGui::CloseCurrentPopup(); logOut(); }
                ImGui::EndPopup();
            }
            // Delete my account (account.delete, the same as the website's Settings): the password proves it's you.
            if (!om.value("official", false)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete account...")) { m_deleteName.clear(); m_deletePassword.clear(); m_deleteMsg.clear(); ImGui::OpenPopup("##deleteaccount"); }
                if (ImGui::BeginPopup("##deleteaccount")) {
                    const std::string username = om.value("username", std::string());
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28);
                    ImGui::TextUnformatted("Delete your account for good? Your password, avatar, About me, posts, outfits, messages, "
                                           "friends, follows, favourites and notifications are deleted. It can't be undone.");
                    ImGui::TextDisabled("Games and items you made stay up, shown as made by \"Deleted user\". Your username and number are "
                                        "never given to anyone else. Limited copies you own stay with the deleted account and can't be traded.");
                    ImGui::PopTextWrapPos();
                    ImGui::SetNextItemWidth(220);
                    ImGui::InputTextWithHint("##delname", ("Type " + username + " to say you're sure").c_str(), &m_deleteName);
                    ImGui::SetNextItemWidth(220);
                    ImGui::InputTextWithHint("##delpw", "Your password", &m_deletePassword, ImGuiInputTextFlags_Password);
                    ImGui::BeginDisabled(m_deleteBusy || m_deleteName != username || m_deletePassword.empty());
                    if (Classic::button("Delete my account", ImVec4(0.78f, 0.16f, 0.16f, 1.0f))) {
                        m_deleteBusy = true;
                        m_deleteMsg = "Deleting...";
                        Online::request("account.salt", {{"username", username}}, [this](const nlohmann::json& r) {
                            std::string lock, auth;
                            if (!r.value("ok", false) || !Account::passwordKeys(m_deletePassword, r.value("salt", std::string()), lock, auth)) {
                                m_deleteBusy = false;
                                m_deleteMsg = r.value("error", std::string("Something went wrong. Try again."));
                                return;
                            }
                            Online::request("account.delete", {{"auth", auth}}, [this](const nlohmann::json& d) {
                                m_deleteBusy = false;
                                m_deletePassword.clear();
                                if (!d.value("ok", false)) { m_deleteMsg = d.value("error", std::string("Couldn't delete it.")); return; }
                                m_deleteMsg.clear();
                                logOut();   // this device becomes a new, empty account
                            });
                        });
                    }
                    ImGui::EndDisabled();
                    if (!m_deleteMsg.empty()) ImGui::TextWrapped("%s", m_deleteMsg.c_str());
                    ImGui::EndPopup();
                }
            }
        } else {
            ImGui::TextColored(ImVec4(0.75f, 0.45f, 0.0f, 1), "No password yet, so you can only use this account on this device.");
            if (Classic::button("Set a password", Classic::kBlue)) {
                m_page = Page::Login;
                m_loginTab = 0;
                m_loginUser = om.value("username", std::string());
            }
        }
    }
    if (Online::online() && Online::me().value("userId", 0LL) > 0) {   // privacy (worker/server.js account.privacy)
        ImGui::SeparatorText("Privacy");
        const nlohmann::json& om = Online::me();
        const nlohmann::json pv = om.contains("privacy") && om["privacy"].is_object() ? om["privacy"] : nlohmann::json::object();
        static const char* kKeys[] = {"everyone", "friends", "nobody"};
        auto pick = [&](const char* label, const char* field, const char* const* names) {
            std::string cur = pv.contains(field) && pv[field].is_string() ? pv[field].get<std::string>() : "everyone";
            int at = 0;
            for (int i = 0; i < 3; ++i) if (cur == kKeys[i]) at = i;
            ImGui::SetNextItemWidth(220);
            if (ImGui::Combo(label, &at, names, 3))
                Online::request("account.privacy", {{field, kKeys[at]}}, [](const nlohmann::json&) {});
        };
        static const char* kSee[] = {"Everyone", "Friends only", "No one (appear offline)"};
        static const char* kJoin[] = {"Everyone", "Friends only", "No one"};
        pick("Who sees me online##pvs", "status", kSee);
        pick("Who can join me##pvj", "join", kJoin);
        pick("Who can message me##pvm", "messages", kJoin);
        pick("Who sees my inventory##pvi", "inventory", kJoin);   // (and can trade with me)
        drawBlockedList();
    }
    ImGui::Text("Account ID: %s...", Account::shortId().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy full ID")) ImGui::SetClipboardText(Account::id().c_str());
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Staff need this ID to give you a badge. It's safe to share - the secret key that "
                        "proves it's you never leaves this computer.");
    ImGui::PopTextWrapPos();
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##redeem", "Paste a badge code", &m_redeemCode);
    ImGui::SameLine();
    if (Classic::button("Redeem", Classic::kBlue)) {
        Badges::redeem(m_redeemCode, m_redeemMsg);
        m_redeemCode.clear();
    }
    if (!m_redeemMsg.empty()) ImGui::TextWrapped("%s", m_redeemMsg.c_str());
}

void PlayerApp::drawStaff() {
    if (!Badges::canVerify()) { m_page = Page::Home; return; }
    const bool official = Account::iAmStaff();
    Badges::icon(official ? Badges::Id::Administrator : Badges::Id::Staff, 40.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Staff Tools");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled(official ? "Only the official Guts account can see all of this page."
                                 : "You're Staff: you can verify people.");
    ImGui::EndGroup();

    ImGui::SeparatorText(official ? "Give someone a badge" : "Verify someone");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Ask them for their account ID (Avatar page > Copy full ID), make a code here and send "
                        "it to them. The code only works for their account. (When everyone uses a Guts&Bolts "
                        "server, you can verify people straight from the server section below instead.)");
    ImGui::PopTextWrapPos();
    std::vector<const char*> names;
    std::vector<int> ids;
    for (int i = 0; i < (int)Badges::Id::Count; ++i) {
        const Badges::Info& in = Badges::info((Badges::Id)i);
        if (in.grantable && (official || in.staffCanGive)) { names.push_back(in.name); ids.push_back(i); }
    }
    int cur = 0;
    for (int k = 0; k < (int)ids.size(); ++k) if (ids[k] == m_grantBadge) cur = k;
    Badges::icon((Badges::Id)ids[cur], 32.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::Combo("Badge", &cur, names.data(), (int)names.size())) m_grantBadge = ids[cur];
    m_grantBadge = ids[cur];
    ImGui::SetNextItemWidth(520);
    ImGui::InputTextWithHint("Their account ID", "64 letters and numbers", &m_grantTo);
    if (Classic::button("Make badge code", Classic::kPlay, ImVec2(180, 30))) {
        m_grantError.clear();
        m_grantCode = Badges::makeCode((Badges::Id)m_grantBadge, m_grantTo, m_grantError);
    }
    if (!m_grantError.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_grantError.c_str());
    if (!m_grantCode.empty()) {
        ImGui::InputTextMultiline("##code", &m_grantCode, ImVec2(520, 60), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy code")) ImGui::SetClipboardText(m_grantCode.c_str());
    }

    if (Online::online()) drawOnlineStaff();   // verify people straight from the server
    if (!official) return;   // the rest is for the official account only

    // The sample games that come with Guts&Bolts, put on the server as official games
    // (so the website has them for everyone, not just people who installed the apps).
    if (Online::online()) {
        ImGui::SeparatorText("Official games on the website");
        int waiting = 0;
        for (const GameCard& g : m_games)
            if (!g.broken && g.info.author == "Guts and Bolts" && g.info.publishedId.empty()) ++waiting;
        ImGui::PushTextWrapPos(0);
        if (waiting)
            ImGui::TextDisabled("%d sample game%s on this computer %s not on the server yet.", waiting,
                                waiting == 1 ? "" : "s", waiting == 1 ? "is" : "are");
        else
            ImGui::TextDisabled("All the sample games on this computer are on the server.");
        ImGui::PopTextWrapPos();
        if (waiting && Classic::button("Put the sample games online", Classic::kPlay, ImVec2(240, 30))) {
            m_staffMsg = "Publishing the sample games...";
            for (const GameCard& g : m_games) {
                if (g.broken || g.info.author != "Guts and Bolts" || !g.info.publishedId.empty()) continue;
                publishGameFile(g.path, g.thumb ? g.thumb->toPng() : std::string(),
                                [this](bool, const std::string& msg) { m_staffMsg = msg; });
            }
        }
    }

    ImGui::SeparatorText("Give someone Bolts");
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Same idea as badges: make a code for their account ID and send it to them. Each code "
                        "works once, and only for that account.");
    ImGui::PopTextWrapPos();
    Bolts::drawIcon(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + 14, ImGui::GetCursorScreenPos().y + 14), 26.0f);
    ImGui::Dummy(ImVec2(28, 28));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputInt("Bolts", &m_giveBolts, 25, 100)) m_giveBolts = std::clamp(m_giveBolts, 1, 1000000);
    ImGui::SetNextItemWidth(520);
    ImGui::InputTextWithHint("Their account ID##bolts", "64 letters and numbers", &m_giveBoltsTo);
    if (Classic::button("Make Bolts code", Classic::kPlay, ImVec2(180, 30))) {
        m_giveBoltsError.clear();
        m_giveBoltsCode = Bolts::makeCode(m_giveBolts, m_giveBoltsTo, m_giveBoltsError);
    }
    if (!m_giveBoltsError.empty()) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "%s", m_giveBoltsError.c_str());
    if (!m_giveBoltsCode.empty()) {
        ImGui::InputTextMultiline("##boltscode", &m_giveBoltsCode, ImVec2(520, 60), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Copy code##bolts")) ImGui::SetClipboardText(m_giveBoltsCode.c_str());
    }

    ImGui::SeparatorText("Catalog");
    ImGui::Text("%d item(s) in the catalog.", (int)m_items.size());
    ImGui::TextDisabled("New hats are made in Studio's Accessory window; shirts and pants are uploaded from their templates.");

    ImGui::SeparatorText("Your staff key");
    ImGui::PushTextWrapPos(0);
    ImGui::TextWrapped("Your secret key is %s. Back it up somewhere safe and never share it - whoever has it "
                       "is the Guts account.", (Account::folder() / "account.key").string().c_str());
    ImGui::PopTextWrapPos();
    ImGui::Text("Official ID: %s...", Account::shortId().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy official ID")) ImGui::SetClipboardText(Account::id().c_str());
}

// The Bolts page: your balance, the daily reward, ways to earn, codes and history.
void PlayerApp::drawBolts() {
    if (Online::online()) { drawOnlineBolts(); return; }   // Bolts kept on the server
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    Bolts::drawIcon(dl, ImVec2(p.x + 30, p.y + 30), 58.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 72, p.y + 2));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Bolts");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("The Guts&Bolts currency. Earn them, then spend them in the Catalog.");
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + 66, ImGui::GetItemRectMax().y + 8)));

    // Balance card.
    {
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = std::min(ImGui::GetContentRegionAvail().x, 420.0f);
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + 70), IM_COL32(255, 248, 225, 255), 8.0f);
        dl->AddRect(a, ImVec2(a.x + w, a.y + 70), IM_COL32(225, 180, 60, 255), 8.0f);
        ImGui::SetCursorScreenPos(ImVec2(a.x + 14, a.y + 8));
        ImGui::TextDisabled("Your balance");
        ImGui::SetCursorScreenPos(ImVec2(a.x + 14, a.y + 28));
        ImGui::SetWindowFontScale(1.6f);
        Bolts::amount(Bolts::balance(), 30.0f);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + 80));
    }
    if (Bolts::wasReset()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.1f, 1),
                           "Your Bolts file had been edited by hand, so your Bolts were reset. (Bolts are signed "
                           "with your account key, so they can't be typed in.)");
        ImGui::PopTextWrapPos();
    }

    // Daily reward.
    ImGui::SeparatorText("Daily reward");
    if (Bolts::canClaimDaily()) {
        if (Classic::button(("Claim " + std::to_string(Bolts::kDaily) + " Bolts").c_str(), Classic::kPlay, ImVec2(200, 34)))
            Bolts::claimDaily(m_boltsMsg);
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Claimed today", ImVec2(200, 34));
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Next one in %s", Bolts::timeUntilDaily().c_str());
    }
    if (!m_boltsMsg.empty()) ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.15f, 1), "%s", m_boltsMsg.c_str());

    // Ways to earn.
    ImGui::SeparatorText("Ways to earn");
    ImGui::PushTextWrapPos(0);
    ImGui::Bullet(); ImGui::TextWrapped("Come back every day: %lld Bolts.", Bolts::kDaily);
    ImGui::Bullet(); ImGui::TextWrapped("Play games: %lld Bolts for every %d minutes (up to %lld a day - %lld so far today).",
                                        Bolts::kPlayReward, (int)(Bolts::kPlaySeconds / 60), Bolts::kPlayDailyCap,
                                        Bolts::earnedFromPlayToday());
    ImGui::Bullet(); ImGui::TextWrapped("Get a Bolts code from the Guts&Bolts staff (contests, helping out, finding bugs...).");
    ImGui::PopTextWrapPos();

    // Redeem a code.
    ImGui::SeparatorText("Redeem a Bolts code");
    ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x - 90));
    ImGui::InputTextWithHint("##boltscode", "BOLTS-...", &m_boltsCode);
    ImGui::SameLine();
    if (Classic::button("Redeem", Classic::kBlue)) {
        Bolts::redeem(m_boltsCode, m_boltsMsg);
        m_boltsCode.clear();
    }
    ImGui::TextDisabled("Your account ID: %s...", Account::shortId().c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy full ID")) ImGui::SetClipboardText(Account::id().c_str());

    // History, newest first.
    ImGui::SeparatorText("History");
    const auto& h = Bolts::history();
    if (h.empty()) ImGui::TextDisabled("Nothing yet.");
    if (ImGui::BeginTable("##boltshistory", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        const bool narrow = ImGui::GetContentRegionAvail().x < 560;
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, narrow ? 64.0f : 150.0f);
        ImGui::TableSetupColumn("What");
        ImGui::TableSetupColumn("Bolts", ImGuiTableColumnFlags_WidthFixed, narrow ? 64.0f : 90.0f);
        int shown = 0;
        for (auto it = h.rbegin(); it != h.rend() && shown < 40; ++it, ++shown) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            std::time_t t = (std::time_t)it->time;
            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &t);
#else
            localtime_r(&t, &tm);
#endif
            char when[32];
            std::strftime(when, sizeof(when), narrow ? "%b %d" : "%b %d, %H:%M", &tm);
            ImGui::TextDisabled("%s", when);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", it->reason.c_str());
            ImGui::TableNextColumn();
            if (it->amount >= 0) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "+%s", Bolts::format(it->amount).c_str());
            else ImGui::TextColored(ImVec4(0.75f, 0.2f, 0.15f, 1), "%s", Bolts::format(it->amount).c_str());
        }
        ImGui::EndTable();
    }
}

void PlayerApp::drawNotice() {
    static bool opened = false;
    if (m_notice.empty()) { opened = false; return; }
    if (!opened) { ImGui::OpenPopup("Guts&Bolts##notice"); opened = true; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(620), 0));
    if (ImGui::BeginPopupModal("Guts&Bolts##notice", nullptr, ImGuiWindowFlags_NoResize)) {
        if (tappedOutside()) { m_notice.clear(); m_noticePlain = false; ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
        ImGui::TextWrapped("%s", m_notice.c_str());
        ImGui::Spacing();
        if (!m_noticePlain) {
            if (ImGui::Button("Copy my account ID", ImVec2(200, 32))) ImGui::SetClipboardText(Account::id().c_str());
            ImGui::SameLine();
        }
        if (bigButton("OK", kGreen, ImVec2(100, 32))) { m_notice.clear(); m_noticePlain = false; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------------------
// Catalog
// ---------------------------------------------------------------------------


void PlayerApp::drawCatalog() {
    if (Online::online()) { drawOnlineCatalog(); return; }   // the server's catalog
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Catalog");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Hats, shirts and pants for your avatar.");
    ImGui::Spacing();

    const char* tabs[] = {"All", "Hats", "Shirts", "Pants"};
    float tabW = std::min(90.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4.0f);
    for (int i = 0; i < 4; ++i) {
        if (i > 0) ImGui::SameLine();
        bool on = m_itemType == i - 1;
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(tabW, 28)) : ImGui::Button(tabs[i], ImVec2(tabW, 28)))
            m_itemType = i - 1;
    }
    if (!m_catalogMsg.empty()) ImGui::TextWrapped("%s", m_catalogMsg.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    std::vector<int> list;
    for (int i = 0; i < (int)m_items.size(); ++i)
        if (m_itemType < 0 || (int)m_items[i].type == m_itemType) list.push_back(i);

    if (list.empty()) {
        ImGui::Dummy(ImVec2(0, 40));
        const char* a = m_items.empty() ? "The catalog is empty right now." : "Nothing in this section yet.";
        const char* b = "New items are on the way - check back soon!";
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetWindowFontScale(1.3f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::CalcTextSize(a).x) * 0.5f);
        ImGui::TextUnformatted(a);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (w - ImGui::CalcTextSize(b).x) * 0.5f);
        ImGui::TextDisabled("%s", b);
        return;
    }

    const float tile = 150.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, (int)((avail + 14) / (tile + 14)));
    for (size_t k = 0; k < list.size(); ++k) {
        const Catalog::Item& it = m_items[list[k]];
        if (k % perRow != 0) ImGui::SameLine(0, 14);
        ImGui::PushID(list[k]);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##item", ImVec2(tile, tile))) m_openItem = list[k];
        bool hover = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(255, 255, 255, 255));
        dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(160, 165, 175, 255),
                    0, 0, hover ? 2.0f : 1.0f);
        itemPicture(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.8f, it);
        if (Catalog::isWearing(it))
            dl->AddText(ImVec2(p.x + 6, p.y + 4), IM_COL32(20, 140, 60, 255), "Wearing");
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextColored(Classic::kLink, "%s", it.name.c_str());
        ImGui::PopTextWrapPos();
        if (Catalog::owns(it) && it.price > 0) ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Owned");
        else if (it.price > 0) Bolts::amount(it.price);
        else ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "Free");
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

void PlayerApp::drawItemDialog() {
    if (m_openItem >= (int)m_items.size()) m_openItem = -1;
    if (m_openItem >= 0 && !ImGui::IsPopupOpen("Catalog Item")) ImGui::OpenPopup("Catalog Item");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(520), 0));
    if (!ImGui::BeginPopupModal("Catalog Item", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (tappedOutside()) m_openItem = -1;
    if (m_openItem < 0) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    Catalog::Item it = m_items[m_openItem];

    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(180, 180));
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 180, p.y + 180), IM_COL32(245, 246, 250, 255), 6);
    itemPicture(ImGui::GetWindowDrawList(), ImVec2(p.x + 90, p.y + 90), 150, it);
    ImGui::SameLine(0, 18);
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted(it.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("%s  -  by Guts", Catalog::typeName(it.type));
    const bool owned = Catalog::owns(it);
    if (it.price == 0) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "Free");
    else if (owned) ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "You own this");
    else Bolts::amount(it.price, 20.0f);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(it.description.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::Spacing();

    bool wearing = Catalog::isWearing(it);
    if (it.price > 0 && !owned) {
        // Buy it with Bolts.
        long long have = Bolts::balance();
        std::string label = "Buy for " + Bolts::format(it.price);
        ImGui::BeginDisabled(have < it.price);
        if (bigButton(label.c_str(), kGreen, ImVec2(170, 34))) {
            if (Catalog::buy(it, m_buyMsg)) {
                Catalog::wear(it);
                if (Player* pl = m_avatarScene->player()) Profile::get().applyTo(*pl);
            }
        }
        ImGui::EndDisabled();
        if (have < it.price && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("You need %s more Bolts", Bolts::format(it.price - have).c_str());
    } else {
        ImGui::BeginDisabled(wearing);
        if (bigButton(wearing ? "Wearing" : "Wear", kGreen, ImVec2(140, 34))) {
            if (it.price == 0) Catalog::buy(it, m_buyMsg);   // free: it's yours
            Catalog::wear(it);
            if (Player* pl = m_avatarScene->player()) Profile::get().applyTo(*pl);
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 34))) { m_openItem = -1; m_buyMsg.clear(); ImGui::CloseCurrentPopup(); }
    if (Account::iAmStaff()) {
        ImGui::SameLine();
        if (bigButton("Remove from catalog", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(0, 34))) {
            Catalog::remove(it, m_catalogMsg);
            m_items = Catalog::load();
            m_openItem = -1;
            ImGui::CloseCurrentPopup();
        }
    }
    // Your balance, and what happened when you tried to buy.
    ImGui::Spacing();
    ImGui::TextDisabled("You have");
    ImGui::SameLine();
    Bolts::amount(Bolts::balance());
    if (it.price > 0 && !owned && Bolts::balance() < it.price) {
        ImGui::SameLine();
        ImGui::TextDisabled("- earn more on the Bolts page");
    }
    if (!m_buyMsg.empty()) ImGui::TextWrapped("%s", m_buyMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Touch scrolling: drag a list up and down with your finger, like a phone.
// ---------------------------------------------------------------------------

void PlayerApp::touchScroll() {
    if (!m_window->hasTouchScreen()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || io.WantTextInput) return;
    ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
    if (std::abs(drag.y) < 12.0f || std::abs(drag.y) < std::abs(drag.x) * 1.5f) return;   // not a vertical swipe
    // The scrollable window under the finger (or the one it's inside).
    ImGuiWindow* w = g.HoveredWindow;
    while (w && w->ScrollMax.y <= 0.0f && (w->Flags & ImGuiWindowFlags_ChildWindow)) w = w->ParentWindow;
    if (!w || w->ScrollMax.y <= 0.0f) return;
    ImGui::SetScrollY(w, std::clamp(w->Scroll.y - io.MouseDelta.y, 0.0f, w->ScrollMax.y));
    if (g.ActiveId != 0) ImGui::ClearActiveID();   // a swipe isn't a tap on whatever it started on
}

// The menu under a name on the leaderboard: friend them, or follow them.
void PlayerApp::drawPlayerMenu() {
    if (m_listMenu.empty()) return;
    const std::string who = m_listMenu;
    ImGui::SetNextWindowPos(m_listMenuAt);
    ImGui::SetNextWindowSize(ImVec2(190, 0));
    Classic::pushLight();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    bool keep = true;
    ImGui::Begin("##playermenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextUnformatted(who.c_str());
    ImGui::Separator();
    auto send = [this, who](const char* op, const char* done) {
        std::string doneMsg = done;
        Online::request(op, {{"user", who}}, [this, who, doneMsg](const nlohmann::json& r) {
            if (m_listMenu != who) return;
            m_listMsg = r.value("ok", false) ? doneMsg : r.value("error", std::string("That didn't work."));
            Online::request("friends.relation", {{"user", who}}, [this, who](const nlohmann::json& rel) {
                if (m_listMenu == who) m_listRel = rel;
            });
        });
    };
    const ImVec2 full(-1, 26);
    if (Online::isGuest()) {
        ImGui::TextWrapped("Sign up to add friends and follow people.");
    } else if (m_listRel.is_null()) {
        ImGui::TextDisabled("...");
    } else if (!m_listRel.value("ok", false)) {
        ImGui::TextWrapped("%s is playing as a guest.", who.c_str());
    } else {
        const std::string f = m_listRel.value("friendship", std::string("none"));
        if (f == "none") {
            if (Classic::button("Add Friend", Classic::kBlue, full)) send("friends.add", "Friend request sent!");
        } else if (f == "sent") {
            ImGui::BeginDisabled(); ImGui::Button("Friend request sent", full); ImGui::EndDisabled();
        } else if (f == "received") {
            if (Classic::button("Accept Friend Request", Classic::kPlay, full)) send("friends.accept", "You're friends now!");
        } else if (f == "friends") {
            ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "Friends");
        }
        const bool following = m_listRel.value("following", false);
        if (following) {
            if (ImGui::Button("Unfollow", full)) send("follow.remove", "Unfollowed.");
        } else if (Classic::button("Follow", Classic::kPlay, full)) {
            send("follow.add", "You're following them now.");
        }
        ImGui::TextDisabled("%d follower%s", m_listRel.value("followers", 0), m_listRel.value("followers", 0) == 1 ? "" : "s");
    }
    if (voiceHere()) {   // voice chat: mute them (just for you; they aren't told)
        const bool muted = Voice::muted(who);
        if (ImGui::Button(muted ? "Unmute Voice" : "Mute Voice", full)) Voice::setMuted(who, !muted);
    }
    if (!m_listMsg.empty()) ImGui::TextWrapped("%s", m_listMsg.c_str());
    if (ImGui::Button("Close", full)) keep = false;
    // A click anywhere else closes it too.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::GetIO().MousePos.y > m_listMenuAt.y + 2)
        keep = false;
    ImGui::End();
    ImGui::PopStyleVar();
    Classic::popLight();
    if (!keep || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) m_listMenu.clear();
}
