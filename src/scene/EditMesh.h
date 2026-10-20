#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "SceneNode.h"

// A shape you build yourself in Studio's Modeling mode (like Blender's Edit
// Mode): a list of corner points ("vertices") and the flat sides ("faces")
// between them. Each face lists its vertices counter-clockwise as seen from
// outside, so its front is the outside.
//
// The vertices always fit inside the part's box (-0.5..0.5 on each axis),
// like a Roblox MeshPart: after an edit, fit() grows or shrinks the part so
// the box wraps the new shape. That keeps physics, picking and resizing
// working with no special cases.
struct EditMesh {
    std::vector<glm::vec3>             verts;
    std::vector<std::vector<uint32_t>> faces;
    bool                               smooth = false;   // smooth shading (no hard edges)
    // Optional texture coordinates, one per corner of each face (the character's
    // clothing layout, or laid out in Studio's UV Editor). Empty, or out of step
    // with `faces` after an edit = box mapping.
    std::vector<std::vector<glm::vec2>> uvs;
    // Seams: edges (corner pairs, lower number first) where Unwrap may cut the
    // surface open to lay it flat, like Blender's Mark Seam. Kept sorted.
    std::vector<std::pair<uint32_t, uint32_t>> seams;
};

namespace MeshEdit {

using Selection = std::vector<char>;   // one flag per vertex

// A mesh shaped like a built-in part (Cube, Sphere, Cylinder or Plane).
std::shared_ptr<EditMesh> fromPrimitive(PrimitiveType type);
// Turn `node` into a custom-mesh part using `mesh`.
void attach(SceneNode& node, std::shared_ptr<EditMesh> mesh);
// Rebuild what the GPU draws after the mesh changed.
void refresh(SceneNode& node);
// Wrap the part's box around the mesh again (keeps everything where it is in the world).
void fit(SceneNode& node);
// Make sure this node has its own copy of the mesh before changing it.
EditMesh& own(SceneNode& node);

glm::vec3 faceNormal(const EditMesh& m, const std::vector<uint32_t>& face);   // unit length
glm::vec3 faceCenter(const EditMesh& m, const std::vector<uint32_t>& face);
std::vector<std::pair<uint32_t, uint32_t>> edges(const EditMesh& m);        // each edge once (a < b)
std::vector<int> selectedFaces(const EditMesh& m, const Selection& sel);   // all corners selected
int  countSelected(const Selection& sel);

// Editing tools. Each works on the selection and leaves the result selected.
bool extrude(EditMesh& m, Selection& sel);          // pull faces (or edges) out; new copies stay in place
bool inset(EditMesh& m, Selection& sel, float t);   // a smaller face inside each selected face
bool subdivide(EditMesh& m, Selection& sel);        // split selected faces (all faces if none)
bool remove(EditMesh& m, Selection& sel, int what); // what: 0 vertices, 1 edges, 2 faces
bool merge(EditMesh& m, Selection& sel);            // selected vertices -> one in the middle
bool fill(EditMesh& m, Selection& sel);             // make a face from the selected vertices
bool flip(EditMesh& m, const Selection& sel);       // turn selected faces inside out (all if none)

// Drop vertices no face uses (keeps `sel` in step).
void compact(EditMesh& m, Selection& sel);

// ---- More tools, like Blender's (MeshTools.cpp) ----------------------------

// Picking helpers. A "loop" runs along a line of four-sided faces (Alt+click),
// a "ring" goes across them (Ctrl+Alt+click).
std::vector<uint32_t> edgeLoop(const EditMesh& m, uint32_t a, uint32_t b);   // its corners
std::vector<std::pair<uint32_t, uint32_t>> edgeRing(const EditMesh& m, uint32_t a, uint32_t b);
void selectLinked(const EditMesh& m, Selection& sel);   // everything joined to what's picked (Ctrl+L)
void grow(const EditMesh& m, Selection& sel);           // one step out (Ctrl+Plus)
void shrink(const EditMesh& m, Selection& sel);         // one step in (Ctrl+Minus)
void selectBoundary(const EditMesh& m, Selection& sel); // just the outline of the picked faces
void selectNonManifold(const EditMesh& m, Selection& sel);   // holes and odd edges

// Shaping tools. Each works on the selection and leaves the result selected.
bool duplicate(EditMesh& m, Selection& sel);   // copy the picked faces (Shift+D)
// Cut `cuts` new edge loops across the ring through edge a-b (Ctrl+R).
bool loopCut(EditMesh& m, Selection& sel, uint32_t a, uint32_t b, int cuts);
bool bridge(EditMesh& m, Selection& sel);      // join two picked edge loops with faces
bool dissolve(EditMesh& m, Selection& sel, int what);   // like delete, but the faces around close up (0 verts, 1 edges, 2 faces)
bool triangulate(EditMesh& m, Selection& sel);  // picked faces (or all) -> triangles (Ctrl+T)
bool trisToQuads(EditMesh& m, Selection& sel);  // pairs of triangles -> four-sided faces (Alt+J)
bool recalcNormals(EditMesh& m, const Selection& sel, bool inside);   // faces all point out (Shift+N)
bool smoothVerts(EditMesh& m, const Selection& sel, float amount);    // round off the picked corners
bool mergeByDistance(EditMesh& m, Selection& sel, float dist, int* removed = nullptr);
bool collapse(EditMesh& m, Selection& sel);     // each joined group of picked corners -> its middle
bool subdivideSmooth(EditMesh& m, Selection& sel);   // Catmull-Clark: smaller faces AND rounder
bool mirror(EditMesh& m, Selection& sel, int axis);   // flip the picked part across its middle (local axis)
bool spin(EditMesh& m, Selection& sel, glm::vec3 axis, glm::vec3 center, float degrees, int steps);

// Shapes you can add into a mesh (Shift+A).
enum class AddShape { Cube, Plane, Circle, Cylinder, Cone, Sphere, IcoSphere, Torus, Grid, Capsule };
inline constexpr int kAddShapeCount = 10;
inline const char* const kAddShapeNames[kAddShapeCount] = {"Cube", "Plane", "Circle", "Cylinder", "Cone",
                                                            "UV Sphere", "Ico Sphere", "Torus", "Grid", "Capsule"};
std::shared_ptr<EditMesh> shape(AddShape s);   // about 1 across, centred on 0
// Add `src` (moved by `place`) to `dst`; the new part is picked. Used by Shift+A and Join.
void append(EditMesh& dst, Selection& sel, const EditMesh& src, const glm::mat4& place);
// The picked faces as a mesh of their own (Separate, P).
EditMesh extract(const EditMesh& m, const Selection& sel);
// Carry per-corner flags (like "hidden") across an edit: a corner of `after`
// gets the flag of a corner of `before` in exactly the same place.
std::vector<char> carryFlags(const EditMesh& before, const std::vector<char>& flags, const EditMesh& after);
// ---- UV mapping: how a picture wraps onto the mesh (MeshUV.cpp) -----------

// The ways to lay the picked faces (or all) out flat, like Blender's U menu.
enum class Unwrap { Unwrap, Smart, Cube, Cylinder, Sphere, View, Reset };
inline constexpr int kUnwrapCount = 7;
inline const char* const kUnwrapNames[kUnwrapCount] = {"Unwrap", "Smart UV Project", "Cube Projection", "Cylinder Projection",
                                                       "Sphere Projection", "Project From View", "Reset"};
// `scale` is the part's size (so nothing comes out stretched); `view` takes the
// mesh's own points to the screen (Project From View only).
bool unwrap(EditMesh& m, const Selection& sel, Unwrap how, glm::vec3 scale, const glm::mat4& view = glm::mat4(1.0f));
// Give every face texture coordinates (the box mapping it's drawn with now) if it has none.
void ensureUVs(EditMesh& m);
// Spread out pieces of the layout so none overlap, filling the square (picked faces, or all).
void packIslands(EditMesh& m, const std::vector<int>& faces, glm::vec3 scale);
// Faces joined in the layout (sharing an edge with the same UVs on both sides).
std::vector<std::vector<int>> uvIslands(const EditMesh& m, const std::vector<int>& faces);
bool markSeams(EditMesh& m, const Selection& sel, bool on);   // picked edges become (or stop being) seams
bool isSeam(const EditMesh& m, uint32_t a, uint32_t b);
// After a tool changed the mesh: keep the layout and seams where the faces are
// still the same, and give new faces UVs that fit their neighbours'.
void carryUVs(const EditMesh& before, EditMesh& after);
// A picture of the layout (for painting a texture in another program), RGBA.
std::vector<unsigned char> layoutPicture(const EditMesh& m, int size);

} // namespace MeshEdit
