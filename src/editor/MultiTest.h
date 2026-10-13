#pragma once
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// Studio's "Test with N players" (TEST > Players). Playing in Studio is just you;
// this starts the open place on a local test server (GutsAndBoltsGameServer --place,
// the same local network server "Host on LAN" uses) and opens N Player windows on
// this computer that join it as Player1, Player2... So RemoteEvents, LocalScripts
// and people joining and leaving can be checked without a friend.
//
// What the server and each window print shows in Studio's Output, marked
// [Server] / [Player1]... Stopping the test closes the server and every window.
// The AI tools (multiplayer_test) drive it too.
class MultiTest {
public:
    MultiTest();
    ~MultiTest();   // stops the test

    static constexpr int kMinPlayers = 2, kMaxPlayers = 4;   // offered in Studio
    static constexpr int kMostPlayers = 8;                   // with "Add a player"

    // `place`: the game as a saved file's text. False (with `error`) if it couldn't start.
    bool start(const std::string& place, int players, std::string& error);
    void stop();
    bool addPlayer(std::string& error);   // one more window joins (testing someone joining late)
    // Every frame: brings the test's output into the Output panel, opens the
    // windows once the server is ready, and notices when the server stops.
    void update();

    bool running() const { return (bool)m_server; }
    bool serverReady() const { return m_ready; }
    int  port() const { return m_port; }
    int  playerCount() const { return (int)m_players.size(); }   // windows opened (some may have closed)

    struct Line {
        std::string from;    // "Server", "Player1"...
        std::string level;   // info, warning, error, system
        std::string text, time;
    };
    // The test's own output, oldest first: the last `lines` lines (from one of them, or "" = all).
    std::vector<Line> output(int lines, const std::string& from = "") const;
    // {running, ready, port, server: {running}, players: [{name, running}]}
    nlohmann::json status() const;

    struct Process;   // a program we started, and what it prints (MultiTest.cpp)

private:
    bool startPlayer(std::string& error);
    void take(Process& p);   // its new lines -> Output

    std::unique_ptr<Process>              m_server;
    std::vector<std::unique_ptr<Process>> m_players;
    std::string m_placeFile;
    int         m_port = 0;
    int         m_wanted = 0;      // windows to open once the server is ready
    bool        m_ready = false;
    double      m_startedAt = 0.0;
    std::deque<Line> m_output;
};
