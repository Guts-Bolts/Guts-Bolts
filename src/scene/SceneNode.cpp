#include "SceneNode.h"
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace { uint64_t g_nextId = 1; }

uint64_t SceneNode::newId() { return g_nextId++; }
void SceneNode::reserveId(uint64_t used) { if (used >= g_nextId) g_nextId = used + 1; }

namespace {
template <typename Key, typename V>
V sampleKeys(const std::vector<Key>& keys, float t, V Key::*val, V fallback) {
    if (keys.empty()) return fallback;
    if (t <= keys.front().t) return keys.front().*val;
    for (size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].t) {
            const float span = keys[i].t - keys[i - 1].t;
            const float f = span > 1e-6f ? (t - keys[i - 1].t) / span : 1.0f;
            return keys[i - 1].*val + (keys[i].*val - keys[i - 1].*val) * f;
        }
    return keys.back().*val;
}
} // namespace

glm::vec3 sampleSequence(const std::vector<ColorKey>& keys, float t) { return sampleKeys(keys, t, &ColorKey::c, glm::vec3(1.0f)); }
float sampleSequence(const std::vector<NumberKey>& keys, float t) { return sampleKeys(keys, t, &NumberKey::v, 0.0f); }

glm::mat4 Transform::matrix() const {
    // Rotation order Z * Y * X (applied X first) to match how ImGuizmo composes
    // and decomposes Euler angles, so the gizmo stays in sync with the inspector.
    glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
    m = glm::rotate(m, glm::radians(rotation.z), {0,0,1});
    m = glm::rotate(m, glm::radians(rotation.y), {0,1,0});
    m = glm::rotate(m, glm::radians(rotation.x), {1,0,0});
    m = glm::scale(m, scale);
    return m;
}

std::string SceneNode::valueText() const {
    char buf[64];
    switch (value.type) {
        case Attribute::Bool:   return value.b ? "true" : "false";
        case Attribute::String: return value.s;
        case Attribute::Number:
            if (intValue || value.n == (double)(long long)value.n) std::snprintf(buf, sizeof(buf), "%lld", (long long)value.n);
            else std::snprintf(buf, sizeof(buf), "%.2f", value.n);
            return buf;
        default:
            std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f", value.v.x, value.v.y, value.v.z);
            return buf;
    }
}

SceneNode::SceneNode(std::string name, NodeKind kind)
    : id(newId()), name(std::move(name)), kind(kind) {}

SceneNode* SceneNode::addChild(std::unique_ptr<SceneNode> child) {
    child->parent = this;
    children.push_back(std::move(child));
    return children.back().get();
}

void SceneNode::removeChild(SceneNode* child) {
    children.erase(std::remove_if(children.begin(), children.end(),
        [child](const auto& p) { return p.get() == child; }), children.end());
}

std::unique_ptr<SceneNode> SceneNode::detachChild(SceneNode* child) {
    for (auto it = children.begin(); it != children.end(); ++it) {
        if (it->get() == child) {
            std::unique_ptr<SceneNode> out = std::move(*it);
            children.erase(it);
            out->parent = nullptr;
            return out;
        }
    }
    return nullptr;
}

SceneNode* SceneNode::findChild(const std::string& n, bool recursive) const {
    for (auto& c : children)
        if (c->name == n) return c.get();
    if (recursive)
        for (auto& c : children)
            if (SceneNode* f = c->findChild(n, true)) return f;
    return nullptr;
}

bool SceneNode::isAncestorOf(const SceneNode* other) const {
    for (const SceneNode* p = other ? other->parent : nullptr; p; p = p->parent)
        if (p == this) return true;
    return false;
}

glm::mat4 SceneNode::worldMatrix() const {
    glm::mat4 m = parent ? parentFrame() * transform.matrix() : transform.matrix();
    // A Jiggle / LookAt / IK bone's extra turn this frame (things attached to it follow).
    if (bone && rig.live != glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) m = m * glm::mat4_cast(rig.live);
    return m;
}

glm::mat4 SceneNode::parentFrame() const {
    if (!parent) return glm::mat4(1.0f);
    glm::mat4 m = parent->worldMatrix();
    if (bone && kind == NodeKind::Attachment && parent->kind == NodeKind::Part)
        for (int i = 0; i < 3; ++i) {   // take the part's size out
            glm::vec3 axis(m[i]);
            float len = glm::length(axis);
            if (len > 1e-9f) m[i] = glm::vec4(axis / len, 0.0f);
        }
    return m;
}

std::string SceneNode::fullName() const {
    if (!parent) return name;
    return parent->fullName() + "." + name;
}
