#include "Badges.h"
#include "Profile.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Badges {

namespace {
const Info kInfo[] = {
    {"admin",    "Administrator",    "Runs Guts&Bolts. Only the official staff account has this badge.", false, false},
    {"tester",   "Tester",           "Helped test Guts&Bolts before everyone else.",                      true,  false},
    {"bughunter","Bug Hunter",       "Found and reported a real bug.",                                    true,  false},
    {"featured", "Featured Creator", "Made a game the staff picked as a favourite.",                     true,  false},
    {"verified", "Verified",         "A recognised creator. Verified people can publish anything, upload "
                                     "clothes, audio and plugins for free, and sell their creations.",   true,  true},
    {"staff",    "Staff",            "Helps run Guts&Bolts and can verify people.",                       true,  false},
    {"partner",  "Partner",          "An official partner of Guts&Bolts, like the people who make Lyreto.", true,  true},
};

std::string grantMessage(const std::string& key, const std::string& accountId) {
    return Online::grantMessage(key, accountId);
}

ImU32 rgb(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// A shield outline, used by every badge.
void shield(ImDrawList* dl, ImVec2 c, float s, ImU32 fill, ImU32 rim) {
    float w = s * 0.42f, top = c.y - s * 0.45f, mid = c.y + s * 0.12f, bot = c.y + s * 0.48f;
    ImVec2 pts[] = {
        {c.x - w, top}, {c.x + w, top}, {c.x + w, mid},
        {c.x + w * 0.55f, c.y + s * 0.34f}, {c.x, bot}, {c.x - w * 0.55f, c.y + s * 0.34f}, {c.x - w, mid}};
    dl->AddConvexPolyFilled(pts, 7, fill);
    dl->AddPolyline(pts, 7, rim, ImDrawFlags_Closed, std::max(1.5f, s * 0.07f));
}

// A hex bolt head, the Guts&Bolts logo mark.
void bolt(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 hole) {
    ImVec2 p[6];
    for (int i = 0; i < 6; ++i) {
        float a = 3.14159265f / 3.0f * i + 3.14159265f / 6.0f;
        p[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    dl->AddConvexPolyFilled(p, 6, col);
    dl->AddCircleFilled(c, r * 0.42f, hole, 16);
}
} // namespace

const Info& info(Id id) { return kInfo[(int)id]; }

bool fromKey(const std::string& key, Id& out) {
    for (int i = 0; i < (int)Id::Count; ++i)
        if (key == kInfo[i].key) { out = (Id)i; return true; }
    return false;
}

bool grantValid(const std::string& official, const std::string& key, const std::string& accountId, const std::string& sig) {
    return Online::grantValid(official, key, accountId, sig);
}

std::vector<Id> verified(const std::string& accountId, const std::vector<Grant>& grants) {
    std::vector<Id> out;
    if (Account::isOfficial(accountId)) out.push_back(Id::Administrator);
    std::string official = Account::officialId();
    if (official.empty()) return out;
    for (const auto& [key, sig] : grants) {
        Id id;
        if (!fromKey(key, id) || !info(id).grantable) continue;
        bool dup = false;
        for (Id o : out) if (o == id) dup = true;
        if (!dup && grantValid(official, key, accountId, sig)) out.push_back(id);
    }
    return out;
}

bool has(const std::string& accountId, const std::vector<Grant>& grants, Id id) {
    if (id == Id::Verified && Account::isOfficial(accountId)) return true;   // staff are always verified
    for (Id h : verified(accountId, grants)) if (h == id) return true;
    return false;
}

bool iHave(Id id) { return has(Account::id(), Profile::get().grants, id); }

bool canVerify() { return Account::iAmStaff() || iHave(Id::Staff); }

Grant makeGrant(Id id, const std::string& accountId, std::string& error) {
    const bool official = Account::iAmStaff();
    if (!info(id).grantable) { error = "That badge can't be given to anyone."; return {}; }
    if (!official && !(info(id).staffCanGive && iHave(Id::Staff))) {
        error = info(id).staffCanGive ? "Only staff can give out that badge." : "Only the official Guts account can give out that badge.";
        return {};
    }
    std::string who;
    for (char c : accountId) if (!std::isspace((unsigned char)c)) who += (char)std::tolower((unsigned char)c);
    if (who.size() != 64) { error = "An account ID is 64 letters and numbers long (from their Avatar page)."; return {}; }
    std::string key = info(id).key;
    std::string sig = Account::sign(grantMessage(key, who));
    if (!official) {
        // Carry our own Staff badge along, so anyone can check we were allowed to give it.
        std::string staffSig;
        for (const auto& [k, s] : Profile::get().grants) if (k == "staff") staffSig = s;
        sig = "s:" + Account::id() + ":" + staffSig + ":" + sig;
    }
    return {key, sig};
}

std::string makeCode(Id id, const std::string& accountId, std::string& error) {
    Grant g = makeGrant(id, accountId, error);
    return g.first.empty() ? std::string() : g.first + ":" + g.second;
}

bool redeem(const std::string& codeIn, std::string& message) {
    std::string code;
    for (char c : codeIn) if (!std::isspace((unsigned char)c)) code += c;
    auto colon = code.find(':');
    Id id;
    if (colon == std::string::npos || !fromKey(code.substr(0, colon), id)) {
        message = "That doesn't look like a badge code.";
        return false;
    }
    Grant g{code.substr(0, colon), code.substr(colon + 1)};
    auto ok = verified(Account::id(), {g});
    if (ok.empty() || ok.back() != id) {
        message = "That code isn't valid for your account (codes only work for the account they were made for).";
        return false;
    }
    Profile& me = Profile::get();
    for (auto& old : me.grants) if (old.first == g.first) { message = "You already have that badge!"; return false; }
    me.grants.push_back(g);
    me.save();
    message = std::string("You got the ") + info(id).name + " badge!";
    return true;
}

void drawIcon(ImDrawList* dl, ImVec2 c, float s, Id id) {
    switch (id) {
    case Id::Administrator: {
        // Red shield, gold rim, silver bolt: the staff badge.
        shield(dl, c, s, rgb(200, 30, 36), rgb(255, 205, 70));
        bolt(dl, ImVec2(c.x, c.y - s * 0.03f), s * 0.24f, rgb(225, 228, 235), rgb(200, 30, 36));
        break;
    }
    case Id::Tester: {
        shield(dl, c, s, rgb(40, 110, 200), rgb(200, 225, 255));
        // A flask.
        float w = s * 0.13f;
        ImVec2 neckA(c.x - w * 0.5f, c.y - s * 0.28f), neckB(c.x + w * 0.5f, c.y - s * 0.1f);
        dl->AddRectFilled(neckA, neckB, rgb(235, 240, 250));
        dl->AddTriangleFilled(ImVec2(c.x - w * 0.5f, c.y - s * 0.1f), ImVec2(c.x + w * 0.5f, c.y - s * 0.1f),
                              ImVec2(c.x, c.y - s * 0.1f), rgb(235, 240, 250));
        ImVec2 body[] = {{c.x - w * 0.5f, c.y - s * 0.1f}, {c.x + w * 0.5f, c.y - s * 0.1f},
                         {c.x + s * 0.22f, c.y + s * 0.2f}, {c.x - s * 0.22f, c.y + s * 0.2f}};
        dl->AddConvexPolyFilled(body, 4, rgb(120, 230, 120));
        break;
    }
    case Id::BugHunter: {
        shield(dl, c, s, rgb(40, 140, 60), rgb(190, 240, 160));
        dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.02f), s * 0.16f, rgb(30, 30, 30), 20);
        dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.17f), s * 0.08f, rgb(30, 30, 30), 16);
        for (int i = -1; i <= 1; ++i) {
            float y = c.y + s * (0.02f + 0.08f * i);
            dl->AddLine(ImVec2(c.x - s * 0.28f, y + s * 0.03f * i), ImVec2(c.x + s * 0.28f, y + s * 0.03f * i),
                        rgb(30, 30, 30), std::max(1.0f, s * 0.04f));
        }
        dl->AddLine(ImVec2(c.x, c.y - s * 0.12f), ImVec2(c.x, c.y + s * 0.18f), rgb(200, 60, 50), std::max(1.0f, s * 0.03f));
        break;
    }
    case Id::FeaturedCreator: {
        shield(dl, c, s, rgb(120, 60, 170), rgb(255, 215, 90));
        ImVec2 star[10];
        for (int i = 0; i < 10; ++i) {
            float a = -3.14159265f / 2 + i * 3.14159265f / 5;
            float r = (i % 2 == 0) ? s * 0.26f : s * 0.11f;
            star[i] = ImVec2(c.x + std::cos(a) * r, c.y - s * 0.02f + std::sin(a) * r);
        }
        for (int i = 0; i < 10; ++i)
            dl->AddTriangleFilled(ImVec2(c.x, c.y - s * 0.02f), star[i], star[(i + 1) % 10], rgb(255, 215, 90));
        break;
    }
    case Id::Verified: {
        shield(dl, c, s, rgb(30, 140, 235), rgb(200, 230, 255));
        drawCheck(dl, ImVec2(c.x, c.y - s * 0.02f), s * 0.5f);
        break;
    }
    case Id::Staff: {
        shield(dl, c, s, rgb(30, 45, 90), rgb(120, 200, 255));
        bolt(dl, ImVec2(c.x, c.y - s * 0.04f), s * 0.2f, rgb(120, 200, 255), rgb(30, 45, 90));
        dl->AddText(ImVec2(c.x - s * 0.09f, c.y + s * 0.14f), rgb(200, 230, 255), "S");
        break;
    }
    case Id::Partner: {
        // Teal shield with two gold rings linked together.
        shield(dl, c, s, rgb(20, 130, 120), rgb(255, 215, 90));
        float r = s * 0.13f, t = std::max(1.5f, s * 0.06f);
        dl->AddCircle(ImVec2(c.x - s * 0.09f, c.y - s * 0.02f), r, rgb(255, 215, 90), 20, t);
        dl->AddCircle(ImVec2(c.x + s * 0.09f, c.y - s * 0.02f), r, rgb(255, 215, 90), 20, t);
        break;
    }
    default: break;
    }
}

void drawCheck(ImDrawList* dl, ImVec2 c, float s) {
    // A blue badge with a white tick, like other sites' verified mark.
    const int n = 12;
    ImVec2 pts[n * 2];
    for (int i = 0; i < n * 2; ++i) {   // a slightly bumpy "seal" outline
        float a = 3.14159265f * i / n;
        float r = (i % 2 ? 0.44f : 0.5f) * s;
        pts[i] = ImVec2(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
    }
    dl->AddConvexPolyFilled(pts, n * 2, rgb(29, 155, 240));
    float t = std::max(1.3f, s * 0.13f);
    dl->AddLine(ImVec2(c.x - s * 0.2f, c.y + s * 0.01f), ImVec2(c.x - s * 0.05f, c.y + s * 0.16f), rgb(255, 255, 255), t);
    dl->AddLine(ImVec2(c.x - s * 0.05f, c.y + s * 0.16f), ImVec2(c.x + s * 0.22f, c.y - s * 0.15f), rgb(255, 255, 255), t);
}

void check(float size) {
    if (size <= 0.0f) size = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawCheck(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size);
    if (ImGui::IsItemHovered()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        ImGui::SetTooltip("Verified");
        ImGui::PopStyleColor();
    }
}

void icon(Id id, float size) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(size, size));
    drawIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size, id);
    if (ImGui::IsItemHovered()) {
        // Readable on whatever the tooltip box is: dark text on a light box, white on a dark one.
        const ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        const bool lightBox = 0.3f * bg.x + 0.59f * bg.y + 0.11f * bg.z > 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Text, lightBox ? ImVec4(0.12f, 0.12f, 0.14f, 1) : ImVec4(1, 1, 1, 1));
        ImGui::SetTooltip("%s\n%s", info(id).name, info(id).description);
        ImGui::PopStyleColor();
    }
}

} // namespace Badges
