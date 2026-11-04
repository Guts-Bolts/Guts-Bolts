// Drawing many copies at once (src/renderer/Instancing.h): which parts can be drawn
// as copies of each other, and which have to be drawn one by one.
//
//   InstancingTests
#include "renderer/Instancing.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"

#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    Mesh::headless = true;
    SceneNode brick("Brick");
    brick.primitiveType = PrimitiveType::Cube;
    brick.mesh = MeshLibrary::get(PrimitiveType::Cube);
    const glm::mat4 turned = glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), {3, 1, 2}), 0.7f, glm::vec3(0, 1, 0)), {4, 1, 2});

    check(Instancing::canCopy(brick, turned, false), "a plain brick, moved, turned and stretched, can be a copy");
    check(!Instancing::canCopy(brick, turned, true), "water is drawn on its own");

    SceneNode shirt = SceneNode("Torso");
    shirt.mesh = brick.mesh;
    shirt.texture = "gb:123";
    check(!Instancing::canCopy(shirt, turned, false), "a part with a picture (clothing) is drawn on its own");

    SceneNode variant("Floor");
    variant.mesh = brick.mesh;
    variant.materialVariant = "Rusty";
    check(!Instancing::canCopy(variant, turned, false), "a part with a MaterialVariant is drawn on its own");

    SceneNode special("Old");
    special.mesh = brick.mesh;
    auto sm = std::make_unique<SceneNode>("Mesh", NodeKind::Value);
    Attribute a; a.name = "RobloxClass"; a.type = Attribute::String; a.s = "SpecialMesh";
    sm->attributes.push_back(a);
    special.addChild(std::move(sm));
    check(!Instancing::canCopy(special, turned, false), "a part with a Roblox SpecialMesh is drawn on its own");

    SceneNode noMesh("Empty");
    check(!Instancing::canCopy(noMesh, turned, false), "no mesh, nothing to copy");

    // A stretched parent turned at an angle makes a slanted shape: drawn on its own.
    glm::mat4 slanted = glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0, 0, 1)) * glm::scale(glm::mat4(1.0f), {3, 1, 1}) *
                        glm::rotate(glm::mat4(1.0f), 0.6f, glm::vec3(0, 0, 1));
    check(!Instancing::straightSides(slanted), "a slanted shape isn't straight-sided");
    check(!Instancing::canCopy(brick, slanted, false), "so it's drawn on its own");
    check(!Instancing::straightSides(glm::scale(glm::mat4(1.0f), {0, 1, 1})), "a squashed-flat part isn't either");

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
