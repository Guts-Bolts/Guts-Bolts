#pragma once
// VideoFrame: a video on a screen UI or, inside a SurfaceGui, on a part (a TV, a cinema
// screen, an advert board). Its Video is an MPEG-1 file (.mpg) in the games folder or
// "gb:<id>"; pictures and sound both play. Scripts: Play(), Pause(), Playing, Looped,
// Volume, TimePosition, TimeLength, IsLoaded, Resolution, and the Played / Paused /
// Ended / DidLoop / Loaded events.
//
// Every computer plays its own copy (Playing and Looped are part of the game, like any
// property, so everyone's starts together).
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

class Scene;
class SceneNode;

namespace Videos {

enum class Event { Loaded, Played, Paused, Ended, DidLoop };

// Move every playing video on (call once a frame while a game runs). `fire` hears what
// happened, for the scripts' events.
void update(Scene& scene, float dt, const std::function<void(uint64_t videoFrame, Event)>& fire = nullptr);

// The picture to show now (a GL texture), or 0 while there's none. Shows the first
// picture of a video that isn't playing yet.
unsigned texture(const SceneNode& videoFrame);

// About its video (loads it if it isn't yet). False if it can't be read.
bool loaded(const SceneNode& videoFrame);
double length(const SceneNode& videoFrame);      // seconds
glm::ivec2 resolution(const SceneNode& videoFrame);
double position(const SceneNode& videoFrame);    // TimePosition (seconds)
void seek(const SceneNode& videoFrame, double seconds);
// The colour of the picture now at (u, v), 0..1 across and down (tests; 0 if there's none).
glm::vec3 colourAt(const SceneNode& videoFrame, float u, float v);

// Stop every video and let go of them (when a game stops).
void stopAll();

} // namespace Videos
