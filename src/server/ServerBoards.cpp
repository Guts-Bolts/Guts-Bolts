// G&B Boards: plan things with cards in columns, alone or with friends. worker/boards.js has the
// same rules (and the notes on why):
//   - a board has a title, words, a colour and columns of cards (title, notes, labels, due date,
//     checklist, people, done)
//   - the owner adds up to kMembers people; everyone on it changes columns and cards; only the
//     owner changes settings, adds or removes people or deletes it (staff can delete too); anyone can leave
//   - 'private' (people on it), 'link' (anyone with the link looks) or 'public' (listed too)
//   - people are told when they're added to a board or put on a card
//   board.mine {}  board.list {page}  board.get {id}  board.make {title, template}  board.set {id, title, text, visibility, color}
//   board.delete {id}  board.member {id, who, on}  board.column {id, do, column, title, to}  board.card {id, do, card, column, to, ...}
// Kept in boards.json ({boards: id -> board}).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <ctime>
#include <set>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const char* kColors[] = {"red", "orange", "yellow", "green", "blue", "purple", "gray"};
const char* kSees[] = {"private", "link", "public"};
constexpr size_t kTitle = 80, kText = 500, kColTitle = 40, kCardTitle = 120, kCardText = 2000, kChecks = 30, kCheckText = 120;
constexpr size_t kBoardsEach = 50, kColumns = 20, kCards = 500, kMembers = 30, kPageSize = 30;

struct Template { const char* id; std::vector<std::pair<const char*, std::vector<const char*>>> columns; };
const std::vector<Template>& templates() {   // (worker/boards.js BOARD_TEMPLATES)
    static const std::vector<Template> t = {
        {"blank", {{"To do", {}}, {"Doing", {}}, {"Done", {}}}},
        {"game", {{"Ideas", {"Write down what the game is about", "Pick a name"}},
                  {"To build", {"Make the map", "Script the main idea", "Add sounds"}},
                  {"Testing", {"Play it with friends"}}, {"Done", {}}}},
        {"bugs", {{"Reported", {}}, {"Fixing", {}}, {"Check the fix", {}}, {"Fixed", {}}}},
        {"event", {{"Plan", {"Pick a day and time", "Pick the game or place"}}, {"Tell people", {"Post it in the group"}},
                   {"On the day", {}}, {"Done", {}}}},
    };
    return t;
}

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
bool isColor(const std::string& c) { for (const char* x : kColors) if (c == x) return true; return false; }
bool isSee(const std::string& c) { for (const char* x : kSees) if (c == x) return true; return false; }
bool has(const json& arr, const std::string& id) { return std::find(arr.begin(), arr.end(), id) != arr.end(); }

json labels(const json& in) {
    json out = json::array();
    if (in.is_array()) for (const json& x : in) if (x.is_string() && isColor(x.get<std::string>()) && !has(out, x.get<std::string>())) out.push_back(x);
    return out;
}
// "2026-12-31", or "" when it isn't a real day.
std::string due(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return "";
    for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9}) if (s[i] < '0' || s[i] > '9') return "";
    const int y = std::stoi(s.substr(0, 4)), mo = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    if (y < 2000 || y > 2200 || mo < 1 || mo > 12 || d < 1) return "";
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return d <= kDays[mo - 1] + (mo == 2 && leap ? 1 : 0) ? s : "";
}
size_t countCards(const json& b) { size_t n = 0; for (const json& c : b["columns"]) n += c["cards"].size(); return n; }
// Put x into list at spot `to` (the end if it's missing or too big).
void place(json& list, const json& x, const json& args) {
    long long to = args.contains("to") && args["to"].is_number() ? (long long)args["to"].get<double>() : -1;
    if (to < 0 || to > (long long)list.size()) to = (long long)list.size();
    list.insert(list.begin() + to, x);
}
json newCard(const std::string& title, const std::string& by, long long t) {
    return {{"id", Account::randomHex(4)}, {"title", title}, {"text", ""}, {"labels", json::array()}, {"due", ""}, {"checks", json::array()},
            {"people", json::array()}, {"done", false}, {"at", t}, {"by", by}};
}
} // namespace

void GbServer::saveKanban() { writeFile(m_opts.data / "boards.json", m_kanban.dump(1)); }
void GbServer::loadKanban() {
    std::string text;
    if (readFile(m_opts.data / "boards.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("boards") && j["boards"].is_object()) m_kanban = j;
    }
}

bool GbServer::kanbanOn(const json& b, const User* me) const { return me && (sv(b, "by") == me->id || has(b["members"], me->id)); }
bool GbServer::kanbanCanSee(const json& b, const User* me) const { return sv(b, "visibility") != "private" || kanbanOn(b, me) || (me && isStaff(*me)); }

json GbServer::kanbanPerson(const std::string& id) const {
    auto u = m_users.find(id);
    if (u == m_users.end()) return {{"id", id}, {"name", "?"}, {"userId", 0}, {"verified", false}};
    return {{"id", id}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
}

json GbServer::kanbanSummary(const json& b) const {
    long long done = 0;
    for (const json& c : b["columns"]) for (const json& k : c["cards"]) if (k.value("done", false)) ++done;
    return {{"id", sv(b, "id")}, {"title", sv(b, "title")}, {"color", sv(b, "color")}, {"visibility", sv(b, "visibility")},
            {"by", kanbanPerson(sv(b, "by"))}, {"people", b["members"].size() + 1}, {"columns", b["columns"].size()}, {"cards", countCards(b)},
            {"done", done}, {"edited", b.value("edited", b.value("at", 0LL))}, {"at", b.value("at", 0LL)}};
}

json GbServer::kanbanJson(const json& b, const User* me) const {
    json out = kanbanSummary(b);
    out["text"] = sv(b, "text");
    out["owner"] = me && sv(b, "by") == me->id;
    out["canEdit"] = kanbanOn(b, me);
    out["canDelete"] = me && (sv(b, "by") == me->id || isStaff(*me));
    json members = json::array();
    for (const json& id : b["members"]) members.push_back(kanbanPerson(id.get<std::string>()));
    out["members"] = members;
    json lists = json::array();
    for (const json& c : b["columns"]) {
        json cards = json::array();
        for (const json& k : c["cards"]) {
            json checks = json::array(), people = json::array();
            for (const json& x : k["checks"]) checks.push_back({{"id", sv(x, "id")}, {"text", sv(x, "text")}, {"done", x.value("done", false)}});
            for (const json& p : k["people"]) people.push_back(kanbanPerson(p.get<std::string>()));
            cards.push_back({{"id", sv(k, "id")}, {"title", sv(k, "title")}, {"text", sv(k, "text")}, {"labels", k["labels"]}, {"due", sv(k, "due")},
                             {"done", k.value("done", false)}, {"at", k.value("at", 0LL)}, {"by", kanbanPerson(sv(k, "by"))}, {"checks", checks}, {"people", people}});
        }
        lists.push_back({{"id", sv(c, "id")}, {"title", sv(c, "title")}, {"cards", cards}});
    }
    out["lists"] = lists;
    return out;
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::kanbanReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    json& all = m_kanban["boards"];
    if (!all.contains(id) || !kanbanCanSee(all[id], &me)) return "That board isn't there any more.";
    const json& b = all[id];
    if (sv(b, "by") == me.id) return "You can't report yourself.";
    std::string words = sv(b, "text");
    for (const json& c : b["columns"]) {
        words += (words.empty() ? "" : "\n") + sv(c, "title") + ":";
        for (const json& k : c["cards"]) words += "\n- " + sv(k, "title") + (sv(k, "text").empty() ? "" : ": " + sv(k, "text"));
    }
    target = id; about = sv(b, "by");
    copy = {{"subject", "Board: " + sv(b, "title")}, {"body", words.substr(0, 2000)}, {"at", b.value("at", 0LL)}};
    return "";
}
std::string GbServer::kanbanReportName(const std::string& target) const {
    const json& all = m_kanban["boards"];
    return all.contains(target) ? sv(all[target], "title") : "(deleted)";
}

json GbServer::kanbanOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_kanban["boards"];
    auto edited = [&](json& b) -> json { b["edited"] = t; saveKanban(); json r = okay(); r["board"] = kanbanJson(b, me); return r; };
    if (name == "board.list") {
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const json*> list;
        for (const auto& [id, b] : all.items()) if (sv(b, "visibility") == "public") list.push_back(&b);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("edited", 0LL) > y->value("edited", 0LL); });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(kanbanSummary(*list[i]));
        json r = okay(); r["boards"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page;
        return r;
    }
    const std::string id = sv(args, "id");
    json* bp = all.contains(id) ? &all[id] : nullptr;
    if (name == "board.get") {
        if (!bp || !kanbanCanSee(*bp, me)) return fail("That board isn't here, or it's private.");
        json r = okay(); r["board"] = kanbanJson(*bp, me); return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to make boards.");
    if (name == "board.mine") {
        std::vector<const json*> list;
        for (const auto& [k, b] : all.items()) if (kanbanOn(b, me)) list.push_back(&b);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("edited", 0LL) > y->value("edited", 0LL); });
        json out = json::array();
        for (const json* b : list) out.push_back(kanbanSummary(*b));
        json r = okay(); r["boards"] = out; return r;
    }
    if (name == "board.make") {
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return fail("Give your board a name.");
        size_t mine = 0;
        for (const auto& [k, b] : all.items()) if (sv(b, "by") == me->id) ++mine;
        if (mine >= kBoardsEach) return fail("You have " + std::to_string(kBoardsEach) + " boards. Delete an old one first.");
        const Template* tpl = &templates()[0];
        for (const Template& x : templates()) if (sv(args, "template") == x.id) tpl = &x;
        json columns = json::array();
        for (const auto& [ct, cards] : tpl->columns) {
            json list = json::array();
            for (const char* k : cards) list.push_back(newCard(k, me->id, t));
            columns.push_back({{"id", Account::randomHex(4)}, {"title", ct}, {"cards", list}});
        }
        const std::string bid = "bd-" + Account::randomHex(6);
        all[bid] = {{"id", bid}, {"by", me->id}, {"title", title}, {"text", ""}, {"visibility", "private"}, {"color", "red"},
                    {"members", json::array()}, {"at", t}, {"edited", t}, {"columns", columns}};
        saveKanban();
        json r = okay(); r["board"] = kanbanJson(all[bid], me); return r;
    }

    if (!bp || !kanbanCanSee(*bp, me)) return fail("That board isn't here (any more).");
    json& b = *bp;
    const bool owner = sv(b, "by") == me->id;
    if (name == "board.delete") {
        if (!owner && !isStaff(*me)) return fail("Only the board's owner, or staff, can delete it.");
        all.erase(id);
        saveKanban();
        return okay();
    }
    if (name == "board.member") {
        User* who = nullptr;
        if (sv(args, "who") == me->id) who = me;
        else if (args.contains("who") && !(args["who"].is_string() && args["who"].get<std::string>().empty()))
            who = findPerson(args["who"].is_number() ? std::to_string(args["who"].get<long long>()) : sv(args, "who"));
        if (!who) return fail("There's nobody with that name or number.");
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        json& members = b["members"];
        if (who->id == me->id && !on) {   // leaving
            if (owner) return fail("You own this board. Delete it instead, if you're done with it.");
            if (!has(members, me->id)) return fail("You're not on this board.");
            members.erase(std::remove(members.begin(), members.end(), json(me->id)), members.end());
        } else {
            if (!owner) return fail("Only the board's owner can add or remove people.");
            if (who->id == sv(b, "by")) return fail("That's you: you own this board.");
            if (on) {
                if (who->userId == 0) return fail("They need to sign up first.");
                if (has(members, who->id)) return fail(who->name + " is already on this board.");
                if (members.size() >= kMembers) return fail("A board can have " + std::to_string(kMembers) + " people on it at most.");
                members.push_back(who->id);
                notify(who, "board", me->name + " added you to the board \"" + sv(b, "title") + "\".", id);
            } else {
                if (!has(members, who->id)) return fail(who->name + " isn't on this board.");
                members.erase(std::remove(members.begin(), members.end(), json(who->id)), members.end());
            }
        }
        if (!on)   // (off their cards too)
            for (json& c : b["columns"]) for (json& k : c["cards"]) k["people"].erase(std::remove(k["people"].begin(), k["people"].end(), json(who->id)), k["people"].end());
        return edited(b);
    }
    if (!kanbanOn(b, me)) return fail("Only people on this board can change it. Ask its owner to add you.");
    if (name == "board.set") {
        if (!owner) return fail("Only the board's owner can change its settings.");
        if (args.contains("title")) {
            const std::string title = say(sv(args, "title"), kTitle);
            if (title.empty()) return fail("Give your board a name.");
            b["title"] = title;
        }
        if (args.contains("text")) b["text"] = say(sv(args, "text"), kText, true);
        if (args.contains("visibility")) b["visibility"] = isSee(sv(args, "visibility")) ? sv(args, "visibility") : "private";
        if (args.contains("color")) b["color"] = isColor(sv(args, "color")) ? sv(args, "color") : "red";
        return edited(b);
    }
    const std::string what = sv(args, "do");
    json& columns = b["columns"];
    auto colAt = [&](const std::string& cid) -> long long {
        for (size_t i = 0; i < columns.size(); ++i) if (sv(columns[i], "id") == cid) return (long long)i;
        return -1;
    };
    if (name == "board.column") {
        const long long ci = colAt(sv(args, "column"));
        if (what == "add") {
            const std::string title = say(sv(args, "title"), kColTitle);
            if (title.empty()) return fail("Give the column a name.");
            if (columns.size() >= kColumns) return fail("A board can have " + std::to_string(kColumns) + " columns at most.");
            place(columns, {{"id", Account::randomHex(4)}, {"title", title}, {"cards", json::array()}}, args);
        } else {
            if (ci < 0) return fail("That column isn't there any more.");
            if (what == "rename") {
                const std::string title = say(sv(args, "title"), kColTitle);
                if (title.empty()) return fail("Give the column a name.");
                columns[ci]["title"] = title;
            } else if (what == "move") {
                json col = columns[ci];
                columns.erase(columns.begin() + ci);
                place(columns, col, args);
            } else if (what == "delete") {
                columns.erase(columns.begin() + ci);
            } else return fail("Unknown request.");
        }
        return edited(b);
    }
    if (name == "board.card") {
        std::set<std::string> people = {sv(b, "by")};
        for (const json& x : b["members"]) people.insert(x.get<std::string>());
        long long ci = -1, ki = -1;
        const std::string cardId = sv(args, "card");
        for (size_t i = 0; i < columns.size() && ki < 0; ++i)
            for (size_t j = 0; j < columns[i]["cards"].size(); ++j)
                if (sv(columns[i]["cards"][j], "id") == cardId) { ci = (long long)i; ki = (long long)j; break; }
        const long long target = colAt(sv(args, "column"));
        // The bits of a card that were sent (the ones left out stay as they are). "" when it worked.
        auto fill = [&](json& k) -> std::string {
            if (args.contains("title")) {
                const std::string title = say(sv(args, "title"), kCardTitle);
                if (title.empty()) return "Give the card a name.";
                k["title"] = title;
            }
            if (args.contains("text")) k["text"] = say(sv(args, "text"), kCardText, true);
            if (args.contains("labels")) k["labels"] = labels(args["labels"]);
            if (args.contains("due")) k["due"] = due(sv(args, "due"));
            if (args.contains("done")) k["done"] = args["done"].is_boolean() && args["done"].get<bool>();
            if (args.contains("checks")) {
                const json list = args["checks"].is_array() ? args["checks"] : json::array();
                if (list.size() > kChecks) return "A checklist can have " + std::to_string(kChecks) + " things on it at most.";
                json out = json::array();
                for (const json& x : list) {
                    if (!x.is_object()) continue;
                    const std::string text = say(sv(x, "text"), kCheckText);
                    if (text.empty()) continue;
                    std::string cid = sv(x, "id");
                    if (!isHex(cid, 8, 8)) cid = Account::randomHex(4);
                    out.push_back({{"id", cid}, {"text", text}, {"done", x.contains("done") && x["done"] == true}});
                }
                k["checks"] = out;
            }
            if (args.contains("people")) {
                std::set<std::string> before;
                for (const json& p : k["people"]) before.insert(p.get<std::string>());
                json out = json::array();
                if (args["people"].is_array())
                    for (const json& p : args["people"])
                        if (p.is_string() && people.count(p.get<std::string>()) && !has(out, p.get<std::string>())) out.push_back(p);
                k["people"] = out;
                for (const json& p : out) {
                    const std::string pid = p.get<std::string>();
                    if (!before.count(pid) && pid != me->id)
                        notify(findUser(pid), "board", me->name + " put you on the card \"" + sv(k, "title") + "\" in \"" + sv(b, "title") + "\".", id);
                }
            }
            return "";
        };
        if (what == "add") {
            if (target < 0) return fail("That column isn't there any more.");
            if (countCards(b) >= kCards) return fail("A board can have " + std::to_string(kCards) + " cards at most. Delete some done ones first.");
            if (say(sv(args, "title"), kCardTitle).empty()) return fail("Give the card a name.");
            json card = newCard("", me->id, t);
            const std::string why = fill(card);
            if (!why.empty()) return fail(why);
            place(columns[target]["cards"], card, args);
        } else {
            if (ki < 0) return fail("That card isn't there any more.");
            json& cards = columns[ci]["cards"];
            if (what == "edit") {
                json card = cards[ki];
                const std::string why = fill(card);
                if (!why.empty()) return fail(why);
                cards[ki] = card;
            } else if (what == "move") {
                if (target < 0) return fail("That column isn't there any more.");
                json card = cards[ki];
                cards.erase(cards.begin() + ki);
                place(columns[target]["cards"], card, args);
            } else if (what == "delete") {
                cards.erase(cards.begin() + ki);
            } else return fail("Unknown request.");
        }
        return edited(b);
    }
    return fail("Unknown request.");
}
