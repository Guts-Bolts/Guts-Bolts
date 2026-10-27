#pragma once
#include <imgui.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class Scene;
class SceneNode;

// Game UI made of ScreenGui / Frame / TextLabel / TextButton / TextBox / ImageLabel /
// ImageButton / ScrollingFrame objects (with UICorner, UIStroke, UIShadow, UIBlur,
// UIPadding, UIListLayout and UIGridLayout inside them), drawn over the
// 3D view like Roblox's. Used by the Player app, Studio's Play and Studio's
// viewport (so you see the UI while you build it).
namespace GameGui {

// What the pointer is doing with the UI (kept between frames by whoever shows the game).
struct Input {
    uint64_t hovered = 0;   // the button under the pointer
    uint64_t pressed = 0;   // the button the mouse went down on
    uint64_t dragging = 0;  // a ScrollingFrame being dragged (its bar, or a finger on it)
    bool     dragBar = false;
    float    dragFrom = 0.0f;   // (dragging the bar) where on the thumb it was grabbed
};

// Focused / FocusLost / TextChanged are a TextBox's (`enter`: FocusLost because Enter was pressed).
// Prompt* are a ProximityPrompt's (see prompts()).
enum class EventKind { Click, Enter, Leave, Focused, FocusLost, TextChanged,
                       PromptShown, PromptHidden, PromptHoldBegan, PromptHoldEnded, PromptTriggered, PromptTriggerEnded };
struct Event { EventKind kind; uint64_t id; bool enter = false; };

// The camera the world is drawn with (before draw / handle each frame), so BillboardGuis
// and SurfaceGuis land on their parts. Without it they aren't shown.
void setCamera(const glm::mat4& view, const glm::mat4& proj, const glm::vec3& position);

// Draw every enabled ScreenGui, BillboardGui and SurfaceGui in the scene over [min, max]. Also works out each
// object's AbsolutePosition / AbsoluteSize. `selected` gets a highlight (Studio).
void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, Scene& scene, const Input* input = nullptr, uint64_t selected = 0);

// Pointer input. `down` = the button went down this frame, `up` = it came up,
// `tapped` = a finger tapped (touch screens). Returns true when the pointer is
// over the UI, so the click shouldn't also reach the 3D world.
bool handle(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 pointer, bool inside, bool down, bool up, bool tapped,
            Input& input, std::vector<Event>& events);

// TextBoxes: after draw(), in the same ImGui window, this puts the typing box over the
// TextBox that has focus (clicked, or box:CaptureFocus()), so you get a cursor,
// selection, copy and paste and the phone keyboard. While it's up ImGui's
// WantTextInput is set, so typing doesn't also walk the character around.
void textInput(Scene& scene, std::vector<Event>& events);
uint64_t focused();                         // the TextBox being typed in (0 = none)
void focus(uint64_t id, bool enter = false); // CaptureFocus (0 = ReleaseFocus: `enter` is its `submitted`)

// Is the pointer over something that scrolls (so the mouse wheel scrolls it, not the camera)?
bool overScroller(Scene& scene, ImVec2 p);
// Where the ScrollingFrames are on screen (touch controls leave fingers there alone).
std::vector<std::pair<ImVec2, ImVec2>> scrollerRects(Scene& scene);

// The topmost UI object under `p` (Studio picks these like parts), or null.
SceneNode* pick(Scene& scene, ImVec2 min, ImVec2 max, ImVec2 p, bool buttonsOnly = false);

// Work out every object's AbsolutePosition / AbsoluteSize now, on the screen last
// drawn to (scripts ask right after making things, before the next frame).
void refresh(Scene& scene);

// Where the game was last drawn on the window (Studio's 3D view sits inside a
// bigger window), so a mouse position can be turned into a spot on the game screen.
void lastScreen(ImVec2& min, ImVec2& max);

// UIBlur: does anything on screen blur the world behind it? If so, whoever shows the
// game makes SceneRenderer::makeBackdrop() after drawing the world and hands the
// blurred pictures (a little blurry first, very blurry last) to setBackdrop() before
// draw(). Without them UIBlur draws nothing.
bool needsBackdrop(Scene& scene);
void setBackdrop(const unsigned* levels, int count);

// Where a point in the world is on the screen (false: behind the camera, or no camera
// set), and whether the camera can see it (`part` and its model don't block it).
bool worldToScreen(const glm::vec3& p, ImVec2 min, ImVec2 max, ImVec2& out);
bool canSee(Scene& scene, uint64_t key, const glm::vec3& p, const SceneNode* part);

// ProximityPrompts (Prompts.cpp): the "E  Open door" cards that pop up when your
// character (at `player`; null = nobody, so none) is close to one. Each frame after
// setCamera(): works out which show, and handles their key and clicks / taps on them,
// adding Prompt* events. Returns true when the pointer is on one (the click is
// theirs, not the world's). drawPrompts() draws them (after draw()).
bool prompts(Scene& scene, ImVec2 min, ImVec2 max, const glm::vec3* player, ImVec2 pointer, bool acceptInput,
             float dt, std::vector<Event>& events);
void drawPrompts(ImDrawList* dl);

// Its rectangle on screen as last drawn (min, max). False if it isn't on screen.
bool rectOf(Scene& scene, ImVec2 min, ImVec2 max, const SceneNode* node, ImVec2& a, ImVec2& b);

} // namespace GameGui
