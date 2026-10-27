// MaterialVariants (the Material Editor's materials) going to Roblox files and back, and
// coming in from places made in Roblox Studio.
#include "renderer/Mesh.h"
#include "scene/RobloxFile.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
SceneNode* find(Scene& scene, const std::string& name) {
    SceneNode* found = nullptr;
    scene.forEach([&](SceneNode* n) { if (!found && n->name == name) found = n; });
    return found;
}

// A place as Roblox Studio saves it (cut down): a MaterialVariant and a part using it.
const char* kPlace = R"(<roblox version="4">
<Item class="Workspace" referent="RBX0"><Properties><string name="Name">Workspace</string></Properties>
 <Item class="Part" referent="RBX1"><Properties>
  <string name="Name">Floor</string>
  <CoordinateFrame name="CFrame"><X>0</X><Y>0</Y><Z>0</Z><R00>1</R00><R01>0</R01><R02>0</R02><R10>0</R10><R11>1</R11><R12>0</R12><R20>0</R20><R21>0</R21><R22>1</R22></CoordinateFrame>
  <Vector3 name="size"><X>20</X><Y>1</Y><Z>20</Z></Vector3>
  <token name="Material">512</token>
  <string name="MaterialVariantSerialized">Old Planks</string>
  <bool name="Anchored">true</bool>
 </Properties></Item>
</Item>
<Item class="MaterialService" referent="RBX2"><Properties><string name="Name">MaterialService</string></Properties>
 <Item class="MaterialVariant" referent="RBX3"><Properties>
  <string name="Name">Old Planks</string>
  <token name="BaseMaterial">512</token>
  <Content name="ColorMap"><url>rbxassetid://123456</url></Content>
  <Content name="NormalMap"><null></null></Content>
  <float name="StudsPerTile">4</float>
 </Properties></Item>
</Item>
</roblox>
)";
} // namespace

int main() {
    Mesh::headless = true;
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gb-material-tests";
    std::filesystem::create_directories(dir);

    // From Roblox Studio.
    {
        const std::string path = (dir / "from-roblox.rbxlx").string();
        std::ofstream(path) << kPlace;
        Scene scene;
        RobloxFile::Report report;
        std::string err;
        check(RobloxFile::importPlace(scene, path, report, err), "a place with a MaterialVariant imports " + err);
        SceneNode* svc = scene.serviceFolder("MaterialService", false);
        SceneNode* v = find(scene, "Old Planks");
        check(svc && v && v->isMaterialVariant() && v->parent == svc, "the MaterialVariant lands in MaterialService");
        check(v && v->variant.base == Material::Wood && v->variant.colorMap == "rbxassetid://123456" &&
              v->variant.normalMap.empty() && v->variant.studsPerTile == 4.0f, "...with its base material, pictures and size");
        SceneNode* floor = find(scene, "Floor");
        check(floor && floor->materialVariant == "Old Planks", "the part still uses it");
    }

    // Ours, to Roblox and back.
    {
        Scene scene;
        scene.buildDefault();
        SceneNode* svc = scene.serviceFolder("MaterialService", true);
        auto v = std::make_unique<SceneNode>("Shiny Tiles", NodeKind::MaterialVariant);
        v->variant.base = Material::Metal;
        v->variant.colorMap = "tiles.png";
        v->variant.roughness = 0.2f;
        v->variant.metalness = 0.9f;
        v->variant.glow = 1.5f;
        v->variant.studsPerTile = 6.0f;
        scene.insert(std::move(v), svc);
        auto part = std::make_unique<SceneNode>("Wall", NodeKind::Part);
        part->material = Material::Metal;
        part->materialVariant = "Shiny Tiles";
        scene.insert(std::move(part), nullptr);
        const std::string path = (dir / "ours.rbxlx").string();
        std::string err;
        check(RobloxFile::exportPlace(scene, path, err), "exported " + err);

        Scene back;
        RobloxFile::Report report;
        check(RobloxFile::importPlace(back, path, report, err), "imported again " + err);
        SceneNode* bv = find(back, "Shiny Tiles");
        check(bv && bv->isMaterialVariant() && bv->variant.base == Material::Metal && bv->variant.colorMap == "tiles.png" &&
              bv->variant.studsPerTile == 6.0f, "the material comes back");
        check(bv && bv->variant.roughness == 0.2f && bv->variant.metalness == 0.9f && bv->variant.glow == 1.5f &&
              !bv->findAttribute("GutsAndBoltsMaterial"), "...with our own numbers too");
        SceneNode* wall = find(back, "Wall");
        check(wall && wall->materialVariant == "Shiny Tiles", "the part still uses it");
    }

    std::filesystem::remove_all(dir);
    std::cout << (failures ? "material variant tests FAILED\n" : "material variant tests passed\n");
    return failures ? 1 : 0;
}
