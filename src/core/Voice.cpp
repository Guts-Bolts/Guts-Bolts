#define MA_NO_ENCODING          // (the same settings as Audio.cpp, which builds the code itself)
#include <miniaudio.h>
#include <opus.h>

#include "Voice.h"
#include "Audio.h"
#include "Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace Voice {

namespace {

constexpr int    kRate = 48000;           // Opus likes 48 kHz
constexpr int    kFrame = 960;            // 20 ms
constexpr int    kFramesPerPacket = 3;    // 60 ms a packet
constexpr int    kBitrate = 24000;
constexpr float  kGate = 0.015f;          // Open Mic: quieter than this is silence
constexpr double kHangover = 0.35;        // keep sending a moment after you stop talking
constexpr double kSpeakingFor = 0.35;     // the speaker icon stays this long after a packet
constexpr double kForgetAfter = 30.0;     // a quiet speaker's sound is freed after this long
constexpr ma_uint32 kPlayBuffer = kRate;  // 1 s per speaker
constexpr ma_uint32 kMaxQueued = kRate * 2 / 5;   // over 0.4 s behind: drop (so we never lag far behind)

double clockNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// --- sending ---
bool        g_active = false;
bool        g_micOpen = false;
ma_device   g_mic;
ma_pcm_rb   g_micRb;              // filled by the mic's own thread, emptied in update()
OpusEncoder* g_enc = nullptr;
bool        g_held = false, g_openMic = false;
bool        g_sending = false;
double      g_loudAt = -100.0;
float       g_level = 0.0f;
std::vector<std::string> g_frames;   // frames waiting to make a packet
std::vector<std::string> g_out;      // packets waiting to be sent

void onMic(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* rb = (ma_pcm_rb*)device->pUserData;
    const float* in = (const float*)input;
    while (frames > 0) {
        ma_uint32 n = frames;
        void* dst = nullptr;
        if (ma_pcm_rb_acquire_write(rb, &n, &dst) != MA_SUCCESS || n == 0) return;   // full: drop
        std::copy(in, in + n, (float*)dst);
        ma_pcm_rb_commit_write(rb, n);
        in += n;
        frames -= n;
    }
}

void closeMic() {
    if (g_micOpen) {
        ma_device_uninit(&g_mic);
        ma_pcm_rb_uninit(&g_micRb);
        g_micOpen = false;
    }
    if (g_enc) { opus_encoder_destroy(g_enc); g_enc = nullptr; }
    g_frames.clear();
    g_out.clear();
    g_sending = false;
    g_level = 0.0f;
}

void openMic() {
    if (g_micOpen) return;
    int err = 0;
    g_enc = opus_encoder_create(kRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (!g_enc || err != OPUS_OK) { g_enc = nullptr; return; }
    opus_encoder_ctl(g_enc, OPUS_SET_BITRATE(kBitrate));
    opus_encoder_ctl(g_enc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(g_enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    if (ma_pcm_rb_init(ma_format_f32, 1, kRate, nullptr, nullptr, &g_micRb) != MA_SUCCESS) { closeMic(); return; }
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = kRate;
    cfg.dataCallback = onMic;
    cfg.pUserData = &g_micRb;
    if (ma_device_init(nullptr, &cfg, &g_mic) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&g_micRb);
        opus_encoder_destroy(g_enc);
        g_enc = nullptr;
        Log::system("Voice chat: no microphone found, so you can listen but not talk.");
        return;
    }
    g_micOpen = true;
    if (ma_device_start(&g_mic) != MA_SUCCESS) { closeMic(); return; }
}

// --- hearing ---
struct Speaker {
    OpusDecoder* dec = nullptr;
    ma_pcm_rb    rb;
    ma_sound     sound;
    bool         ready = false;
    double       heardAt = -100.0;
    glm::vec3    pos{0.0f};
    float        level = 0.0f;            // how loud the last bit they said was (0..1, for lip sync)
    ~Speaker() {
        if (ready) { ma_sound_uninit(&sound); ma_pcm_rb_uninit(&rb); }
        if (dec) opus_decoder_destroy(dec);
    }
};
std::map<std::string, std::unique_ptr<Speaker>> g_speakers;
std::set<std::string> g_muted;
float g_volume = 1.0f;

Speaker* speakerFor(const std::string& name) {
    auto it = g_speakers.find(name);
    if (it != g_speakers.end()) return it->second.get();
    auto* engine = (ma_engine*)Audio::engineHandle();
    if (!engine) return nullptr;
    auto s = std::make_unique<Speaker>();
    int err = 0;
    s->dec = opus_decoder_create(kRate, 1, &err);
    if (!s->dec || err != OPUS_OK) return nullptr;
    if (ma_pcm_rb_init(ma_format_f32, 1, kPlayBuffer, nullptr, nullptr, &s->rb) != MA_SUCCESS) return nullptr;
    ma_pcm_rb_set_sample_rate(&s->rb, kRate);
    if (ma_sound_init_from_data_source(engine, &s->rb, 0, nullptr, &s->sound) != MA_SUCCESS) {
        ma_pcm_rb_uninit(&s->rb);
        return nullptr;
    }
    s->ready = true;
    // Clear up close, fading out across a big room; gone by 90 studs.
    ma_sound_set_attenuation_model(&s->sound, ma_attenuation_model_linear);
    ma_sound_set_min_distance(&s->sound, 12.0f);
    ma_sound_set_max_distance(&s->sound, 90.0f);
    ma_sound_set_volume(&s->sound, g_volume * 1.4f);
    ma_sound_start(&s->sound);
    Speaker* raw = s.get();
    g_speakers[name] = std::move(s);
    return raw;
}

void writeSamples(ma_pcm_rb* rb, const float* data, ma_uint32 frames) {
    while (frames > 0) {
        ma_uint32 n = frames;
        void* dst = nullptr;
        if (ma_pcm_rb_acquire_write(rb, &n, &dst) != MA_SUCCESS || n == 0) return;
        if (data) { std::copy(data, data + n, (float*)dst); data += n; }
        else std::fill((float*)dst, (float*)dst + n, 0.0f);
        ma_pcm_rb_commit_write(rb, n);
        frames -= n;
    }
}

} // namespace

void setActive(bool on) {
    if (on == g_active) return;
    g_active = on;
    if (on) openMic();
    else    closeMic();
}
bool active() { return g_active; }
bool micWorks() { return g_micOpen; }

void setTalking(bool held, bool openMic) { g_held = held; g_openMic = openMic; }
bool sending() { return g_sending; }
float micLevel() { return g_level; }

bool takePacket(std::string& bytes) {
    if (g_out.empty()) return false;
    bytes = std::move(g_out.front());
    g_out.erase(g_out.begin());
    return true;
}

void update(float) {
    if (!g_micOpen) return;
    float frame[kFrame];
    unsigned char packet[1500];
    const double now = clockNow();
    while (ma_pcm_rb_available_read(&g_micRb) >= (ma_uint32)kFrame) {
        ma_uint32 got = 0;
        while (got < (ma_uint32)kFrame) {
            ma_uint32 n = kFrame - got;
            void* src = nullptr;
            if (ma_pcm_rb_acquire_read(&g_micRb, &n, &src) != MA_SUCCESS || n == 0) break;
            std::copy((float*)src, (float*)src + n, frame + got);
            ma_pcm_rb_commit_read(&g_micRb, n);
            got += n;
        }
        if (got < (ma_uint32)kFrame) break;
        float sum = 0.0f;
        for (float v : frame) sum += v * v;
        const float rms = std::sqrt(sum / kFrame);
        g_level = std::max(rms * 6.0f > 1.0f ? 1.0f : rms * 6.0f, g_level * 0.85f);
        if (rms > kGate) g_loudAt = now;
        const bool want = g_held || (g_openMic && now - g_loudAt < kHangover);
        if (!want) {
            if (g_sending && !g_frames.empty())   // the end of what you said
                g_out.push_back(pack(g_frames.data(), (int)g_frames.size()));
            g_frames.clear();
            g_sending = false;
            continue;
        }
        g_sending = true;
        const int len = opus_encode_float(g_enc, frame, kFrame, packet, sizeof(packet));
        if (len <= 0) continue;
        g_frames.emplace_back((const char*)packet, (size_t)len);
        if ((int)g_frames.size() >= kFramesPerPacket) {
            g_out.push_back(pack(g_frames.data(), (int)g_frames.size()));
            g_frames.clear();
        }
    }
    if (g_out.size() > 20) g_out.erase(g_out.begin(), g_out.end() - 20);   // nobody's taking them: keep it small
    // Quiet speakers: free their sound.
    for (auto it = g_speakers.begin(); it != g_speakers.end();)
        if (now - it->second->heardAt > kForgetAfter) it = g_speakers.erase(it);
        else ++it;
}

void receive(const std::string& speaker, const std::string& bytes, const glm::vec3& pos) {
    if (speaker.empty() || g_muted.count(speaker)) return;
    Speaker* s = speakerFor(speaker);
    if (!s) return;
    std::string frames[8];
    int count = 0;
    if (!unpack(bytes, frames, count, 8)) return;
    s->pos = pos;
    ma_sound_set_position(&s->sound, pos.x, pos.y, pos.z);
    const ma_uint32 queued = ma_pcm_rb_available_read(&s->rb);
    if (queued > kMaxQueued) return;   // we're behind: skip this bit to catch up
    // Starting to talk again: a little silence first, so a late packet doesn't cut them up.
    if (queued < (ma_uint32)kFrame) writeSamples(&s->rb, nullptr, kFrame * 3);
    float pcm[kFrame * 6];
    float loud = 0.0f;
    for (int i = 0; i < count; ++i) {
        const int n = opus_decode_float(s->dec, (const unsigned char*)frames[i].data(), (opus_int32)frames[i].size(),
                                        pcm, kFrame * 6, 0);
        if (n > 0) {
            writeSamples(&s->rb, pcm, (ma_uint32)n);
            double sum = 0.0;
            for (int k = 0; k < n; ++k) sum += (double)pcm[k] * pcm[k];
            loud = std::max(loud, (float)std::sqrt(sum / n));
        }
    }
    s->level = std::min(1.0f, loud * 6.0f);
    s->heardAt = clockNow();
}

namespace {
// Their voice is still coming out (or only just stopped).
bool isSpeaking(Speaker& s, double now) {
    return now - s.heardAt < kSpeakingFor || (now - s.heardAt < 2.0 && ma_pcm_rb_available_read(&s.rb) > (ma_uint32)kFrame);
}
}

bool speaking(const std::string& speaker) {
    auto it = g_speakers.find(speaker);
    return it != g_speakers.end() && isSpeaking(*it->second, clockNow());
}

void eachSpeaking(const std::function<void(const std::string&, const glm::vec3&)>& fn) {
    const double now = clockNow();
    for (auto& [name, s] : g_speakers)
        if (isSpeaking(*s, now)) fn(name, s->pos);
}

float level(const std::string& speaker) {
    auto it = g_speakers.find(speaker);
    if (it == g_speakers.end()) return 0.0f;
    // Fades out quickly once their packets stop (so the mouth closes).
    const double quiet = clockNow() - it->second->heardAt;
    return quiet > 0.25 ? 0.0f : it->second->level * (float)(1.0 - quiet / 0.25);
}

void forget(const std::string& speaker) { g_speakers.erase(speaker); }
void forgetAll() { g_speakers.clear(); }

void setMuted(const std::string& speaker, bool m) {
    if (m) { g_muted.insert(speaker); g_speakers.erase(speaker); }
    else g_muted.erase(speaker);
}
bool muted(const std::string& speaker) { return g_muted.count(speaker) > 0; }

void setVolume(float v) {
    g_volume = std::clamp(v, 0.0f, 2.0f);
    for (auto& [name, s] : g_speakers) ma_sound_set_volume(&s->sound, g_volume * 1.4f);
}

void shutdown() {
    closeMic();
    g_active = false;
    g_speakers.clear();
}

std::string pack(const std::string* frames, int count) {
    std::string out;
    out.push_back((char)count);
    for (int i = 0; i < count; ++i) {
        const size_t n = std::min<size_t>(frames[i].size(), 0xFFFF);
        out.push_back((char)(n & 0xFF));
        out.push_back((char)(n >> 8));
        out.append(frames[i], 0, n);
    }
    return out;
}

bool unpack(const std::string& bytes, std::string* frames, int& count, int maxCount) {
    if (bytes.empty()) return false;
    count = (unsigned char)bytes[0];
    if (count < 1 || count > maxCount) return false;
    size_t at = 1;
    for (int i = 0; i < count; ++i) {
        if (at + 2 > bytes.size()) return false;
        const size_t n = (unsigned char)bytes[at] | ((size_t)(unsigned char)bytes[at + 1] << 8);
        at += 2;
        if (n == 0 || at + n > bytes.size()) return false;
        frames[i] = bytes.substr(at, n);
        at += n;
    }
    return at == bytes.size();
}

} // namespace Voice
