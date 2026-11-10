#include "Shatter.h"
#include "Physics.h"
#include "Scene.h"
#include "SceneNode.h"
#include "../renderer/MeshLibrary.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace Shatter {

namespace {

struct Piece { uint64_t id; float age; bool small; };
std::vector<Piece>& pieces() {
    static std::vector<Piece> p;
    return p;
}

} // namespace

bool breakable(const SceneNode& part) {
    if (!part.isPart()) return false;
    if (std::find(part.tags.begin(), part.tags.end(), "Breakable") != part.tags.end()) return true;
    const Attribute* a = part.findAttribute("Breakable");
    return a && a->type == Attribute::Bool && a->b;
}

float breakSpeed(const SceneNode& part) {
    const Attribute* a = part.findAttribute("BreakSpeed");
    return a && a->type == Attribute::Number ? std::max(1.0f, (float)a->n) : 45.0f;
}

std::vector<Box> split(const glm::vec3& hit, int count, uint32_t seed) {
    std::mt19937 rng(seed);
    auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    std::vector<Box> boxes{{glm::vec3(0.0f), glm::vec3(1.0f)}};
    // Keep cutting the piece that's biggest for how near the hit it is, across its
    // longest side, somewhere near its middle. Pieces by the hit end up small.
    while ((int)boxes.size() < count) {
        int pick = 0;
        float best = -1.0f;
        for (int i = 0; i < (int)boxes.size(); ++i) {
            const Box& b = boxes[(size_t)i];
            const float big = b.size.x * b.size.y * b.size.z;
            const float near = 1.0f / (0.25f + glm::length(b.center - hit));
            const float score = big * near * near;
            if (score > best) { best = score; pick = i; }
        }
        Box b = boxes[(size_t)pick];
        int axis = 0;
        for (int k = 1; k < 3; ++k) if (b.size[k] > b.size[axis]) axis = k;
        if (b.size[axis] < 0.02f) break;   // (nothing left worth cutting)
        const float at = rnd(0.3f, 0.7f);
        Box a = b, c = b;
        a.size[axis] = b.size[axis] * at;
        c.size[axis] = b.size[axis] * (1.0f - at);
        a.center[axis] = b.center[axis] - b.size[axis] * 0.5f + a.size[axis] * 0.5f;
        c.center[axis] = b.center[axis] + b.size[axis] * 0.5f - c.size[axis] * 0.5f;
        boxes[(size_t)pick] = a;
        boxes.push_back(c);
    }
    return boxes;
}

std::vector<SceneNode*> breakPart(Scene& scene, SceneNode* part, const glm::vec3& point, float force) {
    std::vector<SceneNode*> out;
    if (!part || !part->isPart() || !part->parent || scene.isCharacterPart(part)) return out;
    const glm::vec3 S = part->transform.scale;
    const float volume = S.x * S.y * S.z;
    if (volume < 0.02f) return out;
    force = std::clamp(force, 0.05f, 2.0f);
    // More pieces for bigger things and harder hits.
    const int count = std::clamp((int)(4.0f + std::cbrt(volume) * 5.0f * (0.5f + force)), 4, 28);
    const glm::mat4 M = part->worldMatrix();
    glm::vec3 local = glm::vec3(glm::inverse(M) * glm::vec4(point, 1.0f));
    local = glm::clamp(local, glm::vec3(-0.5f), glm::vec3(0.5f));
    const std::vector<Box> boxes = split(local, count, (uint32_t)(part->id * 2654435761u) ^ (uint32_t)(force * 1000.0f));

    const glm::vec3 base = part->velocity;
    const float speed = 6.0f + 22.0f * force;
    std::mt19937 rng((uint32_t)part->id);
    auto rnd = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    for (const Box& b : boxes) {
        auto n = std::make_unique<SceneNode>(part->name + " Piece");
        n->primitiveType = PrimitiveType::Cube;
        n->mesh = MeshLibrary::get(PrimitiveType::Cube);
        // Where it sits: the part's own turn, at its piece of the box.
        const glm::vec3 world = glm::vec3(M * glm::vec4(b.center, 1.0f));
        n->transform.position = world;
        n->transform.rotation = part->transform.rotation;
        n->transform.scale = glm::max(S * b.size * 0.97f, glm::vec3(0.05f));   // (tiny gaps so they don't start stuck)
        n->color = part->color;
        n->material = part->material;
        n->materialVariant = part->materialVariant;
        n->transparency = part->transparency;
        n->density = part->density;
        n->friction = part->friction;
        n->elasticity = part->elasticity;
        n->anchored = false;
        n->canCollide = true;
        n->castShadow = part->castShadow;
        n->collisionGroup = part->collisionGroup;
        // Thrown away from the hit, faster the closer it was.
        glm::vec3 away = world - point;
        const float d = glm::length(away);
        away = d > 1e-3f ? away / d : glm::vec3(0, 1, 0);
        const float k = 1.0f / (1.0f + d * 0.6f);
        n->velocity = base + (away + glm::vec3(rnd(-0.3f, 0.3f), rnd(0.0f, 0.4f), rnd(-0.3f, 0.3f))) * speed * k;
        n->angularVelocity = glm::vec3(rnd(-1, 1), rnd(-1, 1), rnd(-1, 1)) * 8.0f * force;
        SceneNode* made = part->parent->addChild(std::move(n));
        const glm::vec3 sz = made->transform.scale;
        pieces().push_back({made->id, 0.0f, sz.x * sz.y * sz.z < 0.6f});
        out.push_back(made);
    }
    scene.removeNode(part);
    scene.markDirty();
    // Too many lying around: the oldest little ones go.
    while ((int)pieces().size() > kMostPieces) {
        auto it = std::find_if(pieces().begin(), pieces().end(), [](const Piece& p) { return p.small; });
        if (it == pieces().end()) { pieces().erase(pieces().begin()); continue; }
        if (SceneNode* n = scene.findById(it->id)) scene.removeNode(n);
        pieces().erase(it);
    }
    return out;
}

void update(Scene& scene, float dt) {
    if (pieces().empty()) return;
    for (auto it = pieces().begin(); it != pieces().end();) {
        it->age += dt;
        SceneNode* n = scene.findById(it->id);
        if (!n) { it = pieces().erase(it); continue; }
        if (!it->small) {   // big ones stay (rubble); stop following them after a while
            if (it->age > kPieceLife) { it = pieces().erase(it); continue; }
            ++it;
            continue;
        }
        const float fade = kPieceLife - 2.0f;
        if (it->age > fade) n->transparency = std::max(n->transparency, std::clamp((it->age - fade) / 2.0f, 0.0f, 1.0f));
        if (it->age > kPieceLife) { scene.removeNode(n); it = pieces().erase(it); continue; }
        ++it;
    }
}

void clear() { pieces().clear(); }

} // namespace Shatter
