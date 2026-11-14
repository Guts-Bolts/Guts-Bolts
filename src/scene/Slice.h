#pragma once
#include <vector>
#include <glm/glm.hpp>

class Scene;
class SceneNode;
struct EditMesh;

// Cutting things in two with a blade: a part and a flat cut (a point on it and the way
// it faces) become two parts, each closed off where it was cut.
//
// Where Shatter swaps a part for box-shaped rubble, this keeps the real shape: half a
// barrel is still round, a plank cut on a slant has a slanted end. The cut ends are
// new faces. On most things they're the part's own colour (or its InsideColor
// attribute: pale wood inside dark bark). On a part with the attribute Flesh = true
// (a side of meat, a zombie dummy made of parts) the cut shows what's inside in
// layers: skin round the outside, a thin band of fat, muscle, and bone in the middle,
// and it bleeds. With Gore off a Flesh part cuts like anything else.
//
// A script cuts with part:Slice(point, normal). Only the host cuts things; the halves
// are ordinary parts, so everyone sees them.
namespace Slice {

// How the cut end looks, as colours the part's own colour is multiplied by.
struct Inside {
    // A plain cut: one colour.
    glm::vec4 cap{1.0f};
    // A layered cut: `layers` bands from the outside in, then the middle. Band k runs
    // from reach[k] to reach[k + 1] of the way out from the middle (reach[0] = 1, the
    // outline) and has colour band[k]; the middle (inside reach[layers]) is `core`.
    int       layers = 0;          // 0 = plain
    float     reach[4] = {1.0f, 0.93f, 0.86f, 0.3f};
    glm::vec4 band[3] = {glm::vec4(1.0f), glm::vec4(1.0f), glm::vec4(1.0f)};
    glm::vec4 core{1.0f};
};
// Skin, fat, muscle and bone for a part of this colour (its skin) with this blood.
Inside flesh(const glm::vec3& skin, const glm::vec3& blood);

// Cut a mesh along the flat surface through `point` facing `normal`. `front` gets the
// side the normal points to, `back` the other; both are closed with new faces on the
// cut. False (and nothing useful in front / back) if the cut misses the mesh, only
// grazes it, or the mesh is bent by bones.
bool cut(const EditMesh& mesh, const glm::vec3& point, const glm::vec3& normal, const Inside& inside,
         EditMesh& front, EditMesh& back);

// How much space a closed mesh takes up, and whether it is closed (every edge has a
// face on both sides). For tests, and to refuse slivers.
float volume(const EditMesh& mesh);
bool  closed(const EditMesh& mesh);

// Cut a part in the world. Returns the two halves (front first), or nothing if it
// can't be cut: not a part, a character's body, a flat plane, or the cut misses it or
// would leave a sliver. The half on the normal's side comes loose and is pushed away
// at `push` studs / second; the other half stays anchored if the part was.
std::vector<SceneNode*> part(Scene& scene, SceneNode* part, const glm::vec3& point, const glm::vec3& normal, float push = 2.0f);

constexpr float kSliver = 0.02f;   // a half smaller than this share of the whole isn't cut off

} // namespace Slice
