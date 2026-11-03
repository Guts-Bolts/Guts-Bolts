// The website's chat panel (worker/chats.js has the same): a short summary of each chat for the
// list, and Do Not Disturb. Voice calls and the Lyreto gateway only run on the Cloudflare server
// (they need its live connections and Lyreto's secret), so here they say so.
//
//   chat.inbox   {}      -> {chats: [{id, title, owner, members, updated, unread, last, call, bridge}], invites, dnd, relay, bridge}
//   presence.set {dnd}   -> {me}
#include "Server.h"
#include "ServerUtil.h"
#include "../online/Protocol.h"

#include <algorithm>

using json = nlohmann::json;
using namespace ServerUtil;

json GbServer::chatOp(const std::string& name, User& me, const json& args) {
    if (me.userId == 0) return fail("Sign up first.");
    if (name == "presence.set") {
        if (args.contains("dnd")) me.dnd = args["dnd"] == true;
        saveUsers();
        json r = okay(); r["me"] = meJson(me); return r;
    }
    if (name == "chat.inbox") {
        json chats = json::array();
        if (me.chats.is_array())
            for (const json& c : me.chats) {
                const json members = c.value("members", json::array());
                bool in = false;
                for (const json& m : members) if (m.value("id", std::string()) == me.id) in = true;
                if (!in) continue;
                const json msgs = c.value("messages", json::array());
                long long readAt = c.contains("readAt") && c["readAt"].is_object() ? c["readAt"].value(me.id, 0LL) : 0;
                int unread = 0;
                for (size_t i = msgs.size(); i-- > 0 && unread < 99;) {
                    if (msgs[i].value("at", 0LL) <= readAt) break;
                    if (msgs[i].value("from", std::string()) != me.id) ++unread;
                }
                json list = json::array();
                for (const json& m : members) {
                    json p = {{"id", m.value("id", std::string())}, {"userId", m.value("userId", 0LL)}, {"name", m.value("name", std::string())}};
                    if (p["id"] != me.id)
                        if (const User* u = findUser(p["id"].get<std::string>())) {
                            json pr = presence(me, *u);
                            p["online"] = pr["online"]; p["dnd"] = pr.value("dnd", false);
                        }
                    list.push_back(p);
                }
                json last = nullptr;
                if (!msgs.empty()) {
                    const json& l = msgs.back();
                    last = {{"name", l.value("name", std::string())}, {"body", l.value("body", std::string())}, {"at", l.value("at", 0LL)},
                            {"system", l.value("system", false)}, {"mine", l.value("from", std::string()) == me.id}};
                }
                chats.push_back({{"id", c.value("id", std::string())}, {"title", c.value("title", std::string())}, {"owner", c.value("owner", std::string())},
                                 {"members", list}, {"updated", c.value("updated", c.value("created", 0LL))}, {"unread", unread}, {"last", last},
                                 {"call", nullptr}, {"bridge", nullptr}});
            }
        std::stable_sort(chats.begin(), chats.end(), [](const json& a, const json& b) { return a.value("updated", 0LL) > b.value("updated", 0LL); });
        json r = okay();
        r["chats"] = chats; r["invites"] = me.partyInvites.is_array() ? me.partyInvites : json::array();
        r["dnd"] = me.dnd; r["relay"] = false; r["bridge"] = false;
        return r;
    }
    if (name == "call.poll") { json r = okay(); r["call"] = nullptr; r["signals"] = json::array(); return r; }
    if (name.rfind("call.", 0) == 0) return fail("Voice calls only work on the main Guts&Bolts server (gutsandbolts.net), not on this one.");
    if (name == "chat.bridge" || name == "chat.unbridge") return fail("The Lyreto gateway only runs on the main Guts&Bolts server.");
    return fail("Unknown request.");
}
