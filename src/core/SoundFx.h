#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

// Sound effects (ReverbSoundEffect, EchoSoundEffect...): what each one is, its settings
// (names, starting values and limits are Roblox's) and the sound processing itself.
//
// The processing is plain C++ on blocks of samples, so it doesn't need a sound device:
// Audio.cpp runs it inside the mixer (one Chain per playing sound and per SoundGroup),
// and render() runs it on a test tone for checks.
namespace SoundFx {

enum class Type { Reverb, Echo, Equalizer, Distortion, PitchShift, Chorus, Compressor, Flange, Tremolo };
inline constexpr int kTypeCount = 9;
inline const char* const kClassNames[kTypeCount] = {
    "ReverbSoundEffect", "EchoSoundEffect", "EqualizerSoundEffect", "DistortionSoundEffect", "PitchShiftSoundEffect",
    "ChorusSoundEffect", "CompressorSoundEffect", "FlangeSoundEffect", "TremoloSoundEffect"};
inline constexpr int kMostParams = 5;

struct Param { const char* name; float start, lo, hi; const char* tip; };
// The settings of one kind of effect (at most kMostParams), in the order they're stored.
const std::vector<Param>& params(Type t);
int  paramIndex(Type t, const char* name);   // -1 if it has no setting called that
Type typeFromClass(const char* className, bool& found);

// One effect, ready to use: its settings and which object it came from (so its echo or
// reverb tail carries on while you change a setting).
struct Settings {
    uint64_t id = 0;
    Type     type = Type::Reverb;
    float    v[kMostParams] = {0, 0, 0, 0, 0};
};
Settings defaults(Type t, uint64_t id = 0);

class Effect;
// A row of effects, applied in order, on interleaved float samples.
class Chain {
public:
    Chain(int sampleRate, int channels);
    ~Chain();
    // Change the effects (keeps the ones that are still there, with their sound in flight).
    void set(const std::vector<Settings>& fx);
    void process(float* samples, uint32_t frames);
    bool empty() const { return m_fx.empty(); }

private:
    int m_rate, m_channels;
    std::vector<std::pair<uint64_t, std::unique_ptr<Effect>>> m_fx;
};

// About how long these effects keep sounding after the sound itself has stopped (echoes, reverb).
float tailSeconds(const std::vector<Settings>& fx);
bool  same(const std::vector<Settings>& a, const std::vector<Settings>& b);

// Runs `fx` on a sine tone (mono, 44.1 kHz) and returns the output, for checks.
std::vector<float> render(const std::vector<Settings>& fx, float frequency, float seconds, float amplitude = 0.5f);

} // namespace SoundFx
