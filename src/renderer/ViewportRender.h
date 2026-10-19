#pragma once
#include <cstdint>

class Scene;
class SceneNode;

// ViewportFrames (game UI): the parts and models inside one, drawn into a picture of
// their own (render-to-texture) as seen from its CurrentCamera, lit by its Ambient,
// LightColor and LightDirection. Only frames that are on screen are drawn (GameGui
// asks while it draws them), and a picture is only redrawn when something in it,
// its camera, its lights or its size changed.
namespace ViewportRender {

// The picture of `frame` (a ViewportFrame) for a box of w x h pixels on screen: an
// OpenGL texture, upside down like the scene's (v = 0 at the bottom). 0 = nothing
// to show (no graphics, or nothing inside it).
unsigned render(Scene& scene, const SceneNode& frame, int w, int h);

// Frees the pictures of frames that haven't been drawn for a while (GameGui calls
// it once a frame).
void collect();

} // namespace ViewportRender
