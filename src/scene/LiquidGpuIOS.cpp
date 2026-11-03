// iOS and web fallback: OpenGL ES 3.0 / WebGL 2 do not provide the compute shaders / SSBOs
// required by the accelerated liquid solver. Liquid.cpp uses its CPU solver
// when supported() is false, so the rest of the engine remains available.
#include "LiquidGpu.h"

LiquidGpu::LiquidGpu() = default;
LiquidGpu::~LiquidGpu() = default;

bool LiquidGpu::supported(std::string* why) {
    if (why) *why = "GPU liquid simulation requires OpenGL ES 3.1 compute shaders; iOS uses the CPU solver."; 
    return false;
}

bool LiquidGpu::init(std::string& error) {
    error = "GPU liquid simulation is unavailable on this OpenGL ES 3.0 target."; 
    return false;
}

uint32_t LiquidGpu::maxCapacity() { return 0; }
void LiquidGpu::beginFrame() {}
void LiquidGpu::setColliders(const std::vector<glm::vec4>&, const std::vector<uint64_t>&, const std::vector<int>&, int,
                             const glm::ivec3&, const glm::ivec3&, float) {}
void LiquidGpu::setPools(const std::vector<Pool>&) {}
void LiquidGpu::step(float, const glm::vec3&, const std::vector<glm::vec4>&) {}
void LiquidGpu::endFrame(const std::vector<glm::vec4>&) {}
void LiquidGpu::clear() { m_res = Results{}; }
void LiquidGpu::dropReadbacks() {}
void LiquidGpu::collect(Readback&, bool) {}
void LiquidGpu::forget() { clear(); }
std::vector<glm::vec4> LiquidGpu::readDrops() { return {}; }
