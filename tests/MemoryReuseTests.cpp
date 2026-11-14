// Not asking for memory every frame (step 7c of the photoreal upgrade):
//  * IdMap (src/core/IdMap.h): an id -> value list that keeps its room when emptied;
//  * Scratch (src/core/Scratch.h): borrowed lists that are kept for next time;
//  * a physics step, the touch checks and finding parts by id ask for no memory once
//    a scene has settled (this file counts every `new` to check).
//
//   MemoryReuseTests
#include "core/IdMap.h"
#include "core/Scratch.h"
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/Physics.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"

#include <glm/glm.hpp>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string>
#include <vector>

// Every `new` in this program goes through here and is counted.
static std::atomic<long> g_news{0};
void* operator new(size_t n) {
    ++g_news;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

SceneNode* addPart(Scene& scene, const std::string& name, glm::vec3 at, glm::vec3 size, bool anchored) {
    SceneNode* p = scene.addNode(name, PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
    p->transform.position = at;
    p->transform.scale = size;
    p->anchored = anchored;
    return p;
}

// One frame of physics, the way the game does it.
void frame(Physics& phys, Scene& scene, std::vector<uint64_t>& fallen, std::vector<TouchEvent>& touches,
           std::vector<TouchEvent>& ended) {
    fallen.clear();
    touches.clear();
    ended.clear();
    phys.gather(scene);
    phys.stepParts(scene, 1.0f / 60.0f, fallen);
    phys.gather(scene);
    phys.collectTouches(scene, touches, &ended);
}
} // namespace

int main() {
    Mesh::headless = true;   // (no window: parts don't need their meshes on a graphics card)

    // --- IdMap ---
    {
        IdMap<int> m;
        check(m.find(5) == nullptr && m.size() == 0, "an empty map has nothing in it");
        for (int i = 0; i < 5000; ++i) m.set((uint64_t)i * 7919u + 1u, i);
        bool all = m.size() == 5000;
        for (int i = 0; i < 5000 && all; ++i) {
            const int* v = m.find((uint64_t)i * 7919u + 1u);
            all = v && *v == i;
        }
        check(all, "5000 ids all find their own number");
        check(m.find(2) == nullptr && m.find(7919u * 6000u + 1u) == nullptr, "ids never put in aren't found");
        m.set(1, 42);
        check(*m.find(1) == 42 && m.size() == 5000, "setting an id again changes it, not adds it");
        m.clear();
        check(m.size() == 0 && m.find(1) == nullptr, "clear empties it");
        const long before = g_news;
        for (int i = 0; i < 5000; ++i) m.set((uint64_t)i * 31u, i);
        const bool none = g_news == before;   // (worked out before check() makes its message)
        check(none, "filling it again asks for no memory");
        IdMap<int> other;
        other.set(9, 9);
        other.swap(m);
        check(other.size() == 5000 && m.size() == 1 && *m.find(9) == 9, "swap trades what two maps hold");
    }

    // --- Scratch lists ---
    {
        void* firstList = nullptr;
        {
            Scratch<int> a;
            for (int i = 0; i < 1000; ++i) a->push_back(i);
            firstList = a->data();
            Scratch<int> b;   // (borrowed while `a` is still out: a different list)
            check(b->empty() && &*a != &*b, "a second list borrowed at once is its own, empty");
        }
        bool sameRoom = false, wasEmpty = false;
        const long before = g_news;
        {
            Scratch<int> again;
            wasEmpty = again->empty();
            for (int i = 0; i < 1000; ++i) again->push_back(i);
            sameRoom = again->data() == firstList;
        }
        const bool none = g_news == before;
        check(wasEmpty, "a list given back comes back empty");
        check(sameRoom, "and with the same room as before");
        check(none, "borrowing it again asks for no memory");
    }

    // --- A physics step ---
    {
        Scene scene;
        addPart(scene, "Floor", {0, -0.5f, 0}, {80, 1, 80}, true);
        // Stacks of crates, a row of loose bricks and a few walls: lots of touching.
        for (int x = 0; x < 6; ++x)
            for (int y = 0; y < 5; ++y) addPart(scene, "Crate", {x * 3.0f - 8.0f, 0.5f + y * 1.0f, 0}, {1, 1, 1}, false);
        for (int i = 0; i < 40; ++i) addPart(scene, "Brick", {(i % 10) * 1.5f - 7.0f, 0.4f, 6.0f + (i / 10) * 1.5f}, {1, 0.8f, 1}, false);
        for (int i = 0; i < 6; ++i) addPart(scene, "Wall", {i * 6.0f - 15.0f, 2, -8}, {4, 4, 0.5f}, true);
        Physics phys;
        std::vector<uint64_t> fallen;
        std::vector<TouchEvent> touches, ended;
        fallen.reserve(64); touches.reserve(4096); ended.reserve(4096);
        for (int i = 0; i < 120; ++i) frame(phys, scene, fallen, touches, ended);   // (settle, and let the lists find their size)
        const long before = g_news;
        for (int i = 0; i < 60; ++i) frame(phys, scene, fallen, touches, ended);
        const long asked = g_news - before;
        check(asked == 0, "a second of physics on a settled scene asks for no memory (" + std::to_string(asked) + " times)");
        // Finding parts by id, even ones that aren't there (that rebuilds the list).
        const uint64_t firstId = scene.root()->children.front()->id;
        scene.findById(firstId);
        const long beforeFind = g_news;
        bool found = true;
        for (int i = 0; i < 200; ++i) {
            found = found && scene.findById(firstId) != nullptr;
            scene.findById(0xFFFFFFFFull + (uint64_t)i);   // (missing: the list is rebuilt each time)
        }
        const bool none = g_news == beforeFind;
        check(found, "parts are found by id");
        check(none, "looking parts up by id asks for no memory, even when one is missing");
    }

    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all passed")) << "\n";
    return failures ? 1 : 0;
}
