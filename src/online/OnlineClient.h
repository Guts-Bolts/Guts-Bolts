#pragma once
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

// Talking to a Guts&Bolts server from the apps (the site, Studio).
//
// Requests are signed with your account key and sent on a background thread,
// so a slow (or missing) server never freezes the game. Each reply comes back
// to your callback on the main thread, from update().
namespace Online {

enum class Status { Off, Connecting, Online, Failed };

// The server to use: always the official Guts&Bolts server (everyone plays on
// it). GB_SERVER overrides it, for tests and people developing the server.
std::string serverAddress();
void        setServerAddress(const std::string& address);   // (kept for old callers) reconnects
bool        configured();

Status             status();
const std::string& statusText();   // "Online: Guts&Bolts", "Couldn't reach ..."
bool               online();       // connected and logged in

// You, as the server sees you (after connecting): name, bolts, verified, staff,
// official, grants, canDaily, uploadsLeft, owned[] ...
const nlohmann::json& me();
const nlohmann::json& serverInfo();
bool  verified();
bool  staff();
long long bolts();
bool  owns(const std::string& assetId);

using Reply = std::function<void(const nlohmann::json& reply)>;
// Send a request. The reply always has "ok"; on failure also "error".
void request(const std::string& op, const nlohmann::json& args, Reply done = nullptr, int timeoutSeconds = 20);
// (Re)connect: says hello, which also refreshes me().
void connect();
// Call once a frame: delivers replies. Also retries connecting now and then.
void update();
// Keep me() up to date from a reply that carries "me".
void takeMe(const nlohmann::json& reply);
// Save your avatar (Profile) on the server, so the website and your other devices
// show it too. (The server's copy comes back by itself: see takeMe.)
void pushAvatar(std::function<void(const nlohmann::json&)> done = nullptr);
// Requests still waiting for an answer (for "Working..." spinners).
int  pending();

// A signed request, ready to send yourself (the game relay uses its own connection).
nlohmann::json signedRequest(const std::string& op, const nlohmann::json& args);
// The server's host name and port, split out of serverAddress().
bool serverHostPort(std::string& host, int& port);

// Guests: playing online without an account (like Roblox's guests). They can
// play games (on their own or with others) but not chat, make friends, buy or
// earn Bolts. isGuest() stays false once the account is signed up.
void        setGuest(bool on);
bool        isGuest();
std::string guestName();   // "Guest 1234" (the same on this device every time)
std::string playerName();  // your name in games: the guest name for guests, else your profile's
// Studio's multiplayer test: this window plays as "Player1", "Player2"... ("" = back to
// normal). Only for this run of the app; nothing is saved.
void        setTestName(const std::string& name);
inline const char* kGuestChatText = "Sign up to chat with other users!";

// Wait for every request to finish (tests / quitting).
void finishAll(int timeoutMs = 30000);

} // namespace Online
