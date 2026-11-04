// VideoFrame: a video in game UI or on a part. It loads an MPEG-1 file, shows its first
// picture, plays (the picture changes with time), stops at the end or loops, jumps with
// TimePosition, tells scripts (Played / Ended / DidLoop) and is saved with the game
// (game/Videos.cpp).
//
//   VideoFrameTests <a game file with a player> <tests/media/red-then-blue.mpg>
#include "core/Log.h"
#include "game/GameSession.h"
#include "game/Videos.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

std::string run(GameSession& session, const std::string& code) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    std::string err;
    const bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    std::string out = ok ? "" : "ERROR " + err;
    for (const Log::Entry& e : lines) {
        if (e.level == Log::Level::Error) out += "ERROR " + e.text;
        else if (e.text.rfind("> ", 0) != 0) out += e.text;
    }
    return out;
}

void frames(GameSession& session, int n) {
    for (int i = 0; i < n; ++i) {
        ImGui::NewFrame();
        session.update(1.0f / 60.0f, 0.0f, false);
        ImGui::EndFrame();
    }
}

bool red(const glm::vec3& c) { return c.r > 0.7f && c.b < 0.3f; }
bool blue(const glm::vec3& c) { return c.b > 0.7f && c.r < 0.3f; }
std::string show(const glm::vec3& c) {
    return "(" + std::to_string(c.r).substr(0, 4) + ", " + std::to_string(c.g).substr(0, 4) + ", " + std::to_string(c.b).substr(0, 4) + ")";
}
float numAttr(const SceneNode* n, const char* name) {
    const Attribute* a = n ? n->findAttribute(name) : nullptr;
    return a && a->type == Attribute::Number ? (float)a->n : -999.0f;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: VideoFrameTests <game file> <video.mpg>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.start();
    frames(session, 5);

    // A TV: a part with a SurfaceGui, and a VideoFrame on it.
    std::string code = R"(
        local tv = Instance.new("Part") tv.Name = "TV" tv.Anchored = true tv.Size = Vector3.new(8, 4.5, 0.5)
        tv.Position = Vector3.new(0, 10, -20) tv.Parent = workspace
        local screen = Instance.new("SurfaceGui") screen.Face = Enum.NormalId.Front screen.Parent = tv
        local v = Instance.new("VideoFrame") v.Name = "Video" v.Size = UDim2.new(1, 0, 1, 0)
        v.Video = "VIDEO" v.Parent = screen
        v:SetAttribute("Played", 0) v:SetAttribute("Ended", 0) v:SetAttribute("Loops", 0)
        v.Played:Connect(function() v:SetAttribute("Played", v:GetAttribute("Played") + 1) end)
        v.Ended:Connect(function() v:SetAttribute("Ended", v:GetAttribute("Ended") + 1) end)
        v.DidLoop:Connect(function() v:SetAttribute("Loops", v:GetAttribute("Loops") + 1) end)
        print(v.ClassName, v.IsLoaded, math.floor(v.TimeLength * 10 + 0.5) / 10, v.Resolution.X, v.Resolution.Y, v.Playing)
    )";
    code.replace(code.find("VIDEO"), 5, argv[2]);
    std::string out = run(session, code);
    check(out == "VideoFrame true 2.0 64.0 48.0 false", "Instance.new(\"VideoFrame\") loads the video: " + out);
    SceneNode* video = nullptr;
    scene.forEach([&](SceneNode* n) { if (n->name == "Video" && n->isGui()) video = n; });
    if (!video) { std::cerr << "no VideoFrame\n"; return 1; }

    check(red(Videos::colourAt(*video, 0.5f, 0.5f)), "before it plays, it shows the first picture (red) " + show(Videos::colourAt(*video, 0.5f, 0.5f)));
    frames(session, 30);
    check(red(Videos::colourAt(*video, 0.5f, 0.5f)) && Videos::position(*video) < 0.01, "not playing: it stays put");

    out = run(session, "workspace.TV.SurfaceGui.Video:Play()");
    if (!out.empty()) std::cout << "      " << out << "\n";
    frames(session, 30);   // half a second
    check(red(Videos::colourAt(*video, 0.5f, 0.5f)) && std::fabs(Videos::position(*video) - 0.5) < 0.05,
          "Play(): half a second in, still red (TimePosition " + std::to_string(Videos::position(*video)) + ")");
    frames(session, 60);   // 1.5 seconds
    check(blue(Videos::colourAt(*video, 0.5f, 0.5f)), "a second later the picture has changed to blue " + show(Videos::colourAt(*video, 0.5f, 0.5f)));
    check(numAttr(video, "Played") == 1.0f, "Played fired");
    frames(session, 45);   // past the end
    check(!video->gui.playing && numAttr(video, "Ended") == 1.0f, "at the end it stops by itself, and Ended fires");

    // Jump about with TimePosition.
    out = run(session, "local v = workspace.TV.SurfaceGui.Video v.TimePosition = 0.2 print(math.floor(v.TimePosition * 10 + 0.5) / 10)");
    check(out == "0.2" && red(Videos::colourAt(*video, 0.5f, 0.5f)), "TimePosition = 0.2 goes back to the red part: " + out);
    run(session, "workspace.TV.SurfaceGui.Video.TimePosition = 1.5");
    check(blue(Videos::colourAt(*video, 0.5f, 0.5f)), "TimePosition = 1.5 jumps to the blue part");

    // Looped: round it goes.
    run(session, "local v = workspace.TV.SurfaceGui.Video v.Looped = true v.TimePosition = 0 v.Playing = true");
    frames(session, 140);   // about 2.3 seconds
    check(video->gui.playing && numAttr(video, "Loops") >= 1.0f && red(Videos::colourAt(*video, 0.5f, 0.5f)),
          "Looped: after the end it starts again (DidLoop fired, red again)");
    run(session, "workspace.TV.SurfaceGui.Video:Pause()");
    const double pausedAt = Videos::position(*video);
    frames(session, 30);
    check(!video->gui.playing && std::fabs(Videos::position(*video) - pausedAt) < 1e-6, "Pause() stops it where it is");

    // Saved with the game.
    auto copy = Serializer::nodeFromString(Serializer::nodeToString(*video), false);
    check(copy && copy->gui.type == GuiType::VideoFrame && copy->gui.video == argv[2] && copy->gui.looped && !copy->gui.playing &&
              std::fabs(copy->gui.volume - 0.5f) < 1e-4,
          "it's saved with its video and settings");

    // A file that isn't a video.
    out = run(session, "local v = workspace.TV.SurfaceGui.Video v.Video = 'not-a-video.txt' print(v.IsLoaded, v.TimeLength)");
    check(out.find("false 0") == 0, "a file that isn't a video: IsLoaded is false (" + out + ")");
    out = run(session, "workspace.TV.SurfaceGui.Video.TimeLength = 5");
    check(out.find("can't be set") != std::string::npos, "TimeLength can't be set: " + out);

    std::cout << (failures ? "FAILED" : "all passed") << "\n";
    return failures ? 1 : 0;
}
