// The ocean's waves (src/scene/OceanWaves.h): the FFT, the spectrum's bands, and the
// grid the physics floats things on.
//
//   OceanWavesTests
#include "scene/OceanWaves.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
} // namespace

int main() {
    using namespace OceanWaves;

    // --- the FFT ---
    {
        // One plain wave with 3 crests comes out as a single spike at 3.
        const int n = 64;
        std::vector<glm::vec2> a(n);
        for (int i = 0; i < n; ++i) a[i] = glm::vec2(std::cos(6.2831853f * 3.0f * i / n), 0.0f);
        fft(a.data(), n, false);
        bool ok = near(a[3].x, n * 0.5f, 0.01f) && near(a[n - 3].x, n * 0.5f, 0.01f);
        for (int i = 0; i < n; ++i)
            if (i != 3 && i != n - 3 && (std::fabs(a[i].x) > 0.01f || std::fabs(a[i].y) > 0.01f)) ok = false;
        check(ok, "a plain wave turns into one spike (and its mirror)");
        // There and back gives the same numbers (times n).
        std::vector<glm::vec2> b(n), keep(n);
        for (int i = 0; i < n; ++i) keep[i] = b[i] = glm::vec2(std::sin(i * 1.7f) + 0.3f * i, std::cos(i * 0.31f));
        fft(b.data(), n, false);
        fft(b.data(), n, true);
        ok = true;
        for (int i = 0; i < n; ++i)
            if (!near(b[i].x / n, keep[i].x, 1e-3f) || !near(b[i].y / n, keep[i].y, 1e-3f)) ok = false;
        check(ok, "forwards then backwards gives the numbers back");
    }

    // --- the spectrum ---
    {
        check(startWave(0, 0, 0) == glm::vec2(0.0f), "no wave with no crests");
        const glm::vec2 a = startWave(0, 3, 1), b = startWave(0, 3, 1);
        check(a == b && (a.x != 0.0f || a.y != 0.0f), "the same wave has the same start every time");
        check(startWave(0, 3, 1) != startWave(0, 1, 3) && startWave(0, 3, 1) != startWave(1, 3, 1), "different waves start differently");
        // Bands don't overlap: a wave belongs to the band whose sizes it fits.
        bool ok = true;
        for (int band = 0; band < kBands; ++band)
            for (int nx = -40; nx <= 40; ++nx)
                for (int nz = -40; nz <= 40; nz += 3) {
                    if (!nx && !nz) continue;
                    const float wave = kBand[band].length / std::sqrt((float)(nx * nx + nz * nz));
                    const glm::vec2 s = startWave(band, nx, nz);
                    const bool in = wave >= kBand[band].minWave && wave < kBand[band].maxWave;
                    if (!in && (s.x != 0.0f || s.y != 0.0f)) ok = false;
                }
        check(ok, "each band only has waves of its own sizes");
        check(near(kBand[0].minWave, kBand[1].maxWave) && near(kBand[1].minWave, kBand[2].maxWave), "the bands meet with no gap");
        // Waves with the wind are taller than waves against it (on average).
        double with = 0, against = 0;
        for (int n = 2; n < 20; ++n) {
            const glm::vec2 f = startWave(0, n, 0), r = startWave(0, -n, 0);
            with += f.x * f.x + f.y * f.y;
            against += r.x * r.x + r.y * r.y;
        }
        check(with > against * 1.5, "waves roll with the wind more than against it");
    }

    // --- the grids ---
    Field field;
    {
        field.update(3.7f);
        const int N = kCpuN;
        double sum = 0, sq = 0;
        float top = -1e9f, bottom = 1e9f;
        bool finite = true;
        for (int i = 0; i < N * N; ++i) {
            float h = 0.0f;
            for (int b = 0; b < kCpuBands; ++b) {
                const glm::vec4 d = field.disp(b)[i];
                if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) || !std::isfinite(d.w)) finite = false;
                h += d.y;
            }
            sum += h; sq += h * h;
            top = std::max(top, h); bottom = std::min(bottom, h);
        }
        const float mean = (float)(sum / (N * N)), dev = (float)std::sqrt(sq / (N * N) - mean * mean);
        check(finite, "no broken numbers in the grids");
        check(std::fabs(mean) < 0.02f, "the sea is level on average");
        check(dev > 0.2f && dev < 0.6f, "a swell of 1 has waves of the right size");
        check(top > 0.5f && top < 2.0f && bottom < -0.4f && bottom > -2.0f, "crests about a stud up, troughs about a stud down");
    }
    {
        // The slope grid matches the height grid.
        const int N = kCpuN;
        const float cell = kBand[0].length / N;
        const glm::vec4* d = field.disp(0);
        const glm::vec4* s = field.slope(0);
        const float expect = (d[10 * N + 21].y - d[10 * N + 19].y) / (2.0f * cell);
        check(near(s[10 * N + 20].x, expect, 1e-4f), "steepness is the height's change between neighbours");
    }
    {
        // It moves, and the same moment always gives the same sea.
        const float a = field.heightAt(12.0f, -7.0f, 1.0f, 1.0f);
        const int v = field.version();
        field.update(3.7f);
        check(field.version() == v, "asking for the same moment again does no work");
        field.update(4.2f);
        const float b = field.heightAt(12.0f, -7.0f, 1.0f, 1.0f);
        check(std::fabs(a - b) > 1e-4f && field.version() == v + 1, "half a second later the water has moved");
        Field other;
        other.update(3.7f);
        check(near(other.heightAt(12.0f, -7.0f, 1.0f, 1.0f), a, 1e-6f), "two computers at the same moment see the same wave");
    }
    {
        // Floating: the height under a point is where the pulled-sideways water really is.
        field.update(9.0f);
        bool ok = true;
        float worst = 0.0f;
        for (int i = 0; i < 200; ++i) {
            const float x0 = i * 1.37f - 60.0f, z0 = i * 0.91f + 13.0f;
            const glm::vec3 d = field.offset(x0, z0, 0.6f, 1.0f);
            const float h = field.heightAt(x0 + d.x, z0 + d.z, 0.6f, 1.0f);
            worst = std::max(worst, std::fabs(h - d.y));
        }
        if (worst > 0.03f) ok = false;
        check(ok, "the height above a point follows the sideways pull (worst miss " + std::to_string(worst) + ")");
        check(field.heightAt(5.0f, 5.0f, 0.0f, 1.0f) == 0.0f, "no swell, flat water");
        check(near(field.heightAt(5.0f, 5.0f, 2.0f, 0.0f), field.offset(5.0f, 5.0f, 2.0f, 0.0f).y), "at a pool's side the water only goes up and down");
        // It repeats over the bands' squares (and nowhere sooner).
        const float h0 = field.offset(3.0f, 4.0f, 1.0f, 1.0f).y;
        const float rep = kBand[0].length * kBand[1].length;
        check(near(field.offset(3.0f + rep, 4.0f, 1.0f, 1.0f).y, h0, 0.02f), "the sea only repeats after both squares line up");
        check(!near(field.offset(3.0f + kBand[0].length, 4.0f, 1.0f, 1.0f).y, h0, 1e-4f), "one square along it is a different sea");
    }
    {
        // A bigger swell never folds a crest over itself.
        check(chop(0.0f) == 0.0f && near(chop(0.5f), 0.85f) && chop(4.0f) * 4.0f <= 0.85f * 0.8f + 1e-4f, "big swells bunch up less, so crests can't fold over");
        field.update(2.0f);
        const int N = kCpuN;
        int folded = 0;
        for (float A : {0.3f, 0.6f, 1.0f, 2.0f, 4.0f}) {
            const float c = A * chop(A);
            for (int i = 0; i < N * N; ++i) {
                float jxx = 0, jzz = 0, jxz = 0;
                for (int b = 0; b < kCpuBands; ++b) { jxx += field.slope(b)[i].z; jzz += field.slope(b)[i].w; jxz += field.disp(b)[i].w; }
                if ((1.0f + c * jxx) * (1.0f + c * jzz) - c * c * jxz * jxz <= 0.0f) ++folded;
            }
        }
        check(folded == 0, "no folded-over water at any swell height (" + std::to_string(folded) + " points)");
    }
    {
        // Cheap enough to do every frame.
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 50; ++i) field.update(20.0f + i * 0.016f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 50.0;
        std::cout << "      (one update takes " << ms << " ms)\n";
        check(ms < 8.0, "an update is fast enough for every frame");
    }

    std::cout << (failures ? "\nFAILED\n" : "\nall passed\n");
    return failures ? 1 : 0;
}
