#include "SoftTissue.h"
#include "EditMesh.h"
#include "Scene.h"
#include "SceneNode.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace SoftTissue {

namespace {

struct Dented {
    const EditMesh* mesh = nullptr;     // the mesh the rest shape belongs to
    std::vector<glm::vec3> rest;        // how it was before any hit
    std::vector<float> torn;            // 0..1 per corner: how much of the dent stays (a tear)
    float since = 0.0f;                 // seconds since the last refresh of its shape
};

std::unordered_map<uint64_t, Dented>& dents() {
    static std::unordered_map<uint64_t, Dented> d;
    return d;
}

bool has(const std::string& name, const char* word) {
    std::string n = name;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return n.find(word) != std::string::npos;
}

} // namespace

Feel feelOf(const SceneNode& part) {
    if (has(part.name, "head")) return {0.55f, 0.25f};
    if (has(part.name, "torso") || has(part.name, "body") || has(part.name, "belly")) return {1.25f, 0.9f};
    if (has(part.name, "arm") || has(part.name, "leg") || has(part.name, "hand") || has(part.name, "foot")) return {0.9f, 0.6f};
    return {1.0f, 0.5f};
}

bool dent(SceneNode& part, const glm::vec3& point, const glm::vec3& normal, const glm::vec3& velocity, bool springBack) {
    if (!part.isPart()) return false;
    if (!part.editMesh) {
        if (part.primitiveType == PrimitiveType::None) return false;
        MeshEdit::attach(part, MeshEdit::fromPrimitive(part.primitiveType));
    }
    if (!part.editMesh || part.editMesh->verts.empty()) return false;
    const glm::mat4 world = part.worldMatrix();
    const glm::mat3 linear(world);
    if (std::abs(glm::determinant(linear)) < 1e-7f) return false;
    const glm::mat3 toLocal = glm::inverse(linear);
    EditMesh& mesh = MeshEdit::own(part);

    // Remember its shape before the first hit (to spring back to).
    Dented* d = nullptr;
    if (springBack) {
        auto it = dents().find(part.id);
        if (it == dents().end() || it->second.mesh != &mesh || it->second.rest.size() != mesh.verts.size()) {
            Dented fresh;
            fresh.rest = mesh.verts;
            fresh.torn.assign(mesh.verts.size(), 0.0f);
            it = dents().insert_or_assign(part.id, std::move(fresh)).first;
        }
        d = &it->second;
        d->mesh = &mesh;
    }

    const Feel feel = feelOf(part);
    const float speed = glm::length(velocity);
    const float radius = std::clamp(0.18f + speed * 0.0018f, 0.18f, 0.58f);
    const float depth = std::clamp(0.025f + speed * 0.00015f, 0.025f, 0.14f) * feel.depth;
    const glm::vec3 n = glm::length(normal) > 1e-5f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
    // A part this thick tears when a dent goes deeper than about a third of it.
    const glm::vec3 S = part.transform.scale;
    const float tearAt = std::max(0.08f, std::min(S.x, std::min(S.y, S.z)) * 0.3f);
    bool changed = false, tore = false;
    for (size_t i = 0; i < mesh.verts.size(); ++i) {
        glm::vec3& vertex = mesh.verts[i];
        const glm::vec3 worldVertex = glm::vec3(world * glm::vec4(vertex, 1.0f));
        const float distance = glm::length(worldVertex - point);
        if (distance > radius * 1.8f) continue;
        const float falloff = std::exp(-3.2f * distance / radius);
        const float rim = std::exp(-std::pow((distance - radius * 0.68f) / (radius * 0.22f), 2.0f));
        vertex += toLocal * (-n * (depth * falloff) + n * (depth * 0.08f * rim));
        changed = true;
        if (d) {
            const float deep = glm::length(linear * (vertex - d->rest[i]));
            // (Near the hit as it was before any dent: a deep dent has moved away from the surface.)
            const float fromRest = glm::length(glm::vec3(world * glm::vec4(d->rest[i], 1.0f)) - point);
            const float closeness = std::exp(-3.2f * std::min(distance, fromRest) / radius);
            if (deep > tearAt && closeness > 0.5f && d->torn[i] <= 0.0f) { d->torn[i] = 0.6f; tore = true; }
        }
    }
    if (!changed) return false;
    mesh.normals.clear();
    MeshEdit::refresh(part);
    return tore;
}

void update(Scene& scene, float dt) {
    if (dents().empty() || dt <= 0.0f) return;
    for (auto it = dents().begin(); it != dents().end();) {
        SceneNode* part = scene.findById(it->first);
        Dented& d = it->second;
        if (!part || !part->editMesh || part->editMesh.get() != d.mesh || part->editMesh->verts.size() != d.rest.size()) {
            it = dents().erase(it);
            continue;
        }
        EditMesh& mesh = *part->editMesh;
        const Feel feel = feelOf(*part);
        const float k = 1.0f - std::exp(-feel.recover * dt);
        float most = 0.0f;
        for (size_t i = 0; i < mesh.verts.size(); ++i) {
            // Torn bits only come back part of the way.
            const glm::vec3 target = d.rest[i];
            glm::vec3 off = mesh.verts[i] - target;
            const float keep = d.torn[i];
            const glm::vec3 goal = target + off * keep;
            mesh.verts[i] += (goal - mesh.verts[i]) * k;
            most = std::max(most, glm::length(mesh.verts[i] - goal));
        }
        // Redraw the shape about 20 times a second (not every frame) while it moves.
        d.since += dt;
        if (d.since >= 0.05f || most < 1e-4f) {
            d.since = 0.0f;
            mesh.normals.clear();
            MeshEdit::refresh(*part);
        }
        // All the way back (or as far as a tear lets it): done (a tear stays as a scar).
        if (most < 1e-4f) it = dents().erase(it);
        else ++it;
    }
}

float depth(const SceneNode& part) {
    auto it = dents().find(part.id);
    if (it == dents().end() || !part.editMesh || part.editMesh->verts.size() != it->second.rest.size()) return 0.0f;
    const glm::mat3 linear(part.worldMatrix());
    float most = 0.0f;
    for (size_t i = 0; i < it->second.rest.size(); ++i)
        most = std::max(most, glm::length(linear * (part.editMesh->verts[i] - it->second.rest[i])));
    return most;
}

void clear() { dents().clear(); }

} // namespace SoftTissue
