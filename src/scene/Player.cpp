#include "Player.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Physics.h"
#include "PlayerModel.h"
#include "../renderer/MeshLibrary.h"
#include "../renderer/Textures.h"
#include "../core/Audio.h"
#include "../core/Paths.h"
#include "Serializer.h"
#include "Animation.h"
#include "Emotes.h"
#include "Movers.h"
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <random>

namespace {

const glm::vec3 kYellow = {1.00f, 0.84f, 0.30f};
const glm::vec3 kBlue   = {0.20f, 0.45f, 0.85f};
const glm::vec3 kGreen  = {0.32f, 0.60f, 0.26f};
const glm::vec3 kBlack  = {0.05f, 0.05f, 0.05f};

const char* kLimbs[] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg"};

bool startsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

float approach(float cur, float target, float rate, float dt) {
    return cur + (target - cur) * std::min(1.0f, rate * dt);
}

std::mt19937& rng() { static std::mt19937 r{1234u}; return r; }
float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }

} // namespace

// ---------------------------------------------------------------------------
// Building the rig
// ---------------------------------------------------------------------------

void Player::resetSettings() {
    m_humanoid = Humanoid{};
    m_hat = HatStyle::None;
}

SceneNode* Player::root() const {
    return m_rootId ? m_scene->findById(m_rootId) : nullptr;
}

SceneNode* Player::part(const char* name) const {
    SceneNode* r = root();
    return r ? r->findChild(name) : nullptr;
}

void Player::build() {
    if (SceneNode* old = root()) m_scene->removeNode(old);
    SceneNode* r = buildRig(*m_scene, "Player", m_spawn);
    m_rootId = r->id;
    addAnimateScript(*m_scene, r);
    setHat(m_hat, m_hatTint);
    applyClothing(r, m_shirt, m_pants, m_tshirt);
    applyAccessories(*m_scene, r, m_accessories);
    applyFace(*m_scene, r, m_face);
    applyHeadless(r, m_headless);
    m_scene->markDirty();
}

void Player::setAccessories(const Accessories& acc) {
    m_accessories = acc;
    if (SceneNode* r = root()) applyAccessories(*m_scene, r, acc);
    rememberWornRest();
}

void Player::rememberWornRest() {
    // Hats and accessories turn with the head when it's animated, from how they
    // sat when put on (also when that happens in the middle of Play).
    if (m_rest.empty()) return;
    for (SceneNode* p : Anim::wornParts(root())) m_rest.try_emplace(p->id, p->transform);
}

void Player::setFace(const std::string& face) {
    m_face = face;
    if (SceneNode* r = root()) applyFace(*m_scene, r, face);
}

namespace {
std::string readSource(const std::string& src) {   // "gb:<id>" (downloaded) or a file
    std::filesystem::path p = src.rfind("gb:", 0) == 0 ? Paths::downloaded(src.substr(3)) : std::filesystem::path(src);
    if (p.empty()) return {};
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

std::unique_ptr<SceneNode> Player::accessoryFrom(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (!j.is_object() || j.value("format", std::string()) != "gbaccessory" || !j.contains("node")) return nullptr;
    auto n = Serializer::nodeFromString(j["node"].dump(), true);
    if (!n) return nullptr;
    // Worn things don't bump into anything or fall off.
    std::vector<SceneNode*> stack{n.get()};
    while (!stack.empty()) {
        SceneNode* c = stack.back(); stack.pop_back();
        c->canCollide = false;
        c->anchored = true;
        for (auto& k : c->children) stack.push_back(k.get());
    }
    return n;
}

void Player::applyAccessories(Scene& scene, SceneNode* r, const Accessories& acc) {
    std::vector<SceneNode*> old;
    for (auto& c : r->children) if (c->name.rfind("Accessory (", 0) == 0) old.push_back(c.get());
    for (auto* o : old) scene.removeNode(o);
    for (const auto& [kind, src] : acc) {
        if (src.empty()) continue;
        auto n = accessoryFrom(readSource(src));
        if (!n) continue;   // not downloaded yet
        n->name = "Accessory (" + kind + ")";
        r->addChild(std::move(n));   // placed relative to the feet, like the classic hats
    }
    hideBuiltInHat(r);
    scene.markDirty();
}

void Player::hideBuiltInHat(SceneNode* r) {
    // A hat or hair from the catalog takes the place of the built-in hat.
    bool worn = r->findChild("Accessory (hat)") || r->findChild("Accessory (hair)");
    for (auto& c : r->children)
        if (c->name.rfind("Hat", 0) == 0) c->visible = !worn;
}

void Player::applyFace(Scene& scene, SceneNode* r, const std::string& face) {
    SceneNode* head = r->findChild("Head");
    if (!head) return;
    const bool picture = !face.empty() && !readSource(face).empty();
    removeOldFace(scene, head);
    head->texture = picture ? face : std::string(Textures::kClassicFace);
    scene.markDirty();
}

void Player::setHeadless(bool on) {
    m_headless = on;
    if (SceneNode* r = root()) { applyHeadless(r, on); m_scene->markDirty(); }
}

void Player::applyHeadless(SceneNode* r, bool on) {
    if (SceneNode* head = r->findChild("Head")) head->transparency = on ? 1.0f : 0.0f;
}

void Player::removeOldFace(Scene& scene, SceneNode* head) {
    // Faces used to be little 3D shapes (eyes, a smile) or a flat card stuck in front
    // of the head. Now a face is a picture painted onto the head itself.
    std::vector<SceneNode*> old;
    for (auto& c : head->children)
        if (c->name == "FaceDecal" || c->name.rfind("Eye", 0) == 0 || c->name.rfind("Smile", 0) == 0) old.push_back(c.get());
    for (SceneNode* o : old) scene.removeNode(o);
}

void Player::setClothing(const std::string& shirt, const std::string& pants, const std::string& tshirt) {
    m_shirt = shirt;
    m_pants = pants;
    m_tshirt = tshirt;
    if (SceneNode* r = root()) applyClothing(r, shirt, pants, tshirt);
}

void Player::applyClothing(SceneNode* r, const std::string& shirt, const std::string& pants, const std::string& tshirt) {
    auto set = [&](const char* n, const std::string& t) { if (SceneNode* p = r->findChild(n)) p->texture = t; };
    set("Torso", shirt.empty() ? pants : shirt);
    if (SceneNode* torso = r->findChild("Torso")) torso->tshirt = tshirt;
    set("Left Arm", shirt); set("Right Arm", shirt);
    set("Left Leg", pants); set("Right Leg", pants);
}

SceneNode* Player::buildRig(Scene& scene, const std::string& name, const glm::vec3& feet) {
    // Container node for the whole character (origin at the feet).
    auto model = std::make_unique<SceneNode>(name, NodeKind::Model);
    model->transform.position = feet;
    SceneNode* r = scene.insert(std::move(model));

    auto addPart = [&](SceneNode* parent, const std::string& partName, PrimitiveType shape,
                       glm::vec3 pos, glm::vec3 scale, glm::vec3 col,
                       bool internal = false, bool visible = true) -> SceneNode* {
        auto node = std::make_unique<SceneNode>(partName);
        node->primitiveType      = shape;
        node->mesh               = MeshLibrary::get(shape);
        node->transform.position = pos;
        node->transform.scale    = scale;
        node->color    = col;
        node->internal = internal;
        node->visible  = visible;
        return parent->addChild(std::move(node));
    };
    const auto Cube = PrimitiveType::Cube;

    // Roblox part order. HumanoidRootPart is an invisible reference part.
    addPart(r, "HumanoidRootPart", Cube, {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f}, kBlue, false, false);
    addPart(r, "Torso",            Cube, {0.0f, 1.5f, 0.0f}, {1.0f, 1.0f, 0.5f}, kBlue);
    // The classic Roblox head is a rounded cylinder.
    SceneNode* head =
    addPart(r, "Head", PrimitiveType::Cylinder, {0.0f, 2.325f, 0.0f}, {0.72f, 0.65f, 0.72f}, kYellow);
    addPart(r, "Left Arm",         Cube, {-0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kYellow);
    addPart(r, "Right Arm",        Cube, { 0.75f, 1.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kYellow);
    addPart(r, "Left Leg",         Cube, {-0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kGreen);
    addPart(r, "Right Leg",        Cube, { 0.25f, 0.5f, 0.0f}, {0.5f, 1.0f, 0.5f}, kGreen);
    // Then give them the default character model's shapes (assets/models/player.obj).
    for (auto& c : r->children) usePlayerModel(*c);

    addFace(head);
    scene.markDirty();
    return r;
}

// The classic smiley: a flat picture painted onto the front (+Z) of the head
// (see Textures::kClassicFace and the lit shader's uFace).
void Player::addFace(SceneNode* head) {
    head->texture = Textures::kClassicFace;
}

void Player::upgradeFace() {
    // Characters saved with a 3D face (eye and smile parts): swap in the flat picture.
    SceneNode* r = root();
    SceneNode* head = r ? r->findChild("Head") : nullptr;
    if (!head) return;
    bool old = false;
    for (auto& c : head->children)
        if (c->name.rfind("Eye", 0) == 0 || c->name.rfind("Smile", 0) == 0) old = true;
    if (!old && !head->texture.empty()) return;
    removeOldFace(*m_scene, head);
    if (head->texture.empty()) addFace(head);
    m_scene->markDirty();
}

bool Player::usePlayerModel(SceneNode& part) {
    glm::vec3 pos, size;
    if (part.primitiveType == PrimitiveType::Mesh && PlayerModel::nameOf(part.editMesh.get())) return false;   // already
    if (!PlayerModel::placement(part.name, pos, size) || !PlayerModel::apply(part)) return false;
    part.transform.position = pos;
    part.transform.scale = size;
    return true;
}

void Player::upgradeRig(SceneNode* rig) {
    // Characters saved before the new model: swap the blocky parts for the model's.
    // (The face still fits: both heads are round with the same radius.)
    if (!rig) return;
    for (auto& c : rig->children)
        if (c->primitiveType == PrimitiveType::Cube || c->primitiveType == PrimitiveType::Cylinder) usePlayerModel(*c);
}

glm::vec3 Player::position() const {
    SceneNode* r = root();
    if (!m_dead && m_ragdoll.active()) return m_ragdoll.center() - glm::vec3(0.0f, 0.6f, 0.0f);   // knocked over: where the body lies
    return r ? r->transform.position : glm::vec3(0.0f);
}

glm::vec3 Player::focusPoint() const {
    if (m_ragdoll.active()) return m_ragdoll.center();   // (dead, or knocked over)
    if (m_dead)
        if (SceneNode* t = part("Torso")) return glm::vec3(t->worldMatrix()[3]);
    // Like Roblox, the camera looks at (and in first person, sits in) the head.
    // (Where the head is on the body, not how it bobs, so the view stays steady.)
    if (SceneNode* r = root())
        if (SceneNode* h = part("Head")) {
            auto it = m_rest.find(h->id);
            glm::vec3 local = it != m_rest.end() ? it->second.position : h->transform.position;
            return glm::vec3(r->worldMatrix() * glm::vec4(local, 1.0f));
        }
    return position() + glm::vec3(0.0f, 2.3f, 0.0f);
}

// ---------------------------------------------------------------------------
// Appearance
// ---------------------------------------------------------------------------

BodyColors Player::bodyColors() const {
    auto col = [&](const char* n, glm::vec3 fallback) {
        SceneNode* p = part(n);
        return p ? p->color : fallback;
    };
    return {col("Head", kYellow), col("Torso", kBlue), col("Left Arm", kYellow),
            col("Right Arm", kYellow), col("Left Leg", kGreen), col("Right Leg", kGreen)};
}

void Player::setBodyColors(const BodyColors& c) {
    if (SceneNode* r = root()) applyColors(r, c);
}

void Player::applyColors(SceneNode* r, const BodyColors& c) {
    auto set = [&](const char* n, glm::vec3 v) { if (SceneNode* p = r->findChild(n)) p->color = v; };
    set("Head", c.head);      set("Torso", c.torso);
    set("Left Arm", c.leftArm); set("Right Arm", c.rightArm);
    set("Left Leg", c.leftLeg); set("Right Leg", c.rightLeg);
    set("HumanoidRootPart", c.torso);
}

std::vector<std::pair<const char*, BodyColors>> Player::colorPresets() {
    const glm::vec3 skin  = {0.96f, 0.80f, 0.65f};
    const glm::vec3 grey  = {0.64f, 0.64f, 0.66f};
    const glm::vec3 dark  = {0.12f, 0.12f, 0.14f};
    return {
        {"Classic Noob", {kYellow, kBlue, kYellow, kYellow, kGreen, kGreen}},
        {"Guest",        {grey, dark, grey, grey, {0.85f,0.85f,0.87f}, {0.85f,0.85f,0.87f}}},
        {"Builder",      {skin, {0.93f,0.55f,0.16f}, skin, skin, {0.35f,0.27f,0.20f}, {0.35f,0.27f,0.20f}}},
        {"Ninja",        {dark, dark, dark, dark, dark, dark}},
        {"Red Team",     {skin, {0.80f,0.18f,0.16f}, skin, skin, dark, dark}},
        {"Blue Team",    {skin, {0.16f,0.36f,0.85f}, skin, skin, dark, dark}},
        {"Robot",        {{0.72f,0.74f,0.78f}, {0.45f,0.48f,0.53f}, {0.72f,0.74f,0.78f},
                          {0.72f,0.74f,0.78f}, {0.45f,0.48f,0.53f}, {0.45f,0.48f,0.53f}}},
    };
}

const char* Player::hatName(HatStyle s) {
    switch (s) {
        case HatStyle::TopHat: return "Top Hat";
        case HatStyle::Cap:    return "Cap";
        case HatStyle::Crown:  return "Crown";
        case HatStyle::Ponytail: return "Ponytail";
        default:               return "None";
    }
}

void Player::setHat(HatStyle style, glm::vec3 tint) {
    m_hat = style;
    m_hatTint = tint;
    if (SceneNode* r = root()) applyHat(*m_scene, r, style, tint);
    rememberWornRest();
}

void Player::applyHat(Scene& scene, SceneNode* r, HatStyle style, glm::vec3 tint) {
    const bool tinted = tint.x >= 0.0f;
    auto main = [&](glm::vec3 normal) { return tinted ? tint : normal; };
    // Remove the old hat pieces.
    std::vector<SceneNode*> old;
    for (auto& c : r->children)
        if (startsWith(c->name, "Hat")) old.push_back(c.get());
    for (auto* o : old) scene.removeNode(o);

    auto add = [&](const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 scale,
                   glm::vec3 col, Material mat = Material::Plastic) {
        auto n = std::make_unique<SceneNode>(name);
        n->primitiveType      = shape;
        n->mesh               = MeshLibrary::get(shape);
        n->transform.position = pos;
        n->transform.scale    = scale;
        n->color              = col;
        n->material           = mat;
        r->addChild(std::move(n));
    };
    // Top of the head, and how much narrower it is than the hats were made for (0.72 wide).
    glm::vec3 headPos{0.0f, 2.325f, 0.0f}, headSize{0.72f, 0.65f, 0.72f};
    PlayerModel::placement("Head", headPos, headSize);
    const float top = headPos.y + headSize.y * 0.5f;
    const float w = headSize.x / 0.72f;
    auto addW = [&](const char* name, PrimitiveType shape, glm::vec3 pos, glm::vec3 scale, glm::vec3 col,
                    Material mat = Material::Plastic) {
        add(name, shape, {pos.x * w, pos.y, pos.z * w}, {scale.x * w, scale.y, scale.z * w}, col, mat);
    };
    switch (style) {
        case HatStyle::TopHat:
            addW("Hat Brim", PrimitiveType::Cylinder, {0, top + 0.025f, 0}, {1.0f, 0.05f, 1.0f}, main(kBlack));
            addW("Hat",      PrimitiveType::Cylinder, {0, top + 0.325f, 0}, {0.62f, 0.55f, 0.62f}, main(kBlack));
            addW("Hat Band", PrimitiveType::Cylinder, {0, top + 0.11f, 0}, {0.64f, 0.1f, 0.64f}, {0.75f, 0.12f, 0.12f});
            break;
        case HatStyle::Cap:
            addW("Hat",       PrimitiveType::Sphere, {0, top - 0.05f, 0}, {0.78f, 0.5f, 0.78f}, main({0.85f, 0.15f, 0.15f}));
            addW("Hat Visor", PrimitiveType::Cube,   {0, top - 0.03f, 0.46f}, {0.52f, 0.04f, 0.36f}, main({0.85f, 0.15f, 0.15f}));
            break;
        case HatStyle::Crown:
            addW("Hat",       PrimitiveType::Cylinder, {0, top + 0.14f, 0}, {0.74f, 0.28f, 0.74f}, main({1.0f, 0.78f, 0.2f}), Material::Metal);
            addW("Hat Jewel", PrimitiveType::Cube, {0, top + 0.14f, 0.37f}, {0.12f, 0.12f, 0.05f}, {0.9f, 0.1f, 0.2f}, Material::Neon);
            break;
        case HatStyle::Ponytail: {   // hair: a cap over the top and back of the head, a bun and a tail
            const glm::vec3 hair = main({0.86f, 0.2f, 0.62f});
            addW("Hat",       PrimitiveType::Sphere, {0, top - 0.1f, -0.05f}, {0.8f, 0.46f, 0.82f}, hair);
            addW("Hat Back",  PrimitiveType::Cube,   {0, top - 0.3f, -0.3f}, {0.74f, 0.42f, 0.16f}, hair);
            addW("Hat Bun",   PrimitiveType::Sphere, {0, top + 0.12f, -0.3f}, {0.36f, 0.34f, 0.36f}, hair);
            addW("Hat Tail",  PrimitiveType::Sphere, {0, top - 0.14f, -0.5f}, {0.26f, 0.56f, 0.26f}, hair);
            break;
        }
        default: break;
    }
    hideBuiltInHat(r);
    scene.markDirty();
}

CharacterPose Player::capturePose(const SceneNode* r) {
    CharacterPose pose;
    pose.root = r->transform;
    pose.forceField = r->hasForceField();
    for (auto& c : r->children) {
        if (c->isPart()) pose.parts.push_back({c->name, c->transform});
        // Accessories' parts too, so they turn with the head for everyone ("Accessory (hat)/Brim").
        if (c->kind == NodeKind::Model && c->name.rfind("Accessory (", 0) == 0)
            for (auto& k : c->children)
                if (k->isPart()) pose.parts.push_back({c->name + "/" + k->name, k->transform});
    }
    return pose;
}

void Player::applyPose(SceneNode* r, const CharacterPose& pose) {
    r->transform = pose.root;
    // Show / hide the ForceField to match.
    if (pose.forceField && !r->hasForceField()) {
        r->addChild(std::make_unique<SceneNode>("ForceField", NodeKind::ForceField));
    } else if (!pose.forceField && r->hasForceField()) {
        for (auto& c : r->children)
            if (c->kind == NodeKind::ForceField) { r->removeChild(c.get()); break; }
    }
    for (const auto& [name, t] : pose.parts) {
        const size_t slash = name.find('/');
        SceneNode* c = r->findChild(name.substr(0, slash));
        if (c && slash != std::string::npos) c = c->findChild(name.substr(slash + 1));
        if (c) c->transform = t;
    }
}

// ---------------------------------------------------------------------------
// Play mode
// ---------------------------------------------------------------------------

namespace {
void collectSpawns(SceneNode* node, std::vector<SceneNode*>& out) {
    if (node->isSpawnLocation() && node->visible) out.push_back(node);
    for (auto& c : node->children) collectSpawns(c.get(), out);
}
bool sameTeamColor(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b) < 0.02f; }
} // namespace

// Like Roblox: a Neutral SpawnLocation is for anybody, a team one only for players on
// the team with its TeamColor; one of those at random. A random spot on top of it, so
// players in multiplayer don't all appear inside each other.
bool Player::findSpawn(Scene& scene, bool neutral, const glm::vec3& teamColor, glm::vec3& out) {
    std::vector<SceneNode*> all, usable;
    collectSpawns(scene.root(), all);
    if (all.empty()) return false;
    for (SceneNode* s : all)
        if (s->spawnNeutral || (!neutral && sameTeamColor(s->teamColor, teamColor))) usable.push_back(s);
    SceneNode* spawn = usable.empty() ? all.front()
                                      : usable[std::min(usable.size() - 1, (size_t)(rand01() * (float)usable.size()))];
    AABB b = Physics::worldBounds(spawn);
    glm::vec3 c = (b.min + b.max) * 0.5f;
    glm::vec3 half = glm::max((b.max - b.min) * 0.5f - glm::vec3(0.6f), glm::vec3(0.0f));
    out = {c.x + (rand01() * 2 - 1) * half.x, b.max.y + 0.001f, c.z + (rand01() * 2 - 1) * half.z};
    return true;
}

glm::vec3 Player::spawnSpot() const {
    glm::vec3 spawnAt = m_spawn;
    findSpawn(*m_scene, m_neutral, m_teamColor, spawnAt);
    if (SceneNode* cp = m_checkpoint ? m_scene->findById(m_checkpoint) : nullptr) {   // a checkpoint you reached
        AABB b = Physics::worldBounds(cp);
        spawnAt = glm::vec3((b.min.x + b.max.x) * 0.5f, b.max.y + 0.05f, (b.min.z + b.max.z) * 0.5f);
    }
    return spawnAt;
}

void Player::placeAtSpawn() {
    SceneNode* r = root();
    if (!r || m_dead) return;
    standUp(false);
    r->transform.position = spawnSpot();
    m_velocity = glm::vec3(0.0f);
    m_grounded = false;
    m_groundId = 0;
}

void Player::beginPlay() {
    m_seatId = 0;   // (off any seat)
    m_sitHere = m_sitPending = false;
    m_checkpoint = 0;
    m_rest.clear();
    m_debris.clear();
    SceneNode* r = root();
    if (r) {
        addAnimateScript(*m_scene, r);   // (games made before it existed)
        m_customClips.clear();
        m_rootRest = r->transform;
        for (auto& c : r->children) m_rest[c->id] = c->transform;
        rememberWornRest();
    }
    m_dead = m_diedFlag = false;
    respawn(true);
}

void Player::endPlay() {
    footsteps(false, glm::vec3(0.0f));
    m_checkpoint = 0;
    m_dead = false;
    m_debris.clear();
    m_rest.clear();
}

void Player::respawn(bool firstSpawn) {
    if (!firstSpawn && onRespawning) onRespawning();   // (Player.CharacterRemoving: still where it died)
    m_aliveTime = 0.0f;
    m_seatId = 0;   // (off any seat)
    m_sitHere = m_sitPending = false;
    m_tilt = m_tiltVel = m_tripTime = m_getUp = 0.0f;   // back on your feet
    stopEmote(0.0f);
    m_scriptMove = glm::vec3(0.0f);
    m_humanoid.platformStand = false;
    SceneNode* r = root();
    if (!r) return;
    m_scene->particles().wipe(*m_scene, r);   // a clean body (no blood stains from the last life)
    m_feet.reset();
    for (auto& c : r->children) {
        auto it = m_rest.find(c->id);
        if (it != m_rest.end()) c->transform = it->second;
    }
    for (SceneNode* p : Anim::wornParts(r))
        if (auto it = m_rest.find(p->id); it != m_rest.end()) p->transform = it->second;
    const glm::vec3 spawnAt = spawnSpot();
    r->transform = m_rootRest;
    r->transform.position = spawnAt;

    m_humanoid.health = m_humanoid.maxHealth;
    m_respawnedFlag = true;
    {   // a splat when you arrive in the game, a bass hit when you come back to life
        glm::vec3 at = r->transform.position + glm::vec3(0, 2, 0);
        Audio::play(firstSpawn ? "spawn" : "respawn", 0.7f, 1.0f, false, &at);
    }
    m_velocity  = glm::vec3(0.0f);
    m_grounded  = false;
    m_groundId  = 0;
    m_walkPhase = m_swing = m_airBlend = 0.0f;
    m_dead      = false;
    m_debris.clear();
    m_ragdoll.unhide(*m_scene);   // (blown to pieces last time)
    m_ragdoll.release(*m_scene);
    m_knockTool = 0;
    m_knockTime = 0.0f;
    m_pendingForce = 0.0f;
    m_pendingImpulse = glm::vec3(0.0f);
    m_lastHealth = m_humanoid.health;

    // A few seconds of ForceField after spawning.
    if (SceneNode* old = m_scene->findById(m_spawnFF)) m_scene->removeNode(old);
    m_spawnFF = 0;
    m_spawnFFTime = m_scene->world().spawnForceField;
    if (m_spawnFFTime > 0.0f) {
        auto ff = std::make_unique<SceneNode>("ForceField", NodeKind::ForceField);
        m_spawnFF = m_scene->insert(std::move(ff), r)->id;
    }
}

bool Player::hasForceField() const {
    SceneNode* r = root();
    return r && r->hasForceField();
}

void Player::kill(float force, const glm::vec3& impulse) {
    if (m_dead) return;
    m_pendingForce   = std::max(m_pendingForce, glm::clamp(force, 0.0f, 1.0f));
    m_pendingImpulse += impulse;
    m_humanoid.health = 0.0f;
}

void Player::hurt(float damage, float force, const glm::vec3& impulse) {
    if (m_dead || damage <= 0.0f) return;
    if (hasForceField()) {   // shielded: just a shove (a big one still knocks you over)
        launch(m_velocity + impulse * 0.5f);
        if (glm::length(impulse) > 16.0f) trip(1.0f, glm::vec3(glm::length(impulse) * 15.0f, 0, 0));
        return;
    }
    m_humanoid.health = std::max(0.0f, m_humanoid.health - damage);
    if (m_humanoid.health <= 0.0f) { kill(force, impulse); return; }
    launch(m_velocity + impulse);
    // A big blast knocks you off your feet and spins you (2011 style).
    if (glm::length(impulse) > 8.0f) trip(1.4f, glm::vec3(glm::length(impulse) * 30.0f * (impulse.x + impulse.z >= 0.0f ? 1.0f : -1.0f), 0, 0));
}

// Little squirts of blood / oil when the character gets hurt.
void Player::bleed(float damage) {
    if (!m_scene->goreEnabled()) return;
    SceneNode* t = part("Torso");
    if (!t) return;
    glm::vec3 at = glm::vec3(t->worldMatrix()[3]);
    int n = std::clamp((int)(damage * 0.5f), 3, 30);
    m_scene->particles().spray(m_scene->goreKind(), at, glm::vec3(0, 0.5f, 0), n, 2.5f);
    // The blood soaks into the body where it came out (bigger hits, bigger stains).
    glm::vec3 spot = at + glm::vec3(rand01() - 0.5f, rand01() - 0.5f, rand01() - 0.5f) * 2.0f;
    SceneNode* where = t;
    if (rand01() < 0.35f) {
        static const char* kOther[] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg", "Head"};
        if (SceneNode* o = part(kOther[(int)(rand01() * 4.99f)])) { where = o; spot = glm::vec3(o->worldMatrix()[3]) + (spot - at); }
    }
    m_scene->particles().stain(m_scene->goreKind(), where, spot, std::clamp(0.3f + damage * 0.02f, 0.3f, 1.0f));
    if (m_scene->goreKind() == GoreKind::Oil) m_scene->particles().sparks(at, n / 2);
}

bool Player::consumeRespawned() {
    bool r = m_respawnedFlag;
    m_respawnedFlag = false;
    return r;
}

// ---------------------------------------------------------------------------
// Tools
// ---------------------------------------------------------------------------

SceneNode* Player::backpackOf(Scene& scene, SceneNode* r) {
    if (!r) return nullptr;
    if (SceneNode* b = r->findChild("Backpack")) return b;
    auto b = std::make_unique<SceneNode>("Backpack", NodeKind::Model);
    b->visible = false;      // tools in here aren't drawn and don't collide
    b->internal = true;      // (and don't show in the Explorer)
    SceneNode* raw = r->addChild(std::move(b));
    scene.markDirty();
    return raw;
}

SceneNode* Player::heldTool(const SceneNode* r) {
    if (!r) return nullptr;
    for (auto& c : r->children) if (c->isTool()) return c.get();
    return nullptr;
}

uint64_t Player::backpackId() {
    SceneNode* b = backpackOf(*m_scene, root());
    return b ? b->id : 0;
}

SceneNode* Player::equippedTool() const { return heldTool(root()); }

void Player::syncSlotList(Scene& scene, std::vector<uint64_t>& slots, SceneNode* r) {
    // Keep the hotbar order stable: tools keep their slot while they're held,
    // new ones go at the end, gone ones leave a gap that closes up.
    std::vector<uint64_t> have;
    if (SceneNode* t = heldTool(r)) have.push_back(t->id);
    if (SceneNode* b = r ? backpackOf(scene, r) : nullptr)
        for (auto& c : b->children) if (c->isTool()) have.push_back(c->id);
    slots.erase(std::remove_if(slots.begin(), slots.end(), [&](uint64_t id) {
        return std::find(have.begin(), have.end(), id) == have.end();
    }), slots.end());
    for (uint64_t id : have)
        if (std::find(slots.begin(), slots.end(), id) == slots.end()) slots.push_back(id);
}

void Player::syncSlots() { syncSlotList(*m_scene, m_slots, root()); }

std::vector<SceneNode*> Player::tools() {
    syncSlots();
    std::vector<SceneNode*> out;
    for (uint64_t id : m_slots) if (SceneNode* t = m_scene->findById(id)) out.push_back(t);
    return out;
}

bool Player::give(SceneNode* tool) {
    if (!tool || !tool->isTool() || !root()) return false;
    if ((int)tools().size() >= kMaxTools) return false;
    SceneNode* bag = m_scene->findById(backpackId());
    if (!bag || tool->parent == bag) return false;
    auto owned = m_scene->detach(tool);
    if (!owned) return false;
    owned->transform = Transform{};
    bag->addChild(std::move(owned));
    m_scene->markDirty();
    syncSlots();
    return true;
}

void Player::equip(uint64_t toolId) {
    SceneNode* r = root();
    if (!r || m_dead) return;
    SceneNode* held = equippedTool();
    if (held && held->id == toolId) return;
    SceneNode* bag = m_scene->findById(backpackId());
    if (held) {   // put the one in your hand away first
        uint64_t id = held->id;
        auto owned = r->detachChild(held);
        owned->transform = Transform{};
        bag->addChild(std::move(owned));
        if (onToolEquip) onToolEquip(id, false);
    }
    if (toolId) {
        SceneNode* t = m_scene->findById(toolId);
        if (t && t->isTool() && t->parent != bag) give(t);   // (humanoid:EquipTool on one lying about, like Roblox)
        if (t && t->isTool() && t->parent == bag) {
            r->addChild(bag->detachChild(t));
            updateGrip();
            if (onToolEquip) onToolEquip(toolId, true);
        }
    }
    m_scene->markDirty();
}

void Player::toggleSlot(int slot) {
    auto list = tools();
    if (slot < 0 || slot >= (int)list.size()) return;
    SceneNode* held = equippedTool();
    equip(held == list[(size_t)slot] ? 0 : list[(size_t)slot]->id);
}

SceneNode* Player::layDown(Scene& scene, SceneNode* r) {
    SceneNode* held = heldTool(r);
    if (!r || !held) return nullptr;
    auto owned = r->detachChild(held);
    float yaw = glm::radians(r->transform.rotation.y);
    glm::vec3 front = r->transform.position + glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw)) * 2.5f;   // (facing = atan2(x, z))
    owned->transform = Transform{};
    owned->transform.position = glm::vec3(front.x, r->transform.position.y + 0.6f, front.z);
    owned->transform.rotation = glm::vec3(90.0f, r->transform.rotation.y, 0.0f);   // lying on its side
    return scene.insert(std::move(owned));
}

void Player::equipOn(Scene& scene, SceneNode* r, uint64_t toolId, const std::function<void(uint64_t, bool)>& events) {
    SceneNode* bag = backpackOf(scene, r);
    if (!bag) return;
    SceneNode* held = heldTool(r);
    if (held && held->id == toolId) return;
    if (held) {   // put the one in their hand away first
        const uint64_t id = held->id;
        auto owned = r->detachChild(held);
        owned->transform = Transform{};
        bag->addChild(std::move(owned));
        if (events) events(id, false);
    }
    if (SceneNode* t = toolId ? scene.findById(toolId) : nullptr; t && t->isTool() && t->parent == bag) {
        r->addChild(bag->detachChild(t));
        placeInHand(r, t);
        if (events) events(toolId, true);
    }
    scene.markDirty();
}

SceneNode* Player::drop() {
    SceneNode* held = equippedTool();
    if (!root() || !held || !held->canBeDropped) return nullptr;
    uint64_t id = held->id;
    SceneNode* raw = layDown(*m_scene, root());
    if (onToolEquip) onToolEquip(id, false);
    syncSlots();
    return raw;
}

void Player::clearTools() {
    std::vector<SceneNode*> gone = tools();
    for (SceneNode* t : gone) m_scene->removeNode(t);
    m_slots.clear();
}

void Player::updateGrip() {
    SceneNode* r = root();
    SceneNode* tool = equippedTool();
    SceneNode* arm = part("Right Arm");
    if (!r || !arm) return;
    SceneNode* weld = arm->findChild("RightGrip");
    if (!tool) {   // nothing in the hand: no RightGrip
        if (weld) m_scene->removeNode(weld);
        m_gripMade = 0;
        return;
    }
    SceneNode* handle = tool->findChild("Handle");
    if (!handle) { tool->transform = Transform{}; return; }
    // Like Roblox: the arm holds the tool through a weld called RightGrip (a script
    // can look for it; deleting it lets go of the tool, like in Roblox).
    if (!weld) {
        if (m_gripMade == tool->id) {   // it was there and a script deleted it: drop the tool
            m_gripMade = 0;
            const bool could = tool->canBeDropped;
            tool->canBeDropped = true;
            drop();
            tool->canBeDropped = could;
            return;
        }
        auto w = std::make_unique<SceneNode>("RightGrip", NodeKind::Constraint);
        w->constraintType = ConstraintType::Weld;
        w->enabled = false;   // (shows how they're joined; the hand itself moves the tool)
        w->visible = false;
        weld = arm->addChild(std::move(w));
        m_gripMade = tool->id;
    }
    weld->ref0 = arm->id;
    weld->ref1 = handle->id;
    placeInHand(r, tool);
}

void Player::placeInHand(SceneNode* r, SceneNode* tool) {
    SceneNode* arm = r ? r->findChild("Right Arm") : nullptr;
    SceneNode* handle = tool ? tool->findChild("Handle") : nullptr;
    if (!arm || !handle) { if (tool) tool->transform = Transform{}; return; }
    // Roblox's maths: Handle = RightArm * RightGrip.C0 * Tool.Grip:Inverse(), where C0
    // is the hand (the bottom of the arm) turned so +Y points the way the arm's front
    // faces. Our characters face +Z where Roblox's face -Z, hence the arm's -X / +Z / +Y.
    glm::mat4 a = arm->worldMatrix();
    glm::vec3 x = glm::normalize(glm::vec3(a[0])), y = glm::normalize(glm::vec3(a[1])), z = glm::normalize(glm::vec3(a[2]));
    float len = glm::length(glm::vec3(a[1]));
    glm::vec3 hand = glm::vec3(a[3]) - y * (len * 0.5f);
    glm::mat4 c0(1.0f);
    c0[0] = glm::vec4(-x, 0.0f);
    c0[1] = glm::vec4(z, 0.0f);
    c0[2] = glm::vec4(y, 0.0f);
    c0[3] = glm::vec4(hand, 1.0f);
    glm::mat4 gripM(tool->gripRot);
    gripM[3] = glm::vec4(tool->gripPos, 1.0f);
    glm::mat4 grip = c0 * glm::inverse(gripM);
    // Where the Handle sits inside the tool (without its size).
    Transform h = handle->transform;
    h.scale = glm::vec3(1.0f);
    glm::mat4 local = glm::inverse(r->worldMatrix()) * grip * glm::inverse(h.matrix());
    float rz, ry, rx;
    glm::extractEulerAngleZYX(local, rz, ry, rx);
    tool->transform.position = glm::vec3(local[3]);
    tool->transform.rotation = glm::degrees(glm::vec3(rx, ry, rz));
    tool->transform.scale = glm::vec3(1.0f);
}

bool Player::consumeDied() {
    bool d = m_diedFlag;
    m_diedFlag = false;
    return d;
}

namespace {
bool flagged(const SceneNode* n, const char* what) {
    if (std::find(n->tags.begin(), n->tags.end(), what) != n->tags.end()) return true;
    const Attribute* a = n->findAttribute(what);
    return a && ((a->type == Attribute::Bool && a->b) || (a->type == Attribute::Number && a->n != 0));
}
bool nameHas(const SceneNode* n, const char* word) { return n->name.find(word) != std::string::npos; }
bool insideBox(const AABB& b, const glm::vec3& p, float pad = 0.0f) {
    return p.x >= b.min.x - pad && p.x <= b.max.x + pad && p.y >= b.min.y - pad && p.y <= b.max.y + pad &&
           p.z >= b.min.z - pad && p.z <= b.max.z + pad;
}
} // namespace

bool Player::isClimbable(const SceneNode* n) {
    return n->isPart() && n->canCollide && (nameHas(n, "Truss") || nameHas(n, "Ladder") || flagged(n, "Climbable"));
}
bool Player::isSeat(const SceneNode* n) {
    return n->isPart() && (n->name == "Seat" || n->name == "VehicleSeat" || flagged(n, "Seat"));
}
bool Player::seatDisabled(const SceneNode* n) {
    const Attribute* a = n->findAttribute("Disabled");
    return a && a->type == Attribute::Bool && a->b;
}

// The top middle of a seat, and which way its front faces (flat).
static void seatFrame(const SceneNode* seat, glm::vec3& top, float& yawDeg) {
    const glm::mat4 m = seat->worldMatrix();
    top = glm::vec3(m * glm::vec4(0.0f, 0.5f, 0.0f, 1.0f));
    glm::vec3 front = -glm::vec3(m[2]);   // like Roblox: a seat faces its Front (-Z)
    front.y = 0.0f;
    yawDeg = glm::length(front) > 1e-4f ? glm::degrees(std::atan2(front.x, front.z)) : 0.0f;
}

void Player::sit(SceneNode* seat) {
    if (!seat || m_dead || !isSeat(seat) || seatDisabled(seat)) return;
    m_seatId = seat->id;
    m_velocity = glm::vec3(0.0f);
    m_climbing = m_swimming = false;
    footsteps(false, glm::vec3(0.0f));
}

void Player::sitDown() {
    if (m_dead || sitting() || m_climbing || m_swimming || tripped()) return;
    SceneNode* r = root();
    if (!r) return;
    if (!m_grounded) { m_sitPending = true; return; }
    m_sitPending = false;
    m_sitHere = true;
    m_sitSpot = r->transform.position - glm::vec3(0.0f, m_stepShown, 0.0f);
    m_velocity = glm::vec3(0.0f);
    stopEmote(0.2f);
    footsteps(false, glm::vec3(0.0f));
}

void Player::standUp(bool jumpOff) {
    m_sitPending = false;
    if (m_sitHere) {   // up off the floor
        m_sitHere = false;
        if (SceneNode* r = root()) r->transform.position = m_sitSpot;
        m_grounded = !jumpOff;
        if (jumpOff) { m_velocity.y = m_humanoid.launchSpeed(m_scene->world().gravity); m_groundId = 0; }
        return;
    }
    if (!m_seatId) return;
    SceneNode* r = root();
    SceneNode* seat = m_scene->findById(m_seatId);
    m_seatId = 0;
    m_seatCooldown = 1.0f;   // (or you'd sit straight back down)
    if (r && seat) {
        glm::vec3 top; float yaw;
        seatFrame(seat, top, yaw);
        r->transform.position = top + glm::vec3(0.0f, 0.05f, 0.0f);   // up on top of the seat
    }
    m_grounded = false;
    m_groundId = 0;
    if (jumpOff) m_velocity.y = m_humanoid.launchSpeed(m_scene->world().gravity);
}

void Player::sitStep(float dt, bool jump) {
    SceneNode* r = root();
    SceneNode* seat = m_scene->findById(m_seatId);
    if (!r || !seat || jump || seatDisabled(seat)) { standUp(jump); return; }
    glm::vec3 top; float yaw;
    seatFrame(seat, top, yaw);
    // The hips on the seat (the root is at the feet; standing, the hips are 1 up),
    // a little sunk in, facing the seat's front. The seat carries us if it moves.
    r->transform.position = top - glm::vec3(0.0f, 0.75f, 0.0f);
    r->transform.rotation.y = yaw;
    m_velocity = glm::vec3(0.0f);
    m_grounded = true;
    m_groundSpeed = 0.0f;
    animate(dt, 0.0f, true);
    footsteps(false, glm::vec3(0.0f));
    updateGrip();
}

void Player::sitHereStep(float dt, bool jump) {
    SceneNode* r = root();
    if (!r || jump) { standUp(jump); return; }
    // Sitting on something that moves (a boat, a train): ride along with it.
    if (m_groundId)
        if (SceneNode* g = m_scene->findById(m_groundId)) {
            const glm::mat4 change = g->worldMatrix() * glm::inverse(m_groundPrevM);
            m_sitSpot = glm::vec3(change * glm::vec4(m_sitSpot, 1.0f));
            r->transform.rotation.y += glm::degrees(std::atan2(change[2][0], change[2][2]));
            m_groundPrevM = g->worldMatrix();
        }
    // The hips on the floor, legs out in front (the feet would be under the floor).
    r->transform.position = m_sitSpot - glm::vec3(0.0f, 0.75f, 0.0f);
    m_velocity = glm::vec3(0.0f);
    m_grounded = true;
    m_groundSpeed = 0.0f;
    animate(dt, 0.0f, true);
    footsteps(false, glm::vec3(0.0f));
    updateGrip();
}

void Player::characterMovers(float dt) {
    using namespace Movers;
    m_gyro = false;
    SceneNode* r = root();
    if (!r || dt <= 0.0f) return;
    std::vector<SceneNode*> movers;
    float mass = 0.0f;
    std::vector<SceneNode*> stack{r};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->isMover() && n->enabled) movers.push_back(n);
        if (n->isPart()) mass += Physics::densityOf(n) * n->transform.scale.x * n->transform.scale.y * n->transform.scale.z;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    if (movers.empty()) return;
    const float M = std::max(mass, 0.5f), I = M;   // (a character spins about as easily as it moves)
    const glm::vec3 feet = r->transform.position;
    for (SceneNode* mn : movers) {
        const MoverProps& m = mn->mover;
        SceneNode* part = mn->parent;
        SceneNode* att0 = nullptr, *att1 = nullptr;
        if (!isBodyMover(m.type)) {
            att0 = mn->ref0 ? m_scene->findById(mn->ref0) : nullptr;
            att1 = mn->ref1 ? m_scene->findById(mn->ref1) : nullptr;
            if (att0 && !att0->isAttachment()) att0 = nullptr;
            if (att1 && !att1->isAttachment()) att1 = nullptr;
            if (att0) part = att0->parent;
        }
        if (!part) continue;
        const glm::vec3 at = att0 ? glm::vec3(att0->worldMatrix()[3]) : glm::vec3(part->worldMatrix()[3]);
        const glm::quat rot = att0 ? worldRot(att0) : worldRot(part);
        const glm::vec3 lift = at - feet;   // (we steer the feet; the mover sits higher up the body)
        switch (m.type) {
            case MoverType::BodyForce:   m_velocity += m.value * dt / M; break;
            case MoverType::BodyThrust:  m_velocity += (worldRot(part) * m.value) * dt / M; break;
            case MoverType::VectorForce: m_velocity += (m.relativeToAttachment ? rot * m.value : m.value) * dt / M; break;
            case MoverType::BodyVelocity:
                m_velocity += clampAxes(m.value - m_velocity, m.maxAxes * dt / M);
                break;
            case MoverType::LinearVelocity: {
                const glm::vec3 want = m.relativeToAttachment ? rot * m.value : m.value;
                m_velocity += clampLength(want - m_velocity, m.maxForce * dt / M);
                break;
            }
            case MoverType::BodyPosition: {
                const glm::vec3 x = m.value - at;
                glm::vec3 dv;
                for (int i = 0; i < 3; ++i) dv[i] = springDv(x[i], m_velocity[i], m.p, m.d, M, dt);
                m_velocity += clampAxes(dv, m.maxAxes * dt / M);
                break;
            }
            case MoverType::AlignPosition: {
                const glm::vec3 target = att1 ? glm::vec3(att1->worldMatrix()[3]) : m.value;
                glm::vec3 want = (target - at) * (m.rigid ? 0.5f / dt : m.responsiveness);
                if (m.maxVelocity > 0.0f && !m.rigid) want = clampLength(want, m.maxVelocity);
                m_velocity += clampLength(want - m_velocity, (m.rigid ? 1e30f : m.maxForce) * dt / M);
                break;
            }
            case MoverType::BodyGyro:
            case MoverType::AlignOrientation: {
                // A character stays upright-ish: it turns to face the way the target faces
                // and leans forward or back with it.
                const glm::quat target = m.type == MoverType::AlignOrientation && att1 ? worldRot(att1) : eulerQuat(m.rotation);
                const glm::vec3 f = target * glm::vec3(0.0f, 0.0f, -1.0f);   // (its LookVector)
                const bool yaw = m.type == MoverType::AlignOrientation || m.maxAxes.y > 0.0f;
                const bool tilt = m.type == MoverType::AlignOrientation || m.maxAxes.x > 0.0f || m.maxAxes.z > 0.0f;
                const float rate = std::min(1.0f, dt * (m.type == MoverType::BodyGyro ? std::sqrt(std::max(m.p, 0.0f) / M) : m.responsiveness));
                if (yaw && f.x * f.x + f.z * f.z > 1e-6f) {
                    const float want = glm::degrees(std::atan2(f.x, f.z)), cur = r->transform.rotation.y;
                    r->transform.rotation.y = cur + (std::fmod(want - cur + 540.0f, 360.0f) - 180.0f) * rate;
                }
                if (tilt) { m_gyro = true; m_gyroTilt = glm::degrees(std::asin(std::clamp(-f.y, -1.0f, 1.0f))); }
                break;
            }
            case MoverType::BodyAngularVelocity:
                if (m.maxAxes.y > 0.0f) r->transform.rotation.y += glm::degrees(m.value.y) * dt;
                break;
            case MoverType::AngularVelocity:
                r->transform.rotation.y += glm::degrees((m.relativeToAttachment ? rot * m.value : m.value).y) * dt;
                break;
            case MoverType::Torque: break;   // (a character doesn't tumble from a push)
        }
        (void)lift;
    }
    if (m_velocity.y > 0.0f) m_grounded = false;   // lifted off the ground
}

bool Player::isWater(const SceneNode* n) {
    return n->isPart() && !n->canCollide && (n->name == "Water" || nameHas(n, "Water") || flagged(n, "Water"));
}

bool Player::waterAt(Scene& scene, const glm::vec3& p, float* top, glm::vec3* flow, glm::vec3* color) {
    WaterSystem& waves = scene.water();
    bool found = false;
    float best = -1e9f;
    scene.forEach([&](SceneNode* n) {
        if (!isWater(n) || scene.isCharacterPart(n)) return;
        // Water slides and sloping rivers: the real tipped-over box, with its sloping top.
        if (WaterSystem::tilted(n)) {
            if (!WaterSystem::insideTilted(n, p)) return;
            const float t = WaterSystem::tiltedSurface(n, p.x, p.z);
            if (t < best) return;
            found = true; best = t;
            if (flow) { const Attribute* a = n->findAttribute("Flow"); *flow = a && a->type == Attribute::Vector3 ? a->v : glm::vec3(0.0f); }
            if (color) *color = n->color;
            return;
        }
        const AABB b = Physics::worldBounds(n);
        // While playing, the surface moves with the waves.
        const float t = waves.active() ? waves.surfaceOf(n, p.x, p.z) : b.max.y;
        AABB wetBox = b;
        wetBox.max.y = t;
        if (!insideBox(wetBox, p) || t < best) return;
        found = true; best = t;
        if (flow) { const WaterSystem::Body* wb = waves.find(n->id); *flow = wb ? waves.flowAt(*wb, p.x, p.z) : glm::vec3(0.0f); }
        if (color) *color = n->color;
    });
    // Flowing water from a WaterSource (floods, rivers).
    if (!found && waves.active()) {
        float t;
        glm::vec3 f;
        if (waves.at(p, &t, &f)) { found = true; best = t; if (flow) *flow = f; if (color) *color = glm::vec3(0.2f, 0.45f, 0.7f); }
    }
    if (found && top) *top = best;
    return found;
}

void Player::update(float dt, const glm::vec3& moveIn, bool jumpIn, Physics& physics) {
    SceneNode* r = root();
    if (!r) return;
    // Knocked over (tripped, flung) or PlatformStand: the controls do nothing.
    const bool knocked = tripped();
    // (No keys pushed: a script's humanoid:Move(direction) walks us instead.)
    const glm::vec3 moveDir = knocked ? glm::vec3(0.0f) : glm::length(moveIn) > 1e-4f ? moveIn : m_scriptMove;
    const bool jump = knocked ? false : (jumpIn || m_scriptJump);
    m_scriptJump = false;
    if (knocked && sitting()) standUp(false);

    if (m_dead) { updateDeath(dt, physics); return; }
    m_aliveTime += dt;

    if (m_spawnFF && (m_spawnFFTime -= dt) <= 0.0f) {
        if (SceneNode* ff = m_scene->findById(m_spawnFF)) m_scene->removeNode(ff);
        m_spawnFF = 0;
    }

    // Took damage since last frame (scripts, traps, explosions)?
    if (m_humanoid.health < m_lastHealth - 0.5f) {
        bleed(m_lastHealth - m_humanoid.health);
        if (m_humanoid.health > 0.0f) {
            glm::vec3 at = r->transform.position + glm::vec3(0, 1.5f, 0);
            Audio::play("hit", 0.6f, 1.0f, false, &at);
        }
    }
    m_lastHealth = m_humanoid.health;
    if (m_humanoid.health <= 0.0f) { startDeath(); return; }

    // Knocked over as a ragdoll: the body's muscles try to catch the fall; once it
    // has come to rest (and the trip time is up) we get back up.
    if (m_ragdoll.active()) {
        m_ragdoll.update(dt, *m_scene, physics);
        m_knockTime += dt;
        if (m_ragdoll.lying()) m_tripTime = std::max(0.0f, m_tripTime - dt);   // (the clock runs once we're down)
        if (m_ragdoll.center().y < m_scene->world().fallenPartsHeight) m_humanoid.health = 0.0f;
        else if ((m_tripTime <= 0.0f && m_ragdoll.settled() && !m_humanoid.platformStand) || m_knockTime > 8.0f) getUp();
        return;
    }

    // Sitting: stay on the seat until you jump.
    m_seatCooldown = std::max(0.0f, m_seatCooldown - dt);
    if (m_seatId) { sitStep(dt, jump); if (m_seatId) return; }
    if (m_sitHere) { sitHereStep(dt, jump); if (m_sitHere) return; }

    glm::vec3 pos = r->transform.position;
    // Where the physics really has us: take away the step smoothing we showed last
    // frame (unless something else moved us since: a teleport, a seat, respawning).
    if (m_stepShown != 0.0f) {
        if (glm::length(pos - m_shownAt) < 1e-4f) pos.y -= m_stepShown;
        else m_stepOffset = 0.0f;
        m_stepShown = 0.0f;
    }

    // Ride moving platforms: whatever we stood on last frame carries us, sliding
    // and spinning (a turntable turns you with it), and we keep its speed.
    m_platformVel = glm::vec3(0.0f);
    if (m_groundId) {
        if (SceneNode* g = m_scene->findById(m_groundId)) {
            glm::mat4 change = g->worldMatrix() * glm::inverse(m_groundPrevM);
            glm::vec3 carried = glm::vec3(change * glm::vec4(pos, 1.0f));
            if (dt > 0.0f) m_platformVel = (carried - pos) / dt;
            pos = carried;
            r->transform.rotation.y += glm::degrees(std::atan2(change[2][0], change[2][2]));
        }
    }

    // Horizontal movement.
    glm::vec3 horiz = {moveDir.x, 0.0f, moveDir.z};
    float len = glm::length(horiz);
    bool moving = len > 1e-4f;
    if (moving) {
        horiz /= len;
        float amount = std::min(1.0f, len);   // a half-pushed thumbstick walks slower
        if (m_humanoid.autoRotate && !m_faceLock) {
            // Turn smoothly towards the direction of travel (shortest way round).
            float target = glm::degrees(std::atan2(horiz.x, horiz.z));
            float cur    = r->transform.rotation.y;
            float diff   = std::fmod(target - cur + 540.0f, 360.0f) - 180.0f;
            r->transform.rotation.y = cur + diff * std::min(1.0f, dt * 14.0f);
        }
        horiz *= amount;
    }

    // First person: the body faces wherever the camera looks.
    if (m_faceLock) { r->transform.rotation.y = m_faceYaw; m_faceLock = false; }

    // Trusses / ladders in front of us, and water around us.
    const float yaw = glm::radians(r->transform.rotation.y);
    const glm::vec3 facing(std::sin(yaw), 0.0f, std::cos(yaw));
    const glm::vec3 ahead = moving ? glm::normalize(glm::vec3(horiz.x, 0.0f, horiz.z)) : facing;
    bool truss = false, water = false;
    float waterTop = -1e9f;
    glm::vec3 current(0.0f);   // water flowing along (a "Flow" attribute)
    WaterSystem& waves = m_scene->water();
    m_climbCooldown = std::max(0.0f, m_climbCooldown - dt);
    const CollisionGroups& groups = m_scene->world().collisionGroups;
    const std::string& myGroup = Physics::characterGroup(r);
    m_scene->forEach([&](SceneNode* n) {
        if (truss || !n->isPart() || m_scene->isCharacterPart(n)) return;
        if (m_climbCooldown > 0.0f || !isClimbable(n)) return;
        if (!groups.collides(myGroup, n->collisionGroup)) return;   // (a ladder our collision group passes through)
        AABB b = Physics::worldBounds(n);
        for (float h : {0.4f, 1.3f, 2.2f})
            if (insideBox(b, pos + glm::vec3(0.0f, h, 0.0f) + ahead * 0.75f, 0.05f)) { truss = true; break; }
    });
    // Water parts, waves and floods: is our chest in it?
    water = waterAt(*m_scene, pos + glm::vec3(0.0f, 1.2f, 0.0f), &waterTop, &current);
    // Real liquid (FluidSource): a stream carries you along; deep enough, you swim in it.
    bool stream = false;
    glm::vec3 streamVel(0.0f);
    if (waves.liquid().count()) {
        float top;
        const int n = waves.liquid().sample(pos + glm::vec3(0.0f, 0.5f, 0.0f), 1.4f, &streamVel, &top);
        if (n >= 5) {
            stream = true;
            m_wet = 2.0f;
            // (Deep enough to swim: over the chest. Already swimming: deep enough to float in.)
            if (n >= 25 && top > pos.y + (m_swimming ? kFloatHeight - 0.3f : 1.2f) && !water) {
                water = true; waterTop = top; current = streamVel;
            }
        }
    }
    // Climb when walking into a truss (or when already on one and still touching it).
    const bool wasClimbing = m_climbing;
    m_climbing = truss && (moving || (m_climbing && !m_grounded));
    m_swimming = water && !m_climbing;
    if (!m_swimming) m_underwater = false;
    // Out of the water you drip for a while, leaving wet patches where you walk.
    if (m_swimming) m_drip = 1.0f;
    else if (m_drip > 0.0f) {
        m_drip = std::max(0.0f, m_drip - dt / 20.0f);
        if ((m_dripTimer -= dt) <= 0.0f) { m_dripTimer = 0.3f; waves.addWetSpot(pos, 0.7f + m_drip * 0.5f); }
    }
    float speed = m_humanoid.walkSpeed * (m_swimming ? 0.75f : 1.0f);

    // Gravity + jumping.
    if (m_climbing) {
        if (jump && wasClimbing && !m_grounded) {   // leap off backwards
            m_climbing = false;
            m_climbCooldown = 0.35f;
            m_velocity = -ahead * 10.0f + glm::vec3(0.0f, m_humanoid.launchSpeed(m_scene->world().gravity) * 0.7f, 0.0f);
        } else {
            m_velocity.y = moving ? m_humanoid.walkSpeed * 0.9f : 0.0f;   // stop pushing = hang on
            m_climbPhase += dt * (moving ? 9.0f : 0.0f);
            horiz *= 0.3f;   // (the truss is in the way anyway)
        }
    } else if (m_swimming) {
        // Which way we want to go up or down: Space swims up, C / Ctrl dives, and
        // swimming forward goes where the camera looks (look down to dive, up to rise).
        float want = 0.0f;
        if (jump) want = 1.0f;
        else if (m_swimDown) want = -1.0f;
        else if (moving && std::abs(m_swimLook) > 0.2f) want = std::clamp(m_swimLook * 1.8f, -1.0f, 1.0f);
        // How far under the surface the shoulders are (negative = above it).
        const float depth = waterTop - (pos.y + kFloatHeight);
        if (want != 0.0f) {
            // Swimming up or down on purpose.
            const float target = want * m_humanoid.walkSpeed * 0.7f;
            m_velocity.y += (target - m_velocity.y) * std::min(1.0f, 4.0f * dt);
            // At the surface, Space hops out (onto the side of a pool).
            if (jump && depth < 0.4f) m_velocity.y = std::max(m_velocity.y, m_humanoid.launchSpeed(m_scene->world().gravity) * 0.8f);
        } else {
            // Floating: people are a bit lighter than water, so we bob up to the surface
            // and float there with the head out, rising and falling with the waves.
            const float bob = 0.06f * std::sin(m_bobPhase);
            const float up = std::clamp((depth + bob) * 7.0f, -m_scene->world().gravity, 10.0f);
            m_velocity.y += (up - 2.8f * m_velocity.y) * dt;
            m_velocity.y = std::clamp(m_velocity.y, -12.0f, 3.5f + std::max(0.0f, depth) * 0.5f);
        }
        m_bobPhase += dt * 2.2f;
        m_underwater = depth > 0.5f;
        m_climbPhase += dt * (moving || want != 0.0f ? 6.0f : 2.5f);
    } else {
        if (m_grounded && jump) {
            // Jumping off something moving keeps its speed (off a train, you fly forward).
            m_velocity.x += m_platformVel.x;
            m_velocity.z += m_platformVel.z;
            m_velocity.y = m_humanoid.launchSpeed(m_scene->world().gravity) + std::max(0.0f, m_platformVel.y);
            m_grounded = false;
            glm::vec3 at = pos + glm::vec3(0, 1, 0);
            Audio::play("jump", 0.35f, 1.0f, false, &at);
        }
        m_velocity.y -= m_scene->world().gravity * dt;
    }
    characterMovers(dt);

    // Wet = slippery: in a stream, or just out of one (the slide is still wet where it went).
    m_wet = std::max(0.0f, m_wet - dt);
    bool wet = stream || m_wet > 0.0f;
    if (!wet && m_grounded && m_groundId) {
        // Standing on something the liquid ran over lately, or something slippery (tag
        // "Slippery": a water slide, ice).
        if (waves.liquid().isWet(m_groundId)) wet = true;
        else if (SceneNode* g = m_scene->findById(m_groundId))
            wet = std::find(g->tags.begin(), g->tags.end(), "Slippery") != g->tags.end();
    }
    float drag = std::max(0.0f, 1.0f - (m_grounded ? (wet ? 0.3f : 8.0f) : 0.8f) * dt);
    // (Before working out this frame's move, so the push counts straight away.)
    const bool sliding = wet && !m_swimming && !m_climbing;
    if (sliding) {
        // A wet slope is almost frictionless, so gravity slides you down it (a water
        // slide) ...
        if (m_grounded && m_groundId)
            if (SceneNode* ground = m_scene->findById(m_groundId)) {
                glm::vec3 n = glm::normalize(glm::vec3(ground->worldMatrix()[1]));
                if (n.y < 0.0f) n = -n;
                if (n.y > 0.5f && n.y < 0.9995f) {
                    const glm::vec3 g(0.0f, -m_scene->world().gravity, 0.0f);
                    const glm::vec3 downhill = g - glm::dot(g, n) * n;
                    m_velocity.x += downhill.x * dt;
                    m_velocity.z += downhill.z * dt;
                }
            }
        // ... and water going faster than you carries you along (a river, a slide's
        // gush). Slower water doesn't hold you back: it just makes things slippery.
        if (glm::length(glm::vec2(streamVel.x, streamVel.z)) > glm::length(glm::vec2(m_velocity.x, m_velocity.z))) {
            const float k = std::min(1.0f, 0.8f * dt);
            m_velocity.x += (streamVel.x - m_velocity.x) * k;
            m_velocity.z += (streamVel.z - m_velocity.z) * k;
        }
        const float sp = glm::length(glm::vec2(m_velocity.x, m_velocity.z));
        if (sp > 40.0f) { m_velocity.x *= 40.0f / sp; m_velocity.z *= 40.0f / sp; }
    }
    // Walking plus any leftover push (from jump pads, etc.), which fades out.
    glm::vec3 delta = horiz * speed * dt + current * (m_swimming ? dt : 0.0f);
    delta.x += m_velocity.x * dt;
    delta.z += m_velocity.z * dt;
    delta.y = m_velocity.y * dt;
    m_velocity.x *= drag;
    m_velocity.z *= drag;

    Physics::MoveResult res = physics.moveCharacter(pos, delta, m_grounded, r->transform.rotation.y, m_rootId, physics.groupOf(r));
    if (sliding && dt > 0.0f) {
        // Sliding into a wall takes that speed away, so you follow the bends.
        const glm::vec3 moved = (res.position - pos) / dt - horiz * speed;
        m_velocity.x = moved.x;
        m_velocity.z = moved.z;
    }
    // Walking into loose parts pushes them (heavier = harder, handled by the solver).
    for (auto& [node, dir] : res.pushed) {
        glm::vec3 want = dir * m_humanoid.walkSpeed * 0.9f;
        if (glm::dot(node->velocity, dir) < glm::dot(want, dir)) node->velocity += dir * (m_humanoid.walkSpeed * 0.15f);
        Physics::wake(node);
    }

    // Fall damage: landing hard hurts, landing very hard is fatal (and messy).
    const WorldSettings& world = m_scene->world();
    float impact = -m_velocity.y;
    if (res.grounded && !m_grounded && world.fallDamage && impact > world.fallDamageSpeed &&
        !hasForceField() && !m_swimming) {
        float over = impact - world.fallDamageSpeed;
        float damage = over * 7.0f * world.fallDamageScale;
        m_humanoid.health = std::max(0.0f, m_humanoid.health - damage);
        if (m_humanoid.health <= 0.0f)
            kill(std::clamp(over / 15.0f, 0.0f, 1.0f), glm::vec3(m_velocity.x, 2.0f, m_velocity.z));
    }
    // Landing really hard (and living): you trip and go down for a moment.
    if (res.grounded && !m_grounded && !m_swimming && m_humanoid.health > 0.0f &&
        impact > std::max(45.0f, world.fallDamageSpeed * 1.6f))
        trip(1.0f, glm::vec3((m_velocity.x + m_velocity.z >= 0.0f ? 1.0f : -1.0f) * impact * 3.0f, 0, 0));
    m_grounded = res.grounded;
    if (res.grounded && m_velocity.y < 0.0f) m_velocity.y = 0.0f;
    const bool sitNow = m_sitPending && m_grounded;
    if (res.hitCeiling && m_velocity.y > 0.0f) m_velocity.y = 0.0f;
    m_groundId = res.groundId;
    if (m_groundId)
        if (SceneNode* g = m_scene->findById(m_groundId))
            m_groundPrevM = g->worldMatrix();

    // Loose parts that crash into us knock us back (a rolling boulder, a swinging
    // wrecking ball), harder the heavier and faster they are.
    for (const Physics::Shove& s : physics.shoves(res.position, r->transform.rotation.y, kBodyMass, physics.groupOf(r))) {
        glm::vec3 dir(s.dir.x, 0.0f, s.dir.z);
        float l = glm::length(dir);
        if (l < 1e-3f) continue;
        dir /= l;
        float have = glm::dot(glm::vec3(m_velocity.x, 0.0f, m_velocity.z), dir);
        if (s.speed > have) m_velocity += dir * (s.speed - have);
        if (s.speed > 6.0f && !m_climbing) {   // a big hit throws you off your feet
            m_velocity.y = std::max(m_velocity.y, s.speed * 0.35f);
            m_grounded = false;
            m_groundId = 0;
        }
        if (s.speed > 12.0f) {   // ...and a really big one knocks you flat and flings you spinning
            const float fwd = glm::dot(dir, glm::vec3(std::sin(glm::radians(r->transform.rotation.y)), 0.0f,
                                                      std::cos(glm::radians(r->transform.rotation.y))));
            trip(1.2f + s.speed * 0.04f, glm::vec3((fwd >= 0.0f ? 1.0f : -1.0f) * s.speed * 22.0f, 0, 0));
        }
    }

    // Smooth steps: going up (or down) a step, the body glides there in about a
    // tenth of a second instead of jumping, like Roblox. Only what you see: the
    // physics (and everyone's collisions) still use the real feet.
    m_stepOffset = std::clamp(m_stepOffset - res.stepped + res.dropped, -1.0f, 1.0f);
    m_stepOffset *= std::exp(-dt * 14.0f);
    if (std::abs(m_stepOffset) < 0.002f) m_stepOffset = 0.0f;
    m_stepShown = m_stepOffset;
    r->transform.position = res.position + glm::vec3(0.0f, m_stepShown, 0.0f);
    m_shownAt = r->transform.position;

    // Fell off the world.
    if (res.position.y < m_scene->world().fallenPartsHeight) m_humanoid.health = 0.0f;

    if (sitNow) sitDown();   // (Sit = true in the air: down we go on landing)

    // Bumped into a free seat (walked into it or landed on it): sit down, like Roblox.
    if (m_seatCooldown <= 0.0f && !m_swimming && !m_climbing && !jump) {
        const AABB body = Physics::characterBox(res.position);
        SceneNode* found = nullptr;
        m_scene->forEach([&](SceneNode* n) {
            if (found || !isSeat(n) || seatDisabled(n) || m_scene->isCharacterPart(n)) return;
            AABB b = Physics::worldBounds(n);
            const float pad = 0.08f;
            if (body.max.x > b.min.x - pad && body.min.x < b.max.x + pad && body.max.y > b.min.y - pad &&
                body.min.y < b.max.y + pad && body.max.z > b.min.z - pad && body.min.z < b.max.z + pad)
                found = n;
        });
        if (found) sit(found);
    }

    // How fast we really moved (not how hard the keys are pushed): walking into a
    // wall doesn't run on the spot, and a slower WalkSpeed takes slower steps.
    const float moved = dt > 0.0f ? glm::length(glm::vec2(res.position.x - pos.x, res.position.z - pos.z)) / dt : 0.0f;
    m_groundSpeed = approach(m_groundSpeed, moving ? std::min(moved, m_humanoid.walkSpeed * 2.0f + 2.0f) : 0.0f, 12.0f, dt);
    // Falling over and getting back up. The body turns about the feet (pitch):
    // flung, it spins freely; on the ground it settles flat; then stands back up.
    if (m_gyro) {   // a BodyGyro / AlignOrientation holds us at its lean (flying), even PlatformStanding
        m_tilt = std::remainder(m_tilt, 360.0f);
        m_tilt += (m_gyroTilt - m_tilt) * std::min(1.0f, dt * 8.0f);
        m_tiltVel = 0.0f;
        m_getUp = 0.0f;
    } else if (knocked) {
        if (m_grounded) m_tripTime = std::max(0.0f, m_tripTime - dt);   // (the clock runs once you've landed)
        if (std::abs(m_tilt) < 1.0f && std::abs(m_tiltVel) < 1.0f) m_tiltVel = 90.0f;   // PlatformStand: topple forward
        m_tilt = std::remainder(m_tilt, 360.0f);
        if (!m_grounded) {
            m_tilt += m_tiltVel * dt;
            m_tiltVel *= std::exp(-0.4f * dt);
        } else {
            const float target = m_tilt >= 0.0f ? 90.0f : -90.0f;
            m_tiltVel += (target - m_tilt) * 40.0f * dt;
            m_tiltVel *= std::exp(-7.0f * dt);
            m_tilt += m_tiltVel * dt;
        }
        m_getUp = 0.0f;
        stopEmote(0.2f);
    } else if (m_tilt != 0.0f) {
        m_tilt = std::remainder(m_tilt, 360.0f);
        m_getUp += dt;   // GettingUp
        const float step = 240.0f * dt;
        m_tilt = std::abs(m_tilt) <= step ? 0.0f : m_tilt - step * (m_tilt > 0.0f ? 1.0f : -1.0f);
        m_tiltVel = 0.0f;
        if (m_tilt == 0.0f) m_getUp = 0.0f;
    }
    r->transform.rotation.x = m_tilt;
    r->transform.rotation.z = 0.0f;
    m_wasTripped = knocked;

    animate(dt, m_groundSpeed, m_grounded);
    footsteps(m_groundSpeed > 0.5f && m_grounded && !m_swimming && !m_climbing, res.position);
    if (m_scene->goreEnabled()) m_scene->particles().walk(m_feet, m_scene->goreKind(), res.position, m_grounded && !m_swimming);
    updateGrip();
}

void Player::footsteps(bool running, const glm::vec3& at) {
    if (!running) {
        if (m_stepSound) Audio::stop(m_stepSound);
        m_stepSound = 0;
        return;
    }
    glm::vec3 feet = at + glm::vec3(0.0f, 0.5f, 0.0f);
    float pitch = std::clamp(m_humanoid.walkSpeed / 6.0f, 0.6f, 1.6f);   // faster walkers step faster (6 = normal)
    if (m_stepSound && Audio::isPlaying(m_stepSound)) {
        Audio::setPosition(m_stepSound, feet);
        Audio::setPitch(m_stepSound, pitch);
        return;
    }
    m_stepSound = Audio::play("footsteps", 0.35f, pitch, true, &feet);
}

bool Player::isLimb(const SceneNode* p) {
    for (const char* n : kLimbs) if (p->name == n) return true;
    return false;
}

bool Player::drivesPart(const SceneNode* p) const {
    for (const char* n : kLimbs) if (p->name == n) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Animation: the classic (2011) set, like Roblox's old Animate script
// ---------------------------------------------------------------------------

namespace {
// A limb's pose: pitch swings it forward (negative) / back, `out` lifts it out to the side.
struct LimbPose { float pitch = 0.0f, out = 0.0f; };
using BodyPose = std::array<LimbPose, 4>;   // Left Arm, Right Arm, Left Leg, Right Leg

BodyPose mix(const BodyPose& a, const BodyPose& b, float t) {
    BodyPose r;
    for (int i = 0; i < 4; ++i) r[i] = {a[i].pitch + (b[i].pitch - a[i].pitch) * t, a[i].out + (b[i].out - a[i].out) * t};
    return r;
}

const char* const kAnimNames[] = {"idle", "walk", "run", "jump", "fall", "climb", "sit", "toolnone",
                                  "dance", "dance2", "dance3", "laugh", "cheer", "wave", "point", "swim"};
} // namespace

bool Player::playEmote(const std::string& name) {
    const std::string e = Emotes::normalize(name);
    if (e == "sit") {   // sit down right here (like Humanoid.Sit = true)
        if (m_dead || sitting() || m_climbing || m_swimming || tripped() || !m_grounded) return false;
        sitDown();
        return true;
    }
    // The built-in emotes, then the ones you equipped from the catalog.
    std::shared_ptr<const Anim::Clip> clip;
    std::string title = e;
    if (const Emotes::Emote* b = Emotes::findBuiltIn(e)) { clip = b->clip; title = b->title; }
    else
        for (const Emotes::Slot& s : Emotes::mine().slots)
            if (s.clip && Emotes::normalize(s.name) == e) { clip = s.clip; title = s.name; break; }
    if (!clip || clip->keys.empty()) return false;
    if (m_dead || sitting() || m_climbing || m_swimming || tripped() || !m_grounded) return false;
    stopEmote(0.15f);
    if (equippedTool()) equip(0);   // (an emote needs both hands: the tool goes back in the backpack)
    m_emote = e;
    m_emoteTime = 0.0f;
    m_emoteLength = clip->loop ? 0.0f : clip->length();
    // It plays like a script's AnimationTrack (so everyone sees it, and scripts can find it in
    // Animator:GetPlayingAnimationTracks()), unless the game's Animate script has its own.
    if (!hasCustomAnimation(e.c_str())) {
        Anim::Animator& an = m_scene->animator();
        m_emoteTrack = an.loadClip(m_rootId, title, *clip);
        an.play(m_emoteTrack, 0.2f, 1.0f, 1.0f);
    }
    return true;
}

void Player::stopEmote(float fade) {
    m_emote.clear();
    if (!m_emoteTrack) return;
    Anim::Animator& an = m_scene->animator();
    if (an.valid(m_emoteTrack)) an.stop(m_emoteTrack, fade);
    m_fadingTracks.push_back(m_emoteTrack);   // forgotten once it has faded out
    m_emoteTrack = 0;
}

void Player::trip(float seconds, const glm::vec3& spin) {
    if (m_dead) return;
    m_tripTime = std::max(0.0f, seconds);
    if (seconds > 0.0f) {
        m_emote.clear();
        if (sitting()) standUp(false);
        m_tiltVel += spin.x;   // (pitch, degrees / second)
        if (std::abs(m_tiltVel) < 60.0f) m_tiltVel = m_tiltVel < 0.0f ? -120.0f : 120.0f;
        if (canKnockdown()) startKnockdown();
    }
}

bool Player::canKnockdown() const {
    const WorldSettings& w = m_scene->world();
    return w.deathStyle == DeathStyle::Ragdoll && w.knockdowns && !m_dead && !m_ragdoll.active() &&
           !m_gyro && !m_swimming && !m_climbing && root() && part("Torso") && part("Head");
}

// Thrown off our feet: the body becomes an (active) ragdoll until it comes to rest.
void Player::startKnockdown() {
    SceneNode* r = root();
    if (!r) return;
    SceneNode* tool = equippedTool();
    m_knockTool = tool ? tool->id : 0;
    if (m_knockTool) equip(0);   // (it'd float where the hand was; we pick it back up after)
    footsteps(false, glm::vec3(0.0f));
    stopEmote(0.0f);
    m_knockTime = 0.0f;
    m_ragdoll.start(*m_scene, r, m_velocity * 0.7f, m_velocity * 0.3f, 0.0f, true);
}

// Back on our feet from lying in a heap: the character stands where the body lay,
// facing the way it fell, and does the usual getting-up (from flat on the floor).
void Player::getUp() {
    SceneNode* r = root();
    if (!r) { m_ragdoll.stop(); return; }
    const glm::vec3 c = m_ragdoll.center();
    const glm::mat3 f = m_ragdoll.bodyFrame();
    // Face down = fell forwards (the head is in front of the feet); face up = fell backwards.
    const float forwards = f[2].y < 0.0f ? 1.0f : -1.0f;
    glm::vec3 fwd = f[1] * forwards;
    fwd.y = 0.0f;
    if (glm::length(fwd) < 0.2f) { fwd = glm::vec3(f[2].x, 0.0f, f[2].z); }
    fwd = glm::length(fwd) > 1e-4f ? glm::normalize(fwd) : glm::vec3(0, 0, 1);
    float dist = 0.0f;
    const glm::vec3 from = c + glm::vec3(0.0f, 0.5f, 0.0f);
    glm::vec3 feet = c - fwd * 1.3f;   // (lying flat, the feet are behind the middle of the body)
    if (Physics::raycast(*m_scene, from, glm::vec3(0, -1, 0), &dist, r) && dist < 6.0f) feet.y = from.y - dist;
    else feet.y = c.y - 0.6f;

    m_ragdoll.release(*m_scene);
    for (auto& ch : r->children) {
        auto it = m_rest.find(ch->id);
        if (it != m_rest.end()) ch->transform = it->second;
    }
    r->transform = m_rootRest;
    r->transform.position = feet;
    r->transform.rotation.y = glm::degrees(std::atan2(fwd.x, fwd.z));
    m_tilt = 90.0f * forwards;   // lying flat: the usual getting-up takes it from here
    m_tiltVel = 0.0f;
    m_tripTime = 0.0f;
    m_getUp = 0.0f;
    m_velocity = glm::vec3(0.0f);
    m_grounded = false;
    m_stepOffset = m_stepShown = 0.0f;
    m_knockTime = 0.0f;
    if (m_knockTool) equip(m_knockTool);
    m_knockTool = 0;
}

const char* Player::stateName() const {
    if (m_dead) return "Dead";
    if (m_humanoid.platformStand) return "PlatformStanding";
    if (m_tripTime > 0.0f) return "FallingDown";
    if (m_getUp > 0.0f) return "GettingUp";
    if (sitting()) return "Seated";
    if (m_climbing) return "Climbing";
    if (m_swimming) return "Swimming";
    if (!m_grounded) return m_velocity.y > 0.0f ? "Jumping" : "Freefall";
    return "Running";
}

void Player::addAnimateScript(Scene& scene, SceneNode* rig) {
    if (!rig || rig->findChild("Animate")) return;
    auto script = std::make_unique<SceneNode>("Animate", NodeKind::Script);
    script->source =
        "-- Animate: your character's animations, the classic (2011) way.\n"
        "-- The engine plays them for you: idle, walk, run, jump, fall, climb, sit,\n"
        "-- toolnone (holding a tool), swim, and the emotes dance, dance2, dance3,\n"
        "-- laugh, cheer, wave and point (say \"/e dance\" in chat, or call\n"
        "-- humanoid:PlayEmote(\"dance\") from a script).\n"
        "--\n"
        "-- Make your own: open one of the values in here (walk, idle...), give the\n"
        "-- Animation inside it some keyframes in the Animation Editor, and your\n"
        "-- character plays that instead of the classic one.\n"
        "-- Turn this script off (Enabled) to stop the automatic animations.\n";
    SceneNode* s = rig->addChild(std::move(script));
    for (const char* name : kAnimNames) {
        auto v = std::make_unique<SceneNode>(name, NodeKind::Value);
        v->value.type = Attribute::String;
        v->value.s = "classic";
        SceneNode* vn = s->addChild(std::move(v));
        auto a = std::make_unique<SceneNode>(std::string(name) + "Anim", NodeKind::Animation);
        a->source = Anim::emptyClipText();
        vn->addChild(std::move(a));
    }
    scene.markDirty();
}

SceneNode* Player::customAnimation(const char* state) const {
    SceneNode* r = root();
    SceneNode* animate = r ? r->findChild("Animate") : nullptr;
    if (!animate || !animate->enabled) return nullptr;
    SceneNode* v = animate->findChild(state);
    if (!v) return nullptr;
    for (auto& c : v->children) if (c->isAnimation()) return c.get();
    return nullptr;
}

bool Player::hasCustomAnimation(const char* state) const {
    SceneNode* anim = customAnimation(state);
    return anim && Anim::parse(anim->source).length() > 0.0f;
}

bool Player::applyCustomAnimation(const char* state, float dt) {
    SceneNode* r = root();
    SceneNode* anim = customAnimation(state);
    if (!anim) return false;
    CustomClip& cc = m_customClips[anim->id];
    const size_t h = std::hash<std::string>{}(anim->source);
    static std::unordered_map<uint64_t, Anim::Clip> clips;   // parsed, by Animation id
    if (cc.hash != h) {
        cc.hash = h;
        clips[anim->id] = Anim::parse(anim->source);
        cc.has = !clips[anim->id].keys.empty() && clips[anim->id].length() > 0.0f;
        cc.time = 0.0f;
    }
    if (!cc.has) return false;
    const Anim::Clip& clip = clips[anim->id];
    cc.time += dt;
    const float len = clip.length();
    float t = cc.time;
    if (clip.loop || std::string(state) != "jump") t = std::fmod(t, len);   // loop (jump plays once)
    else t = std::min(t, len);
    std::map<std::string, Anim::Sample> poses;
    Anim::sample(clip, t, poses);
    Anim::restore(r, m_rest);
    Anim::applyPoses(r, m_rest, poses, 1.0f);
    return true;
}

void Player::animate(float dt, float groundSpeed, bool grounded) {
    rememberWornRest();   // (a script may have put a hat on)
    m_swing    = approach(m_swing, grounded ? strideSwing(groundSpeed) : 0.0f, 10.0f, dt);
    m_airBlend = approach(m_airBlend, grounded ? 0.0f : 1.0f, 10.0f, dt);
    m_walkPhase += dt * strideRate(groundSpeed);
    m_airTime = grounded ? 0.0f : m_airTime + dt;
    m_idleTime = groundSpeed > 0.3f || !grounded ? 0.0f : m_idleTime + dt;

    // Emotes stop when you move, jump, sit or climb; the short ones end by themselves.
    if (!m_emote.empty()) {
        m_emoteTime += dt;
        if (groundSpeed > 0.5f || !grounded || sitting() || m_climbing || m_swimming || tripped())
            stopEmote(0.2f);
        else if (m_emoteTrack) {   // played to the end (or a script stopped it)
            const Anim::Animator::Track* t = m_scene->animator().track(m_emoteTrack);
            if (!t || !t->playing || t->target <= 0.0f) stopEmote(0.1f);
        } else if (m_emoteLength > 0.0f && m_emoteTime > m_emoteLength) stopEmote(0.1f);   // (the game's own one)
    }
    for (auto it = m_fadingTracks.begin(); it != m_fadingTracks.end();) {
        const Anim::Animator::Track* t = m_scene->animator().track(*it);
        if (t && t->playing) { ++it; continue; }
        if (t) m_scene->animator().forget(*it);
        it = m_fadingTracks.erase(it);
    }

    // Which animation this is (the names in the Animate script).
    const char* state = "idle";
    if (sitting()) state = "sit";
    else if (m_climbing) state = "climb";
    else if (m_swimming) state = "swim";
    else if (!grounded) state = m_velocity.y > 0.0f && m_airTime < 0.6f ? "jump" : "fall";
    else if (!m_emote.empty()) state = m_emote.c_str();
    else if (groundSpeed > 0.3f) state = groundSpeed > m_humanoid.walkSpeed * 1.2f ? "run" : "walk";
    if (!tripped() && applyCustomAnimation(state, dt)) return;   // your own animation from the Animate script

    m_holdBlend = approach(m_holdBlend, equippedTool() ? 1.0f : 0.0f, 12.0f, dt);
    float s = std::sin(m_walkPhase) * m_swing;
    // Using a tool: raise the arm, then slash down through the front, then back.
    float hold = -90.0f;   // "toolnone": arm straight out, holding it
    if (m_toolSwing > 0.0f) {
        float t = 1.0f - m_toolSwing / kToolSwingTime;
        hold = t < 0.35f ? -90.0f + (-170.0f + 90.0f) * (t / 0.35f)
             : t < 0.7f  ? -170.0f + (-40.0f + 170.0f) * ((t - 0.35f) / 0.35f)
             :             -40.0f + (-90.0f + 40.0f) * ((t - 0.7f) / 0.3f);
        m_toolSwing = std::max(0.0f, m_toolSwing - dt);
    }

    // Standing still: the idle - a slow breath in the arms, like the old character.
    const float breath = std::sin(m_idleTime * 1.6f);
    BodyPose ground = {{{s + 2.5f * breath * (1.0f - m_swing / 60.0f), 2.0f + 1.5f * breath}, {-s - 2.5f * breath * (1.0f - m_swing / 60.0f), 2.0f + 1.5f * breath},
                        {-s, 0.0f}, {s, 0.0f}}};
    // In the air: arms up (jumping), then out wide as you fall, legs apart.
    const float falling = std::clamp((m_airTime - 0.35f) * 3.0f, 0.0f, 1.0f) * (m_velocity.y < 0.0f ? 1.0f : 0.0f);
    BodyPose air = {{{-165.0f, 5.0f + 20.0f * falling}, {-165.0f, 5.0f + 20.0f * falling},
                     {12.0f, 6.0f * falling}, {-12.0f, 6.0f * falling}}};
    BodyPose pose = mix(ground, air, m_airBlend);
    // Holding a tool ("toolnone"): the right arm straight out.
    pose[1].pitch = pose[1].pitch * (1.0f - m_holdBlend) + hold * m_holdBlend;
    pose[1].out *= 1.0f - m_holdBlend;

    // Climbing: hand over hand, knees up. Swimming: big arm strokes, kicking legs.
    m_climbBlend = approach(m_climbBlend, m_climbing ? 1.0f : 0.0f, 12.0f, dt);
    m_swimBlend = approach(m_swimBlend, m_swimming ? 1.0f : 0.0f, 8.0f, dt);
    const float c = std::sin(m_climbPhase);
    const BodyPose climb = {{{-150.0f + 30.0f * c, 0.0f}, {-150.0f - 30.0f * c, 0.0f}, {-35.0f - 25.0f * c, 0.0f}, {-35.0f + 25.0f * c, 0.0f}}};
    const BodyPose swim = {{{-90.0f + 85.0f * c, 0.0f}, {-90.0f - 85.0f * c, 0.0f}, {20.0f * std::sin(m_climbPhase * 2.0f), 0.0f},
                            {-20.0f * std::sin(m_climbPhase * 2.0f), 0.0f}}};
    for (int i = 0; i < 4; ++i) {
        const float k = 1.0f - m_climbBlend - m_swimBlend;
        pose[i].pitch = pose[i].pitch * k + climb[i].pitch * m_climbBlend + swim[i].pitch * m_swimBlend;
        pose[i].out *= k;
    }
    // Sitting: legs straight out in front, arms resting forward (the classic sit).
    m_sitBlend = approach(m_sitBlend, sitting() ? 1.0f : 0.0f, 14.0f, dt);
    const BodyPose sitPose = {{{-45.0f, 0.0f}, m_holdBlend > 0.5f ? pose[1] : LimbPose{-45.0f, 0.0f}, {-90.0f, 0.0f}, {-90.0f, 0.0f}}};
    pose = mix(pose, sitPose, m_sitBlend);
    // Knocked over: stiff as a board, arms a little out.
    const float down = std::clamp(std::abs(m_tilt) / 90.0f, 0.0f, 1.0f);
    if (down > 0.01f) {
        const BodyPose stiff = {{{-10.0f, 25.0f}, {-10.0f, 25.0f}, {0.0f, 6.0f}, {0.0f, 6.0f}}};
        pose = mix(pose, stiff, down);
    }

    for (int i = 0; i < 4; ++i) {
        SceneNode* limb = part(kLimbs[i]);
        if (!limb) continue;
        auto it = m_rest.find(limb->id);
        if (it == m_rest.end()) continue;
        const Transform& rest = it->second;
        // Turn around the shoulder / hip (the top of the limb): swing (pitch) and lift out
        // to the side (roll, away from the body: left arm to the left, right to the right).
        const float side = (i == 0 || i == 2) ? -1.0f : 1.0f;
        const float pitch = glm::radians(pose[i].pitch), roll = glm::radians(pose[i].out * side);
        const float h = rest.scale.y * 0.5f;
        const glm::vec3 pivot = rest.position + glm::vec3(0.0f, h, 0.0f);
        const glm::vec3 drop(h * std::cos(pitch) * std::sin(roll), -h * std::cos(pitch) * std::cos(roll), -h * std::sin(pitch));
        limb->transform.position = pivot + drop;
        limb->transform.rotation = rest.rotation + glm::vec3(pose[i].pitch, 0.0f, pose[i].out * side);
    }
    if (SceneNode* head = part("Head")) {
        auto it = m_rest.find(head->id);
        if (it != m_rest.end()) head->transform.rotation = it->second.rotation;
    }
    animationPack(m_emote.empty() ? state : "idle", dt, groundSpeed);
}

// An animation pack from the catalog: its idle / walk / run / jump / fall play instead of
// the classic ones (a game's own Animate script animations still win). Changing from one
// to the next blends over a moment; walking and running keep time with your real speed.
void Player::animationPack(const std::string& state, float dt, float groundSpeed) {
    SceneNode* r = root();
    // What the pack moved last frame that the walk cycle doesn't put back (the Torso) goes back first.
    for (uint64_t id : m_packMoved)
        if (SceneNode* n = m_scene->findById(id))
            if (auto it = m_rest.find(id); it != m_rest.end()) {
                n->transform.position = it->second.position;
                n->transform.rotation = it->second.rotation;
            }
    m_packMoved.clear();
    const Emotes::Pack& pack = Emotes::mine().pack;
    if (!r || pack.empty()) { m_packBlend = 0.0f; m_packState.clear(); return; }
    std::string want = state;
    if (want == "run" && !pack.count("run")) want = "walk";
    if (want == "fall" && !pack.count("fall")) want = "jump";
    const bool use = !tripped() && pack.count(want);
    m_packBlend = approach(m_packBlend, use ? 1.0f : 0.0f, 10.0f, dt);
    if (use && want != m_packState) {
        m_packPrev = m_packState;
        m_packPrevTime = m_packTime;
        m_packState = want;
        m_packTime = 0.0f;
        m_packFade = m_packPrev.empty() ? 1.0f : 0.0f;
    }
    if (m_packState.empty() || m_packBlend < 0.001f) return;
    float rate = 1.0f;
    if (m_packState == "walk" || m_packState == "run")
        rate = std::clamp(groundSpeed / std::max(1.0f, m_humanoid.walkSpeed * (m_packState == "run" ? 1.6f : 1.0f)), 0.3f, 2.5f);
    m_packTime += dt * rate;
    m_packPrevTime += dt;
    m_packFade = std::min(1.0f, m_packFade + dt / 0.2f);
    auto sampleAt = [&](const std::string& st, float t, std::map<std::string, Anim::Sample>& out) {
        out.clear();
        auto it = pack.find(st);
        if (it == pack.end()) return;
        const Anim::Clip& c = *it->second;
        const float len = c.length();
        Anim::sample(c, len <= 0.0f ? 0.0f : c.loop ? std::fmod(t, len) : std::min(t, len), out);
    };
    std::map<std::string, Anim::Sample> poses, before;
    sampleAt(m_packState, m_packTime, poses);
    if (m_packFade < 1.0f && !m_packPrev.empty()) {   // blend over from the last one
        sampleAt(m_packPrev, m_packPrevTime, before);
        for (const auto& [n, b] : before) if (!poses.count(n)) poses[n] = Anim::Sample{};
        for (auto& [n, s] : poses) {
            auto b = before.find(n);
            const Anim::Sample from = b == before.end() ? Anim::Sample{} : b->second;
            s.pos = glm::mix(from.pos, s.pos, m_packFade);
            s.rot = glm::slerp(from.rot, s.rot, m_packFade);
        }
    }
    if (m_holdBlend > 0.5f) poses.erase("Right Arm");   // keep holding the tool
    Anim::applyPoses(r, m_rest, poses, m_packBlend);
    for (const auto& [n, s] : poses)
        if (SceneNode* p = Anim::findPart(r, n); p && !isLimb(p) && p->name != "Head") m_packMoved.push_back(p->id);
    for (SceneNode* p : Anim::wornParts(r)) m_packMoved.push_back(p->id);   // (they turned with the head)
    // The Head always goes back too: it rides on the Torso, so it moves even when the pack
    // doesn't pose it, and the walk cycle only puts back its turn. Left where it was, each
    // frame's bounce piled onto the last and the head wandered off round the body.
    if (SceneNode* head = part("Head")) m_packMoved.push_back(head->id);
}

void Player::startDeath() {
    m_seatId = 0;   // (off any seat)
    m_sitHere = m_sitPending = false;
    SceneNode* r = root();
    if (!r) return;
    equip(0);   // the tool goes back in the backpack (it'd get in the ragdoll's way)
    footsteps(false, glm::vec3(0.0f));
    if (SceneNode* ff = m_scene->findById(m_spawnFF)) m_scene->removeNode(ff);
    m_spawnFF = 0;
    m_dead = true;
    m_diedFlag = true;
    {
        glm::vec3 at = r->transform.position + glm::vec3(0, 2, 0);
        Audio::play("oof", 0.8f, 1.0f, false, &at);
    }
    m_deadTime = 0.0f;
    m_debris.clear();

    if (m_scene->world().deathStyle == DeathStyle::Ragdoll) {
        glm::vec3 vel(m_velocity.x, std::max(m_velocity.y, -30.0f), m_velocity.z);
        m_ragdoll.start(*m_scene, r, vel, m_pendingImpulse, m_pendingForce);
        m_pendingForce = 0.0f;
        m_pendingImpulse = glm::vec3(0.0f);
        return;
    }

    // Move every body part into world space so they can tumble independently.
    glm::mat4 rootM = r->transform.matrix();
    float yaw = r->transform.rotation.y;
    glm::vec3 center = r->transform.position + glm::vec3(0, 1.3f, 0);
    for (auto& c : r->children) {
        if (!c->isPart()) continue;
        c->transform.position = glm::vec3(rootM * glm::vec4(c->transform.position, 1.0f));
        c->transform.rotation.y += yaw;
        glm::vec3 out = c->transform.position - center;
        out.y = 0.0f;
        if (glm::length(out) > 1e-3f) out = glm::normalize(out);
        glm::vec3 vel = out * (1.5f + rand01() * 2.0f) +
                        glm::vec3(rand01() - 0.5f, 3.0f + rand01() * 2.5f, rand01() - 0.5f);
        glm::vec3 spin = (glm::vec3(rand01(), rand01(), rand01()) - 0.5f) * 500.0f;
        m_debris.push_back({c->id, vel, spin});
    }
    r->transform.position = glm::vec3(0.0f);
    r->transform.rotation = glm::vec3(0.0f);
    r->transform.scale    = glm::vec3(1.0f);

    // Classic death with gore on: every piece sprays as it flies.
    if (m_scene->goreEnabled()) {
        GoreKind k = m_scene->goreKind();
        for (auto& d : m_debris)
            if (SceneNode* n = m_scene->findById(d.id))
                m_scene->particles().spray(k, n->transform.position, glm::normalize(d.vel), 8, 3.0f);
        if (m_pendingForce > 0.5f) m_scene->particles().gibs(k, center, glm::vec3(0, 2, 0), 8);
    }
    for (auto& d : m_debris) d.vel += m_pendingImpulse;
    m_pendingForce = 0.0f;
    m_pendingImpulse = glm::vec3(0.0f);
}

void Player::updateDeath(float dt, Physics& physics) {
    m_deadTime += dt;
    if (m_ragdoll.active()) {
        m_ragdoll.update(dt, *m_scene, physics);
        if (m_deadTime >= m_respawnDelay) respawn();
        return;
    }
    const float g = m_scene->world().gravity;
    for (auto& d : m_debris) {
        SceneNode* n = m_scene->findById(d.id);
        if (!n) continue;
        d.vel.y -= g * dt;
        n->transform.position += d.vel * dt;
        n->transform.rotation += d.spin * dt;

        float push = physics.pushUp(Physics::worldBounds(n));
        if (push > 0.0f) {
            n->transform.position.y += push;
            if (d.vel.y < 0.0f) d.vel.y = -d.vel.y * 0.25f;   // small bounce
            d.vel.x *= 0.8f;  d.vel.z *= 0.8f;
            d.spin *= 0.8f;
        }
    }
    if (m_deadTime >= m_respawnDelay) respawn();
}
