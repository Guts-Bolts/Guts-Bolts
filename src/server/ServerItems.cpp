// Items after they're made: editing catalog items, Limiteds (numbered copies that people
// resell and trade), thumbs up / down and icons for games, making models public or private,
// and the update log staff post on the website. (worker/server.js has the same requests,
// rules and answers.)
#include "Server.h"
#include "ServerUtil.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <algorithm>
#include <cctype>

using json = nlohmann::json;
using namespace ServerUtil;

namespace {
constexpr int       kPublicModelsPerWeek = 5;
constexpr size_t    kTradeItems          = 4;     // items on each side of a trade
constexpr size_t    kOpenTrades          = 20;    // trades someone can have waiting
constexpr size_t    kMaxTrades           = 3000;  // kept (the open ones, and a month of finished ones)
constexpr long long kKeepTradesFor       = 30 * 86400;
// Past trades (worker/server.js has the same): a trade nobody answers expires after kTradeExpiry.
// Every finished trade goes in both people's Past trades, kept for kKeepPastTrades (at most
// kMaxPastTrades in all), kPastTradesPage to a page. Only the two people in a trade see it.
constexpr long long kTradeExpiry         = 30 * 86400;
constexpr long long kKeepPastTrades      = 365 * 86400;
constexpr size_t    kMaxPastTrades       = 50000;
constexpr long long kPastTradesPage      = 20;
// Screenshots on a game's page (worker/server.js has the same): up to kMaxShots pictures,
// .png, .jpg or .webp, up to kShotSize each.
constexpr size_t    kMaxShots            = 5;
constexpr size_t    kShotSize            = 2u << 20;
constexpr size_t    kMaxUpdates          = 300;
constexpr int       kTemplateW = 585, kTemplateH = 559;   // shirts and pants
constexpr unsigned  kTShirtMaxSide = 1024;
const char* const   kUpdateTags[] = {"Engine", "Studio", "Website", "Player", "Server", "Fix"};

long long weekOf(long long t) { return t / (7 * 86400); }
bool canBeLimited(const std::string& kind) { return Online::isAccessory(kind) || kind == "face"; }

// A PNG's width and height (false if it isn't a PNG).
bool pngSize(const std::string& d, unsigned& w, unsigned& h) {
    if (d.size() <= 24 || d.compare(0, 4, "\x89PNG") != 0) return false;
    auto u = [&](size_t i) { return (unsigned)(unsigned char)d[i]; };
    w = u(16) << 24 | u(17) << 16 | u(18) << 8 | u(19);
    h = u(20) << 24 | u(21) << 16 | u(22) << 8 | u(23);
    return true;
}
bool jpgFile(const std::string& d) {
    return d.size() > 3 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xD8 && (unsigned char)d[2] == 0xFF;
}
bool webpFile(const std::string& d) { return d.size() > 12 && d.compare(0, 4, "RIFF") == 0 && d.compare(8, 4, "WEBP") == 0; }
// "png", "jpg", "webp", or "" if it isn't one of those.
std::string pictureType(const std::string& d) {
    unsigned w = 0, h = 0;
    return pngSize(d, w, h) ? "png" : jpgFile(d) ? "jpg" : webpFile(d) ? "webp" : "";
}
bool tradeDone(const std::string& status) {
    return status == "accepted" || status == "declined" || status == "cancelled" || status == "expired" || status == "failed";
}
std::string tshirtProblem(const std::string& data) {
    unsigned w = 0, h = 0;
    const bool png = pngSize(data, w, h);
    if (!png && !jpgFile(data)) return "T-shirts must be .png or .jpg pictures.";
    if (png && (w > kTShirtMaxSide || h > kTShirtMaxSide))
        return "T-shirt pictures can be at most " + std::to_string(kTShirtMaxSide) + " x " + std::to_string(kTShirtMaxSide) + ".";
    return "";
}
// Why someone can't see a thing (like GbServer::op "get").
std::string noSeeText(const std::string& kind, const json& meta) {
    const std::string review = meta.is_object() ? meta.value("review", std::string()) : std::string();
    return review == "pending" ? "This is waiting for a staff check." : review == "rejected" ? "This didn't pass the staff check."
         : kind == "animation" ? "This animation is private." : "This model is private.";
}
// An id sent as text or as a number; a whole number sent as a number or as text.
std::string idText(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    return "";
}
long long wholeNumber(const json& v) {
    if (v.is_number()) return v.get<long long>();
    if (v.is_string()) { try { return std::stoll(v.get<std::string>()); } catch (...) {} }
    return 0;
}
} // namespace

// --- Limiteds -----------------------------------------------------------------------

json GbServer::copiesOf(const User& u) const {
    json out = json::array();
    for (const auto& [id, a] : m_assets) {
        if (!a.limited) continue;
        for (const json& c : a.copies)
            if (c.value("owner", std::string()) == u.id)
                out.push_back({{"id", a.id}, {"name", a.name}, {"kind", a.kind}, {"serial", c.value("serial", 0LL)}, {"price", c.value("price", 0LL)}});
    }
    return out;
}

// After copies change hands: `owned` (what you can wear) follows the copies you hold.
void GbServer::syncOwned(User& u, const Asset& a) {
    const bool has = std::any_of(a.copies.begin(), a.copies.end(), [&](const json& c) { return c.value("owner", std::string()) == u.id; });
    if (has) u.owned.insert(a.id);
    if (!has && a.creator != u.id) {
        u.owned.erase(a.id);
        if (u.avatar.is_object() && u.avatar.contains("wearing") && u.avatar["wearing"].is_array()) {
            json keep = json::array();
            for (const json& w : u.avatar["wearing"]) if (w != a.id) keep.push_back(w);
            u.avatar["wearing"] = keep;
        }
    }
    saveUsers();
}

json GbServer::limitedJson(const Asset& a) const {
    if (!a.limited) return nullptr;
    long long resellers = 0, lowest = 0;
    for (const json& c : a.copies) {
        const long long p = c.value("price", 0LL);
        if (p <= 0) continue;
        lowest = resellers == 0 ? p : std::min(lowest, p);
        ++resellers;
    }
    return {{"stock", a.stock}, {"sold", a.sales}, {"left", std::max(0LL, a.stock - a.sales)}, {"resellers", resellers}, {"lowest", lowest}};
}

// --- Public models: a few a week, unless you're Verified ------------------------------

int GbServer::publicModelsLeft(const User& u) const {
    if (isVerified(u)) return -1;
    const int used = u.publicWeek == weekOf(Online::unixNow()) ? u.publicThisWeek : 0;
    return std::max(0, kPublicModelsPerWeek - used);
}
void GbServer::countPublicModel(User& u) {
    if (isVerified(u)) return;
    const long long w = weekOf(Online::unixNow());
    if (u.publicWeek != w) { u.publicWeek = w; u.publicThisWeek = 0; }
    u.publicThisWeek++;
    saveUsers();
}
std::string GbServer::publicModelLimitText() {
    return "You've made " + std::to_string(kPublicModelsPerWeek) +
           " models public this week. Publish it as private for now, or get Verified for no limit.";
}

// --- The requests -------------------------------------------------------------------

json GbServer::itemOp(const std::string& name, User& me, const json& args) {
    const long long now = Online::unixNow();
    auto str = [&](const char* k) { return args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
    auto num = [&](const char* k) { return args.contains(k) && args[k].is_number() ? args[k].get<long long>() : 0LL; };
    auto arg = [&](const char* k) { return args.contains(k) ? args[k] : json(); };
    auto assetReply = [&](const Asset& a) { json r = okay(); r["asset"] = publicAsset(a, &me); return r; };
    // A Limited's copy with this number (null if there's no such copy).
    auto copyIn = [](Asset& a, long long serial) -> json* {
        if (!a.limited) return nullptr;
        for (json& c : a.copies) if (c.value("serial", 0LL) == serial) return &c;
        return nullptr;
    };
    auto copyOf = [&](const std::string& id, long long serial) -> json* {
        auto it = findAsset(id);
        return it == m_assets.end() ? nullptr : copyIn(it->second, serial);
    };

    // --- Games: thumbs up or down (you have to have played it first), and icons ---
    if (name == "game.vote") {
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        if (std::find(me.played.begin(), me.played.end(), a.id) == me.played.end() && a.creator != me.id)
            return fail("Play the game first, then you can vote.");
        const long long n = num("vote"), v = n > 0 ? 1 : n < 0 ? -1 : 0;
        if (!a.votes.is_object()) a.votes = json::object();
        const long long old = a.votes.value(me.id, 0LL);
        if (old == 1) a.likes--; else if (old == -1) a.dislikes--;
        if (v == 0) a.votes.erase(me.id); else a.votes[me.id] = v;
        if (v == 1) a.likes++;
        if (v == -1) a.dislikes++;
        saveAssets();
        return assetReply(a);
    }
    if (name == "icon.set") {
        // A game's icon: a small square picture (PNG or JPG, up to 200 KB).
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("You can only change your own games.");
        std::string data;
        if (!Online::base64Decode(str("data"), data)) return fail("The picture got scrambled. Try again.");
        const bool png = data.size() > 8 && data.compare(0, 4, "\x89PNG") == 0;
        const bool jpg = data.size() > 3 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xD8;
        if (!png && !jpg) return fail("Icons must be .png or .jpg.");
        if (data.size() > 200 * 1024) return fail("That icon is too big (the most is 200 KB).");
        if (!writeFile(m_opts.data / "files" / ("icon-" + a.id), data)) return fail("The server couldn't save that picture.");
        a.icon = now;
        saveAssets();
        json r = okay(); r["asset"] = publicAsset(a); return r;
    }
    if (name == "icon.get") {   // a game's icon (the app shows it while you connect)
        auto it = findAsset(str("id"));
        if (it != m_assets.end() && !canSee(it->second, me)) return fail(noSeeText(it->second.kind, it->second.meta));
        if (it == m_assets.end()) return fail("That game doesn't exist (any more).");
        const Asset& a = it->second;
        std::string data;
        const bool has = a.icon && readFile(m_opts.data / "files" / ("icon-" + a.id), data);
        const User* c = findUser(a.creator);
        json r = okay();
        r["data"] = has ? Online::base64Encode(data) : std::string();
        r["icon"] = has ? a.icon : 0LL;
        r["name"] = a.name;
        r["creatorName"] = c ? c->name : std::string();
        return r;
    }
    if (name == "model.access") {
        // Make one of your Library models public or private.
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || (it->second.kind != "model" && it->second.kind != "animation")) return fail("That model doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("You can only change your own models.");
        const std::string access = str("access") == "private" ? "private" : "public";
        if (!a.meta.is_object()) a.meta = json::object();
        if (access == "public" && a.meta.value("access", std::string("public")) != "public" && a.kind == "model") {
            if (publicModelsLeft(me) == 0) return fail(publicModelLimitText());
            countPublicModel(me);
        }
        a.meta["access"] = access;
        saveAssets();
        json r = assetReply(a); r["me"] = meJson(me); return r;
    }

    // --- Editing items (their creator or staff) ---
    if (name == "item.edit") {
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || !Online::isCatalogItem(it->second.kind)) return fail("That item doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("Only the item's creator or staff can change it.");
        if (args.contains("name")) {
            const std::string title = say(str("name"), 50);
            if (title.empty()) return fail("Give it a name.");
            a.name = title;
        }
        if (args.contains("description")) a.description = say(str("description"), 1000, true);
        if (args.contains("price")) {
            const long long price = std::clamp(num("price"), 0LL, 1000000LL);
            const User* creator = findUser(a.creator);
            if (price > 0 && !isVerified(creator ? *creator : me) && !isStaff(me)) return fail("Only Verified creators can sell things.");
            a.price = price;
        }
        if (!a.meta.is_object()) a.meta = json::object();
        if (args.contains("offsaleAt")) {   // a timed item: off sale from then on (0 = for sale for good)
            a.meta["offsaleAt"] = std::max(0LL, num("offsaleAt"));
            if (a.meta.value("timedFor", std::string()).empty()) a.meta["timedFor"] = "set";
        }
        if (a.kind == "hat" && args.contains("style")) a.meta["style"] = std::clamp(num("style"), 1LL, 3LL);   // the hat's shape
        const bool picture = a.kind == "shirt" || a.kind == "pants" || a.kind == "face" || a.kind == "tshirt";
        if (picture && !str("data").empty()) {   // a new clothing / face picture
            std::string data;
            if (!Online::base64Decode(str("data"), data)) return fail("The upload got scrambled. Try again.");
            unsigned w = 0, h = 0;
            const bool png = pngSize(data, w, h);
            if (a.kind == "face") {
                if (!png) return fail("Faces must be .png pictures.");
            } else if (a.kind == "tshirt") {
                if (std::string problem = tshirtProblem(data); !problem.empty()) return fail(problem);
            } else if (!png || w != (unsigned)kTemplateW || h != (unsigned)kTemplateH) {
                return fail("Clothing pictures must be " + std::to_string(kTemplateW) + " x " + std::to_string(kTemplateH) + " .png files.");
            }
            if (data.size() > Online::maxSize(a.kind)) return fail("That's too big.");
            if (!writeFile(blobPath(a.id), data)) return fail("The server couldn't save that file.");
            if ((a.kind == "face" || a.kind == "tshirt") && data.size() <= 400 * 1024 &&
                writeFile(m_opts.data / "files" / ("thumb-" + a.id), data))
                a.thumb = now;
            a.meta["image"] = true;
            a.meta["ext"] = a.kind == "tshirt" && !png ? "jpg" : "png";
            a.size = data.size();
        }
        a.meta["updated"] = now;
        saveAssets();
        return assetReply(a);
    }
    if (name == "item.limited") {
        // Only Guts makes items Limited: a fixed number of numbered copies. People who
        // already have it get the first serial numbers.
        if (!isOfficial(me)) return fail("Only Guts can make items Limited.");
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || !Online::isClothing(it->second.kind)) return fail("That item doesn't exist (any more).");
        Asset& a = it->second;
        if (!canBeLimited(a.kind)) return fail("Only accessories and faces can be Limited (not shirts, pants, audio, decals or models).");
        if (a.creator != me.id) return fail("Only accessories and faces Guts made can be Limited.");
        const long long stock = std::clamp(num("stock"), 1LL, 1000000LL);
        if (!a.limited) {
            a.copies = json::array();
            long long serial = 0;
            for (const auto& [uid, u] : m_users)
                if (u.owned.count(a.id) && u.id != a.creator) a.copies.push_back({{"serial", ++serial}, {"owner", u.id}, {"price", 0}});
            a.sales = serial;
            a.limited = true;
        }
        if (stock < a.sales) {
            saveAssets();
            return fail(std::to_string(a.sales) + " copies are already out there: the stock can't be less than that.");
        }
        a.stock = stock;
        saveAssets();
        return assetReply(a);
    }
    if (name == "item.copies") {
        // A Limited's copies: who has which number, and which are for sale.
        auto it = findAsset(str("id"));
        json copies = json::array();
        if (it != m_assets.end() && it->second.limited)
            for (const json& c : it->second.copies) {
                // (Who holds a copy that isn't for sale stays hidden when they hid their inventory from you.)
                const std::string owner = c.value("owner", std::string());
                const long long price = c.value("price", 0LL);
                const User* u = findUser(owner);
                const bool shown = price > 0 || !u || seesInventory(me, *u);
                copies.push_back({{"serial", c.value("serial", 0LL)}, {"price", price}, {"owner", shown ? owner : std::string()},
                                  {"ownerName", !shown ? std::string("Hidden") : u ? u->name : std::string("?")}, {"mine", owner == me.id}});
            }
        json r = okay(); r["copies"] = copies; return r;
    }

    // --- Reselling Limited copies ---
    if (name == "resale.list") {
        // Put your copy of a Limited up for sale (price 0 = take it off sale).
        auto it = findAsset(str("id"));
        json* c = it == m_assets.end() ? nullptr : copyIn(it->second, num("serial"));
        if (!c || c->value("owner", std::string()) != me.id) return fail("You don't have that copy.");
        (*c)["price"] = std::clamp(num("price"), 0LL, 10000000LL);
        saveAssets();
        return assetReply(it->second);
    }
    if (name == "resale.buy") {
        auto it = findAsset(str("id"));
        json* c = it == m_assets.end() ? nullptr : copyIn(it->second, num("serial"));
        if (!c || c->value("price", 0LL) <= 0) return fail("That copy isn't for sale (any more).");
        Asset& a = it->second;
        const std::string owner = c->value("owner", std::string());
        const long long price = c->value("price", 0LL), serial = c->value("serial", 0LL);
        if (owner == me.id) return fail("That's your own copy.");
        if (balance(me) < price) return fail("You need " + std::to_string(price - balance(me)) + " more Bolts for that.");
        User* seller = findUser(owner);
        const std::string copyName = a.name + " #" + std::to_string(serial);
        add(me, -price, "Bought " + copyName, "resale:" + a.id + ":" + std::to_string(serial) + ":" + Account::randomHex(4));
        if (seller) {
            const long long share = price * Online::kCreatorSharePercent / 100;
            add(*seller, share, "Sold " + copyName, "resold:" + a.id + ":" + std::to_string(serial) + ":" + Account::randomHex(4));
            notify(seller, "sale", me.name + " bought your " + copyName + ". You got " + std::to_string(share) + " Bolts.", a.id);
        }
        (*c)["owner"] = me.id;
        (*c)["price"] = 0;
        saveAssets();
        syncOwned(me, a);
        if (seller) syncOwned(*seller, a);
        json r = okay(); r["me"] = meJson(me); return r;
    }

    // --- Trading Limited copies ---
    if (name.rfind("trade.", 0) == 0) expireTrades(now);   // (trades nobody answered in 30 days run out)
    if (name == "trade.inventory") {
        User* u = findPerson(idText(arg("user")));
        if (!u) return fail("There's no account with that ID on this server.");
        // Someone who hid their inventory from you: you see nothing (and can't trade with them).
        const bool canSeeIt = seesInventory(me, *u);
        json r = okay(); r["user"] = publicUser(*u); r["items"] = canSeeIt ? copiesOf(*u) : json::array(); r["hidden"] = !canSeeIt;
        return r;
    }
    if (name == "trade.send") {
        User* to = findPerson(idText(arg("to")));
        if (!to || to->userId == 0) return fail("There's no account with that ID on this server.");
        if (to->id == me.id) return fail("You can't trade with yourself.");
        if (blocks(me, *to)) return fail("You can't trade with that account.");
        if (!seesInventory(me, *to)) return fail(to->name + " keeps their inventory private, so you can't trade with them.");
        auto pick = [&](const char* k) {   // the first few {id, serial}
            json out = json::array();
            const json list = arg(k);
            if (list.is_array())
                for (const json& x : list) {
                    if (out.size() >= kTradeItems) break;
                    out.push_back({{"id", x.is_object() && x.contains("id") ? idText(x["id"]) : std::string()},
                                   {"serial", x.is_object() && x.contains("serial") ? wholeNumber(x["serial"]) : 0LL}});
                }
            return out;
        };
        const json give = pick("give"), get = pick("get");
        if (give.empty() || get.empty()) return fail("Pick at least one of your limiteds and one of theirs.");
        auto holds = [&](const User& u, const json& list) {
            for (const json& x : list) {
                const json* c = copyOf(x["id"].get<std::string>(), x["serial"].get<long long>());
                if (!c || c->value("owner", std::string()) != u.id) return false;
            }
            return true;
        };
        if (!holds(me, give)) return fail("You don't have all of those.");
        if (!holds(*to, get)) return fail("They don't have all of those.");
        size_t waiting = 0;
        for (const json& x : m_trades) waiting += x.value("from", std::string()) == me.id && x.value("status", std::string()) == "open";
        if (waiting >= kOpenTrades) return fail("You have " + std::to_string(kOpenTrades) + " trades waiting already.");
        json trade = {{"id", "tr-" + Account::randomHex(6)}, {"from", me.id}, {"to", to->id}, {"give", give}, {"get", get},
                      {"status", "open"}, {"created", now}, {"updated", now}};
        m_trades.push_back(trade);
        saveTrades();
        notify(to, "trade", me.name + " sent you a trade.", trade["id"].get<std::string>());
        json r = okay(); r["trade"] = trade; return r;
    }
    if (name == "trade.list") {
        auto label = [&](const json& list) {
            json out = json::array();
            for (const json& i : list) {
                auto it = findAsset(i.value("id", std::string()));
                json one = {{"name", it != m_assets.end() ? it->second.name : std::string("(gone)")},
                            {"kind", it != m_assets.end() ? it->second.kind : std::string()}};
                one.update(i);
                out.push_back(one);
            }
            return out;
        };
        std::vector<const json*> mine;
        for (const json& x : m_trades)
            if (x.value("from", std::string()) == me.id || x.value("to", std::string()) == me.id) mine.push_back(&x);
        if (mine.size() > 100) mine.erase(mine.begin(), mine.end() - 100);
        json trades = json::array();
        for (auto p = mine.rbegin(); p != mine.rend(); ++p) {   // newest first
            const json& x = **p;
            const User* f = findUser(x.value("from", std::string()));
            const User* t = findUser(x.value("to", std::string()));
            json one = x;
            one["fromName"] = f ? f->name : std::string("?");
            one["toName"] = t ? t->name : std::string("?");
            one["give"] = label(x["give"]);
            one["get"] = label(x["get"]);
            trades.push_back(one);
        }
        json r = okay(); r["trades"] = trades; r["items"] = copiesOf(me); return r;
    }
    if (name == "trade.accept" || name == "trade.decline" || name == "trade.cancel") {
        json* tr = nullptr;
        for (json& x : m_trades) if (x.value("id", std::string()) == str("id")) { tr = &x; break; }
        if (!tr || tr->value("status", std::string()) != "open") return fail("That trade isn't open (any more).");
        const std::string fromId = tr->value("from", std::string()), toId = tr->value("to", std::string());
        if (name == "trade.cancel" ? fromId != me.id : toId != me.id) return fail("That isn't your trade to answer.");
        if (name != "trade.accept") {
            finishTrade(*tr, name == "trade.cancel" ? "cancelled" : "declined", now);
            const json trade = *tr;   // (saving tidies the list, so `tr` doesn't last)
            saveTrades();
            if (name == "trade.decline") notify(findUser(fromId), "trade", me.name + " turned down your trade.", trade.value("id", std::string()));
            json r = okay(); r["trade"] = trade; return r;
        }
        User* from = findUser(fromId);
        auto holds = [&](const User* u, const json& list) {
            for (const json& x : list) {
                const json* c = copyOf(x.value("id", std::string()), x.value("serial", 0LL));
                if (!u || !c || c->value("owner", std::string()) != u->id) return false;
            }
            return true;
        };
        if (!from || !holds(from, (*tr)["give"]) || !holds(&me, (*tr)["get"])) {
            finishTrade(*tr, "failed", now);
            saveTrades();
            return fail("Someone doesn't have those items any more, so the trade can't happen.");
        }
        std::set<std::string> touched;
        auto move = [&](const json& list, const User& to) {
            for (const json& x : list) {
                json* c = copyOf(x.value("id", std::string()), x.value("serial", 0LL));
                (*c)["owner"] = to.id;
                (*c)["price"] = 0;
                touched.insert(findAsset(x.value("id", std::string()))->first);
            }
        };
        move((*tr)["give"], me);
        move((*tr)["get"], *from);
        for (const std::string& id : touched) {
            const Asset& a = m_assets.at(id);
            syncOwned(me, a);
            syncOwned(*from, a);
        }
        finishTrade(*tr, "accepted", now);
        const json trade = *tr;   // (saving tidies the list, so `tr` doesn't last)
        saveAssets();
        saveTrades();
        notify(from, "trade", me.name + " accepted your trade!", trade.value("id", std::string()));
        json r = okay(); r["trade"] = trade; r["me"] = meJson(me); return r;
    }
    // Past trades: your finished trades, newest first, a page at a time. Only the two people
    // in a trade see it. `gave` and `got` are from your side.
    if (name == "trade.history") {
        std::vector<const json*> mine;
        for (auto x = m_pastTrades.rbegin(); x != m_pastTrades.rend(); ++x)   // (the list is oldest first)
            if (x->value("from", std::string()) == me.id || x->value("to", std::string()) == me.id) mine.push_back(&*x);
        std::stable_sort(mine.begin(), mine.end(), [](const json* x, const json* y) {   // newest first
            return x->value("finished", 0LL) > y->value("finished", 0LL);
        });
        const long long total = (long long)mine.size();
        const long long pages = std::max(1LL, (total + kPastTradesPage - 1) / kPastTradesPage);
        long long page = args.contains("page") && args["page"].is_number() ? (long long)args["page"].get<double>() : 1;
        page = std::clamp(page, 1LL, pages);
        json trades = json::array();
        for (long long i = (page - 1) * kPastTradesPage; i < total && i < page * kPastTradesPage; ++i) {
            const json& x = *mine[(size_t)i];
            const bool sent = x.value("from", std::string()) == me.id;
            const std::string otherId = x.value(sent ? "to" : "from", std::string());
            const User* other = findUser(otherId);
            trades.push_back({{"id", x.value("id", std::string())}, {"status", x.value("status", std::string())},
                              {"created", x.value("created", 0LL)}, {"finished", x.value("finished", 0LL)}, {"sent", sent},
                              {"with", {{"id", otherId}, {"userId", other ? other->userId : 0LL},
                                        {"name", other ? other->name : x.value(sent ? "toName" : "fromName", std::string("?"))}}},
                              {"gave", x.value(sent ? "give" : "get", json::array())}, {"got", x.value(sent ? "get" : "give", json::array())}});
        }
        json r = okay(); r["trades"] = trades; r["page"] = page; r["pages"] = pages; r["total"] = total; return r;
    }

    // --- Screenshots on a game's page: the creator (or staff) adds up to kMaxShots, deletes
    // them and puts them in order. The page shows them, in that order, after the thumbnail.
    auto shotFile = [&](const Asset& a, const std::string& id) { return m_opts.data / "files" / ("shot-" + a.id + "-" + id); };
    if (name == "shots.add" || name == "shots.delete" || name == "shots.order") {
        auto it = findAsset(str("id"));
        if (it == m_assets.end() || it->second.kind != "game") return fail("That game doesn't exist (any more).");
        Asset& a = it->second;
        if (a.creator != me.id && !isStaff(me)) return fail("You can only change your own games.");
        if (name == "shots.add") {
            if (a.shots.size() >= kMaxShots) return fail("A game can have " + std::to_string(kMaxShots) + " screenshots. Delete one first.");
            std::string data;
            if (!Online::base64Decode(str("data"), data)) return fail("The picture got scrambled. Try again.");
            const std::string type = pictureType(data);
            if (type.empty()) return fail("Screenshots must be .png, .jpg or .webp pictures.");
            if (data.size() > kShotSize)
                return fail("That screenshot is too big (the most is " + std::to_string(kShotSize >> 20) + " MB). Try a smaller picture, or save it as a .jpg.");
            const json shot = {{"id", Account::randomHex(6)}, {"at", now}, {"type", type}};
            if (!writeFile(shotFile(a, shot["id"].get<std::string>()), data)) return fail("The server couldn't save that picture.");
            a.shots.push_back(shot);
        } else if (name == "shots.delete") {
            auto s = std::find_if(a.shots.begin(), a.shots.end(), [&](const json& x) { return x.value("id", std::string()) == str("shot"); });
            if (s == a.shots.end()) return fail("That screenshot is already gone.");
            std::error_code ec;
            fs::remove(shotFile(a, s->value("id", std::string())), ec);
            a.shots.erase(s);
            if (a.creator != me.id) staffDid(me, "screenshot", "Deleted a screenshot of the game \"" + a.name + "\"", a.creator);
        } else {
            std::vector<std::string> order;
            if (args.contains("order") && args["order"].is_array())
                for (const json& x : args["order"]) order.push_back(x.is_string() ? x.get<std::string>() : std::string());
            json sorted = json::array();
            std::set<std::string> seen;
            for (const std::string& id : order) {
                auto s = std::find_if(a.shots.begin(), a.shots.end(), [&](const json& x) { return x.value("id", std::string()) == id; });
                if (s == a.shots.end() || !seen.insert(id).second) break;
                sorted.push_back(*s);
            }
            if (sorted.size() != a.shots.size() || order.size() != a.shots.size())
                return fail("The screenshots changed while you were moving them. Reload the page and try again.");
            a.shots = sorted;
        }
        saveAssets();
        json ids = json::array();
        for (const json& s : a.shots) ids.push_back(s.value("id", std::string()));
        json r = assetReply(a); r["shots"] = ids; return r;
    }
    if (name == "shots.get") {
        // One screenshot's picture (the main server's website shows them from /shot/<game>/<screenshot>).
        auto it = findAsset(str("id"));
        std::string data;
        if (it != m_assets.end() && it->second.kind == "game")
            for (const json& s : it->second.shots)
                if (s.value("id", std::string()) == str("shot") && readFile(shotFile(it->second, str("shot")), data)) {
                    json r = okay(); r["data"] = Online::base64Encode(data); r["type"] = s.value("type", std::string()); return r;
                }
        return fail("No picture.");
    }

    // --- The update log (the website's Updates page). The built-in updates are in
    // worker/updates.js, which only the main server has; this one lists what staff posted here.
    if (name == "updates.list") {
        // Newest first. A date in the future (a typo) counts as now, so it can't sit on top for ever.
        json all = json::array();
        for (json u : m_updates) {
            u["time"] = std::min(u.value("time", 0LL), now);
            all.push_back(u);
        }
        std::stable_sort(all.begin(), all.end(), [](const json& x, const json& y) { return x["time"].get<long long>() > y["time"].get<long long>(); });
        const long long limit = std::clamp(args.contains("limit") && args["limit"].is_number_integer() ? args["limit"].get<long long>() : 50LL, 1LL, 100LL);
        json list = json::array();
        for (size_t i = 0; i < all.size() && (long long)list.size() < limit; ++i) list.push_back(all[i]);
        json r = okay(); r["updates"] = list; r["latest"] = all.empty() ? std::string() : all[0].value("id", std::string()); return r;
    }
    if (name == "updates.post") {
        if (!isStaff(me)) return fail("Only staff can post updates.");
        const std::string title = Online::cleanText(str("name"), 40);
        if (title.empty()) return fail("Give the update a name.");
        const std::string summary = Online::cleanText(str("summary"), 200);
        json items = json::array();
        if (args.contains("items") && args["items"].is_array())
            for (const json& x : args["items"]) {
                if (!x.is_string() || items.size() >= 20) continue;
                const std::string line = Online::cleanText(x.get<std::string>(), 160);
                if (!line.empty()) items.push_back(line);
            }
        if (summary.empty() && items.empty()) return fail("Say what changed.");
        std::string tag = "Engine";
        for (const char* t : kUpdateTags) if (str("tag") == t) tag = t;
        json u = {{"id", "u-" + Account::randomHex(6)}, {"name", title}, {"version", Online::cleanText(str("version"), 12)},
                  {"time", now}, {"tag", tag}, {"summary", summary}, {"items", items},
                  {"by", !me.username.empty() ? me.username : me.name}};
        m_updates.insert(m_updates.begin(), u);
        if (m_updates.size() > kMaxUpdates) m_updates.erase(m_updates.begin() + kMaxUpdates, m_updates.end());
        saveUpdates();
        json r = okay(); r["update"] = u; return r;
    }
    if (name == "updates.delete") {
        if (!isStaff(me)) return fail("Only staff can delete updates.");
        const std::string id = str("id");
        json keep = json::array();
        for (const json& u : m_updates) if (u.value("id", std::string()) != id) keep.push_back(u);
        if (keep.size() == m_updates.size()) return fail("That update is built in (or already gone).");
        m_updates = keep;
        saveUpdates();
        return okay();
    }
    return fail("The server doesn't know how to do \"" + name + "\". It might need updating.");
}

// --- Saving and loading ---------------------------------------------------------------

void GbServer::saveTrades() {
    // Keep the open trades, and the finished ones for a month (at most kMaxTrades in all).
    const long long now = Online::unixNow();
    json kept = json::array();
    for (const json& x : m_trades)
        if (x.value("status", std::string()) == "open" || now - x.value("updated", 0LL) < kKeepTradesFor) kept.push_back(x);
    if (kept.size() > kMaxTrades) kept.erase(kept.begin(), kept.end() - (long)kMaxTrades);
    m_trades = kept;
    writeFile(m_opts.data / "trades.json", m_trades.dump(1));
}

void GbServer::loadTrades() {
    std::string text;
    if (!readFile(m_opts.data / "trades.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (!all.is_array()) return;
    for (const json& x : all)
        if (x.is_object() && x.contains("give") && x["give"].is_array() && x.contains("get") && x["get"].is_array()) m_trades.push_back(x);
}

void GbServer::finishTrade(json& tr, const std::string& status, long long now) {
    tr["status"] = status;
    tr["updated"] = now;
    keepPastTrade(tr);
}

// A finished trade goes in Past trades with the names of the people and items as they were
// then (so it still reads right if an item is deleted later).
void GbServer::keepPastTrade(const json& tr, bool onlyIfNew) {
    const std::string id = tr.value("id", std::string());
    auto nameOf = [&](const std::string& who) { const User* u = findUser(who); return u ? u->name : std::string("?"); };
    auto label = [&](const json& list) {
        json out = json::array();
        if (list.is_array())
            for (const json& i : list) {
                auto it = findAsset(i.value("id", std::string()));
                out.push_back({{"id", i.value("id", std::string())}, {"serial", i.value("serial", 0LL)},
                               {"name", it != m_assets.end() ? it->second.name : std::string("(gone)")},
                               {"kind", it != m_assets.end() ? it->second.kind : std::string()}});
            }
        return out;
    };
    json rec = {{"id", id}, {"from", tr.value("from", std::string())}, {"to", tr.value("to", std::string())},
                {"fromName", nameOf(tr.value("from", std::string()))}, {"toName", nameOf(tr.value("to", std::string()))},
                {"give", label(tr.contains("give") ? tr["give"] : json())}, {"get", label(tr.contains("get") ? tr["get"] : json())},
                {"status", tr.value("status", std::string())}, {"created", tr.value("created", 0LL)}, {"finished", tr.value("updated", 0LL)}};
    if (!onlyIfNew)   // (onlyIfNew: the caller knows it isn't there yet)
        for (auto x = m_pastTrades.rbegin(); x != m_pastTrades.rend(); ++x)
            if (x->value("id", std::string()) == id) {
                *x = rec;
                savePastTrades();
                return;
            }
    m_pastTrades.push_back(rec);
    if (!onlyIfNew) savePastTrades();
}

void GbServer::expireTrades(long long now) {
    bool any = false;
    for (json& x : m_trades)
        if (x.value("status", std::string()) == "open" && now - x.value("created", 0LL) >= kTradeExpiry) {
            finishTrade(x, "expired", now);
            const User* to = findUser(x.value("to", std::string()));
            notify(findUser(x.value("from", std::string())), "trade",
                   "Your trade with " + (to ? to->name : std::string("someone")) + " expired: nobody answered it in 30 days.", x.value("id", std::string()));
            any = true;
        }
    if (any) saveTrades();
}

void GbServer::savePastTrades() {
    // Keep a year of them (at most kMaxPastTrades, the newest).
    const long long now = Online::unixNow();
    json kept = json::array();
    for (const json& x : m_pastTrades) if (now - x.value("finished", 0LL) <= kKeepPastTrades) kept.push_back(x);
    if (kept.size() > kMaxPastTrades) {
        std::stable_sort(kept.begin(), kept.end(), [](const json& x, const json& y) { return x.value("finished", 0LL) < y.value("finished", 0LL); });
        kept.erase(kept.begin(), kept.end() - (long)kMaxPastTrades);
    }
    m_pastTrades = kept;
    writeFile(m_opts.data / "pasttrades.json", m_pastTrades.dump(1));
}

void GbServer::loadPastTrades() {
    std::string text;
    if (readFile(m_opts.data / "pasttrades.json", text)) {
        json all = json::parse(text, nullptr, false);
        if (all.is_array())
            for (const json& x : all) if (x.is_object() && x.contains("id") && x["id"].is_string()) m_pastTrades.push_back(x);
    }
    // Past trades started being kept for good with the "Paper Trail" update: the finished
    // trades the server still had then (the last month's) go in too.
    const size_t before = m_pastTrades.size();
    std::set<std::string> have;
    for (const json& x : m_pastTrades) have.insert(x.value("id", std::string()));
    for (const json& x : m_trades)
        if (tradeDone(x.value("status", std::string())) && have.insert(x.value("id", std::string())).second) keepPastTrade(x, true);
    if (m_pastTrades.size() != before) savePastTrades();
}

void GbServer::saveUpdates() { writeFile(m_opts.data / "updates.json", m_updates.dump(1)); }

void GbServer::loadUpdates() {
    std::string text;
    if (!readFile(m_opts.data / "updates.json", text)) return;
    json all = json::parse(text, nullptr, false);
    if (!all.is_array()) return;
    for (const json& u : all) if (u.is_object() && u.contains("id") && u["id"].is_string()) m_updates.push_back(u);
}
