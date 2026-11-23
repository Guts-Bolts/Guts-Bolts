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

// The performance report (tools/perf/README.md, GB_PERF_REPORT=<file>): draws the same
// test world at every quality level, on the phone profile, with battery saver, and at
// High with one costly effect taken away at a time, and writes a plain table of how long
// a frame took and how much memory the program was using. GB_PERF_FRAMES sets how many
// frames each line averages (default 12), GB_PERF_SIZE the size ("1280x720").
bool writeReport(SceneRenderer& renderer, const std::string& path);

} // namespace GraphicsBenchmark
