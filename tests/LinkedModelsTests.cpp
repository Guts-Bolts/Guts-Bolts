// Linked models: change what's inside one copy and the others match.
#include "renderer/Mesh.h"
#include "scene/LinkedModels.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

SceneNode* addModel(Scene& scene, const std::string& name) {
    SceneNode* m = scene.insert(std::make_unique<SceneNode>(name, NodeKind::Model), nullptr);
    for (int i = 0; i < 2; ++i) {
        auto p = std::make_unique<SceneNode>("Part" + std::to_string(i), NodeKind::Part);
        p->transform.position = glm::vec3((float)i * 2.0f, 0.5f, 0.0f);
        p->color = glm::vec3(0.5f);
        scene.insert(std::move(p), m);
    }
    return m;
}
SceneNode* copyOf(Scene& scene, SceneNode* m, const glm::vec3& at) {
    SceneNode* c = scene.insert(Serializer::clone(*m), nullptr);
    c->transform.position = at;
    return c;
}
} // namespace

int main() {
    Mesh::headless = true;
    Scene scene;
    SceneNode* a = addModel(scene, "Lamp");
    LinkedModels::link(a, LinkedModels::newLink());
    SceneNode* b = copyOf(scene, a, glm::vec3(20.0f, 0.0f, 0.0f));
    SceneNode* c = copyOf(scene, a, glm::vec3(-20.0f, 0.0f, 5.0f));
    b->name = "Lamp by the door";
    check(!LinkedModels::linkOf(a).empty() && LinkedModels::linkOf(b) == LinkedModels::linkOf(a), "copies keep the link");

    LinkedModels::Tracker t;
    t.remember(scene);
    check(t.sync(scene) == 0, "nothing changed: nothing to copy");

    a->children[0]->color = glm::vec3(1.0f, 0.0f, 0.0f);
    a->children[1]->transform.position.y = 3.0f;
    std::string name;
    check(t.sync(scene, &name) == 2 && name == "Lamp", "one copy changed: the other two follow");
    check(b->children.size() == 2 && b->children[0]->color == glm::vec3(1.0f, 0.0f, 0.0f) &&
          c->children[1]->transform.position.y == 3.0f, "...with the same insides");
    check(b->transform.position == glm::vec3(20.0f, 0.0f, 0.0f) && b->name == "Lamp by the door",
          "...each keeping its own place and name");

    // Added and removed parts too.
    c->children[0]->name = "Bulb";
    scene.removeNode(c->children[1].get());
    check(t.sync(scene) == 2 && a->children.size() == 1 && a->children[0]->name == "Bulb", "parts added / removed in any copy");

    // Several edited at once (all of them together): left alone.
    a->children[0]->color = glm::vec3(0.0f, 1.0f, 0.0f);
    b->children[0]->color = glm::vec3(0.0f, 0.0f, 1.0f);
    check(t.sync(scene) == 0 && c->children[0]->color == glm::vec3(1.0f, 0.0f, 0.0f), "two copies changed differently: neither wins");

    // Unlinked copies stand alone.
    LinkedModels::unlink(c);
    t.remember(scene);
    a->children[0]->transparency = 0.5f;
    check(t.sync(scene) == 1 && b->children[0]->transparency == 0.5f && c->children[0]->transparency == 0.0f, "an unlinked copy doesn't follow");

    // A new copy (pasted) doesn't count as a change.
    SceneNode* d = copyOf(scene, a, glm::vec3(0.0f, 0.0f, 40.0f));
    d->children[0]->name = "Different";
    check(t.sync(scene) == 0, "a brand-new copy isn't an edit");

    std::cout << (failures ? "linked model tests FAILED\n" : "linked model tests passed\n");
    return failures ? 1 : 0;
}
