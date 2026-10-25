#pragma once
#include <string>

// gutsandbolts:// links: the website's Play button opens the app with one,
// like gutsandbolts://play/<game id>?guest=boy, and its "Edit in Studio" button with
// gutsandbolts://edit/<game id> (the app downloads it and opens Studio on it).
namespace LaunchLink {

struct Link {
    std::string game;    // server asset id of the game to play
    std::string guest;   // "boy" / "girl" when a visitor picked a guest character, else ""
    std::string server;  // a running server to join (the game page's Join button), else ""
    bool        edit = false;   // open it in Studio instead of playing it
};
bool parse(const std::string& url, Link& out);

// Queue a URL received after startup (iOS SDL drop-file delivery and other
// platform callbacks). poll() returns it to the Player's normal link handler.
void enqueue(const std::string& url);

// Computers: make gutsandbolts:// links open this program (Windows, Linux and Mac;
// quietly does nothing where it can't). On a Mac that means a small app in
// ~/Applications ("Guts&Bolts Player.app") that runs this program.
void registerScheme();

// Mac: start listening for links (macOS hands them over as messages, not on the
// command line). Call it first thing in main(), before any window opens.
void listen();

// Android and Mac: a link that opened (or re-opened) the app since the last call, or "".
std::string poll();

} // namespace LaunchLink
