#pragma once
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../scene/EditMesh.h"

// Bringing 3D models made in other programs (Blender, Maya, 3ds Max, SketchUp,
// Tinkercad, ...) into Studio. Each object in the file (split by material)
// becomes one mesh part; together they go in a Model.
//
// Formats: .fbx, .obj (+ its .mtl), .gltf / .glb (also squashed with Draco or
// meshopt), .stl and .ply read here; Collada (.dae), .3ds, .x, .3mf, LightWave
// and about 30 more go through Assimp.
// Sizes: 1 metre (or 1 unit, for files with no units) is 1 stud. Something far
// too big or too small (like a model made in millimetres) is shrunk or grown to
// about 10 studs, and the Output says so.
namespace ModelImport {

struct Piece {
    std::string               name;
    std::shared_ptr<EditMesh> mesh;             // points squeezed into the -0.5..0.5 box, like every part
    glm::vec3                 center{0.0f};     // where it sits, in studs, relative to the model's bottom middle
    glm::vec3                 size{1.0f};
    glm::vec3                 color{0.65f, 0.65f, 0.80f};
    float                     transparency = 0.0f;
};

struct Result {
    std::vector<Piece>       pieces;
    glm::vec3                size{0.0f};        // the whole model
    size_t                   faces = 0;
    std::vector<std::string> notes;             // things worth telling (rescaled, skipped bits, ...)
};

// True for the file endings above (any capitalisation).
bool isModelFile(const std::string& path);
// A 3D file Studio can't read (.blend, .max, .usd, CAD files, ...): load() says what to do instead.
bool isOtherModelFile(const std::string& path);
// "*.fbx *.obj ..." for the Open window, and ".fbx, .obj, ..." for people.
const char* patterns();
const char* formatList();

bool load(const std::string& path, Result& out, std::string& err);

} // namespace ModelImport
