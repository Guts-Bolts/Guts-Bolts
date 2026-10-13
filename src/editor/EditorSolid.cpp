// Solid modeling, like Roblox Studio's MODEL tab: Union (Ctrl+Shift+G) joins the
// selected parts into one shape, Negate (Ctrl+Shift+N) turns a part into a "hole"
// that Union cuts out, Intersect (Ctrl+Shift+I) keeps only where parts overlap,
// and Separate (Ctrl+Shift+U) gives back the parts a union was made from.

#include "Editor.h"
#include "../core/Log.h"
#include "../scene/Csg.h"
#include "../scene/EditMesh.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"

#include <cmath>
#include <map>
#include <tuple>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

const glm::vec3 kNegateColor{1.0f, 0.45f, 0.45f};

// A part's shape as polygons, in its parent's space (where the union will go).
Csg::Solid solidOf(const SceneNode& n, const glm::mat4& toParent) {
    std::shared_ptr<EditMesh> m = n.primitiveType == PrimitiveType::Mesh && n.editMesh ? n.editMesh
                                                                                      : MeshEdit::fromPrimitive(n.primitiveType);
    const glm::dmat4 mat = glm::dmat4(toParent * n.transform.matrix());
    Csg::Solid s;
    for (const auto& face : m->faces) {
        Csg::Polygon p;
        for (uint32_t i : face) p.verts.push_back(glm::dvec3(mat * glm::dvec4(glm::dvec3(m->verts[i]), 1.0)));
        s.push_back(std::move(p));
    }
    return s;
}

// Polygons -> a mesh that fits the part's box (-0.5..0.5), with shared corners welded.
std::shared_ptr<EditMesh> meshOf(const Csg::Solid& s, glm::vec3& center, glm::vec3& size) {
    glm::dvec3 lo(1e30), hi(-1e30);
    for (const auto& p : s) for (const auto& v : p.verts) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
    const glm::dvec3 c = (lo + hi) * 0.5, ext = glm::max(hi - lo, glm::dvec3(1e-3));
    center = glm::vec3(c);
    size = glm::vec3(ext);
    auto m = std::make_shared<EditMesh>();
    std::map<std::tuple<long long, long long, long long>, uint32_t> weld;
    for (const auto& p : s) {
        std::vector<uint32_t> face;
        for (const auto& v : p.verts) {
            const glm::dvec3 local = (v - c) / ext;
            auto key = std::make_tuple(std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4));
            auto it = weld.find(key);
            uint32_t idx;
            if (it == weld.end()) {
                idx = (uint32_t)m->verts.size();
                m->verts.push_back(glm::clamp(glm::vec3(local), glm::vec3(-0.5f), glm::vec3(0.5f)));
                weld.emplace(key, idx);
            } else idx = it->second;
            if (face.empty() || (face.back() != idx && face.front() != idx)) face.push_back(idx);
        }
        if (face.size() >= 3) m->faces.push_back(std::move(face));
    }
    return m;
}

} // namespace

void Editor::negateSelected() {
    int n = 0;
    for (SceneNode* p : m_scene->selectionRoots()) {
        if (!p->isPart() || !canEdit(p) || m_scene->isCharacterPart(p)) continue;
        if (!p->negated) {
            p->negated = true;
            p->negColor = p->color; p->negTransparency = p->transparency; p->negCollide = p->canCollide;
            p->color = kNegateColor; p->transparency = 0.6f; p->canCollide = false;
        } else {   // negate again = back to normal
            p->negated = false;
            p->color = p->negColor; p->transparency = p->negTransparency; p->canCollide = p->negCollide;
        }
        ++n;
    }
    if (!n) { Log::warn("Select one or more parts to negate."); return; }
    m_scene->markDirty();
    Log::info("Negated " + std::to_string(n) + " part(s). Select them with the parts to cut and press Union (Ctrl+Shift+G).");
}

// mode 0 = Union, 1 = Intersect.
void Editor::unionSelected(int mode) {
    std::vector<SceneNode*> plus, minus;
    for (SceneNode* p : m_scene->selectionRoots()) {
        if (!p->isPart() || !canEdit(p) || m_scene->isCharacterPart(p)) continue;
        (p->negated ? minus : plus).push_back(p);
    }
    if (plus.empty() || plus.size() + minus.size() < 2) {
        Log::warn(mode == 1 ? "Select two or more overlapping parts to intersect."
                            : "Select two or more parts to union (negated parts get cut out).");
        return;
    }
    SceneNode* parent = plus[0]->parent ? plus[0]->parent : m_scene->root();
    const glm::mat4 toParent = glm::inverse(parent->worldMatrix());
    auto inParent = [&](SceneNode* p) { return toParent * (p->parent ? p->parent->worldMatrix() : glm::mat4(1.0f)); };

    Csg::Solid result = solidOf(*plus[0], inParent(plus[0]));
    for (size_t i = 1; i < plus.size(); ++i) {
        Csg::Solid b = solidOf(*plus[i], inParent(plus[i]));
        result = mode == 1 ? Csg::intersect(result, b) : Csg::unite(result, b);
    }
    for (SceneNode* p : minus) result = Csg::subtract(result, solidOf(*p, inParent(p)));
    if (result.empty()) {
        Log::warn(mode == 1 ? "Those parts don't overlap, so there's nothing left." : "Nothing would be left of that union.");
        return;
    }

    glm::vec3 center, size;
    std::shared_ptr<EditMesh> mesh = meshOf(result, center, size);
    if (mesh->faces.empty()) { Log::warn("That union came out empty."); return; }

    // The new part looks like the first part (Roblox's union uses one colour too).
    auto u = std::make_unique<SceneNode>(mode == 1 ? "Intersection" : "Union");
    const SceneNode& look = *plus[0];
    u->color = look.color; u->material = look.material; u->transparency = look.transparency;
    u->anchored = look.anchored; u->canCollide = look.canCollide; u->castShadow = look.castShadow;
    u->collisionGroup = look.collisionGroup;
    u->transform.position = center;
    u->transform.scale = size;
    // What it was made from (positions in the union's parent), for Separate.
    json parts = json::array();
    for (SceneNode* p : plus)  parts.push_back(Serializer::nodeToString(*p));
    for (SceneNode* p : minus) parts.push_back(Serializer::nodeToString(*p));
    u->unionSource = json{{"at", {center.x, center.y, center.z}}, {"parts", parts}}.dump();
    MeshEdit::attach(*u, mesh);

    m_scene->deselect();
    for (SceneNode* p : plus) m_scene->removeNode(p);
    for (SceneNode* p : minus) m_scene->removeNode(p);
    SceneNode* made = m_scene->insert(std::move(u), parent);
    m_scene->select(made);
    Log::info(std::string(mode == 1 ? "Intersected " : "Unioned ") + std::to_string(plus.size() + minus.size()) +
              " parts into one (" + std::to_string(mesh->faces.size()) + " faces). Ctrl+Shift+U separates it again.");
}

void Editor::separateSelected() {
    std::vector<SceneNode*> back;
    std::vector<SceneNode*> unions;
    for (SceneNode* p : m_scene->selectionRoots())
        if (!p->unionSource.empty() && canEdit(p)) unions.push_back(p);
    if (unions.empty()) { Log::warn("Select a union to separate."); return; }
    for (SceneNode* u : unions) {
        json src = json::parse(u->unionSource, nullptr, false);
        if (!src.is_object()) continue;
        // If the union was moved, the parts move with it.
        glm::vec3 at(0.0f);
        if (src["at"].is_array() && src["at"].size() == 3) at = {src["at"][0].get<float>(), src["at"][1].get<float>(), src["at"][2].get<float>()};
        const glm::vec3 moved = u->transform.position - at;
        SceneNode* parent = u->parent ? u->parent : m_scene->root();
        for (const auto& s : src.value("parts", json::array())) {
            if (!s.is_string()) continue;
            auto n = Serializer::nodeFromString(s.get<std::string>(), true);
            if (!n) continue;
            n->transform.position += moved;
            back.push_back(m_scene->insert(std::move(n), parent));
        }
        m_scene->removeNode(u);
    }
    m_scene->deselect();
    for (SceneNode* n : back) m_scene->addToSelection(n);
    Log::info("Separated into " + std::to_string(back.size()) + " part(s).");
}
