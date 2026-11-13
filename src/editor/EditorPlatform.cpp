// The manage_game AI tool (Fyxa and other AI apps): look after the published game on the
// Guts&Bolts website from Studio, not just build it. Read things straight away (its page info,
// stats, passes, products, servers, update notes); anything that changes the live game (its
// settings, a pass or product, its price, an update note everyone sees) waits for the PERSON to
// press Allow in Studio's "Fyxa wants to change your game" window. The AI can't press it: a
// confirm flag in the call is not enough, because these changes can't be undone with Undo and
// some of them are about money.
//
// Requests go to the server as the signed-in account (Online::request), with the server's own
// checks, so the AI can never do more than the person could on the website.
// Requests are answered later, so every call gives back a job number; manage_game
// {action: "result", job} says how it went.
#include "Editor.h"
#include "../core/Log.h"
#include "../online/OnlineClient.h"
#include "../scene/Scene.h"

#include <imgui.h>

using json = nlohmann::json;

namespace {
struct PlatformAction {
    const char* name;
    const char* op;        // the server request
    bool        changes;   // needs the person's Allow
};
const PlatformAction kActions[] = {
    {"info", "asset.info", false},
    {"stats", "creator.stats", false},
    {"passes", "pass.list", false},
    {"products", "pass.list", false},
    {"servers", "servers.list", false},
    {"updates", "gamelog.list", false},
    {"settings", "game.settings", true},
    {"post_update", "gamelog.post", true},
    {"make_pass", "pass.create", true},
    {"make_product", "pass.create", true},
    {"edit_pass", "pass.edit", true},
};
const PlatformAction* findAction(const std::string& name) {
    for (const PlatformAction& a : kActions) if (name == a.name) return &a;
    return nullptr;
}
std::string sv(const json& j, const char* k) { return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string(); }

// What the person reads in the Allow window, in plain words.
std::string describeChange(const std::string& action, const json& a) {
    auto price = [&]() {
        const long long p = a.contains("price") && a["price"].is_number() ? a["price"].get<long long>() : 0;
        return p > 0 ? std::to_string(p) + " Bolts" : std::string("free");
    };
    if (action == "make_pass") return "Make a game pass \"" + sv(a, "name") + "\" (" + price() + ")";
    if (action == "make_product") return "Make a developer product \"" + sv(a, "name") + "\" (" + price() + ")";
    if (action == "edit_pass") {
        std::string s = "Change pass/product " + sv(a, "id") + ":";
        if (a.contains("name")) s += " name \"" + sv(a, "name") + "\"";
        if (a.contains("price")) s += " price " + price();
        if (a.contains("offsale")) s += a.value("offsale", false) ? " take it off sale" : " put it on sale";
        if (a.contains("description")) s += " new description";
        return s;
    }
    if (action == "post_update") return "Post an update note everyone can see: \"" + sv(a, "title") + "\"";
    std::string s = "Change the game's settings:";
    for (auto& [k, v] : a.items()) {
        if (k == "id") continue;
        s += "\n  " + k + " = " + (v.is_string() ? "\"" + v.get<std::string>() + "\"" : v.dump());
    }
    return s;
}
} // namespace

AiToolResult Editor::runPlatformTool(const json& args) {
    const std::string name = "manage_game";
    auto done = [&](json d) {
        d["success"] = true; d["operation"] = name;
        AiToolResult r; r.text = d.dump(1); r.data = std::move(d); return r;
    };
    auto fail = [&](const char* code, const std::string& message, const std::string& suggestion) {
        json d = {{"success", false}, {"operation", name},
                  {"error", {{"code", code}, {"message", message}, {"recoverable", true}, {"suggested_action", suggestion}}}};
        AiToolResult r; r.text = d.dump(1); r.error = true; r.data = std::move(d); return r;
    };
    const std::string action = sv(args, "action");

    if (action == "result") {
        const std::string job = args.contains("job") ? (args["job"].is_string() ? args["job"].get<std::string>() : args["job"].dump()) : std::string();
        if (!m_platformJobs.contains(job)) return fail("NOT_FOUND", "There's no job " + job + ".", "Use the job number a manage_game call gave back.");
        json d = m_platformJobs[job];
        d["job"] = job;
        return done(d);
    }
    const PlatformAction* act = findAction(action);
    if (!act) {
        std::string all = "result";
        for (const PlatformAction& a : kActions) all += std::string(", ") + a.name;
        return fail("INVALID_INPUT", "action must be one of: " + all + ".", "Pick an action.");
    }
    if (!Online::configured() || !Online::online() || Online::isGuest())
        return fail("NOT_SIGNED_IN", "Studio isn't signed in to a Guts&Bolts account, so it can't reach the game's page.",
                    "Ask the person to sign in, then try again. Everything happens as the account they signed in with.");
    std::string game = sv(args, "game");
    if (game.empty()) game = m_scene->info().publishedId;
    if (game.empty() && action != "stats" && action != "edit_pass")
        return fail("NOT_PUBLISHED", "This game hasn't been published yet, so it has no page to manage.",
                    "Publish it first (publish_game, only when the person asks), or give `game` (a game id or number).");

    // The request for the server.
    json req = json::object();
    const json& f = args.contains("fields") && args["fields"].is_object() ? args["fields"] : json::object();
    if (action == "info" || action == "updates") req = {{"id", game}};
    else if (action == "passes") req = {{"game", game}};
    else if (action == "products") req = {{"game", game}, {"products", true}};
    else if (action == "servers") req = {{"game", game}};
    else if (action == "settings") {
        static const char* kAllowed[] = {"name", "description", "access", "genres", "maxPlayers", "allowGear", "comments", "privatePrice"};
        req = {{"id", game}};
        for (const char* k : kAllowed) if (f.contains(k)) req[k] = f[k];
        if (req.size() == 1) return fail("INVALID_INPUT", "settings needs `fields` with something to change.",
                                         "Like fields: {description: \"...\", maxPlayers: 20, genres: [\"Obby\"]}.");
    } else if (action == "post_update") {
        req = {{"id", game}, {"title", sv(f, "title")}, {"text", sv(f, "text")}};
        if (sv(f, "title").empty()) return fail("INVALID_INPUT", "post_update needs fields.title (and fields.text).", "Give a short title.");
    } else if (action == "make_pass" || action == "make_product") {
        req = {{"game", game}, {"name", sv(f, "name")}, {"description", sv(f, "description")},
               {"price", f.contains("price") && f["price"].is_number() ? f["price"].get<long long>() : 0}};
        if (action == "make_product") req["product"] = true;
        if (sv(f, "name").empty()) return fail("INVALID_INPUT", "Give fields.name for the " + std::string(action == "make_pass" ? "pass" : "product") + ".", "");
    } else if (action == "edit_pass") {
        req = {{"id", sv(args, "pass")}};
        for (const char* k : {"name", "description", "price", "offsale"}) if (f.contains(k)) req[k] = f[k];
        if (sv(args, "pass").empty()) return fail("INVALID_INPUT", "edit_pass needs `pass` (the pass or product id, from passes / products).", "");
    }

    const std::string job = std::to_string(m_platformNext++);
    json entry = {{"action", action}, {"game", game}};
    if (act->changes && m_fyxaBypass && m_aiSource == "Fyxa") {   // bypass permissions mode: no Allow window
        entry["asked"] = describeChange(action, req);
        m_platformJobs[job] = entry;
        Log::system("Fyxa changed your game (bypass mode, no Allow asked): " + entry["asked"].get<std::string>());
        sendPlatformJob(job, act->op, req);
        return done({{"job", job}, {"state", "sending"}, {"bypass", true},
                     {"note", "Bypass permissions mode is on, so this was sent without asking. Tell the person what you changed. "
                              "The server answers in a moment: manage_game {action: \"result\", job: \"" + job + "\"}."}});
    }
    if (act->changes) {
        entry["state"] = "waiting_for_person";
        entry["asked"] = describeChange(action, req);
        entry["op"] = act->op;
        entry["request"] = req;
        m_platformJobs[job] = entry;
        m_platformWaiting.push_back(job);
        Log::system("Fyxa wants to change your game: " + entry["asked"].get<std::string>() + " (waiting for you to Allow)");
        return done({{"job", job}, {"state", "waiting_for_person"}, {"asked", entry["asked"]},
                     {"note", "Nothing changes until the person presses Allow in Studio. Tell them what you asked for, "
                              "then check with manage_game {action: \"result\", job: \"" + job + "\"}."}});
    }
    entry["state"] = "sending";
    m_platformJobs[job] = entry;
    sendPlatformJob(job, act->op, req);
    return done({{"job", job}, {"state", "sending"},
                 {"note", "The server answers in a moment: manage_game {action: \"result\", job: \"" + job + "\"}."}});
}

void Editor::sendPlatformJob(const std::string& job, const std::string& op, const json& req) {
    m_platformJobs[job]["state"] = "sending";
    const std::string game = m_platformJobs[job].value("game", std::string());
    const std::string action = m_platformJobs[job].value("action", std::string());
    Online::request(op, req, [this, job, game, action](const json& r) {
        if (!m_platformJobs.contains(job)) return;
        json& e = m_platformJobs[job];
        if (!r.value("ok", false)) {
            e["state"] = "failed";
            e["error"] = r.value("error", std::string("The server said no."));
            return;
        }
        json out = r;
        out.erase("ok");
        // creator.stats lists everything you made: keep just this game.
        if (action == "stats" && !game.empty() && out.contains("items") && out["items"].is_array()) {
            json keep = json::array();
            for (const json& it : out["items"]) if (it.value("id", std::string()) == game || std::to_string(it.value("num", 0LL)) == game) keep.push_back(it);
            out["items"] = keep;
        }
        e["state"] = "done";
        e["result"] = out;
    });
}

// "Fyxa wants to change your game": one waiting change at a time, Allow or Deny.
void Editor::renderPlatformApprovals() {
    while (!m_platformWaiting.empty() && (!m_platformJobs.contains(m_platformWaiting.front()) ||
           m_platformJobs[m_platformWaiting.front()].value("state", std::string()) != "waiting_for_person"))
        m_platformWaiting.erase(m_platformWaiting.begin());
    if (m_platformWaiting.empty()) return;
    const std::string job = m_platformWaiting.front();
    json& e = m_platformJobs[job];
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
    if (ImGui::Begin("Fyxa wants to change your game", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextWrapped("The AI asked to change your published game on Guts&Bolts:");
        ImGui::Separator();
        ImGui::TextWrapped("%s", e.value("asked", std::string()).c_str());
        ImGui::Separator();
        ImGui::TextDisabled("This changes the live game for everyone. Undo can't take it back.");
        if (m_platformWaiting.size() > 1) ImGui::TextDisabled("%d more waiting after this one.", (int)m_platformWaiting.size() - 1);
        if (ImGui::Button("Allow")) {
            Log::system("You allowed: " + e.value("asked", std::string()));
            sendPlatformJob(job, e.value("op", std::string()), e.value("request", json::object()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Deny")) {
            e["state"] = "denied";
            e["error"] = "The person said no.";
        }
    }
    ImGui::End();
}
