#pragma once
// Network simulator (Studio: Test menu > Network simulator...). Makes a multiplayer test feel
// like a real connection: a phone on 3G, wobbly Wi-Fi, or a bad signal, so problems that only
// show up away from your own fast computer can be found before players find them.
//
// How it works:
//   - Studio writes the conditions (lag, wobble, lost packets, speed) to a small text file and
//     starts each test player window with --netsim <file>. The players read the file again
//     twice a second, so changing the conditions mid-test needs no restart.
//   - Each player window holds back what it sends and what it gets (a Pipe per connection):
//       one-way delay = half the lag, plus or minus half the wobble (random)
//       a lost packet = sent again later, like TCP does (200 ms + the lag on top)
//       a slow connection = big messages take longer to squeeze through
//     Messages always stay in order (the game talks over TCP, which keeps the order).
//   - Each player writes what it saw (bytes, updates, how late the world arrives, physics
//     corrections...) next to the file once a second, and Studio shows it.
//
// It only changes the network, not the device: a test player still runs at your computer's
// speed (Test like a phone / Stress test are for that).
#include <cstdint>
#include <deque>
#include <random>
#include <string>
#include <vector>

namespace NetSim {

struct Conditions {
    std::string name = "Off";
    int   latencyMs = 0;      // extra round trip (ping), both ways together
    int   jitterMs = 0;       // how much that wobbles, either way
    float lossPercent = 0;    // packets lost (and sent again)
    int   kbps = 0;           // speed limit in kilobits a second (0 = no limit)
    bool on() const { return latencyMs > 0 || jitterMs > 0 || lossPercent > 0 || kbps > 0; }
};

struct Preset { const char* name; Conditions c; const char* about; };
const std::vector<Preset>& presets();
// A preset by name ("3G", "Wi-Fi"...); false if there's none called that.
bool preset(const std::string& name, Conditions& out);

// "name|latency|jitter|loss|kbps" (one line), and back. Numbers are kept in range.
std::string toText(const Conditions& c);
bool fromText(const std::string& text, Conditions& out);
Conditions clamp(Conditions c);

// --- Studio's side ---
bool writeFile(const std::string& path, const Conditions& c);
// Where a player's numbers are written: "<file>.<who>.json".
std::string statsPath(const std::string& file, const std::string& who);

// --- A test player's side ---
// Use this file (from --netsim) for every connection this program makes to a game.
void useFile(const std::string& path, const std::string& who);
bool active();
// The conditions now (the file is read again at most twice a second).
const Conditions& current();
// Write this player's numbers (a JSON text) for Studio. At most once a second is plenty.
void writeStats(const std::string& jsonText);

// One connection held back by the current conditions. `now` is in seconds (any steady clock).
class Pipe {
public:
    Pipe();
    void send(std::string bytes, double now);        // bytes going out (already framed)
    void releaseOut(std::string& out, double now);   // the ones whose time has come, appended
    void arrive(std::string message, double now);    // a whole message that came in
    bool popIn(std::string& message, double now);    // the next one whose time has come

    struct Counters {
        uint64_t bytesIn = 0, bytesOut = 0, msgsIn = 0, msgsOut = 0;
        uint64_t resent = 0;          // "lost" packets that had to be sent again
        size_t   waitingIn = 0, waitingOut = 0;
    };
    Counters counters() const;

private:
    struct Item { double at; std::string data; };
    double when(size_t bytes, double now, double& last);
    std::deque<Item> m_out, m_in;
    double m_lastOut = 0.0, m_lastIn = 0.0;
    Counters m_count;
    std::mt19937 m_rng;
};

} // namespace NetSim
