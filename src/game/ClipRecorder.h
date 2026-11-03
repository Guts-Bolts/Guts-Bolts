#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// The clip recorder: while you play, the Player keeps the last 30 seconds of the game
// (small, 10 pictures a second), and F3 (or the menu's Save Clip button) saves them as an
// animated .gif you can share anywhere. Like photo mode, but moving.
//
// Frames are shrunk to at most 480 pixels wide and turned into 252 colours as they come in
// (with a little dithering so skies don't band), so 30 seconds take about 40 MB.
class ClipRecorder {
public:
    static constexpr int    kFps = 10;
    static constexpr double kSeconds = 30.0;
    static constexpr int    kMaxWidth = 480;

    // Is it time for the next picture? (now in seconds, any clock)
    bool wantFrame(double now) const { return now - m_lastAt >= 1.0 / kFps - 0.002; }
    // The picture to fit a w x h view into (keeps its shape, at most kMaxWidth wide).
    static void frameSize(int w, int h, int& outW, int& outH);
    // A new picture, already frameSize() big: RGBA, bottom row first when flipY (OpenGL's way).
    void addFrame(const uint8_t* rgba, int w, int h, bool flipY, double now);
    void clear() { m_frames.clear(); m_lastAt = -1e9; }

    size_t frameCount() const { return m_frames.size(); }
    double seconds() const { return (double)m_frames.size() / kFps; }

    // The frames so far as an animated GIF file ("" if there are none). Copies what it needs
    // first, so it can be written on another thread while recording goes on (see snapshot).
    struct Snapshot {
        int w = 0, h = 0;
        std::vector<std::vector<uint8_t>> frames;   // palette indexes, top row first
    };
    Snapshot snapshot() const;
    static std::string toGif(const Snapshot& clip);

    // The fixed 252-colour palette (6 reds x 7 greens x 6 blues), as RGB triples (256 entries).
    static const std::vector<uint8_t>& palette();
    static uint8_t nearest(int r, int g, int b);   // a colour's palette index (no dithering)

private:
    struct Frame { int w, h; std::vector<uint8_t> px; };
    std::deque<Frame> m_frames;
    double m_lastAt = -1e9;
};
