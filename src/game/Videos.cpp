#include "Videos.h"
#include "../core/Audio.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../renderer/Mesh.h"
#include "../renderer/Textures.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#define PL_MPEG_IMPLEMENTATION
#include <pl_mpeg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace Videos {
namespace {

constexpr int kMaxSide = 4096;   // (bigger than this isn't a video for a game)

struct Video {
    std::string source;           // the VideoFrame's Video when this was loaded
    plm_t* plm = nullptr;
    bool broken = false;
    int w = 0, h = 0;
    double fps = 30.0, length = 0.0;
    double time = 0.0;            // TimePosition
    double shownAt = -1.0;        // the time of the picture in `rgba`
    std::vector<uint8_t> rgba;
    bool dirty = false;           // `rgba` changed since the texture was made
    bool playing = false;         // what we last did (to notice Playing changing)
    bool told = false;            // Loaded fired
    std::string wav;              // its sound, as a .wav the sound engine can play ("" = none)
    int sound = 0;                // the sound playing now
    float volume = -1.0f;
    ~Video() {
        if (sound) Audio::stop(sound);
        if (plm) plm_destroy(plm);
    }
};

std::unordered_map<uint64_t, std::unique_ptr<Video>>& all() {
    static std::unordered_map<uint64_t, std::unique_ptr<Video>> v;
    return v;
}

std::filesystem::path fileFor(const std::string& video) {
    std::error_code ec;
    if (video.rfind("gb:", 0) == 0) return Paths::downloaded(video.substr(3));
    std::filesystem::path p = Paths::gamesFolder() / video;
    if (std::filesystem::exists(p, ec)) return p;
    return std::filesystem::path(video);
}

// The video's sound, written out as a .wav once (MPEG-1 sound isn't a file type the
// sound engine reads). Stereo 32-bit float.
std::string soundFile(const std::filesystem::path& video, uint64_t id) {
    plm_t* a = plm_create_with_filename(video.string().c_str());
    if (!a) return "";
    std::string out;
    if (plm_has_headers(a) && plm_get_num_audio_streams(a) > 0) {
        plm_set_video_enabled(a, 0);
        plm_set_audio_enabled(a, 1);
        const int rate = plm_get_samplerate(a);
        std::vector<float> samples;
        while (plm_samples_t* s = plm_decode_audio(a)) {
            samples.insert(samples.end(), s->interleaved, s->interleaved + s->count * 2);
            if (samples.size() > (size_t)rate * 2 * 60 * 30) break;   // (half an hour is plenty)
        }
        if (!samples.empty() && rate > 0) {
            std::error_code ec;
            const std::filesystem::path dir = Paths::appFolder() / "video-sound";
            std::filesystem::create_directories(dir, ec);
            const std::filesystem::path wav = dir / (std::to_string(id) + ".wav");
            std::ofstream f(wav, std::ios::binary);
            auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
            auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
            const uint32_t bytes = (uint32_t)(samples.size() * sizeof(float));
            f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
            f.write("fmt ", 4); u32(16); u16(3); u16(2); u32((uint32_t)rate); u32((uint32_t)rate * 8); u16(8); u16(32);
            f.write("data", 4); u32(bytes);
            f.write(reinterpret_cast<const char*>(samples.data()), bytes);
            if (f) out = wav.string();
        }
    }
    plm_destroy(a);
    return out;
}

void open(Video& v, uint64_t id, const std::string& source) {
    v.source = source;
    if (source.empty()) { v.broken = true; return; }
    const std::filesystem::path file = fileFor(source);
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) { v.broken = true; return; }
    v.plm = plm_create_with_filename(file.string().c_str());
    if (!v.plm || !plm_has_headers(v.plm) || plm_get_width(v.plm) <= 0) {
        Log::warn("VideoFrame: \"" + source + "\" isn't a video Guts&Bolts can play (use an MPEG-1 .mpg file)");
        if (v.plm) plm_destroy(v.plm);
        v.plm = nullptr;
        v.broken = true;
        return;
    }
    plm_set_audio_enabled(v.plm, 0);   // (the sound plays from its own .wav)
    v.w = plm_get_width(v.plm);
    v.h = plm_get_height(v.plm);
    if (v.w <= 0 || v.h <= 0 || v.w > kMaxSide || v.h > kMaxSide) {
        plm_destroy(v.plm);
        v.plm = nullptr;
        v.broken = true;
        return;
    }
    v.fps = plm_get_framerate(v.plm) > 1.0 ? plm_get_framerate(v.plm) : 30.0;
    v.length = plm_get_duration(v.plm);
    if (v.length < 0.05) {   // (a short file may not say how long it is: count its pictures)
        if (plm_t* c = plm_create_with_filename(file.string().c_str())) {
            plm_set_audio_enabled(c, 0);
            int count = 0;
            while (plm_decode_video(c) && count < 60 * 60 * 120) ++count;
            v.length = count / v.fps;
            plm_destroy(c);
        }
    }
    v.rgba.assign((size_t)v.w * v.h * 4, 255);
    if (!Mesh::headless) v.wav = soundFile(file, id);
}

Video* get(const SceneNode& n) {
    if (n.kind != NodeKind::Gui || n.gui.type != GuiType::VideoFrame) return nullptr;
    auto& slot = all()[n.id];
    if (!slot || slot->source != n.gui.video) {
        slot = std::make_unique<Video>();
        open(*slot, n.id, n.gui.video);
    }
    return slot->broken ? nullptr : slot.get();
}

// Decode up to `time` (only the last picture is turned into colours).
void showUpTo(Video& v, double time, bool& ended) {
    ended = false;
    if (!v.plm) return;
    plm_frame_t* last = nullptr;
    // A picture lasts 1/fps: the next one is due once its time has come.
    while (v.shownAt < 0.0 || v.shownAt + 1.0 / v.fps <= time + 1e-6) {
        plm_frame_t* f = plm_decode_video(v.plm);
        if (!f) { ended = plm_has_ended(v.plm); break; }
        last = f;
        v.shownAt = f->time;
        if (v.shownAt < 0.0) v.shownAt = 0.0;
    }
    if (last) {
        plm_frame_to_rgba(last, v.rgba.data(), v.w * 4);
        v.dirty = true;
    }
}

void startSound(Video& v, float volume) {
    if (v.sound) { Audio::stop(v.sound); v.sound = 0; }
    if (v.wav.empty()) return;
    v.sound = Audio::play(v.wav, volume);
    if (v.sound && v.time > 0.01) Audio::seek(v.sound, v.time);
    v.volume = volume;
}
void stopSound(Video& v) {
    if (v.sound) Audio::stop(v.sound);
    v.sound = 0;
}

void restart(Video& v) {
    if (!v.plm) return;
    plm_rewind(v.plm);
    v.time = 0.0;
    v.shownAt = -1.0;
}

} // namespace

void update(Scene& scene, float dt, const std::function<void(uint64_t, Event)>& fire) {
    std::set<uint64_t> seen;
    scene.forEach([&](SceneNode* n) {
        if (n->kind != NodeKind::Gui || n->gui.type != GuiType::VideoFrame) return;
        seen.insert(n->id);
        Video* v = get(*n);
        if (!v) return;
        auto tell = [&](Event e) { if (fire) fire(n->id, e); };
        if (!v->told) { v->told = true; tell(Event::Loaded); }
        GuiProps& g = n->gui;
        // Playing changed (by a script, or the game started with it on).
        if (g.playing != v->playing) {
            v->playing = g.playing;
            if (g.playing) {
                if (plm_has_ended(v->plm) || v->time >= v->length - 1e-3) restart(*v);   // (played to the end: from the start)
                startSound(*v, g.volume);
                tell(Event::Played);
            } else {
                stopSound(*v);
                tell(Event::Paused);
            }
        }
        if (v->sound && v->volume != g.volume) { Audio::setVolume(v->sound, g.volume); v->volume = g.volume; }
        if (!g.playing) return;
        v->time += dt;
        bool ended = false;
        showUpTo(*v, v->time, ended);
        if (ended || (v->length > 0.0 && v->time >= v->length + 1.0 / v->fps)) {
            if (g.looped) {
                restart(*v);
                showUpTo(*v, 0.0, ended);
                startSound(*v, g.volume);
                tell(Event::DidLoop);
            } else {
                v->time = v->length;
                g.playing = false;
                v->playing = false;
                stopSound(*v);
                tell(Event::Ended);
            }
        }
    });
    // Let go of videos whose VideoFrame is gone.
    for (auto it = all().begin(); it != all().end();) {
        if (!seen.count(it->first)) {
            if (!Mesh::headless) Textures::forget("video:" + std::to_string(it->first));
            it = all().erase(it);
        } else {
            ++it;
        }
    }
}

unsigned texture(const SceneNode& n) {
    Video* v = get(n);
    if (!v || Mesh::headless) return 0;
    if (v->shownAt < 0.0) { bool ended = false; showUpTo(*v, v->time, ended); }   // (its first picture, even before it plays)
    const std::string id = "video:" + std::to_string(n.id);
    if (v->dirty) {
        v->dirty = false;
        return Textures::put(id, v->rgba.data(), v->w, v->h);
    }
    return v->shownAt >= 0.0 ? Textures::get(id) : 0;
}

bool loaded(const SceneNode& n) { return get(n) != nullptr; }
double length(const SceneNode& n) { Video* v = get(n); return v ? v->length : 0.0; }
glm::ivec2 resolution(const SceneNode& n) { Video* v = get(n); return v ? glm::ivec2(v->w, v->h) : glm::ivec2(0); }
double position(const SceneNode& n) { Video* v = get(n); return v ? v->time : 0.0; }

void seek(const SceneNode& n, double seconds) {
    Video* v = get(n);
    if (!v || !v->plm) return;
    const double t = std::clamp(seconds, 0.0, std::max(0.0, v->length));
    plm_frame_t* f = plm_seek_frame(v->plm, t, 1);
    if (f && std::fabs(f->time - t) < 1.0) {
        plm_frame_to_rgba(f, v->rgba.data(), v->w * 4);
        v->shownAt = f->time;
        v->dirty = true;
    } else {   // (a file it can't jump about in: from the start, up to there)
        restart(*v);
        bool ended = false;
        showUpTo(*v, t, ended);
    }
    v->time = t;
    if (v->sound) Audio::seek(v->sound, t);
}

glm::vec3 colourAt(const SceneNode& n, float u, float vv) {
    Video* v = get(n);
    if (!v) return glm::vec3(0.0f);
    if (v->shownAt < 0.0) { bool ended = false; showUpTo(*v, v->time, ended); }
    const int x = std::clamp((int)(u * v->w), 0, v->w - 1), y = std::clamp((int)(vv * v->h), 0, v->h - 1);
    const uint8_t* p = &v->rgba[((size_t)y * v->w + x) * 4];
    return glm::vec3(p[0], p[1], p[2]) / 255.0f;
}

void stopAll() {
    for (auto& [id, v] : all())
        if (!Mesh::headless) Textures::forget("video:" + std::to_string(id));
    all().clear();
}

} // namespace Videos
