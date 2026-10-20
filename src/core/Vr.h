#pragma once
#include <glm/glm.hpp>
#include <string>

struct GLFWwindow;

// VR headsets, through OpenXR: the standard that SteamVR, Quest Link / Air Link,
// Windows Mixed Reality, Pico Connect, Monado and the rest all speak. The game
// draws each eye into its own picture and hands them to the headset; the
// headset says where your head is and what the controllers are doing.
//
// Positions here are in the headset's own space (metres, y up, you start
// looking along -Z); the Player app turns them into studs around your character.
//
// Built only when GB_VR is on (the computer Player). Without it, start() just
// says VR isn't in this build.
namespace Vr {

// Connects to the headset's software (SteamVR...) and starts showing the game in
// it. `window` is the app's window: the headset borrows its OpenGL context.
// False, with a plain-words reason, when there's no headset (or no VR software).
bool start(GLFWwindow* window, std::string& error);
void stop();
// Started, and the headset hasn't closed us (taking it off doesn't close it).
bool running();
// The headset is showing our picture right now (it's on someone's head and we have focus).
bool visible();
// Shown when it stopped by itself (the VR software quit, the headset was unplugged).
const std::string& lastError();

struct View {
    glm::mat4 pose{1.0f};         // where this eye is and the way it looks (headset space)
    glm::mat4 projection{1.0f};   // this eye's lens shape (near / far in studs)
    int width = 0, height = 0;    // how many pixels the headset wants for it
};

// Once a frame, before drawing. Waits for the headset's next moment to show a
// picture (so the game runs at the headset's speed). True = draw both eyes now
// (then submitEye for each, then endFrame); false = nothing to draw this time
// (still call endFrame). `near` / `far` go into the lens shapes.
bool beginFrame(View eyes[2], float nearPlane, float farPlane);
// The finished picture for eye 0 (left) or 1 (right): an OpenGL texture with
// the same size as View::width x height.
void submitEye(int eye, unsigned colorTexture);
void endFrame();

// Where your head is (headset space), this frame.
glm::mat4 head();
// Where a controller is (0 = left hand, 1 = right), headset space, this frame.
// False when the headset can't see it (put down, out of view, switched off).
bool hand(int side, glm::mat4& pose);
// Where a controller points (0 = left, 1 = right): a beam out of it along -Z.
bool aim(int side, glm::mat4& pose);

// A flat screen floating in the headset, for the menus and buttons: the picture
// `colorTexture` (w x h pixels, see-through where it's clear) shown this frame
// at `pose` (headset space; its front faces +Z), `widthMetres` wide. Call it
// after beginFrame, before endFrame. The headset draws it on top of the world.
void showPanel(unsigned colorTexture, int w, int h, const glm::mat4& pose, float widthMetres);

// The controllers (filled in by beginFrame).
struct Controls {
    glm::vec2 move{0.0f};   // left stick: walk (y = forward)
    glm::vec2 turn{0.0f};   // right stick: turn (x)
    bool jump = false;      // A (or the right trackpad / stick click)
    bool menu = false;      // the left controller's menu button, pressed this frame
    bool use = false;       // the right trigger, pressed this frame (use the tool you hold)
    bool useHeld = false;   // the right trigger is down (clicking and dragging on a menu)
};
const Controls& controls();

} // namespace Vr
