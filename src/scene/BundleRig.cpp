#include "BundleRig.h"
#include "EditMesh.h"
#include "SceneNode.h"
#include "Serializer.h"
#include "Skin.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <map>

namespace BundleRig {

namespace {

using json = nlohmann::json;

// Bones made from a bundle carry this tag, so wearing another bundle (or the normal
// body) takes them off without touching bones a game script put there.
constexpr const char* kTag = "BundleBone";

glm::vec3 vec3(const json& a, glm::vec3 fallback = glm::vec3(0.0f)) {
    if (!a.is_array() || a.size() != 3) return fallback;
    glm::vec3 v;
    for (int i = 0; i < 3; ++i) {
        if (!a[i].is_number()) return fallback;
        v[i] = a[i].get<float>();
        if (!std::isfinite(v[i])) return fallback;
    }
    return v;
}

// A bone's place (relative to the bone it's in, or the part) as position + turn.
void setLocal(SceneNode& bone, const glm::mat4& local) {
    float z, y, x;
    glm::extractEulerAngleZYX(local, z, y, x);
    bone.transform.position = glm::vec3(local[3]);
    bone.transform.rotation = glm::degrees(glm::vec3(x, y, z));
}

bool isBundleBone(const SceneNode& n) {
    return n.isBone() && std::find(n.tags.begin(), n.tags.end(), kTag) != n.tags.end();
}

SceneNode* addBone(SceneNode& in, const std::string& name) {
    auto b = std::make_unique<SceneNode>(name, NodeKind::Attachment);
    b->bone = true;
    b->tags.push_back(kTag);
    return in.addChild(std::move(b));
}

} // namespace

void toJson(const SceneNode& part, json& out) {
    std::vector<SceneNode*> list = Skin::bones(const_cast<SceneNode*>(&part));
    if (list.empty()) return;
    if (list.size() > 32) list.resize(32);
    const EditMesh* m = part.editMesh.get();
    const bool bound = m && m->skinned();
    std::map<const SceneNode*, int> index;
    json bones = json::array();
    for (size_t i = 0; i < list.size(); ++i) {
        SceneNode* b = list[i];
        index[b] = (int)i;
        auto parent = index.find(b->parent);
        // Where it was when the mesh was bound (its rest pose), else where it is now.
        glm::mat4 local = glm::translate(glm::mat4(1.0f), b->transform.position) *
                          glm::eulerAngleZYX(glm::radians(b->transform.rotation.z), glm::radians(b->transform.rotation.y),
                                             glm::radians(b->transform.rotation.x));
        if (bound) {
            auto at = std::find(m->bones.begin(), m->bones.end(), b->name);
            if (at != m->bones.end()) {
                const glm::mat4 rest = glm::inverse(m->bind[at - m->bones.begin()]);
                glm::mat4 parentRest(1.0f);
                if (b->parent && b->parent->isBone()) {
                    auto pa = std::find(m->bones.begin(), m->bones.end(), b->parent->name);
                    parentRest = pa != m->bones.end() ? glm::inverse(m->bind[pa - m->bones.begin()])
                                                      : Skin::frame(b->parent, &part);
                }
                local = glm::inverse(parentRest) * rest;
            }
        }
        float z, y, x;
        glm::extractEulerAngleZYX(local, z, y, x);
        const glm::vec3 pos = glm::clamp(glm::vec3(local[3]), glm::vec3(-16.0f), glm::vec3(16.0f));
        glm::vec3 rot = glm::degrees(glm::vec3(x, y, z));
        json one = {{"name", b->name.substr(0, 32)},
                    {"parent", parent == index.end() ? -1 : parent->second},
                    {"pos", {pos.x, pos.y, pos.z}},
                    {"rot", {rot.x, rot.y, rot.z}},
                    {"type", b->rig.type == BoneType::Jiggle ? "Jiggle" : "Normal"},
                    {"stiffness", std::clamp(b->rig.stiffness, 0.0f, 1.0f)},
                    {"damping", std::clamp(b->rig.damping, 0.0f, 1.0f)},
                    {"gravity", std::clamp(b->rig.gravity, -200.0f, 200.0f)},
                    {"length", std::clamp(b->rig.length, 0.0f, 16.0f)}};
        bones.push_back(std::move(one));
    }
    out["bones"] = std::move(bones);
    if (!bound) return;   // (the wearer works the weights out)
    // The mesh's weights, with bone numbers from the list above.
    std::vector<int> remap(m->bones.size(), -1);
    for (size_t k = 0; k < m->bones.size(); ++k)
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i]->name == m->bones[k]) { remap[k] = (int)i; break; }
    json w = json::array();
    for (const auto& vw : m->weights) {
        json one = json::array();
        for (const auto& x : vw) {
            if (x.bone >= remap.size() || remap[x.bone] < 0 || one.size() >= 8) continue;
            one.push_back(remap[x.bone]);
            one.push_back(std::clamp(x.w, 0.0f, 1.0f));
        }
        w.push_back(std::move(one));
    }
    out["w"] = std::move(w);
}

void clear(SceneNode& part) {
    bool had = false;
    std::vector<SceneNode*> stack{&part};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        const size_t before = n->children.size();
        n->children.erase(std::remove_if(n->children.begin(), n->children.end(),
                                         [](const std::unique_ptr<SceneNode>& c) { return isBundleBone(*c); }),
                          n->children.end());
        had = had || n->children.size() != before;
        for (auto& c : n->children) if (c->isBone()) stack.push_back(c.get());
    }
    if (had) Skin::unbind(part);
}

bool apply(SceneNode& part, const json& j) {
    clear(part);
    if (!j.is_object()) return false;
    const bool bend = j.value("bend", false) == true && part.name != "Head" && part.name != "Torso";
    const json bones = j.contains("bones") && j["bones"].is_array() ? j["bones"] : json::array();
    if (bones.empty() && !bend) return false;
    if (!part.editMesh) {   // (a plain block: give it corners to bend)
        if (part.primitiveType == PrimitiveType::None) return false;
        MeshEdit::attach(part, MeshEdit::fromPrimitive(part.primitiveType));
    }

    std::vector<SceneNode*> made;
    for (const json& b : bones) {
        if (!b.is_object() || made.size() >= 32) break;
        const int parent = b.value("parent", -1);
        SceneNode* in = parent >= 0 && parent < (int)made.size() ? made[(size_t)parent] : &part;
        SceneNode* bone = addBone(*in, b.value("name", std::string("Bone")).substr(0, 32));
        bone->transform.position = vec3(b.value("pos", json()));
        bone->transform.rotation = vec3(b.value("rot", json()));
        Serializer::boneRigFromJson(b, bone->rig);
        bone->rig.target = 0;   // (only Normal and Jiggle come in bundles)
        if (bone->rig.type != BoneType::Jiggle) bone->rig.type = BoneType::Normal;
        made.push_back(bone);
    }

    if (bend && made.empty()) {
        // An elbow / knee: an upper bone from the shoulder (hip) pointing down the limb,
        // and a springy lower one from the middle. When the limb swings, the lower half
        // lags a little and bends instead of the whole limb moving like a plank.
        const float len = std::max(0.2f, part.transform.scale.y);
        SceneNode* upper = addBone(part, kUpperBone);
        setLocal(*upper, glm::translate(glm::mat4(1.0f), {0.0f, len * 0.5f, 0.0f}) *
                             glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), {0.0f, 0.0f, 1.0f}));
        upper->rig.length = len * 0.5f;
        SceneNode* lower = addBone(*upper, kLowerBone);
        lower->transform.position = {0.0f, len * 0.5f, 0.0f};
        lower->rig.type = BoneType::Jiggle;
        lower->rig.stiffness = 0.82f;
        lower->rig.damping = 0.45f;
        lower->rig.gravity = 6.0f;
        lower->rig.length = len * 0.5f;
        made = {upper, lower};
    }
    if (made.empty()) return false;

    // Weights: the bundle's own, or worked out from how close each corner is to each bone.
    const json& w = j.contains("w") ? j["w"] : json();
    EditMesh& m = MeshEdit::own(part);
    if (w.is_array() && w.size() == m.verts.size() && !bones.empty()) {
        Skin::still(part);
        m.bones.clear();
        m.bind.clear();
        for (SceneNode* b : made) {
            m.bones.push_back(b->name);
            m.bind.push_back(glm::inverse(Skin::frame(b, &part)));
        }
        m.weights.assign(m.verts.size(), {});
        for (size_t v = 0; v < w.size(); ++v) {
            float sum = 0.0f;
            for (size_t k = 0; k + 1 < w[v].size() && m.weights[v].size() < 4; k += 2) {
                if (!w[v][k].is_number_integer() || !w[v][k + 1].is_number()) continue;
                const int b = w[v][k].get<int>();
                const float x = std::clamp(w[v][k + 1].get<float>(), 0.0f, 1.0f);
                if (b < 0 || b >= (int)made.size() || x <= 0.0f) continue;
                m.weights[v].push_back({(uint16_t)b, x});
                sum += x;
            }
            if (sum > 1e-6f) for (auto& x : m.weights[v]) x.w /= sum;
        }
        MeshEdit::refresh(part);
    } else if (!Skin::bindAutomatic(part)) {
        clear(part);
        return false;
    }
    return true;
}

} // namespace BundleRig
