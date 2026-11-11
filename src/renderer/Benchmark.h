#pragma once
#include <string>

class SceneRenderer;

// The quick graphics benchmark (Scalability.h): draws a small test world (a floor, rows of
// blocks in different materials, lamps, the sun) for a few frames at High, and says how
// long one frame took. Auto quality starts at the highest level that fits (pickLevel).
// Runs once per graphics chip, in well under a second.
namespace GraphicsBenchmark {

// Milliseconds per frame at High, drawing w x h pixels (0 if it couldn't run).
float run(SceneRenderer& renderer, int w, int h, int frames = 12);

// This graphics chip's name (OpenGL's GL_RENDERER).
std::string gpuName();

} // namespace GraphicsBenchmark
