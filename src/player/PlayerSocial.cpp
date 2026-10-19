// The site's People (user search and profiles) and Groups pages. Both live on
// the Guts&Bolts server, so they need one to be connected.
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Account.h"
#include "../game/Badges.h"
#include "../game/Bolts.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Scene.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <ctime>

using namespace Site;
using json = nlohmann::json;

#include "SocialUi.h"
using namespace Social;

namespace {
std::string sinceText(long long t) {
    long long s = std::max(0LL, Online::unixNow() - t);
    if (s < 60) return "just now";
    if (s < 3600) return std::to_string(s / 60) + (s / 60 == 1 ? " minute ago" : " minutes ago");
    if (s < 86400) return std::to_string(s / 3600) + (s / 3600 == 1 ? " hour ago" : " hours ago");
    return std::to_string(s / 86400) + (s / 86400 == 1 ? " day ago" : " days ago");
}

// Player Badges: a coloured shield with a white picture, same as the website. Locked ones are grey.
void playerBadgeIcon(const std::string& key, bool got, float size) {
    struct Look { const char* key; ImU32 col; };
    static const Look looks[] = {
        {"creator", IM_COL32(232, 89, 12, 255)},  {"builder", IM_COL32(29, 111, 216, 255)},
        {"architect", IM_COL32(107, 47, 179, 255)}, {"friendly", IM_COL32(22, 163, 74, 255)},
        {"collector", IM_COL32(176, 120, 0, 255)}, {"oldtimer", IM_COL32(91, 100, 114, 255)},
    };
    ImU32 col = IM_COL32(136, 136, 136, 255);
    for (const auto& l : looks) if (key == l.key) col = l.col;
    if (!got) col = IM_COL32(190, 190, 190, 255);
    const ImU32 white = got ? IM_COL32(255, 255, 255, 255) : IM_COL32(240, 240, 240, 255);
    const float h = size * 1.125f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##pb", ImVec2(size, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float u = size / 32.0f;
    auto P = [&](float x, float y) { return ImVec2(p.x + x * u, p.y + y * u); };
    // Shield: flat top corners, pointed bottom.
    ImVec2 shield[] = {P(16, 1), P(30, 6), P(30, 18), P(28, 25), P(23, 31), P(16, 35), P(9, 31), P(4, 25), P(2, 18), P(2, 6)};
    dl->AddConvexPolyFilled(shield, 10, col);
    dl->AddPolyline(shield, 10, IM_COL32(0, 0, 0, 64), ImDrawFlags_Closed, 1.0f);
    ImVec2 shine[] = {P(16, 3), P(28, 7.5f), P(28, 12), P(4, 12), P(4, 7.5f)};
    dl->AddConvexPolyFilled(shine, 5, IM_COL32(255, 255, 255, 40));
    if (key == "creator") {          // a hammer over a brick
        dl->AddRectFilled(P(9, 17), P(23, 24), white, 1.0f * u);
        dl->AddLine(P(11, 15), P(21, 7), white, 2.4f * u);
        dl->AddLine(P(18, 6), P(23, 11), white, 3.0f * u);
    } else if (key == "builder") {   // one brick
        dl->AddRectFilled(P(7, 14), P(25, 23), white, 1.0f * u);
        dl->AddCircleFilled(P(12, 14), 2.0f * u, white);
        dl->AddCircleFilled(P(20, 14), 2.0f * u, white);
    } else if (key == "architect") { // a house
        dl->AddTriangleFilled(P(16, 6), P(26, 15), P(6, 15), white);
        dl->AddRectFilled(P(9, 15), P(23, 25), white);
        dl->AddRectFilled(P(14, 18), P(18, 25), col);
    } else if (key == "friendly") {  // two people
        dl->AddCircleFilled(P(12, 12), 3.5f * u, white);
        dl->AddCircleFilled(P(20, 12), 3.5f * u, white);
        dl->AddRectFilled(P(6, 18), P(18, 25), white, 4.0f * u, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(P(14, 18), P(26, 25), white, 4.0f * u, ImDrawFlags_RoundCornersTop);
    } else if (key == "collector") { // a star
        ImVec2 c = P(16, 16);
        for (int i = 0; i < 5; ++i) {
            float a0 = -1.5708f + i * 1.2566f, a1 = a0 + 0.6283f, a2 = a0 - 0.6283f;
            dl->AddTriangleFilled(ImVec2(c.x + std::cos(a0) * 10 * u, c.y + std::sin(a0) * 10 * u),
                                  ImVec2(c.x + std::cos(a1) * 4 * u, c.y + std::sin(a1) * 4 * u),
                                  ImVec2(c.x + std::cos(a2) * 4 * u, c.y + std::sin(a2) * 4 * u), white);
        }
        dl->AddCircleFilled(c, 4.2f * u, white);
    } else if (key == "oldtimer") {  // a clock
        dl->AddCircle(P(16, 16), 9 * u, white, 0, 2.5f * u);
        dl->AddLine(P(16, 10), P(16, 16), white, 2.5f * u);
        dl->AddLine(P(16, 16), P(20, 19), white, 2.5f * u);
    }
}

// A group row: emblem, name, owner, members. True when clicked.
bool groupRow(const json& g, int index) {
    ImGui::PushID(index);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    bool clicked = ImGui::InvisibleButton("##g", ImVec2(w, 64));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 64), IM_COL32(40, 120, 230, 20), 6);
    dl->AddRect(p, ImVec2(p.x + w, p.y + 64), IM_COL32(200, 205, 215, 255), 6);
    emblem(dl, ImVec2(p.x + 8, p.y + 8), 48, g.value("color", 0x3A7BD5), g.value("name", std::string()));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 66, p.y + 7));
    ImGui::BeginGroup();
    ImGui::TextColored(Classic::kLink, "%s", g.value("name", std::string()).c_str());
    if (g.contains("role")) { ImGui::SameLine(); ImGui::TextDisabled("(%s)", g.value("role", std::string()).c_str()); }
    ImGui::TextDisabled("by %s  -  %lld member%s%s", g.value("ownerName", std::string()).c_str(), g.value("members", 0LL),
                        g.value("members", 0LL) == 1 ? "" : "s", g.value("open", true) ? "" : "  -  ask to join");
    std::string shout = g.value("shout", std::string());
    if (!shout.empty()) {
        if (shout.size() > 80) shout = shout.substr(0, 80) + "...";
        ImGui::TextDisabled("\"%s\"", shout.c_str());
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + 70));
    ImGui::Dummy(ImVec2(0, 0));
    ImGui::PopID();
    return clicked;
}

} // namespace

bool PlayerApp::needsServer(const char* what) {
    if (Online::online()) return false;
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s live on a Guts&Bolts server, so you need to be connected to one.", what);
    ImGui::PopTextWrapPos();
    if (Classic::button("Try again", Classic::kBlue, ImVec2(160, 30))) Online::connect();
    return true;
}

void PlayerApp::openProfile(const std::string& id) {
    m_profileId = id;
    m_profile = json::object();
    m_page = Page::Profile;
    m_socialMsg.clear();
    if (!Online::online()) return;   // drawProfile asks again once we're connected
    Online::request("profile", {{"id", id}}, [this, id](const json& r) {
        if (id != m_profileId) return;
        if (r.value("ok", false)) m_profile = r;
        else m_socialMsg = r.value("error", std::string());
    });
}

void PlayerApp::openGroup(const std::string& id) {
    if (id != m_groupId) { m_group = json::object(); m_editingGroup = false; m_rankEdits.clear(); m_groupMyGames = json::array(); }
    m_groupId = id;
    m_page = Page::Group;
    if (!Online::online()) return;   // drawGroup asks again once we're connected
    Online::request("groups.get", {{"id", id}}, [this, id](const json& r) {
        if (id != m_groupId) return;
        if (r.value("ok", false)) m_group = r;
        else { m_socialMsg = r.value("error", std::string()); m_group = json::object(); }
    });
}

// ---------------------------------------------------------------------------
// People
// ---------------------------------------------------------------------------

void PlayerApp::drawPeople() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("People");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Find other Guts&Bolts players.");
    ImGui::Spacing();
    if (needsServer("Profiles")) return;

    ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 190));
    bool enter = ImGui::InputTextWithHint("##people", "Search by name", &m_peopleQuery, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool first = m_loaded.find("people ") == std::string::npos;   // show recent people when the page opens
    if (first) m_loaded += "people ";
    if (Classic::button("Search", Classic::kBlue) || enter || first) {
        m_socialMsg.clear();
        Online::request("users.search", {{"query", m_peopleQuery}}, [this](const json& r) {
            if (r.value("ok", false)) {
                m_peopleResults = r["users"];
                if (m_peopleResults.empty()) m_socialMsg = m_peopleQuery.empty() ? "Nobody here yet." : "Nobody by that name yet.";
            } else {
                m_socialMsg = r.value("error", std::string());
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("My profile")) openProfile(Account::id());
    if (!m_socialMsg.empty()) ImGui::TextDisabled("%s", m_socialMsg.c_str());
    else if (m_peopleQuery.empty() && !m_peopleResults.empty()) ImGui::TextDisabled("Recently online");
    ImGui::Spacing();

    // A grid of people cards.
    const float cw = 180.0f, ch = 150.0f;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 12) / (cw + 12)));
    for (size_t i = 0; i < m_peopleResults.size(); ++i) {
        const json& u = m_peopleResults[i];
        if (i % perRow != 0) ImGui::SameLine(0, 12);
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##card", ImVec2(cw, ch))) openProfile(u.value("id", std::string()));
        bool hover = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), IM_COL32(255, 255, 255, 255), 6);
        dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), hover ? IM_COL32(40, 120, 230, 255) : IM_COL32(190, 195, 205, 255), 6,
                    0, hover ? 2.0f : 1.0f);
        std::string name = u.value("name", std::string());
        avatarCircle(dl, ImVec2(p.x + cw * 0.5f, p.y + 48), 34, u.value("id", std::string()), name);
        ImVec2 ts = ImGui::CalcTextSize(name.c_str());
        bool ver = u.value("verified", false);
        float nx = p.x + (cw - ts.x - (ver ? 18 : 0)) * 0.5f;
        dl->AddText(ImVec2(nx, p.y + 92), ImGui::ColorConvertFloat4ToU32(Classic::kLink), name.c_str());
        if (ver) Badges::drawCheck(dl, ImVec2(nx + ts.x + 10, p.y + 92 + ts.y * 0.5f), 14);
        const char* tag = u.value("official", false) ? "Guts&Bolts staff" : u.value("staff", false) ? "Staff" : nullptr;
        if (tag) {
            ImVec2 tt = ImGui::CalcTextSize(tag);
            dl->AddText(ImVec2(p.x + (cw - tt.x) * 0.5f, p.y + 114), IM_COL32(200, 40, 40, 255), tag);
        }
        ImGui::PopID();
    }
}

void PlayerApp::buildProfileStage(const json& av, const json& wearing) {
    m_profileScene = std::make_unique<Scene>();
    std::vector<SceneNode*> remove;
    for (auto& c : m_profileScene->root()->children)
        if (c->name != "Baseplate" && !m_profileScene->isProtected(c.get())) remove.push_back(c.get());
    for (auto* r : remove) m_profileScene->removeNode(r);
    Player* p = m_profileScene->player();
    if (!p) return;
    p->setSpawn({0, 0, 0});
    p->build();
    // Their colours (0-255 on the server), then the clothes they wear on top.
    BodyColors bc = Player::colorPresets()[0].second;
    auto color = [&](const char* k, glm::vec3& out) {
        if (!av.is_object() || !av.contains(k) || !av[k].is_array() || av[k].size() != 3) return;
        if (av[k][0].get<double>() < 0) return;
        out = glm::vec3(av[k][0].get<float>(), av[k][1].get<float>(), av[k][2].get<float>()) / 255.0f;
    };
    color("head", bc.head); color("torso", bc.torso); color("leftArm", bc.leftArm);
    color("rightArm", bc.rightArm); color("leftLeg", bc.leftLeg); color("rightLeg", bc.rightLeg);
    HatStyle hat = av.is_object() ? (HatStyle)std::clamp(av.value("hat", 0), 0, kHatStyleCount - 1) : HatStyle::None;
    glm::vec3 hatTint(-1.0f);
    color("hatColor", hatTint);
    // Everything they wear, the way a game shows it: shirt / pants / T-shirt pictures,
    // their face, and Studio-made hats, hair and accessories (downloaded if needed).
    std::vector<Catalog::Item> items;
    if (wearing.is_array())
        for (const auto& a : wearing) items.push_back(Catalog::fromServer(a));
    m_profileRetryAt = dressPlayer(*p, bc, hat, hatTint, items) ? 0.0 : ImGui::GetTime() + 1.0;   // try again once they've downloaded
    Online::fetchSounds(*m_profileScene);
    Environment& e = m_profileScene->environment();
    e.fogEnabled = false;
    e.sunAzimuth = 70.0f;
    e.sunElevation = 40.0f;
    m_profileCam.pivot = {0, 1.4f, 0};
    m_profileCam.yaw = 70.0f;
    m_profileCam.pitch = 8.0f;
    m_profileCam.distance = 6.2f;
    m_profileCam.fov = 45.0f;
}

namespace {
// A box with a blue title bar, like the classic Roblox profile.
void boxTitle(const char* title) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeight() + 10.0f;
    dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), Classic::kNavTop, Classic::kNavTop, Classic::kNavBottom, Classic::kNavBottom);
    dl->AddText(ImVec2(p.x + 8, p.y + 5), IM_COL32(255, 255, 255, 255), title);
    ImGui::Dummy(ImVec2(w, h + 4));
}
} // namespace

void PlayerApp::drawProfile() {
    if (backLink()) m_page = Page::People;
    if (needsServer("Profiles")) return;
    if (m_loaded.find("profile ") == std::string::npos) { m_loaded += "profile "; if (m_profile.empty()) openProfile(m_profileId); }
    if (!m_profile.contains("user")) { ImGui::TextDisabled("%s", m_socialMsg.empty() ? "Loading..." : m_socialMsg.c_str()); return; }
    const json& u = m_profile["user"];
    std::string id = u.value("id", std::string()), name = u.value("name", std::string());
    const json avatar = u.contains("avatar") ? u["avatar"] : json();
    const json wearing = m_profile.contains("wearing") && m_profile["wearing"].is_array() ? m_profile["wearing"] : json::array();
    const json& made = m_profile.contains("creations") ? m_profile["creations"] : json::array();
    long long visits = m_profile.value("placeVisits", 0LL);
    int gamesMade = 0;
    for (const auto& a : made) if (a.value("kind", std::string()) == "game") ++gamesMade;

    // Header: name, online, friend button.
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    if (u.value("verified", false)) { ImGui::SameLine(0, 6); Badges::check(22.0f); }
    if (m_profile.contains("online")) {
        ImGui::SameLine(0, 12);
        bool on = m_profile.value("online", false);
        if (on) ImGui::TextColored(ImVec4(0.1f, 0.6f, 0.25f, 1), "[ Online ]");
        else ImGui::TextDisabled("[ Offline ]");
    }
    std::string fs = m_profile.value("friendship", std::string("none"));
    const bool blocked = m_profile.value("blocked", false);
    if (fs != "self" && !blocked) {
        ImGui::SameLine(0, 16); friendButton(id, fs);
        if (!Online::isGuest()) {
            ImGui::SameLine(0, 8);
            const long long num = u.value("userId", 0LL);
            if (ImGui::Button("Send Message", ImVec2(0, 28))) openNewMessage(num > 0 ? "#" + std::to_string(num) : id);
        }
    }
    if (fs != "self" && canReport() && m_profile.contains("blocked")) {   // (older servers don't know blocking)
        ImGui::SameLine(0, 8);
        if (ImGui::Button(blocked ? "Unblock" : "Block", ImVec2(0, 28))) {
            if (blocked) setBlocked(id, false);
            else ImGui::OpenPopup("Block##profile");
        }
        ImGui::SameLine(0, 8);
        if (ImGui::Button("Report", ImVec2(0, 28))) openReport("user", id, name, id);
        if (ImGui::BeginPopupModal("Block##profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Block %s?", name.c_str());
            ImGui::TextDisabled("You won't be friends any more, and you can't message, follow, trade with or join each other.");
            if (Classic::button("Block", ImVec4(0.78f, 0.2f, 0.2f, 1), ImVec2(100, 28))) { setBlocked(id, true); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 28))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    if (blocked) ImGui::TextColored(ImVec4(0.15f, 0.3f, 0.6f, 1), "You blocked %s. You can't message, friend, follow, trade with or join each other.", name.c_str());
    if (m_profile.contains("playing") && m_profile["playing"].is_object()) {
        const json& pl = m_profile["playing"];
        ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "Playing %s", pl.value("title", std::string()).c_str());
        if (fs != "self" && pl.contains("session") && pl["session"].is_string()) {
            ImGui::SameLine(0, 12);
            bool full = pl.value("full", false);
            ImGui::BeginDisabled(full);
            if (Classic::button(full ? "Full" : "Join", Classic::kPlay, ImVec2(80, 28)))
                joinRelay(pl["session"].get<std::string>(), "", pl.value("title", std::string()));
            ImGui::EndDisabled();
        }
    }
    if (u.value("official", false)) ImGui::TextColored(ImVec4(0.8f, 0.15f, 0.15f, 1), "Guts&Bolts staff");
    else if (u.value("staff", false)) ImGui::TextColored(ImVec4(0.15f, 0.3f, 0.6f, 1), "Staff");
    if (u.value("banned", false)) ImGui::TextColored(ImVec4(0.8f, 0.1f, 0.1f, 1), "This account is banned.");
    ImGui::Spacing();

    const bool tall = portraitScreen();
    const float leftW = tall ? ImGui::GetContentRegionAvail().x : 300.0f;
    ImGui::BeginGroup();
    ImGui::BeginChild("##profileLeft", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY);

    // The avatar in 3D (drag to turn).
    std::string key = id + avatar.dump() + wearing.dump();
    if (m_profileSceneFor != key || !m_profileScene || (m_profileRetryAt > 0 && ImGui::GetTime() > m_profileRetryAt)) {
        buildProfileStage(avatar, wearing);
        m_profileSceneFor = key;
    }
    {
        ImVec2 size(ImGui::GetContentRegionAvail().x, tall ? 240.0f : 330.0f);
        const float fb = ImGui::GetIO().DisplayFramebufferScale.x;
        m_profileView.resize((int)(size.x * fb), (int)(size.y * fb));
        m_profileCam.resize((int)(size.x * fb), (int)(size.y * fb));
        m_renderer->render(*m_profileScene, m_profileCam, m_profileView, false);
        ImGui::Image((ImTextureID)(intptr_t)m_profileView.colorTexture(), size, ImVec2(0, 1), ImVec2(1, 0));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            m_profileCam.yaw += ImGui::GetIO().MouseDelta.x * 0.5f;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to turn");
    }
    ImGui::Spacing();

    boxTitle("Currently Wearing");
    if (wearing.empty()) ImGui::TextDisabled("Nothing from the catalog.");
    drawItemTiles(wearing, 3);
    ImGui::Spacing();

    boxTitle("Statistics");
    {
        long long created = u.value("created", 0LL);
        std::time_t tt = (std::time_t)created;
        char since[32];
        std::strftime(since, sizeof(since), "%b %d, %Y", std::localtime(&tt));
        auto row = [](const char* k, const std::string& v) {
            ImGui::TextDisabled("%s", k);
            ImGui::SameLine(120);
            ImGui::TextUnformatted(v.c_str());
        };
        row("Joined", since);
        row("Username", "@" + u.value("username", std::string()));
        row("User number", "#" + std::to_string(u.value("userId", 0LL)));
        // The counts are links: click to see the people.
        auto linkRow = [&](const char* k, long long n, const char* which) {
            ImGui::TextDisabled("%s", k);
            ImGui::SameLine(120);
            ImGui::PushID(which);
            if (nameLink({{"name", std::to_string(n)}}, which)) openPeople(id, which);
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        };
        linkRow("Friends", m_profile.value("friendCount", 0LL), "friends");
        if (m_profile.contains("followerCount")) {
            linkRow("Followers", m_profile.value("followerCount", 0LL), "followers");
            linkRow("Following", m_profile.value("followingCount", 0LL), "following");
        }
        if (m_profile.contains("placeVisits")) row("Place visits", std::to_string(visits));
        row("Games made", std::to_string(gamesMade));
    }
    ImGui::Spacing();

    boxTitle("Guts&Bolts Badges");   // given by staff
    int shown = 0;
    if (u.contains("badges"))
        for (const auto& k : u["badges"]) {
            Badges::Id bid;
            if (!k.is_string() || !Badges::fromKey(k.get<std::string>(), bid)) continue;
            if (shown++ % 3) ImGui::SameLine(0, 14);
            ImGui::BeginGroup();
            Badges::icon(bid, 48.0f);
            ImGui::TextDisabled("%s", Badges::info(bid).name);
            ImGui::EndGroup();
        }
    if (!shown) ImGui::TextDisabled("No badges yet.");
    ImGui::Spacing();

    // Game badges: made by game creators, earned by playing their games.
    const json& gb = m_profile.contains("gameBadges") && m_profile["gameBadges"].is_array() ? m_profile["gameBadges"] : json::array();
    std::string gt = "Game Badges (" + std::to_string(gb.size()) + ")";
    boxTitle(gt.c_str());
    if (gb.empty()) ImGui::TextDisabled("No badges from games yet.");
    for (size_t i = 0; i < gb.size() && i < 24; ++i) {
        const json& b = gb[i];
        if (i % 3) ImGui::SameLine(0, 14);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(48, 48));
        ImU32 col = IM_COL32(240, 180, 40, 255);
        if (b.contains("color") && b["color"].is_array() && b["color"].size() == 3)
            col = IM_COL32(b["color"][0].get<int>(), b["color"][1].get<int>(), b["color"][2].get<int>(), 255);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c(p.x + 24, p.y + 24);
        dl->AddCircleFilled(c, 22, col, 32);
        dl->AddCircle(c, 22, IM_COL32(0, 0, 0, 80), 32, 2.0f);
        for (int k = 0; k < 5; ++k) {   // a white star
            float a0 = -1.5708f + k * 1.2566f, a1 = a0 + 0.6283f;
            dl->AddTriangleFilled(c, ImVec2(c.x + std::cos(a0) * 13, c.y + std::sin(a0) * 13),
                                  ImVec2(c.x + std::cos(a1) * 5.5f, c.y + std::sin(a1) * 5.5f), IM_COL32(255, 255, 255, 230));
            dl->AddTriangleFilled(c, ImVec2(c.x + std::cos(a1) * 5.5f, c.y + std::sin(a1) * 5.5f),
                                  ImVec2(c.x + std::cos(a0 + 1.2566f) * 13, c.y + std::sin(a0 + 1.2566f) * 13), IM_COL32(255, 255, 255, 230));
        }
        std::string name = b.value("name", std::string());
        if (name.size() > 12) name = name.substr(0, 11) + "...";
        ImGui::TextDisabled("%s", name.c_str());
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n%s\nFrom %s", b.value("name", std::string()).c_str(), b.value("description", std::string()).c_str(),
                              b.value("gameName", std::string()).c_str());
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    if (!tall) ImGui::SameLine(0, 18);

    ImGui::BeginChild("##profileRight", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
    {   // About: "Right now I'm..." and the "About me" blurb (yours can be changed here)
        boxTitle(("About " + name).c_str());
        ImGui::PushTextWrapPos(0);
        if (m_profile.contains("status") && m_profile["status"].is_object()) {
            const json& st = m_profile["status"];
            ImGui::TextColored(Classic::kBlue, "Right now:");
            ImGui::SameLine();
            ImGui::Text("\"%s\"  (%s)", st.value("text", std::string()).c_str(), sinceText(st.value("at", 0LL)).c_str());
        }
        const std::string blurb = m_profile.value("blurb", std::string());
        if (!blurb.empty()) ImGui::TextUnformatted(blurb.c_str());
        else ImGui::TextDisabled(fs == "self" ? "Tell people about yourself below." : "Nothing here yet.");
        ImGui::PopTextWrapPos();
        if (fs == "self" && m_profile.contains("blurb")) {
            if (ImGui::TreeNode("Edit")) {
                if (ImGui::IsItemToggledOpen() || m_blurbEdit.empty()) m_blurbEdit = blurb;
                ImGui::TextUnformatted("Right now I'm...");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90);
                if (ImGui::InputText("##status", &m_statusEdit) && m_statusEdit.size() > 140) m_statusEdit.resize(140);
                ImGui::SameLine();
                if (Classic::button("Update", Classic::kBlue, ImVec2(80, 0)) && !m_statusEdit.empty()) {
                    Online::request("profile.set", {{"status", m_statusEdit}}, [this](const json& r) {
                        if (r.value("ok", false)) { m_profile["status"] = r["status"]; m_statusEdit.clear(); }
                        else m_socialMsg = r.value("error", std::string());
                    });
                }
                ImGui::TextUnformatted("About me");
                if (ImGui::InputTextMultiline("##blurb", &m_blurbEdit, ImVec2(ImGui::GetContentRegionAvail().x, 90)) && m_blurbEdit.size() > 1000)
                    m_blurbEdit.resize(1000);
                if (Classic::button("Save", Classic::kPlay, ImVec2(80, 26))) {
                    Online::request("profile.set", {{"blurb", m_blurbEdit}}, [this](const json& r) {
                        if (r.value("ok", false)) m_profile["blurb"] = r.value("blurb", std::string());
                        else m_socialMsg = r.value("error", std::string());
                    });
                }
                ImGui::TreePop();
            }
        }
        ImGui::Spacing();
    }
    if (m_profile.contains("allPlayerBadges")) {   // earned on their own by playing and building
        std::vector<std::string> have;
        for (const auto& b : m_profile.value("playerBadges", json::array())) have.push_back(b.value("key", std::string()));
        const json& all = m_profile["allPlayerBadges"];
        boxTitle(("Player Badges (" + std::to_string(have.size()) + ")").c_str());
        int n = 0;
        for (const auto& b : all) {
            const std::string key = b.value("key", std::string());
            const bool got = std::find(have.begin(), have.end(), key) != have.end();
            if (n > 0 && ImGui::GetItemRectMax().x + 50.0f < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
                ImGui::SameLine(0, 10);   // wraps on narrow phone screens
            ImGui::PushID(n++);
            playerBadgeIcon(key, got, 40.0f);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s%s\n%s", b.value("name", std::string()).c_str(), got ? " - earned!" : " (locked)",
                                  b.value("need", std::string()).c_str());
            ImGui::PopID();
        }
        ImGui::Spacing();
    }
    {
        std::string t = "Friends (" + std::to_string(m_profile.value("friendCount", 0LL)) + ")";
        boxTitle(t.c_str());
        if (m_profile.value("friendCount", 0LL) > 0) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
            if (nameLink({{"name", "See all"}}, "seeall")) openPeople(id, "friends");
        }
        const json& friends = m_profile.contains("friends") && m_profile["friends"].is_array() ? m_profile["friends"] : json::array();
        if (friends.empty()) ImGui::TextDisabled("No friends yet.");
        const float cw = 96.0f, ch = 110.0f;
        int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 10) / (cw + 10)));
        for (size_t i = 0; i < friends.size(); ++i) {
            const json& f = friends[i];
            if (i % perRow != 0) ImGui::SameLine(0, 10);
            ImGui::PushID((int)i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##f", ImVec2(cw, ch))) openProfile(f.value("id", std::string()));
            bool hover = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (hover) dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), IM_COL32(40, 120, 230, 30), 6);
            std::string fname = f.value("name", std::string());
            avatarCircle(dl, ImVec2(p.x + cw * 0.5f, p.y + 40), 32, f.value("id", std::string()), fname);
            ImU32 dot = f.value("online", false) ? IM_COL32(34, 179, 94, 255) : IM_COL32(170, 170, 170, 255);
            dl->AddCircleFilled(ImVec2(p.x + cw * 0.5f + 24, p.y + 64), 6, dot);
            ImVec2 ts = ImGui::CalcTextSize(fname.c_str());
            dl->AddText(ImVec2(p.x + std::max(0.0f, (cw - ts.x) * 0.5f), p.y + 82),
                        ImGui::ColorConvertFloat4ToU32(Classic::kLink), fname.c_str());
            ImGui::PopID();
        }
    }
    ImGui::Spacing();
    boxTitle("Games");
    {
        int n = 0;
        for (const auto& a : made) {
            if (a.value("kind", std::string()) != "game") continue;
            ++n;
            ImGui::BulletText("%s", a.value("name", std::string()).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%lld plays", a.value("plays", 0LL));
        }
        if (!n) ImGui::TextDisabled("None yet.");
    }
    ImGui::Spacing();
    boxTitle("Creations");
    {
        int n = 0;
        for (const auto& a : made) {
            std::string kind = a.value("kind", std::string());
            if (kind == "game") continue;
            ++n;
            ImGui::BulletText("%s", a.value("name", std::string()).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", Online::kindTitle(kind));
            long long price = a.value("price", 0LL);
            if (price > 0) { ImGui::SameLine(); Bolts::amount(price); }
        }
        if (!n) ImGui::TextDisabled("Nothing published yet.");
    }
    ImGui::Spacing();
    if (m_profile.contains("inventory") && m_profile["inventory"].is_object()) {   // what they own (older servers don't say)
        const json& inv = m_profile["inventory"];
        const bool hidden = inv.value("hidden", false);
        boxTitle(hidden ? "Inventory" : ("Inventory (" + std::to_string(inv.value("total", 0LL)) + ")").c_str());
        if (!hidden && inv.value("total", 0LL) > 0) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
            if (nameLink({{"name", "See all"}}, "invall")) openInventory(id, "all");
        }
        const json& items = inv.contains("items") && inv["items"].is_array() ? inv["items"] : json::array();
        if (hidden) ImGui::TextDisabled("%s keeps their inventory private.", name.c_str());
        else if (items.empty()) ImGui::TextDisabled("Nothing from the catalog yet.");
        else drawItemTiles(items, std::max(3, (int)(ImGui::GetContentRegionAvail().x / 110.0f)));
        ImGui::Spacing();
    }
    boxTitle("Groups");
    const json& groups = m_profile.contains("groups") ? m_profile["groups"] : json::array();
    if (groups.empty()) ImGui::TextDisabled("Not in any groups.");
    for (size_t i = 0; i < groups.size(); ++i)
        if (groupRow(groups[i], (int)i)) openGroup(groups[i].value("id", std::string()));
    ImGui::EndChild();
    drawPeopleDialog();
    drawInventoryDialog();
}

void PlayerApp::drawItemTiles(const json& items, int perRow) {
    const float tile = (ImGui::GetContentRegionAvail().x - 8.0f * (perRow - 1)) / (float)perRow;
    for (size_t i = 0; i < items.size(); ++i) {
        Catalog::Item it = Catalog::fromServer(items[i]);
        if (i % perRow != 0) ImGui::SameLine(0, 8);
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(255, 255, 255, 255));
        dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(170, 175, 185, 255));
        itemPicture(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile * 0.8f, it);
        if (items[i].contains("limited") && items[i]["limited"].is_object())   // like the website's green LIMITED tag
            dl->AddText(ImVec2(p.x + 4, p.y + 3), IM_COL32(26, 127, 55, 255), "LIMITED");
        ImGui::Dummy(ImVec2(tile, tile));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextColored(Classic::kLink, "%s", it.name.c_str());
        if (items[i].contains("serials") && items[i]["serials"].is_array() && !items[i]["serials"].empty()) {
            std::string nums;   // which numbered copies they hold
            for (const json& n : items[i]["serials"]) nums += (nums.empty() ? "#" : ", #") + std::to_string(n.get<long long>());
            ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.22f, 1), "%s", nums.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
    }
}

void PlayerApp::openInventory(const std::string& user, const std::string& cat, int page) {
    m_invUser = user;
    m_invCat = cat;
    m_invPage = std::max(0, page);
    m_inv = json::array();
    m_invTotal = 0;
    m_invMsg = "Loading...";
    const int per = 30;
    Online::request("profile.inventory", {{"id", user}, {"cat", cat}, {"offset", m_invPage * per}, {"limit", per}},
                    [this, user, cat](const json& r) {
        if (user != m_invUser || cat != m_invCat) return;
        if (!r.value("ok", false)) { m_invMsg = r.value("error", std::string("Couldn't load that.")); return; }
        if (r.value("hidden", false)) { m_invMsg = "This inventory is private."; return; }
        m_inv = r.value("items", json::array());
        m_invCounts = r.value("counts", json::object());
        m_invTotal = r.value("total", 0LL);
        m_invMsg.clear();
    });
}

void PlayerApp::drawInventoryDialog() {
    if (m_invCat.empty()) return;
    if (!ImGui::IsPopupOpen("##inventory")) ImGui::OpenPopup("##inventory");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(640), 0));
    if (!ImGui::BeginPopupModal("##inventory", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) return;
    if (tappedOutside()) m_invCat.clear();
    if (m_invCat.empty()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    const json& u = m_profile.contains("user") ? m_profile["user"] : json::object();
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%s's Inventory", u.value("name", std::string()).c_str());
    ImGui::SetWindowFontScale(1.0f);
    // The same groups as the website (worker/server.js INVENTORY_CATS); they wrap on narrow screens.
    static const char* kTabs[][2] = {{"all", "All"}, {"accessories", "Accessories"}, {"clothing", "Clothing"},
                                     {"faces", "Faces"}, {"gear", "Gear"}, {"emotes", "Emotes"}, {"limited", "Limiteds"}};
    for (int i = 0; i < (int)(sizeof(kTabs) / sizeof(kTabs[0])); ++i) {
        const std::string label = std::string(kTabs[i][1]) + " (" + std::to_string(m_invCounts.value(kTabs[i][0], 0LL)) + ")";
        const float w = ImGui::CalcTextSize(label.c_str()).x + 20;
        if (i && ImGui::GetItemRectMax().x + 8 + w < ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine(0, 6);
        const bool on = m_invCat == kTabs[i][0];
        if (on ? Classic::button(label.c_str(), Classic::kBlue, ImVec2(w, 26)) : ImGui::Button(label.c_str(), ImVec2(w, 26)))
            if (!on) openInventory(m_invUser, kTabs[i][0]);
    }
    ImGui::Separator();
    if (!m_invMsg.empty()) ImGui::TextDisabled("%s", m_invMsg.c_str());
    else if (m_inv.empty()) ImGui::TextDisabled(m_invCat == "all" ? "Nothing from the catalog yet." : "Nothing here.");
    // As tall as the rows need (a tile plus two lines of text each), scrolling past that.
    const int perRow = std::max(2, (int)(ImGui::GetContentRegionAvail().x / 110.0f)), rows = ((int)m_inv.size() + perRow - 1) / perRow;
    const float tileH = (ImGui::GetContentRegionAvail().x - 8.0f * (perRow - 1)) / (float)perRow + ImGui::GetTextLineHeightWithSpacing() * 2 + 8;
    ImGui::BeginChild("##invlist", ImVec2(0, std::min({380.0f, ImGui::GetMainViewport()->Size.y * 0.55f, rows * tileH + 8})));
    drawItemTiles(m_inv, perRow);
    ImGui::EndChild();
    const int per = 30, pages = (int)std::max(1LL, (m_invTotal + per - 1) / per);
    if (pages > 1) {
        ImGui::BeginDisabled(m_invPage <= 0);
        if (ImGui::Button("< Back")) openInventory(m_invUser, m_invCat, m_invPage - 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Page %d of %d", m_invPage + 1, pages);
        ImGui::SameLine();
        ImGui::BeginDisabled(m_invPage + 1 >= pages);
        if (ImGui::Button("Next >")) openInventory(m_invUser, m_invCat, m_invPage + 1);
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Close", ImVec2(100, 30))) m_invCat.clear();
    if (m_invCat.empty()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void PlayerApp::openPeople(const std::string& user, const std::string& which, int page) {
    m_peopleUser = user;
    m_peopleWhich = which;
    m_peoplePage = std::max(0, page);
    m_people = json::array();
    m_peopleTotal = 0;
    m_peopleMsg = "Loading...";
    const int per = 50;
    Online::request("people.list", {{"user", user}, {"which", which}, {"offset", m_peoplePage * per}, {"limit", per}},
                    [this, user, which](const json& r) {
        if (user != m_peopleUser || which != m_peopleWhich) return;
        if (!r.value("ok", false)) { m_peopleMsg = r.value("error", std::string("Couldn't load that list.")); return; }
        m_people = r.value("people", json::array());
        m_peopleTotal = r.value("total", 0LL);
        m_peopleMsg.clear();
    });
}

void PlayerApp::drawPeopleDialog() {
    if (m_peopleWhich.empty()) return;
    if (!ImGui::IsPopupOpen("##people")) ImGui::OpenPopup("##people");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(fitWidth(460), 0));
    if (!ImGui::BeginPopupModal("##people", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) return;
    if (tappedOutside()) m_peopleWhich.clear();
    if (m_peopleWhich.empty()) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
    const json& u = m_profile.contains("user") ? m_profile["user"] : json::object();
    ImGui::SetWindowFontScale(1.3f);
    ImGui::Text("%s", u.value("name", std::string()).c_str());
    ImGui::SetWindowFontScale(1.0f);
    static const char* kTabs[][2] = {{"friends", "Friends"}, {"following", "Following"}, {"followers", "Followers"}};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool on = m_peopleWhich == kTabs[i][0];
        if (on ? Classic::button(kTabs[i][1], Classic::kBlue, ImVec2(110, 26)) : ImGui::Button(kTabs[i][1], ImVec2(110, 26)))
            if (!on) openPeople(m_peopleUser, kTabs[i][0]);
    }
    ImGui::Separator();
    if (!m_peopleMsg.empty()) ImGui::TextDisabled("%s", m_peopleMsg.c_str());
    else if (m_people.empty()) ImGui::TextDisabled(m_peopleWhich == "friends" ? "No friends yet." : m_peopleWhich == "following" ? "Not following anyone yet." : "No followers yet.");
    ImGui::BeginChild("##plist", ImVec2(0, std::min(360.0f, 8.0f + 30.0f * (float)std::max<size_t>(1, m_people.size()))));
    std::string go;
    for (size_t i = 0; i < m_people.size(); ++i) {
        const json& p = m_people[i];
        ImGui::PushID((int)i);
        ImVec2 at = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetTextLineHeight();
        avatarCircle(ImGui::GetWindowDrawList(), ImVec2(at.x + 11, at.y + h * 0.5f + 2), 11, p.value("id", std::string()), p.value("name", std::string()));
        ImGui::Dummy(ImVec2(24, h + 4));
        ImGui::SameLine();
        if (nameLink(p, "n")) go = p.value("id", std::string());   // blue: click to see their profile
        ImGui::SameLine();
        ImGui::TextDisabled("#%lld%s", p.value("userId", 0LL), p.value("online", false) ? "  (online)" : "");
        ImGui::PopID();
    }
    ImGui::EndChild();
    const int per = 50, pages = (int)std::max(1LL, (m_peopleTotal + per - 1) / per);
    if (pages > 1) {
        ImGui::BeginDisabled(m_peoplePage <= 0);
        if (ImGui::Button("< Back")) openPeople(m_peopleUser, m_peopleWhich, m_peoplePage - 1);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Page %d of %d", m_peoplePage + 1, pages);
        ImGui::SameLine();
        ImGui::BeginDisabled(m_peoplePage + 1 >= pages);
        if (ImGui::Button("Next >")) openPeople(m_peopleUser, m_peopleWhich, m_peoplePage + 1);
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Close", ImVec2(100, 30))) m_peopleWhich.clear();
    if (m_peopleWhich.empty() || !go.empty()) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (!go.empty()) { m_peopleWhich.clear(); openProfile(go); }
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

void PlayerApp::drawGroups() {
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextUnformatted("Groups");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Communities for people who like the same things.");
    ImGui::Spacing();
    if (needsServer("Groups")) return;

    const char* tabs[] = {"My Groups", "Browse", "Create a Group"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        bool on = m_groupsTab == i;
        float tw = std::min(140.0f, (ImGui::GetContentRegionAvail().x - 16) / (3 - i));
        if (on ? Classic::button(tabs[i], Classic::kBlue, ImVec2(tw, 28)) : ImGui::Button(tabs[i], ImVec2(tw, 28))) {
            m_groupsTab = i;
            m_socialMsg.clear();
            if (size_t at = m_loaded.find("groups "); at != std::string::npos) m_loaded.erase(at, 7);   // refresh the list
        }
    }
    ImGui::Separator();
    ImGui::Spacing();

    auto loadList = [this]() {
        if (m_groupsTab == 0)
            Online::request("groups.mine", json::object(), [this](const json& r) { if (r.value("ok", false)) m_myGroups = r["groups"]; });
        else
            Online::request("groups.list", {{"query", m_groupQuery}}, [this](const json& r) { if (r.value("ok", false)) m_groupList = r["groups"]; });
    };
    if (m_groupsTab < 2 && m_loaded.find("groups ") == std::string::npos) { m_loaded += "groups "; loadList(); }

    if (m_groupsTab == 0) {
        if (m_myGroups.empty()) {
            ImGui::TextDisabled("You're not in any groups yet. Find one under Browse, or make your own!");
            return;
        }
        for (size_t i = 0; i < m_myGroups.size(); ++i)
            if (groupRow(m_myGroups[i], (int)i)) openGroup(m_myGroups[i].value("id", std::string()));
        return;
    }
    if (m_groupsTab == 1) {
        ImGui::SetNextItemWidth(std::min(360.0f, ImGui::GetContentRegionAvail().x - 90));
        bool enter = ImGui::InputTextWithHint("##gq", "Search groups", &m_groupQuery, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (Classic::button("Search", Classic::kBlue) || enter) loadList();
        ImGui::Spacing();
        if (m_groupList.empty()) ImGui::TextDisabled("No groups found. Why not make one?");
        for (size_t i = 0; i < m_groupList.size(); ++i)
            if (groupRow(m_groupList[i], (int)i)) openGroup(m_groupList[i].value("id", std::string()));
        return;
    }

    // Create a group.
    const bool verified = Online::verified();
    float fieldW = std::min(360.0f, ImGui::GetContentRegionAvail().x - 110);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputTextWithHint("Name", "At least 3 letters", &m_newGroupName);
    ImGui::InputTextMultiline("About", &m_newGroupDesc, ImVec2(fieldW, 70));
    ImGui::SetNextItemWidth(fieldW);
    ImGui::ColorEdit3("Colour", &m_newGroupColor.x);
    ImGui::Checkbox("Anyone can join (untick: people ask first)", &m_newGroupOpen);
    ImGui::EndGroup();
    if (!portraitScreen()) {
        ImGui::SameLine(0, 24);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(96, 96));
        int col = ((int)std::lround(m_newGroupColor.r * 255) << 16) | ((int)std::lround(m_newGroupColor.g * 255) << 8) |
                  (int)std::lround(m_newGroupColor.b * 255);
        emblem(ImGui::GetWindowDrawList(), p, 96, col, m_newGroupName.empty() ? "?" : m_newGroupName);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", verified ? "Making a group is free for you (Verified)." : "Making a group costs 50 Bolts (free for Verified people).");
    ImGui::BeginDisabled(m_busy);
    if (Classic::button(m_busy ? "Making..." : "Create group", Classic::kPlay, ImVec2(180, 34))) {
        int col = ((int)std::lround(m_newGroupColor.r * 255) << 16) | ((int)std::lround(m_newGroupColor.g * 255) << 8) |
                  (int)std::lround(m_newGroupColor.b * 255);
        m_busy = true;
        Online::request("groups.create", {{"name", m_newGroupName}, {"description", m_newGroupDesc}, {"color", col},
                                          {"open", m_newGroupOpen}}, [this](const json& r) {
            m_busy = false;
            if (!r.value("ok", false)) { m_socialMsg = r.value("error", std::string()); return; }
            m_newGroupName.clear();
            m_newGroupDesc.clear();
            m_socialMsg.clear();
            m_loaded.clear();
            openGroup(r["group"].value("id", std::string()));
        });
    }
    ImGui::EndDisabled();
    if (!m_socialMsg.empty()) ImGui::TextColored(ImVec4(0.8f, 0.15f, 0.1f, 1), "%s", m_socialMsg.c_str());
}

namespace {
// What each group permission lets a rank do (the server checks the same).
const char* const kGroupPerms[][2] = {{"shout", "Shout"}, {"manage", "Let people in, remove people, change settings"},
                                      {"ranks", "Change people's ranks"}, {"games", "Add their games to the group"},
                                      {"funds", "See the group's Bolts and pay people"}};
} // namespace

void PlayerApp::drawGroup() {
    if (backLink()) { m_page = Page::Groups; m_loaded.clear(); }
    if (needsServer("Groups")) return;
    if (m_loaded.find("group ") == std::string::npos) { m_loaded += "group "; if (m_group.empty()) openGroup(m_groupId); }
    if (!m_group.contains("group")) { ImGui::TextDisabled("%s", m_socialMsg.empty() ? "Loading..." : m_socialMsg.c_str()); return; }
    const json g = m_group["group"];
    const std::string myRole = m_group.value("myRole", std::string());
    // My rank and what it can do. (An older server only says Owner / Admin / Member.)
    json myRank = m_group.contains("myRank") && m_group["myRank"].is_object() ? m_group["myRank"] : json();
    if (myRank.is_null() && !myRole.empty())
        myRank = {{"id", myRole}, {"name", myRole}, {"level", myRole == "Owner" ? 255 : myRole == "Admin" ? 200 : 1},
                  {"perms", myRole == "Admin" ? json{"shout", "manage", "games"} : json::array()}};
    const bool member = myRank.is_object(), owner = member && myRank.value("id", std::string()) == "Owner";
    const int myLevel = member ? myRank.value("level", 0) : 0;
    auto can = [&](const char* perm) {
        if (!member) return false;
        if (owner) return true;
        for (const auto& p : myRank.value("perms", json::array())) if (p == perm) return true;
        return false;
    };
    const bool manage = can("manage");
    const bool staff = Online::staff();
    const json ranks = m_group.value("ranks", json::array());
    auto act = [this](const std::string& op, json args) {
        args["id"] = m_groupId;
        Online::request(op, args, [this](const json& r) {
            m_socialMsg = r.value("ok", false) ? std::string() : r.value("error", std::string());
            if (r.value("ok", false) && r.value("requested", false)) m_socialMsg = "Asked to join - an admin will let you in.";
            openGroup(m_groupId);   // show the new state
        });
    };

    // --- Header ---
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    emblem(dl, p, 110, g.value("color", 0x3A7BD5), g.value("name", std::string()));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 126, p.y + 2));
    ImGui::BeginGroup();
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextUnformatted(g.value("name", std::string()).c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextDisabled("Owned by");
    ImGui::SameLine(0, 4);
    if (nameLink({{"id", g.value("owner", std::string())}, {"name", g.value("ownerName", std::string())},
                  {"verified", g.value("ownerVerified", false)}}, "owner"))
        openProfile(g.value("owner", std::string()));
    ImGui::TextDisabled("%lld member%s  -  %s", g.value("members", 0LL), g.value("members", 0LL) == 1 ? "" : "s",
                        g.value("open", true) ? "anyone can join" : "ask to join");
    if (member) ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "Your rank: %s", myRank.value("name", myRole).c_str());
    ImGui::Spacing();
    if (!member) {
        bool requested = m_group.value("requested", false);
        ImGui::BeginDisabled(requested);
        if (Classic::button(requested ? "Asked to join" : g.value("open", true) ? "Join group" : "Ask to join", Classic::kPlay,
                            ImVec2(150, 30)))
            act("groups.join", json::object());
        ImGui::EndDisabled();
    } else if (!owner) {
        if (ImGui::Button("Leave group", ImVec2(120, 28))) act("groups.leave", json::object());
    }
    if (!owner && canReport()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Report##group")) openReport("group", g.value("id", std::string()), g.value("name", std::string()));
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(ImVec2(p.x, std::max(p.y + 118, ImGui::GetItemRectMax().y + 8)));
    ImGui::Dummy(ImVec2(0, 0));
    if (!m_socialMsg.empty()) ImGui::TextColored(ImVec4(0.75f, 0.2f, 0.1f, 1), "%s", m_socialMsg.c_str());

    ImGui::PushTextWrapPos(0);
    std::string about = g.value("description", std::string());
    if (!about.empty()) ImGui::TextUnformatted(about.c_str());
    ImGui::PopTextWrapPos();

    // --- Shout: the group's pinned message ---
    const json& shout = m_group.contains("shoutInfo") ? m_group["shoutInfo"] : json::object();
    if (shout.contains("text") || can("shout")) {
        ImVec2 s0 = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(s0, ImVec2(s0.x + w, s0.y + 58), IM_COL32(255, 248, 225, 255), 6);
        dl->AddRect(s0, ImVec2(s0.x + w, s0.y + 58), IM_COL32(225, 190, 90, 255), 6);
        ImGui::SetCursorScreenPos(ImVec2(s0.x + 10, s0.y + 6));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(s0.x + w - 10);
        if (shout.contains("text")) {
            ImGui::TextUnformatted(shout.value("text", std::string()).c_str());
            ImGui::TextDisabled("- %s, %s", shout.value("name", std::string()).c_str(), when(shout.value("time", 0LL)).c_str());
        } else {
            ImGui::TextDisabled("No shout yet. Shouts are a message pinned to the top of the group.");
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SetCursorScreenPos(ImVec2(s0.x, s0.y + 64));
        ImGui::Dummy(ImVec2(0, 0));
        if (can("shout")) {
            ImGui::SetNextItemWidth(std::min(400.0f, ImGui::GetContentRegionAvail().x - 90));
            ImGui::InputTextWithHint("##shout", "New shout", &m_shoutInput);
            ImGui::SameLine();
            if (ImGui::Button("Shout")) { act("groups.shout", {{"text", m_shoutInput}}); m_shoutInput.clear(); }
        }
    }

    // Wall / Members / Games / Bolts / Admin, as simple buttons (they match the rest of the site)
    enum Tab { Wall, Members, Games, Funds, Admin };
    const json& reqList = m_group.contains("requests") ? m_group["requests"] : json::array();
    const json& games = m_group.contains("games") ? m_group["games"] : json::array();
    std::vector<std::pair<Tab, std::string>> tabs = {{Wall, "Wall"}, {Members, "Members (" + std::to_string(g.value("members", 0LL)) + ")"}};
    if (!games.empty() || can("games")) tabs.push_back({Games, "Games (" + std::to_string(games.size()) + ")"});
    if (m_group.contains("funds")) tabs.push_back({Funds, "Group Bolts"});
    if (manage || owner || staff) tabs.push_back({Admin, reqList.empty() ? std::string("Admin") : "Admin (" + std::to_string(reqList.size()) + ")"});
    if (m_groupTab >= (int)tabs.size()) m_groupTab = 0;
    const Tab tab = tabs[m_groupTab].first;
    ImGui::Spacing();
    for (int i = 0; i < (int)tabs.size(); ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        const char* label = tabs[i].second.c_str();
        if (m_groupTab == i ? Classic::button(label, Classic::kBlue, ImVec2(118, 28)) : ImGui::Button(label, ImVec2(118, 28)))
            m_groupTab = i;
        ImGui::PopID();
    }
    ImGui::Separator();

    // --- Wall ---
    if (tab == Wall) {
        if (member) {
            ImGui::SetNextItemWidth(std::min(460.0f, ImGui::GetContentRegionAvail().x - 80));
            bool enter = ImGui::InputTextWithHint("##post", "Say something to the group", &m_wallInput,
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if ((ImGui::Button("Post") || enter) && !m_wallInput.empty()) {
                act("groups.post", {{"text", m_wallInput}});
                m_wallInput.clear();
            }
        } else {
            ImGui::TextDisabled("Join the group to post here.");
        }
        const json& wall = m_group.contains("wall") ? m_group["wall"] : json::array();
        if (wall.empty()) ImGui::TextDisabled("Nothing on the wall yet.");
        for (size_t k = wall.size(); k-- > 0;) {
            const json& post = wall[k];
            ImGui::PushID((int)k);
            ImGui::Separator();
            if (nameLink(post, "poster")) openProfile(post.value("id", std::string()));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", when(post.value("time", 0LL)).c_str());
            if (post.value("id", std::string()) == Account::id() || manage || staff) {
                ImGui::SameLine();
                if (ImGui::SmallButton("delete"))
                    act("groups.deletePost", {{"time", post.value("time", 0LL)}, {"by", post.value("id", std::string())}});
            }
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(post.value("text", std::string()).c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopID();
        }
    }
    // --- Members ---
    if (tab == Members) {
        const json& list = m_group.contains("memberList") ? m_group["memberList"] : json::array();
        for (size_t i = 0; i < list.size(); ++i) {
            const json& m = list[i];
            const std::string id = m.value("id", std::string()), role = m.value("role", std::string());
            const std::string rankId = m.value("rank", role);
            const int level = m.value("level", role == "Owner" ? 255 : role == "Admin" ? 200 : 1);
            const bool below = level < myLevel && id != Account::id() && rankId != "Owner";
            ImGui::PushID((int)i);
            avatarCircle(ImGui::GetWindowDrawList(), ImVec2(ImGui::GetCursorScreenPos().x + 11, ImGui::GetCursorScreenPos().y + 10),
                         10, id, m.value("name", std::string()));
            ImGui::Dummy(ImVec2(24, 20));
            ImGui::SameLine();
            if (nameLink(m, "member")) openProfile(id);
            ImGui::SameLine();
            if (can("ranks") && below && !ranks.empty()) {
                ImGui::SetNextItemWidth(130);
                if (ImGui::BeginCombo("##rank", role.c_str())) {
                    for (const auto& r : ranks) {
                        const std::string rid = r.value("id", std::string());
                        if (rid == "Owner" || (!owner && r.value("level", 0) >= myLevel)) continue;
                        if (ImGui::Selectable(r.value("name", std::string()).c_str(), rid == rankId) && rid != rankId)
                            act("groups.member", {{"user", id}, {"action", "rank"}, {"rank", rid}});
                    }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::TextDisabled("%s", role.c_str());
            }
            if (owner && rankId != "Owner") {
                ImGui::SameLine();
                if (ImGui::SmallButton("Give ownership")) ImGui::OpenPopup("##giveaway");
                if (ImGui::BeginPopup("##giveaway")) {
                    ImGui::Text("Make %s the owner? You'll get the next rank down.", m.value("name", std::string()).c_str());
                    if (ImGui::Button("Yes, give it to them")) {
                        act("groups.member", {{"user", id}, {"action", "owner"}});
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::EndPopup();
                }
            }
            if ((manage && below) || (staff && rankId != "Owner" && id != Account::id())) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) act("groups.member", {{"user", id}, {"action", "kick"}});
            }
            ImGui::PopID();
        }
    }
    // --- Games: the group's games (their pass and product sales go to the group's Bolts) ---
    if (tab == Games) {
        if (games.empty()) ImGui::TextDisabled("No games yet.");
        for (size_t i = 0; i < games.size(); ++i) {
            const json& a = games[i];
            ImGui::PushID((int)i + 2000);
            ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
            if (ImGui::Selectable(a.value("name", std::string()).c_str(), false, 0, ImVec2(std::min(320.0f, ImGui::GetContentRegionAvail().x * 0.5f), 0))) {
                m_page = Page::Games;
                m_openGame = a;
                m_openOnlineGame = 0;
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("%lld visits  -  made by %s", a.value("plays", 0LL), a.value("creatorName", std::string("?")).c_str());
            if (owner || (can("games") && a.value("creator", std::string()) == Account::id())) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Take out")) act("groups.removeGame", {{"game", a.value("id", std::string())}});
            }
            ImGui::PopID();
        }
        if (can("games")) {
            ImGui::SeparatorText("Add one of your games");
            if (m_loaded.find("groupgames ") == std::string::npos) {
                m_loaded += "groupgames ";
                Online::request("list", {{"creator", Account::id()}, {"kind", "game"}, {"limit", 100}}, [this](const json& r) {
                    if (r.value("ok", false)) m_groupMyGames = r["assets"];
                });
            }
            std::vector<const json*> mine;
            for (const auto& a : m_groupMyGames)
                if (!a.contains("group") || !a["group"].is_object() || a["group"].value("id", std::string()) != m_groupId) mine.push_back(&a);
            if (mine.empty()) ImGui::TextDisabled("You don't have any other published games.");
            else {
                if (m_groupAddGame >= (int)mine.size()) m_groupAddGame = 0;
                ImGui::SetNextItemWidth(std::min(300.0f, ImGui::GetContentRegionAvail().x - 90));
                if (ImGui::BeginCombo("##addgame", mine[m_groupAddGame]->value("name", std::string()).c_str())) {
                    for (int i = 0; i < (int)mine.size(); ++i)
                        if (ImGui::Selectable((mine[i]->value("name", std::string()) + "##" + std::to_string(i)).c_str(), i == m_groupAddGame))
                            m_groupAddGame = i;
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (Classic::button("Add", Classic::kPlay)) {
                    const std::string gameId = mine[m_groupAddGame]->value("id", std::string());
                    Online::request("game.settings", {{"id", gameId}, {"group", m_groupId}}, [this](const json& r) {
                        m_socialMsg = r.value("ok", false) ? std::string() : r.value("error", std::string());
                        if (size_t at = m_loaded.find("groupgames "); at != std::string::npos) m_loaded.erase(at, 11);
                        openGroup(m_groupId);
                    });
                }
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("Its game pass and product sales go to the group's Bolts instead of to you.");
                ImGui::PopTextWrapPos();
            }
        }
    }
    // --- Group Bolts: what came in, what went out, and paying members ---
    if (tab == Funds) {
        const long long funds = m_group.value("funds", 0LL);
        Bolts::drawIcon(dl, ImVec2(ImGui::GetCursorScreenPos().x + 10, ImGui::GetCursorScreenPos().y + 12), 20.0f);
        ImGui::Dummy(ImVec2(22, 0));
        ImGui::SameLine();
        ImGui::SetWindowFontScale(1.4f);
        ImGui::Text("%lld", funds);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("Sales from the group's games come in here. Pay members from it.");
        const json& list = m_group.contains("memberList") ? m_group["memberList"] : json::array();
        if (can("funds") && !list.empty()) {
            if (m_payTo >= (int)list.size()) m_payTo = 0;
            const std::string who = list[m_payTo].value("name", std::string());
            ImGui::SetNextItemWidth(160);
            if (ImGui::BeginCombo("##payto", who.c_str())) {
                for (int i = 0; i < (int)list.size(); ++i)
                    if (ImGui::Selectable((list[i].value("name", std::string()) + "##" + std::to_string(i)).c_str(), i == m_payTo)) m_payTo = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputInt("##payamount", &m_payAmount, 0);
            m_payAmount = std::clamp(m_payAmount, 0, 100000000);
            ImGui::SameLine();
            ImGui::BeginDisabled(m_payAmount < 1 || m_payAmount > funds);
            if (Classic::button("Pay", Classic::kPlay, ImVec2(70, 0))) ImGui::OpenPopup("##payout");
            ImGui::EndDisabled();
            if (ImGui::BeginPopup("##payout")) {
                ImGui::Text("Pay %s %d Bolts from the group?", who.c_str(), m_payAmount);
                if (ImGui::Button("Yes, pay them")) {
                    act("groups.payout", {{"user", list[m_payTo].value("id", std::string())}, {"amount", m_payAmount}});
                    m_payAmount = 0;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::SeparatorText("History");
        const json& ledger = m_group.contains("ledger") ? m_group["ledger"] : json::array();
        if (ledger.empty()) ImGui::TextDisabled("Nothing yet.");
        for (const auto& e : ledger) {
            const long long n = e.value("amount", 0LL);
            ImGui::TextColored(n < 0 ? ImVec4(0.7f, 0.15f, 0.1f, 1) : ImVec4(0.1f, 0.5f, 0.2f, 1), "%s%lld", n > 0 ? "+" : "", n);
            ImGui::SameLine(80);
            ImGui::TextUnformatted(e.value("reason", std::string()).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", when(e.value("at", 0LL)).c_str());
        }
    }
    // --- Admin ---
    if (tab == Admin) {
        if (manage) {
            ImGui::SeparatorText("Asking to join");
            if (reqList.empty()) ImGui::TextDisabled("Nobody's waiting.");
            for (size_t i = 0; i < reqList.size(); ++i) {
                const json& u = reqList[i];
                ImGui::PushID((int)i + 1000);
                if (nameLink(u, "req")) openProfile(u.value("id", std::string()));
                ImGui::SameLine();
                if (ImGui::SmallButton("Let in")) act("groups.request", {{"user", u.value("id", std::string())}, {"accept", true}});
                ImGui::SameLine();
                if (ImGui::SmallButton("No")) act("groups.request", {{"user", u.value("id", std::string())}, {"accept", false}});
                ImGui::PopID();
            }
            ImGui::SeparatorText("Group settings");
            if (!m_editingGroup) { m_editDesc = g.value("description", std::string()); m_editingGroup = true; }
            ImGui::InputTextMultiline("About##edit", &m_editDesc, ImVec2(std::min(460.0f, ImGui::GetContentRegionAvail().x - 60), 70));
            bool open = g.value("open", true);
            if (ImGui::Checkbox("Anyone can join", &open)) act("groups.edit", {{"open", open}});
            if (ImGui::Button("Save")) act("groups.edit", {{"description", m_editDesc}});
        }
        if (owner && !ranks.empty()) {
            ImGui::SeparatorText("Ranks");
            ImGui::PushTextWrapPos(0);
            ImGui::TextDisabled("Higher levels are more in charge. People can only change the rank of people below them. Up to 10 ranks.");
            ImGui::PopTextWrapPos();
            // Each rank's form keeps what you typed until you save (or the group reloads).
            std::vector<json> rows(ranks.begin(), ranks.end());
            if (rows.size() < 10) rows.push_back({{"id", ""}, {"name", ""}, {"level", 0}, {"perms", json::array()}});
            for (size_t i = 0; i < rows.size(); ++i) {
                const std::string rid = rows[i].value("id", std::string());
                json& e = m_rankEdits[rid];
                if (e.is_null()) e = rows[i];
                ImGui::PushID((int)i + 3000);
                if (rid.empty()) ImGui::TextUnformatted("New rank");
                std::string name = e.value("name", std::string());
                ImGui::SetNextItemWidth(160);
                if (ImGui::InputTextWithHint("##rname", "Rank name", &name)) e["name"] = name.substr(0, 24);
                if (rid != "Owner" && rid != "Member") {
                    ImGui::SameLine();
                    int level = e.value("level", 0);
                    ImGui::SetNextItemWidth(90);
                    if (ImGui::InputInt("level", &level, 0)) e["level"] = std::clamp(level, 0, 254);
                } else {
                    ImGui::SameLine();
                    ImGui::TextDisabled("level %d", e.value("level", 0));
                }
                if (rid != "Owner") {
                    for (const auto& perm : kGroupPerms) {
                        bool on = false;
                        for (const auto& x : e.value("perms", json::array())) if (x == perm[0]) on = true;
                        if (ImGui::Checkbox(perm[1], &on)) {
                            json list = json::array();
                            for (const auto& other : kGroupPerms) {
                                bool keep = std::string(other[0]) == perm[0] ? on : false;
                                if (std::string(other[0]) != perm[0])
                                    for (const auto& x : e.value("perms", json::array())) if (x == other[0]) keep = true;
                                if (keep) list.push_back(other[0]);
                            }
                            e["perms"] = list;
                        }
                    }
                } else {
                    ImGui::TextDisabled("The owner can do everything.");
                }
                if (ImGui::Button(rid.empty() ? "Add rank" : "Save rank")) {
                    json send = e;
                    act("groups.rank", {{"rank", send}});
                    m_rankEdits.clear();
                }
                if (rid != "Owner" && rid != "Member" && !rid.empty()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Delete rank")) ImGui::OpenPopup("##delrank");
                    if (ImGui::BeginPopup("##delrank")) {
                        ImGui::Text("Delete %s? Everyone in it becomes a Member.", e.value("name", std::string()).c_str());
                        if (ImGui::Button("Yes, delete it")) { act("groups.rankDelete", {{"rank", rid}}); m_rankEdits.clear(); ImGui::CloseCurrentPopup(); }
                        ImGui::EndPopup();
                    }
                }
                ImGui::Separator();
                ImGui::PopID();
            }
        }
        if (owner || staff) {
            ImGui::SeparatorText("Danger zone");
            if (bigButton("Delete group", ImVec4(0.75f, 0.25f, 0.25f, 1), ImVec2(140, 30))) ImGui::OpenPopup("##delgroup");
            if (ImGui::BeginPopup("##delgroup")) {
                ImGui::TextUnformatted("Delete this group for everyone? Its games stay, but its Bolts are gone. This can't be undone.");
                if (ImGui::Button("Yes, delete it")) {
                    Online::request("groups.delete", {{"id", m_groupId}}, [this](const json& r) {
                        if (r.value("ok", false)) { m_page = Page::Groups; m_loaded.clear(); m_groupsTab = 0; }
                        else m_socialMsg = r.value("error", std::string());
                    });
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }
}
