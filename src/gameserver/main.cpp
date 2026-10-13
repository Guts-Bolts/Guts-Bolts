// GutsAndBoltsGameServer — runs online games by itself, with nobody playing on it.
//
// Normally the first person to press Play hosts the game on their own computer,
// and when they leave everyone has to move to a new server. A game server machine
// fixes that: leave this program running on a computer (a spare PC, or a cheap
// cloud server) and the Guts&Bolts server sends it games to run. The game keeps
// going whoever leaves, like a Roblox server.
//
//   GutsAndBoltsGameServer [--slots N]       wait for games to run (up to N at once, 4 if not given)
//   GutsAndBoltsGameServer --game <id>       run one game (the waiting one starts these by itself)
//
// It signs in with the account on this computer (like the Player app), which must
// be a Guts&Bolts staff account. No window or graphics card is needed.
#include "../core/Account.h"
#include "../core/Log.h"
#include "../game/GameSession.h"
#include "../net/NetGame.h"
#include "../net/Socket.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../renderer/Mesh.h"
#include "../scene/Player.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

using json = nlohmann::json;

namespace {
using Clock = std::chrono::steady_clock;
double seconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

constexpr double kEmptyClose   = 60.0;    // a game nobody is in closes after this long
constexpr double kPingEvery    = 20.0;    // "still here" to the Guts&Bolts server
constexpr double kRetryEvery   = 10.0;    // reconnecting after losing the server

void say(const std::string& text) {
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
}

// Sign in (the account key on this computer). False if the server can't be reached.
bool goOnline(double waitSeconds) {
    Online::connect();
    const double until = seconds() + waitSeconds;
    while (!Online::online() && seconds() < until) {
        Online::update();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return Online::online();
}

// ImGui is only used for keyboard state in scripts here; it needs a context and fonts.
void setUpImGui() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280, 720);
    io.IniFilename = nullptr;
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
}

// --- One game ---------------------------------------------------------------

int runGame(const std::string& gameId, int max) {
    Mesh::headless = true;
    setUpImGui();
    if (!goOnline(30.0)) { say("Couldn't reach the Guts&Bolts server: " + Online::statusText()); return 1; }

    // Download the newest version of the game.
    bool done = false, ok = false;
    std::filesystem::path file;
    json info;
    Online::download(gameId, [&](bool good, const std::filesystem::path& f, const json& i) {
        done = true; ok = good; file = f; info = i;
    }, true);
    for (const double until = seconds() + 60.0; !done && seconds() < until;) {
        Online::update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!ok) { say("Couldn't download game " + gameId + ": " + info.value("error", std::string("no answer"))); return 1; }

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, file.string(), &err)) { say("Couldn't load " + gameId + ": " + err); return 1; }
    // Nobody plays here: no character of our own, no LocalPlayer in scripts.
    if (Player* p = scene.player()) {
        if (SceneNode* r = p->root()) scene.removeNode(r);
        p->setRootId(0);
    }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.scripts().setPlayerName("Server");
    session.scripts().setOnlineData(true, gameId);   // DataStores: the game's data on the Guts&Bolts server
    session.scripts().setTeleportsWork(true);   // TeleportService: we tell the player's app where to go

    NetServer server(&scene, &session);
    server.setDedicated(true);
    server.setOwner(info.value("creator", std::string()));   // (the dev console's server side is theirs)
    std::string host;
    int port = 0;
    if (!Online::serverHostPort(host, port)) { say("No Guts&Bolts server address."); return 1; }
    const std::string title = info.value("name", scene.info().title);
    json args = {{"game", gameId}, {"title", title}, {"private", false}, {"max", std::clamp(max, 2, 30)}, {"dedicated", true}};
    if (!server.startRelay(host, port, Online::signedRequest("relay.host", args).dump(), err)) {
        say("Couldn't open the server: " + err);
        return 1;
    }
    session.start();
    say("Running " + title + " (" + gameId + ")");

    // 60 steps a second until everyone has gone.
    constexpr double kStep = 1.0 / 60.0;
    double next = seconds(), emptySince = seconds();
    int lastCount = 0;
    while (true) {
        ImGui::NewFrame();
        Online::update();
        server.update((float)kStep);
        session.update((float)kStep, 0.0f, false);
        for (auto& tp : session.scripts().takeTeleports())
            if (!server.teleport(tp.player, tp.place, tp.data, gameId)) say("Teleport: " + tp.player + " isn't here");
        ImGui::EndFrame();

        if (!server.relayed() && !server.relayError().empty()) { say("Server closed: " + server.relayError()); break; }
        const int count = server.playerCount();
        if (count != lastCount) { say(std::to_string(count) + " playing"); lastCount = count; }
        if (count > 0) emptySince = seconds();
        else if (seconds() - emptySince > kEmptyClose) { say("Nobody's here: closing."); break; }

        next += kStep;
        const double wait = next - seconds();
        if (wait > 0) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        else if (wait < -0.5) next = seconds();   // fell behind: don't try to catch up
    }
    server.stop();
    session.stop();
    Online::finishAll(5000);
    return 0;
}

// --- Studio's multiplayer test ----------------------------------------------
//
// Studio's "Test with N players" runs the open place here, on this computer, as a
// local network server (the same one "Host on LAN" uses), and opens Player windows
// that join it. Nothing goes through the Guts&Bolts server and no account is needed.
// Everything printed (scripts' print / warn / errors) goes to stdout, which Studio
// shows in its Output. Studio stops us when the test ends; if it doesn't (it
// crashed), we close by ourselves once nobody has been here for a minute.

int runLocalTest(const std::string& place, int port) {
    Mesh::headless = true;
    setUpImGui();
    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, place, &err)) { say("Couldn't load " + place + ": " + err); return 1; }
    if (Player* p = scene.player()) {   // nobody plays on the server itself
        if (SceneNode* r = p->root()) scene.removeNode(r);
        p->setRootId(0);
    }
    session.setRunOnly(true);
    session.scripts().setNoLocalPlayer(true);
    session.scripts().setPlayerName("Server");

    NetServer server(&scene, &session);
    server.setDedicated(true);
    server.setOwner(Account::id());   // the test windows are this computer's account: they get the dev console
    if (!server.start(port, err)) { say("Couldn't start the test server: " + err); return 1; }
    session.start();
    say("Test server running " + scene.info().title + " on port " + std::to_string(port));

    constexpr double kStep = 1.0 / 60.0;
    double next = seconds(), emptySince = seconds();
    std::vector<std::string> here;   // who's in, to say who joins and leaves
    while (true) {
        ImGui::NewFrame();
        server.update((float)kStep);
        session.update((float)kStep, 0.0f, false);
        for (auto& tp : session.scripts().takeTeleports())
            say("Teleport: " + tp.player + " would go to " + tp.place + " (teleports don't work in a test)");
        ImGui::EndFrame();

        std::vector<std::string> now;
        for (const PlayerEntry& e : server.players()) now.push_back(e.name);
        for (const auto& n : now) if (std::find(here.begin(), here.end(), n) == here.end()) say(n + " joined the test");
        for (const auto& n : here) if (std::find(now.begin(), now.end(), n) == now.end()) say(n + " left the test");
        here = std::move(now);
        if (!here.empty()) emptySince = seconds();
        else if (seconds() - emptySince > kEmptyClose) { say("Nobody's here: closing the test server."); break; }

        next += kStep;
        const double wait = next - seconds();
        if (wait > 0) std::this_thread::sleep_for(std::chrono::duration<double>(wait));
        else if (wait < -0.5) next = seconds();
    }
    server.stop();
    session.stop();
    return 0;
}

// --- Waiting for games ------------------------------------------------------

std::string quoted(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
#endif
}

bool safeId(const std::string& id) {
    if (id.empty() || id.size() > 80) return false;
    for (char c : id) if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    return true;
}

// Each game runs in its own copy of this program, so one broken game can't take the others down.
void startGame(const std::string& self, const std::string& gameId, int max) {
    if (!safeId(gameId)) return;
    const std::string cmd = quoted(self) + " --game " + gameId + " --max " + std::to_string(max);
    say("Starting game " + gameId);
    std::thread([cmd] { if (std::system(cmd.c_str()) != 0) {} }).detach();   // (it says how it went itself)
}

int runPool(const std::string& self, int slots) {
    if (!goOnline(30.0)) { say("Couldn't reach the Guts&Bolts server: " + Online::statusText()); return 1; }
    say("Signed in as " + Online::me().value("name", std::string("?")) + ". Waiting for games (up to " +
        std::to_string(slots) + " at once). Leave this running.");
    std::unique_ptr<Net::Connection> control;
    double pingAt = 0.0, retryAt = 0.0;
    while (true) {
        Online::update();
        if (!control && seconds() >= retryAt) {
            std::string host, err;
            int port = 0;
            if (Online::serverHostPort(host, port)) control = Net::Connection::connectTo(host, port, err, 5000);
            if (control) control->send(Online::signedRequest("relay.pool", {{"slots", slots}}).dump());
            else { say("Couldn't connect (" + err + "), trying again soon."); retryAt = seconds() + kRetryEvery; }
            pingAt = seconds() + kPingEvery;
        }
        if (control) {
            const bool alive = control->poll();
            std::string text;
            while (control && control->pop(text)) {
                json m = json::parse(text, nullptr, false);
                if (!m.is_object()) continue;
                const std::string t = m.value("t", std::string());
                if (t == "relay" && !m.value("ok", false)) {
                    say("The Guts&Bolts server said no: " + m.value("error", std::string("?")));
                    return 1;   // (not staff: no point trying again)
                }
                if (t == "relay") say("Ready.");
                if (t == "start") startGame(self, m.value("game", std::string()), m.value("max", 12));
            }
            if (!alive || !control->alive()) {
                say("Lost the Guts&Bolts server, reconnecting...");
                control.reset();
                retryAt = seconds() + kRetryEvery;
            } else if (seconds() >= pingAt) {
                control->send(json{{"t", "ping"}}.dump());
                pingAt = seconds() + kPingEvery;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}
} // namespace

int main(int argc, char** argv) {
    std::string game, place;
    int slots = 4, max = 12, port = kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--game"))       game = next();
        else if (!std::strcmp(argv[i], "--slots")) slots = std::clamp(std::atoi(next()), 1, 50);
        else if (!std::strcmp(argv[i], "--max"))   max = std::atoi(next());
        else if (!std::strcmp(argv[i], "--place")) place = next();
        else if (!std::strcmp(argv[i], "--port"))  port = std::atoi(next());
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            say("GutsAndBoltsGameServer [--slots N]   wait for games to run (up to N at once)\n"
                "GutsAndBoltsGameServer --game <id>   run one game\n"
                "GutsAndBoltsGameServer --place <file> [--port N]   Studio's multiplayer test: run a game file\n"
                "                                     on this computer's network (Player windows join it)");
            return 0;
        }
    }
    if (!place.empty()) return runLocalTest(place, port > 0 && port < 65536 ? port : kDefaultPort);
    if (!game.empty()) return runGame(game, max);
    std::error_code ec;
    std::filesystem::path self = std::filesystem::canonical(argv[0], ec);
    if (ec) self = std::filesystem::absolute(argv[0]);
    return runPool(self.string(), slots);
}
