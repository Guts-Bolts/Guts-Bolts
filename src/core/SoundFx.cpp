#include "SoundFx.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace SoundFx {

namespace {

constexpr float kPi = 3.14159265358979f;

float dbToGain(float db) { return db <= -80.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }
float gainToDb(float g) { return g <= 1e-6f ? -120.0f : 20.0f * std::log10(g); }

// A circular delay line read at a fractional distance back.
struct DelayLine {
    std::vector<float> buf;
    size_t at = 0;
    void resize(size_t n) { buf.assign(std::max<size_t>(n, 4), 0.0f); at = 0; }
    void push(float v) { buf[at] = v; at = (at + 1) % buf.size(); }
    float back(float d) const {   // d samples ago (1 = the newest)
        d = std::clamp(d, 1.0f, (float)buf.size() - 2.0f);
        const float pos = (float)at - d + (float)buf.size();
        const size_t i0 = (size_t)pos % buf.size(), i1 = (i0 + 1) % buf.size();
        const float f = pos - std::floor(pos);
        return buf[i0] + (buf[i1] - buf[i0]) * f;
    }
    float backWhole(size_t d) const { return buf[(at + buf.size() - std::min(d, buf.size() - 1)) % buf.size()]; }
};

// A two-pole filter (Robert Bristow-Johnson's cookbook), one state per channel.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::vector<float> z1, z2;
    void channels(int n) { z1.assign(n, 0.0f); z2.assign(n, 0.0f); }
    void set(float nb0, float nb1, float nb2, float na0, float na1, float na2) {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void lowpass(float f, float rate, float q = 0.70710678f) {
        const float w = 2 * kPi * f / rate, c = std::cos(w), alpha = std::sin(w) / (2 * q);
        set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    void highpass(float f, float rate, float q = 0.70710678f) {
        const float w = 2 * kPi * f / rate, c = std::cos(w), alpha = std::sin(w) / (2 * q);
        set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    float run(int ch, float x) {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};

} // namespace

class Effect {
public:
    Effect(Type t, int rate, int channels) : type(t), m_rate((float)rate), m_ch(channels) {}
    virtual ~Effect() = default;
    virtual void set(const Settings& s) = 0;
    virtual void process(float* x, uint32_t frames) = 0;
    const Type type;

protected:
    float m_rate;
    int   m_ch;
};

namespace {

// --- Reverb: a Freeverb-style room (comb filters into all-pass filters, per channel). ---------
// DecayTime is how long the tail takes to die away (60 dB quieter), Density how many echoes
// make it up, Diffusion how smeared they are; DryLevel / WetLevel in decibels.
class Reverb : public Effect {
public:
    Reverb(int rate, int ch) : Effect(Type::Reverb, rate, ch) {
        static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int passes[4] = {556, 441, 341, 225};
        const float k = m_rate / 44100.0f;
        m_lanes.resize(ch);
        for (int c = 0; c < ch; ++c) {
            const int spread = c % 2 ? 23 : 0;   // left and right sound a little different (stereo)
            for (int i = 0; i < 8; ++i) { m_lanes[c].comb[i].resize((size_t)((combs[i] + spread) * k)); }
            for (int i = 0; i < 4; ++i) { m_lanes[c].pass[i].resize((size_t)((passes[i] + spread) * k)); }
        }
    }
    void set(const Settings& s) override {
        const float decay = std::clamp(s.v[0], 0.1f, 20.0f);
        m_combs = 2 + (int)std::lround(6.0f * std::clamp(s.v[1], 0.0f, 1.0f));
        m_diffuse = 0.15f + 0.55f * std::clamp(s.v[2], 0.0f, 1.0f);
        m_dry = dbToGain(s.v[3]);
        m_wet = dbToGain(s.v[4]);
        for (Lane& l : m_lanes)
            for (int i = 0; i < 8; ++i)
                l.feedback[i] = std::min(0.985f, std::pow(10.0f, -3.0f * (float)l.comb[i].buf.size() / (decay * m_rate)));
    }
    void process(float* x, uint32_t frames) override {
        const float in = 0.015f * 8.0f / (float)m_combs;
        for (uint32_t f = 0; f < frames; ++f) {
            float mono = 0.0f;
            for (int c = 0; c < m_ch; ++c) mono += x[f * m_ch + c];
            mono /= (float)m_ch;
            for (int c = 0; c < m_ch; ++c) {
                Lane& l = m_lanes[c];
                float out = 0.0f;
                for (int i = 0; i < m_combs; ++i) {
                    DelayLine& d = l.comb[i];
                    const float y = d.buf[d.at];
                    l.store[i] = y * (1.0f - kDamp) + l.store[i] * kDamp;
                    d.buf[d.at] = mono * in + l.store[i] * l.feedback[i];
                    d.at = (d.at + 1) % d.buf.size();
                    out += y;
                }
                for (DelayLine& p : l.pass) {
                    const float b = p.buf[p.at];
                    p.buf[p.at] = out + b * m_diffuse;
                    p.at = (p.at + 1) % p.buf.size();
                    out = b - out;
                }
                float& s = x[f * m_ch + c];
                s = s * m_dry + out * 3.0f * m_wet;
            }
        }
    }

private:
    static constexpr float kDamp = 0.25f;   // high sounds die away a little faster, like a real room
    struct Lane { DelayLine comb[8], pass[4]; float store[8] = {}; float feedback[8] = {}; };
    std::vector<Lane> m_lanes;
    int   m_combs = 8;
    float m_diffuse = 0.5f, m_dry = 1.0f, m_wet = 1.0f;
};

// --- Echo: the sound again after Delay seconds, Feedback of it again after that... --------
class Echo : public Effect {
public:
    Echo(int rate, int ch) : Effect(Type::Echo, rate, ch) {
        m_lines.resize(ch);
        for (DelayLine& d : m_lines) d.resize((size_t)(5.05f * m_rate));
    }
    void set(const Settings& s) override {
        m_delay = std::clamp(s.v[0], 0.01f, 5.0f) * m_rate;
        m_dry = dbToGain(s.v[1]);
        m_feedback = std::clamp(s.v[2], 0.0f, 0.995f);
        m_wet = dbToGain(s.v[3]);
    }
    void process(float* x, uint32_t frames) override {
        const size_t d = (size_t)m_delay;
        for (uint32_t f = 0; f < frames; ++f)
            for (int c = 0; c < m_ch; ++c) {
                float& s = x[f * m_ch + c];
                const float e = m_lines[c].backWhole(d);
                m_lines[c].push(s + e * m_feedback);
                s = s * m_dry + e * m_wet;
            }
    }

private:
    std::vector<DelayLine> m_lines;
    float m_delay = 44100.0f, m_dry = 1.0f, m_feedback = 0.5f, m_wet = 1.0f;
};

// --- Equalizer: three bands (below 400 Hz, 400-4000 Hz, above 4000 Hz), each louder or
// quieter in decibels. Linkwitz-Riley crossovers split the bands so they add back up to the
// whole sound (all at 0 dB changes nothing). ---------------------------------------------------
class Equalizer : public Effect {
public:
    Equalizer(int rate, int ch) : Effect(Type::Equalizer, rate, ch) {
        const float top = std::min(4000.0f, m_rate * 0.45f);
        for (int i = 0; i < 2; ++i) {
            for (Biquad* b : {&m_lo[i], &m_hi[i], &m_midLo[i], &m_midHi[i], &m_apLo[i], &m_apHi[i]}) b->channels(ch);
            m_lo[i].lowpass(400.0f, m_rate);
            m_hi[i].highpass(400.0f, m_rate);
            m_midLo[i].lowpass(top, m_rate);
            m_midHi[i].highpass(top, m_rate);
            m_apLo[i].lowpass(top, m_rate);    // (the low band goes through the 4000 Hz split too,
            m_apHi[i].highpass(top, m_rate);   //  so it stays in step with the other two)
        }
    }
    void set(const Settings& s) override {
        m_high = dbToGain(std::clamp(s.v[0], -80.0f, 10.0f));
        m_low  = dbToGain(std::clamp(s.v[1], -80.0f, 10.0f));
        m_mid  = dbToGain(std::clamp(s.v[2], -80.0f, 10.0f));
    }
    void process(float* x, uint32_t frames) override {
        if (m_low == 1.0f && m_mid == 1.0f && m_high == 1.0f) return;
        for (uint32_t f = 0; f < frames; ++f)
            for (int c = 0; c < m_ch; ++c) {
                float& s = x[f * m_ch + c];
                const float low = m_lo[1].run(c, m_lo[0].run(c, s));     // (two in a row: a steep split)
                const float rest = m_hi[1].run(c, m_hi[0].run(c, s));
                const float mid = m_midLo[1].run(c, m_midLo[0].run(c, rest));
                const float high = m_midHi[1].run(c, m_midHi[0].run(c, rest));
                const float lowInStep = m_apLo[1].run(c, m_apLo[0].run(c, low)) + m_apHi[1].run(c, m_apHi[0].run(c, low));
                s = lowInStep * m_low + mid * m_mid + high * m_high;
            }
    }

private:
    Biquad m_lo[2], m_hi[2], m_midLo[2], m_midHi[2], m_apLo[2], m_apHi[2];
    float  m_low = 1.0f, m_mid = 1.0f, m_high = 1.0f;
};

// --- Distortion: drives the sound into a soft clip (Level 0 = clean, 1 = fuzz). -------------
class Distortion : public Effect {
public:
    Distortion(int rate, int ch) : Effect(Type::Distortion, rate, ch) {}
    void set(const Settings& s) override {
        m_level = std::clamp(s.v[0], 0.0f, 1.0f);
        m_drive = 1.0f + 40.0f * m_level * m_level;
        m_norm = 1.0f / (std::tanh(m_drive) * (1.0f + m_level));
    }
    void process(float* x, uint32_t frames) override {
        if (m_level <= 0.001f) return;
        for (uint32_t i = 0; i < frames * (uint32_t)m_ch; ++i) x[i] = std::tanh(x[i] * m_drive) * m_norm;
    }

private:
    float m_level = 0.75f, m_drive = 1.0f, m_norm = 1.0f;
};

// --- PitchShift: higher or lower without changing the speed (Octave 2 = an octave up,
// 0.5 = an octave down). Two read heads sweep through a short delay, crossfading. -----------
class PitchShift : public Effect {
public:
    PitchShift(int rate, int ch) : Effect(Type::PitchShift, rate, ch) {
        m_window = 0.06f * m_rate;
        m_lines.resize(ch);
        for (DelayLine& d : m_lines) d.resize((size_t)(m_window * 2.0f) + 8);
    }
    void set(const Settings& s) override { m_ratio = std::clamp(s.v[0], 0.5f, 2.0f); }
    void process(float* x, uint32_t frames) override {
        const float step = (1.0f - m_ratio) / m_window;
        for (uint32_t f = 0; f < frames; ++f) {
            m_phase += step;
            m_phase -= std::floor(m_phase);
            const float p2 = m_phase + 0.5f - std::floor(m_phase + 0.5f);
            const float w1 = std::sin(kPi * m_phase), w2 = std::sin(kPi * p2);
            for (int c = 0; c < m_ch; ++c) {
                float& s = x[f * m_ch + c];
                m_lines[c].push(s);
                if (std::fabs(m_ratio - 1.0f) < 1e-4f) continue;
                s = m_lines[c].back(1.0f + m_phase * m_window) * w1 * w1 + m_lines[c].back(1.0f + p2 * m_window) * w2 * w2;
            }
        }
    }

private:
    std::vector<DelayLine> m_lines;
    float m_window = 2646.0f, m_ratio = 1.25f, m_phase = 0.0f;
};

// --- Chorus / Flange: the sound mixed with a copy delayed by a slowly wobbling amount. -------
// Chorus: 20 ms back, wobbling up to 10 ms (Depth) Rate times a second; Flange: much shorter
// (1-5 ms) and fed back into itself, for the sweeping "jet" sound.
class Modulated : public Effect {
public:
    Modulated(Type t, int rate, int ch) : Effect(t, rate, ch) {
        m_lines.resize(ch);
        for (DelayLine& d : m_lines) d.resize((size_t)(0.05f * m_rate));
    }
    void set(const Settings& s) override {
        m_depth = std::clamp(s.v[0], 0.0f, 1.0f);
        m_mix = std::clamp(s.v[1], 0.0f, 1.0f);
        m_lfo = std::clamp(s.v[2], 0.0f, 20.0f);
    }
    void process(float* x, uint32_t frames) override {
        const bool flange = type == Type::Flange;
        const float base = (flange ? 0.0005f : 0.020f) * m_rate;
        const float swing = (flange ? 0.005f : 0.010f) * m_depth * m_rate;
        const float feedback = flange ? 0.5f : 0.0f;
        for (uint32_t f = 0; f < frames; ++f) {
            m_phase += m_lfo / m_rate;
            m_phase -= std::floor(m_phase);
            for (int c = 0; c < m_ch; ++c) {
                const float wob = 0.5f + 0.5f * std::sin(2.0f * kPi * (m_phase + 0.25f * (float)c));   // (channels a little apart)
                float& s = x[f * m_ch + c];
                const float d = m_lines[c].back(base + swing * wob + 1.0f);
                m_lines[c].push(s + d * feedback);
                s = s * (1.0f - m_mix) + d * m_mix;
            }
        }
    }

private:
    std::vector<DelayLine> m_lines;
    float m_depth = 0.15f, m_mix = 0.5f, m_lfo = 0.5f, m_phase = 0.0f;
};

// --- Compressor: turns the sound down by Ratio when it's louder than Threshold (dB), reacting
// in Attack seconds and letting go in Release seconds, then GainMakeup (dB) louder. ---------
class Compressor : public Effect {
public:
    Compressor(int rate, int ch) : Effect(Type::Compressor, rate, ch) {}
    void set(const Settings& s) override {
        m_attack = std::exp(-1.0f / (std::clamp(s.v[0], 0.001f, 1.0f) * m_rate));
        m_makeup = std::clamp(s.v[1], 0.0f, 30.0f);
        m_ratio = std::clamp(s.v[2], 1.0f, 50.0f);
        m_release = std::exp(-1.0f / (std::clamp(s.v[3], 0.001f, 5.0f) * m_rate));
        m_threshold = std::clamp(s.v[4], -60.0f, 0.0f);
    }
    void process(float* x, uint32_t frames) override {
        for (uint32_t f = 0; f < frames; ++f) {
            float level = 0.0f;
            for (int c = 0; c < m_ch; ++c) level = std::max(level, std::fabs(x[f * m_ch + c]));
            const float k = level > m_env ? m_attack : m_release;
            m_env = level + (m_env - level) * k;
            const float over = gainToDb(m_env) - m_threshold;
            const float gain = dbToGain((over > 0.0f ? -over * (1.0f - 1.0f / m_ratio) : 0.0f) + m_makeup);
            for (int c = 0; c < m_ch; ++c) x[f * m_ch + c] *= gain;
        }
    }

private:
    float m_attack = 0.0f, m_release = 0.0f, m_makeup = 0.0f, m_ratio = 40.0f, m_threshold = -40.0f, m_env = 0.0f;
};

// --- Tremolo: the volume pulses Frequency times a second, down by Depth for the part of each
// pulse after Duty (smoothed a little, so it doesn't click). ---------------------------------
class Tremolo : public Effect {
public:
    Tremolo(int rate, int ch) : Effect(Type::Tremolo, rate, ch) { m_smooth = 1.0f - std::exp(-1.0f / (0.004f * m_rate)); }
    void set(const Settings& s) override {
        m_depth = std::clamp(s.v[0], 0.0f, 1.0f);
        m_duty = std::clamp(s.v[1], 0.0f, 1.0f);
        m_freq = std::clamp(s.v[2], 0.1f, 10.0f);
    }
    void process(float* x, uint32_t frames) override {
        for (uint32_t f = 0; f < frames; ++f) {
            m_phase += m_freq / m_rate;
            m_phase -= std::floor(m_phase);
            const float target = m_phase < m_duty ? 1.0f : 1.0f - m_depth;
            m_gain += (target - m_gain) * m_smooth;
            for (int c = 0; c < m_ch; ++c) x[f * m_ch + c] *= m_gain;
        }
    }

private:
    float m_depth = 1.0f, m_duty = 0.5f, m_freq = 5.0f, m_phase = 0.0f, m_gain = 1.0f, m_smooth = 0.01f;
};

std::unique_ptr<Effect> make(Type t, int rate, int ch) {
    switch (t) {
        case Type::Reverb:     return std::make_unique<Reverb>(rate, ch);
        case Type::Echo:       return std::make_unique<Echo>(rate, ch);
        case Type::Equalizer:  return std::make_unique<Equalizer>(rate, ch);
        case Type::Distortion: return std::make_unique<Distortion>(rate, ch);
        case Type::PitchShift: return std::make_unique<PitchShift>(rate, ch);
        case Type::Chorus:     return std::make_unique<Modulated>(Type::Chorus, rate, ch);
        case Type::Compressor: return std::make_unique<Compressor>(rate, ch);
        case Type::Flange:     return std::make_unique<Modulated>(Type::Flange, rate, ch);
        case Type::Tremolo:    return std::make_unique<Tremolo>(rate, ch);
    }
    return nullptr;
}

} // namespace

const std::vector<Param>& params(Type t) {
    static const std::vector<Param> table[kTypeCount] = {
        {{"DecayTime", 1.5f, 0.1f, 20.0f, "Seconds the echoes of the room take to die away."},
         {"Density", 1.0f, 0.0f, 1.0f, "How many echoes make up the tail (0 = a few, 1 = lots)."},
         {"Diffusion", 1.0f, 0.0f, 1.0f, "How smeared together the echoes are."},
         {"DryLevel", 0.0f, -80.0f, 20.0f, "How loud the plain sound is (decibels)."},
         {"WetLevel", 0.0f, -80.0f, 20.0f, "How loud the reverb is (decibels)."}},
        {{"Delay", 1.0f, 0.01f, 5.0f, "Seconds until the echo."},
         {"DryLevel", 0.0f, -80.0f, 10.0f, "How loud the plain sound is (decibels)."},
         {"Feedback", 0.5f, 0.0f, 1.0f, "How much of each echo echoes again."},
         {"WetLevel", 0.0f, -80.0f, 10.0f, "How loud the echoes are (decibels)."}},
        {{"HighGain", 0.0f, -80.0f, 10.0f, "Sounds above 4000 Hz: louder or quieter (decibels)."},
         {"LowGain", 0.0f, -80.0f, 10.0f, "Sounds below 400 Hz: louder or quieter (decibels)."},
         {"MidGain", 0.0f, -80.0f, 10.0f, "Sounds from 400 to 4000 Hz: louder or quieter (decibels)."}},
        {{"Level", 0.75f, 0.0f, 1.0f, "How distorted (0 = clean)."}},
        {{"Octave", 1.25f, 0.5f, 2.0f, "Pitch change: 2 = an octave higher, 0.5 = an octave lower."}},
        {{"Depth", 0.15f, 0.0f, 1.0f, "How far the copies wobble."},
         {"Mix", 0.5f, 0.0f, 1.0f, "How much of the chorus you hear (0 = none)."},
         {"Rate", 0.5f, 0.0f, 20.0f, "Wobbles a second."}},
        {{"Attack", 0.1f, 0.001f, 1.0f, "Seconds it takes to turn a loud sound down."},
         {"GainMakeup", 0.0f, 0.0f, 30.0f, "Decibels louder afterwards."},
         {"Ratio", 40.0f, 1.0f, 50.0f, "How hard loud sounds are turned down."},
         {"Release", 0.1f, 0.001f, 5.0f, "Seconds it takes to let go again."},
         {"Threshold", -40.0f, -60.0f, 0.0f, "Sounds louder than this (decibels) are turned down."}},
        {{"Depth", 0.45f, 0.01f, 1.0f, "How far the sweep goes."},
         {"Mix", 0.85f, 0.0f, 1.0f, "How much of the flange you hear (0 = none)."},
         {"Rate", 5.0f, 0.0f, 20.0f, "Sweeps a second."}},
        {{"Depth", 1.0f, 0.0f, 1.0f, "How far the volume dips (1 = to silence)."},
         {"Duty", 0.5f, 0.0f, 1.0f, "How much of each pulse is at full volume."},
         {"Frequency", 5.0f, 0.1f, 10.0f, "Pulses a second."}},
    };
    return table[std::clamp((int)t, 0, kTypeCount - 1)];
}

int paramIndex(Type t, const char* name) {
    const auto& p = params(t);
    for (size_t i = 0; i < p.size(); ++i)
        if (std::strcmp(p[i].name, name) == 0) return (int)i;
    return -1;
}

Type typeFromClass(const char* cls, bool& found) {
    for (int i = 0; i < kTypeCount; ++i)
        if (std::strcmp(kClassNames[i], cls) == 0) { found = true; return (Type)i; }
    found = false;
    return Type::Reverb;
}

Settings defaults(Type t, uint64_t id) {
    Settings s;
    s.id = id;
    s.type = t;
    const auto& p = params(t);
    for (size_t i = 0; i < p.size(); ++i) s.v[i] = p[i].start;
    return s;
}

Chain::Chain(int sampleRate, int channels) : m_rate(std::max(8000, sampleRate)), m_channels(std::max(1, channels)) {}
Chain::~Chain() = default;

void Chain::set(const std::vector<Settings>& fx) {
    std::vector<std::pair<uint64_t, std::unique_ptr<Effect>>> next;
    next.reserve(fx.size());
    for (const Settings& s : fx) {
        std::unique_ptr<Effect> e;
        for (auto& [id, old] : m_fx)
            if (old && id == s.id && old->type == s.type) { e = std::move(old); break; }
        if (!e) e = make(s.type, m_rate, m_channels);
        if (!e) continue;
        e->set(s);
        next.emplace_back(s.id, std::move(e));
    }
    m_fx = std::move(next);
}

void Chain::process(float* samples, uint32_t frames) {
    for (auto& [id, e] : m_fx) e->process(samples, frames);
    // (A runaway feedback setting must never blast the speakers.)
    for (uint32_t i = 0; i < frames * (uint32_t)m_channels; ++i) {
        float& s = samples[i];
        if (!std::isfinite(s)) s = 0.0f;
        else s = std::clamp(s, -4.0f, 4.0f);
    }
}

float tailSeconds(const std::vector<Settings>& fx) {
    float tail = 0.0f;
    for (const Settings& s : fx) {
        if (s.type == Type::Reverb) tail += std::clamp(s.v[0], 0.1f, 20.0f);
        else if (s.type == Type::Echo) {
            const float fb = std::clamp(s.v[2], 0.0f, 0.995f), delay = std::clamp(s.v[0], 0.01f, 5.0f);
            // Echoes until they're 60 dB down (each one is `fb` of the last).
            tail += delay * (fb > 0.001f ? 1.0f + std::log(0.001f) / std::log(fb) : 1.0f);
        } else tail += 0.1f;
    }
    return std::min(tail, 20.0f);
}

bool same(const std::vector<Settings>& a, const std::vector<Settings>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].id != b[i].id || a[i].type != b[i].type || std::memcmp(a[i].v, b[i].v, sizeof(a[i].v)) != 0) return false;
    return true;
}

std::vector<float> render(const std::vector<Settings>& fx, float frequency, float seconds, float amplitude) {
    constexpr int kRate = 44100;
    Chain chain(kRate, 1);
    chain.set(fx);
    std::vector<float> out((size_t)(seconds * kRate));
    for (size_t i = 0; i < out.size(); ++i) out[i] = amplitude * std::sin(2.0f * kPi * frequency * (float)i / kRate);
    for (size_t at = 0; at < out.size(); at += 512)
        chain.process(out.data() + at, (uint32_t)std::min<size_t>(512, out.size() - at));
    return out;
}

} // namespace SoundFx
