#include "Streaming.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>

namespace Streaming {
namespace {

// A ball around every part in (and under) n. False if there are no parts.
bool ballOf(const SceneNode* n, glm::vec3& center, float& radius) {
    glm::vec3 lo(1e30f), hi(-1e30f);
    bool any = false;
    std::vector<const SceneNode*> stack{n};
    while (!stack.empty()) {
        const SceneNode* s = stack.back();
        stack.pop_back();
        if (s->isPart()) {
            const glm::mat4 m = s->worldMatrix();
            const glm::vec3 c(m[3]);
            const float r = 0.5f * std::sqrt(glm::dot(glm::vec3(m[0]), glm::vec3(m[0])) + glm::dot(glm::vec3(m[1]), glm::vec3(m[1])) +
                                             glm::dot(glm::vec3(m[2]), glm::vec3(m[2])));
            if (std::isfinite(r) && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z)) {
                lo = glm::min(lo, c - glm::vec3(r));
                hi = glm::max(hi, c + glm::vec3(r));
                any = true;
            }
        }
        for (const auto& k : s->children) stack.push_back(k.get());
    }
    if (!any) return false;
    center = 0.5f * (lo + hi);
    radius = 0.5f * glm::length(hi - lo);
    return true;
}

bool canBePiece(const SceneNode* n) {
    return n->kind == NodeKind::Part || n->kind == NodeKind::Model || n->kind == NodeKind::Tool;
}

void collect(Scene& scene, SceneNode* n, float target, std::vector<Piece>& out, int depth) {
    if (scene.isCharacterPart(n) || scene.isServiceFolder(n) || !canBePiece(n)) return;
    Piece p;
    p.node = n;
    if (!ballOf(n, p.center, p.radius)) return;   // (nothing to see: it stays)
    // A Folder, or a group spread wider than the circle: its insides are the pieces.
    // (A part is always one piece; so is anything with a Humanoid, like an NPC.)
    const bool group = n->kind == NodeKind::Model && !n->findChild("Humanoid");
    if (group && depth < 8 && p.radius > target) {
        for (auto& k : n->children) collect(scene, k.get(), target, out, depth + 1);
        return;
    }
    out.push_back(p);
}

bool prune(nlohmann::json& j, const std::unordered_set<uint64_t>& leaveOut) {
    auto it = j.find("children");
    if (it == j.end() || !it->is_array()) return false;
    auto& kids = *it;
    for (size_t i = 0; i < kids.size();) {
        auto id = kids[i].find("id");
        if (id != kids[i].end() && id->is_number_unsigned() && leaveOut.count(id->get<uint64_t>())) kids.erase(kids.begin() + (long)i);
        else { prune(kids[i], leaveOut); ++i; }
    }
    return true;
}

} // namespace

std::vector<Piece> pieces(Scene& scene, float targetRadius) {
    std::vector<Piece> out;
    for (auto& c : scene.root()->children) collect(scene, c.get(), targetRadius, out, 0);
    return out;
}

bool wanted(const Piece& p, const glm::vec3& focus, float targetRadius, bool haveIt) {
    const float edge = glm::length(p.center - focus) - p.radius;   // how far its nearest bit could be
    return edge < (haveIt ? targetRadius * kKeepFactor : targetRadius);
}

std::string sceneWithout(Scene& scene, const std::unordered_set<uint64_t>& leaveOut) {
    std::string text = Serializer::saveScene(scene);
    if (leaveOut.empty()) return text;
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.contains("workspace")) return text;
    prune(j["workspace"], leaveOut);
    return j.dump();
}

} // namespace Streaming
