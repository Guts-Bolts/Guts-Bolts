#pragma once
class Camera;
class Player;
class Scene;
struct CameraOptions;

// The camera while playing, like Roblox's: it orbits the character's head,
// the mouse wheel zooms, and zooming all the way in goes into first person
// (the camera sits in the head). As the camera comes close, your own character
// fades away so it doesn't block the view; in first person it's invisible
// (except the tool you're holding). Used by the Player app and Studio's Play.
//
// Scripts steer it through workspace.CurrentCamera (Scene::camera()): CameraType
// (Custom / Scriptable / Fixed), CFrame, FieldOfView, CameraSubject, and the
// player's CameraMode and zoom limits (Scene::cameraOptions()).
namespace PlayCamera {

bool firstPerson(const Camera& cam);
// Up/down for swimming, from where the camera looks: -1 straight down .. 1 straight up.
// (The normal over-the-shoulder view looks down a little: that counts as level.)
float swimLook(const Camera& cam);
// Mouse wheel (or pinch): closer / further, snapping into and out of first person,
// within the player's CameraMinZoomDistance / CameraMaxZoomDistance (and never
// out of first person with CameraMode LockFirstPerson).
void zoom(Camera& cam, float wheel, const CameraOptions& options);
// Move the camera with the character (call every frame while playing).
// `shiftLock`: Roblox's Shift Lock: the camera sits over the right shoulder and
// the character turns to face wherever the camera looks.
void follow(Camera& cam, Player& player, float dt, bool shiftLock = false);
// Mouse movement -> camera turn, with the player's sensitivity / invert settings.
void turn(Camera& cam, float dx, float dy);
// Fade the character by how close the camera is.
void fade(Scene& scene, Player& player, const Camera& cam);

} // namespace PlayCamera
