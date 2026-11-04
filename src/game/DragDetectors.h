#pragma once
// DragDetectors, like Roblox's: put one in a part (or a Model) and players can grab it
// with the mouse or a finger and drag it about. It's a ClickDetector too (MouseClick,
// hovering, MaxActivationDistance, CursorIcon all work), plus:
//
//   DragStyle  TranslatePlane      slide it across a flat surface (the floor, by default)
//              TranslateLine       slide it along one line (up and down, by default)
//              TranslateViewPlane  move it across the screen, at the same distance
//              RotateAxis          spin it round (a wheel, a valve, a door)
//   Axis       which way that line / surface / spin goes, turned with the thing (0, 1, 0)
//   MinDragTranslation / MaxDragTranslation   how far along the line it may go (studs)
//   MinDragAngle / MaxDragAngle               how far it may turn (degrees)
//   DragStart / DragContinue / DragEnd        events, with the player who's dragging
//
// Anchored things simply move; loose ones are carried, and keep their speed when let go
// (so they can be thrown). GameSession runs a drag from the pointer each frame; in
// multiplayer the host does the real move (NetGame), checking the player is close.
#include <cstdint>
#include <glm/glm.hpp>

class Scene;
class SceneNode;

namespace DragDetectors {

enum Style { TranslatePlane = 0, TranslateLine = 1, TranslateViewPlane = 2, RotateAxis = 3 };

struct Drag {
    uint64_t  detector = 0;
    uint64_t  thing = 0;          // what moves: the part or Model the detector is in
    int       style = 0;
    glm::vec3 grab{0.0f};         // where the pointer first took hold (world)
    glm::vec3 pivot{0.0f};        // the thing's middle when it was grabbed (world)
    glm::vec3 normal{0.0f, 1.0f, 0.0f};   // the surface the pointer moves on
    glm::vec3 axis{0.0f, 1.0f, 0.0f};     // the line / spin axis (world)
    float     startAngle = 0.0f;
    glm::mat4 startWorld{1.0f};   // the thing's world matrix when grabbed
    glm::vec3 lastPos{0.0f};      // (for the speed it's let go with)
    bool      active() const { return detector != 0; }
};

// Is this a DragDetector that can be used now?
bool isDrag(const SceneNode* n);

// Take hold of what `detector` drags, at `hit` (where the pointer's ray met it).
// `camForward`: the way the camera looks (for TranslateViewPlane).
bool begin(Scene& scene, SceneNode* detector, const glm::vec3& hit, const glm::vec3& camForward, Drag& out);

// The pointer's ray now (origin, direction): move the thing to follow it.
// False if the drag can't go on (the thing is gone).
bool move(Scene& scene, Drag& d, const glm::vec3& rayOrigin, const glm::vec3& rayDir, float dt);

// Let go (a loose part keeps the speed it was moving at).
void end(Scene& scene, Drag& d, float dt);

} // namespace DragDetectors
