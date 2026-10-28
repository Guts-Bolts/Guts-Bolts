#include "Skin.h"
#include "EditMesh.h"
#include "SceneNode.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <algorithm>
#include <chrono>
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
    for (const SceneNode* b = bone; b && b != part && b->isBone(); b = b->parent)
        f = rigid(b->transform) * glm::mat4_cast(b->rig.live) * f;
    return f;
}

glm::mat4 partSpace(const SceneNode* part) {
    glm::mat4 m = part ? part->worldMatrix() : glm::mat4(1.0f);
    for (int i = 0; i < 3; ++i) {   // take the part's size out
        glm::vec3 axis(m[i]);
        float len = glm::length(axis);
        if (len > 1e-9f) m[i] = glm::vec4(axis / len, 0.0f);
    }
    return m;
}

glm::vec3 localTip(const SceneNode* bone) {
    for (auto& c : bone->children)
        if (c->isBone() && glm::length(c->transform.position) > 1e-3f) return c->transform.position;
    // No child: its own Length, else as long as the bone it's in (or a stud), along its own up.
    if (bone->rig.length > 1e-4f) return {0.0f, bone->rig.length, 0.0f};
    float len = 1.0f;
    if (bone->parent && bone->parent->isBone()) len = std::max(0.2f, glm::length(bone->transform.position));
    return {0.0f, len, 0.0f};
}

glm::vec3 tip(SceneNode* bone, SceneNode* part) {
    return glm::vec3(frame(bone, part) * glm::vec4(localTip(bone), 1.0f));
}

bool bindAutomatic(SceneNode& part) {
    if (!part.editMesh) return false;
    std::vector<SceneNode*> list = bones(&part);
    if (list.empty()) return false;
    still(part);   // bind to where the bones are posed, not mid-wobble
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
    still(part);
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

namespace {
// Set one vertex's weight for bone bi; the other bones share what's left.
void setWeight(std::vector<EditMesh::Weight>& w, int bi, float weight) {
    weight = std::clamp(weight, 0.0f, 1.0f);
    w.erase(std::remove_if(w.begin(), w.end(), [&](const EditMesh::Weight& x) { return x.bone == bi; }), w.end());
    float others = 0.0f;
    for (auto& x : w) others += x.w;
    if (others > 1e-6f) for (auto& x : w) x.w *= (1.0f - weight) / others;
    if (weight > 0.0f) w.push_back({(uint16_t)bi, weight});
    tidy(w);
}
float weightIn(const std::vector<EditMesh::Weight>& w, int bi) {
    for (auto& x : w) if (x.bone == bi) return x.w;
    return 0.0f;
}
} // namespace

bool paint(SceneNode& part, const std::vector<float>& amount, const std::string& bone, int mode) {
    if (!part.editMesh) return false;
    bool any = false;
    for (float a : amount) if (a > 0.0f) { any = true; break; }
    if (!any) return false;
    EditMesh& m = MeshEdit::own(part);
    if (!m.skinned()) { m.bones.clear(); m.bind.clear(); m.weights.assign(m.verts.size(), {}); }
    still(part);
    int bi = boneIndex(m, part, bone);
    if (bi < 0) return false;
    const size_t nv = std::min(m.verts.size(), amount.size());
    std::vector<float> now(m.verts.size());
    for (size_t v = 0; v < m.verts.size(); ++v) now[v] = weightIn(m.weights[v], bi);
    std::vector<float> want = now;
    if (mode == 2) {   // smooth: each one towards the average of the corners it shares an edge with
        std::vector<float> sum(m.verts.size(), 0.0f), count(m.verts.size(), 0.0f);
        for (const auto& f : m.faces)
            for (size_t i = 0; i < f.size(); ++i) {
                uint32_t a = f[i], b = f[(i + 1) % f.size()];
                if (a >= now.size() || b >= now.size()) continue;
                sum[a] += now[b]; count[a] += 1.0f;
                sum[b] += now[a]; count[b] += 1.0f;
            }
        for (size_t v = 0; v < nv; ++v)
            if (amount[v] > 0.0f && count[v] > 0.0f) want[v] = now[v] + (sum[v] / count[v] - now[v]) * std::min(1.0f, amount[v]);
    } else {
        for (size_t v = 0; v < nv; ++v) {
            float a = std::clamp(amount[v], 0.0f, 1.0f);
            if (a > 0.0f) want[v] = mode == 1 ? now[v] * (1.0f - a) : now[v] + (1.0f - now[v]) * a;
        }
    }
    bool changed = false;
    for (size_t v = 0; v < nv; ++v)
        if (std::abs(want[v] - now[v]) > 1e-5f) { setWeight(m.weights[v], bi, want[v] < 0.002f ? 0.0f : want[v]); changed = true; }
    return changed;
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
    still(part);
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

// ---------------------------------------------------------------------------
// Bones that move by themselves: Jiggle, LookAt and IK. Each one gets an extra
// turn (rig.live) on top of where it's posed, worked out again every frame.
// ---------------------------------------------------------------------------
namespace {
bool g_live = true;
const glm::quat kNoTurn(1.0f, 0.0f, 0.0f, 0.0f);

// The shortest turn that takes direction a onto direction b.
glm::quat between(glm::vec3 a, glm::vec3 b) {
    float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-6f || lb < 1e-6f) return kNoTurn;
    a /= la; b /= lb;
    float d = glm::dot(a, b);
    if (d > 0.999999f) return kNoTurn;
    if (d < -0.999999f) {   // straight back: turn half way round any side axis
        glm::vec3 side = std::abs(a.x) < 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        return glm::angleAxis(glm::pi<float>(), glm::normalize(glm::cross(a, side)));
    }
    glm::vec3 c = glm::cross(a, b);
    return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
}

glm::quat partly(const glm::quat& q, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    return amount >= 1.0f ? q : glm::slerp(kNoTurn, q, amount);
}

// The world turn of a bone frame, as a quaternion.
glm::quat turnOf(const glm::mat4& world) {
    glm::mat3 r(world);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    return glm::quat_cast(r);
}

SceneNode* findIn(SceneNode* root, uint64_t id) {
    if (!root || !id) return nullptr;
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        if (n->id == id) return n;
        for (auto& c : n->children) stack.push_back(c.get());
    }
    return nullptr;
}

SceneNode* topOf(SceneNode* n) {
    while (n && n->parent) n = n->parent;
    return n;
}

void jiggle(SceneNode& b, SceneNode& part, const glm::mat4& W, float dt) {
    BoneRig& r = b.rig;
    const glm::mat4 F = W * frame(&b, &part);   // (its own live is off: where it should be)
    const glm::vec3 head(F[3]);
    const glm::vec3 local = localTip(&b);
    const float len = glm::length(local);
    if (len < 1e-4f) return;
    const glm::vec3 rest(F * glm::vec4(local, 1.0f));
    // First frame, or it jumped (respawned, teleported): start at rest.
    if (!r.tipStarted || !std::isfinite(r.tipPos.x) || glm::length(r.tipPos - head) > len * 4.0f + 2.0f) {
        r.tipPos = rest;
        r.tipVel = glm::vec3(0.0f);
        r.tipStarted = true;
    }
    // A spring: stiffness 0 swings about once every 3 seconds, 1 about 3 times a second.
    const float w = 2.0f + 18.0f * std::clamp(r.stiffness, 0.0f, 1.0f);
    const float k = w * w;
    const float c = 2.0f * (0.05f + 0.95f * std::clamp(r.damping, 0.0f, 1.0f)) * w;
    const int steps = std::clamp((int)std::ceil(dt * 240.0f), 1, 12);
    const float h = dt / (float)steps;
    for (int i = 0; i < steps; ++i) {
        glm::vec3 a = k * (rest - r.tipPos) - c * r.tipVel + glm::vec3(0.0f, -r.gravity, 0.0f);
        r.tipVel += a * h;
        r.tipPos += r.tipVel * h;
        // The bone doesn't stretch: keep the tip its length from the joint.
        glm::vec3 d = r.tipPos - head;
        float dl = glm::length(d);
        glm::vec3 dir = dl > 1e-6f ? d / dl : glm::normalize(rest - head);
        r.tipPos = head + dir * len;
        r.tipVel -= dir * glm::dot(r.tipVel, dir);
    }
    const glm::quat q = turnOf(F);
    glm::vec3 want = glm::inverse(q) * (r.tipPos - head);
    r.live = partly(between(local, want), r.influence);
}

void lookAt(SceneNode& b, SceneNode& part, const glm::mat4& W, glm::vec3 target) {
    const glm::mat4 F = W * frame(&b, &part);
    glm::vec3 want = glm::inverse(turnOf(F)) * (target - glm::vec3(F[3]));
    b.rig.live = partly(between(localTip(&b), want), b.rig.influence);
}

// Cyclic coordinate descent: from the end bone up, turn each bone of the chain so
// the end's tip swings toward the target; a few rounds of that and it's there.
void reach(SceneNode& end, SceneNode& part, const glm::mat4& W, glm::vec3 target) {
    std::vector<SceneNode*> chain;
    for (SceneNode* b = &end; b && b->isBone() && (int)chain.size() < std::clamp(end.rig.chain, 1, 16); b = b->parent)
        chain.push_back(b);
    const glm::vec3 local = localTip(&end);
    auto tipNow = [&] { return glm::vec3(W * frame(&end, &part) * glm::vec4(local, 1.0f)); };
    for (int round = 0; round < 12; ++round) {
        for (SceneNode* b : chain) {
            const glm::mat4 F = W * frame(b, &part);
            const glm::vec3 head(F[3]);
            const glm::quat swing = between(tipNow() - head, target - head);
            const glm::quat q = turnOf(F);
            b->rig.live = glm::normalize(b->rig.live * (glm::inverse(q) * swing * q));
        }
        if (glm::length(tipNow() - target) < 1e-3f) break;
    }
    const float amount = end.rig.influence;
    if (amount < 1.0f) for (SceneNode* b : chain) b->rig.live = partly(b->rig.live, amount);
}

// Where a LookAt / IK bone's target is (anything in the same game, by id).
glm::vec3 targetPoint(const SceneNode& part, uint64_t id, bool& found) {
    found = false;
    SceneNode* t = findIn(topOf(const_cast<SceneNode*>(&part)), id);
    if (!t) return glm::vec3(0.0f);
    found = true;
    return glm::vec3(t->worldMatrix()[3]);
}
} // namespace

void setLive(bool on) { g_live = on; }
bool live() { return g_live; }

const char* typeName(int type) {
    switch ((BoneType)type) {
        case BoneType::Jiggle: return "Jiggle";
        case BoneType::LookAt: return "LookAt";
        case BoneType::IK:     return "IK";
        default:               return "Normal";
    }
}
int typeFrom(const std::string& name) {
    for (int i = 0; i < 4; ++i) if (name == typeName(i)) return i;
    if (name == "Spring") return (int)BoneType::Jiggle;
    if (name == "Look At") return (int)BoneType::LookAt;
    return -1;
}

void still(SceneNode& part) {
    for (SceneNode* b : bones(&part)) { b->rig.live = kNoTurn; b->rig.tipStarted = false; }
}

void solve(SceneNode& part, float dt) {
    std::vector<SceneNode*> list = bones(&part);
    bool any = false;
    for (SceneNode* b : list) if (b->rig.type != BoneType::Normal) { any = true; break; }
    if (!any) {
        for (SceneNode* b : list) b->rig.live = kNoTurn;
        return;
    }
    for (SceneNode* b : list) b->rig.live = kNoTurn;
    const glm::mat4 W = partSpace(&part);
    dt = std::clamp(dt, 0.0f, 0.05f);
    // IK first (it turns the bones it's in), then looks, then the jiggles hang off the result.
    for (SceneNode* b : list)
        if (b->rig.type == BoneType::IK) {
            bool found;
            glm::vec3 t = targetPoint(part, b->rig.target, found);
            if (found) reach(*b, part, W, t);
        }
    for (SceneNode* b : list)
        if (b->rig.type == BoneType::LookAt) {
            bool found;
            glm::vec3 t = targetPoint(part, b->rig.target, found);
            if (found) lookAt(*b, part, W, t);
        }
    for (SceneNode* b : list)
        if (b->rig.type == BoneType::Jiggle) jiggle(*b, part, W, dt);
}

void update(SceneNode* root) {
    if (!root) return;
    // How long since the last frame (for the jiggles).
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    struct Seen { const EditMesh* mesh = nullptr; const void* gpu = nullptr; std::vector<float> sig; bool bent = false; };
    static std::unordered_map<uint64_t, Seen> seen;
    std::vector<SceneNode*> stack{root};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        for (auto& c : n->children) stack.push_back(c.get());
        if (!n->isPart()) continue;
        if (g_live) solve(*n, dt);
        else if (!n->children.empty()) for (SceneNode* b : bones(n)) b->rig.live = kNoTurn;
        if (!n->editMesh || !n->editMesh->skinned()) continue;
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
