#include "TeamCreate.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../game/Profile.h"
#include "../online/OnlineClient.h"
#include "../core/Log.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <chrono>

using json = nlohmann::json;

namespace {
constexpr uint64_t kIdSpacing = 1ull << 40;   // each member makes ids in their own range

std::string cleanText(std::string s, size_t max) {
    if (s.size() > max) s.resize(max);
    for (char& c : s) if ((unsigned char)c < 32) c = ' ';
    return s;
}
} // namespace

namespace {
double cloudNow() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}
} // namespace

TeamCreate::TeamCreate(Scene* scene) : m_scene(scene) {}
TeamCreate::~TeamCreate() { *m_alive = false; leave(); }

glm::vec3 TeamCreate::colorFor(int id) {
    static const glm::vec3 palette[] = {
        {1.0f, 0.55f, 0.15f}, {0.25f, 0.65f, 1.0f}, {0.35f, 0.85f, 0.4f}, {0.95f, 0.35f, 0.75f},
        {0.95f, 0.85f, 0.2f}, {0.6f, 0.45f, 1.0f},  {0.2f, 0.9f, 0.85f},  {1.0f, 0.35f, 0.35f}};
    return palette[id % 8];
}

TeamCreate::Member* TeamCreate::member(int id) {
    for (auto& m : m_members) if (m.id == id) return &m;
    return nullptr;
}

void TeamCreate::addSystem(const std::string& text) {
    m_chat.push_back({"", text, true});
    Log::system("[Team Create] " + text);
}

bool TeamCreate::host(int port, std::string& error) {
    leave();
    if (!m_listener.open(port, error)) return false;
    m_hosting = true;
    m_myId = 0;
    m_nextId = 1;
    m_members = {{0, Profile::get().name, "", colorFor(0), 0, {}, false}};
    snapshotSynced();
    std::string ips = Net::localAddresses();
    addSystem("Team Create started on port " + std::to_string(port) + ". Others can join with " +
              (ips.empty() ? std::string("your IP address") : ips));
    return true;
}

bool TeamCreate::join(const std::string& address, std::string& error) {
    leave();
    std::string host = address;
    int port = kTeamCreatePort;
    size_t colon = address.rfind(':');
    if (colon != std::string::npos) {
        host = address.substr(0, colon);
        port = std::atoi(address.c_str() + colon + 1);
        if (port <= 0) port = kTeamCreatePort;
    }
    if (host.empty()) host = "127.0.0.1";
    m_server = Net::Connection::connectTo(host, port, error);
    if (!m_server) return false;
    m_server->send(json{{"t", "hello"}, {"name", Profile::get().name}}.dump());
    return true;
}


bool TeamCreate::hostCloud(std::string& error) {
    if (!Online::online() || Online::isGuest()) { error = "Sign in to Guts&Bolts first to host cloud Team Create."; return false; }
    leave();
    m_cloud = true; m_cloudOwner = true; m_cloudReady = false;
    const uint64_t generation = ++m_cloudGeneration;
    m_cloudAccount = Online::me().value("id", std::string());
    if (m_cloudAccount.empty()) { m_cloud = false; error = "Your account session isn't ready."; return false; }
    m_cloudNextPoll = cloudNow();
    const std::string snapshot = Serializer::saveScene(*m_scene);
    std::weak_ptr<bool> alive = m_alive;
    Online::request("team.create", {{"title", m_scene->info().title}, {"snapshot", snapshot}},
        [this, alive, generation](const json& r) {
            auto keep = alive.lock(); if (!keep || !*keep || generation != m_cloudGeneration || !m_cloud) return;
            if (!r.value("ok", false)) { m_error = r.value("error", std::string("Couldn't start cloud Team Create.")); m_cloud = false; return; }
            m_cloudSession = r.value("session", std::string());
            m_cloudCode = r.value("code", std::string());
            m_cloudChat = r.value("chat", std::string());
            m_myId = r.value("member", 0);
            m_cloudSeq = r.value("seq", (uint64_t)0);
            cloudMembers(r.value("members", json::array()));
            m_cloudReady = !m_cloudSession.empty();
            snapshotSynced();
            addSystem("Cloud Team Create is live. Invite code: " + m_cloudCode);
        }, 30);
    return true;
}

bool TeamCreate::joinCloud(const std::string& code, std::string& error) {
    if (!Online::online() || Online::isGuest()) { error = "Sign in to Guts&Bolts first to join cloud Team Create."; return false; }
    const std::string invite = cleanText(code, 16);
    if (invite.size() != 16) { error = "Enter the 16-character invite code."; return false; }
    leave();
    m_cloud = true; m_cloudOwner = false; m_cloudReady = false;
    const uint64_t generation = ++m_cloudGeneration;
    m_cloudAccount = Online::me().value("id", std::string());
    if (m_cloudAccount.empty()) { m_cloud = false; error = "Your account session isn't ready."; return false; }
    std::weak_ptr<bool> alive = m_alive;
    Online::request("team.join", {{"code", invite}},
        [this, alive, generation](const json& r) {
            auto keep = alive.lock(); if (!keep || !*keep || generation != m_cloudGeneration || !m_cloud) return;
            if (!r.value("ok", false)) { m_error = r.value("error", std::string("Couldn't join cloud Team Create.")); m_cloud = false; return; }
            std::string loadError;
            if (!Serializer::loadScene(*m_scene, r.value("snapshot", std::string()), &loadError)) {
                m_error = "Couldn't load the cloud team's game: " + loadError;
                Online::request("team.leave", {{"session", r.value("session", std::string())}});
                m_cloud = false; return;
            }
            m_cloudSession = r.value("session", std::string());
            m_cloudCode = r.value("code", std::string());
            m_cloudChat = r.value("chat", std::string());
            m_myId = r.value("member", 1);
            m_cloudSeq = r.value("seq", (uint64_t)0);
            SceneNode::reserveId(kIdSpacing * (uint64_t)m_myId);
            cloudMembers(r.value("members", json::array()));
            m_cloudReady = !m_cloudSession.empty();
            snapshotSynced();
            m_remoteEdit = true;
            addSystem("Joined cloud Team Create. Changes are shared over the internet.");
        }, 30);
    return true;
}

void TeamCreate::cloudMembers(const json& members) {
    if (!members.is_array()) return;
    std::vector<Member> next;
    for (const auto& m : members) {
        if (!m.is_object()) continue;
        const int id = m.value("id", 0);
        Member member;
        member.id = id;
        member.name = cleanText(m.value("name", std::string("Builder")), 20);
        member.account = m.value("account", std::string());
        member.color = colorFor(m.value("color", id));
        member.selected = m.value("selected", (uint64_t)0);
        if (m.contains("cursor") && m["cursor"].is_object()) {
            const auto& c = m["cursor"];
            member.cursor = {c.value("x", 0.0f), c.value("y", 0.0f), c.value("z", 0.0f)};
            member.cursorActive = true;
        }
        next.push_back(std::move(member));
    }
    m_members = std::move(next);
}

void TeamCreate::cloudPush(const std::string& kind, const std::string& payload) {
    if (!kind.empty()) m_cloudPushQueue.emplace_back(kind, payload);
    if (!m_cloudReady || m_cloudPushPending || m_cloudPushQueue.empty()) return;
    auto item = std::move(m_cloudPushQueue.front());
    m_cloudPushQueue.erase(m_cloudPushQueue.begin());
    m_cloudPushPending = true;
    std::weak_ptr<bool> alive = m_alive;
    Online::request("team.push", {{"session", m_cloudSession}, {"kind", item.first}, {"payload", item.second}},
        [this, alive, generation](const json& r) {
            auto keep = alive.lock(); if (!keep || !*keep || generation != m_cloudGeneration) return;
            m_cloudPushPending = false;
            if (!r.value("ok", false)) m_error = r.value("error", std::string("A Team Create update couldn't be sent."));
            cloudPush("", "");
        });
}

void TeamCreate::updateCloud(bool canApply) {
    if (!m_cloudReady) return;
    const double t = cloudNow();
    if (!waiting() && t >= m_cloudNextPoll && !m_cloudPollPending) {
        m_cloudPollPending = true;
        m_cloudNextPoll = t + 0.5;
        const uint64_t after = m_cloudSeq;
        std::weak_ptr<bool> alive = m_alive;
        json cursor = nullptr;
        if (m_cursorActive) cursor = {{"x", m_cursor.x}, {"y", m_cursor.y}, {"z", m_cursor.z}};
        Online::request("team.poll", {{"session", m_cloudSession}, {"after", after}, {"selected", m_selection}, {"cursor", cursor}},
            [this, alive, generation](const json& r) {
                auto keep = alive.lock(); if (!keep || !*keep || generation != m_cloudGeneration) return;
                m_cloudPollPending = false;
                if (!r.value("ok", false)) { m_error = r.value("error", std::string("Lost the cloud Team Create session.")); return; }
                if (r.contains("members")) cloudMembers(r["members"]);
                if (r.value("resync", false) && r.contains("snapshot")) {
                    std::string loadError;
                    if (Serializer::loadScene(*m_scene, r.value("snapshot", std::string()), &loadError)) {
                        m_cloudSeq = r.value("snapshotSeq", (uint64_t)0);
                        snapshotSynced();
                        m_remoteEdit = true;
                    } else m_error = "Couldn't resync the cloud scene: " + loadError;
                }
                if (r.contains("events") && r["events"].is_array()) for (const auto& e : r["events"]) {
                    const uint64_t seq = e.value("seq", (uint64_t)0);
                    if (seq <= m_cloudSeq) continue;
                    m_cloudSeq = seq;
                    if (e.value("account", std::string()) == m_cloudAccount) continue;
                    const std::string kind = e.value("kind", std::string());
                    const std::string payload = e.value("payload", std::string());
                    if (kind == "ops") m_pending.push_back(payload);
                    else if (kind == "chat") {
                        std::string from = "Builder";
                        for (const auto& mem : m_members) if (mem.account == e.value("account", std::string())) from = mem.name;
                        m_chat.push_back({from, cleanText(payload, 300), false});
                    }
                }
            }, 10);
    }
    if (canApply && !m_pending.empty()) {
        for (const auto& ops : m_pending) applyOps(ops);
        m_pending.clear();
    }
    std::string ops = diffLocal();
    if (!ops.empty()) cloudPush("ops", ops);
    if (m_cloudOwner && canApply && m_pending.empty() && t >= m_cloudNextSnapshot && !m_cloudSnapshotPending) {
        m_cloudSnapshotPending = true;
        m_cloudNextSnapshot = t + 15.0;
        std::weak_ptr<bool> alive = m_alive;
        const std::string snapshot = Serializer::saveScene(*m_scene);
        Online::request("team.snapshot", {{"session", m_cloudSession}, {"snapshot", snapshot}, {"upto", m_cloudSeq}},
            [this, alive, generation](const json& r) {
                auto keep = alive.lock(); if (!keep || !*keep || generation != m_cloudGeneration) return;
                m_cloudSnapshotPending = false;
                if (!r.value("ok", false)) m_error = r.value("error", std::string("Couldn't checkpoint the shared scene."));
            }, 20);
    }
}

void TeamCreate::leave() {
    ++m_cloudGeneration;
    if (m_cloud && m_cloudReady && !m_cloudSession.empty())
        Online::request("team.leave", {{"session", m_cloudSession}});
    m_cloud = m_cloudReady = m_cloudOwner = false;
    m_cloudPollPending = m_cloudPushPending = m_cloudSnapshotPending = false;
    m_cloudSession.clear(); m_cloudCode.clear(); m_cloudChat.clear(); m_cloudAccount.clear();
    m_cloudPushQueue.clear(); m_cloudSeq = 0; m_cloudNextPoll = m_cloudNextSnapshot = 0.0;
    if (m_hosting) {
        for (auto& c : m_clients) {
            c->conn->send(json{{"t", "bye"}}.dump());
            c->conn->poll();
        }
    }
    m_clients.clear();
    m_listener.close();
    m_server.reset();
    if (m_hosting || m_joined) addSystem("Left Team Create.");
    m_hosting = m_joined = false;
    m_members.clear();
    m_pending.clear();
    m_synced.clear();
}

void TeamCreate::send(const std::string& msg, Client* except) {
    if (m_hosting) {
        for (auto& c : m_clients)
            if (c->joined && c.get() != except) c->conn->send(msg);
    } else if (m_server) {
        m_server->send(msg);
    }
}

// ---------------------------------------------------------------------------
// Syncing objects
// ---------------------------------------------------------------------------

void TeamCreate::snapshotSynced() {
    m_synced.clear();
    std::vector<SceneNode*> stack;
    for (auto& c : m_scene->root()->children) stack.push_back(c.get());
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        size_t index = 0;
        for (auto& s : n->parent->children) { if (s.get() == n) break; ++index; }
        m_synced[n->id] = std::to_string(n->parent->id) + "|" + std::to_string(index) + "|" +
                          Serializer::nodeShallowToString(*n);
        for (auto& c : n->children) stack.push_back(c.get());
    }
    m_syncedSettings = Serializer::settingsToString(*m_scene);
}

std::string TeamCreate::diffLocal() {
    json upserts = json::array(), deletes = json::array();
    std::set<uint64_t> seen;

    // Parents before children, so the other side can always find the parent.
    std::vector<SceneNode*> stack;
    auto& top = m_scene->root()->children;
    for (auto it = top.rbegin(); it != top.rend(); ++it) stack.push_back(it->get());
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        seen.insert(n->id);
        size_t index = 0;
        for (auto& s : n->parent->children) { if (s.get() == n) break; ++index; }
        std::string data = Serializer::nodeShallowToString(*n);
        std::string key = std::to_string(n->parent->id) + "|" + std::to_string(index) + "|" + data;
        auto it = m_synced.find(n->id);
        if (it == m_synced.end() || it->second != key) {
            upserts.push_back({{"id", n->id}, {"parent", n->parent->id}, {"index", index}, {"data", data}});
            m_synced[n->id] = std::move(key);
        }
        for (auto c = n->children.rbegin(); c != n->children.rend(); ++c) stack.push_back(c->get());
    }
    for (auto it = m_synced.begin(); it != m_synced.end();) {
        if (!seen.count(it->first)) { deletes.push_back(it->first); it = m_synced.erase(it); }
        else ++it;
    }

    json ops = json::object();
    if (!upserts.empty()) ops["up"] = upserts;
    if (!deletes.empty()) ops["del"] = deletes;
    std::string settings = Serializer::settingsToString(*m_scene);
    if (settings != m_syncedSettings) { ops["settings"] = settings; m_syncedSettings = settings; }
    return ops.empty() ? std::string() : ops.dump();
}

void TeamCreate::applyOps(const std::string& text) {
    json ops = json::parse(text, nullptr, false);
    if (!ops.is_object()) return;

    if (ops.contains("del"))
        for (const auto& d : ops["del"])
            if (SceneNode* n = m_scene->findById(d.get<uint64_t>()))
                if (!m_scene->isProtected(n) && n != m_scene->root()) m_scene->removeNode(n);

    if (ops.contains("up"))
        for (const auto& u : ops["up"]) {
            uint64_t id = u.value("id", (uint64_t)0);
            SceneNode* parent = m_scene->findById(u.value("parent", (uint64_t)0));
            if (!parent) parent = m_scene->root();
            size_t index = u.value("index", (size_t)0);
            std::string data = u.value("data", std::string());

            SceneNode* n = m_scene->findById(id);
            if (!n) {
                auto fresh = Serializer::nodeShallowFromString(data);
                if (!fresh) continue;
                n = m_scene->insert(std::move(fresh), parent);
            } else {
                Serializer::applyNodeShallow(*n, data);
                if (n->parent != parent && n != parent && !n->isAncestorOf(parent)) {
                    auto owned = n->parent->detachChild(n);    // keep the sent (local) transform
                    n = parent->addChild(std::move(owned));
                }
            }
            // Same place among its siblings as on the sender's side.
            auto& kids = n->parent->children;
            auto cur = std::find_if(kids.begin(), kids.end(), [&](auto& p) { return p.get() == n; });
            size_t want = std::min(index, kids.size() - 1);
            size_t have = (size_t)(cur - kids.begin());
            if (have != want) {
                auto owned = std::move(*cur);
                kids.erase(cur);
                kids.insert(kids.begin() + (long)want, std::move(owned));
            }
        }

    if (ops.contains("settings")) Serializer::settingsFromString(*m_scene, ops["settings"].get<std::string>());
    m_scene->markDirty();
    snapshotSynced();          // what we have now is in sync; don't echo it back
    m_remoteEdit = true;
}

void TeamCreate::localChanged() {
    if (!active() || waiting()) return;
    std::string ops = diffLocal();
    if (ops.empty()) return;
    if (m_cloud) cloudPush("ops", ops);
    else send(json{{"t", "ops"}, {"ops", ops}}.dump());
}

void TeamCreate::setSelection(uint64_t id) { m_selection = id; }
void TeamCreate::setCursor(const glm::vec3& world, bool active) { m_cursor = world; m_cursorActive = active; }

void TeamCreate::say(const std::string& text) {
    std::string t = cleanText(text, 300);
    if (t.empty() || !active()) return;
    if (m_cloud) {
        m_chat.push_back({Profile::get().name, t});
        cloudPush("chat", t);
        return;
    }
    if (m_hosting) {
        m_chat.push_back({Profile::get().name, t});
        send(json{{"t", "chat"}, {"from", Profile::get().name}, {"text", t}}.dump());
    } else {
        send(json{{"t", "chat"}, {"text", t}}.dump());
    }
}

// ---------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------

void TeamCreate::handle(const std::string& text, Client* from) {
    json m = json::parse(text, nullptr, false);
    if (!m.is_object()) return;
    std::string t = m.value("t", "");

    if (m_hosting) {
        if (!from) return;
        if (t == "hello" && !from->joined) {
            from->name = cleanText(m.value("name", std::string("Builder")), 20);
            from->joined = true;
            m_members.push_back({from->id, from->name, "", colorFor(from->id), 0, {}, false});
            json members = json::array();
            for (auto& mm : m_members) members.push_back({{"id", mm.id}, {"name", mm.name}, {"sel", mm.selected}});
            from->conn->send(json{{"t", "welcome"}, {"id", from->id}, {"scene", Serializer::saveScene(*m_scene)},
                                  {"members", members}}.dump());
            send(json{{"t", "joined"}, {"id", from->id}, {"name", from->name}}.dump(), from);
            addSystem(from->name + " joined Team Create");
            return;
        }
        if (!from->joined) return;
        if (t == "ops") {
            std::string ops = m.value("ops", std::string());
            send(text, from);                                  // pass it on to everyone else
            m_pending.push_back(ops);
        } else if (t == "sel") {
            uint64_t id = m.value("id", (uint64_t)0);
            if (Member* mm = member(from->id)) mm->selected = id;
            send(json{{"t", "sel"}, {"member", from->id}, {"id", id}}.dump(), from);
        } else if (t == "chat") {
            std::string msg = cleanText(m.value("text", std::string()), 300);
            if (msg.empty()) return;
            m_chat.push_back({from->name, msg});
            send(json{{"t", "chat"}, {"from", from->name}, {"text", msg}}.dump());
        }
        return;
    }

    // --- client side ---
    if (t == "welcome") {
        std::string err;
        if (!Serializer::loadScene(*m_scene, m.value("scene", std::string()), &err)) {
            m_error = "Couldn't load the host's game: " + err;
            m_server.reset();
            return;
        }
        m_myId = m.value("id", 1);
        SceneNode::reserveId(kIdSpacing * (uint64_t)m_myId);
        m_members.clear();
        for (auto& mm : m["members"]) {
            int id = mm.value("id", 0);
            m_members.push_back({id, mm.value("name", std::string()), "", colorFor(id), mm.value("sel", (uint64_t)0), {}, false});
        }
        if (!member(m_myId)) m_members.push_back({m_myId, Profile::get().name, "", colorFor(m_myId), 0, {}, false});
        m_joined = true;
        snapshotSynced();
        m_remoteEdit = true;
        addSystem("Joined Team Create. Everything you change is shared.");
    } else if (t == "ops") {
        m_pending.push_back(m.value("ops", std::string()));
    } else if (t == "joined") {
        int id = m.value("id", 0);
        m_members.push_back({id, m.value("name", std::string()), "", colorFor(id), 0, {}, false});
        addSystem(m.value("name", std::string()) + " joined Team Create");
    } else if (t == "left") {
        int id = m.value("id", 0);
        if (Member* mm = member(id)) addSystem(mm->name + " left Team Create");
        m_members.erase(std::remove_if(m_members.begin(), m_members.end(), [&](auto& x) { return x.id == id; }),
                        m_members.end());
    } else if (t == "sel") {
        if (Member* mm = member(m.value("member", -1))) mm->selected = m.value("id", (uint64_t)0);
    } else if (t == "chat") {
        m_chat.push_back({m.value("from", std::string()), m.value("text", std::string())});
    } else if (t == "bye") {
        m_error = "The host ended Team Create.";
    }
}

void TeamCreate::update(bool canApply) {
    if (m_cloud) { updateCloud(canApply); return; }
    if (m_hosting) {
        while (auto conn = m_listener.accept()) {
            auto c = std::make_unique<Client>();
            c->conn = std::move(conn);
            c->id = m_nextId++;
            m_clients.push_back(std::move(c));
        }
        for (size_t i = 0; i < m_clients.size();) {
            Client& c = *m_clients[i];
            bool ok = c.conn->poll();
            std::string msg;
            while (c.conn->pop(msg)) handle(msg, &c);
            if (!ok || !c.conn->alive()) {
                if (c.joined) {
                    addSystem(c.name + " left Team Create");
                    int id = c.id;
                    m_members.erase(std::remove_if(m_members.begin(), m_members.end(),
                                                   [&](auto& x) { return x.id == id; }), m_members.end());
                    send(json{{"t", "left"}, {"id", id}}.dump(), &c);
                }
                m_clients.erase(m_clients.begin() + (long)i);
                continue;
            }
            ++i;
        }
    } else if (m_server) {
        bool ok = m_server->poll();
        std::string msg;
        while (m_server && m_server->pop(msg)) handle(msg, nullptr);
        if (!m_server || !ok || !m_server->alive()) {
            if (m_error.empty()) m_error = m_joined ? "Lost connection to the Team Create host."
                                                    : "Couldn't join Team Create.";
            addSystem(m_error);
            m_server.reset();
            m_joined = false;
            m_members.clear();
        }
    }

    // Apply other people's edits (unless we're playtesting right now).
    if (canApply && !m_pending.empty()) {
        for (const auto& ops : m_pending) applyOps(ops);
        m_pending.clear();
    }

    // Tell others what we have selected.
    if (active() && !waiting() && m_selection != m_sentSelection) {
        m_sentSelection = m_selection;
        if (Member* me = member(m_myId)) me->selected = m_selection;
        if (m_hosting) send(json{{"t", "sel"}, {"member", m_myId}, {"id", m_selection}}.dump());
        else           send(json{{"t", "sel"}, {"id", m_selection}}.dump());
    }
}
