#pragma once
// The Live Profiler: a window that shows, while a game runs, how long each part of a
// frame takes (physics, scripts, water, drawing...) and which scripts take the longest,
// so you can find what makes a game laggy. Studio: View > Live Profiler. Player: Ctrl+F6.
// The numbers come from FrameProfile (core/FrameProfile.h); it only measures while open.
namespace LiveProfiler {

// Draw the window (call every frame). Closing it stops the measuring.
void draw(bool* open);

// "game: part physics" -> "Part physics", "draw: liquid" -> "Drawing: liquid".
const char* niceName(const char* section, char* buffer, int size);

} // namespace LiveProfiler
