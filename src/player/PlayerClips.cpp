// The clip recorder: while you play, the last 30 seconds of the game are kept (small, 10
// pictures a second), and F3 or the menu's Save Clip button saves them as an animated .gif
// in the Clips folder next to the Player (in a browser, it downloads). Settings can turn the
// recorder off (it's off on phones to save memory). See game/ClipRecorder.h.
#include "PlayerApp.h"
#include "SiteUi.h"
#include "../core/Paths.h"
#include "../core/Settings.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <imgui.h>
#include <chrono>
#include <ctime>
#include <fstream>

namespace {
// "Clip 2026-11-02 14-20-50.gif" (a number on the end if that's taken).
std::filesystem::path clipName(const std::filesystem::path& folder) {
    char stamp[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "Clip %Y-%m-%d %H-%M-%S", std::localtime(&now));
    std::filesystem::path p = folder / (std::string(stamp) + ".gif");
    for (int i = 2; std::filesystem::exists(p) && i < 100; ++i)
        p = folder / (std::string(stamp) + " (" + std::to_string(i) + ").gif");
    return p;
}
} // namespace

bool PlayerApp::canSaveClip() const {
    return m_page == Page::Game && m_scene && GraphicsSettings::get().clipRecorder && m_clips.frameCount() > 0;
}

void PlayerApp::recordClipFrame() {
    if (!GraphicsSettings::get().clipRecorder || m_page != Page::Game) {
        if (m_clips.frameCount()) m_clips.clear();   // (turned off: give the memory back)
        return;
    }
    const double now = ImGui::GetTime();
    if (!m_clips.wantFrame(now)) return;
    int w = 0, h = 0;
    ClipRecorder::frameSize(m_view.width(), m_view.height(), w, h);
    if (w <= 0 || !m_view.readShrunk(m_clipFbo, w, h, m_clipPx)) return;
    m_clips.addFrame(m_clipPx.data(), w, h, true, now);
}

void PlayerApp::saveClip() {
    if (!canSaveClip()) return;
    if (m_clipSaving.valid()) { m_clipMsg = "Still saving the last clip..."; m_clipMsgUntil = ImGui::GetTime() + 3.0; return; }
    ClipRecorder::Snapshot snap = m_clips.snapshot();
    const double secs = (double)snap.frames.size() / ClipRecorder::kFps;
    char what[64];
    std::snprintf(what, sizeof what, "%.0f seconds", secs);
#ifdef __EMSCRIPTEN__
    // In a browser: hand the file to the page as a download (no threads here, so right away).
    const std::string gif = ClipRecorder::toGif(snap);
    EM_ASM({
        const bytes = HEAPU8.slice($0, $0 + $1);
        const a = document.createElement('a');
        a.href = URL.createObjectURL(new Blob([bytes], { type: 'image/gif' }));
        a.download = UTF8ToString($2);
        document.body.appendChild(a);
        a.click();
        setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
    }, gif.data(), (int)gif.size(), clipName("").filename().string().c_str());
    m_clipMsg = std::string("Clip saved to your downloads (") + what + ")";
    m_clipMsgUntil = ImGui::GetTime() + 4.0;
#else
    // Writing a GIF takes a moment: do it on the side so the game keeps going.
    const std::filesystem::path folder = Paths::appFolder() / "Clips";
    m_clipSaving = std::async(std::launch::async, [snap = std::move(snap), folder, w = std::string(what)]() {
        const std::string gif = ClipRecorder::toGif(snap);
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        const std::filesystem::path file = clipName(folder);
        std::ofstream out(file, std::ios::binary);
        out.write(gif.data(), (std::streamsize)gif.size());
        return out.good() ? "Clip saved (" + w + "): " + file.string() : "Couldn't save the clip in " + folder.string();
    });
    m_clipMsg = std::string("Saving the last ") + what + "...";
    m_clipMsgUntil = ImGui::GetTime() + 30.0;
#endif
}

void PlayerApp::drawClipMsg(ImDrawList* dl, ImVec2 pos, ImVec2 max) {
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !m_chatOpen && ImGui::IsKeyPressed(ImGuiKey_F3, false)) saveClip();
    if (m_clipSaving.valid() && m_clipSaving.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        m_clipMsg = m_clipSaving.get();
        m_clipMsgUntil = ImGui::GetTime() + 4.0;
    }
    if (ImGui::GetTime() >= m_clipMsgUntil || m_clipMsg.empty()) return;
    const ImVec2 ts = ImGui::CalcTextSize(m_clipMsg.c_str());
    const float w = ts.x + 32, x = pos.x + (max.x - pos.x - w) * 0.5f, y = pos.y + 16;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + ts.y + 16), IM_COL32(20, 20, 24, 220), 8.0f);
    dl->AddText(ImVec2(x + 16, y + 8), IM_COL32(255, 255, 255, 255), m_clipMsg.c_str());
}
