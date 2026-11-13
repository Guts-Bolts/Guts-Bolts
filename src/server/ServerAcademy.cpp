// G&B Academy: courses anyone can write. worker/academy.js has the same rules (and the notes on why):
//   - a course: title, summary, topic, level, up to kMostLessons lessons (title + text)
//   - drafts until the writer publishes; learners tick lessons off (kept per account);
//     finishing someone else's course tells its writer
//   - likes, reports, deletes by the writer or staff; kPerDay new courses a day (Verified: kPerDayVerified)
//   academy.list {topic, level, q, sort, by, page}  academy.get {id}  academy.lesson {id, lesson}
//   academy.save {id?, title, summary, topic, level, lessons}  academy.publish {id, on}  academy.delete {id}
//   academy.done {id, lesson, on}  academy.like {id, on}  academy.mine {}
// Kept in academy.json ({courses: id -> course, progress: account -> {course: [lesson ids]}, made: account -> {day, n}}).
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
const char* kTopics[] = {"scripting", "building", "modeling", "animation", "gamedesign", "art", "audio", "studio", "other"};
const char* kLevels[] = {"beginner", "intermediate", "advanced"};
constexpr size_t kTitle = 80, kSummary = 300, kLessonTitle = 80, kLessonText = 20000, kMostLessons = 30, kPageSize = 24, kMostEach = 100;
constexpr long long kPerDay = 5, kPerDayVerified = 20;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
bool oneOf(const std::string& s, const char* const* list, size_t n) { for (size_t i = 0; i < n; ++i) if (s == list[i]) return true; return false; }
bool isTopic(const std::string& s) { return oneOf(s, kTopics, std::size(kTopics)); }
bool isLevel(const std::string& s) { return oneOf(s, kLevels, std::size(kLevels)); }
bool has(const json& arr, const std::string& id) { return arr.is_array() && std::find(arr.begin(), arr.end(), id) != arr.end(); }
bool hasLesson(const json& c, const std::string& id) { for (const json& l : c["lessons"]) if (sv(l, "id") == id) return true; return false; }
// Lesson text: tabs become 4 spaces (so code keeps its shape), then the usual clean-up.
std::string lessonText(const std::string& in) {
    std::string s;
    for (char ch : in) { if (ch == '\t') s += "    "; else s += ch; }
    return say(s, kLessonText, true);
}
} // namespace

void GbServer::saveAcademy() { writeFile(m_opts.data / "academy.json", m_academy.dump(1)); }
void GbServer::loadAcademy() {
    std::string text;
    if (readFile(m_opts.data / "academy.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("courses") && j["courses"].is_object()) m_academy = j;
    }
    for (const char* k : {"progress", "made"}) if (!m_academy.contains(k) || !m_academy[k].is_object()) m_academy[k] = json::object();
}

bool GbServer::academyCanSee(const json& c, const User* me) const {
    return c.value("published", false) || (me && (sv(c, "by") == me->id || isStaff(*me)));
}

json GbServer::academyJson(const json& c, const User* me, bool full) const {
    const std::string by = sv(c, "by"), id = sv(c, "id");
    auto u = m_users.find(by);
    json who = u == m_users.end() ? json{{"id", by}, {"name", "?"}, {"userId", 0}, {"verified", false}}
                                  : json{{"id", by}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    json done = json::array();
    if (me && m_academy["progress"].contains(me->id) && m_academy["progress"][me->id].contains(id))
        for (const json& x : m_academy["progress"][me->id][id]) if (x.is_string() && hasLesson(c, x.get<std::string>())) done.push_back(x);
    json out = {{"id", id}, {"title", sv(c, "title")}, {"summary", sv(c, "summary")}, {"topic", sv(c, "topic")}, {"level", sv(c, "level")},
                {"published", c.value("published", false)}, {"at", c.value("at", 0LL)}, {"edited", c.value("edited", 0LL)},
                {"lessons", c["lessons"].size()}, {"likes", c["likes"].size()}, {"learners", c.value("learners", 0LL)}, {"done", done.size()}, {"by", who}};
    if (full) {
        json list = json::array();
        for (const json& l : c["lessons"]) list.push_back({{"id", sv(l, "id")}, {"title", sv(l, "title")}, {"done", has(done, sv(l, "id"))}});
        out["lessonList"] = list;
        out["liked"] = me && has(c["likes"], me->id);
        out["mine"] = me && by == me->id;
        out["canDelete"] = me && (by == me->id || isStaff(*me));
    }
    return out;
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::academyReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    json& all = m_academy["courses"];
    if (!all.contains(id) || !academyCanSee(all[id], &me)) return "That course isn't there any more.";
    const json& c = all[id];
    if (sv(c, "by") == me.id) return "You can't report yourself.";
    target = id; about = sv(c, "by");
    copy = {{"subject", "Course: " + sv(c, "title")}, {"body", sv(c, "summary").empty() ? "(no summary)" : sv(c, "summary")}, {"at", c.value("at", 0LL)}};
    return "";
}
std::string GbServer::academyReportName(const std::string& target) const {
    const json& all = m_academy["courses"];
    return all.contains(target) ? sv(all[target], "title") : "(deleted)";
}

json GbServer::academyOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_academy["courses"];
    if (name == "academy.list") {
        const std::string topic = isTopic(sv(args, "topic")) ? sv(args, "topic") : "";
        const std::string level = isLevel(sv(args, "level")) ? sv(args, "level") : "";
        const bool top = sv(args, "sort") == "top";
        const std::string q = lower(sv(args, "q")).substr(0, 60);
        const User* by = nullptr;
        if (args.contains("by") && !(args["by"].is_string() && args["by"].get<std::string>().empty())) {
            by = findPerson(args["by"].is_number() ? std::to_string(args["by"].get<long long>()) : sv(args, "by"));
            if (!by) return fail("There's nobody with that name or number.");
        }
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const json*> list;
        for (const auto& [id, c] : all.items()) {
            if (!c.value("published", false) || c["lessons"].empty()) continue;
            if ((!topic.empty() && sv(c, "topic") != topic) || (!level.empty() && sv(c, "level") != level)) continue;
            if (by && sv(c, "by") != by->id) continue;
            if (!q.empty() && lower(sv(c, "title")).find(q) == std::string::npos && lower(sv(c, "summary")).find(q) == std::string::npos) continue;
            list.push_back(&c);
        }
        std::sort(list.begin(), list.end(), [&](const json* x, const json* y) {
            if (top && x->at("likes").size() != y->at("likes").size()) return x->at("likes").size() > y->at("likes").size();
            return x->value("at", 0LL) > y->value("at", 0LL);
        });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(academyJson(*list[i], me, false));
        json r = okay(); r["courses"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page; r["total"] = list.size();
        r["topics"] = json::array(); for (const char* x : kTopics) r["topics"].push_back(x);
        r["levels"] = json::array(); for (const char* x : kLevels) r["levels"].push_back(x);
        return r;
    }
    const std::string id = sv(args, "id");
    json* c = !id.empty() && all.contains(id) ? &all[id] : nullptr;
    if (name == "academy.get" || name == "academy.lesson") {
        if (!c || !academyCanSee(*c, me)) return fail("That course isn't here (any more).");
        json r = okay(); r["course"] = academyJson(*c, me, true);
        if (name == "academy.get") return r;
        const json& ls = (*c)["lessons"];
        for (size_t i = 0; i < ls.size(); ++i) {
            if (sv(ls[i], "id") != sv(args, "lesson")) continue;
            r["lesson"] = {{"id", sv(ls[i], "id")}, {"title", sv(ls[i], "title")}, {"text", sv(ls[i], "text")}, {"n", i + 1},
                           {"prev", i > 0 ? sv(ls[i - 1], "id") : ""}, {"next", i + 1 < ls.size() ? sv(ls[i + 1], "id") : ""}};
            return r;
        }
        return fail("That lesson isn't in this course (any more).");
    }
    if (!me || me->userId == 0) return fail("Sign up to write courses and keep your progress.");
    const bool staff = isStaff(*me);

    if (name == "academy.mine") {
        std::vector<const json*> mine;
        for (const auto& [k, x] : all.items()) if (sv(x, "by") == me->id) mine.push_back(&x);
        std::sort(mine.begin(), mine.end(), [](const json* x, const json* y) { return x->value("at", 0LL) > y->value("at", 0LL); });
        json written = json::array(), learning = json::array();
        for (const json* x : mine) written.push_back(academyJson(*x, me, false));
        if (m_academy["progress"].contains(me->id))
            for (const auto& [cid, ids] : m_academy["progress"][me->id].items())
                if (all.contains(cid) && all[cid].value("published", false) && sv(all[cid], "by") != me->id) learning.push_back(academyJson(all[cid], me, false));
        json r = okay(); r["written"] = written; r["learning"] = learning; return r;
    }
    if (name == "academy.save") {
        if (c && sv(*c, "by") != me->id) return fail("Only the writer can change this course.");
        if (!c && !id.empty()) return fail("That course isn't here (any more).");
        const std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) return fail("Give your course a title.");
        const std::string summary = say(sv(args, "summary"), kSummary, true);
        const std::string topic = isTopic(sv(args, "topic")) ? sv(args, "topic") : "other";
        const std::string level = isLevel(sv(args, "level")) ? sv(args, "level") : "beginner";
        const json given = args.contains("lessons") && args["lessons"].is_array() ? args["lessons"] : json::array();
        if (given.size() > kMostLessons) return fail("A course can have up to " + std::to_string(kMostLessons) + " lessons.");
        json lessons = json::array();
        for (size_t i = 0; i < given.size(); ++i) {
            const json& x = given[i];
            const std::string n = std::to_string(i + 1);
            if (!x.is_object()) return fail("Lesson " + n + " looks wrong.");
            const std::string lt = say(sv(x, "title"), kLessonTitle), text = lessonText(sv(x, "text"));
            if (lt.empty()) return fail("Give lesson " + n + " a title.");
            if (text.empty()) return fail("Lesson " + n + " (\"" + lt + "\") needs some text.");
            std::string lid = sv(x, "id");   // (keeps its id, so ticks stay)
            bool taken = false;
            for (const json& l : lessons) if (sv(l, "id") == lid) taken = true;
            if (!c || lid.empty() || taken || !hasLesson(*c, lid)) lid = Account::randomHex(4);
            lessons.push_back({{"id", lid}, {"title", lt}, {"text", text}});
        }
        if (c) {
            if (c->value("published", false) && lessons.empty()) return fail("A published course needs at least one lesson.");
            (*c)["title"] = title; (*c)["summary"] = summary; (*c)["topic"] = topic; (*c)["level"] = level; (*c)["lessons"] = lessons; (*c)["edited"] = t;
            saveAcademy();
            json r = okay(); r["course"] = academyJson(*c, me, true); return r;
        }
        json& made = m_academy["made"][me->id];
        const long long today = t / 86400;
        if (!made.is_object() || made.value("day", 0LL) != today) made = {{"day", today}, {"n", 0}};
        const long long most = isVerified(*me) || staff ? kPerDayVerified : kPerDay;
        if (made.value("n", 0LL) >= most) return fail("You've started " + std::to_string(most) + " courses today. Come back tomorrow!");
        size_t count = 0;
        for (const auto& [k, x] : all.items()) if (sv(x, "by") == me->id) ++count;
        if (count >= kMostEach) return fail("You have " + std::to_string(kMostEach) + " courses. Delete some first.");
        const std::string cid = Account::randomHex(5);
        all[cid] = {{"id", cid}, {"by", me->id}, {"title", title}, {"summary", summary}, {"topic", topic}, {"level", level}, {"lessons", lessons},
                    {"published", false}, {"at", t}, {"edited", 0}, {"likes", json::array()}, {"learners", 0}};
        made["n"] = made.value("n", 0LL) + 1;
        saveAcademy();
        json r = okay(); r["course"] = academyJson(all[cid], me, true); return r;
    }

    if (!c || !academyCanSee(*c, me)) return fail("That course isn't here (any more).");
    if (name == "academy.publish") {
        if (sv(*c, "by") != me->id) return fail("Only the writer can publish this course.");
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        if (on && (*c)["lessons"].empty()) return fail("Add at least one lesson first.");
        if (on && !c->value("published", false)) (*c)["at"] = t;
        (*c)["published"] = on;
        saveAcademy();
        json r = okay(); r["course"] = academyJson(*c, me, true); return r;
    }
    if (name == "academy.delete") {
        if (sv(*c, "by") != me->id && !staff) return fail("Only the writer or staff can delete this course.");
        all.erase(id);
        saveAcademy();
        return okay();
    }
    if (!c->value("published", false)) return fail("This course isn't published yet.");
    if (name == "academy.like") {
        if (sv(*c, "by") == me->id) return fail("You can't like your own course.");
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        json& likes = (*c)["likes"];
        const bool had = has(likes, me->id);
        if (on && !had) likes.push_back(me->id);
        if (!on && had) likes.erase(std::remove(likes.begin(), likes.end(), json(me->id)), likes.end());
        if (on != had) saveAcademy();
        json r = okay(); r["likes"] = likes.size(); r["liked"] = on; return r;
    }
    if (name == "academy.done") {
        const std::string lid = sv(args, "lesson");
        if (!hasLesson(*c, lid)) return fail("That lesson isn't in this course (any more).");
        json& mine = m_academy["progress"][me->id];
        if (!mine.is_object()) mine = json::object();
        const bool started = mine.contains(id);
        json list = json::array();
        if (started) for (const json& x : mine[id]) if (x.is_string() && hasLesson(*c, x.get<std::string>())) list.push_back(x);
        auto all_done = [&](const json& l) { for (const json& x : (*c)["lessons"]) if (!has(l, sv(x, "id"))) return false; return true; };
        const bool before = all_done(list);
        const bool on = !(args.contains("on") && args["on"].is_boolean() && !args["on"].get<bool>());
        if (on && !has(list, lid)) list.push_back(lid);
        if (!on) list.erase(std::remove(list.begin(), list.end(), json(lid)), list.end());
        mine[id] = list;
        if (!started && sv(*c, "by") != me->id) (*c)["learners"] = c->value("learners", 0LL) + 1;
        const bool finished = all_done(list);
        saveAcademy();
        if (finished && !before && sv(*c, "by") != me->id)
            notify(findUser(sv(*c, "by")), "academy", me->name + " finished your course \"" + sv(*c, "title") + "\"!", id);
        json r = okay(); r["done"] = list.size(); r["finished"] = finished; return r;
    }
    return fail("Unknown request.");
}
