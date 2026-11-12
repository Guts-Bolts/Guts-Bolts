#pragma once
#include "Mesh.h"
#include <vector>

// Detail levels (LODs): simpler copies of a big mesh, made once when the mesh is
// built, and drawn instead of it when it is small on the screen. A statue with
// 20,000 triangles that covers 30 pixels looks the same with 300.
//
// How a simpler copy is made (vertex clustering): the mesh's box is cut into a grid
// of cells, every corner inside one cell becomes one point (in the middle of them),
// and triangles that shrank to nothing are thrown away. Corners that face different
// ways (the 6 sides of a box) stay apart, so hard edges keep their shading, but they
// share one position, so no holes open up. It needs no extra library, so it runs the
// same in Studio, the Player, on phones and on the web.
namespace MeshLod {

constexpr int kLevels = 3;                        // simpler copies after the full mesh
constexpr int kCells[kLevels] = {32, 16, 8};      // grid cells along the mesh's longest side
constexpr int kMinTriangles = 300;                // smaller meshes are cheap already
// Draw level 1 when the mesh is under this many pixels across, level 2, level 3
// (at detail bias 1: each cell is then about 3 pixels).
constexpr float kPixels[kLevels] = {110.0f, 50.0f, 22.0f};

struct Level {
    std::vector<Vertex>   verts;
    std::vector<uint32_t> idx;
    float pixels = 0.0f;   // drawn when the mesh is under this many pixels across (kPixels)
};

// One simpler copy: `cells` grid cells along the longest side.
Level cluster(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx, int cells);

// All the copies worth having, most detailed first. A copy is only kept when it has
// at most 70% of the triangles of the one before (so a level is always a real saving);
// none at all for a small mesh.
std::vector<Level> build(const std::vector<Vertex>& verts, const std::vector<uint32_t>& idx);

// How many pixels across something `radius` big looks, `distance` away, on a screen
// where 1 stud at 1 stud away covers `pixelsPerStud` pixels (half the screen height /
// tan(half the field of view)).
float pixelsAcross(float radius, float distance, float pixelsPerStud);

// Which level to draw (0 = the full mesh) for a mesh that many pixels across, given each
// level's own limit (Level::pixels). `bias` > 1 swaps to simpler copies sooner (Low
// graphics), < 1 later (Epic), 0 = never.
int pick(float pixels, float bias, const std::vector<float>& levelPixels);

} // namespace MeshLod
