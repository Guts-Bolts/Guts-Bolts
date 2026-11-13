#pragma once
// The ocean's waves as pictures the water shader reads (see scene/OceanWaves.h for how
// the waves are made).
//
// For each band there are two pictures that repeat across the sea:
//   disp:  red, blue = how far the water is pulled sideways (x, z), green = its height,
//          alpha = how the pull twists
//   slope: red, green = how steep the surface is along x and z,
//          blue, alpha = how much the pull squeezes the water along x and z
// (all for a swell of height 1; the shader multiplies by the water part's own swell).
//
// Two ways to fill them, and the shader can't tell which was used:
//   * Graphics cards with compute shaders work all three bands out themselves on
//     256 x 256 grids, every frame: a spectrum pass, the FFT (each row, then each
//     column, a whole row per work group), and a pass that sorts the results into the
//     two pictures.
//   * Everything else (Macs, iPhones, the web Player, Low and Medium water) uploads the
//     64 x 64 grids the processor already made for the physics: two bands.
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace OceanWaves { class Field; }

class OceanGpu {
public:
    OceanGpu();
    ~OceanGpu();
    OceanGpu(const OceanGpu&) = delete;
    OceanGpu& operator=(const OceanGpu&) = delete;

    // Can this device work the waves out on the graphics card? (`why` says why not.)
    static bool supported(std::string* why = nullptr);

    // Brings the pictures up to date with the field's moment. `wantGpu`: use the
    // graphics card if it can. Returns how many bands there are to draw (2 or 3).
    // Changes which program and textures are bound.
    int prepare(const OceanWaves::Field& field, bool wantGpu);

    unsigned disp(int band) const { return m_disp[band]; }
    unsigned slope(int band) const { return m_slope[band]; }
    int  size() const { return m_size; }           // points along a side of each picture
    bool onGpu() const { return m_gpu; }
    const std::string& error() const { return m_error; }   // why the graphics card path was given up (if it was)

    void release();

private:
    void makePictures(int size, int bands, bool fixedSize);
    void uploadFromProcessor(const OceanWaves::Field& field);
    bool initCompute(const OceanWaves::Field& field);
    void runCompute(float time);

    unsigned m_disp[3] = {0, 0, 0}, m_slope[3] = {0, 0, 0};
    int   m_size = 0, m_bands = 0;
    bool  m_gpu = false, m_gpuFailed = false;
    int   m_version = -1;
    float m_time = -1.0f;
    std::string m_error;
    std::vector<glm::vec4> m_small[2];     // room for the smaller copies of the processor's grids
    // Compute path only:
    unsigned m_start[3] = {0, 0, 0};       // each wave's starting size and timing
    unsigned m_work[4] = {0, 0, 0, 0};     // the FFT's working pictures (A, B and a spare of each)
    unsigned m_progSpectrum = 0, m_progFft = 0, m_progFinish = 0;
};
