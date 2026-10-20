#include "Skin.h"
#include "EditMesh.h"
#include "SceneNode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>

namespace Skin {

namespace {
// A transform without its size.
glm::mat4 rigid(const Transform& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = glm::rotate(m, glm::radians(t.rotation.z), {0, 0, 1});
    m = glm::rotate(m, glm::radians(t.rotation.y), {0, 1, 0});
    m = glm::rotate(m, glm::radians(t.rotation.x), {1, 0, 0});
    return m;
}

void collect(SceneNode* n, std::vector<SceneNode*>& out) {
    for (auto& c : n->children)
        if (c->isBone()) { out.push_back(c.get()); collect(c.get(), out); }
}

glm::vec3 closestOnSegment(glm::vec3 p, glm::vec3 a, glm::vec3 b) {
    glm::vec3 ab = b - a;
    float len2 = glm::dot(ab, ab);
    float t = len2 > 1e-12f ? std::clamp(glm::dot(p - a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return a + ab * t;
}

// Keep the strongest 4, adding up to 1.
void tidy(std::vector<EditMesh::Weight>& w) {
    w.erase(std::remove_if(w.begin(), w.end(), [](const EditMesh::Weight& x) { return x.w <= 1e-4f; }), w.end());
    std::sort(w.begin(), w.end(), [](const EditMesh::Weight& a, const EditMesh::Weight& b) { return a.w > b.w; });
    if (w.size() > 4) w.resize(4);
}

int boneIndex(EditMesh& m, SceneNode& part, const std::string& name) {
    for (size_t i = 0; i < m.bones.size(); ++i) if (m.bones[i] == name) return (int)i;
    for (SceneNode* b : bones(&part))
        if (b->name == name) {
            m.bones.push_back(name);
            m.bind.push_back(glm::inverse(frame(b, &part)));
            return (int)m.bones.size() - 1;
        }
    return -1;
}
} // namespace

std::vector<SceneNode*> bones(SceneNode* part) {
    std::vector<SceneNode*> out;
    if (part) collect(part, out);
    return out;
}

SceneNode* partOf(SceneNode* bone) {
    for (SceneNode* p = bone ? bone->parent : nullptr; p; p = p->parent) {
        if (p->isPart()) return p;
        if (!p->isBone()) return nullptr;
    }
    return nullptr;
}

glm::mat4 frame(const SceneNode* bone, const SceneNode* part) {
    glm::mat4 f(1.0f);
    for (const SceneNode* b = bone; b && b != part && b->isBone(); b = b->parent) f = rigid(b->transform) * f;
    return f;
}

glm::vec3 tip(SceneNode* bone, SceneNode* part) {
    glm::mat4 f = frame(bone, part);
    for (auto& c : bone->children)
        if (c->isBone() && glm::length(c->transform.position) > 1e-3f) return glm::vec3(f * rigid(c->transform)[3]);
    // No child: as long as the bone it's in (or a stud), pointing along its own up.
    float len = 1.0f;
    if (bone->parent && bone->parent->isBone()) len = std::max(0.2f, glm::length(bone->transform.position));
    return glm::vec3(f * glm::vec4(0.0f, len, 0.0f, 1.0f));
}

bool bindAutomatic(SceneNode& part) {
    if (!part.editMesh) return false;
    std::vector<SceneNode*> list = bones(&part);
    if (list.empty()) return false;
    EditMesh& m = MeshEdit::own(part);
    const glm::vec3 S = part.transform.scale;
    m.bones.clear();
    m.bind.clear();
    std::vector<glm::vec3> head, tail;
    for (SceneNode* b : list) {
        glm::mat4 f = frame(b, &part);
        m.bones.push_back(b->name);
        m.bind.push_back(glm::inverse(f));
        head.push_back(glm::vec3(f[3]));
        tail.push_back(tip(b, &part));
    }
    // Each vertex follows the bones closest to it: much closer = much stronger.
    m.weights.assign(m.verts.size(), {});
    for (size_t v = 0; v < m.verts.size(); ++v) {
        glm::vec3 p = m.verts[v] * S;
        std::vector<EditMesh::Weight> w;
        float nearest = 1e30f;
        std::vector<float> d(list.size());
        for (size_t b = 0; b < list.size(); ++b) {
            d[b] = glm::length(p - closestOnSegment(p, head[b], tail[b]));
            nearest = std::min(nearest, d[b]);
        }
        for (size_t b = 0; b < list.size(); ++b) {
            // Bones much further away than the nearest one don't count.
            float r = (d[b] + 0.05f) / (nearest + 0.05f);
            float k = 1.0f / (r * r * r * r);
            if (k > 0.02f) w.push_back({(uint16_t)b, k});
        }
        tidy(w);
        float sum = 0.0f;
        for (auto& x : w) sum += x.w;
        for (auto& x : w) x.w /= sum;
        m.weights[v] = std::move(w);
    }
    return true;
}

bool assign(SceneNode& part, const std::vector<char>& picked, const std::string& bone, float weight) {
    if (!part.editMesh) return false;
    EditMesh& m = MeshEdit::own(part);
    if (!m.skinned()) { m.bones.clear(); m.bind.clear(); m.weights.assign(m.verts.size(), {}); }
    int bi = boneIndex(m, part, bone);
    if (bi < 0) return false;
    bool any = false;
    weight = std::clamp(weight, 0.0f, 1.0f);
    for (size_t v = 0; v < m.verts.size() && v < picked.size(); ++v) {
        if (!picked[v]) continue;
        any = true;
        auto& w = m.weights[v];
        w.erase(std::remove_if(w.begin(), w.end(), [&](const EditMesh::Weight& x) { return x.bone == bi; }), w.end());
        float others = 0.0f;
        for (auto& x : w) others += x.w;
        if (others > 1e-6f) for (auto& x : w) x.w *= (1.0f - weight) / others;
        if (weight > 0.0f) w.push_back({(uint16_t)bi, weight});
        tidy(w);
    }
    return any;
}

void unbind(SceneNode& part) {
    if (!part.editMesh || part.editMesh->bones.empty()) return;
    EditMesh& m = MeshEdit::own(part);
    m.bones.clear();
    m.bind.clear();
    m.weights.clear();
    MeshEdit::refresh(part);
}

void resetPose(SceneNode& part) {
    if (!part.editMesh) return;
    const EditMesh& m = *part.editMesh;
    for (SceneNode* b : bones(&part)) {
        auto it = std::find(m.bones.begin(), m.bones.end(), b->name);
        if (it == m.bones.end()) continue;
        glm::mat4 want = glm::inverse(m.bind[it - m.bones.begin()]);
        glm::mat4 parent = b->parent && b->parent->isBone() ? frame(b->parent, &part) : glm::mat4(1.0f);
        glm::mat4 local = glm::inverse(parent) * want;
        float z, y, x;
        glm::extractEulerAngleZYX(local, z, y, x);
        b->transform.position = glm::vec3(local[3]);
        b->transform.rotation = glm::degrees(glm::vec3(x, y, z));
    }
}

float weightOf(const EditMesh& m, size_t vertex, const std::string& bone) {
    if (!m.skinned() || vertex >= m.weights.size()) return 0.0f;
    for (auto& x : m.weights[vertex])
        if (x.bone < m.bones.size() && m.bones[x.bone] == bone) return x.w;
    return 0.0f;
}

namespace {
// Each bound bone's move since binding (studs space), and whether any moved at all.
bool moves(const SceneNode& part, std::vector<glm::mat4>& out) {
    const EditMesh& m = *part.editMesh;
    std::vector<SceneNode*> list = bones(const_cast<SceneNode*>(&part));
    out.assign(m.bones.size(), glm::mat4(1.0f));
    bool any = false;
    for (size_t i = 0; i < m.bones.size(); ++i)
        for (SceneNode* b : list)
            if (b->name == m.bones[i]) {
                out[i] = frame(b, &part) * m.bind[i];
                for (int c = 0; c < 4 && !any; ++c)
                    if (glm::length(out[i][c] - glm::mat4(1.0f)[c]) > 1e-5f) any = true;
                break;
            }
    return any;
}
} // namespace

bool deform(const SceneNode& part, std::vector<glm::vec3>& out) {
    if (!part.editMesh || !part.editMesh->skinned()) return false;
    std::vector<glm::mat4> mv;
    moves(part, mv);
    const EditMesh& m = *part.editMesh;
    glm::vec3 S = glm::max(glm::abs(part.transform.scale), glm::vec3(1e-6f));
    out.resize(m.verts.size());
    for (size_t v = 0; v < m.verts.size(); ++v) {
        glm::vec4 p(m.verts[v] * S, 1.0f);
        glm::vec3 acc(0.0f);
        float total = 0.0f;
        for (auto& x : m.weights[v]) {
            if (x.bone >= mv.size()) continue;
            acc += x.w * glm::vec3(mv[x.bone] * p);
            total += x.w;
        }
        if (total < 1.0f) acc += (1.0f - total) * glm::vec3(p);   // not fully held: the rest stays put
        out[v] = acc / S;
    }
    return true;
}

void update(SceneNode* root) {
    if (!root) return;
    struct Seen { const EditMesh* mesh = nullptr; const void* gpu = nullptr; std::vector<float> sig; bool bent = false; };
    static std::unordered_map<uint64_t, Seen> seen;
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart() || !n->editMesh || !n->editMesh->skinned()) continue;
        std::vector<glm::mat4> mv;
        bool bent = moves(*n, mv);
        std::vector<float> sig;
        sig.reserve(mv.size() * 16 + 3);
        for (auto& mat : mv) for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) sig.push_back(mat[c][r]);
        for (int i = 0; i < 3; ++i) sig.push_back(n->transform.scale[i]);
        Seen& s = seen[n->id];
        bool same = s.mesh == n->editMesh.get() && s.gpu == n->mesh.get() && s.sig.size() == sig.size();
        for (size_t i = 0; same && i < sig.size(); ++i) same = std::abs(s.sig[i] - sig[i]) < 1e-6f;
        if (same) continue;
        if (bent) {
            std::vector<glm::vec3> posed;
            deform(*n, posed);
            MeshEdit::refresh(*n, &posed);
        } else if (s.bent) {
            MeshEdit::refresh(*n);
        }
        s.mesh = n->editMesh.get();
        s.gpu = n->mesh.get();
        s.sig = std::move(sig);
        s.bent = bent;
    }
}

void carry(const EditMesh& before, EditMesh& after) {
    if (!before.skinned()) return;
    after.bones = before.bones;
    after.bind = before.bind;
    after.weights.assign(after.verts.size(), {});
    for (size_t v = 0; v < after.verts.size(); ++v) {
        size_t best = 0;
        float bd = 1e30f;
        for (size_t o = 0; o < before.verts.size(); ++o) {
            float d = glm::length(before.verts[o] - after.verts[v]);
            if (d < bd) { bd = d; best = o; if (d < 1e-7f) break; }
        }
        if (bd < 1e30f) after.weights[v] = before.weights[best];
    }
}

} // namespace Skin
