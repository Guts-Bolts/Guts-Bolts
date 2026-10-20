#pragma once
#include <imgui.h>
#include <string>
#include <utility>
#include <vector>

// Official Guts&Bolts badges. Only the staff account can hand them out: each
// one is a signature from the official key saying "account X has badge Y",
// so they can't be copied onto someone else's account or made up.
namespace Badges {

enum class Id { Administrator, Tester, BugHunter, FeaturedCreator, Verified, Staff, Partner, Count };

struct Info {
    const char* key;          // stored in files / sent over the network
    const char* name;
    const char* description;
    bool        grantable;    // false: only the staff account has it
    bool        staffCanGive; // Staff members (not just the official account) can hand it out
};

const Info& info(Id id);
bool        fromKey(const std::string& key, Id& out);

// A signed "grant": {badge key, signature}.
using Grant = std::pair<std::string, std::string>;

// Which official badges an account really has (bad grants are ignored).
// A grant is signed by the official account, or (for Verified) by a Staff
// member: then its signature carries the Staff member's own signed Staff badge.
std::vector<Id> verified(const std::string& accountId, const std::vector<Grant>& grants);
bool            has(const std::string& accountId, const std::vector<Grant>& grants, Id id);

// This computer's account: official or Staff, so it can give Verified.
bool canVerify();
// Our own verified badges (from the profile).
bool iHave(Id id);

// A code the other player pastes into "Redeem a badge code". The official
// account can give any badge; Staff members can give Verified and Partner.
std::string makeCode(Id id, const std::string& accountId, std::string& error);
// The raw grant (badge key + signature) that makeCode wraps, for the server.
Grant       makeGrant(Id id, const std::string& accountId, std::string& error);
// Checks a code is for this account and adds it to the profile.
bool        redeem(const std::string& code, std::string& message);

// The badge's picture, centred on `c`, `size` pixels across.
void drawIcon(ImDrawList* dl, ImVec2 c, float size, Id id);
// A badge picture as an ImGui item (with a tooltip).
void icon(Id id, float size);
// The little blue check shown next to Verified people's names.
void drawCheck(ImDrawList* dl, ImVec2 c, float size);
// The check as an ImGui item, on the current line (with a tooltip).
void check(float size = 0.0f);

} // namespace Badges
