#include "NetGame.h"
#include "../scripting/LuaApi.h"   // SignalKind (UI clicks)
#include "../scene/PlayerModel.h"
#include "../online/AssetCache.h"
#include "../core/Paths.h"
#include "../game/Badges.h"
#include "../game/GameSession.h"
#include "../scene/SoundMix.h"
#include "../game/Profile.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include "../scene/Physics.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Log.h"
#include "../core/Audio.h"
#include "../core/Voice.h"
#include "../core/Settings.h"
#include "../online/Protocol.h"
#include "../core/Account.h"
#include "../core/TextFilter.h"
#include "../online/OnlineClient.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <map>
#include <set>

using json = nlohmann::json;

namespace {

// Our Verified / Staff badges, sent along so others can check them.
json myGrants() {
    json g = json::array();
    for (const auto& [k, sig] : Profile::get().grants)
        if (k == "verified" || k == "staff") g.push_back({k, sig});
    return g;
}
std::vector<Badges::Grant> grantsFrom(const json& m) {
    std::vector<Badges::Grant> out;
    if (m.contains("grants") && m["grants"].is_array())
        for (const auto& g : m["grants"])
            if (g.is_array() && g.size() == 2 && g[0].is_string() && g[1].is_string() && out.size() < 8)
                out.push_back({g[0].get<std::string>(), g[1].get<std::string>()});
    return out;
}

constexpr float    kTickRate    = 1.0f / 20.0f;        // network updates per second: 20
constexpr uint64_t kLocalIdBase = 1ull << 40;          // ids clients make for themselves
constexpr int      kVersion     = 2;   // 2: accounts (signed join)

json vec3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
glm::vec3 vec3(const json& j) {
    if (!j.is_array() || j.size() < 3) return glm::vec3(0.0f);
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

json transformJson(const Transform& t) {
    return json::array({t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y,
                        t.rotation.z, t.scale.x, t.scale.y, t.scale.z});
}
Transform transformFrom(const json& j) {
    Transform t;
    if (!j.is_array() || j.size() < 9) return t;
    t.position = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    t.rotation = {j[3].get<float>(), j[4].get<float>(), j[5].get<float>()};
    t.scale    = {j[6].get<float>(), j[7].get<float>(), j[8].get<float>()};
    return t;
}

json poseJson(const CharacterPose& pose) {
    json parts = json::array();
    for (const auto& [name, t] : pose.parts) parts.push_back({name, transformJson(t)});
    return {{"root", transformJson(pose.root)}, {"parts", parts}, {"ff", pose.forceField}};
}
CharacterPose poseFrom(const json& j) {
    CharacterPose pose;
    if (!j.is_object()) return pose;
    pose.root = transformFrom(j.value("root", json()));
    pose.forceField = j.value("ff", false);
    if (j.contains("parts") && j["parts"].is_array())
        for (const auto& p : j["parts"])
            if (p.is_array() && p.size() == 2 && p[0].is_string())
                pose.parts.push_back({p[0].get<std::string>(), transformFrom(p[1])});
    return pose;
}

// Seconds on a steady clock (only differences matter).
double clockNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

float lerpAngle(float a, float b, float t) {
    float d = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;   // the short way round
    return a + d * t;
}
Transform lerpTransform(const Transform& a, const Transform& b, float t) {
    Transform o;
    o.position = glm::mix(a.position, b.position, t);
    o.rotation = {lerpAngle(a.rotation.x, b.rotation.x, t), lerpAngle(a.rotation.y, b.rotation.y, t),
                  lerpAngle(a.rotation.z, b.rotation.z, t)};
    o.scale = glm::mix(a.scale, b.scale, t);
    return o;
}
CharacterPose lerpPose(const CharacterPose& a, const CharacterPose& b, float t) {
    // Teleported (respawned, a script moved them): jump straight there.
    if (glm::length(b.root.position - a.root.position) > 15.0f || a.parts.size() != b.parts.size()) return t < 0.5f ? a : b;
    CharacterPose o = b;
    o.root = lerpTransform(a.root, b.root, t);
    for (size_t i = 0; i < o.parts.size(); ++i)
        if (a.parts[i].first == b.parts[i].first) o.parts[i].second = lerpTransform(a.parts[i].second, b.parts[i].second, t);
    return o;
}

json humanoidJson(const Humanoid& h) {
    return {{"health", h.health}, {"maxHealth", h.maxHealth}, {"walkSpeed", h.walkSpeed},
            {"jumpPower", h.jumpPower}, {"jumpHeight", h.jumpHeight}, {"useJumpPower", h.useJumpPower}};
}
void humanoidFrom(Humanoid& h, const json& j) {
    h.maxHealth = j.value("maxHealth", h.maxHealth);
    h.health    = j.value("health", h.health);
    h.walkSpeed = j.value("walkSpeed", h.walkSpeed);
    h.jumpPower = j.value("jumpPower", h.jumpPower);
    h.jumpHeight = j.value("jumpHeight", h.jumpHeight);
    h.useJumpPower = j.value("useJumpPower", h.useJumpPower);
}

// A player's camera options (CameraMode and zoom limits).
json cameraJson(const CameraOptions& o) {
    return {{"first", o.lockFirstPerson}, {"min", o.minZoom}, {"max", o.maxZoom}};
}
CameraOptions cameraFrom(const json& j) {
    CameraOptions o;
    if (!j.is_object()) return o;
    o.lockFirstPerson = j.value("first", false);
    o.minZoom = std::clamp(j.value("min", o.minZoom), 0.0f, CameraOptions::kLimit);
    o.maxZoom = std::clamp(j.value("max", o.maxZoom), 0.0f, CameraOptions::kLimit);
    return o;
}

json avatarJson(const Profile& p) {
    const BodyColors& c = p.colors;
    return {{"head", vec3(c.head)}, {"torso", vec3(c.torso)}, {"leftArm", vec3(c.leftArm)},
            {"rightArm", vec3(c.rightArm)}, {"leftLeg", vec3(c.leftLeg)}, {"rightLeg", vec3(c.rightLeg)},
            {"hat", (int)p.hat}, {"hatColor", vec3(p.hatColor)},
            {"shirtImage", p.shirtImage}, {"pantsImage", p.pantsImage}, {"tshirtImage", p.tshirtImage},
            {"faceImage", p.faceImage}, {"accessories", p.accessories}};
}

// Everything a joined player needs to see about a (non-character) object.
std::string nodeState(const SceneNode* n) {
    char buf[512];
    const Transform& t = n->transform;
    std::snprintf(buf, sizeof(buf),
        "%llu|%.4f %.4f %.4f|%.3f %.3f %.3f|%.4f %.4f %.4f|%.3f %.3f %.3f|%.3f|%d%d%d|%d|%d|%.3f %.3f %.3f",
        (unsigned long long)(n->parent ? n->parent->id : 0),
        t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y, t.rotation.z,
        t.scale.x, t.scale.y, t.scale.z, n->color.r, n->color.g, n->color.b, n->transparency,
        (int)n->visible, (int)n->canCollide, (int)n->enabled, (int)n->material, (int)n->primitiveType,
        n->brightness, n->range, n->spotAngle);
    if (n->isGui()) return std::string(buf) + Serializer::guiToJson(n->gui).dump();   // text, colours, sizes...
    if (n->isPrompt()) return std::string(buf) + Serializer::promptToJson(n->prompt, n->enabled).dump();
    if (n->isHighlight()) return std::string(buf) + Serializer::highlightToJson(n->highlight, n->enabled).dump();
    if (n->isEffect()) return std::string(buf) + Serializer::effectToJson(n->effect, n->enabled, n->kind == NodeKind::Beam).dump();
    if (n->isSound() || n->isSoundGroup() || n->isSoundEffect()) return std::string(buf) + Serializer::soundToJson(*n).dump();
    return buf;
}

json nodeUpdate(const SceneNode* n) {
    json u = {{"i", n->id}, {"t", transformJson(n->transform)}, {"c", vec3(n->color)},
            {"a", n->transparency}, {"v", n->visible}, {"cc", n->canCollide}, {"e", n->enabled},
            {"m", (int)n->material}, {"sh", (int)n->primitiveType}, {"b", n->brightness},
            {"rg", n->range}, {"sa", n->spotAngle}};
    if (n->isGui()) u["gui"] = Serializer::guiToJson(n->gui);
    if (n->isPrompt()) u["pr"] = Serializer::promptToJson(n->prompt, n->enabled);
    if (n->isHighlight()) u["hl"] = Serializer::highlightToJson(n->highlight, n->enabled);
    if (n->isEffect()) u["fx"] = Serializer::effectToJson(n->effect, n->enabled, n->kind == NodeKind::Beam);
    if (n->isSound() || n->isSoundGroup() || n->isSoundEffect()) u["snd"] = Serializer::soundToJson(*n);
    return u;
}

void applyUpdate(SceneNode* n, const json& u) {
    n->transform    = transformFrom(u.value("t", json()));
    n->color        = vec3(u.value("c", json()));
    n->transparency = u.value("a", 0.0f);
    n->visible      = u.value("v", true);
    n->canCollide   = u.value("cc", true);
    n->enabled      = u.value("e", true);
    n->material     = (Material)std::clamp(u.value("m", 0), 0, kMaterialCount - 1);
    auto shape = (PrimitiveType)u.value("sh", (int)n->primitiveType);
    if (shape != n->primitiveType) {
        if (shape == PrimitiveType::Mesh) PlayerModel::apply(*n);   // (only the character's shapes change like this)
        else { n->primitiveType = shape; n->mesh = MeshLibrary::get(shape); }
    }
    n->brightness   = u.value("b", n->brightness);
    n->range        = u.value("rg", n->range);
    n->spotAngle    = u.value("sa", n->spotAngle);
    if (n->isGui() && u.contains("gui") && u["gui"].is_object()) {
        const glm::vec2 absPos = n->gui.absPos, absSize = n->gui.absSize;   // (worked out here, not sent)
        Serializer::guiFromJson(n->gui, u["gui"]);
        n->gui.absPos = absPos; n->gui.absSize = absSize;
    }
    if (n->isPrompt() && u.contains("pr") && u["pr"].is_object()) Serializer::promptFromJson(u["pr"], n->prompt, n->enabled);
    if (n->isHighlight() && u.contains("hl") && u["hl"].is_object()) Serializer::highlightFromJson(u["hl"], n->highlight, n->enabled);
    if (n->isEffect() && u.contains("fx") && u["fx"].is_object()) Serializer::effectFromJson(u["fx"], n->effect, n->enabled);
    if (u.contains("snd") && u["snd"].is_object()) Serializer::soundFromJson(*n, u["snd"]);
}

glm::vec3 spawnPoint(Scene& scene) {
    if (SceneNode* s = scene.root()->findChild("SpawnLocation", true)) {
        AABB b = Physics::worldBounds(s);
        return {(b.min.x + b.max.x) * 0.5f, b.max.y + 0.001f, (b.min.z + b.max.z) * 0.5f};
    }
    return scene.player() ? scene.player()->spawn() : glm::vec3(0.0f);
}

void replayFx(Scene& scene, const json& list) {
    if (!list.is_array()) return;
    ParticleSystem& ps = scene.particles();
    for (const auto& f : list) {
        int type = f.value("k", 0);
        glm::vec3 p = vec3(f.value("p", json()));
        float amount = f.value("n", 10.0f);
        switch ((FxEvent::Type)type) {
            case FxEvent::Explosion: {
                // A blast (Blast.h): shown here; the host already moved things and hurt people.
                const std::string opts = f.value("s", std::string());
                if (opts.empty()) { ps.explosion(p, amount); Audio::play("explosion", 1.0f, 1.0f, false, &p); break; }
                BlastOptions o = BlastOptions::decode(opts, amount);
                if (o.visible) ps.sparks(p, (int)std::clamp(o.radius * 4.0f, 10.0f, 80.0f));
                scene.blasts().start(scene, p, o, false);
                break;
            }
            case FxEvent::Sparks:    ps.sparks(p, (int)amount); break;
            case FxEvent::Blood:     if (scene.goreEnabled()) ps.spray(GoreKind::Blood, p, {0, 1, 0}, (int)amount, 3.0f); break;
            case FxEvent::Oil:       if (scene.goreEnabled()) ps.spray(GoreKind::Oil, p, {0, 1, 0}, (int)amount, 3.0f); break;
            case FxEvent::Gibs:      if (scene.goreEnabled()) ps.gibs(scene.goreKind(), p, {0, 2, 0}, (int)amount); break;
            case FxEvent::Sound: {
                std::string name = f.value("s", std::string());
                // From a Sound object: its pitch, effects and SoundGroup (as the host has them).
                SceneNode* node = f.contains("id") ? scene.findById(f.value("id", (uint64_t)0)) : nullptr;
                if (node && !node->isSound()) node = nullptr;
                const float pitch = node ? node->pitch : 1.0f;
                const int h = f.value("pos3d", true) ? Audio::play(name, amount, pitch, false, &p) : Audio::play(name, amount, pitch);
                if (node && h) {
                    SoundMix::apply(scene, node, h);
                    Audio::setVolume(h, amount);   // (the host already worked out how loud)
                    node->audioHandle = h;         // (so its group's effects stay up to date while it plays)
                }
                break;
            }
        }
    }
}

std::string cleanText(std::string s, size_t max) {
    if (s.size() > max) s.resize(max);
    for (char& c : s) if ((unsigned char)c < 32) c = ' ';
    return s;
}

} // namespace

// ===========================================================================
// Smooth movement of other players
// ===========================================================================

void PoseBuffer::push(double sentAt, double now, const CharacterPose& pose) {
    if (!m_snaps.empty() && sentAt <= m_snaps.back().t) return;   // old or repeated
    // How far our clock is ahead of theirs. The quickest message tells us best
    // (slow ones were held up on the way); drift slowly in case clocks wander.
    double off = now - sentAt;
    if (!m_haveOffset || off < m_offset) { m_offset = off; m_haveOffset = true; }
    else m_offset += (off - m_offset) * 0.01;
    // How bumpy the connection is: how much later than the quickest this one came.
    m_jitter = std::max(off - m_offset, m_jitter * 0.99);
    m_snaps.push_back({sentAt, pose});
    while (m_snaps.size() > 2 && m_snaps.front().t < sentAt - 1.0) m_snaps.erase(m_snaps.begin());
}

double PoseBuffer::delay() const { return std::clamp(m_jitter + 0.06, kDelay, kMaxDelay); }

bool PoseBuffer::sample(double now, CharacterPose& out) const {
    if (m_snaps.empty()) return false;
    // Clock difference plus the delay, changed gently: their movement plays at most
    // 8% faster or slower while it adjusts, which nobody notices (a jump they would).
    const double want = m_offset + delay();
    if (m_lastSample < 0.0 || std::abs(want - m_lag) > 1.0) m_lag = want;
    else {
        double step = std::clamp(now - m_lastSample, 0.0, 0.1) * 0.08;
        m_lag = std::clamp(want, m_lag - step, m_lag + step);
    }
    m_lastSample = now;
    const double target = now - m_lag;   // in the sender's time
    if (target <= m_snaps.front().t || m_snaps.size() == 1) { out = m_snaps.front().t >= target ? m_snaps.front().pose : m_snaps.back().pose; return true; }
    for (size_t i = 1; i < m_snaps.size(); ++i) {
        const Snap& a = m_snaps[i - 1];
        const Snap& b = m_snaps[i];
        if (target <= b.t) {
            out = lerpPose(a.pose, b.pose, (float)((target - a.t) / std::max(1e-6, b.t - a.t)));
            return true;
        }
    }
    // Past the newest pose (a late update): keep going the same way for a moment.
    const Snap& a = m_snaps[m_snaps.size() - 2];
    const Snap& b = m_snaps.back();
    double ahead = std::min(target - b.t, 0.15);
    out = lerpPose(a.pose, b.pose, (float)(1.0 + ahead / std::max(1e-6, b.t - a.t)));
    return true;
}

// Put every other player where their buffer says they are right now.
void showSmoothly(Scene& scene, std::unordered_map<uint64_t, PoseBuffer>& poses) {
    const double now = clockNow();
    for (auto it = poses.begin(); it != poses.end();) {
        SceneNode* rig = scene.findById(it->first);
        if (!rig) { it = poses.erase(it); continue; }   // they left
        CharacterPose pose;
        if (it->second.sample(now, pose)) Player::applyPose(rig, pose);
        ++it;
    }
}

// ===========================================================================
// Chat
// ===========================================================================

// What players say goes through the text filter on the way in ("[ Content Deleted ]") (the host filters it too,
// this catches a host that doesn't).
void ChatLog::add(const std::string& from, const std::string& said, bool system, bool admin, bool verified) {
    const std::string text = system ? said : TextFilter::filter(said);
    lines.push_back({from, text, system, admin, verified});
    if (lines.size() > 100) lines.erase(lines.begin());
    if (!system) bubbles[from] = {text, 6.0f};
}

void ChatLog::addWhisper(const std::string& from, const std::string& to, const std::string& said, bool admin,
                         bool verified) {
    Line l{from, TextFilter::filter(said), false, admin, verified};
    l.whisper = true;
    l.to = to;
    lines.push_back(l);
    if (lines.size() > 100) lines.erase(lines.begin());
}

bool ChatLog::parseWhisper(const std::string& text, std::string& to, std::string& message) {
    std::string lower = text;
    for (char& ch : lower) ch = (char)std::tolower((unsigned char)ch);
    size_t skip = 0;
    for (const char* p : {"/whisper ", "/w "})
        if (lower.rfind(p, 0) == 0) { skip = std::strlen(p); break; }
    if (!skip) { to.clear(); message.clear(); return lower == "/w" || lower == "/whisper"; }
    size_t start = text.find_first_not_of(' ', skip);
    if (start == std::string::npos) { to.clear(); message.clear(); return true; }
    size_t end = text.find(' ', start);
    to = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
    size_t rest = end == std::string::npos ? std::string::npos : text.find_first_not_of(' ', end);
    message = rest == std::string::npos ? std::string() : text.substr(rest);
    return true;
}

void ChatLog::update(float dt) {
    for (auto it = bubbles.begin(); it != bubbles.end();) {
        it->second.second -= dt;
        if (it->second.second <= 0.0f) it = bubbles.erase(it);
        else ++it;
    }
}

// ===========================================================================
// Server (host)
// ===========================================================================

namespace {
// The server's Script code stays on the server: players get the Script objects but
// not what's written in them. They keep LocalScripts (they run them) and ModuleScripts
// a LocalScript might require (unless they're in ServerStorage / ServerScriptService).
// (Safe for picking a new host: that reloads the published game, code and all.)
bool serverOnlyCode(const SceneNode* n) {
    if (!n->isScript() || n->isLocal) return false;
    if (!n->isModule) return true;
    for (const SceneNode* p = n->parent; p; p = p->parent)
        if (p->name == "ServerStorage" || p->name == "ServerScriptService") return true;
    return false;
}
struct HideServerCode {
    std::vector<std::pair<SceneNode*, std::string>> kept;
    explicit HideServerCode(SceneNode* root) {
        std::vector<SceneNode*> stack{root};
        while (!stack.empty()) {
            SceneNode* n = stack.back(); stack.pop_back();
            if (serverOnlyCode(n) && !n->source.empty()) kept.emplace_back(n, std::move(n->source)), n->source.clear();
            for (auto& c : n->children) stack.push_back(c.get());
        }
    }
    ~HideServerCode() { for (auto& [n, src] : kept) n->source = std::move(src); }
};

// A tool as it is, without where it sits right now (held tools move every frame).
std::string toolString(SceneNode* t) {
    const Transform keep = t->transform;
    t->transform = Transform{};
    std::string s;
    { HideServerCode hide(t); s = Serializer::nodeToString(*t); }
    t->transform = keep;
    return s;
}
// Highlights, Trails and Beams inside a character. Characters travel as poses (their
// ids differ on each computer), so these go on their own: the object, where it sits
// and what it points at, as paths of names from the character ("" = the character,
// "Head", "Head/HatAttachment"...), or "w": an id out in the world.
std::string pathIn(const SceneNode* root, const SceneNode* n) {
    std::string path;
    for (const SceneNode* p = n; p && p != root; p = p->parent) path = p->name + (path.empty() ? "" : "/" + path);
    return path;
}
SceneNode* findPath(SceneNode* root, const std::string& path) {
    SceneNode* on = root;
    for (size_t at = 0; on && !path.empty() && at <= path.size();) {
        size_t slash = path.find('/', at);
        if (slash == std::string::npos) slash = path.size();
        on = on->findChild(path.substr(at, slash - at));
        at = slash + 1;
    }
    return on;
}
std::string charEffects(Scene& scene, SceneNode* root) {
    json list = json::array();
    auto ref = [&](json& e, const char* inside, const char* world, uint64_t id) {
        SceneNode* to = id ? scene.findById(id) : nullptr;
        if (!to) return;
        if (to == root || root->isAncestorOf(to)) e[inside] = pathIn(root, to);
        else e[world] = to->id;
    };
    std::vector<SceneNode*> stack{root};
    while (!stack.empty() && list.size() < 16) {
        SceneNode* n = stack.back();
        stack.pop_back();
        for (auto& c : n->children) {
            if (!c->isHighlight() && !c->isEffect()) { stack.push_back(c.get()); continue; }
            json e = {{"in", pathIn(root, n)}, {"n", Serializer::nodeToString(*c)}};
            if (c->isHighlight()) ref(e, "a", "w", c->highlight.adornee);
            else { ref(e, "a0", "w0", c->effect.a0); ref(e, "a1", "w1", c->effect.a1); }
            list.push_back(e);
        }
    }
    return list.empty() ? std::string() : list.dump();
}
} // namespace

struct NetServer::Client {
    std::unique_ptr<Net::Connection> conn;
    int         id = 0;
    std::string name;
    uint64_t    rootId = 0;
    bool        joined = false;
    std::string nonce;            // they sign this to prove which account they are
    std::string accountId;        // "" if they couldn't prove it
    bool        admin = false;
    bool        verified = false;
    bool        guest = false;       // no account: can play but not chat
    std::set<uint64_t> knownChars;   // rigs this client already has
    bool        owner = false;       // owns the game: sees the server's dev console
    unsigned long long logSent = 0;  // Log::count() already sent to them
    std::string toolsSent;           // the tools they carry, as last sent to them
    std::string cameraSent;          // their camera options (player.CameraMode...), as last sent to them
    std::map<uint64_t, std::string> heldSent;   // what each other character holds, as last sent
    std::map<uint64_t, std::string> fxSent;     // the Highlights / Trails / Beams on each character, as last sent
    double      remoteBudget = 60.0;  // RemoteEvent messages they may still send (refills over time)
    double      remoteAt = 0.0;
    bool        voiceOn = false;      // they turned voice chat on (they hear others and may talk)
    double      voiceBudget = 40.0;   // voice packets they may still send (about 17 a second while talking)
    double      voiceAt = 0.0;
};

NetServer::NetServer(Scene* scene, GameSession* session) : m_scene(scene), m_session(session) {}
NetServer::~NetServer() { stop(); }

bool NetServer::start(int port, std::string& error) {
    if (!m_listener.open(port, error)) return false;
    m_port = port;
    m_scene->recordFx = true;
    m_session->setRole(GameSession::Role::Host);
    // Local network only: this address is for people on the same Wi-Fi. (Online, the relay is used and no addresses are shared.)
    m_chat.add("", "Local network server on port " + std::to_string(port) + ". People on the same Wi-Fi can join with " +
               (Net::localAddresses().empty() ? std::string("this computer's local address") : Net::localAddresses()), true);
    return true;
}

bool NetServer::startRelay(const std::string& server, int port, const std::string& hostRequest, std::string& error) {
    m_control = Net::Connection::connectTo(server, port, error, 5000);
    if (!m_control) return false;
    m_control->send(hostRequest);
    m_relayServer = server;
    m_relayPort = port;
    m_scene->recordFx = true;
    m_session->setRole(GameSession::Role::Host);
    return true;
}

void NetServer::stop() {
    for (auto& c : m_clients) {
        c->conn->send(json{{"t", "bye"}, {"reason", "The host closed the game."}}.dump());
        c->conn->poll();
    }
    m_clients.clear();
    m_listener.close();
    m_control.reset();
    m_sessionId.clear();
    m_code.clear();
    m_sent.clear();
    m_scene->remotes().clear();
    m_scene->recordFx = false;
    m_scene->fxQueue.clear();
    m_session->setRole(GameSession::Role::Solo);
    Voice::forgetAll();
}

namespace {
json statsJson(const std::vector<std::pair<std::string, std::string>>& stats) {
    json a = json::array();
    for (auto& [k, v] : stats) a.push_back({k, v});
    return a;
}
std::vector<std::pair<std::string, std::string>> statsFrom(const json& j) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!j.is_array()) return out;
    for (const auto& e : j)
        if (e.is_array() && e.size() == 2 && e[0].is_string() && e[1].is_string() && out.size() < 4)
            out.push_back({e[0].get<std::string>(), e[1].get<std::string>()});
    return out;
}
} // namespace

std::vector<PlayerEntry> NetServer::players() const {
    std::vector<PlayerEntry> out;
    ScriptEngine& s = m_session->scripts();
    if (!m_dedicated)
        out.push_back({Online::playerName() + " (host)", Account::iAmStaff(), Badges::iHave(Badges::Id::Verified),
                       s.leaderstats(Online::playerName()), iAmOwner()});
    for (auto& c : m_clients)
        if (c->joined) out.push_back({c->name, c->admin, c->verified, s.leaderstats(c->name), c->owner});
    return out;
}

int NetServer::playerCount() const {
    int n = 0;
    for (auto& c : m_clients) if (c->joined) ++n;
    return n;
}

void NetServer::broadcast(const std::string& msg, const Client* except) {
    for (auto& c : m_clients)
        if (c->joined && c.get() != except) c->conn->send(msg);
}

void NetServer::announce(const std::string& text) {
    m_chat.add("", text, true);
    broadcast(json{{"t", "chat"}, {"from", ""}, {"text", text}, {"sys", true}}.dump(), nullptr);
}

void NetServer::say(const std::string& text) {
    std::string t = TextFilter::filter(cleanText(text, 200));
    if (t.empty()) return;
    if (Online::isGuest() && Online::online()) { m_chat.add("", Online::kGuestChatText, true); return; }
    bool admin = Account::iAmStaff(), ver = Badges::iHave(Badges::Id::Verified);
    std::string to, msg;
    if (ChatLog::parseWhisper(t, to, msg)) {
        if (to.empty() || msg.empty()) { m_chat.add("", ChatLog::kWhisperHelp, true); return; }
        Client* target = findClient(to);
        if (!target) {
            m_chat.add("", sameName(to, Online::playerName()) ? "You can't whisper to yourself."
                                                             : "There's no player called " + to + " in this game.", true);
            return;
        }
        target->conn->send(json{{"t", "chat"}, {"from", Online::playerName()}, {"to", target->name}, {"text", msg},
                                {"wh", true}, {"adm", admin}, {"ver", ver}}.dump());
        m_chat.addWhisper(Online::playerName(), target->name, msg, admin, ver);
        return;
    }
    m_chat.add(Online::playerName(), t, false, admin, ver);
    broadcast(json{{"t", "chat"}, {"from", Online::playerName()}, {"text", t}, {"adm", admin}, {"ver", ver}}.dump());
}

NetServer::Client* NetServer::findClient(const std::string& name) {
    // Exact name first (any capitals), then the only name that starts with it.
    Client* prefix = nullptr;
    int matches = 0;
    for (auto& c : m_clients) {
        if (!c->joined) continue;
        if (sameName(c->name, name)) return c.get();
        if (c->name.size() > name.size() && sameName(c->name.substr(0, name.size()), name)) { prefix = c.get(); ++matches; }
    }
    return matches == 1 ? prefix : nullptr;
}

bool NetServer::sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

void NetServer::dropClient(size_t index, const char* reason) {
    Client& c = *m_clients[index];
    if (c.joined) {
        if (SceneNode* rig = m_scene->findById(c.rootId)) {
            m_session->scripts().stopScripts(rig);   // (the tools they carried)
            m_scene->removeNode(rig);
        }
        auto& rem = m_scene->remotes();
        rem.erase(std::remove_if(rem.begin(), rem.end(), [&](auto& r) { return r.rootId == c.rootId; }), rem.end());
        m_session->scripts().removePlayer(c.name);
        for (auto& o : m_clients) o->heldSent.erase(c.rootId);
        m_chat.add("", c.name + " " + reason, true);
        Voice::forget(c.name);
        broadcast(json{{"t", "left"}, {"id", c.rootId}, {"name", c.name}}.dump(), &c);
        broadcast(json{{"t", "chat"}, {"from", ""}, {"text", c.name + " " + reason}, {"sys", true}}.dump(), &c);
    }
    m_clients.erase(m_clients.begin() + (long)index);
}

void NetServer::addClient(std::unique_ptr<Net::Connection> conn) {
    auto c = std::make_unique<Client>();
    c->conn = std::move(conn);
    c->id = m_nextId++;
    c->nonce = Account::randomHex(16);
    c->conn->send(json{{"t", "challenge"}, {"nonce", c->nonce}, {"version", kVersion}}.dump());
    m_clients.push_back(std::move(c));
}

void NetServer::updateRelay(float dt) {
    bool ok = m_control->poll();
    std::string msg;
    while (m_control && m_control->pop(msg)) {
        json m = json::parse(msg, nullptr, false);
        if (!m.is_object()) continue;
        std::string t = m.value("t", "");
        if (t == "relay") {
            if (!m.value("ok", false)) {
                m_relayError = m.value("error", std::string("The Guts&Bolts server wouldn't start this server."));
                m_chat.add("", m_relayError + " You're playing alone.", true);
                m_control.reset();
                return;
            }
            m_sessionId = m.value("session", std::string());
            m_code = m.value("code", std::string());
            std::printf("RELAY server %s%s%s\n", m_sessionId.c_str(), m_code.empty() ? "" : " code ", m_code.c_str());
            std::fflush(stdout);
            m_chat.add("", m_code.empty() ? "Public server: anyone can join by pressing Play."
                                          : "Private server. Friends can join from their Friends list, or anyone with the code " + m_code + ".",
                       true);
        } else if (t == "incoming") {
            // Someone's joining: open a fresh line to the server for them. We only
            // ever talk to the server, so we never learn their address (nor they ours).
            std::string err;
            if (m.value("guest", false)) m_guestAccounts.insert(m.value("account", std::string()));   // (the server says so)
            if (auto conn = Net::Connection::connectTo(m_relayServer, m_relayPort, err, 3000)) {
                conn->send(json{{"t", "accept"}, {"ticket", m.value("ticket", std::string())}}.dump());
                addClient(std::move(conn));
            }
        }
    }
    if (!m_control) return;
    if (!ok || !m_control->alive()) {
        m_relayError = "Lost the connection to the Guts&Bolts server, so nobody else can join.";
        m_chat.add("", m_relayError, true);
        m_control.reset();
        return;
    }
    // "Still here" every 20 real seconds (not game time: a slow computer mustn't lose its server).
    (void)dt;
    double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now >= m_pingAt) {
        m_pingAt = now + 20.0;
        m_control->send(json{{"t", "ping"}}.dump());
    }
}

void NetServer::update(float dt) {
    if (!m_listener.isOpen() && !m_control && m_clients.empty()) return;
    while (m_listener.isOpen())
        if (auto conn = m_listener.accept()) addClient(std::move(conn));
        else break;
    if (m_control) updateRelay(dt);
    for (size_t i = 0; i < m_clients.size();) {
        Client& c = *m_clients[i];
        bool ok = c.conn->poll();
        std::string msg;
        while (c.conn->pop(msg)) handle(c, msg);
        if (!ok || !c.conn->alive()) { dropClient(i, "left the game"); continue; }
        ++i;
    }
    // What the scripts are sending players (FireClient / FireAllClients / answers).
    for (auto& out : m_session->scripts().takeRemoteOut()) {
        const std::string msg = out.msg.dump();
        for (auto& c : m_clients)
            if (c->joined && (out.to == 0 || out.to == c->id + 1)) c->conn->send(msg);
    }
    showSmoothly(*m_scene, m_poses);
    updateVoice(clockNow());

    m_tick += dt;
    if (m_tick >= kTickRate) {
        m_tick = std::min(m_tick - kTickRate, kTickRate);   // keep an even beat (don't lose the leftover)
        sendTick();
    }
    m_chat.update(dt);
}

// ---------------------------------------------------------------------------
// Voice chat. Packets come to us (the host) and we pass them to everyone near
// enough who has voice chat on. We check the game allows it and the speaker
// has an account, like the text chat; muting happens on each listener's side.
// ---------------------------------------------------------------------------
namespace {
constexpr float  kVoiceRange = 100.0f;      // studs: further than this, voices aren't sent at all
constexpr size_t kVoiceMaxBytes = 2400;     // one packet (60 ms) as base64: far more than Opus needs
constexpr float  kHeadHeight = 1.5f;        // a voice comes from the head, not the middle of the body

glm::vec3 headOf(Scene& scene, uint64_t rootId, bool& found) {
    SceneNode* rig = rootId ? scene.findById(rootId) : nullptr;
    found = rig != nullptr;
    if (!rig) return glm::vec3(0.0f);
    return glm::vec3(rig->worldMatrix()[3]) + glm::vec3(0.0f, kHeadHeight, 0.0f);
}

// Our own setting says we hear voices (and may talk), and we're allowed to.
bool iUseVoice() { return GraphicsSettings::get().voiceChat && !(Online::isGuest() && Online::online()); }
}

bool NetServer::voiceAllowed() const { return m_scene->world().voiceChat; }

void NetServer::passVoice(const std::string& name, uint64_t rootId, const std::string& data, const Client* from) {
    bool found = false;
    const glm::vec3 at = headOf(*m_scene, rootId, found);
    const std::string msg = json{{"t", "voice"}, {"id", rootId}, {"n", name}, {"d", data}}.dump();
    for (auto& c : m_clients) {
        if (!c->joined || !c->voiceOn || c.get() == from) continue;
        bool theirs = false;
        const glm::vec3 them = headOf(*m_scene, c->rootId, theirs);
        if (found && theirs && glm::length(them - at) > kVoiceRange) continue;
        c->conn->send(msg);
    }
    // We hear them too (a game server machine has nobody listening).
    if (from && !m_dedicated && iUseVoice()) {
        std::string bytes;
        if (Online::base64Decode(data, bytes)) Voice::receive(name, bytes, at);
    }
}

void NetServer::updateVoice(double) {
    const bool allowed = voiceAllowed();
    if (m_voiceSent != (int)allowed) {   // the game turned voice on or off (a script, or just starting)
        m_voiceSent = (int)allowed;
        broadcast(json{{"t", "voicecfg"}, {"on", allowed}}.dump());
        if (!allowed) Voice::forgetAll();
    }
    std::string packet;
    while (Voice::takePacket(packet)) {   // what we (the host) said
        Player* me = m_scene->player();
        if (!allowed || m_dedicated || !iUseVoice() || !me || !me->root()) continue;
        passVoice(Online::playerName(), me->rootId(), Online::base64Encode(packet), nullptr);
    }
}

void NetServer::handle(Client& c, const std::string& text) {
    json m = json::parse(text, nullptr, false);
    if (!m.is_object()) return;
    std::string t = m.value("t", "");

    if (t == "hello" && !c.joined) {
        // Pick a unique name.
        // Who are they? They signed our random challenge with their account key.
        std::string accountId = m.value("id", std::string());
        if (Account::verify(accountId, "gb-join:" + c.nonce, m.value("sig", std::string())))
            c.accountId = accountId;
        c.admin = !c.accountId.empty() && Account::isOfficial(c.accountId);
        c.verified = !c.accountId.empty() && Badges::has(c.accountId, grantsFrom(m), Badges::Id::Verified);
        c.guest = m.value("guest", false) || (!c.accountId.empty() && m_guestAccounts.count(c.accountId));

        std::string base = cleanText(m.value("name", std::string("Player")), 20);
        if (base.empty()) base = "Player";
        if ((Account::nameIsReserved(base) && !c.admin) || TextFilter::changes(base)) base = "Player";   // only the real Guts is Guts
        std::string name = base;
        auto taken = [&](const std::string& n) {
            if (!m_dedicated && n == Online::playerName()) return true;
            for (auto& o : m_clients) if (o.get() != &c && o->joined && o->name == n) return true;
            return false;
        };
        for (int k = 2; taken(name); ++k) name = base + std::to_string(k);
        c.name = name;

        // Build their character here, dressed in their avatar.
        SceneNode* rig = Player::buildRig(*m_scene, name, spawnPoint(*m_scene));
        if (m.contains("avatar")) {
            const json& a = m["avatar"];
            BodyColors col{vec3(a.value("head", json())), vec3(a.value("torso", json())),
                           vec3(a.value("leftArm", json())), vec3(a.value("rightArm", json())),
                           vec3(a.value("leftLeg", json())), vec3(a.value("rightLeg", json()))};
            Player::applyColors(rig, col);
            glm::vec3 tint = a.contains("hatColor") ? vec3(a["hatColor"]) : glm::vec3(-1.0f);
            Player::applyHat(*m_scene, rig, (HatStyle)std::clamp(a.value("hat", 0), 0, kHatStyleCount - 1), tint);
            auto cloth = [&](const char* k) {   // only server clothing pictures ("gb:<id>")
                std::string s = a.value(k, std::string());
                return s.rfind("gb:", 0) == 0 && s.size() < 64 ? s : std::string();
            };
            Player::applyClothing(rig, cloth("shirtImage"), cloth("pantsImage"), cloth("tshirtImage"));
            Online::fetchSounds(*m_scene);   // download their clothing pictures
            // Their accessories and face: download them, then dress the rig (if they're still here).
            Player::Accessories acc;
            if (a.contains("accessories") && a["accessories"].is_object())
                for (auto& [k, v] : a["accessories"].items())
                    if (v.is_string() && v.get<std::string>().rfind("gb:", 0) == 0 && v.get<std::string>().size() < 64)
                        acc[k] = v.get<std::string>();
            const std::string face = cloth("faceImage");
            const uint64_t rigId = rig->id;
            auto dress = [this, rigId, acc, face]() {
                if (SceneNode* r = m_scene->findById(rigId)) {
                    Player::applyAccessories(*m_scene, r, acc);
                    Player::applyFace(*m_scene, r, face);
                }
            };
            dress();
            std::vector<std::string> ids;
            for (auto& [k, v] : acc) ids.push_back(v.substr(3));
            if (!face.empty()) ids.push_back(face.substr(3));
            for (const std::string& id : ids)
                if (Paths::downloaded(id).empty()) Online::download(id, [dress](bool ok, const std::filesystem::path&, const json&) { if (ok) dress(); });
        }
        c.rootId = rig->id;
        RemoteCharacter rc;
        rc.clientId = c.id;
        rc.name = name;
        rc.rootId = rig->id;
        if (Player* host = m_scene->player()) {
            rc.humanoid = host->humanoid();
            rc.humanoid.health = rc.humanoid.maxHealth;
        }
        m_scene->remotes().push_back(rc);
        c.joined = true;
        c.knownChars.insert(rig->id);

        json players = json::array();
        for (auto& e : this->players()) players.push_back({{"n", e.name}, {"a", e.admin}, {"v", e.verified}, {"c", e.creator}});
        // Prove who *we* are too, by signing the joiner's challenge.
        json proof = {{"id", Account::id()}, {"sig", Account::sign("gb-host:" + m.value("cnonce", std::string()))},
                      {"grants", myGrants()}};
        Player* hostPlayer = m_scene->player();
        const uint64_t hostChar = hostPlayer && hostPlayer->root() ? hostPlayer->rootId() : 0;   // 0: a game server machine (nobody)
        std::string scene;
        { HideServerCode hide(m_scene->root()); scene = Serializer::saveScene(*m_scene); }
        c.conn->send(json{{"t", "welcome"}, {"version", kVersion}, {"you", rig->id}, {"name", name}, {"host", proof},
                          {"hostChar", hostChar}, {"uid", c.id + 1}, {"voice", voiceAllowed()},
                          {"scene", scene},
                          {"humanoid", humanoidJson(rc.humanoid)}, {"players", players}}.dump());
        // Everyone already in the scene snapshot counts as known.
        if (Player* host = m_scene->player()) c.knownChars.insert(host->rootId());
        for (auto& r : m_scene->remotes()) c.knownChars.insert(r.rootId);

        // Teleported here from another game (TeleportService): what they brought (player:GetJoinData()).
        // It comes from their app, so like Roblox's TeleportData it's only as trustworthy as they are.
        if (const json tp = m.value("tp", json()); tp.is_object() && tp.dump().size() <= 70 * 1024)
            m_session->scripts().setJoinData(name, tp);
        m_session->scripts().addPlayer(name, rig->id, c.id + 1, Player::backpackOf(*m_scene, rig)->id);
        m_session->joinerArrived(rig->id);   // their StarterPack tools
        if (!c.guest && !c.accountId.empty()) m_session->scripts().setPlayerAccount(c.id + 1, c.accountId);   // (game passes)
        if (!m_owner.empty() && c.accountId == m_owner) {   // the game's owner: the server side of the dev console
            c.owner = true;
            c.logSent = Log::count() - std::min<unsigned long long>(Log::entries().size(), 200);   // the last 200 lines first
            c.conn->send(json{{"t", "devowner"}}.dump());
        }
        m_chat.add("", name + " joined the game", true);
        broadcast(json{{"t", "chat"}, {"from", ""}, {"text", name + " joined the game"}, {"sys", true}}.dump(), &c);
        return;
    }
    if (!c.joined) return;
    RemoteCharacter* rc = m_scene->findRemote(c.rootId);

    if (t == "state" && rc) {
        if (m.contains("ts")) m_poses[c.rootId].push(m.value("ts", 0.0), clockNow(), poseFrom(m.value("pose", json())));
        else if (SceneNode* rig = m_scene->findById(c.rootId)) Player::applyPose(rig, poseFrom(m.value("pose", json())));   // older players
        double now = m_session->scripts().time();
        // Their own damage (e.g. fall damage) counts, unless a script just changed it.
        if (now - rc->editedAt > 0.5 && m.contains("health"))
            rc->humanoid.health = m["health"].get<float>();
        bool dead = m.value("dead", false);
        if (dead && rc->alive) {
            rc->alive = false;
            rc->humanoid.health = 0.0f;
            if (SceneNode* rig = m_scene->findById(c.rootId))
                if (SceneNode* torso = rig->findChild("Torso"))
                    m_scene->pushFx(m_scene->goreKind() == GoreKind::Blood ? FxEvent::Blood : FxEvent::Oil,
                                    glm::vec3(torso->worldMatrix()[3]), 30.0f);
            m_session->scripts().fireDied(c.rootId);
        } else if (!dead && !rc->alive) {
            rc->alive = true;
            rc->humanoid.health = rc->humanoid.maxHealth;
            m_session->joinerRespawned(c.rootId);
        }
    } else if (t == "touch") {
        uint64_t part = m.value("part", (uint64_t)0);
        SceneNode* rig = m_scene->findById(c.rootId);
        SceneNode* limb = rig ? rig->findChild(m.value("limb", std::string())) : nullptr;
        if (limb && m_scene->findById(part)) {
            m_session->scripts().setJoinerEvent(true);
            m_session->scripts().fireTouched(part, limb->id);
            m_session->scripts().setJoinerEvent(false);
            m_session->joinerTouched(c.rootId, part);   // a tool's Handle: they pick it up
        }
    } else if (t == "tool") {   // their hotbar: equip / drop / use (the tool's scripts run here)
        const std::string what = m.value("do", std::string());
        if (what == "equip") m_session->joinerEquip(c.rootId, m.value("id", (uint64_t)0));
        else if (what == "drop") m_session->joinerDrop(c.rootId);
        else if (what == "use") m_session->joinerUse(c.rootId, m.value("down", false));
    } else if (t == "click") {
        uint64_t part = m.value("part", (uint64_t)0);
        if (m_scene->findById(part)) {
            m_session->scripts().setJoinerEvent(true);
            m_session->scripts().fireClicked(part);
            m_session->scripts().setJoinerEvent(false);
        }
    } else if (t == "passdone") {   // they answered a game pass Buy window
        m_session->scripts().passPromptDone(c.id + 1, m.value("pass", std::string()), m.value("bought", false));
    } else if (t == "devcmd") {   // the dev console's command bar: only the game's owner may run code here
        if (!c.owner) return;
        devCommand(m.value("code", std::string()));
    } else if (t == "guiclick") {
        uint64_t id = m.value("id", (uint64_t)0);
        if (SceneNode* b = m_scene->findById(id); b && b->isGuiButton()) {
            m_session->scripts().setJoinerEvent(true);
            m_session->scripts().fireGui(SignalKind::GuiClick, id);
            m_session->scripts().setJoinerEvent(false);
        }
    } else if (t == "guitext") {   // they typed in a TextBox (and it lost focus)
        if (c.guest) return;           // (it shows for everyone, like chat, so guests don't)
        uint64_t id = m.value("id", (uint64_t)0);
        SceneNode* box = m_scene->findById(id);
        if (!box || !box->isGui() || box->gui.type != GuiType::TextBox || !box->gui.editable) return;
        std::string text = m.value("text", std::string());
        if (text.size() > 10000) text.resize(10000);
        for (char& ch : text) if ((unsigned char)ch < 32 && ch != '\n') ch = ' ';
        m_session->scripts().setJoinerEvent(true);
        if (text != box->gui.text) {
            box->gui.text = text;
            m_session->scripts().firePropertyChanged(id, "Text");
        }
        m_session->scripts().fireFocusLost(id, m.value("enter", false));
        m_session->scripts().setJoinerEvent(false);
    } else if (t == "prompt") {   // they pressed a ProximityPrompt (it must be near them)
        const uint64_t id = m.value("id", (uint64_t)0);
        SceneNode* pr = m_scene->findById(id);
        SceneNode* rig = m_scene->findById(c.rootId);
        if (!pr || !pr->isPrompt() || !rig || !rc || !rc->alive) return;
        const std::string what = m.value("do", std::string());
        static const std::map<std::string, SignalKind> kinds = {
            {"t", SignalKind::PromptTriggered}, {"te", SignalKind::PromptTriggerEnded},
            {"hb", SignalKind::PromptHoldBegan}, {"he", SignalKind::PromptHoldEnded}};
        auto k = kinds.find(what);
        if (k == kinds.end()) return;
        // Starting a press needs them close enough (with some slack for lag); letting go always counts.
        if (what == "t" || what == "hb") {
            if (!pr->enabled) return;
            const SceneNode* at = pr->parent;
            if (at && at->kind == NodeKind::Model)
                for (auto& ch : at->children) if (ch->isPart()) { at = ch.get(); break; }
            if (!at) return;
            const glm::vec3 where(at->worldMatrix()[3]), me(rig->worldMatrix()[3]);
            if (glm::length(where - me) > pr->prompt.range + 4.0f) return;
        }
        m_session->scripts().setJoinerEvent(true);
        m_session->scripts().firePrompt(k->second, id, c.id + 1);
        m_session->scripts().setJoinerEvent(false);
    } else if (t == "remote" || t == "invoke") {   // a LocalScript's FireServer / InvokeServer
        // At most about 60 a second each (a burst of 60 is fine), and 64 KB apiece.
        const double now = clockNow();
        c.remoteBudget = std::min(60.0, c.remoteBudget + (now - c.remoteAt) * 60.0);
        c.remoteAt = now;
        if (c.remoteBudget < 1.0 || text.size() > 70 * 1024) return;
        c.remoteBudget -= 1.0;
        m_session->scripts().remoteIn(m, c.id + 1);
    } else if (t == "voiceon") {   // they turned voice chat on or off (guests can't use it)
        c.voiceOn = m.value("on", false) && !c.guest;
    } else if (t == "voice") {
        const double now = clockNow();
        c.voiceBudget = std::min(40.0, c.voiceBudget + (now - c.voiceAt) * 20.0);
        c.voiceAt = now;
        if (!c.voiceOn || c.guest || !voiceAllowed() || c.voiceBudget < 1.0) return;
        const std::string d = m.value("d", std::string());
        if (d.empty() || d.size() > kVoiceMaxBytes) return;
        c.voiceBudget -= 1.0;
        passVoice(c.name, c.rootId, d, &c);
    } else if (t == "chat") {
        std::string msg = TextFilter::filter(cleanText(m.value("text", std::string()), 200));
        if (msg.empty()) return;
        if (c.guest) {   // guests don't chat: tell just them why
            c.conn->send(json{{"t", "chat"}, {"from", ""}, {"text", Online::kGuestChatText}, {"sys", true}}.dump());
            return;
        }
        std::string to, said;
        if (ChatLog::parseWhisper(msg, to, said)) {
            auto tell = [&](const std::string& text) {
                c.conn->send(json{{"t", "chat"}, {"from", ""}, {"text", text}, {"sys", true}}.dump());
            };
            if (to.empty() || said.empty()) { tell(ChatLog::kWhisperHelp); return; }
            json w{{"t", "chat"}, {"from", c.name}, {"text", said}, {"wh", true}, {"adm", c.admin}, {"ver", c.verified}};
            if (!m_dedicated && sameName(to, Online::playerName())) {   // to the host
                w["to"] = Online::playerName();
                m_chat.addWhisper(c.name, Online::playerName(), said, c.admin, c.verified);
            } else if (Client* target = findClient(to); target && target != &c) {
                w["to"] = target->name;
                target->conn->send(w.dump());
            } else {
                tell(target == &c ? "You can't whisper to yourself." : "There's no player called " + to + " in this game.");
                return;
            }
            c.conn->send(w.dump());   // their own copy ("To ...")
            return;
        }
        m_chat.add(c.name, msg, false, c.admin, c.verified);
        broadcast(json{{"t", "chat"}, {"from", c.name}, {"text", msg}, {"adm", c.admin}, {"ver", c.verified}}.dump());
    }
}

std::string NetServer::worldMessage(bool) {
    json upd = json::array(), add = json::array(), del = json::array();
    std::set<uint64_t> seen, addedNow;

    // Depth-first so parents come before their children.
    std::vector<SceneNode*> stack;
    for (auto it = m_scene->root()->children.rbegin(); it != m_scene->root()->children.rend(); ++it)
        stack.push_back(it->get());
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (m_scene->isCharacterPart(n)) continue;          // characters travel as poses
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back(it->get());
        seen.insert(n->id);
        std::string state = nodeState(n);
        auto found = m_sent.find(n->id);
        if (found == m_sent.end()) {
            if (!n->parent || !addedNow.count(n->parent->id))
            {
                HideServerCode hide(n);
                add.push_back({{"parent", n->parent ? n->parent->id : 0}, {"node", Serializer::nodeToString(*n)}});
            }
            addedNow.insert(n->id);
        } else if (found->second != state) {
            upd.push_back(nodeUpdate(n));
        }
        m_sent[n->id] = std::move(state);
    }
    for (auto it = m_sent.begin(); it != m_sent.end();) {
        if (!seen.count(it->first)) { del.push_back(it->first); it = m_sent.erase(it); }
        else ++it;
    }

    json msg = {{"t", "world"}};
    if (!upd.empty()) msg["upd"] = upd;
    if (!add.empty()) msg["add"] = add;
    if (!del.empty()) msg["del"] = del;

    std::string env = Serializer::environmentToString(m_scene->environment());
    if (env != m_lastEnv) { msg["env"] = env; m_lastEnv = env; }
    // The camera setting (scripts can change Workspace.Orthographic while playing).
    const WorldSettings& ws = m_scene->world();
    json cam = {{"ortho", ws.orthographic}, {"size", ws.orthographicSize}, {"player", cameraJson(ws.playerCamera)}};
    if (cam.dump() != m_lastCam) { msg["cam"] = cam; m_lastCam = cam.dump(); }
    // The terrain, when a script changed it (joiners get it with the scene). At most
    // twice a second: it can be big, and a script might change it every frame.
    {
        const Terrain& tr = m_scene->terrain();
        const double t = clockNow();
        if (!m_terrainSent) m_terrainSent = tr.version();
        else if (tr.version() != m_terrainSent && t - m_terrainAt >= 0.5) {
            msg["terrain"] = tr.toJson();
            m_terrainSent = tr.version();
            m_terrainAt = t;
        }
    }

    const GuiState& gui = m_session->gui();
    json g = {{"labels", gui.labels}, {"msg", gui.message}, {"time", gui.message.empty() ? 0.0f : gui.messageTime}};
    std::string gs = json{{"labels", gui.labels}, {"msg", gui.message}}.dump();
    if (gs != m_lastGui) { msg["gui"] = g; m_lastGui = gs; }

    if (!m_scene->fxQueue.empty()) {
        json fx = json::array();
        for (const FxEvent& f : m_scene->fxQueue) {
            json e = {{"k", (int)f.type}, {"p", vec3(f.pos)}, {"n", f.amount}, {"s", f.name}};
            if (f.node) e["id"] = f.node;
            if (!f.at3d) e["pos3d"] = false;
            fx.push_back(e);
        }
        msg["fx"] = fx;
        m_scene->fxQueue.clear();
    }
    return msg.dump();
}

bool NetServer::teleport(const std::string& name, const std::string& place, const json& data, const std::string& from) {
    Client* c = findClient(name);
    if (!c) return false;
    c->conn->send(json{{"t", "teleport"}, {"place", place}, {"data", data}, {"from", from}}.dump());
    return true;
}

bool NetServer::iAmOwner() const { return !m_owner.empty() && m_owner == Account::id(); }

void NetServer::devCommand(const std::string& code) {
    if (code.empty() || code.size() > 20000) return;
    Log::system("> " + code);
    std::string err;
    if (!m_session->scripts().runCommand(code, err)) Log::error(err);
}

// New server log lines, to whoever owns the game.
void NetServer::sendDevLog() {
    const auto& all = Log::entries();
    const unsigned long long total = Log::count();
    for (auto& up : m_clients) {
        Client& c = *up;
        if (!c.owner || c.logSent >= total) continue;
        unsigned long long first = std::max<unsigned long long>(c.logSent, total - all.size());
        json lines = json::array();
        for (unsigned long long i = first; i < total && lines.size() < 300; ++i) {
            const Log::Entry& e = all[all.size() - (size_t)(total - i)];
            lines.push_back({(int)e.level, e.time, e.text.substr(0, 2000)});
            c.logSent = i + 1;
        }
        c.conn->send(json{{"t", "devlog"}, {"lines", lines}}.dump());
    }
}

void NetServer::sendTick() {
    sendDevLog();
    // Host's own death shows up on everyone's screen too.
    if (Player* host = m_scene->player()) {
        static bool wasDead = false;
        if (host->isDead() && !wasDead)
            m_scene->pushFx(m_scene->goreKind() == GoreKind::Blood ? FxEvent::Blood : FxEvent::Oil,
                            host->focusPoint(), 30.0f);
        wasDead = host->isDead();
    }

    std::string world = worldMessage(false);
    json w = json::parse(world);

    // Characters: everyone's pose (each client skips its own).
    struct Char { uint64_t id; std::string name; SceneNode* root; bool admin; bool verified; bool creator; uint64_t held = 0; std::string heldNode, fx; };
    std::vector<Char> chars;
    if (Player* host = m_scene->player())
        if (SceneNode* r = host->root())
            chars.push_back({r->id, Online::playerName(), r, Account::iAmStaff(), Badges::iHave(Badges::Id::Verified), iAmOwner()});
    for (auto& rc : m_scene->remotes())
        if (SceneNode* r = m_scene->findById(rc.rootId)) {
            bool admin = false, ver = false, made = false;
            for (auto& c : m_clients) if (c->rootId == rc.rootId) { admin = c->admin; ver = c->verified; made = c->owner; }
            chars.push_back({rc.rootId, rc.name, r, admin, ver, made});
        }
    // The tool in each hand (sent again only when it changes).
    for (Char& ch : chars)
        if (SceneNode* t = Player::heldTool(ch.root)) { ch.held = t->id; ch.heldNode = toolString(t); }
    for (Char& ch : chars) ch.fx = charEffects(*m_scene, ch.root);

    for (auto& c : m_clients) {
        if (!c->joined) continue;
        json msg = w;
        json list = json::array();
        for (auto& ch : chars) {
            if (ch.id == c->rootId) continue;
            json entry = {{"i", ch.id}, {"n", ch.name}, {"a", ch.admin}, {"v", ch.verified}, {"c", ch.creator},
                          {"pose", poseJson(Player::capturePose(ch.root))}};
            if (auto st = m_session->scripts().leaderstats(ch.name); !st.empty()) entry["s"] = statsJson(st);
            if (!c->knownChars.count(ch.id)) {                   // first time: send the whole model
                entry["rig"] = Serializer::nodeToString(*ch.root);
                c->knownChars.insert(ch.id);
            }
            entry["ti"] = ch.held;
            if (std::string& was = c->heldSent[ch.id]; was != ch.heldNode) { entry["tn"] = ch.heldNode; was = ch.heldNode; }
            list.push_back(entry);
        }
        msg["chars"] = list;
        msg["ts"] = clockNow();
        if (auto mine = m_session->scripts().leaderstats(c->name); !mine.empty()) msg["mys"] = statsJson(mine);
        c->conn->send(msg.dump());
        // Highlights, Trails and Beams on characters (their own too), when they change.
        for (auto& ch : chars) {
            std::string& was = c->fxSent[ch.id];
            if (was == ch.fx) continue;
            was = ch.fx;
            c->conn->send(json{{"t", "fx"}, {"i", ch.id}, {"list", ch.fx.empty() ? json::array() : json::parse(ch.fx)}}.dump());
        }

        // Things only this player needs to know about their own character.
        if (RemoteCharacter* rc = m_scene->findRemote(c->rootId)) {
            // The tools they carry (the real ones are here; they show copies).
            json list = json::array();
            for (SceneNode* t : m_session->joinerTools(c->rootId)) list.push_back({{"i", t->id}, {"n", toolString(t)}});
            SceneNode* held = Player::heldTool(m_scene->findById(c->rootId));
            std::string tools = json{{"t", "tools"}, {"held", held ? held->id : 0}, {"list", list}}.dump();
            if (tools != c->toolsSent) { c->conn->send(tools); c->toolsSent = std::move(tools); }
            if (rc->humanoidDirty) {
                c->conn->send(json{{"t", "hum"}, {"h", humanoidJson(rc->humanoid)}}.dump());
                rc->humanoidDirty = false;
            }
            // Their CameraMode / zoom limits, once a script here changed them.
            if (rc->cameraSet) {
                std::string camera = json{{"t", "camopt"}, {"o", cameraJson(rc->camera)}}.dump();
                if (camera != c->cameraSent) { c->conn->send(camera); c->cameraSent = std::move(camera); }
            }
            for (auto& k : rc->kills)
                c->conn->send(json{{"t", "kill"}, {"force", k.force}, {"impulse", vec3(k.impulse)}}.dump());
            rc->kills.clear();
        }
    }
}

// ===========================================================================
// Client (joined someone's game)
// ===========================================================================

NetClient::NetClient(Scene* scene, GameSession* session) : m_scene(scene), m_session(session) {}
NetClient::~NetClient() { disconnect(); }

bool NetClient::connectRelay(const std::string& server, int port, const std::string& joinRequest) {
    if (!connect(server, port)) return false;
    m_conn->send(joinRequest);   // the server answers {"t":"relay"}, then the host's challenge follows
    return true;
}

bool NetClient::connect(const std::string& host, int port) {
    disconnect();
    m_conn = Net::Connection::connectTo(host, port, m_error);
    if (!m_conn) { m_state = State::Failed; return false; }
    m_nonce = Account::randomHex(16);
    m_hostAdmin = false;
    m_state = State::Connecting;   // "hello" goes out once the host sends its challenge
    return true;
}

void NetClient::disconnect() {
    m_conn.reset();
    Voice::forgetAll();
    m_voiceTold = -1;
    if (m_state == State::Joined) {
        m_session->stop();
        m_session->setRole(GameSession::Role::Solo);
        m_session->onTouch = nullptr;
        m_session->onClick = nullptr;
        m_session->onGuiClick = nullptr;
        m_session->onGuiText = nullptr;
        m_session->onToolRequest = nullptr;
        m_session->onPrompt = nullptr;
        m_session->scripts().idToServer = nullptr;
        m_session->scripts().idFromServer = nullptr;
        if (Player* p = m_scene->player()) p->clearTools();
        m_toolSeen.clear();
        m_scene->remotes().clear();
    }
    m_state = State::Idle;
}

void NetClient::say(const std::string& text) {
    std::string t = cleanText(text, 200);
    if (t.empty() || !m_conn) return;
    if (Online::isGuest() && Online::online()) { m_chat.add("", Online::kGuestChatText, true); return; }
    m_conn->send(json{{"t", "chat"}, {"text", t}}.dump());
}

void NetClient::reportTouch(uint64_t part, const std::string& limb) {
    if (m_conn && part < kLocalIdBase) m_conn->send(json{{"t", "touch"}, {"part", part}, {"limb", limb}}.dump());
}

void NetClient::reportClick(uint64_t part) {
    if (m_conn && part < kLocalIdBase) m_conn->send(json{{"t", "click"}, {"part", part}}.dump());
}

void NetClient::update(float dt) {
    if (!m_conn) return;
    bool ok = m_conn->poll();
    std::string msg;
    while (m_conn && m_conn->pop(msg)) handle(msg);
    if (!m_conn) return;
    if (!ok || !m_conn->alive()) {
        if (m_state == State::Joined) m_hostGone = true;
        if (m_error.empty()) m_error = m_state == State::Joined ? "Lost connection to the host."
                                                                : "The host didn't let us in.";
        disconnect();
        m_state = State::Failed;
        return;
    }

    if (m_state == State::Joined) {
        for (auto& out : m_session->scripts().takeRemoteOut())   // our LocalScripts' FireServer / InvokeServer
            if (out.to == -1) m_conn->send(out.msg.dump());
        // Voice chat: tell the host whether we're listening, then send what we said.
        const bool voice = m_voiceAllowed && iUseVoice();
        if (m_voiceTold != (int)voice) {
            m_voiceTold = (int)voice;
            m_conn->send(json{{"t", "voiceon"}, {"on", voice}}.dump());
            if (!voice) Voice::forgetAll();
        }
        std::string packet;
        while (Voice::takePacket(packet))
            if (voice) m_conn->send(json{{"t", "voice"}, {"d", Online::base64Encode(packet)}}.dump());
        m_tick += dt;
        if (m_tick >= kTickRate) {
            m_tick = std::min(m_tick - kTickRate, kTickRate);
            if (Player* p = m_scene->player())
                if (SceneNode* r = p->root())
                    m_conn->send(json{{"t", "state"}, {"ts", clockNow()}, {"pose", poseJson(Player::capturePose(r))},
                                      {"health", p->humanoid().health}, {"dead", p->isDead()}}.dump());
        }
    }
    showSmoothly(*m_scene, m_poses);
    m_chat.update(dt);
}

bool NetServer::promptPass(int scriptUserId, const std::string& pass) {
    for (auto& c : m_clients)
        if (c->joined && c->id + 1 == scriptUserId) {
            c->conn->send(json{{"t", "passprompt"}, {"pass", pass}}.dump());
            return true;
        }
    return false;
}

void NetClient::passDone(const std::string& pass, bool bought) {
    if (m_conn) m_conn->send(json{{"t", "passdone"}, {"pass", pass}, {"bought", bought}}.dump());
}

void NetClient::devCommand(const std::string& code) {
    if (m_conn && m_devOwner && !code.empty()) m_conn->send(json{{"t", "devcmd"}, {"code", code}}.dump());
}

void NetClient::handle(const std::string& text) {
    json m = json::parse(text, nullptr, false);
    if (!m.is_object()) return;
    std::string t = m.value("t", "");

    if (t == "relay") {   // the Guts&Bolts server, before handing us to the host
        if (m.value("ok", false)) {
            m_relaySession = m.value("session", std::string());
            m_relayGame = m.value("game", std::string());
        } else {
            m_error = m.value("error", std::string("Couldn't join that server."));
            if (m.value("hostLeft", false)) m_movingTurn = m.value("you", false) ? 2 : 1;
            m_conn.reset();
            m_state = State::Failed;
        }
        return;
    }
    if (t == "challenge") {
        Profile& me = Profile::get();
        const bool guest = Online::isGuest() && Online::online();
        json hello = {{"t", "hello"}, {"version", kVersion}, {"name", Online::playerName()},
                      {"guest", guest}, {"avatar", avatarJson(me)},
                      {"id", Account::id()}, {"sig", Account::sign("gb-join:" + m.value("nonce", std::string()))},
                      {"cnonce", m_nonce}, {"grants", myGrants()}};
        if (m_joinData.is_object()) hello["tp"] = m_joinData;   // teleported here (TeleportService)
        m_conn->send(hello.dump());
        return;
    }
    if (t == "welcome") {
        std::string err;
        if (!Serializer::loadScene(*m_scene, m.value("scene", std::string()), &err)) {
            m_error = "Couldn't load the host's game: " + err;
            m_conn.reset();
            m_state = State::Failed;
            return;
        }
        // Objects we create ourselves get ids far away from the host's.
        SceneNode::reserveId(kLocalIdBase);
        m_myServerRoot = m.value("you", (uint64_t)0);
        m_voiceAllowed = m.value("voice", false);
        if (SceneNode* mine = m_scene->findById(m_myServerRoot)) m_scene->removeNode(mine);

        // The host's character is just another model here; build our own.
        Player* p = m_scene->player();
        uint64_t hostRoot = p ? p->rootId() : 0;
        if (m.contains("hostChar")) {   // (older hosts don't say: their character is the scene's player)
            const uint64_t said = m.value("hostChar", (uint64_t)0);
            if (said != hostRoot)   // a game server machine: nobody to show (drop the stand-in the loader made)
                if (SceneNode* extra = hostRoot ? m_scene->findById(hostRoot) : nullptr) m_scene->removeNode(extra);
            hostRoot = said;
        }
        if (p) {
            p->setRootId(0);
            p->setSpawn(spawnPoint(*m_scene));
            p->build();
            Profile::get().applyTo(*p);
            if (SceneNode* r = p->root()) r->name = m.value("name", Online::playerName());
            if (m.contains("humanoid")) humanoidFrom(p->humanoid(), m["humanoid"]);
        }
        if (hostRoot) {
            RemoteCharacter host;
            host.rootId = hostRoot;
            m_scene->remotes().push_back(host);
        }
        // Is the host really who they say they are?
        if (m.contains("host") && m["host"].is_object()) {
            std::string hid = m["host"].value("id", std::string());
            bool proved = Account::verify(hid, "gb-host:" + m_nonce, m["host"].value("sig", std::string()));
            m_hostAdmin = proved && Account::isOfficial(hid);
            m_hostVerified = proved && Badges::has(hid, grantsFrom(m["host"]), Badges::Id::Verified);
        }
        m_hostRoot = hostRoot;
        m_players.clear();
        if (m.contains("players"))
            for (auto& e : m["players"])
                if (e.is_object()) m_players.push_back({e.value("n", std::string()), e.value("a", false), e.value("v", false), {}, e.value("c", false)});
        m_title = m_scene->info().title;

        m_session->setRole(GameSession::Role::Client);
        m_session->onTouch = [this](uint64_t part, const std::string& limb) { reportTouch(part, limb); };
        m_session->onClick = [this](uint64_t part) { reportClick(part); };
        m_session->onGuiClick = [this](uint64_t button) {   // a game UI button: the host's scripts hear it
            if (m_conn && button < kLocalIdBase) m_conn->send(json{{"t", "guiclick"}, {"id", button}}.dump());
        };
        m_session->onGuiText = [this](uint64_t box, const std::string& text, bool enter) {   // a TextBox: the host's scripts read it
            if (m_conn && box < kLocalIdBase) m_conn->send(json{{"t", "guitext"}, {"id", box}, {"text", text}, {"enter", enter}}.dump());
        };
        m_session->onPrompt = [this](uint64_t prompt, const char* what) {   // a ProximityPrompt: the host's scripts hear it
            if (m_conn && prompt < kLocalIdBase) m_conn->send(json{{"t", "prompt"}, {"id", prompt}, {"do", what}}.dump());
        };
        m_session->onToolRequest = [this](const std::string& what, uint64_t tool, bool down) {   // the host has our tools
            if (m_conn) m_conn->send(json{{"t", "tool"}, {"do", what}, {"id", tool}, {"down", down}}.dump());
        };
        // Our LocalScripts: our character has its own ids here, so swap them for the
        // server's when talking to it.
        ScriptEngine& se = m_session->scripts();
        se.setLocalUserId(m.value("uid", 0));
        se.setPlayerName(m.value("name", Online::playerName()));
        se.clearJoinData();
        if (m_joinData.is_object()) se.setJoinData(m.value("name", Online::playerName()), m_joinData);
        se.idToServer = [this](uint64_t id) {
            Player* p = m_scene->player();
            return p && id == p->rootId() ? m_myServerRoot : id;
        };
        se.idFromServer = [this](uint64_t id) {
            Player* p = m_scene->player();
            return p && id == m_myServerRoot ? p->rootId() : id;
        };
        m_toolSeen.clear();
        m_session->start();
        m_state = State::Joined;
        m_chat.add("", "Joined " + m_title + " as " + m.value("name", std::string("Player")), true);
        return;
    }
    if (t == "devowner") { m_devOwner = true; return; }
    if (t == "teleport") {   // the server's scripts sent us to another game (TeleportService)
        m_teleport = json{{"place", m.value("place", std::string())}, {"data", m.value("data", json())},
                          {"from", m.value("from", std::string())}};
        return;
    }
    if (t == "devlog") {
        for (const auto& l : m.value("lines", json::array())) {
            if (!l.is_array() || l.size() != 3 || !l[0].is_number_integer() || !l[1].is_string() || !l[2].is_string()) continue;
            m_serverLog.push_back({(Log::Level)std::clamp(l[0].get<int>(), 0, 3), l[2].get<std::string>(), l[1].get<std::string>()});
        }
        if (m_serverLog.size() > 2000) m_serverLog.erase(m_serverLog.begin(), m_serverLog.begin() + 500);
        return;
    }
    if (t == "bye") {
        m_error = m.value("reason", std::string("The host closed the game."));
        return;
    }
    if (t == "chat") {
        if (m.value("wh", false))
            m_chat.addWhisper(m.value("from", std::string()), m.value("to", std::string()), m.value("text", std::string()),
                              m.value("adm", false), m.value("ver", false));
        else
            m_chat.add(m.value("from", std::string()), m.value("text", std::string()), m.value("sys", false),
                       m.value("adm", false), m.value("ver", false));
        return;
    }
    if (m_state != State::Joined) return;
    if (t == "voicecfg") {
        m_voiceAllowed = m.value("on", false);
        if (!m_voiceAllowed) Voice::forgetAll();
        return;
    }
    if (t == "voice") {
        if (!m_voiceAllowed || !iUseVoice()) return;
        const std::string name = m.value("n", std::string());
        std::string bytes;
        if (name.empty() || !Online::base64Decode(m.value("d", std::string()), bytes)) return;
        bool found = false;
        const glm::vec3 at = headOf(*m_scene, m.value("id", (uint64_t)0), found);
        Voice::receive(name, bytes, at);
        return;
    }
    Player* me = m_scene->player();
    ScriptEngine& scripts = m_session->scripts();
    // Gone from the world: their LocalScripts stop too.
    auto drop = [&](SceneNode* n) { scripts.stopScripts(n); m_scene->removeNode(n); };

    if (t == "remote" || t == "invoked") { scripts.remoteIn(m); return; }

    if (t == "world") {
        if (m.contains("add"))
            for (const auto& a : m["add"]) {
                auto node = Serializer::nodeFromString(a.value("node", std::string()), false);
                if (!node) continue;
                if (SceneNode* old = m_scene->findById(node->id)) drop(old);
                SceneNode* parent = m_scene->findById(a.value("parent", (uint64_t)0));
                scripts.runScriptsIn(m_scene->insert(std::move(node), parent ? parent : m_scene->root()));
            }
        if (m.contains("upd"))
            for (const auto& u : m["upd"])
                if (SceneNode* n = m_scene->findById(u.value("i", (uint64_t)0)))
                    if (!m_scene->isCharacterPart(n)) applyUpdate(n, u);
        if (m.contains("del"))
            for (const auto& d : m["del"])
                if (SceneNode* n = m_scene->findById(d.get<uint64_t>()))
                    if (!m_scene->isCharacterPart(n)) drop(n);
        if (m.contains("env")) Serializer::environmentFromString(m_scene->environment(), m["env"].get<std::string>());
        if (m.contains("terrain")) m_scene->terrain().fromJson(m["terrain"]);
        if (m.contains("cam") && m["cam"].is_object()) {
            m_scene->world().orthographic = m["cam"].value("ortho", false);
            m_scene->world().orthographicSize = std::clamp(m["cam"].value("size", 0.0f), 0.0f, 2000.0f);
            if (m["cam"].contains("player")) m_scene->world().playerCamera = cameraFrom(m["cam"]["player"]);
        }
        if (m.contains("gui")) {
            GuiState& g = m_session->gui();
            g.labels.clear();
            for (auto& [k, v] : m["gui"]["labels"].items()) g.labels[k] = v.get<std::string>();
            g.message = m["gui"].value("msg", std::string());
            g.messageTime = m["gui"].value("time", 0.0f);
        }
        if (m.contains("fx")) replayFx(*m_scene, m["fx"]);
        if (m.contains("chars")) {
            m_players.clear();
            m_players.push_back({me && me->root() ? me->root()->name : Online::playerName(), Account::iAmStaff(),
                                 Badges::iHave(Badges::Id::Verified), statsFrom(m.value("mys", json::array())), m_devOwner});
            for (const auto& ch : m["chars"]) {
                uint64_t id = ch.value("i", (uint64_t)0);
                std::string name = ch.value("n", std::string());
                // The host vouches for everyone else; the host itself proved it on joining.
                bool admin = id == m_hostRoot ? m_hostAdmin : ch.value("a", false);
                bool ver = id == m_hostRoot ? m_hostVerified : ch.value("v", false);
                if (Account::nameIsReserved(name) && !admin) name = "Player";
                m_players.push_back({name, admin, ver, statsFrom(ch.value("s", json::array())), ch.value("c", false)});
                if (id == m_myServerRoot) continue;
                SceneNode* root = m_scene->findById(id);
                if (!root && ch.contains("rig")) {
                    auto rig = Serializer::nodeFromString(ch["rig"].get<std::string>(), false);
                    if (rig) root = m_scene->insert(std::move(rig));
                    // Their backpack stays with the host (only what they hold shows).
                    if (SceneNode* bag = root ? root->findChild("Backpack") : nullptr) m_scene->removeNode(bag);
                }
                if (!root) continue;
                showHeldTool(root, ch);
                root->name = name;
                if (!m_scene->findRemote(id)) {
                    RemoteCharacter rc;
                    rc.rootId = id;
                    rc.name = name;
                    m_scene->remotes().push_back(rc);
                }
                if (m.contains("ts")) m_poses[id].push(m.value("ts", 0.0), clockNow(), poseFrom(ch.value("pose", json())));
                else Player::applyPose(root, poseFrom(ch.value("pose", json())));
            }
        }
        return;
    }
    if (t == "tools" && me) { showMyTools(m); return; }
    if (t == "fx") {   // the Highlights, Trails and Beams on a character
        const uint64_t id = m.value("i", (uint64_t)0);
        auto& mine = m_charFx[id];
        for (uint64_t old : mine)
            if (SceneNode* n = m_scene->findById(old)) m_scene->removeNode(n);
        mine.clear();
        SceneNode* root = m_scene->findById(me && id == m_myServerRoot ? me->rootId() : id);
        if (!root || !m.contains("list") || !m["list"].is_array()) return;
        for (const auto& e : m["list"]) {
            if (!e.is_object()) continue;
            auto node = Serializer::nodeFromString(e.value("n", std::string()), true);
            SceneNode* in = findPath(root, e.value("in", std::string()));
            if (!node || !in || (!node->isHighlight() && !node->isEffect())) continue;
            node->children.clear();
            // What it points at, here.
            auto ref = [&](const char* inside, const char* world) -> uint64_t {
                if (e.contains(world)) return e.value(world, (uint64_t)0);
                if (!e.contains(inside)) return 0;
                SceneNode* to = findPath(root, e.value(inside, std::string()));
                return to ? to->id : 0;
            };
            if (node->isHighlight()) {
                node->highlight.adornee = ref("a", "w");
                if (node->highlight.adornee == root->id && in == root) node->highlight.adornee = 0;
            } else {
                node->effect.a0 = ref("a0", "w0");
                node->effect.a1 = ref("a1", "w1");
            }
            mine.push_back(in->addChild(std::move(node))->id);
        }
        return;
    }
    if (t == "passprompt") { m_passPrompt = m.value("pass", std::string()); return; }
    if (t == "hum" && me) {
        humanoidFrom(me->humanoid(), m.value("h", json::object()));
        return;
    }
    if (t == "camopt") {   // the host's scripts changed our player.CameraMode / zoom limits
        ScriptCamera& sc = m_scene->camera();
        sc.options = cameraFrom(m.value("o", json::object()));
        sc.optionsSet = true;
        return;
    }
    if (t == "kill" && me) {
        float force = m.value("force", 0.0f);
        glm::vec3 impulse = vec3(m.value("impulse", json()));
        if (force >= 0.0f) me->kill(force, impulse);
        else               me->launch(me->velocity() + impulse);
        return;
    }
    if (t == "left") {
        uint64_t id = m.value("id", (uint64_t)0);
        Voice::forget(m.value("name", std::string()));
        if (SceneNode* n = m_scene->findById(id)) drop(n);
        auto& rem = m_scene->remotes();
        rem.erase(std::remove_if(rem.begin(), rem.end(), [&](auto& r) { return r.rootId == id; }), rem.end());
        return;
    }
}

// The tools we carry: the host keeps the real ones (their scripts run there) and
// tells us what we have; we carry copies so the hotbar, our hand and our swing work.
void NetClient::showMyTools(const json& m) {
    Player* me = m_scene->player();
    if (!me || !me->root() || !m.contains("list") || !m["list"].is_array()) return;
    std::set<uint64_t> want;
    for (const auto& e : m["list"]) if (e.is_object()) want.insert(e.value("i", (uint64_t)0));
    for (SceneNode* t : me->tools())
        if (!want.count(t->id)) { m_toolSeen.erase(t->id); m_session->scripts().stopScripts(t); m_scene->removeNode(t); }
    auto isMine = [&](SceneNode* n) {
        for (SceneNode* p = n; p; p = p->parent) if (p == me->root()) return true;
        return false;
    };
    std::vector<uint64_t> order;
    for (const auto& e : m["list"]) {
        if (!e.is_object()) continue;
        const uint64_t id = e.value("i", (uint64_t)0);
        const std::string node = e.value("n", std::string());
        if (!id || id >= kLocalIdBase) continue;
        SceneNode* have = m_scene->findById(id);
        if (!have || !isMine(have) || m_toolSeen[id] != node) {
            if (have) { m_session->scripts().stopScripts(have); m_scene->removeNode(have); }
            auto copy = Serializer::nodeFromString(node, false);
            if (!copy || !copy->isTool()) continue;
            SceneNode* raw = m_scene->insert(std::move(copy));
            if (!me->give(raw)) { m_scene->removeNode(raw); continue; }
            m_toolSeen[id] = node;
            m_session->scripts().runScriptsIn(raw);   // its LocalScripts are ours to run
        }
        order.push_back(id);
    }
    me->setSlotOrder(order);
    const uint64_t held = m.value("held", (uint64_t)0);
    SceneNode* now = me->equippedTool();
    if ((now ? now->id : 0) != held) me->equip(held);
}

// What someone else holds (sent when it changes): shown in their hand.
void NetClient::showHeldTool(SceneNode* root, const json& ch) {
    if (!ch.contains("tn")) return;
    if (SceneNode* old = Player::heldTool(root)) { m_session->scripts().stopScripts(old); m_scene->removeNode(old); }
    const std::string node = ch.value("tn", std::string());
    if (node.empty()) return;
    auto copy = Serializer::nodeFromString(node, false);
    if (!copy || !copy->isTool()) return;
    if (SceneNode* dup = m_scene->findById(copy->id)) m_scene->removeNode(dup);   // (an old copy lying about)
    SceneNode* raw = m_scene->insert(std::move(copy), root);
    Player::placeInHand(root, raw);
}
