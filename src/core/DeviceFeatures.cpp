#include "DeviceFeatures.h"

namespace {
bool has(const std::string& all, const char* part) { return all.find(part) != std::string::npos; }
}

GpuCaps GpuCaps::from(int version, bool es, const std::string& ext, int storageBlocks, bool safeMode) {
    GpuCaps c;
    c.version = version;
    c.es = es;
    c.safeMode = safeMode;
    const int computeAt = es ? 31 : 43;
    // (The liquid needs 8 storage buffers in one compute shader; every real GL 4.3 /
    // ES 3.1 chip we've seen has them, but the rule only promises 4 on phones.)
    c.compute = !safeMode && version >= computeAt && storageBlocks >= 8;
    c.indirectDraw = !safeMode && version >= (es ? 31 : 40);
    c.multiDrawIndirect = !safeMode && ((!es && version >= 43) || has(ext, "multi_draw_indirect"));
    c.floatTargets = !es || has(ext, "color_buffer_float") || has(ext, "color_buffer_half_float");
    c.bc7 = (!es && version >= 42) || has(ext, "texture_compression_bptc");
    c.bc5 = !es || has(ext, "texture_compression_rgtc");
    c.astc = has(ext, "texture_compression_astc") || has(ext, "compressed_texture_astc");
    c.etc2 = es || (version >= 43) || has(ext, "ES3_compatibility") || has(ext, "compressed_texture_etc");
    return c;
}

namespace DeviceFeatures {

const Info& info(Feature f) {
    // `shipped` is turned on by the update that adds the feature.
    static const Info kInfo[kFeatures] = {
        {"Liquid on the graphics card", "Liquid runs on the processor, with fewer drops.", true},
        {"Ocean waves on the graphics card", "The ocean has the same waves, without the finest ripples.", true},
        {"Thick liquid blood", "Blood stays as stains and splashes.", false},
        {"Graphics card picks what to draw", "The processor picks what to draw (as before).", false},
    };
    return kInfo[f >= 0 && f < kFeatures ? f : 0];
}

bool can(const GpuCaps& c, Feature f) {
    switch (f) {
        case GpuLiquid:  return c.compute && c.indirectDraw;
        case FftOcean:   return c.compute && c.floatTargets;
        case GpuBlood:   return c.compute && c.indirectDraw;
        case GpuCulling: return c.compute && c.multiDrawIndirect;
        default:         return false;
    }
}

int missing(const GpuCaps& caps, Feature out[kFeatures]) {
    int n = 0;
    for (int f = 0; f < kFeatures; ++f)
        if (info((Feature)f).shipped && !can(caps, (Feature)f)) out[n++] = (Feature)f;
    return n;
}

const char* reason(const GpuCaps& c) {
    if (c.safeMode) return "Graphics API is set to Safe mode in Settings. Pick Auto to turn them back on.";
#if defined(__EMSCRIPTEN__)
    return "Web browsers can't run them yet. The Guts&Bolts app for Windows or Linux can.";
#elif defined(GB_IOS)
    return "iPhones and iPads don't have the graphics features these need.";
#elif defined(__APPLE__)
    return "Macs don't have the graphics features these need (Apple stopped at OpenGL 4.1).";
#else
    if (c.es) return "This phone's graphics chip is too old for them (they need OpenGL ES 3.1).";
    return "This graphics card or its driver is too old for them (they need OpenGL 4.3). Updating the driver can help.";
#endif
}

} // namespace DeviceFeatures
