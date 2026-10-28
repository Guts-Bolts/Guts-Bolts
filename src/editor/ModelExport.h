#pragma once
#include <string>
#include <vector>

class SceneNode;

// File > Export As: write parts (and everything inside the chosen objects) to a
// 3D model file other programs open, like Blender, Unity or a 3D printer's slicer.
// Shapes are written in studs (1 unit = 1 stud), Y up, each part with its own
// colour. Lights, scripts and sounds have no place in a model file, so they're
// skipped; the Roblox formats keep everything.
namespace ModelExport {

struct Format {
    const char* ext;        // ".obj"
    const char* label;      // "Wavefront OBJ"
    const char* hint;       // what it's good for, in plain words
    bool        roblox;     // .rbxmx / .rbxlx: everything, not just the shapes
    bool        wholeGame;  // only writes the whole game (.rbxlx)
};

// Every type Export As can write, in menu order.
const std::vector<Format>& formats();
// The format a file name ends with (nullptr if none).
const Format* formatFor(const std::string& path);

// Write the shapes of `roots` (and everything inside them) to `path`; the type
// comes from its extension. `parts` / `triangles` say how much was written.
bool write(const std::vector<const SceneNode*>& roots, const std::string& path, std::string& error,
           int* parts = nullptr, int* triangles = nullptr);

// Roblox's own .mesh file (version 2.00, which Roblox Studio and every mesh tool
// read) for all those shapes together, centred on the middle of them.
std::string robloxMesh(const std::vector<const SceneNode*>& roots, int* parts = nullptr, int* triangles = nullptr);

} // namespace ModelExport
