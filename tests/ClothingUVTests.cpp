// The auto UV map for bundle body parts: any shape gets laid out on the shirt / pants
// template the same way the default body is, so clothing fits it.
#include "scene/EditMesh.h"
#include "scene/Player.h"
#include "scene/PlayerModel.h"
#include "scene/SceneNode.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// A UV corner, in template pixels (585 x 559, top row 0).
glm::vec2 pixel(glm::vec2 uv) { return {uv.x * 585.0f, (1.0f - uv.y) * 559.0f}; }

// Every corner of every face that faces `dir` lands inside the box x, y, w, h (pixels).
bool facesIn(const EditMesh& m, glm::vec3 dir, float x, float y, float w, float h) {
    int seen = 0;
    for (size_t f = 0; f < m.faces.size(); ++f) {
        if (glm::dot(glm::normalize(MeshEdit::faceNormal(m, m.faces[f])), dir) < 0.9f) continue;
        ++seen;
        for (glm::vec2 uv : m.uvs[f]) {
            glm::vec2 p = pixel(uv);
            if (p.x < x || p.x > x + w || p.y < y || p.y > y + h) return false;
        }
    }
    return seen > 0;
}
} // namespace

int main() {
    // The built-in torso: the auto map gives exactly the layout it already has.
    auto torso = PlayerModel::mesh("Torso");
    check(torso && torso->uvs.size() == torso->faces.size(), "the default torso has clothing UVs");
    if (torso) {
        EditMesh copy = *torso;
        check(PlayerModel::clothingUVs(copy, "Torso"), "the torso can be mapped");
        float most = 0.0f;
        for (size_t f = 0; f < copy.uvs.size(); ++f)
            for (size_t c = 0; c < copy.uvs[f].size(); ++c) most = std::max(most, glm::length(copy.uvs[f][c] - torso->uvs[f][c]));
        check(most < 0.01f, "re-mapping the default torso keeps its layout (moved " + std::to_string(most) + ")");
    }

    // A big block off to the side, as a new torso: each side goes in its torso box.
    auto block = MeshEdit::fromPrimitive(PrimitiveType::Cube);
    for (glm::vec3& p : block->verts) p = p * glm::vec3(3.0f, 2.0f, 1.0f) + glm::vec3(5.0f, -1.0f, 2.0f);
    check(PlayerModel::clothingUVs(*block, "Torso"), "a block can be a torso");
    check(block->uvs.size() == block->faces.size(), "every face got UVs");
    check(facesIn(*block, {0, 0, 1}, 231, 74, 128, 128), "torso front: the front box");
    check(facesIn(*block, {0, 0, -1}, 427, 74, 128, 128), "torso back: the back box");
    check(facesIn(*block, {0, 1, 0}, 231, 8, 128, 64), "torso top: the top box");
    check(facesIn(*block, {-1, 0, 0}, 165, 74, 64, 128), "torso right side (-X): the right box");

    // Fills the whole box, whatever its size: the front's corners reach the box corners.
    float lo = 1e9f, hi = -1e9f;
    for (size_t f = 0; f < block->faces.size(); ++f)
        if (MeshEdit::faceNormal(*block, block->faces[f]).z > 0.9f)
            for (glm::vec2 uv : block->uvs[f]) { lo = std::min(lo, pixel(uv).x); hi = std::max(hi, pixel(uv).x); }
    check(lo < 233.0f && hi > 357.0f, "the front stretches over the whole front box");

    // Arms and legs: the right ones on the left of the picture, the left ones on the right.
    auto arm = MeshEdit::fromPrimitive(PrimitiveType::Cylinder);
    check(PlayerModel::clothingUVs(*arm, "Right Arm"), "a cylinder can be a right arm");
    check(facesIn(*arm, {0, 0, 1}, 217, 355, 64, 128), "right arm front: its box");
    auto leg = MeshEdit::fromPrimitive(PrimitiveType::Cube);
    check(PlayerModel::clothingUVs(*leg, "Left Leg"), "a block can be a left leg");
    check(facesIn(*leg, {0, 0, 1}, 308, 355, 64, 128), "left leg front: its box");
    check(facesIn(*leg, {0, -1, 0}, 308, 485, 64, 64), "left leg bottom: its box");

    // Clothing doesn't cover the head.
    auto head = MeshEdit::fromPrimitive(PrimitiveType::Sphere);
    check(!PlayerModel::clothingUVs(*head, "Head"), "the Head isn't mapped");
    check(!PlayerModel::clothingUVs(*head, "Wing"), "neither is a part clothing doesn't know");

    // A bundle body part saved with no UV map (made before the auto map, like the girl
    // torso bundle) gets one when it's worn, so a shirt still covers it.
    {
        nlohmann::json j = {{"v", {-0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0, 0, -0.5}},
                            {"f", {{0, 1, 2, 3}, {0, 4, 1}}}, {"size", {1, 1, 0.5}}, {"pos", {0, 1.5, 0}}};
        auto m = Player::bodyPartMesh(j, "Torso");
        check(m && m->uvs.size() == m->faces.size(), "a bundle torso with no UVs gets clothing UVs");
        check(m && facesIn(*m, {0, 0, 1}, 231, 74, 128, 128), "its front shows the shirt's front");
    }

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
