# Tracy: a detailed profiler for engine work

Game makers find slow parts of their games with the **Live Profiler** (Studio's
View menu, or Ctrl+F6 in the Player). Tracy is for the people working on the
engine itself: it shows every frame as a timeline, down to fractions of a
millisecond, so you can see exactly what the engine was doing when a frame was slow.

It's **off by default**. Normal builds, the website downloads, phones and the web
Player never include it.

## Turning it on

1. Build with the switch:

   ```sh
   cmake -S . -B build-tracy -DGB_TRACY=ON -DCMAKE_BUILD_TYPE=Release
   cmake --build build-tracy
   ```

2. Get the Tracy **viewer** (the window that shows the timeline) for the same
   version, v0.13.1: https://github.com/wolfpld/tracy/releases/tag/v0.13.1
   (Windows has a ready-made `.exe`; on Mac and Linux, build `profiler/` from that tag).
3. Start Studio or the Player from `build-tracy`, then press **Connect** in the viewer.

It only records while the viewer is connected, and it only accepts a viewer on the
same computer (`TRACY_ONLY_LOCALHOST`), so a Tracy build doesn't open anything to
the network.

No screen (a server or a cloud machine)? Build Tracy's `capture` and `csvexport`
tools from the same tag instead. `tracy-capture -o run.tracy -a 127.0.0.1 -s 8`
records 8 seconds, and `tracy-csvexport run.tracy` lists every timed bit with how
often it ran and how long it took.

## What you'll see

- One frame mark per frame (Studio and the Player).
- Every `FrameProfile::Scope` in the engine, such as `draw: shadows`,
  `draw: post effects` and `game: scripts`.
- Each script's run, by its name (`Workspace.Door.Script`), nested inside the
  part of the frame that ran it.

## Timing something new

```cpp
#include "core/DevProfiler.h"

void loadMap() {
    GB_ZONE("load the map");
    ...
}
```

`GB_ZONE` does nothing (and costs nothing) unless the build has `GB_TRACY`.
If the timing should also show in the Live Profiler, use
`FrameProfile::Scope` instead: it shows up in both.
