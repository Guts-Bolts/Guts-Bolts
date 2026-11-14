#pragma once
// What this device's graphics chip can do, and which Guts&Bolts features need what.
//
// Some features run their maths on the graphics card with "compute shaders". Windows
// and Linux computers with OpenGL 4.3+ and newer Android phones (OpenGL ES 3.1) have
// them; Macs (Apple stopped at OpenGL 4.1), iPhones / iPads (OpenGL ES 3.0) and the
// web Player (WebGL 2) don't. On those, a feature either uses a simpler version or is
// left out, and Settings says so (see the notice below).
//
// Everything asks here instead of checking version numbers by itself, so there is one
// place that decides. The numbers are read from the graphics driver once by
// GpuCaps::get() (src/renderer/GpuCaps.h); this file has no OpenGL in it, so the
// tests can make up any device they like.
#include <string>

struct GpuCaps {
    int  version = 0;          // 41 = OpenGL 4.1, 31 = OpenGL ES 3.1...
    bool es = false;           // OpenGL ES / WebGL (phones, the web Player)
    bool safeMode = false;     // Settings > Graphics API > Safe mode: use nothing new

    bool compute = false;          // compute shaders + storage buffers (enough of them)
    bool indirectDraw = false;     // the graphics card reads "what to draw" from a buffer
    bool multiDrawIndirect = false;// ...many of them in one go (computers only)
    bool floatTargets = true;      // HDR pictures to draw into
    // Squeezed textures the chip can read as they are:
    bool bc7 = false, bc5 = false; // computers
    bool astc = false, etc2 = false; // phones, tablets, some web browsers

    // Works out the rest from a version, the driver's extension names (separated by
    // spaces) and how many storage buffers a compute shader may use.
    static GpuCaps from(int version, bool es, const std::string& extensions, int storageBlocks,
                        bool safeMode = false);
};

namespace DeviceFeatures {

// A feature that not every device can run.
enum Feature {
    GpuLiquid,       // real liquid worked out on the graphics card (many more drops)
    FftOcean,        // the ocean's waves worked out on the graphics card
    GpuBlood,        // thick blood as real liquid
    GpuCulling,      // the graphics card decides what is in view
    kFeatures
};

struct Info {
    const char* name;      // shown to players, in plain words
    const char* instead;   // what such a device gets instead
    bool        shipped;   // is it in the game yet? (planned ones aren't mentioned to players)
};
const Info& info(Feature f);

// Can this device run it?
bool can(const GpuCaps& caps, Feature f);

// The features in the game today that this device can't run. Fills `out` (up to
// kFeatures) and returns how many. 0 = this device does everything.
int missing(const GpuCaps& caps, Feature out[kFeatures]);

// The notice's first line (the same for everyone who gets one).
inline constexpr const char* kNotice =
    "This device can't run some Guts&Bolts graphics features. The game still works, with a simpler look for these:";
// Why, in one plain line ("Macs stop at OpenGL 4.1...").
const char* reason(const GpuCaps& caps);

} // namespace DeviceFeatures
