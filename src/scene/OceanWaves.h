#pragma once
// The ocean's waves, made the way films and big games do it: a "spectrum" and an FFT.
//
// Real sea is thousands of plain waves of every size and direction on top of each other.
// The spectrum says how tall waves of each size and direction are (long waves rolling
// with the wind are tallest). An FFT is a fast way to add them all up, giving the
// height of the sea on a square grid. The square repeats, so it covers any ocean.
//
// One big grid would have to be huge to hold both 80-stud rollers and finger-sized
// ripples, so the waves are split into bands by size, each with its own square:
//
//   band 0: the big rollers (4 studs and longer), a square 89 studs wide
//   band 1: chop (down to under a stud), a square 21 studs wide
//   band 2: tiny ripples, a square 4.7 studs wide (graphics card only)
//
// The squares are different sizes that don't divide into each other, so added together
// the sea never looks tiled.
//
// Who works it out:
//   * The processor does bands 0 and 1 on small 64 x 64 grids every frame (cheap: a few
//     tenths of a millisecond). The physics floats things on exactly this, on every
//     device and on game servers, which have no graphics card at all.
//   * A graphics card with compute shaders (renderer/OceanGpu) does all three bands on
//     256 x 256 grids, from the same starting numbers. Its big waves are the same waves
//     the processor made, with finer detail on top, so what you see and what you float
//     on agree. Other devices draw the processor's grids.
//
// Everything here is plain C++ with no OpenGL, and nothing is allocated after the
// Field is made.
#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>

namespace OceanWaves {

inline constexpr int   kBands = 3;      // in all
inline constexpr int   kCpuBands = 2;   // ...of which the processor works out these
inline constexpr int   kCpuN = 64;      // the processor's grid (points along a side)
inline constexpr int   kGpuN = 256;     // the graphics card's grid
inline constexpr float kGravity = 9.8f; // deep-water waves: speed from their length

struct Band {
    float length;    // the square's side, in studs
    float minWave;   // only waves at least this long...
    float maxWave;   // ...and shorter than this belong to it (studs, crest to crest)
};
inline constexpr Band kBand[kBands] = {
    {89.0f, 4.0f, 1e9f},
    {21.0f, 0.165f, 4.0f},
    {4.7f, 0.0f, 0.165f},
};

// Which way the wind blows the waves (x, z)
// and how long the tallest waves are, in studs.
inline constexpr float kWindX = 0.958f, kWindZ = 0.287f;
inline constexpr float kPeakWave = 19.0f;

// How much the water bunches towards the crests (sharp peaks, wide troughs) for a
// swell of this height: full for small swells, less for huge ones so a crest never
// folds over itself.
inline float chop(float amplitude) { return amplitude <= 0.0f ? 0.0f : 0.85f * std::min(1.0f, 0.8f / amplitude); }

// One wave's starting size and timing: a complex number (re, im). `nx`, `nz` say which
// wave of the band it is (how many crests fit across the square each way; negative =
// the other way). The same numbers on every computer, for any grid size: a made-up
// "random" pair from the band and nx, nz, shaped by the spectrum.
// Not yet scaled: multiply by Field::scale().
glm::vec2 startWave(int band, int nx, int nz);

// 1D FFT, in place, of `n` complex numbers (n a power of two). `inverse` adds the waves
// up (no dividing by n). For the tests and the Field.
void fft(glm::vec2* data, int n, bool inverse);

class Field {
public:
    Field();
    // Works the waves out for this moment (seconds). Does nothing if it's the same
    // moment as last time.
    void update(float time);
    float time() const { return m_time; }
    int   version() const { return m_version; }   // goes up every time update() changed something

    // Every startWave is multiplied by this, so a swell of height 1 has crests about
    // 1 stud above the still level.
    float scale() const { return m_scale; }

    // The grids, kCpuN x kCpuN each, for a swell of height 1:
    //   disp:  x, z = how far the water is pulled sideways, y = how high it is,
    //          w = how the sideways pull twists (for foam)
    //   slope: x, y = how steep the surface is along x and z,
    //          z, w = how much the sideways pull squeezes the water along x and z
    const glm::vec4* disp(int band) const { return m_disp[band].data(); }
    const glm::vec4* slope(int band) const { return m_slope[band].data(); }

    // Where the water that sits at (x, z) when calm is moved to, for a swell of height
    // `amplitude`: sideways and up. `edge` (0..1) fades the sideways part out near the
    // sides of a pool, so the water never pokes through the walls.
    glm::vec3 offset(float x, float z, float amplitude, float edge) const;
    // The height of the waves above the point (x, z) (the sideways pull undone).
    float heightAt(float x, float z, float amplitude, float edge) const;

private:
    struct Mode { glm::vec2 a, b; float omega, dx, dz; };   // h0(k), conj(h0(-k)), speed, k / |k|
    std::vector<Mode> m_modes[kCpuBands];
    std::vector<glm::vec2> m_h, m_d;                         // working grids (height; sideways x + i z)
    std::vector<glm::vec4> m_disp[kCpuBands], m_slope[kCpuBands];
    float m_scale = 1.0f;
    float m_time = -1.0f;
    int   m_version = 0;
};

} // namespace OceanWaves
