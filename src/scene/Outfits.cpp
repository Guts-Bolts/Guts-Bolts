// Outfits from scripts, like Roblox's HumanoidDescription: humanoid:ApplyDescription(desc)
// ends up here (Player::dressRig / Player::wear), and humanoid:GetAppliedDescription()
// reads Player::lookOf. In multiplayer the host sends a dressed character to everyone
// again (NetGame), so they all see the new outfit.
#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../core/Paths.h"
#include "../online/AssetCache.h"
#include "../renderer/Textures.h"

#include <algorithm>
#include <set>

namespace {

using json = nlohmann::json;

glm::vec3 colorFrom(const json& j, glm::vec3 fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    glm::vec3 c;
    for (int i = 0; i < 3; ++i) {
        if (!j[i].is_number()) return fallback;
        c[i] = std::clamp(j[i].get<float>(), 0.0f, 1.0f);
    }
    return c;
}
json colorJson(const glm::vec3& c) { return json::array({c.x, c.y, c.z}); }

// Only the server's own pictures and models ("gb:<id>"), never a file on someone's computer.
std::string serverId(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return {};
    std::string s = j[key].get<std::string>();
    return s.rfind("gb:", 0) == 0 && s.size() > 3 && s.size() < 64 ? s : std::string();
}
std::map<std::string, std::string> serverIds(const json& j, const char* key) {
    std::map<std::string, std::string> out;
    if (!j.contains(key) || !j[key].is_object()) return out;
    for (auto& [k, v] : j[key].items()) {
        if (k.size() > 32 || !v.is_string()) continue;
        const std::string s = v.get<std::string>();
        if (s.rfind("gb:", 0) == 0 && s.size() > 3 && s.size() < 64) out[k] = s;
    }
    return out;
}

// The look, cleaned up: only the keys we know, with checked values.
struct Parsed {
    BodyColors  colors;
    HatStyle    hat = HatStyle::None;
    glm::vec3   hatTint{-1.0f};
    std::string shirt, pants, tshirt, face, body;
    bool        headless = false;
    std::map<std::string, std::string> bodyParts;
    Player::Accessories accessories;
};
Parsed parse(const json& j, const BodyColors& now) {
    Parsed p;
    p.colors = {colorFrom(j.value("head", json()), now.head),       colorFrom(j.value("torso", json()), now.torso),
                colorFrom(j.value("leftArm", json()), now.leftArm), colorFrom(j.value("rightArm", json()), now.rightArm),
                colorFrom(j.value("leftLeg", json()), now.leftLeg), colorFrom(j.value("rightLeg", json()), now.rightLeg)};
    const json hat = j.value("hat", json());
    p.hat = (HatStyle)std::clamp(hat.is_number_integer() ? hat.get<int>() : 0, 0, kHatStyleCount - 1);
    p.hatTint = j.contains("hatColor") && j["hatColor"].is_array() && j["hatColor"].size() == 3 &&
                j["hatColor"][0].is_number() && j["hatColor"][0].get<float>() >= 0.0f
                    ? colorFrom(j["hatColor"], glm::vec3(-1.0f)) : glm::vec3(-1.0f);
    p.shirt = serverId(j, "shirtImage");
    p.pants = serverId(j, "pantsImage");
    p.tshirt = serverId(j, "tshirtImage");
    p.face = serverId(j, "faceImage");
    p.body = serverId(j, "body");
    p.headless = j.value("headless", json(false)) == json(true);
    p.bodyParts = serverIds(j, "bodyParts");
    p.accessories = serverIds(j, "accessories");
    return p;
}

json toJson(const Parsed& p) {
    return {{"head", colorJson(p.colors.head)}, {"torso", colorJson(p.colors.torso)},
            {"leftArm", colorJson(p.colors.leftArm)}, {"rightArm", colorJson(p.colors.rightArm)},
            {"leftLeg", colorJson(p.colors.leftLeg)}, {"rightLeg", colorJson(p.colors.rightLeg)},
            {"hat", (int)p.hat}, {"hatColor", colorJson(p.hatTint)},
            {"shirtImage", p.shirt}, {"pantsImage", p.pants}, {"tshirtImage", p.tshirt}, {"faceImage", p.face},
            {"headless", p.headless}, {"body", p.body}, {"bodyParts", p.bodyParts}, {"accessories", p.accessories}};
}

BodyColors colorsOf(const SceneNode* r) {
    auto col = [&](const char* n) {
        const SceneNode* p = r ? r->findChild(n) : nullptr;
        return p ? p->color : glm::vec3(0.64f);
    };
    return {col("Head"), col("Torso"), col("Left Arm"), col("Right Arm"), col("Left Leg"), col("Right Leg")};
}

// What still has to be downloaded for a look (asset ids, without "gb:").
std::vector<std::string> missing(const Parsed& p) {
    std::set<std::string> ids;
    auto want = [&](const std::string& src) {
        if (src.size() > 3 && Paths::downloaded(src.substr(3)).empty()) ids.insert(src.substr(3));
    };
    for (const std::string* s : {&p.shirt, &p.pants, &p.tshirt, &p.face, &p.body}) want(*s);
    for (auto& [k, v] : p.accessories) want(v);
    for (auto& [k, v] : p.bodyParts) want(v);
    return {ids.begin(), ids.end()};
}

// Put a parsed look on a rig (no downloading).
void dressNow(Scene& scene, SceneNode* r, const Parsed& p) {
    Player::applyColors(r, p.colors);
    Player::applyHat(scene, r, p.hat, p.hatTint);
    Player::applyClothing(r, p.shirt, p.pants, p.tshirt);
    Player::applyBody(scene, r, p.body, p.bodyParts);
    Player::applyAccessories(scene, r, p.accessories);
    Player::applyFace(scene, r, p.face);
    Player::applyHeadless(r, p.headless);
    scene.markDirty();
}

void remember(Scene& scene, uint64_t rootId, const Parsed& p) {
    Scene::Look& l = scene.looks()[rootId];
    l.look = toJson(p);
    ++l.version;
}

// When the missing things arrive, dress the character again (if it and its scene are
// still here and nobody changed the look in the meantime).
void fetchThenRedress(Scene& scene, uint64_t rootId, const Parsed& p) {
    const std::vector<std::string> ids = missing(p);
    if (ids.empty()) return;
    const int version = scene.looks()[rootId].version;
    std::weak_ptr<int> alive = scene.alive();
    Scene* sp = &scene;
    for (const std::string& id : ids)
        Online::download(id, [alive, sp, rootId, p, version](bool ok, const std::filesystem::path&, const json&) {
            if (!ok || alive.expired()) return;
            auto it = sp->looks().find(rootId);
            SceneNode* r = sp->findById(rootId);
            if (!r || it == sp->looks().end() || it->second.version != version) return;
            dressNow(*sp, r, p);
            ++it->second.version;   // (changed again: everyone gets it once more)
        });
}

} // namespace

void Player::dressRig(Scene& scene, SceneNode* root, const nlohmann::json& look) {
    if (!root || !look.is_object()) return;
    if (Player* me = scene.player(); me && me->rootId() == root->id) { me->wear(look); return; }
    const Parsed p = parse(look, colorsOf(root));
    dressNow(scene, root, p);
    remember(scene, root->id, p);
    Online::fetchSounds(scene);   // clothing pictures
    fetchThenRedress(scene, root->id, p);
}

void Player::wear(const nlohmann::json& look) {
    if (!look.is_object()) return;
    SceneNode* r = root();
    const Parsed p = parse(look, r ? colorsOf(r) : bodyColors());
    m_hat = p.hat;
    m_hatTint = p.hatTint;
    m_shirt = p.shirt;
    m_pants = p.pants;
    m_tshirt = p.tshirt;
    m_face = p.face;
    m_body = p.body;
    m_bodyParts = p.bodyParts;
    m_accessories = p.accessories;
    m_headless = p.headless;
    if (!r) return;
    dressNow(*m_scene, r, p);
    rememberWornRest();
    remember(*m_scene, r->id, p);
    Online::fetchSounds(*m_scene);
    fetchThenRedress(*m_scene, r->id, p);
}

nlohmann::json Player::lookOf(Scene& scene, const SceneNode* root) {
    if (!root) return json::object();
    auto it = scene.looks().find(root->id);
    Parsed p;
    if (it != scene.looks().end()) p = parse(it->second.look, colorsOf(root));
    else if (Player* me = scene.player(); me && me->rootId() == root->id) {
        p.hat = me->m_hat;
        p.hatTint = me->m_hatTint;
        p.shirt = me->m_shirt;
        p.pants = me->m_pants;
        p.tshirt = me->m_tshirt;
        p.face = me->m_face;
        p.body = me->m_body;
        p.bodyParts = me->m_bodyParts;
        p.accessories = me->m_accessories;
        p.headless = me->m_headless;
    } else {   // never dressed by a script (an NPC from Studio): what's on its parts
        auto tex = [&](const char* n) {
            const SceneNode* part = root->findChild(n);
            return part && part->texture.rfind("gb:", 0) == 0 ? part->texture : std::string();
        };
        p.shirt = tex("Left Arm");
        p.pants = tex("Left Leg");
        p.face = tex("Head");
        if (const SceneNode* torso = root->findChild("Torso"); torso && torso->tshirt.rfind("gb:", 0) == 0) p.tshirt = torso->tshirt;
        if (const SceneNode* head = root->findChild("Head")) p.headless = head->transparency >= 0.999f;
    }
    p.colors = colorsOf(root);   // (scripts can recolour parts directly too)
    return toJson(p);
}
