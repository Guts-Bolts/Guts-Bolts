// stb_vorbis lets miniaudio play .ogg sounds (its body is at the bottom of this file).
#define STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include <miniaudio.h>

#include "Audio.h"
#include <atomic>
#include "Voice.h"
#include "SoundJump.h"      // generated from assets/sounds
#include "SoundRespawn.h"
#include "SoundSpawn.h"
#include "SoundFootsteps.h"
#include "RealSounds.h"     // recorded effects from assets/sounds/real (CC0)
#include "Paths.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <random>

namespace Audio {

namespace {

constexpr int   kRate = 44100;
constexpr float kPi   = 3.14159265f;

using Samples = std::vector<float>;

// ---------------------------------------------------------------------------
// Sound synthesis: every built-in effect is made from sine / square / noise.
// ---------------------------------------------------------------------------

std::mt19937& rng() { static std::mt19937 r{77u}; return r; }
float noise() { return std::uniform_real_distribution<float>(-1.0f, 1.0f)(rng()); }

Samples make(float seconds) { return Samples((size_t)(seconds * kRate), 0.0f); }

float env(float t, float attack, float length) {
    if (t < attack) return t / attack;
    float k = 1.0f - (t - attack) / std::max(1e-4f, length - attack);
    return std::max(0.0f, k * k);
}

// A tone that slides from f0 to f1. shape: 0 sine, 1 square, 2 saw.
void tone(Samples& s, float start, float len, float f0, float f1, int shape, float amp) {
    float phase = 0.0f;
    size_t a = (size_t)(start * kRate), n = (size_t)(len * kRate);
    for (size_t i = 0; i < n && a + i < s.size(); ++i) {
        float t = (float)i / kRate;
        float f = f0 + (f1 - f0) * (t / len);
        phase += f / kRate;
        phase -= std::floor(phase);
        float v = shape == 0 ? std::sin(phase * 2 * kPi)
                : shape == 1 ? (phase < 0.5f ? 1.0f : -1.0f) * 0.6f
                             : (phase * 2.0f - 1.0f) * 0.7f;
        s[a + i] += v * amp * env(t, 0.005f, len);
    }
}

// Noise through a sweeping low-pass filter.
void rumble(Samples& s, float start, float len, float cutoff0, float cutoff1, float amp) {
    float lp = 0.0f;
    size_t a = (size_t)(start * kRate), n = (size_t)(len * kRate);
    for (size_t i = 0; i < n && a + i < s.size(); ++i) {
        float t = (float)i / kRate;
        float fc = cutoff0 + (cutoff1 - cutoff0) * (t / len);
        float k = 1.0f - std::exp(-2.0f * kPi * fc / kRate);
        lp += (noise() - lp) * k;
        s[a + i] += lp * amp * env(t, 0.004f, len);
    }
}

// Band-pass "formant" filter, for the voice-like oof.
Samples formant(const Samples& in, float freq, float q) {
    Samples out(in.size());
    float w = 2 * kPi * freq / kRate, alpha = std::sin(w) / (2 * q);
    float b0 = alpha, b2 = -alpha, a0 = 1 + alpha, a1 = -2 * std::cos(w), a2 = 1 - alpha;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        float y = (b0 * in[i] + b2 * x2 - a1 * y1 - a2 * y2) / a0;
        x2 = x1; x1 = in[i]; y2 = y1; y1 = y;
        out[i] = y;
    }
    return out;
}

void normalize(Samples& s, float peak = 0.9f) {
    float m = 1e-6f;
    for (float v : s) m = std::max(m, std::abs(v));
    for (float& v : s) v *= peak / m;
}

std::map<std::string, Samples> synthesize() {
    std::map<std::string, Samples> out;

    { Samples s = make(0.18f); tone(s, 0, 0.18f, 320, 760, 1, 0.8f); out["jump"] = s; }
    { Samples s = make(0.45f); tone(s, 0, 0.08f, 988, 988, 1, 0.7f); tone(s, 0.08f, 0.37f, 1319, 1319, 1, 0.7f); out["coin"] = s; }
    {   // "Oof": a falling buzzy voice through two mouth-like formants.
        Samples src = make(0.42f);
        tone(src, 0, 0.42f, 185, 95, 2, 1.0f);
        Samples a = formant(src, 520, 4.0f), b = formant(src, 900, 5.0f);
        Samples s(src.size());
        for (size_t i = 0; i < s.size(); ++i) s[i] = a[i] + b[i] * 0.6f + src[i] * 0.08f;
        normalize(s, 0.85f);
        out["oof"] = s;
    }
    { Samples s = make(1.6f); rumble(s, 0, 1.6f, 2400, 90, 1.0f); tone(s, 0, 1.2f, 70, 35, 0, 0.6f); normalize(s); out["explosion"] = s; }
    { Samples s = make(0.16f); rumble(s, 0, 0.16f, 1800, 300, 1.0f); tone(s, 0, 0.1f, 160, 70, 0, 0.5f); normalize(s, 0.7f); out["splat"] = s; }
    { Samples s = make(0.03f); rumble(s, 0, 0.03f, 6000, 3000, 1.0f); normalize(s, 0.5f); out["click"] = s; }
    { Samples s = make(0.2f); tone(s, 0, 0.2f, 140, 60, 0, 0.9f); rumble(s, 0, 0.08f, 3000, 500, 0.5f); normalize(s, 0.8f); out["hit"] = s; }
    // A gun: a sharp crack with a short low thump, and the clack of a new magazine.
    { Samples s = make(0.35f); rumble(s, 0, 0.35f, 7000, 400, 1.0f); tone(s, 0, 0.12f, 180, 50, 0, 0.7f); normalize(s, 0.8f); out["gunshot"] = s; }
    { Samples s = make(0.4f); rumble(s, 0, 0.04f, 5000, 2500, 0.8f); rumble(s, 0.22f, 0.06f, 4000, 1500, 1.0f); normalize(s, 0.6f); out["reload"] = s; }
    {   Samples s = make(0.9f);
        const float notes[] = {523.25f, 659.25f, 783.99f, 1046.5f};
        for (int i = 0; i < 4; ++i) tone(s, i * 0.12f, i == 3 ? 0.5f : 0.14f, notes[i], notes[i], 1, 0.6f);
        out["win"] = s; }
    {   Samples s = make(0.5f);
        float phase = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            float t = (float)i / kRate;
            float f = 180 + 520 * std::sin(std::min(1.0f, t / 0.5f) * kPi) + 25 * std::sin(t * 60);
            phase += f / kRate;
            s[i] = std::sin(phase * 2 * kPi) * env(t, 0.01f, 0.5f) * 0.8f;
        }
        out["boing"] = s; }
    {   Samples s = make(0.7f);
        for (int i = 0; i < 6; ++i) tone(s, i * 0.07f, 0.3f, 600.0f * std::pow(1.26f, (float)i), 600.0f * std::pow(1.26f, (float)i), 0, 0.35f);
        out["spawn"] = s; }
    {   // Splash: a whoosh of water plus a few bubbly plops.
        Samples s = make(0.8f);
        rumble(s, 0, 0.45f, 6000, 700, 1.0f);
        rumble(s, 0.03f, 0.7f, 1400, 250, 0.5f);
        for (int i = 0; i < 7; ++i) {
            float at = 0.08f + i * 0.07f + noise() * 0.02f, f = 500.0f + (noise() * 0.5f + 0.5f) * 700.0f;
            tone(s, at, 0.07f, f, f * 1.8f, 0, 0.22f);
        }
        normalize(s, 0.75f);
        out["splash"] = s; }
    return out;
}

// ---------------------------------------------------------------------------

// Effects in the mixer: a node of our own that runs a SoundFx::Chain. One goes after a
// sound that has effects, and one is each SoundGroup's bus (sounds in the group mix into it).
struct FxNode {
    ma_node_base base;                        // (first: miniaudio treats this as an ma_node)
    std::mutex   lock;
    std::unique_ptr<SoundFx::Chain> chain;
    std::vector<SoundFx::Settings>  settings;
    bool         ready = false;
    std::atomic<float> level{0.0f};           // how loud the last bit that went through was (RMS, 0..1)
};

void fxProcess(ma_node* node, const float** in, ma_uint32* inCount, float** out, ma_uint32* outCount) {
    FxNode* n = (FxNode*)node;
    const ma_uint32 frames = *outCount, ch = ma_node_get_output_channels(node, 0);
    if (in && in[0]) std::memcpy(out[0], in[0], sizeof(float) * frames * ch);
    else std::memset(out[0], 0, sizeof(float) * frames * ch);
    if (inCount) *inCount = frames;
    // Never wait in the mixer: if the settings are being changed right now, pass this bit through.
    std::unique_lock<std::mutex> hold(n->lock, std::try_to_lock);
    if (hold.owns_lock() && n->chain) n->chain->process(out[0], frames);
    // The loudness meter (Sound.PlaybackLoudness, lip sync).
    double sum = 0.0;
    const ma_uint32 count = frames * ch;
    for (ma_uint32 i = 0; i < count; ++i) sum += (double)out[0][i] * out[0][i];
    n->level.store(count ? (float)std::sqrt(sum / count) : 0.0f, std::memory_order_relaxed);
}

// Keeps going with nothing attached, so an echo or reverb can ring on after its sounds stop.
ma_node_vtable g_fxVtable = {fxProcess, nullptr, 1, 1, MA_NODE_FLAG_CONTINUOUS_PROCESSING | MA_NODE_FLAG_ALLOW_NULL_INPUT};

struct Instance {
    int               handle;
    ma_sound          sound;
    ma_audio_buffer   buffer;
    bool              usesBuffer = false;
    std::unique_ptr<FxNode> fx;               // its own effects (null = none yet)
    uint64_t          route = 0;              // the SoundGroup bus it goes to (0 = straight out)
    float             tail = 0.0f;            // seconds its effects ring on after it ends
    bool              positional = false;     // somewhere in the world (the room's echo is on it)
    float             volume = 1.0f;          // as asked (before walls muffle it)
    float             occluded = 0.0f;        // 0..1: how much is between it and the listener
    std::vector<SoundFx::Settings> userFx;    // its own effects (the muffling goes after them)
    double            endedAt = -1.0;
};

struct Bus {
    std::unique_ptr<FxNode> node;
    uint64_t parent = 0;
};

bool                                    g_ready = false;
ma_engine                               g_engine;
std::map<uint64_t, std::unique_ptr<Bus>> g_buses;
std::map<std::string, Samples>          g_builtin;
std::map<int, std::unique_ptr<Instance>> g_playing;

// Recorded effects baked into the program (assets/sounds): decoded once at start.
Samples decode(const unsigned char* data, size_t size) {
    Samples out;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, kRate);
    ma_decoder dec;
    if (ma_decoder_init_memory(data, size, &cfg, &dec) != MA_SUCCESS) return out;
    float chunk[4096];
    ma_uint64 got = 0;
    while (ma_decoder_read_pcm_frames(&dec, chunk, 4096, &got) == MA_SUCCESS && got > 0)
        out.insert(out.end(), chunk, chunk + got);
    ma_decoder_uninit(&dec);
    // Even out the loudness (so a quiet recording isn't lost next to the made-up sounds).
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    if (peak > 0.01f) for (float& v : out) v *= 0.8f / peak;
    return out;
}
// Real recordings come in takes ("hit_metal_1", "hit_metal_2"...): asking for "hit_metal"
// picks one of them at random, so the same hit doesn't sound the same every time.
std::map<std::string, std::vector<std::string>> g_takes;
int                                     g_next = 1;
float                                   g_masterVolume = 1.0f;
bool                                    g_suspended = false;

void destroy(Instance& in) {
    ma_sound_uninit(&in.sound);
    if (in.usesBuffer) ma_audio_buffer_uninit(&in.buffer);
    if (in.fx && in.fx->ready) ma_node_uninit(&in.fx->base, nullptr);
    in.fx.reset();
}

bool makeFxNode(FxNode& n) {
    ma_uint32 ch = ma_engine_get_channels(&g_engine);
    ma_node_config cfg = ma_node_config_init();
    cfg.vtable = &g_fxVtable;
    cfg.pInputChannels = &ch;
    cfg.pOutputChannels = &ch;
    if (ma_node_init(ma_engine_get_node_graph(&g_engine), &cfg, nullptr, &n.base) != MA_SUCCESS) return false;
    n.chain = std::make_unique<SoundFx::Chain>((int)ma_engine_get_sample_rate(&g_engine), (int)ch);
    n.ready = true;
    return true;
}

void setChain(FxNode& n, const std::vector<SoundFx::Settings>& fx) {
    if (SoundFx::same(n.settings, fx)) return;
    std::lock_guard<std::mutex> hold(n.lock);
    n.chain->set(fx);
    n.settings = fx;
}

// The room you're in (Acoustics.h): one reverb every sound in the world goes through.
std::unique_ptr<FxNode> g_room;
float g_roomWet = 0.0f;

// Where something's sound goes next: a SoundGroup's bus, or straight out.
ma_node* target(uint64_t bus) {
    auto it = g_buses.find(bus);
    return it != g_buses.end() && it->second->node->ready ? (ma_node*)&it->second->node->base : ma_engine_get_endpoint(&g_engine);
}

void reroute(Instance& in) {
    ma_node* to = target(in.route);
    if (in.route == 0 && in.positional && g_room && g_room->ready && g_roomWet > 0.0f) to = (ma_node*)&g_room->base;
    if (in.fx && in.fx->ready) {
        ma_node_attach_output_bus(&in.sound, 0, &in.fx->base, 0);
        ma_node_attach_output_bus(&in.fx->base, 0, to, 0);
    } else {
        ma_node_attach_output_bus(&in.sound, 0, to, 0);
    }
}

double clockNow() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Instance* find(int handle) {
    auto it = g_playing.find(handle);
    return it == g_playing.end() ? nullptr : it->second.get();
}

} // namespace

std::string takeBase(const std::string& name) {
    const size_t u = name.rfind('_');
    if (u == std::string::npos || u + 1 >= name.size()) return name;
    for (size_t i = u + 1; i < name.size(); ++i) if (!std::isdigit((unsigned char)name[i])) return name;
    return name.substr(0, u);
}

std::vector<std::string> realSoundNames() {
    std::vector<std::string> out;
    for (const RealSound& r : kRealSounds) {
        const std::string b = takeBase(r.name);
        if (std::find(out.begin(), out.end(), b) == out.end()) out.push_back(b);
    }
    return out;
}

const std::vector<std::string>& builtinNames() {
    static const std::vector<std::string> names = {
        "jump", "coin", "oof", "explosion", "splat", "click", "hit", "win", "boing", "spawn", "respawn", "footsteps", "splash", "gunshot", "reload"};
    static const std::vector<std::string> all = [] {
        std::vector<std::string> v = names;
        for (const std::string& r : realSoundNames()) if (std::find(v.begin(), v.end(), r) == v.end()) v.push_back(r);
        return v;
    }();
    return all;
}

void init() {
    if (g_ready) return;
    ma_engine_config cfg = ma_engine_config_init();
    if (ma_engine_init(&cfg, &g_engine) != MA_SUCCESS) {
        Log::system("No sound device found - playing without sound.");
        return;
    }
    g_builtin = synthesize();
    // The recorded ones win over the made-up ones with the same name.
    struct Recorded { const char* name; const unsigned char* data; size_t size; };
    for (const Recorded& r : {Recorded{"jump", kSoundJump, kSoundJumpSize},
                              Recorded{"respawn", kSoundRespawn, kSoundRespawnSize},
                              Recorded{"spawn", kSoundSpawn, kSoundSpawnSize},
                              Recorded{"footsteps", kSoundFootsteps, kSoundFootstepsSize}})
        if (Samples s = decode(r.data, r.size); !s.empty()) g_builtin[r.name] = std::move(s);
    for (const RealSound& r : kRealSounds) {
        Samples s = decode(r.data, r.size);
        if (s.empty()) continue;
        g_builtin[r.name] = std::move(s);
        g_takes[takeBase(r.name)].push_back(r.name);
    }
    g_ready = true;
}

void shutdown() {
    Voice::shutdown();   // (its sounds use the engine)
    if (!g_ready) return;
    stopAll();
    keepBuses({});
    ma_engine_uninit(&g_engine);
    g_ready = false;
}

void update() {
    if (!g_ready) return;
    for (auto it = g_playing.begin(); it != g_playing.end();) {
        Instance& in = *it->second;
        if (!ma_sound_is_looping(&in.sound) && ma_sound_at_end(&in.sound)) {
            // With an echo or reverb of its own, it rings on for a bit before it's let go.
            if (in.fx && in.tail > 0.0f) {
                if (in.endedAt < 0.0) in.endedAt = clockNow();
                if (clockNow() - in.endedAt < in.tail) { ++it; continue; }
            }
            destroy(in);
            it = g_playing.erase(it);
        } else {
            ++it;
        }
    }
}

int play(const std::string& soundId, float volume, float pitch, bool loop, const glm::vec3* position) {
    if (!loop && volume > 0.05f) caption(builtinCaption(soundId), position);   // (even with no sound device)
    if (!g_ready || soundId.empty()) return 0;
    if (g_playing.size() > 96) return 0;   // don't let a runaway script flood the mixer

    auto in = std::make_unique<Instance>();
    in->handle = g_next++;
    ma_uint32 flags = position ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;

    auto takes = g_takes.find(soundId);
    auto builtin = takes != g_takes.end() && !takes->second.empty()
                 ? g_builtin.find(takes->second[(size_t)g_next % takes->second.size()])
                 : g_builtin.find(soundId);
    if (builtin != g_builtin.end()) {
        const Samples& s = builtin->second;
        ma_audio_buffer_config bc = ma_audio_buffer_config_init(ma_format_f32, 1, s.size(), s.data(), nullptr);
        bc.sampleRate = kRate;
        if (ma_audio_buffer_init(&bc, &in->buffer) != MA_SUCCESS) return 0;
        in->usesBuffer = true;
        if (ma_sound_init_from_data_source(&g_engine, &in->buffer, flags, nullptr, &in->sound) != MA_SUCCESS) {
            ma_audio_buffer_uninit(&in->buffer);
            return 0;
        }
    } else {
        // A file: "gb:<id>" is audio from a Guts&Bolts server (downloaded when the
        // game loads); otherwise look in the games folder first, then treat it as a full path.
        std::filesystem::path p;
        std::error_code ec;
        if (soundId.rfind("gb:", 0) == 0) {
            p = Paths::downloaded(soundId.substr(3));
            if (p.empty()) return 0;   // still downloading
        } else {
            p = Paths::gamesFolder() / soundId;
            if (!std::filesystem::exists(p, ec)) p = soundId;
        }
        if (ma_sound_init_from_file(&g_engine, p.string().c_str(), flags | MA_SOUND_FLAG_STREAM, nullptr, nullptr,
                                    &in->sound) != MA_SUCCESS) {
            Log::warn("Couldn't play sound \"" + soundId + "\" (use a built-in name or a .wav / .mp3 / .ogg / .flac file)");
            return 0;
        }
    }

    ma_sound_set_volume(&in->sound, std::max(0.0f, volume));
    in->volume = std::max(0.0f, volume);
    in->positional = position != nullptr;
    ma_sound_set_pitch(&in->sound, std::max(0.05f, pitch));
    ma_sound_set_looping(&in->sound, loop);
    if (position) {
        ma_sound_set_position(&in->sound, position->x, position->y, position->z);
        ma_sound_set_min_distance(&in->sound, 6.0f);
        ma_sound_set_max_distance(&in->sound, 120.0f);
    }
    ma_sound_start(&in->sound);
    int h = in->handle;
    g_playing[h] = std::move(in);
    return h;
}

void stop(int handle) {
    auto it = g_playing.find(handle);
    if (it == g_playing.end()) return;
    destroy(*it->second);
    g_playing.erase(it);
}

void stopAll() {
    for (auto& [h, in] : g_playing) destroy(*in);
    g_playing.clear();
}

bool isPlaying(int handle) {
    Instance* in = find(handle);
    return in && ma_sound_is_playing(&in->sound);
}

namespace {
// Its own effects, then (behind a wall) the muffling: the highs go first, like real walls.
void applyChain(Instance& in) {
    std::vector<SoundFx::Settings> fx = in.userFx;
    if (in.occluded > 0.02f) {
        SoundFx::Settings eq = SoundFx::defaults(SoundFx::Type::Equalizer, ~0ull - 7);
        eq.v[0] = -26.0f * in.occluded;   // HighGain
        eq.v[2] = -9.0f * in.occluded;    // MidGain
        fx.push_back(eq);
    }
    if (fx.empty() && !in.fx) return;
    if (!in.fx) {
        in.fx = std::make_unique<FxNode>();
        if (!makeFxNode(*in.fx)) { in.fx.reset(); return; }
        setChain(*in.fx, fx);
        reroute(in);
    } else {
        setChain(*in.fx, fx);
    }
    in.tail = SoundFx::tailSeconds(in.userFx);
}
} // namespace

void setEffects(int handle, const std::vector<SoundFx::Settings>& fx) {
    Instance* in = find(handle);
    if (!in) return;
    in->userFx = fx;
    applyChain(*in);
}

void setOcclusion(int handle, float amount) {
    Instance* in = find(handle);
    if (!in) return;
    amount = std::clamp(amount, 0.0f, 1.0f);
    if (std::abs(amount - in->occluded) < 0.03f && !(amount == 0.0f && in->occluded > 0.0f)) return;
    in->occluded = amount;
    ma_sound_set_volume(&in->sound, in->volume * (1.0f - 0.55f * amount));
    applyChain(*in);
}

float occlusion(int handle) {
    Instance* in = find(handle);
    return in ? in->occluded : 0.0f;
}

void setVelocity(int handle, const glm::vec3& v) {
    if (Instance* in = find(handle)) ma_sound_set_velocity(&in->sound, v.x, v.y, v.z);
}

void setListenerVelocity(const glm::vec3& v) {
    if (g_ready) ma_engine_listener_set_velocity(&g_engine, 0, v.x, v.y, v.z);
}

void setRoom(float decaySeconds, float wet) {
    if (!g_ready) return;
    wet = std::clamp(wet, 0.0f, 1.0f);
    const bool wasOn = g_room && g_roomWet > 0.0f;
    if (!g_room && wet > 0.0f) {
        g_room = std::make_unique<FxNode>();
        if (!makeFxNode(*g_room)) { g_room.reset(); return; }
        ma_node_attach_output_bus(&g_room->base, 0, ma_engine_get_endpoint(&g_engine), 0);
    }
    if (g_room) {
        SoundFx::Settings r = SoundFx::defaults(SoundFx::Type::Reverb, ~0ull - 8);
        r.v[0] = std::clamp(decaySeconds, 0.1f, 8.0f);   // DecayTime
        r.v[3] = 0.0f;                                    // DryLevel: the sound itself, as loud as ever
        r.v[4] = wet > 0.0f ? -30.0f + 24.0f * wet : -80.0f;   // WetLevel: -30 dB (hardly) .. -6 dB (a cathedral)
        setChain(*g_room, {r});
    }
    g_roomWet = wet;
    if (wasOn != (wet > 0.0f))
        for (auto& [h, in] : g_playing) if (in->positional) reroute(*in);
}

float roomWet() { return g_roomWet; }

bool hasTakes(const std::string& name) { return g_takes.count(name) > 0; }

float loudness(int handle) {
    Instance* in = find(handle);
    if (!in || !ma_sound_is_playing(&in->sound)) return 0.0f;
    if (!in->fx) {
        // Nothing measures it yet: put an empty effects node after it (it just listens).
        in->fx = std::make_unique<FxNode>();
        if (!makeFxNode(*in->fx)) { in->fx.reset(); return 0.0f; }
        reroute(*in);
        return 0.0f;
    }
    return in->fx->level.load(std::memory_order_relaxed);
}

float loudnessOf(const float* samples, size_t count) {
    if (!samples || !count) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) sum += (double)samples[i] * samples[i];
    return (float)std::sqrt(sum / (double)count);
}

void setRoute(int handle, uint64_t bus) {
    Instance* in = find(handle);
    if (!in || in->route == bus) return;
    in->route = bus;
    reroute(*in);
}

void setBus(uint64_t id, uint64_t parent, const std::vector<SoundFx::Settings>& fx) {
    if (!g_ready || id == 0) return;
    auto& b = g_buses[id];
    bool moved = false;
    if (!b) {
        b = std::make_unique<Bus>();
        b->node = std::make_unique<FxNode>();
        if (!makeFxNode(*b->node)) { g_buses.erase(id); return; }
        moved = true;
    }
    setChain(*b->node, fx);
    if (moved || b->parent != parent) {
        b->parent = parent == id ? 0 : parent;
        ma_node_attach_output_bus(&b->node->base, 0, target(b->parent), 0);
    }
}

void keepBuses(const std::set<uint64_t>& used) {
    std::vector<uint64_t> gone;
    for (auto& [id, b] : g_buses) if (!used.count(id)) gone.push_back(id);
    if (gone.empty()) return;
    // Whatever went into them goes straight out (or to what's left) from now on.
    for (uint64_t id : gone) {
        for (auto& [h, in] : g_playing) if (in->route == id) { in->route = 0; reroute(*in); }
        for (auto& [other, b] : g_buses)
            if (b->parent == id && !std::count(gone.begin(), gone.end(), other)) {
                b->parent = 0;
                ma_node_attach_output_bus(&b->node->base, 0, ma_engine_get_endpoint(&g_engine), 0);
            }
    }
    for (uint64_t id : gone) {
        auto it = g_buses.find(id);
        if (it->second->node->ready) ma_node_uninit(&it->second->node->base, nullptr);
        g_buses.erase(it);
    }
}

void setVolume(int handle, float v) {
    if (Instance* in = find(handle)) {
        in->volume = std::max(0.0f, v);
        ma_sound_set_volume(&in->sound, in->volume * (1.0f - 0.55f * in->occluded));
    }
}
void seek(int handle, double seconds) {
    Instance* in = find(handle);
    if (!in) return;
    ma_uint32 rate = 0;
    if (ma_sound_get_data_format(&in->sound, nullptr, nullptr, &rate, nullptr, 0) != MA_SUCCESS || !rate) return;
    ma_sound_seek_to_pcm_frame(&in->sound, (ma_uint64)(std::max(0.0, seconds) * rate));
}
void setPitch(int handle, float p)  { if (Instance* in = find(handle)) ma_sound_set_pitch(&in->sound, std::max(0.05f, p)); }
void setPosition(int handle, const glm::vec3& p) {
    if (Instance* in = find(handle)) ma_sound_set_position(&in->sound, p.x, p.y, p.z);
}

namespace {
std::vector<Caption> g_captions;
glm::vec3 g_listenerAt{0.0f}, g_listenerDir{0.0f, 0.0f, -1.0f};
} // namespace

double captionClock() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string builtinCaption(const std::string& id) {
    static const std::pair<const char*, const char*> words[] = {
        {"jump", "Jump"}, {"coin", "Coin"}, {"oof", "Oof!"}, {"explosion", "Explosion"}, {"splat", "Splat"}, {"hit", "Hit"},
        {"win", "Win!"}, {"boing", "Boing"}, {"spawn", "Spawn"}, {"respawn", "Respawn"}, {"splash", "Splash"}, {"thunder", "Thunder"}};
    for (const auto& [k, w] : words) if (id == k) return w;
    return "";
}

void caption(const std::string& text, const glm::vec3* position) {
    if (text.empty()) return;
    const double now = captionClock();
    int side = 0;
    if (position) {
        const glm::vec3 to = *position - g_listenerAt;
        const glm::vec3 right = glm::cross(g_listenerDir, glm::vec3(0.0f, 1.0f, 0.0f));
        const float len = glm::length(to), sideways = glm::length(right) > 0.001f && len > 0.001f ? glm::dot(to / len, glm::normalize(right)) : 0.0f;
        side = sideways > 0.4f ? 1 : sideways < -0.4f ? -1 : 0;
    }
    for (Caption& c : g_captions)   // the same sound again: count it instead of a new line
        if (c.text == text && now - c.at < 1.0 && c.side == side) { c.at = now; ++c.count; return; }
    g_captions.push_back({text, now, side, 1});
    if (g_captions.size() > 32) g_captions.erase(g_captions.begin());
}

std::vector<Caption> captions(double keepSeconds) {
    const double now = captionClock();
    g_captions.erase(std::remove_if(g_captions.begin(), g_captions.end(), [&](const Caption& c) { return now - c.at > 10.0; }), g_captions.end());
    std::vector<Caption> out;
    for (const Caption& c : g_captions) if (now - c.at <= keepSeconds) out.push_back(c);
    return out;
}

glm::vec3 listenerPosition() { return g_listenerAt; }

void setListener(const glm::vec3& p, const glm::vec3& f) {
    g_listenerAt = p;
    g_listenerDir = f;
    if (!g_ready) return;
    ma_engine_listener_set_position(&g_engine, 0, p.x, p.y, p.z);
    ma_engine_listener_set_direction(&g_engine, 0, f.x, f.y, f.z);
}

void* engineHandle() { return g_ready ? &g_engine : nullptr; }

void setMasterVolume(float v) {
    g_masterVolume = std::clamp(v, 0.0f, 1.0f);
    if (g_ready) ma_engine_set_volume(&g_engine, g_suspended ? 0.0f : g_masterVolume);
}

void suspend() {
    g_suspended = true;
    if (g_ready) ma_engine_set_volume(&g_engine, 0.0f);
}

void resume() {
    g_suspended = false;
    if (g_ready) ma_engine_set_volume(&g_engine, g_masterVolume);
}

} // namespace Audio

#undef STB_VORBIS_HEADER_ONLY
#include <extras/stb_vorbis.c>
