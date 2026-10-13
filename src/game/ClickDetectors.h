#pragma once
#include <imgui.h>
#include <glm/glm.hpp>
#include <string>

class Scene;
class SceneNode;

// ClickDetectors, like Roblox's: one inside a part makes that part clickable; one inside
// a model makes every part in it clickable. GameSession uses these to fire their events;
// the host uses them to check a joiner's click.
namespace ClickDetectors {

// The ClickDetector a click on this part belongs to (its own, else the nearest model's
// it's in), or null.
SceneNode* find(Scene& scene, SceneNode* part);
// How far `from` is from the nearest point of this part's box.
float distanceToPart(const SceneNode* part, const glm::vec3& from);
// How far `from` is from the nearest part this detector makes clickable (a big number
// if there's none).
float distance(const SceneNode* detector, const glm::vec3& from);

// The pointer over something clickable this frame: a hand, or its CursorIcon picture
// (drawn at the pointer, on top of everything).
void showPointer(ImVec2 at, const std::string& cursorIcon);
// Playing with a controller: a ring in the middle of the screen, filled in when what's
// there can be clicked (RT clicks it).
void drawAim(ImDrawList* dl, ImVec2 centre, bool onClickable);

} // namespace ClickDetectors
