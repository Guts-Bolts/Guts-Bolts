// G&B Image: put a picture online and share a link to it. worker/image.js has the same rules
// (and the notes on why):
//   - one picture (PNG, JPG, GIF or WebP, up to kMostBytes) with a title; the website also sends a small preview
//   - "public" ones are in the Recent list, "unlisted" ones only open with their link
//   - pictures from people who aren't Verified wait for a staff check ("pending")
//   - the uploader renames it or changes who can see it; the uploader or staff delete it
//   - kPerDay uploads a day (Verified: kPerDayVerified), at most kMostEach kept each
//   image.upload {title, data, thumb, visibility, w, h}  image.list {page, by}  image.mine {}  image.get {id, quiet}
//   image.picture {id, thumb} (no account too: worker/index.js asks for /imgpic/)  image.edit {id, title, visibility}
//   image.delete {id}  image.pending {}  image.review {id, ok, note}
// Kept in images.json ({images: id -> image, made: account -> {day, n}}); pictures are files/image-<id> and files/imagethumb-<id>.
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;
namespace fs = std::filesystem;

namespace {
constexpr size_t kTitle = 80, kMostBytes = 5 * 1024 * 1024, kThumbBytes = 200 * 1024, kPageSize = 30, kMostEach = 1000;
constexpr long long kPerDay = 20, kPerDayVerified = 100, kMostSide = 20000;

std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }
long long side(const json& args, const char* k) {
    const long long v = args.contains(k) && args[k].is_number() ? (long long)args[k].get<double>() : 0;
    return std::clamp(v, 0LL, kMostSide);
}
// What kind of picture these bytes are, by their first bytes (the same check as ServerArt.cpp).
std::string pictureType(const std::string& d) {
    auto b = [&](size_t i) { return (unsigned char)d[i]; };
    if (d.size() < 12) return "";
    if (b(0) == 0x89 && d.compare(1, 3, "PNG") == 0 && b(4) == 0x0d && b(5) == 0x0a && b(6) == 0x1a && b(7) == 0x0a) return "png";
    if (b(0) == 0xff && b(1) == 0xd8 && b(2) == 0xff) return "jpg";
    if (d.compare(0, 4, "GIF8") == 0) return "gif";
    if (d.compare(0, 4, "RIFF") == 0 && d.compare(8, 4, "WEBP") == 0) return "webp";
    return "";
}
} // namespace

void GbServer::saveImages() { writeFile(m_opts.data / "images.json", m_images.dump(1)); }
void GbServer::loadImages() {
    std::string text;
    if (readFile(m_opts.data / "images.json", text)) {
        json j = json::parse(text, nullptr, false);
        if (j.is_object() && j.contains("images") && j["images"].is_object()) m_images = j;
        if (!m_images.contains("made") || !m_images["made"].is_object()) m_images["made"] = json::object();
    }
}

bool GbServer::imageCanSee(const json& m, const User* me) const {
    if (sv(m, "review").empty()) return true;
    return me && (sv(m, "by") == me->id || isStaff(*me));
}

json GbServer::imageJson(const json& m, const User* me, bool full) const {
    const std::string by = sv(m, "by");
    auto u = m_users.find(by);
    json who = u == m_users.end() ? json{{"id", by}, {"name", "?"}, {"userId", 0}, {"verified", false}}
                                  : json{{"id", by}, {"name", u->second.name}, {"userId", u->second.userId}, {"verified", isVerified(u->second)}};
    json out = {{"id", sv(m, "id")}, {"title", sv(m, "title")}, {"type", sv(m, "type")}, {"visibility", sv(m, "visibility")},
                {"w", m.value("w", 0LL)}, {"h", m.value("h", 0LL)}, {"size", m.value("size", 0LL)}, {"at", m.value("at", 0LL)},
                {"views", m.value("views", 0LL)}, {"thumb", !sv(m, "thumb").empty()}, {"by", who}};
    if (!sv(m, "review").empty()) out["review"] = sv(m, "review");
    if (!full) return out;
    const bool mine = me && by == me->id, staff = me && isStaff(*me);
    out["mine"] = mine;
    out["canDelete"] = mine || staff;
    if ((mine || staff) && !sv(m, "reviewNote").empty()) out["reviewNote"] = sv(m, "reviewNote");
    return out;
}

// For report.send (ServerSafety.cpp): "" when it worked, or why not.
std::string GbServer::imageReportTarget(const std::string& id, const User& me, std::string& target, std::string& about, json& copy) {
    json& all = m_images["images"];
    if (!all.contains(id) || !imageCanSee(all[id], &me)) return "That picture isn't there any more.";
    const json& m = all[id];
    if (sv(m, "by") == me.id) return "You can't report yourself.";
    target = id; about = sv(m, "by");
    copy = {{"subject", "Image: " + sv(m, "title")}, {"body", "(a picture: /imgpic/" + id + ")"}, {"at", m.value("at", 0LL)}};
    return "";
}
std::string GbServer::imageReportName(const std::string& target) const {
    const json& all = m_images["images"];
    return all.contains(target) ? sv(all[target], "title") : "(deleted)";
}

json GbServer::imageOp(const std::string& name, User* me, const json& args) {
    const long long t = Online::unixNow();
    json& all = m_images["images"];
    if (name == "image.list") {
        const User* by = nullptr;
        if (args.contains("by") && !(args["by"].is_string() && args["by"].get<std::string>().empty())) {
            by = findPerson(args["by"].is_number() ? std::to_string(args["by"].get<long long>()) : sv(args, "by"));
            if (!by) return fail("There's nobody with that name or number.");
        }
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, 1000LL);
        std::vector<const json*> list;
        for (const auto& [id, m] : all.items()) {
            if (sv(m, "visibility") != "public" || !sv(m, "review").empty()) continue;
            if (by && sv(m, "by") != by->id) continue;
            list.push_back(&m);
        }
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) > y->value("at", 0LL); });
        json out = json::array();
        const size_t from = (size_t)(page - 1) * kPageSize;
        for (size_t i = from; i < list.size() && i < from + kPageSize; ++i) out.push_back(imageJson(*list[i], me, false));
        json r = okay(); r["images"] = out; r["more"] = list.size() > from + kPageSize; r["page"] = page;
        return r;
    }
    std::string id = sv(args, "id");
    json* m = all.contains(id) ? &all[id] : nullptr;
    if (name == "image.picture") {
        if (!m) {   // (links ending in .png and so on work too)
            for (const char* ext : {".png", ".jpg", ".jpeg", ".gif", ".webp"}) {
                const std::string e = ext;
                if (id.size() > e.size() && id.compare(id.size() - e.size(), e.size(), e) == 0 && all.contains(id.substr(0, id.size() - e.size()))) {
                    id = id.substr(0, id.size() - e.size()); m = &all[id]; break;
                }
            }
        }
        if (!m || !imageCanSee(*m, me)) return fail("No picture.");
        const bool thumb = args.value("thumb", false) && !sv(*m, "thumb").empty();
        std::string data;
        if (!readFile(m_opts.data / "files" / ((thumb ? "imagethumb-" : "image-") + id), data)) return fail("No picture.");
        json r = okay(); r["type"] = thumb ? sv(*m, "thumb") : sv(*m, "type"); r["data"] = Online::base64Encode(data); return r;
    }
    if (name == "image.get") {
        if (!m || !imageCanSee(*m, me)) return fail("That picture isn't here. It may have been deleted.");
        if (!(args.contains("quiet") && args["quiet"] == true) && (!me || sv(*m, "by") != me->id)) { (*m)["views"] = m->value("views", 0LL) + 1; saveImages(); }
        json r = okay(); r["image"] = imageJson(*m, me, true); return r;
    }

    if (!me || me->userId == 0) return fail("Sign up to upload pictures.");
    const bool staff = isStaff(*me), verified = isVerified(*me);
    if (name == "image.mine") {
        std::vector<const json*> list;
        for (const auto& [k, x] : all.items()) if (sv(x, "by") == me->id) list.push_back(&x);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) > y->value("at", 0LL); });
        json out = json::array();
        for (const json* x : list) out.push_back(imageJson(*x, me, false));
        json r = okay(); r["images"] = out; return r;
    }
    if (name == "image.upload") {
        json& made = m_images["made"][me->id];
        const long long today = t / 86400;
        if (!made.is_object() || made.value("day", 0LL) != today) made = {{"day", today}, {"n", 0}};
        const long long most = verified || staff ? kPerDayVerified : kPerDay;
        if (made.value("n", 0LL) >= most) return fail("You've uploaded " + std::to_string(most) + " pictures today. Come back tomorrow!");
        size_t mine = 0;
        for (const auto& [k, x] : all.items()) if (sv(x, "by") == me->id) ++mine;
        if (mine >= kMostEach) return fail("You have " + std::to_string(kMostEach) + " pictures. Delete some old ones first.");
        std::string data, thumb;
        std::string b64 = sv(args, "data");
        if (const size_t comma = b64.find(','); b64.rfind("data:", 0) == 0 && comma != std::string::npos) b64 = b64.substr(comma + 1);
        if (!Online::base64Decode(b64, data)) return fail("The picture got scrambled. Try again.");
        const std::string type = pictureType(data);
        if (type.empty()) return fail("Pictures must be .png, .jpg, .gif or .webp.");
        if (data.size() > kMostBytes) return fail("That picture is too big (the most is " + std::to_string(kMostBytes >> 20) + " MB).");
        if (!sv(args, "thumb").empty() && (!Online::base64Decode(sv(args, "thumb"), thumb) || pictureType(thumb).empty() || thumb.size() > kThumbBytes))
            thumb.clear();   // (the lists use the full picture instead)
        const std::string mid = "im-" + Account::randomHex(6);
        if (!writeFile(m_opts.data / "files" / ("image-" + mid), data)) return fail("The server couldn't save that picture.");
        if (!thumb.empty()) writeFile(m_opts.data / "files" / ("imagethumb-" + mid), thumb);
        std::string title = say(sv(args, "title"), kTitle);
        if (title.empty()) title = "Untitled";
        json img = {{"id", mid}, {"by", me->id}, {"title", title}, {"type", type}, {"thumb", thumb.empty() ? "" : pictureType(thumb)},
                    {"visibility", sv(args, "visibility") == "unlisted" ? "unlisted" : "public"}, {"w", side(args, "w")}, {"h", side(args, "h")},
                    {"size", data.size()}, {"at", t}, {"views", 0}};
        if (!verified && !staff) img["review"] = "pending";
        all[mid] = img;
        made["n"] = made.value("n", 0LL) + 1;
        saveImages();
        json r = okay(); r["image"] = imageJson(all[mid], me, true); return r;
    }
    if (name == "image.pending") {
        if (!staff) return fail("Only staff can do that.");
        std::vector<const json*> list;
        for (const auto& [k, x] : all.items()) if (sv(x, "review") == "pending") list.push_back(&x);
        std::sort(list.begin(), list.end(), [](const json* x, const json* y) { return x->value("at", 0LL) < y->value("at", 0LL); });
        json out = json::array();
        for (const json* x : list) out.push_back(imageJson(*x, me, false));
        json r = okay(); r["images"] = out; return r;
    }

    if (!m || !imageCanSee(*m, me)) return fail("That picture isn't here (any more).");
    if (name == "image.edit") {
        if (sv(*m, "by") != me->id) return fail("Only the person who uploaded it can change it.");
        std::string title = say(sv(args, "title"), kTitle);
        (*m)["title"] = title.empty() ? "Untitled" : title;
        if (args.contains("visibility")) (*m)["visibility"] = sv(args, "visibility") == "unlisted" ? "unlisted" : "public";
        saveImages();
        json r = okay(); r["image"] = imageJson(*m, me, true); return r;
    }
    if (name == "image.delete") {
        if (sv(*m, "by") != me->id && !staff) return fail("Only the person who uploaded it, or staff, can delete it.");
        std::error_code ec;
        fs::remove(m_opts.data / "files" / ("image-" + id), ec);
        fs::remove(m_opts.data / "files" / ("imagethumb-" + id), ec);
        all.erase(id);
        saveImages();
        return okay();
    }
    if (name == "image.review") {
        if (!staff) return fail("Only staff can do that.");
        if (sv(*m, "review").empty()) return fail("That isn't waiting for a check any more.");
        User* owner = findUser(sv(*m, "by"));
        const std::string title = sv(*m, "title");
        if (args.value("ok", false)) {
            m->erase("review"); m->erase("reviewNote");
            notify(owner, "image", "Your picture \"" + title + "\" passed the staff check. Its link works for everyone now!", id);
        } else {
            (*m)["review"] = "rejected";
            (*m)["reviewNote"] = say(sv(args, "note"), 200);
            const std::string note = sv(*m, "reviewNote");
            notify(owner, "image", "Your picture \"" + title + "\" didn't pass the staff check" + (note.empty() ? "." : ": " + note), id);
        }
        saveImages();
        json r = okay(); r["image"] = imageJson(*m, me, true); return r;
    }
    return fail("Unknown request.");
}
