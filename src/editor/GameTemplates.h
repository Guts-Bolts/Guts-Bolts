#pragma once
#include <vector>

class Scene;

// Game templates: File > New from Template starts a game that already works instead of an
// empty baseplate. Each one is built from ready-made pieces (Premades.h, LevelGen.h) with
// short scripts you can read and change.
namespace GameTemplates {

enum class Kind { Obby, Tycoon, SwordFight, Racing };

struct Info {
    Kind        kind;
    const char* name;
    const char* tip;
};
const std::vector<Info>& list();

// Turn `scene` (fresh from Scene::buildDefault) into the template: its parts, scripts,
// spawn and the game's title and description.
void build(Scene& scene, Kind kind);

} // namespace GameTemplates
