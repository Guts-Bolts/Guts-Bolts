// G&B Paste: share code, logs and settings files with a link. worker/paste.js has the same rules
// (and the notes on why):
//   - some text (up to kMostBytes) with a title and a language; "public" ones are in the list,
//     "unlisted" ones only open with their link
//   - a paste can run out after 1 hour, 1 day, 1 week or 1 month (or never); run-out ones are deleted
//   - the text is kept as typed (no word filter: it would break code), the title is filtered
//   - only the person who made it (or staff) can delete it; pastes can't be changed
//   - kPerDay pastes a day (Verified: kPerDayVerified), at most kMostEach kept each
//   paste.make {title, text, lang, visibility, expires}  paste.get {id}  paste.list {page, lang, by}
//   paste.mine {}  paste.delete {id}  paste.raw {id} (no account: worker/index.js asks for /pasteraw/)
// Kept in pastes.json ({pastes: id -> paste, made: account -> {day, n}}).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const char* kLangs[] = {"text", "lua", "luau", "python", "javascript", "json", "cpp", "c", "csharp", "html", "css", "bash", "log", "markdown", "other"};
const long long kExpires[] = {0, 1, 24, 168, 720};   // hours: never, 1 hour, 1 day, 1 week, 1 month
constexpr size_t kTitle = 80, kMostBytes = 100 * 1024, kPageSize = 30, kMostEach = 2000;
constexpr long long kPerDay = 30, kPerDayVerified = 200;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
bool isLang(const std::string& l) { for (const char* x : kLangs) if (l == x) return true; return false; }

// The text as typed: no control characters except tab and new lines, \r\n and \r made \n,
// spaces and empty lines at the very end taken off. False when it's bigger than kMostBytes.
bool pasteText(const std::string& in, std::string& out) {
    out.clear();
    for (size_t i = 0; i < in.size(); ++i) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\r') { c = '\n'; if (i + 1 < in.size() && in[i + 1] == '\n') ++i; }
        if ((c < 32 && c != '\n' && c != '\t') || c == 127) continue;
        if (out.size() >= kMostBytes) return false;
        out += (char)c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\n')) out.pop_back();
    return true;
}
bool runOut(const json& p, long long t) { const long long u = p.value("until", 0LL); return u && u <= t; }
} // namespace

void GbServer::savePastes() { writeFile(m_opts.data / "pastes.json", m_pastes.dump(1)); }
void GbServer::loadPastes() {
    std::string text;
    if (readFile(m_opts.data / "pastes.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("pastes") && j["pastes"].is_object()) m_pastes = j;
        if (!m_pastes.contains("made") || !m_pastes["made"].is_object()) m_pastes["made"] = json::object();
    }
}

// The paste, unless it has run out (then it's deleted now).
json* GbServer::pasteLive(const std::string& id) {
    json& all = m_pastes["pastes"];
    if (id.empty() || !all.contains(id)) return nullptr;
    if (runOut(all[id], Online::unixNow())) { all.erase(id); savePastes(); return nullptr; }
    return &all[id];
}

json GbServer::pasteJson(const json& p, const User* me, bool full) const {
    const std::string by = sv(p, "by");
    auto u = m_users.find(by);
    json who = u == m_users.end() ? json{{"id", by}, {"name", "?"}, {"userId", 0}, {"verified", false}}
                                  : json{{"id", by}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    json out = {{"id", sv(p, "id")}, {"title", sv(p, "title")}, {"lang", sv(p, "lang")}, {"visibility", sv(p, "visibility")},
                {"at", p.value("at", 0LL)}, {"until", p.value("until", 0LL)}, {"lines", p.value("lines", 0LL)}, {"size", p.value("size", 0LL)},
                {"views", p.value("views", 0LL)}, {"by", who}};
    if (full) {
        out["text"] = sv(p, "text");
        out["mine"] = me && by == me->id;
        out["canDelete"] = me && (by == me->id || isStaff(*me));
    }
    return out;
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::pasteReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    json* p = pasteLive(id);
    if (!p) return "That paste isn't there any more.";
    if (sv(*p, "by") == me.id) return "You can't report yourself.";
    target = id; about = sv(*p, "by");
    copy = {{"subject", "Paste: " + sv(*p, "title")}, {"body", sv(*p, "text").substr(0, 2000)}, {"at", p->value("at", 0LL)}};
    return "";
}
std::string GbServer::pasteReportName(const std::string& target) const {
    const json& all = m_pastes["pastes"];
    return all.contains(target) && !runOut(all[target], Online::unixNow()) ? sv(all[target], "title") : "(deleted)";
}

json GbServer::pasteOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_pastes["pastes"];
    if (name == "paste.list") {
        bool dropped = false;
        for (auto it = all.begin(); it != all.end();) {
            if (runOut(*it, t)) { it = all.erase(it); dropped = true; } else ++it;
        }
        if (dropped) savePastes();
        const std::string lang = isLang(sv(args, "lang")) ? sv(args, "lang") : "";
        const User* by = nullptr;
        if (args.contains("by") && !(args["by"].is_string() && args["by"].get<std::string>().empty())) {
            by = findPerson(args["by"].is_number() ? std::to_string(args["by"].get<long long>()) : sv(args, "by"));
            if (!by) return fail("There's nobody with that name or number.");
        }
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const json*> list;
        for (const auto& [id, p] : all.items()) {
            if (sv(p, "visibility") != "public") continue;
            if (!lang.empty() && sv(p, "lang") != lang) continue;
            if (by && sv(p, "by") != by->id) continue;
            list.push_back(&p);
        }
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) > y->value("at", 0LL); });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(pasteJson(*list[i], me, false));
        json r = okay(); r["pastes"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page;
        r["langs"] = json::array(); for (const char* l : kLangs) r["langs"].push_back(l);
        return r;
    }
    if (name == "paste.raw") {   // (no account, no view counted)
        json* p = pasteLive(sv(args, "id"));
        if (!p) return fail("No paste.");
        json r = okay(); r["text"] = sv(*p, "text"); return r;
    }
    if (name == "paste.get") {
        json* p = pasteLive(sv(args, "id"));
        if (!p) return fail("That paste isn't here. It may have run out or been deleted.");
        if (!me || sv(*p, "by") != me->id) { (*p)["views"] = p->value("views", 0LL) + 1; savePastes(); }
        json r = okay(); r["paste"] = pasteJson(*p, me, true); return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to make pastes.");
    if (name == "paste.mine") {
        std::vector<const json*> list;
        for (const auto& [id, p] : all.items()) if (sv(p, "by") == me->id && !runOut(p, t)) list.push_back(&p);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) > y->value("at", 0LL); });
        json out = json::array();
        for (const json* p : list) out.push_back(pasteJson(*p, me, false));
        json r = okay(); r["pastes"] = out; return r;
    }
    if (name == "paste.make") {
        std::string text;
        if (!pasteText(sv(args, "text"), text)) return fail("That's too long (the most is " + std::to_string(kMostBytes >> 10) + " KB).");
        if (text.find_first_not_of(" \t\n") == std::string::npos) return fail("Paste something first.");
        std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) title = "Untitled";
        const std::string lang = isLang(sv(args, "lang")) ? sv(args, "lang") : "text";
        const std::string visibility = sv(args, "visibility") == "unlisted" ? "unlisted" : "public";
        const long long hours = args.contains("expires") && args["expires"].is_number() ? (long long)args["expires"].get<double>() : 0;
        if (std::find(std::begin(kExpires), std::end(kExpires), hours) == std::end(kExpires))
            return fail("Pick when it runs out: never, 1 hour, 1 day, 1 week or 1 month.");
        json& made = m_pastes["made"][me->id];
        const long long today = t / 86400;
        if (!made.is_object() || made.value("day", 0LL) != today) made = {{"day", today}, {"n", 0}};
        const long long most = isVerified(*me) || isStaff(*me) ? kPerDayVerified : kPerDay;
        if (made.value("n", 0LL) >= most) return fail("You've made " + std::to_string(most) + " pastes today. Come back tomorrow!");
        size_t mine = 0;
        for (const auto& [id, p] : all.items()) if (sv(p, "by") == me->id && !runOut(p, t)) ++mine;
        if (mine >= kMostEach) return fail("You have " + std::to_string(kMostEach) + " pastes. Delete some old ones first.");
        const std::string id = Account::randomHex(6);
        const long long lines = 1 + (long long)std::count(text.begin(), text.end(), '\n');
        all[id] = {{"id", id}, {"by", me->id}, {"title", title}, {"lang", lang}, {"visibility", visibility}, {"text", text}, {"at", t},
                   {"until", hours ? t + hours * 3600 : 0}, {"lines", lines}, {"size", text.size()}, {"views", 0}};
        made["n"] = made.value("n", 0LL) + 1;
        savePastes();
        json r = okay(); r["paste"] = pasteJson(all[id], me, true); return r;
    }
    if (name == "paste.delete") {
        json* p = pasteLive(sv(args, "id"));
        if (!p) return fail("That paste isn't here (any more).");
        if (sv(*p, "by") != me->id && !isStaff(*me)) return fail("Only the person who made it, or staff, can delete it.");
        all.erase(sv(args, "id"));
        savePastes();
        return okay();
    }
    return fail("Unknown request.");
}
