#pragma once
// Drawing many copies of the same thing at once ("instancing"): a forest of the same
// tree, a fence of the same post, a city of the same brick. Instead of telling the
// graphics card about each part one by one, parts that share a mesh and have nothing
// special about them go in one list, and the whole list is drawn in one go.
//
// SceneRenderer::drawGeometry uses this for solid (not see-through) parts.
#include "../scene/SceneNode.h"
#include <cmath>
#include <glm/glm.hpp>

namespace Instancing {

// Fewer copies than this aren't worth the extra setup: they're drawn one by one.
inline constexpr int kMinCopies = 4;

// Moved, turned and stretched along its own sides only (no slanting from a stretched
// parent turned at an angle): the shader's quick way of lighting copies needs that.
inline bool straightSides(const glm::mat4& m) {
    const glm::vec3 x(m[0]), y(m[1]), z(m[2]);
    const float lx = glm::length(x), ly = glm::length(y), lz = glm::length(z);
    if (lx < 1e-6f || ly < 1e-6f || lz < 1e-6f) return false;
    const float tol = 1e-3f;
    return std::fabs(glm::dot(x, y)) <= tol * lx * ly && std::fabs(glm::dot(y, z)) <= tol * ly * lz &&
           std::fabs(glm::dot(x, z)) <= tol * lx * lz;
}

// Could this part be drawn as a copy? Parts with a picture on them (clothing, a face,
// a T-shirt), a MaterialVariant, a Roblox SpecialMesh or Bones are drawn one by one,
// because each needs its own settings. (`water` comes from Player::isWater.)
inline bool canCopy(const SceneNode& n, const glm::mat4& model, bool water) {
    if (water || !n.mesh || n.kind != NodeKind::Part) return false;
    if (!n.texture.empty() || !n.tshirt.empty() || !n.materialVariant.empty()) return false;
    for (const auto& c : n.children) {
        if (c->isBone()) return false;
        if (const Attribute* cls = c->findAttribute("RobloxClass"); cls && cls->type == Attribute::String && cls->s == "SpecialMesh")
            return false;
    }
    return straightSides(model);
}

} // namespace Instancing
