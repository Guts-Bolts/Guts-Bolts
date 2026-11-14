// Cutting things in two (src/scene/Slice.h): the cut itself, the layered cut end of a
// Flesh part, and cutting a part in the world.
//
//   SliceTests
#include "renderer/Mesh.h"
#include "renderer/MeshLibrary.h"
#include "scene/EditMesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Slice.h"

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

SceneNode* addPart(Scene& scene, const std::string& name, PrimitiveType type, glm::vec3 at, glm::vec3 size) {
    SceneNode* p = scene.addNode(name, type, MeshLibrary::get(type));
    p->transform.position = at;
    p->transform.scale = size;
    return p;
}
// The box around a mesh's corners.
void bounds(const EditMesh& m, glm::vec3& lo, glm::vec3& hi) {
    lo = glm::vec3(1e30f);
    hi = glm::vec3(-1e30f);
    for (const glm::vec3& v : m.verts) { lo = glm::min(lo, v); hi = glm::max(hi, v); }
}
// How many corners have (about) this colour.
int count(const EditMesh& m, const glm::vec4& color) {
    int n = 0;
    for (const glm::vec4& c : m.colors)
        if (glm::length(c - color) < 1e-3f) ++n;
    return n;
}
// An L-shaped beam (its outline has a dent), 1 long.
EditMesh beam() {
    EditMesh m;
    const glm::vec2 outline[6] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.5f}, {-0.5f, 0.5f}};
    for (float y : {-0.5f, 0.5f})
        for (const glm::vec2& p : outline) m.verts.push_back({p.x, y, p.y});
    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t j = (i + 1) % 6;
        m.faces.push_back({i, j, 6 + j, 6 + i});
    }
    // (the two ends as triangles, so the dent is filled the right way)
    m.faces.push_back({0, 5, 3});  m.faces.push_back({0, 3, 1});  m.faces.push_back({1, 3, 2});  m.faces.push_back({3, 5, 4});
    m.faces.push_back({6, 9, 11}); m.faces.push_back({6, 7, 9});  m.faces.push_back({7, 8, 9});  m.faces.push_back({9, 10, 11});
    return m;
}
} // namespace

int main() {
    Mesh::headless = true;
    using namespace Slice;
    const Inside plain;

    // --- a block ---
    {
        const EditMesh cube = *MeshEdit::fromPrimitive(PrimitiveType::Cube);
        check(near(std::fabs(volume(cube)), 1.0f) && closed(cube), "a block is closed and takes up 1");
        EditMesh a, b;
        check(cut(cube, {0, 0.25f, 0}, {0, 1, 0}, plain, a, b), "a block cuts across");
        check(closed(a) && closed(b), "both halves are closed off");
        check(near(volume(a), 0.25f) && near(volume(b), 0.75f), "the halves are the right sizes (and the right way out)");
        glm::vec3 lo, hi;
        bounds(a, lo, hi);
        check(near(lo.y, 0.25f) && near(hi.y, 0.5f), "the front half is the side the cut faces");
        bounds(b, lo, hi);
        check(near(lo.y, -0.5f) && near(hi.y, 0.25f), "the back half is the other side");
        // On a slant, through the middle: two equal wedges.
        check(cut(cube, {0, 0, 0}, {1, 1, 0}, plain, a, b) && closed(a) && closed(b), "a slanted cut closes too");
        check(near(volume(a), 0.5f) && near(volume(b), 0.5f), "a slanted cut through the middle makes equal halves");
        // Any old cut: nothing is lost.
        check(cut(cube, {0.1f, -0.2f, 0.05f}, {0.3f, -0.8f, 0.5f}, plain, a, b) && closed(a) && closed(b) &&
              near(volume(a) + volume(b), 1.0f) && volume(a) > 0.0f && volume(b) > 0.0f, "an odd cut loses nothing");
        // Right through corners and edges (the awkward case).
        check(cut(cube, {0, 0, 0}, {1, 0, 1}, plain, a, b) && closed(a) && closed(b) && near(volume(a), 0.5f), "a cut through the edges works");
        // Misses and grazes.
        check(!cut(cube, {0, 2, 0}, {0, 1, 0}, plain, a, b), "a cut that misses does nothing");
        check(!cut(cube, {0, 0.5f, 0}, {0, 1, 0}, plain, a, b), "a cut along a side does nothing");
        check(!cut(cube, {0, 0, 0}, {0, 0, 0}, plain, a, b), "a cut facing nowhere does nothing");
    }

    // --- round things keep their shape ---
    {
        const EditMesh can = *MeshEdit::fromPrimitive(PrimitiveType::Cylinder);
        const float whole = volume(can);
        EditMesh a, b;
        check(cut(can, {0, 0, 0}, {0, 1, 0}, plain, a, b) && closed(a) && closed(b), "a cylinder cuts across");
        check(near(volume(a), whole * 0.5f) && near(volume(b), whole * 0.5f), "into two equal cans");
        check(cut(can, {0, 0, 0}, {1, 0.4f, 0.2f}, plain, a, b) && closed(a) && closed(b) && near(volume(a) + volume(b), whole),
              "and lengthways on a slant");
        const EditMesh ball = *MeshEdit::fromPrimitive(PrimitiveType::Sphere);
        check(cut(ball, {0, 0.1f, 0}, {0.2f, 1, 0.1f}, plain, a, b) && closed(a) && closed(b) &&
              near(volume(a) + volume(b), volume(ball)), "a ball cuts into two domes");
        // A half can be cut again.
        EditMesh c, d;
        check(cut(a, {0, 0.25f, 0}, {1, 0, 0}, plain, c, d) && closed(c) && closed(d) && near(volume(c) + volume(d), volume(a)),
              "a half can be cut again");
    }

    // --- an outline with a dent ---
    {
        const EditMesh l = beam();
        check(closed(l) && near(volume(l), 0.75f), "the L-shaped beam is closed");
        EditMesh a, b;
        check(cut(l, {0, 0.1f, 0}, {0, 1, 0}, plain, a, b) && closed(a) && closed(b), "an L-shaped beam cuts across");
        check(near(volume(a), 0.75f * 0.4f) && near(volume(b), 0.75f * 0.6f), "and the dent isn't filled in");
    }

    // --- what's inside ---
    {
        const EditMesh cube = *MeshEdit::fromPrimitive(PrimitiveType::Cube);
        EditMesh a, b;
        Inside pale;
        pale.cap = glm::vec4(2.0f, 1.5f, 1.0f, 1.0f);
        cut(cube, {0, 0, 0}, {0, 1, 0}, pale, a, b);
        check(a.colors.size() == a.verts.size() && count(a, pale.cap) == 4 && count(b, pale.cap) == 4,
              "a plain cut end has its own corners in its own colour");
        check(count(a, glm::vec4(1.0f)) == 8, "and the rest keeps its colour");

        const glm::vec3 skin(0.9f, 0.7f, 0.55f), blood(0.5f, 0.02f, 0.03f);
        const Inside meat = flesh(skin, blood);
        check(meat.layers == 3 && meat.reach[0] == 1.0f && meat.reach[1] > meat.reach[2] && meat.reach[2] > meat.reach[3],
              "flesh has skin, fat and muscle round a bone");
        const glm::vec3 muscle = glm::vec3(meat.band[2]) * skin, bone = glm::vec3(meat.core) * skin, fat = glm::vec3(meat.band[1]) * skin;
        check(muscle.r > muscle.g * 3.0f, "the muscle comes out red on any skin");
        check(bone.r > 0.85f && bone.g > 0.8f && fat.r > 0.85f && fat.b < 0.6f, "the bone is pale and the fat yellow");
        check(cut(cube, {0, 0, 0}, {0, 1, 0}, meat, a, b) && closed(a) && closed(b) && near(volume(a), 0.5f), "a layered cut is still closed");
        check(count(a, meat.core) == 4 && count(a, meat.band[1]) == 8 && count(a, meat.band[2]) == 8,
              "each layer has its own band of corners");
        // The bone is in the middle: its corners are the closest to the middle of the cut.
        float farBone = 0.0f, nearFat = 1e9f;
        for (size_t i = 0; i < a.verts.size(); ++i) {
            const float r = glm::length(glm::vec2(a.verts[i].x, a.verts[i].z));
            if (glm::length(a.colors[i] - meat.core) < 1e-3f) farBone = std::max(farBone, r);
            if (glm::length(a.colors[i] - meat.band[1]) < 1e-3f) nearFat = std::min(nearFat, r);
        }
        check(farBone < nearFat, "bone in the middle, fat near the skin");
    }

    // --- texture coordinates and bones ---
    {
        EditMesh cube = *MeshEdit::fromPrimitive(PrimitiveType::Cube);
        MeshEdit::ensureUVs(cube);
        EditMesh a, b;
        check(cut(cube, {0, 0, 0}, {0, 1, 0}, plain, a, b) && a.uvs.size() == a.faces.size() && b.uvs.size() == b.faces.size(),
              "a mesh with a picture layout keeps one for every face");
        bool match = true;
        for (size_t f = 0; f < a.faces.size(); ++f)
            if (a.uvs[f].size() != a.faces[f].size()) match = false;
        check(match, "one coordinate per corner");
        cube.bones = {"Bone"};
        cube.bind = {glm::mat4(1.0f)};
        cube.weights.assign(cube.verts.size(), {{0, 1.0f}});
        check(!cut(cube, {0, 0, 0}, {0, 1, 0}, plain, a, b), "a mesh bent by bones isn't cut");
    }

    // --- parts in the world ---
    {
        Scene scene;
        // A log lying along x, cut straight across at x = 11 (a quarter of the way along).
        SceneNode* log = addPart(scene, "Log", PrimitiveType::Cylinder, {10, 5, 0}, {1, 4, 1});
        log->transform.rotation = {0, 0, 90};
        log->anchored = true;
        log->color = {0.4f, 0.25f, 0.1f};
        const uint64_t id = log->id;
        const size_t before = scene.root()->children.size();
        const std::vector<SceneNode*> halves = part(scene, log, {11, 5, 0}, {1, 0, 0}, 3.0f);
        check(halves.size() == 2 && !scene.findById(id), "cutting a part swaps it for two halves");
        if (halves.size() == 2) {
            check(scene.root()->children.size() == before + 1, "one part became two");
            const SceneNode* front = halves[0];
            const SceneNode* back = halves[1];
            check(front->primitiveType == PrimitiveType::Mesh && front->editMesh && back->editMesh, "the halves are real shapes, not boxes");
            check(near(front->transform.position.x, 11.5f, 0.01f) && near(back->transform.position.x, 9.5f, 0.01f),
                  "each half sits where its piece was");
            check(near(front->transform.position.y, 5.0f, 0.01f) && near(back->transform.position.y, 5.0f, 0.01f), "at the same height");
            // (lying along x: the part's own y is the length)
            check(near(front->transform.scale.y, 1.0f, 0.01f) && near(back->transform.scale.y, 3.0f, 0.01f), "a short end and a long end");
            check(!front->anchored && front->velocity.x > 2.9f, "the cut-off end comes loose and is pushed away");
            check(back->anchored && glm::length(back->velocity) == 0.0f, "the rest stays where it was fixed");
            check(closed(*front->editMesh) && closed(*back->editMesh), "both are closed");
            glm::vec3 lo, hi;
            bounds(*front->editMesh, lo, hi);
            check(near(lo.x, -0.5f) && near(hi.x, 0.5f) && near(lo.y, -0.5f) && near(hi.y, 0.5f), "each half's shape fits its own box");
            // The long end can be cut again.
            const std::vector<SceneNode*> more = part(scene, halves[1], {9, 5, 0}, {-1, 0, 0});
            check(more.size() == 2 && near(more[0]->transform.scale.y, 1.0f, 0.01f) && near(more[1]->transform.scale.y, 2.0f, 0.01f),
                  "a half can be cut again");
        }
        // What can't be cut.
        SceneNode* crate = addPart(scene, "Crate", PrimitiveType::Cube, {30, 5, 0}, {2, 2, 2});
        check(part(scene, crate, {40, 5, 0}, {1, 0, 0}).empty() && scene.findById(crate->id), "a cut that misses leaves the part alone");
        check(part(scene, crate, {30.999f, 5, 0}, {1, 0, 0}).empty(), "a sliver isn't cut off");
        check(part(scene, crate, {30, 5, 0}, {0, 0, 0}).empty(), "nor is anything by a cut facing nowhere");
        check(part(scene, nullptr, {0, 0, 0}, {1, 0, 0}).empty(), "nothing is nothing");
        SceneNode* floor = addPart(scene, "Floor", PrimitiveType::Plane, {50, 0, 0}, {4, 1, 4});
        check(part(scene, floor, {50, 0, 0}, {1, 0, 0}).empty(), "a flat plane isn't cut");

        // The inside colour.
        Attribute inside;
        inside.name = "InsideColor";
        inside.type = Attribute::Color3;
        inside.v = {0.9f, 0.8f, 0.6f};
        crate->attributes.push_back(inside);
        crate->color = {0.3f, 0.2f, 0.1f};
        const std::vector<SceneNode*> planks = part(scene, crate, {30, 5, 0}, {0, 1, 0});
        check(planks.size() == 2, "a crate cuts in two");
        if (planks.size() == 2) {
            const glm::vec4 pale(glm::vec3(0.9f, 0.8f, 0.6f) / glm::vec3(0.3f, 0.2f, 0.1f), 1.0f);
            check(count(*planks[0]->editMesh, pale) == 4, "InsideColor shows on the cut");
            check(planks[0]->findAttribute("InsideColor") != nullptr, "and the halves keep it for the next cut");
            check(!planks[0]->anchored && !planks[1]->anchored && planks[0]->velocity.y > 0.0f && planks[1]->velocity.y < 0.0f,
                  "two loose halves are pushed apart");
        }

        // Flesh follows the Gore setting.
        Attribute fleshy;
        fleshy.name = "Flesh";
        fleshy.type = Attribute::Bool;
        fleshy.b = true;
        SceneNode* ham = addPart(scene, "Ham", PrimitiveType::Cylinder, {60, 5, 0}, {2, 3, 2});
        ham->color = {0.9f, 0.6f, 0.55f};
        ham->attributes.push_back(fleshy);
        const Inside meat = flesh(ham->color, scene.world().bloodColor);
        const bool gore = scene.goreEnabled();
        const std::vector<SceneNode*> slices = part(scene, ham, {60, 5, 0}, {0, 1, 0});
        check(slices.size() == 2, "a ham cuts in two");
        if (slices.size() == 2)
            check((count(*slices[0]->editMesh, meat.core) > 0) == gore, "it shows bone and meat inside only when Gore is on");
    }

    std::cout << (failures ? "FAILED" : "All slice tests passed") << "\n";
    return failures ? 1 : 0;
}
