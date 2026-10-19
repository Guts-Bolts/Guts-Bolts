// Messages (an inbox like the website's), saved outfits, and your favourite and
// recently played games. The same server requests as the website (worker/server.js).
#include "PlayerApp.h"
#include "SiteUi.h"
#include "SocialUi.h"
#include "../game/Profile.h"
#include "../online/OnlineClient.h"

#include <misc/cpp/imgui_stdlib.h>
#include <ctime>

using json = nlohmann::json;
using namespace Site;

namespace {
// "5 minutes ago" style times.
std::string agoText(long long t) {
    long long s = std::max(0LL, Online::unixNow() - t);
    if (s < 60) return "just now";
    if (s < 3600) return std::to_string(s / 60) + (s / 60 == 1 ? " minute ago" : " minutes ago");
    if (s < 86400) return std::to_string(s / 3600) + (s / 3600 == 1 ? " hour ago" : " hours ago");
    return std::to_string(s / 86400) + (s / 86400 == 1 ? " day ago" : " days ago");
}
glm::vec3 colorOf(const json& a, const char* k, glm::vec3 fallback) {
    if (!a.contains(k) || !a[k].is_array() || a[k].size() != 3) return fallback;
    if (a[k][0].get<double>() < 0) return glm::vec3(-1.0f);
    return glm::vec3(a[k][0].get<float>(), a[k][1].get<float>(), a[k][2].get<float>()) / 255.0f;
}
} // namespace

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

int PlayerApp::unreadMessages() const {
    return Online::online() ? (int)Online::me().value("unreadMessages", 0LL) : 0;
}

// ---------------------------------------------------------------------------
// The bell: friend requests, sales, uploads checked by staff... (worker/server.js
// "notes.list"). Opening it marks them all read.
// ---------------------------------------------------------------------------

void PlayerApp::drawBell(ImVec2 c) {
    if (!Online::online() || Online::isGuest() || Online::me().value("userId", 0LL) == 0) return;
    const int unread = (int)Online::me().value("unreadNotes", 0LL);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(c.x - 13, c.y - 13));
    const bool clicked = ImGui::InvisibleButton("##bell", ImVec2(26, 26));
    if (ImGui::IsItemHovered()) {
        dl->AddCircleFilled(c, 13, IM_COL32(255, 255, 255, 60));
        ImGui::SetTooltip("Notifications");
    }
    // A little bell: dome, rim and clapper.
    const ImU32 col = IM_COL32(255, 255, 255, 255), shade = IM_COL32(0, 30, 80, 140);
    for (int pass = 0; pass < 2; ++pass) {
        const ImVec2 o = pass == 0 ? ImVec2(c.x + 1, c.y + 1) : c;
        const ImU32 k = pass == 0 ? shade : col;
        dl->PathArcTo(ImVec2(o.x, o.y - 1), 6.5f, 3.14159f, 6.28318f);
        dl->PathLineTo(ImVec2(o.x + 6.5f, o.y + 4));
        dl->PathLineTo(ImVec2(o.x + 8.5f, o.y + 6));
        dl->PathLineTo(ImVec2(o.x - 8.5f, o.y + 6));
        dl->PathLineTo(ImVec2(o.x - 6.5f, o.y + 4));
        dl->PathFillConvex(k);
        dl->AddCircleFilled(ImVec2(o.x, o.y + 8.5f), 2.2f, k);
        dl->AddCircleFilled(ImVec2(o.x, o.y - 8.5f), 1.6f, k);
    }
    if (unread > 0) {
        const std::string n = unread > 99 ? "99+" : std::to_string(unread);
        const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
        const ImVec2 b(c.x + 8, c.y - 8);
        const float r = std::max(7.0f, ts.x * 0.5f + 3);
        dl->AddRectFilled(ImVec2(b.x - r, b.y - 7), ImVec2(b.x + r, b.y + 7), IM_COL32(214, 38, 28, 255), 7.0f);
        dl->AddText(ImVec2(b.x - ts.x * 0.5f, b.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), n.c_str());
    }
    if (clicked) {
        ImGui::OpenPopup("##notes");
        m_notesLoaded = false;
        Online::request("notes.list", json::object(), [this](const json& r) {
            if (!r.value("ok", false)) return;
            m_notes = r.value("notes", json::array());
            m_notesLoaded = true;
            bool any = false;
            for (const json& n : m_notes) if (!n.value("read", false)) any = true;
            if (any) Online::request("notes.read", json::object());   // (the reply refreshes the count)
        });
    }
    ImGui::SetNextWindowPos(ImVec2(c.x + 14, c.y + 16), ImGuiCond_Appearing, ImVec2(1, 0));
    ImGui::SetNextWindowSize(ImVec2(360, 0));
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(360, 420));
    if (!ImGui::BeginPopup("##notes")) return;
    ImGui::TextUnformatted("Notifications");
    ImGui::Separator();
    if (!m_notesLoaded) ImGui::TextDisabled("Loading...");
    else if (m_notes.empty()) ImGui::TextDisabled("Nothing yet. Friend requests, sales and more show up here.");
    for (size_t i = 0; i < m_notes.size(); ++i) {
        const json& n = m_notes[i];
        const std::string kind = n.value("kind", std::string()), about = n.value("about", std::string());
        ImGui::PushID((int)i);
        const bool unreadOne = !n.value("read", false);
        if (unreadOne) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.25f, 0.6f, 1));
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(n.value("text", std::string()).c_str());   // (wraps; click it to go there)
        ImGui::PopTextWrapPos();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const bool go = ImGui::IsItemClicked();
        if (unreadOne) ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", agoText(n.value("at", 0LL)).c_str());
        if (go) {
            if (kind == "friendRequest") { m_page = Page::Friends; m_friendsAt = -100.0; }
            else if (kind == "friend" || kind == "follow") openProfile(about);
            else if (kind == "group") openGroup(about);
            else if (kind == "forum") openForum("", about, -1);
            else if (kind == "upload" || kind == "sale" || kind == "gift") openAsset(about);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndPopup();
}

void PlayerApp::openNewMessage(const std::string& to, const std::string& subject) {
    m_page = Page::Messages;
    m_msgBox = "new";
    m_msgTo = to;
    m_msgSubject = subject;
    m_msgBody.clear();
    m_msgStatus.clear();
}

void PlayerApp::drawMessages() {
    if (needsServer("Messages")) return;
    if (Online::isGuest() || Online::me().value("userId", 0LL) == 0) {
        ImGui::TextWrapped("Sign up to send and get messages.");
        if (Classic::button("Log in or sign up", Classic::kBlue, ImVec2(200, 30))) m_page = Page::Login;
        return;
    }
    ImGui::SetWindowFontScale(1.4f);
    ImGui::TextUnformatted("Messages");
    ImGui::SetWindowFontScale(1.0f);

    // Inbox / Sent / New message.
    const int unread = unreadMessages();
    const std::string inboxLabel = unread ? "Inbox (" + std::to_string(unread) + ")" : std::string("Inbox");
    const std::pair<const char*, std::string> tabs[] = {{"inbox", inboxLabel}, {"sent", "Sent"}, {"new", "New message"}};
    for (int t = 0; t < 3; ++t) {
        if (t) ImGui::SameLine();
        const bool on = m_msgBox == tabs[t].first;
        ImGui::PushID(t);
        if (on ? Classic::button(tabs[t].second.c_str(), Classic::kBlue, ImVec2(130, 28)) : ImGui::Button(tabs[t].second.c_str(), ImVec2(130, 28))) {
            m_msgBox = tabs[t].first;
            m_messagesAt = -100.0;
            m_msgStatus.clear();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();

    if (m_msgBox == "new") {
        ImGui::TextUnformatted("To (their user number, like #5, or their username)");
        ImGui::SetNextItemWidth(std::min(240.0f, ImGui::GetContentRegionAvail().x));
        ImGui::InputText("##to", &m_msgTo);
        ImGui::TextUnformatted("Subject");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputText("##subject", &m_msgSubject) && m_msgSubject.size() > 80) m_msgSubject.resize(80);
        ImGui::TextUnformatted("Message");
        if (ImGui::InputTextMultiline("##body", &m_msgBody, ImVec2(ImGui::GetContentRegionAvail().x, 160)) && m_msgBody.size() > 2000)
            m_msgBody.resize(2000);
        ImGui::TextDisabled("Be nice. Messages follow the same rules as chat.");
        ImGui::BeginDisabled(m_busy || m_msgTo.empty() || m_msgBody.empty());
        if (Classic::button(m_busy ? "Sending..." : "Send", Classic::kPlay, ImVec2(120, 32))) {
            m_busy = true;
            Online::request("message.send", {{"to", m_msgTo}, {"subject", m_msgSubject}, {"body", m_msgBody}}, [this](const json& r) {
                m_busy = false;
                if (!r.value("ok", false)) { m_msgStatus = r.value("error", std::string("That didn't send.")); return; }
                m_msgStatus = "Message sent!";
                m_msgBody.clear();
                m_msgSubject.clear();
                m_msgBox = "sent";
                m_messagesAt = -100.0;
            });
        }
        ImGui::EndDisabled();
        if (!m_msgStatus.empty()) ImGui::TextWrapped("%s", m_msgStatus.c_str());
        return;
    }

    // Inbox or Sent: asked for again every 30 seconds while it's open.
    if (ImGui::GetTime() - m_messagesAt > 30.0) {
        m_messagesAt = ImGui::GetTime();
        const std::string box = m_msgBox;
        Online::request("message.list", {{"box", box}}, [this, box](const json& r) {
            if (r.value("ok", false) && box == m_msgBox) m_messages = r.value("messages", json::array());
        });
    }
    if (!m_msgStatus.empty()) ImGui::TextColored(ImVec4(0.1f, 0.5f, 0.2f, 1), "%s", m_msgStatus.c_str());
    const bool sent = m_msgBox == "sent";
    if (!m_messages.is_array() || m_messages.empty()) {
        ImGui::TextDisabled(sent ? "You haven't sent any messages yet." : "No messages yet. When someone sends you one, it shows up here.");
        return;
    }
    for (size_t i = 0; i < m_messages.size(); ++i) {
        json& m = m_messages[i];
        const json& other = sent ? m["to"] : m["from"];
        const std::string id = m.value("id", std::string());
        const bool unreadOne = !m.value("read", true);
        ImGui::PushID(id.c_str());
        // One row: who, the subject and when; click to open it.
        std::string head = (sent ? "To " : "") + other.value("name", std::string("?")) + "   -   " + m.value("subject", std::string()) +
                           "   (" + agoText(m.value("at", 0LL)) + ")";
        if (unreadOne) ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.80f, 0.88f, 0.98f, 1));
        const bool open = ImGui::CollapsingHeader((std::string(unreadOne ? "[new]  " : "") + head + "###m").c_str());
        if (unreadOne) ImGui::PopStyleColor();
        if (open) {
            if (unreadOne) {   // opening it marks it read
                m["read"] = true;
                Online::request("message.read", {{"id", id}, {"box", "inbox"}}, [](const json&) {});
            }
            ImGui::Indent(10);
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(m.value("body", std::string()).c_str());
            ImGui::PopTextWrapPos();
            const std::string otherKey = other.value("userId", 0LL) > 0 ? std::to_string(other.value("userId", 0LL)) : other.value("id", std::string());
            if (!sent) {
                const std::string subj = m.value("subject", std::string());
                if (Classic::button("Reply", Classic::kBlue, ImVec2(80, 26)))
                    openNewMessage("#" + otherKey, subj.rfind("Re: ", 0) == 0 ? subj : "Re: " + subj);
                ImGui::SameLine();
            }
            if (ImGui::Button("Profile", ImVec2(80, 26))) openProfile(otherKey);
            ImGui::SameLine();
            if (!sent && canReport()) {
                if (ImGui::Button("Report", ImVec2(80, 26)))
                    openReport("message", id, other.value("name", std::string("?")), other.value("id", std::string()));
                ImGui::SameLine();
            }
            if (Classic::button("Delete", ImVec4(0.75f, 0.15f, 0.15f, 1), ImVec2(80, 26))) {
                Online::request("message.delete", {{"id", id}, {"box", sent ? "sent" : "inbox"}}, [this](const json&) { m_messagesAt = -100.0; });
                m_messages.erase(m_messages.begin() + (long)i);
                ImGui::Unindent(10);
                ImGui::PopID();
                break;
            }
            ImGui::Unindent(10);
            ImGui::Spacing();
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Who can send you messages is in your privacy settings on the website.");
}

// ---------------------------------------------------------------------------
// Saved outfits (the Avatar page's Outfits tab)
// ---------------------------------------------------------------------------

void PlayerApp::outfitPicture(ImDrawList* dl, ImVec2 c, float s, const json& outfit) {
    const json av = outfit.value("avatar", json::object());
    const json wearing = outfit.value("wearing", json::array());
    std::string key = "outfit|" + outfit.value("id", std::string()) + "|" + av.dump();
    auto& e = m_itemRenders[key];
    if (!e.item) {
        e.item = std::make_unique<Catalog::Item>();
        e.outfit = true;
        e.colors.head = colorOf(av, "head", e.colors.head);
        e.colors.torso = colorOf(av, "torso", e.colors.torso);
        e.colors.leftArm = colorOf(av, "leftArm", e.colors.leftArm);
        e.colors.rightArm = colorOf(av, "rightArm", e.colors.rightArm);
        e.colors.leftLeg = colorOf(av, "leftLeg", e.colors.leftLeg);
        e.colors.rightLeg = colorOf(av, "rightLeg", e.colors.rightLeg);
        e.hat = (HatStyle)std::clamp(av.value("hat", 0), 0, kHatStyleCount - 1);
        e.hatTint = colorOf(av, "hatColor", glm::vec3(-1.0f));
        for (const json& w : wearing) if (w.is_object()) e.wearing.push_back(Catalog::fromServer(w));
    }
    e.lastUsed = ImGui::GetTime();
    const ImVec2 a(c.x - s * 0.5f, c.y - s * 0.5f), b(c.x + s * 0.5f, c.y + s * 0.5f);
    if (e.fb && e.done) dl->AddImage((ImTextureID)(intptr_t)e.fb->colorTexture(), a, b, ImVec2(0, 1), ImVec2(1, 0));
    else {
        dl->AddRectFilled(a, b, IM_COL32(236, 238, 242, 255), 6);
        const float t = (float)ImGui::GetTime() * 4.0f;
        dl->PathArcTo(c, s * 0.12f, t, t + 4.2f, 16);
        dl->PathStroke(IM_COL32(150, 160, 175, 255), 0, std::max(1.5f, s * 0.025f));
    }
}

void PlayerApp::drawOutfits() {
    const bool signedUp = Online::online() && Online::me().value("userId", 0LL) > 0;
    if (!signedUp) {
        ImGui::Spacing();
        ImGui::TextWrapped("Log in to save outfits.");
        return;
    }
    if (ImGui::GetTime() - m_outfitsAt > 60.0) {
        m_outfitsAt = ImGui::GetTime();
        Online::request("outfit.list", json::object(), [this](const json& r) {
            if (r.value("ok", false)) m_outfits = r.value("outfits", json::array());
        });
    }
    auto takeList = [this](const json& r, const char* done) {
        m_busy = false;
        if (!r.value("ok", false)) { m_outfitMsg = r.value("error", std::string("That didn't work.")); return; }
        m_outfits = r.value("outfits", json::array());
        m_outfitMsg = done;
    };
    ImGui::SeparatorText("Save this look");
    ImGui::SetNextItemWidth(std::min(220.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::InputTextWithHint("##outfitName", "Name this outfit", &m_outfitName) && m_outfitName.size() > 40) m_outfitName.resize(40);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_busy);
    if (Classic::button("Save as outfit", Classic::kPlay, ImVec2(140, 0))) {
        // Your look on the server first (it may not be saved there yet), then keep it.
        m_busy = true;
        m_avatarPushAt = 0.0;
        const std::string name = m_outfitName;
        Online::pushAvatar([this, name, takeList](const json& r) {
            if (!r.value("ok", false)) { m_busy = false; m_outfitMsg = r.value("error", std::string("Couldn't save your look.")); return; }
            Online::request("outfit.save", {{"name", name}}, [this, takeList](const json& r2) { takeList(r2, "Outfit saved!"); m_outfitName.clear(); });
        });
    }
    ImGui::EndDisabled();
    if (!m_outfitMsg.empty()) ImGui::TextWrapped("%s", m_outfitMsg.c_str());

    ImGui::SeparatorText("My outfits");
    if (!m_outfits.is_array() || m_outfits.empty()) {
        ImGui::TextDisabled("No outfits yet. Dress up, then save the look here so you can wear it again in one click.");
        return;
    }
    const float tile = 116.0f, gap = 10.0f;
    const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (tile + gap)));
    for (size_t i = 0; i < m_outfits.size(); ++i) {
        const json o = m_outfits[i];
        const std::string id = o.value("id", std::string()), name = o.value("name", std::string());
        if (i % perRow) ImGui::SameLine(0, gap);
        ImGui::PushID(id.c_str());
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(255, 255, 255, 255));
        dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), IM_COL32(170, 175, 185, 255));
        outfitPicture(dl, ImVec2(p.x + tile * 0.5f, p.y + tile * 0.5f), tile - 6, o);
        ImGui::Dummy(ImVec2(tile, tile));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile);
        ImGui::TextColored(Classic::kLink, "%s", name.c_str());
        ImGui::PopTextWrapPos();
        ImGui::BeginDisabled(m_busy);
        if (Classic::button("Wear", Classic::kPlay, ImVec2(tile, 24))) {
            m_busy = true;
            Online::request("outfit.wear", {{"id", id}}, [this, takeList](const json& r) {
                takeList(r, "You're wearing it now.");
                // Online::takeMe puts the look on (and fetches the clothes); show it here as it arrives.
                m_restageUntil = ImGui::GetTime() + 4.0;
            });
        }
        if (ImGui::Button("Rename", ImVec2((tile - 4) * 0.5f, 22))) { m_renameOutfitId = id; m_renameOutfitName = name; ImGui::OpenPopup("Rename outfit"); }
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Delete", ImVec2((tile - 4) * 0.5f, 22))) { m_renameOutfitId = id; m_renameOutfitName = name; ImGui::OpenPopup("Delete outfit"); }
        ImGui::EndDisabled();
        // Little popups for renaming and making sure about deleting.
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Rename outfit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(240);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            if (ImGui::InputText("##rn", &m_renameOutfitName) && m_renameOutfitName.size() > 40) m_renameOutfitName.resize(40);
            if (Classic::button("Save", Classic::kPlay, ImVec2(100, 28)) && !m_renameOutfitName.empty()) {
                Online::request("outfit.rename", {{"id", m_renameOutfitId}, {"name", m_renameOutfitName}},
                                [takeList](const json& r) { takeList(r, "Renamed."); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 28)) || tappedOutside()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete outfit", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Delete the outfit \"%s\"?", m_renameOutfitName.c_str());
            if (Classic::button("Delete", ImVec4(0.75f, 0.15f, 0.15f, 1), ImVec2(100, 28))) {
                Online::request("outfit.delete", {{"id", m_renameOutfitId}}, [takeList](const json& r) { takeList(r, "Deleted."); });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep it", ImVec2(100, 28)) || tappedOutside()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// Your games: Continue Playing and Favorites (the home page)
// ---------------------------------------------------------------------------

// My Feed: what you and your friends are up to right now ("Right now I'm...").
void PlayerApp::drawFeed() {
    if (!Online::online() || Online::me().value("userId", 0LL) <= 0) return;
    if (ImGui::GetTime() - m_feedAt > 60.0) {
        m_feedAt = ImGui::GetTime();
        Online::request("feed.list", json::object(), [this](const json& r) {
            if (r.value("ok", false)) m_feed = r.value("feed", json::array());
        });
    }
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextUnformatted("My Feed");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 1));
    ImGui::BeginChild("##feed", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::TextUnformatted("Right now I'm...");
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 90));
    bool enter = ImGui::InputTextWithHint("##feedpost", "building a castle", &m_feedPost, ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_feedPost.size() > 140) m_feedPost.resize(140);
    ImGui::SameLine();
    if ((Classic::button("Share", Classic::kBlue, ImVec2(80, 0)) || enter) && !m_feedPost.empty()) {
        Online::request("profile.set", {{"status", m_feedPost}}, [this](const json& r) {
            if (r.value("ok", false)) { m_feedPost.clear(); m_feedAt = -1000.0; }
            else m_status = r.value("error", std::string());
        });
    }
    ImGui::Separator();
    if (m_feed.empty()) ImGui::TextDisabled("Nothing yet. Share what you're up to, and add friends to see theirs here.");
    int shown = 0;
    for (const auto& p : m_feed) {
        if (++shown > 5) break;
        const json& u = p.value("user", json::object());
        ImGui::PushID(shown);
        ImGui::PushStyleColor(ImGuiCol_Text, Classic::kLink);
        if (ImGui::Selectable(u.value("name", std::string("?")).c_str(), false, 0,
                              ImGui::CalcTextSize(u.value("name", std::string("?")).c_str())))
            openProfile(u.value("id", std::string()));
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        ImGui::Text("\"%s\"", p.value("text", std::string()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::TextDisabled("  %s", agoText(p.value("at", 0LL)).c_str());
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void PlayerApp::refreshMyGames() {
    if (!Online::online() || Online::me().value("userId", 0LL) == 0) return;
    m_myGamesAt = ImGui::GetTime();
    Online::request("games.mine", {{"which", "recent"}, {"limit", 12}}, [this](const json& r) {
        if (r.value("ok", false)) m_recentGames = r.value("assets", json::array());
    });
    Online::request("games.mine", {{"which", "favorites"}, {"limit", 12}}, [this](const json& r) {
        if (r.value("ok", false)) m_favGames = r.value("assets", json::array());
    });
}

void PlayerApp::setFavorite(const std::string& id, bool on) {
    Online::request("game.favorite", {{"id", id}, {"on", on}}, [this, id](const json& r) {
        if (!r.value("ok", false)) { m_onlineMsg = r.value("error", std::string()); return; }
        const json a = r["asset"];
        for (json* list : {&m_onlineGames, &m_recentGames, &m_favGames})
            for (json& g : *list) if (g.value("id", std::string()) == id) { g["favorites"] = a.value("favorites", 0LL); g["myFavorite"] = a.value("myFavorite", false); }
        if (m_openGame.value("id", std::string()) == id) { m_openGame["favorites"] = a.value("favorites", 0LL); m_openGame["myFavorite"] = a.value("myFavorite", false); }
        refreshMyGames();
    });
}
