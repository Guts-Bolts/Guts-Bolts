#include "GpuCaps.h"
#include "GL.h"
#include "../core/Settings.h"

namespace {
bool    s_asked = false;
GpuCaps s_caps;
}

namespace GpuCapsQuery {

const GpuCaps& get() {
    if (s_asked) return s_caps;
    GLint major = 0, minor = 0, count = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    if (major <= 0) return s_caps;   // no context yet: say "nothing", and ask again later
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    std::string ext;
    for (GLint i = 0; i < count; ++i)
        if (const char* e = (const char*)glGetStringi(GL_EXTENSIONS, (GLuint)i)) { ext += e; ext += ' '; }
#if defined(GB_GLES) || defined(__EMSCRIPTEN__)
    const bool es = true;
#else
    const bool es = false;
#endif
    const int version = major * 10 + minor;
    GLint blocks = 0;
    if (version >= (es ? 31 : 43)) {
        constexpr GLenum kMaxComputeBlocks = 0x90DB, kMaxBindings = 0x90DD;   // GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS
        GLint bindings = 0;
        glGetIntegerv(kMaxComputeBlocks, &blocks);
        glGetIntegerv(kMaxBindings, &bindings);
        if (bindings < blocks) blocks = bindings;
#if !defined(GB_GLES) && !defined(__EMSCRIPTEN__)
        // (An old driver can say 4.3 and still not have loaded the calls.)
        if (!glDispatchCompute || !glDispatchComputeIndirect || !glMemoryBarrier || !glDrawArraysIndirect) blocks = 0;
#endif
    }
    s_caps = GpuCaps::from(version, es, ext, blocks,
                           GraphicsSettings::get().graphicsApi == GraphicsSettings::ApiSafe);
    s_asked = true;
    return s_caps;
}

void forget() { s_asked = false; s_caps = GpuCaps{}; }

} // namespace GpuCapsQuery
