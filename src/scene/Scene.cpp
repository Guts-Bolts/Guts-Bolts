#include <algorithm>
#include "Scene.h"
#include "../renderer/MeshLibrary.h"
#include "../core/Settings.h"
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <ImGuizmo.h>
#include <glm/gtc/type_ptr.hpp>

namespace {

const char* kWelcomeScript =
R"(-- Welcome to scripting in Guts and Bolts!
-- Scripts run when you press Play (F5). Anything you print()
-- shows up in the Output window at the bottom.

print("Hello world! The game has started.")

-- 'workspace' is the whole world. Let's find the part called "Cube":
local cube = workspace:FindFirstChild("Cube")

if cube then
    -- Make it slowly change colour, forever.
    while true do
        cube.Color = Color3.fromHSV((os.clock() * 0.1) % 1, 0.7, 1)
        wait(0.05)
    end
end
)";

} // namespace

Scene::Scene() {
    m_terrainNode = std::make_unique<SceneNode>("Terrain", NodeKind::Model);
    m_terrainNode->id = kTerrainId;
    m_terrainNode->anchored = true;
    m_cameraNode = std::make_unique<SceneNode>("Camera", NodeKind::Model);
    m_cameraNode->id = kCameraId;
    m_player = std::make_unique<Player>(this);
    buildDefault();
}

void Scene::buildDefault() {
    m_selected = nullptr;
    m_selection.clear();
    m_terrain.clear();
    m_cameraNode->children.clear();
    m_root = std::make_unique<SceneNode>("Workspace", NodeKind::Model);
    markDirty();

    // A big flat floor, like Roblox's Baseplate. Its top sits at y = 0.
    auto* base = addNode("Baseplate", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    base->transform.position = {0.0f, -0.5f, 0.0f};
    base->transform.scale    = {64.0f, 1.0f, 64.0f};
    base->color              = {0.36f, 0.62f, 0.33f};

    // Where the character appears when you press Play.
    auto* spawn = addNode("SpawnLocation", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    spawn->transform.position = {0.0f, 0.1f, 0.0f};
    spawn->transform.scale    = {3.0f, 0.2f, 3.0f};
    spawn->color              = {0.55f, 0.57f, 0.60f};

    auto* cube = addNode("Cube", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    cube->transform.position = {3.0f, 0.5f, -3.0f};
    cube->color              = {0.85f, 0.35f, 0.30f};

    auto script = std::make_unique<SceneNode>("WelcomeScript", NodeKind::Script);
    script->source = kWelcomeScript;
    insert(std::move(script));

    m_env   = Environment{};
    m_world = WorldSettings{};
    m_info  = GameInfo{};
    m_player->setSpawn({0.0f, 0.2f, 0.0f});
    m_player->resetSettings();
    m_player->build();
}

bool Scene::goreEnabled() const {
    return m_world.gore != GoreLevel::Off && GraphicsSettings::get().allowGore;
}

GoreKind Scene::goreKind() const {
    return m_world.gore == GoreLevel::Blood ? GoreKind::Blood : GoreKind::Oil;
}

void Scene::select(SceneNode* node) {
    deselect();
    addToSelection(node);
}

void Scene::deselect() {
    for (SceneNode* n : m_selection) n->selected = false;
    m_selection.clear();
    m_selected = nullptr;
}

void Scene::addToSelection(SceneNode* node) {
    if (!node) return;
    auto it = std::find(m_selection.begin(), m_selection.end(), node);
    if (it != m_selection.end()) m_selection.erase(it);
    m_selection.push_back(node);
    node->selected = true;
    m_selected = node;
}

void Scene::toggleSelection(SceneNode* node) {
    if (!node) return;
    auto it = std::find(m_selection.begin(), m_selection.end(), node);
    if (it == m_selection.end()) { addToSelection(node); return; }
    node->selected = false;
    m_selection.erase(it);
    m_selected = m_selection.empty() ? nullptr : m_selection.back();
}

std::vector<SceneNode*> Scene::selectionRoots() const {
    std::vector<SceneNode*> out;
    for (SceneNode* n : m_selection) {
        bool inside = false;
        for (SceneNode* o : m_selection) if (o != n && o->isAncestorOf(n)) { inside = true; break; }
        if (!inside) out.push_back(n);
    }
    return out;
}

void Scene::unselectSubtree(SceneNode* node) {
    bool changed = false;
    for (auto it = m_selection.begin(); it != m_selection.end();) {
        if (*it == node || node->isAncestorOf(*it)) { (*it)->selected = false; it = m_selection.erase(it); changed = true; }
        else ++it;
    }
    if (changed) m_selected = m_selection.empty() ? nullptr : m_selection.back();
}

SceneNode* Scene::addNode(const std::string& name, PrimitiveType type, std::shared_ptr<Mesh> mesh) {
    auto node           = std::make_unique<SceneNode>(name);
    node->primitiveType = type;
    node->mesh          = std::move(mesh);
    return insert(std::move(node));
}

SceneNode* Scene::insert(std::unique_ptr<SceneNode> node, SceneNode* parent) {
    if (!parent) parent = m_root.get();
    SceneNode* raw = parent->addChild(std::move(node));
    markDirty();
    return raw;
}

void Scene::removeNode(SceneNode* node) {
    if (!node || node == m_root.get()) return;
    unselectSubtree(node);
    if (node->parent) node->parent->removeChild(node);
    markDirty();
}

std::unique_ptr<SceneNode> Scene::detach(SceneNode* node) {
    if (!node || node == m_root.get() || !node->parent) return nullptr;
    unselectSubtree(node);
    markDirty();
    return node->parent->detachChild(node);
}

bool Scene::reparent(SceneNode* node, SceneNode* newParent) {
    if (!node || !newParent || node == m_root.get() || !node->parent) return false;
    if (node == newParent || node->isAncestorOf(newParent)) return false;   // no loops
    if (node->parent == newParent) return false;

    glm::mat4 world = node->worldMatrix();
    std::unique_ptr<SceneNode> owned = node->parent->detachChild(node);
    glm::mat4 local = glm::inverse(newParent->worldMatrix()) * world;

    float t[3], r[3], s[3];
    ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local), t, r, s);
    owned->transform.position = {t[0], t[1], t[2]};
    owned->transform.rotation = {r[0], r[1], r[2]};
    owned->transform.scale    = {s[0], s[1], s[2]};

    newParent->addChild(std::move(owned));
    markDirty();
    return true;
}

void Scene::replaceRoot(std::unique_ptr<SceneNode> root) {
    m_selected = nullptr;
    m_selection.clear();
    m_particles.clear();
    m_cameraNode->children.clear();
    m_root = std::move(root);
    markDirty();
}

void Scene::rebuildIndex() {
    m_index.clear();
    forEach([this](SceneNode* n) { m_index[n->id] = n; });
    std::function<void(SceneNode*)> add = [this](SceneNode* n) { m_index[n->id] = n; };
    walk(m_cameraNode.get(), add);   // (what's in the Camera)
    m_indexDirty = false;
}

SceneNode* Scene::findById(uint64_t id) {
    if (id == kTerrainId) return m_terrainNode.get();
    if (m_indexDirty) rebuildIndex();
    auto it = m_index.find(id);
    if (it != m_index.end()) return it->second;
    // Nodes may have been added since the last rebuild — try once more.
    rebuildIndex();
    it = m_index.find(id);
    return it != m_index.end() ? it->second : nullptr;
}

const std::vector<const char*>& Scene::serviceNames() {
    static const std::vector<const char*> names = {"ReplicatedFirst", "ReplicatedStorage", "ServerScriptService", "ServerStorage",
                                                   "StarterGui", "StarterPack", "Teams", "SoundService", "Chat", "TextChatService",
                                                   "Lighting"};
    return names;
}

bool Scene::isServiceName(const std::string& name) {
    for (const char* n : serviceNames()) if (name == n) return true;
    return false;
}

bool Scene::isServiceFolder(const SceneNode* node) const {
    return node && node->parent == m_root.get() && node->kind == NodeKind::Model && isServiceName(node->name);
}

SceneNode* Scene::serviceFolder(const std::string& name, bool create) {
    if (!isServiceName(name)) return nullptr;
    for (auto& c : m_root->children)
        if (c->name == name && c->kind == NodeKind::Model) return c.get();
    if (!create) return nullptr;
    auto folder = std::make_unique<SceneNode>(name, NodeKind::Model);
    // Only the game UI shows in the world; the rest are storage (templates, scripts, sounds).
    if (name != "StarterGui") folder->visible = false;
    return insert(std::move(folder), nullptr);
}

bool Scene::isProtected(const SceneNode* node) const {
    return node == m_root.get() || (node && (node == m_terrainNode.get() || node == m_cameraNode.get())) || (m_player && node && node->id == m_player->rootId());
}

bool Scene::isCharacterPart(const SceneNode* node) const {
    for (const SceneNode* n = node; n; n = n->parent)
        if (isCharacterRoot(n->id)) return true;
    return false;
}

bool Scene::isCharacterRoot(uint64_t id) const {
    if (!id) return false;
    if (m_player && id == m_player->rootId()) return true;
    for (const auto& r : m_remotes) if (r.rootId == id) return true;
    return false;
}

RemoteCharacter* Scene::findRemote(uint64_t rootId) {
    for (auto& r : m_remotes) if (r.rootId == rootId) return &r;
    return nullptr;
}

Humanoid* Scene::humanoidOf(uint64_t rootId) {
    if (m_player && rootId == m_player->rootId()) return &m_player->humanoid();
    if (RemoteCharacter* r = findRemote(rootId)) return &r->humanoid;
    if (Npc* n = m_npcs.find(*this, rootId)) return &n->humanoid;
    return nullptr;
}

void Scene::killCharacter(uint64_t rootId, float force, const glm::vec3& impulse) {
    if (m_player && rootId == m_player->rootId()) { m_player->kill(force, impulse); return; }
    if (RemoteCharacter* r = findRemote(rootId)) {
        r->humanoid.health = 0.0f;
        r->humanoidDirty = true;
        r->kills.push_back({force, impulse});
        return;
    }
    m_npcs.kill(rootId, force, impulse);
}

void Scene::markHumanoidEdited(uint64_t rootId, double now) {
    if (RemoteCharacter* r = findRemote(rootId)) { r->humanoidDirty = true; r->editedAt = now; }
}

void Scene::forEach(std::function<void(SceneNode*)> fn) {
    walk(m_root.get(), fn);
}

void Scene::walk(SceneNode* node, std::function<void(SceneNode*)>& fn) {
    fn(node);
    for (auto& child : node->children)
        walk(child.get(), fn);
}
