// Soft bodies (src/scene/SoftBody.h): parts that squash, wobble, keep their volume and
// tear.
//
//   SoftBodyTests
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/EditMesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Slice.h"
#include "scene/SoftBody.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

glm::mat4 at(glm::vec3 p, glm::vec3 size) { return glm::scale(glm::translate(glm::mat4(1.0f), p), size); }
// A floor: a big box whose top is at y = 0.
SoftBody::Collider floorBox() { return {glm::vec3(0.0f, -10.0f, 0.0f), glm::mat3(glm::vec3(100, 0, 0), glm::vec3(0, 10, 0), glm::vec3(0, 0, 100))}; }
void bounds(const SoftBody::Body& b, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(1e30f); hi = glm::vec3(-1e30f);
    for (const glm::vec3& p : b.pos) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
}
void setAttr(SceneNode* n, const char* name, bool v) {
    Attribute a; a.name = name; a.type = Attribute::Bool; a.b = v; n->attributes.push_back(a);
}
void setAttr(SceneNode* n, const char* name, double v) {
    Attribute a; a.name = name; a.type = Attribute::Number; a.n = v; n->attributes.push_back(a);
}
} // namespace

int main() {
    Mesh::headless = true;
    using namespace SoftBody;
    const float g = 98.1f, dt = 1.0f / 60.0f;

    // --- the bendy block ---
    {
        const EditMesh box = softBox(4);
        check(box.verts.size() == 98 && box.faces.size() == 96, "a bendy block has a grid of corners on every side");
        check(Slice::closed(box) && near(Slice::volume(box), 1.0f), "it's closed, the right way out, and the size of a block");
    }

    // --- building one ---
    {
        Body b;
        build(b, *MeshEdit::fromPrimitive(PrimitiveType::Cube), at({0, 5, 0}, {2, 2, 2}), 0.5f);
        check(b.pos.size() == 8, "corners in the same place are one");
        check(b.cornerOf.size() == MeshEdit::fromPrimitive(PrimitiveType::Cube)->verts.size(), "every corner of the shape knows its own");
        check(near(b.restVolume, 8.0f, 0.01f) && near(volume(b), 8.0f, 0.01f), "it knows its volume in the world");
        check(b.edges.size() == 12 + 12, "an edge along every side and across every face");
        check(near(stretch(b), 1.0f) && near(offShape(b), 0.0f), "it starts in its own shape");
        Body ball;
        build(ball, *MeshEdit::fromPrimitive(PrimitiveType::Sphere), at({0, 5, 0}, {2, 2, 2}), 0.5f);
        check(ball.restVolume > 3.0f && ball.restVolume < 4.3f, "a ball has a ball's volume");
        Body flat;
        build(flat, *MeshEdit::fromPrimitive(PrimitiveType::Plane), at({0, 5, 0}, {2, 2, 2}), 0.5f);
        check(flat.restVolume == 0.0f, "a flat sheet has no volume to keep");
    }

    // --- sitting still ---
    {
        Body b;
        const glm::mat4 M = at({0, 1, 0}, {2, 2, 2});
        build(b, softBox(4), M, 0.5f);
        const SoftBody::Collider floor = floorBox();
        int worked = 0;
        for (int i = 0; i < 240; ++i) worked += solve(b, M, dt, g, &floor, 1) ? 1 : 0;
        check(b.asleep && worked < 200, "left alone it settles and goes to sleep");
        check(!solve(b, M, dt, g, &floor, 1), "asleep, it does no work");
        glm::vec3 lo, hi;
        bounds(b, lo, hi);
        check(lo.y > -1e-3f, "it doesn't sink into the floor");
        check(hi.y < 2.0f && hi.y > 1.6f, "it sags a little under its own weight");
        check(near(volume(b), 8.0f, 0.5f), "and keeps its volume");
        // Softer sags more.
        Body jelly;
        build(jelly, softBox(4), M, 1.0f);
        for (int i = 0; i < 400; ++i) solve(jelly, M, dt, g, &floor, 1);
        glm::vec3 jlo, jhi;
        bounds(jelly, jlo, jhi);
        check(jhi.y < hi.y - 0.05f, "jelly sags more than rubber");
        check(jhi.x > hi.x, "and bulges out further at the sides");
        check(jelly.asleep, "jelly settles too");
        // Moving the part wakes it.
        const glm::mat4 moved = at({0.5f, 1, 0}, {2, 2, 2});
        check(solve(b, moved, dt, g, &floor, 1) && !b.asleep, "moving the part wakes it");
    }

    // --- landing: squash, bulge, wobble ---
    {
        Body b;
        glm::vec3 p(0, 12, 0);
        const glm::vec3 size(3, 3, 3);
        build(b, softBox(4), at(p, size), 0.6f);
        const SoftBody::Collider floor = floorBox();
        float v = 0.0f, lowest = 1e9f, widest = 0.0f, leastVolume = 1e9f, mostVolume = 0.0f;
        bool sane = true, under = false;
        int landed = -1;
        for (int i = 0; i < 300; ++i) {
            // (the part itself: a solid block falling and stopping dead on the floor)
            v -= g * dt;
            p.y += v * dt;
            if (p.y < 1.5f) { p.y = 1.5f; v = 0.0f; if (landed < 0) landed = i; }
            solve(b, at(p, size), dt, g, &floor, 1);
            glm::vec3 lo, hi;
            bounds(b, lo, hi);
            if (lo.y < -1e-3f) under = true;
            if (landed >= 0 && i < landed + 30) {
                lowest = std::min(lowest, hi.y);
                widest = std::max(widest, hi.x - lo.x);
                leastVolume = std::min(leastVolume, volume(b));
                mostVolume = std::max(mostVolume, volume(b));
            }
            for (const glm::vec3& q : b.pos) if (!std::isfinite(q.x + q.y + q.z)) sane = false;
        }
        check(landed > 0 && sane, "a falling block lands");
        check(!under, "no corner goes through the floor");
        check(lowest < 3.0f - 0.25f, "it squashes flat when it lands");
        check(widest > 3.0f + 0.1f, "and bulges at the sides");
        check(leastVolume > 27.0f * 0.8f && mostVolume < 27.0f * 1.2f, "it keeps about the same volume all the way through");
        check(b.asleep, "then the wobble dies away");
        glm::vec3 lo, hi;
        bounds(b, lo, hi);
        check(hi.y > lowest + 0.1f, "and it springs most of the way back");
    }

    // --- falling freely: no wobble from nothing ---
    {
        Body b;
        glm::vec3 p(0, 500, 0);
        build(b, softBox(3), at(p, {2, 2, 2}), 0.7f);
        float v = 0.0f, most = 0.0f;
        for (int i = 0; i < 90; ++i) {
            v -= g * dt;
            p.y += v * dt;
            solve(b, at(p, {2, 2, 2}), dt, g, nullptr, 0);
            most = std::max(most, offShape(b));
        }
        check(most < 0.25f, "falling freely it keeps its shape");
        // Put somewhere else entirely, it goes too.
        solve(b, at({300, 5, 300}, {2, 2, 2}), dt, g, nullptr, 0);
        check(offShape(b) < 1.0f, "moved somewhere else entirely, it goes too");
    }

    // --- a hit ---
    {
        Body b;
        const glm::mat4 M = at({0, 5, 0}, {3, 3, 3});
        build(b, softBox(4), M, 0.5f);
        for (int i = 0; i < 120; ++i) solve(b, M, dt, 0.0f, nullptr, 0);
        check(b.asleep, "(resting)");
        poke(b, {1.5f, 5, 0}, {-25, 0, 0}, 1.5f);
        check(!b.asleep, "a hit wakes it");
        float dent = 0.0f, far = 0.0f;
        for (int i = 0; i < 12; ++i) {
            solve(b, M, dt, 0.0f, nullptr, 0);
            dent = std::max(dent, offShape(b));
        }
        check(dent > 0.15f, "and dents it where it landed");
        for (size_t i = 0; i < b.pos.size(); ++i)
            if (b.rest[i].x < -0.4f) far = std::max(far, glm::length(b.pos[i] - glm::vec3(M * glm::vec4(b.rest[i], 1.0f))));
        check(far < dent * 0.5f, "more than on the far side");
        for (int i = 0; i < 400; ++i) solve(b, M, dt, 0.0f, nullptr, 0);
        check(b.asleep && offShape(b) < 0.02f, "then it springs back to its own shape");
        // A hit nowhere near does nothing.
        poke(b, {50, 5, 0}, {-25, 0, 0}, 1.5f);
        check(b.asleep, "a hit that misses doesn't wake it");
        // A wild hit never blows it up.
        poke(b, {1.5f, 5, 0}, {-9000, 4000, 0}, 3.0f);
        bool sane = true;
        for (int i = 0; i < 200; ++i) {
            solve(b, M, 0.2f, g, nullptr, 0);
            for (const glm::vec3& q : b.pos) if (!std::isfinite(q.x + q.y + q.z) || glm::length(q - glm::vec3(0, 5, 0)) > 60.0f) sane = false;
        }
        check(sane, "a wild hit and big steps stay sane");
    }

    // --- firm against soft ---
    {
        Body firm, soft;
        const glm::mat4 M = at({0, 5, 0}, {3, 3, 3});
        build(firm, softBox(4), M, 0.0f);
        build(soft, softBox(4), M, 1.0f);
        poke(firm, {1.5f, 5, 0}, {-20, 0, 0}, 1.5f);
        poke(soft, {1.5f, 5, 0}, {-20, 0, 0}, 1.5f);
        float df = 0.0f, ds = 0.0f, stretched = 1.0f;
        for (int i = 0; i < 40; ++i) {
            solve(firm, M, dt, 0.0f, nullptr, 0);
            solve(soft, M, dt, 0.0f, nullptr, 0);
            df = std::max(df, offShape(firm));
            ds = std::max(ds, offShape(soft));
            stretched = std::max(stretched, stretch(soft));
        }
        check(ds > df * 1.5f, "the same hit dents jelly deeper than rubber");
        check(stretched > 1.02f, "a dent stretches the skin round it");
    }

    // --- parts in the world ---
    {
        Scene scene;
        auto add = [&](const char* name, PrimitiveType type, glm::vec3 p, glm::vec3 size) {
            SceneNode* n = scene.addNode(name, type, MeshLibrary::get(type));
            n->transform.position = p;
            n->transform.scale = size;
            n->anchored = true;
            return n;
        };
        const std::vector<Physics::Knock> none;
        add("Floor", PrimitiveType::Cube, {0, -0.5f, 0}, {200, 1, 200});
        SceneNode* blob = add("Blob", PrimitiveType::Cube, {0, 1.5f, 0}, {3, 3, 3});
        SceneNode* rock = add("Rock", PrimitiveType::Cube, {20, 1.5f, 0}, {3, 3, 3});
        check(!wants(*blob), "a part isn't soft unless it says so");
        update(scene, dt, none, true);
        check(!bodyOf(*blob), "and gets no body");
        setAttr(blob, "Soft", true);
        setAttr(blob, "Softness", 0.9);
        check(wants(*blob), "Soft = true makes it soft");
        update(scene, dt, none, true);
        const Body* b = bodyOf(*blob);
        check(b && b->pos.size() == 98 && near(b->softness, 0.9f), "a soft block gets a bendy body with its Softness");
        check(blob->editMesh && blob->editMesh->verts.size() == 98, "and a shape with enough corners to bend");
        check(!bodyOf(*rock) && !rock->editMesh, "other parts are left alone");
        for (int i = 0; i < 300; ++i) update(scene, dt, none, true);
        b = bodyOf(*blob);
        glm::vec3 lo, hi;
        bounds(*b, lo, hi);
        check(lo.y > -1e-3f && hi.y < 3.0f - 0.1f, "it sags on the floor it stands on, and not through it");
        check(b->asleep, "and settles");

        // A script's poke.
        check(!poke(scene, *rock, {20, 1.5f, 0}, {0, 10, 0}, 1.0f), "poking a solid part does nothing");
        check(poke(scene, *blob, {1.5f, 1.5f, 0}, {-20, 0, 0}, 1.5f) && !bodyOf(*blob)->asleep, "poking a soft part wakes it");
        update(scene, dt, none, true);
        check(offShape(*bodyOf(*blob)) > 0.05f, "and it dents");

        // Something knocking into it.
        for (int i = 0; i < 400; ++i) update(scene, dt, none, true);
        check(bodyOf(*blob)->asleep, "(resting again)");
        std::vector<Physics::Knock> knocks;
        knocks.push_back({rock->id, blob->id, glm::vec3(1.5f, 1.5f, 0.0f), 18.0f, 5.0f});
        update(scene, dt, knocks, true);
        check(!bodyOf(*blob)->asleep, "a knock from another part wakes it");

        // Tearing: only with TearAt, and only on the host.
        for (int i = 0; i < 400; ++i) update(scene, dt, none, true);
        const uint64_t id = blob->id;
        const size_t before = scene.root()->children.size();
        poke(scene, *blob, {1.5f, 1.5f, 0}, {160, 0, 0}, 0.9f);
        for (int i = 0; i < 20; ++i) update(scene, dt, none, true);
        check(scene.findById(id) != nullptr, "without TearAt it never tears, however hard it's pulled");
        for (int i = 0; i < 400; ++i) update(scene, dt, none, true);
        setAttr(blob, "TearAt", 1.3);
        setAttr(blob, "Flesh", true);
        poke(scene, *blob, {1.5f, 1.5f, 0}, {160, 0, 0}, 0.9f);
        for (int i = 0; i < 20; ++i) update(scene, dt, none, false);
        check(scene.findById(id) != nullptr, "a joined player's computer never tears it");
        for (int i = 0; i < 400; ++i) update(scene, dt, none, true);
        check(scene.findById(id) != nullptr, "(and once it has sprung back, it stays whole)");
        poke(scene, *blob, {1.5f, 1.5f, 0}, {160, 0, 0}, 0.9f);
        for (int i = 0; i < 20 && scene.findById(id); ++i) update(scene, dt, none, true);
        check(!scene.findById(id) && scene.root()->children.size() == before + 1, "pulled past TearAt on the host, it tears in two");
        int softHalves = 0;
        for (auto& c : scene.root()->children)
            if (c->name == "Blob" && wants(*c) && c->editMesh && Slice::closed(*c->editMesh)) ++softHalves;
        check(softHalves == 2, "both halves are closed, and soft themselves");
        for (int i = 0; i < 30; ++i) update(scene, dt, none, true);
        int bodies = 0;
        for (auto& c : scene.root()->children) if (c->name == "Blob" && bodyOf(*c)) ++bodies;
        check(bodies == 2, "and get bodies of their own");

        // Turning it off puts the shape back.
        SceneNode* ball = add("Ball", PrimitiveType::Sphere, {40, 1.5f, 0}, {3, 3, 3});
        setAttr(ball, "Soft", true);
        for (int i = 0; i < 5; ++i) update(scene, dt, none, true);
        check(bodyOf(*ball) != nullptr, "a ball can be soft");
        ball->attributes.clear();
        update(scene, dt, none, true);
        check(!bodyOf(*ball), "no longer Soft, its body is forgotten");
        clearAll();
        int left = 0;
        for (auto& c : scene.root()->children) if (bodyOf(*c)) ++left;
        check(left == 0, "stopping forgets them all");
    }

    std::cout << (failures ? "FAILED" : "All soft body tests passed") << "\n";
    return failures ? 1 : 0;
}
