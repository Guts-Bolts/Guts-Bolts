// Friends and game servers on the site.
//
// - Friends: requests, accepting, and a list showing who's online and what
//   they're playing, with a Join button.
// - Play: puts you in a public server of that game (or starts one if nobody's
//   playing). Create a server: a private server (friends or a code), an offline
//   game (just you), or a local-network one.
//
// Online games all go through the Guts&Bolts server's relay, so nobody ever
// sees anybody else's IP address.
#include "PlayerApp.h"
#include "SocialUi.h"
#include "../core/Account.h"
#include "../core/Log.h"
#include "../game/Badges.h"
#include "../net/NetGame.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../scene/Serializer.h"
#include "../game/GameSession.h"
#include "../scene/Emotes.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <fstream>
#include <sstream>

using namespace Social;
using json = nlohmann::json;

namespace {

bool smallLink(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    bool r = ImGui::SmallButton(label);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    return r;
}

void statusDot(ImU32 color) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5, p.y + h * 0.5f), 4.5f, color, 12);
    ImGui::Dummy(ImVec2(12, h));
    ImGui::SameLine(0, 2);
}

} // namespace

// ---------------------------------------------------------------------------
// Starting games
// ---------------------------------------------------------------------------

PlayerApp::Starter PlayerApp::localStarter(const std::filesystem::path& path) {
    return [this, path](HostMode mode) { joinGame(path, mode); };
}

PlayerApp::Starter PlayerApp::onlineStarter(const std::string& id) {
    return [this, id](HostMode mode) {
        m_busy = true;
        m_playMsg = "Downloading...";
        // Always fetch the newest version (the creator may have updated it).
        Online::download(id, [this, id, mode](bool ok, const std::filesystem::path& file, const json& info) {
            m_busy = false;
            m_playMsg.clear();
            if (!ok) { m_status = info.value("error", std::string("Couldn't download the game.")); return; }
            m_gameOwner = info.value("creator", std::string());   // (the dev console's server side is theirs)
            joinGame(file, mode, id);
            Online::fetchSounds(*m_scene);
            if (info.value("allowGear", false)) giveGear(id);
        }, true);
    };
}

void PlayerApp::giveGear(const std::string& gameKey) {
    const json& me = Online::me();
    if (!me.contains("gear") || !me["gear"].is_array()) return;
    for (const auto& g : me["gear"]) {
        if (!g.is_string()) continue;
        Online::download(g.get<std::string>(), [this, gameKey](bool ok, const std::filesystem::path& file, const json&) {
            if (!ok || m_page != Page::Game || m_loadingGameId != gameKey) return;   // (left the game meanwhile)
            std::ifstream f(file, std::ios::binary);
            std::stringstream text;
            text << f.rdbuf();
            json m = json::parse(text.str(), nullptr, false);
            if (!m.is_object() || !m.contains("nodes") || !m["nodes"].is_array() || m["nodes"].empty()) return;
            if (auto tool = Serializer::nodeFromString(m["nodes"][0].dump(), true)) m_session->addGear(std::move(tool));
        });
    }
}

void PlayerApp::syncEmotes() {
    const json& me = Online::me();
    const json slots = me.contains("emotes") && me["emotes"].is_array() ? me["emotes"] : json::array();
    const std::string pack = me.value("animPack", std::string());
    const std::string want = slots.dump() + "|" + pack;
    if (want == m_emotesSynced) return;
    m_emotesSynced = want;
    auto readText = [](const std::filesystem::path& file) {
        std::ifstream f(file, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    };
    Emotes::Loadout& l = Emotes::mine();
    for (int i = 0; i < Emotes::kSlots; ++i) {
        const std::string id = i < (int)slots.size() && slots[i].is_string() ? slots[i].get<std::string>() : std::string();
        if (id == l.slots[i].id) continue;
        l.slots[i] = Emotes::Slot{id, "", nullptr};
        ++l.version;
        if (id.empty()) continue;
        Online::download(id, [i, id, readText](bool ok, const std::filesystem::path& file, const json& info) {
            Emotes::Slot& s = Emotes::mine().slots[i];
            Anim::Clip clip;
            if (!ok || s.id != id || !Emotes::parseEmoteFile(readText(file), clip)) return;   // (changed meanwhile)
            s.name = info.value("name", std::string("Emote"));
            s.clip = std::make_shared<const Anim::Clip>(std::move(clip));
            ++Emotes::mine().version;
        });
    }
    if (pack != l.packId) {
        l.packId = pack;
        l.pack.clear();
        ++l.version;
        if (!pack.empty())
            Online::download(pack, [pack, readText](bool ok, const std::filesystem::path& file, const json&) {
                Emotes::Loadout& m = Emotes::mine();
                Emotes::Pack p;
                if (!ok || m.packId != pack || !Emotes::parsePackFile(readText(file), p)) return;
                m.pack = std::move(p);
                ++m.version;
            });
    }
}

void PlayerApp::playGame(const std::string& key, const std::string& title, Starter start, int tries) {
    if (!Online::online()) { m_status = "You need to be connected to the Guts&Bolts server to play."; return; }
    if (tries == 0) startLoadingScreen(key, title);
    m_busy = true;
    m_playMsg = tries == 0 ? "Finding a server..." : "Starting a server...";
    // A game server machine may start the game for us (then we ask again in a moment).
    // If it takes too long, we host it ourselves like before.
    constexpr int kMostTries = 12;
    Online::request("servers.play", {{"game", key}, {"dedicated", tries < kMostTries}}, [this, key, title, start, tries](const json& r) {
        m_busy = false;
        m_playMsg.clear();
        if (!r.value("ok", false)) { m_status = r.value("error", std::string("Couldn't find a server. Try again.")); return; }
        if (r.contains("join")) joinRelay(r["join"].get<std::string>(), "", title);
        else if (r.contains("wait")) {
            m_waitKey = key; m_waitTitle = title; m_waitStart = start; m_waitTries = tries + 1;
            m_waitPrivate = false;
            m_waitAt = ImGui::GetTime() + std::clamp(r.value("wait", 2.0), 0.5, 10.0);
            m_busy = true;
            m_playMsg = "Starting a server...";
        }
        else start(HostMode::Public);   // nobody's playing: you're the first in a new public server
    }, 10);
}

void PlayerApp::playPrivate(const std::string& key, const std::string& title, Starter start, const std::string& code, int tries) {
    if (!Online::online()) { m_status = "You need to be connected to the Guts&Bolts server to play."; return; }
    if (key.rfind("local:", 0) == 0) { start(HostMode::Private); return; }   // (a game file on this computer: only you have it)
    if (tries == 0) startLoadingScreen(key, title);
    m_busy = true;
    m_playMsg = "Starting your private server...";
    constexpr int kMostTries = 12;
    json args = {{"game", key}, {"dedicated", tries < kMostTries}};
    if (!code.empty()) args["code"] = code;
    Online::request("servers.private", args, [this, key, title, start, tries](const json& r) {
        m_busy = false;
        m_playMsg.clear();
        if (!r.value("ok", false)) { m_status = r.value("error", std::string("Couldn't start your private server. Try again.")); return; }
        const std::string code = r.value("code", std::string());
        if (r.contains("join")) joinRelay("", code, title);
        else if (r.contains("wait") && tries + 1 < kMostTries) {   // a game server machine is starting it
            m_waitKey = key; m_waitTitle = title; m_waitStart = start; m_waitTries = tries + 1;
            m_waitPrivate = true; m_waitCode = code;
            m_waitAt = ImGui::GetTime() + std::clamp(r.value("wait", 2.0), 0.5, 10.0);
            m_busy = true;
            m_playMsg = "Starting your private server...";
        }
        else start(HostMode::Private);   // no game server machine free: you host it, like before
    }, 10);
}

void PlayerApp::joinRelay(const std::string& session, const std::string& code, const std::string& title) {
    std::string server;
    int port = 0;
    if (!Online::online() || !Online::serverHostPort(server, port)) {
        m_status = "You need to be connected to a Guts&Bolts server to join people.";
        return;
    }
    json args = code.empty() ? json{{"session", session}} : json{{"code", code}};
    m_client = std::make_unique<NetClient>(m_scene.get(), m_session.get());
    m_client->setJoinData(m_teleportData);   // teleported here? (TeleportService)
    m_teleportData = nullptr;
    if (!m_client->connectRelay(server, port, Online::signedRequest("relay.join", args).dump())) {
        m_status = m_client->error();
        m_client.reset();
        return;
    }
    m_currentTitle = title.empty() ? std::string("Joining...") : "Joining " + title + "...";
    if (!m_connectScreen) { m_currentAuthor.clear(); m_loadingGameId.clear(); m_loadingIcon.clear(); m_loadingTitle.clear(); m_loadingAuthor.clear(); }   // (keep what Play knew)
    m_loadingT = 0.9f;
    m_joinedOnce = false;
    m_paused = false;
    m_status.clear();
    m_page = Page::Game;
}

// The host left the game. Instead of kicking everyone, the Guts&Bolts server picks one of the
// players (whoever has been there longest) to host a new server of the same game, and the
// rest follow them there. The world starts fresh, but everyone stays together.
void PlayerApp::startMove(const std::string& session, const std::string& game, const std::string& title) {
    m_session->stop();
    m_moveFrom = session;
    m_moveGame = game;
    m_moveTitle = title;
    m_moveUntil = ImGui::GetTime() + 80.0;
    m_moveRetryAt = ImGui::GetTime() + 0.5;   // give the server a moment to notice the host is gone
    Log::info("The host left. Moving everyone to a new server...");
}

void PlayerApp::moveStep() {
    if (ImGui::GetTime() > m_moveUntil) {
        m_moveFrom.clear();
        leaveGame();
        m_status = "The host left, and nobody could start a new server.";
        return;
    }
    if (ImGui::GetTime() < m_moveRetryAt) return;
    m_moveRetryAt = ImGui::GetTime() + 3.0;   // (if joining can't even start, try again in a bit)
    joinRelay(m_moveFrom, "", m_moveTitle);   // the server either sends us to the new host, or asks us to be it
}

void PlayerApp::openServers(const std::string& key, const std::string& title, Starter start) {
    m_serversOpen = true;
    m_serversKey = key;
    m_serversTitle = title;
    m_serversStart = std::move(start);
    m_serversMsg.clear();
    m_serverList = json::array();
    m_serverPrivate = json::object();
    if (Online::online())
        Online::request("servers.list", {{"game", key}}, [this, key](const json& r) {
            if (key != m_serversKey || !r.value("ok", false)) return;
            m_serverList = r["servers"];
            m_serverPrivate = {{"price", r.value("privatePrice", 0LL)}, {"until", r.value("privateUntil", 0LL)},
                               {"needsBuy", r.value("privateNeedsBuy", false)}};
        });
}

void PlayerApp::drawServersDialog() {
    if (m_serversOpen && !ImGui::IsPopupOpen("Servers##dlg") && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        ImGui::OpenPopup("Servers##dlg");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(560), 0));
    if (!ImGui::BeginPopupModal("Servers##dlg", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) return;
    if (tappedOutside()) m_serversOpen = false;
    if (!m_serversOpen) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    const bool online = Online::online();
    auto start = [this](HostMode mode) {
        Starter s = m_serversStart;
        m_serversOpen = false;
        ImGui::CloseCurrentPopup();
        if (s) s(mode);
    };

    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("Servers - %s", m_serversTitle.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    ImGui::SeparatorText("Start your own");
    const float bw = std::min(170.0f, (ImGui::GetContentRegionAvail().x - 16) / 3);
    // Paid private servers: the game's creator can charge for them (30 days at a time).
    const long long privPrice = m_serverPrivate.value("price", 0LL), privUntil = m_serverPrivate.value("until", 0LL);
    const bool mustBuy = m_serverPrivate.value("needsBuy", false);
    ImGui::BeginDisabled(!online || m_busy);
    if (mustBuy) {
        std::string label = "Buy private server (" + std::to_string(privPrice) + " Bolts)";
        if (Classic::button(label.c_str(), Classic::kPlay, ImVec2(std::min(300.0f, ImGui::GetContentRegionAvail().x), 34)))
            ImGui::OpenPopup("##buyprivate");
    } else if (Classic::button("Private server", Classic::kBlue, ImVec2(bw, 34))) {
        const std::string key = m_serversKey, title = m_serversTitle;
        Starter s = m_serversStart;
        m_serversOpen = false;
        ImGui::CloseCurrentPopup();
        playPrivate(key, title, s);
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##buyprivate")) {
        ImGui::Text("Buy 30 days of your own private server for %lld Bolts?", privPrice);
        if (Classic::button("Buy it", Classic::kPlay, ImVec2(100, 30))) {
            m_busy = true;
            const std::string key = m_serversKey;
            Online::request("servers.buyPrivate", {{"game", key}}, [this, key](const json& r) {
                m_busy = false;
                if (!r.value("ok", false)) { m_serversMsg = r.value("error", std::string()); return; }
                if (key == m_serversKey) {
                    m_serverPrivate["until"] = r.value("until", 0LL);
                    m_serverPrivate["needsBuy"] = false;
                    m_serversMsg = "It's yours for 30 days. Press Private server to start it.";
                }
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("Private: only your friends, and people you give the code to, can join.");
    if (privPrice > 0 && privUntil > 0) {
        std::time_t tt = (std::time_t)privUntil;
        char date[32];
        std::strftime(date, sizeof(date), "%b %d", std::localtime(&tt));
        ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "Your private server lasts until %s.", date);
    } else if (privPrice > 0 && mustBuy) {
        ImGui::TextDisabled("This game's private servers cost %lld Bolts for 30 days. Friends join yours free.", privPrice);
    }
    if (!m_serversMsg.empty()) {
        const bool good = m_serversMsg.rfind("It's yours", 0) == 0;
        ImGui::TextColored(good ? ImVec4(0.1f, 0.5f, 0.2f, 1) : ImVec4(0.75f, 0.2f, 0.1f, 1), "%s", m_serversMsg.c_str());
    }
    ImGui::PopTextWrapPos();

    if (online) {
        ImGui::SeparatorText("Join with a code");
        ImGui::SetNextItemWidth(160);
        bool enter = ImGui::InputTextWithHint("##code", "e.g. K7PQ2M", &m_codeInput,
                                              ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(m_codeInput.size() < 4);
        if (ImGui::Button("Join") || (enter && m_codeInput.size() >= 4)) {
            std::string code = m_codeInput, title = m_serversTitle;
            m_serversOpen = false;
            ImGui::CloseCurrentPopup();
            joinRelay("", code, title);
        }
        ImGui::EndDisabled();

        ImGui::SeparatorText("Running now");
        if (m_serverList.empty()) ImGui::TextDisabled("Nobody's playing this right now. Press Play to start a public server!");
        ImGui::BeginChild("##list", ImVec2(0, std::min(220.0f, 12.0f + 44.0f * (float)m_serverList.size())), ImGuiChildFlags_None);
        for (size_t i = 0; i < m_serverList.size(); ++i) {
            const json& sv = m_serverList[i];
            ImGui::PushID((int)i);
            int players = sv.value("players", 1), max = sv.value("max", 12), friends = sv.value("friends", 0);
            ImGui::BeginGroup();
            ImGui::Text("%d / %d players", players, max);
            if (sv.value("private", false)) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.7f, 0.45f, 0.0f, 1), "Private"); }
            if (friends) { ImGui::SameLine(); ImGui::TextColored(ImVec4(0.1f, 0.55f, 0.2f, 1), "%d friend%s", friends, friends == 1 ? "" : "s"); }
            ImGui::TextDisabled("Host: %s", sv.value("hostName", std::string()).c_str());
            if (sv.value("hostVerified", false)) { ImGui::SameLine(0, 3); Badges::check(); }
            ImGui::EndGroup();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
            ImGui::BeginDisabled(players >= max);
            if (Classic::button(players >= max ? "Full" : "Join", Classic::kPlay, ImVec2(70, 30))) {
                std::string id = sv.value("id", std::string()), title = m_serversTitle;
                m_serversOpen = false;
                ImGui::CloseCurrentPopup();
                joinRelay(id, "", title);
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(100, 30))) { m_serversOpen = false; ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Friends
// ---------------------------------------------------------------------------

void PlayerApp::refreshFriends() {
    m_friendsAt = ImGui::GetTime();
    Online::request("friends.list", json::object(), [this](const json& r) {
        if (r.value("ok", false)) m_friends = r;
    });
}

void PlayerApp::friendButton(const std::string& id, const std::string& status) {
    auto act = [this, id](const char* op) {
        Online::request(op, {{"user", id}}, [this, id](const json& r) {
            m_friendMsg = r.value("ok", false) ? std::string() : r.value("error", std::string());
            refreshFriends();
            if (m_page == Page::Profile && m_profileId == id) openProfile(id);   // show the new state
        });
    };
    ImGui::PushID(id.c_str());
    if (status == "none") {
        if (Classic::button("Add Friend", Classic::kBlue, ImVec2(120, 28))) act("friends.add");
    } else if (status == "sent") {
        ImGui::BeginDisabled();
        ImGui::Button("Request sent", ImVec2(120, 28));
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (smallLink("cancel")) act("friends.cancel");
    } else if (status == "received") {
        if (Classic::button("Accept friend request", Classic::kPlay, ImVec2(180, 28))) act("friends.accept");
        ImGui::SameLine();
        if (smallLink("decline")) act("friends.decline");
    } else if (status == "friends") {
        ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "Friends");
        ImGui::SameLine();
        if (smallLink("unfriend")) ImGui::OpenPopup("##unfriend");
        if (ImGui::BeginPopup("##unfriend")) {
            ImGui::TextUnformatted("Remove this friend?");
            if (ImGui::Button("Yes, unfriend")) { act("friends.remove"); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    ImGui::PopID();
}

void PlayerApp::drawFriends() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Friends");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("See who's online and jump into their game.");
    ImGui::Spacing();
    if (needsServer("Friends")) return;
    if (ImGui::GetTime() - m_friendsAt > 15.0) refreshFriends();   // keep the online dots fresh

    const json& friends = m_friends.contains("friends") ? m_friends["friends"] : json::array();
    const json& in = m_friends.contains("incoming") ? m_friends["incoming"] : json::array();
    const json& out = m_friends.contains("outgoing") ? m_friends["outgoing"] : json::array();
    std::string tabs[] = {"Friends (" + std::to_string(friends.size()) + ")",
                          in.empty() ? std::string("Requests") : "Requests (" + std::to_string(in.size()) + ")",
                          "Add Friends"};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        float tw = std::min(140.0f, (ImGui::GetContentRegionAvail().x - 16) / (3 - i));
        if (m_friendsTab == i ? Classic::button(tabs[i].c_str(), Classic::kBlue, ImVec2(tw, 28)) : ImGui::Button(tabs[i].c_str(), ImVec2(tw, 28)))
            m_friendsTab = i;
    }
    ImGui::Separator();
    if (!m_friendMsg.empty()) ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.1f, 1), "%s", m_friendMsg.c_str());

    if (m_friendsTab == 0) {
        if (friends.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("No friends yet. Find people under Add Friends, or click Add Friend on someone's profile.");
            ImGui::PopTextWrapPos();
        }
        // Playing first, then online, then offline.
        std::vector<const json*> sorted;
        for (const auto& f : friends) sorted.push_back(&f);
        auto rank = [](const json* f) { return f->contains("playing") ? 0 : f->value("online", false) ? 1 : 2; };
        std::stable_sort(sorted.begin(), sorted.end(), [&](const json* a, const json* b) { return rank(a) < rank(b); });
        for (size_t i = 0; i < sorted.size(); ++i) {
            const json& f = *sorted[i];
            std::string id = f.value("id", std::string());
            ImGui::PushID((int)i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            avatarCircle(ImGui::GetWindowDrawList(), ImVec2(p.x + 20, p.y + 21), 18, id, f.value("name", std::string()));
            ImGui::Dummy(ImVec2(44, 42));
            ImGui::SameLine();
            ImGui::BeginGroup();
            if (nameLink(f, "name")) openProfile(id);
            if (f.contains("playing")) {
                const json& pl = f["playing"];
                statusDot(IM_COL32(40, 160, 70, 255));
                ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "Playing %s%s", pl.value("title", std::string()).c_str(),
                                   pl.value("private", false) ? " (private server)" : "");
            } else if (f.value("online", false)) {
                statusDot(IM_COL32(40, 120, 220, 255));
                ImGui::TextColored(ImVec4(0.1f, 0.35f, 0.7f, 1), "Online");
            } else {
                statusDot(IM_COL32(160, 165, 175, 255));
                ImGui::TextDisabled("Offline");
            }
            ImGui::EndGroup();
            if (f.contains("playing") && f["playing"].is_object() && f["playing"].contains("session") &&
                f["playing"]["session"].is_string()) {   // no session = they don't let you join
                const json& pl = f["playing"];
                bool full = pl.value("full", false);
                ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 90));
                ImGui::BeginDisabled(full);
                if (Classic::button(full ? "Full" : "Join", Classic::kPlay, ImVec2(80, 30)))
                    joinRelay(pl.value("session", std::string()), "", pl.value("title", std::string()));
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    } else if (m_friendsTab == 1) {
        ImGui::SeparatorText("Asking to be your friend");
        if (in.empty()) ImGui::TextDisabled("No new friend requests.");
        for (size_t i = 0; i < in.size(); ++i) {
            ImGui::PushID((int)i);
            if (nameLink(in[i], "n")) openProfile(in[i].value("id", std::string()));
            ImGui::SameLine(0, 16);
            friendButton(in[i].value("id", std::string()), "received");
            ImGui::PopID();
        }
        ImGui::SeparatorText("You asked");
        if (out.empty()) ImGui::TextDisabled("Nothing waiting.");
        for (size_t i = 0; i < out.size(); ++i) {
            ImGui::PushID((int)i + 1000);
            if (nameLink(out[i], "n")) openProfile(out[i].value("id", std::string()));
            ImGui::SameLine(0, 16);
            friendButton(out[i].value("id", std::string()), "sent");
            ImGui::PopID();
        }
    } else {
        ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 90));
        bool enter = ImGui::InputTextWithHint("##fq", "Search by name", &m_friendQuery, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((Classic::button("Search", Classic::kBlue) || enter) && !m_friendQuery.empty())
            Online::request("users.search", {{"query", m_friendQuery}}, [this](const json& r) {
                m_friendSearch = r.value("ok", false) ? r["users"] : json::array();
                m_friendMsg = r.value("ok", false) ? (m_friendSearch.empty() ? "Nobody by that name." : "") : r.value("error", std::string());
            });
        // Work out where we stand with each person from the lists we already have.
        auto statusOf = [&](const std::string& id) {
            if (id == Account::id()) return std::string("self");
            for (const auto& f : friends) if (f.value("id", std::string()) == id) return std::string("friends");
            for (const auto& f : out) if (f.value("id", std::string()) == id) return std::string("sent");
            for (const auto& f : in) if (f.value("id", std::string()) == id) return std::string("received");
            return std::string("none");
        };
        for (size_t i = 0; i < m_friendSearch.size(); ++i) {
            const json& u = m_friendSearch[i];
            std::string id = u.value("id", std::string()), st = statusOf(id);
            ImGui::PushID((int)i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            avatarCircle(ImGui::GetWindowDrawList(), ImVec2(p.x + 16, p.y + 16), 14, id, u.value("name", std::string()));
            ImGui::Dummy(ImVec2(36, 32));
            ImGui::SameLine();
            if (nameLink(u, "n")) openProfile(id);
            ImGui::SameLine(0, 16);
            if (st == "self") ImGui::TextDisabled("(you)");
            else friendButton(id, st);
            ImGui::PopID();
        }
    }

}
