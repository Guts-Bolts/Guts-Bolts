// File > Export As: every 3D model type is written, then read back with Studio's
// own importer (or Roblox's mesh reader), and has the same shapes, size and colours.
#include "editor/ModelExport.h"
#include "editor/ModelImport.h"
#include "scene/RobloxMesh.h"
#include "scene/SceneNode.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

bool near(glm::vec3 a, glm::vec3 b, float tol = 0.05f) { return glm::length(a - b) < tol; }

std::string str(glm::vec3 v) {
    std::ostringstream s;
    s << "(" << v.x << ", " << v.y << ", " << v.z << ")";
    return s.str();
}

SceneNode* part(SceneNode& parent, const char* name, PrimitiveType type, glm::vec3 pos, glm::vec3 size, glm::vec3 color) {
    auto n = std::make_unique<SceneNode>(name, NodeKind::Part);
    n->primitiveType = type;
    n->transform.position = pos;
    n->transform.scale = size;
    n->color = color;
    return parent.addChild(std::move(n));
}

} // namespace

int main() {
    // A little car: a red block body 4 x 1 x 2 studs, a black ball wheel, a hidden
    // part and a script (neither of which belongs in a model file).
    SceneNode root("Workspace", NodeKind::Model);
    auto car = std::make_unique<SceneNode>("Car", NodeKind::Model);
    SceneNode* carNode = root.addChild(std::move(car));
    part(*carNode, "Body", PrimitiveType::Cube, {0, 0.5f, 0}, {4, 1, 2}, {1, 0, 0});
    part(*carNode, "Wheel", PrimitiveType::Sphere, {3, 1, 0}, {2, 2, 2}, {0, 0, 0});
    part(*carNode, "Ghost", PrimitiveType::Cube, {50, 50, 50}, {1, 1, 1}, {0, 1, 0})->visible = false;
    carNode->addChild(std::make_unique<SceneNode>("Drive", NodeKind::Script));
    const std::vector<const SceneNode*> roots{carNode};
    const glm::vec3 wantSize(6, 2, 2);   // x -2..4, y 0..2, z -1..1

    const fs::path dir = fs::temp_directory_path() / "gb-model-export-test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    for (const char* ext : {".obj", ".fbx", ".glb", ".gltf", ".stl", ".ply", ".dae"}) {
        std::cout << ext << "\n";
        const std::string path = (dir / (std::string("Car") + ext)).string();
        std::string err;
        int parts = 0, tris = 0;
        const bool wrote = ModelExport::write(roots, path, err, &parts, &tris);
        check(wrote && fs::file_size(path) > 100, "writes the file " + err);
        check(parts == 2 && tris > 12, "two parts (the hidden one and the script are left out), " +
                                       std::to_string(tris) + " triangles");
        ModelImport::Result back;
        const bool read = ModelImport::load(path, back, err);
        check(read, "Studio can import it again " + err);
        if (!read) continue;
        check(near(back.size, wantSize, 0.1f), "same size " + str(back.size) + ", want " + str(wantSize));
        if (std::string(ext) == ".stl" || std::string(ext) == ".ply") continue;   // one piece (PLY's colours are on its corners)
        bool red = false, black = false;
        for (const auto& p : back.pieces) {
            red |= near(p.color, {1, 0, 0});
            black |= near(p.color, {0, 0, 0});
        }
        check(back.pieces.size() == 2, "two pieces (" + std::to_string(back.pieces.size()) + ")");
        check(red && black, "keeps the red body and the black wheel");
    }

    std::cout << ".obj writes its colours next to it\n";
    check(fs::exists(dir / "Car.mtl"), "Car.mtl is there too");

    std::cout << ".mesh (Roblox)\n";
    {
        const std::string path = (dir / "Car.mesh").string();
        std::string err;
        check(ModelExport::write(roots, path, err), "writes the file " + err);
        std::ifstream f(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        check(bytes.rfind("version 2.00\n", 0) == 0, "starts like a Roblox mesh (version 2.00)");
        RobloxMesh::Shape s = RobloxMesh::parse(bytes, &err);
        check(s.mesh && !s.mesh->faces.empty(), "Roblox's mesh reader reads it " + err);
        check(near(s.size, wantSize, 0.1f), "same size " + str(s.size));
        check(near(s.center, {0, 0, 0}, 0.01f), "centred on the middle of the shapes " + str(s.center));
        bool red = false;
        if (s.mesh) for (const auto& c : s.mesh->colors) red |= near(glm::vec3(c), {1, 0, 0}, 0.01f);
        check(red, "the body's red is baked onto its corners");
    }

    std::cout << "nothing to export\n";
    {
        SceneNode empty("Empty", NodeKind::Model);
        empty.addChild(std::make_unique<SceneNode>("Main", NodeKind::Script));
        std::string err;
        check(!ModelExport::write({&empty}, (dir / "Empty.obj").string(), err) && !err.empty(), "says so: " + err);
        check(!ModelExport::write(roots, (dir / "Car.xyz").string(), err), "an unknown file type is refused");
    }

    std::cout << "a turned, stretched part keeps its shape\n";
    {
        SceneNode holder("Holder", NodeKind::Model);
        SceneNode* plank = part(holder, "Plank", PrimitiveType::Cube, {0, 0, 0}, {8, 1, 1}, {0.5f, 0.3f, 0.1f});
        plank->transform.rotation = {0, 90, 0};   // now 8 studs long along Z
        const std::string path = (dir / "Plank.glb").string();
        std::string err;
        ModelImport::Result back;
        check(ModelExport::write({&holder}, path, err) && ModelImport::load(path, back, err), "exports and imports " + err);
        check(near(back.size, {1, 1, 8}, 0.1f), "8 studs along Z " + str(back.size));
    }

    if (!failures) fs::remove_all(dir);
    std::cout << (failures ? std::to_string(failures) + " check(s) failed\n" : "all passed\n");
    return failures ? 1 : 0;
}
