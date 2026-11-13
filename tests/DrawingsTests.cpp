// Drawing in games and publishing the drawing: AssetService:CreateEditableImage (EditableImages)
// and AssetService:PromptPublishImageAsync (ImagePublish). Offline here, so a "yes" fails with
// "sign in", and a "no" comes back as Cancelled.
//
//   DrawingsTests <a game file>
#include "core/Log.h"
#include "game/GameSession.h"
#include "game/ImagePublish.h"
#include "renderer/EditableImages.h"
#include "renderer/Mesh.h"
#include "scene/Scene.h"
#include "scene/Serializer.h"

#include <imgui.h>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

bool run(GameSession& session, const std::string& code, std::string& err) {
    std::vector<Log::Entry> lines;
    Log::setCapture(&lines);
    err.clear();
    bool ok = session.scripts().runCommand(code, err);
    Log::setCapture(nullptr);
    for (const Log::Entry& e : lines)
        if (e.level == Log::Level::Error) { ok = false; err += e.text + " "; }
    return ok;
}

void test(GameSession& session, const std::string& what, const std::string& code) {
    std::string err;
    const bool ok = run(session, code, err);
    check(ok, what + (err.empty() ? "" : "  (" + err + ")"));
}
void step(GameSession& session, int frames) {
    for (int i = 0; i < frames; ++i) session.update(1.0f / 60.0f, 0.0f, false);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: DrawingsTests <game file>\n"; return 2; }
    Mesh::headless = true;
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(1280, 720);
    unsigned char* pixels = nullptr;
    int pw = 0, ph = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &pw, &ph);

    // The drawing itself.
    {
        const int id = EditableImages::create(16, 8);
        check(id > 0 && EditableImages::contentId(id) == "editable://" + std::to_string(id), "a new picture has a content id");
        check(EditableImages::create(2000, 10) == 0, "too wide is refused");
        EditableImages::fillRect(id, 0, 0, 16, 8, {1, 1, 1, 1}, false);
        EditableImages::setPixel(id, 3, 2, {1, 0, 0, 1}, false);
        EditableImages::Colour c;
        check(EditableImages::getPixel(id, 3, 2, c) && c.r == 1.0f && c.g == 0.0f, "a pixel can be set and read back");
        EditableImages::setPixel(id, 4, 2, {0, 0, 1, 0.5f}, true);
        check(EditableImages::getPixel(id, 4, 2, c) && c.b > 0.9f && c.r > 0.45f && c.r < 0.55f && c.a == 1.0f,
              "see-through paint mixes with what's under it");
        const std::string png = EditableImages::png(id);
        check(png.size() > 8 && png.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0, "it saves as a .png");
        EditableImages::destroy(id);
        check(!EditableImages::exists(id), "a destroyed picture is gone");
    }

    Scene scene;
    GameSession session(&scene);
    std::string err;
    if (!Serializer::loadGameFile(scene, argv[1], &err)) { std::cerr << "couldn't load: " << err << "\n"; return 1; }
    session.setRunOnly(true);
    session.start();

    test(session, "AssetService draws", R"(
        local AS = game:GetService("AssetService")
        assert(AS.ClassName == "AssetService")
        local img = AS:CreateEditableImage({ Size = Vector2.new(64, 32) })
        assert(img.Size == Vector2.new(64, 32))
        assert(img.ContentId:sub(1, 11) == "editable://", img.ContentId)
        assert(Content.fromObject(img) == img.ContentId)
        img:DrawRectangle(Vector2.new(0, 0), img.Size, Color3.new(1, 1, 1), 0)
        img:DrawCircle(Vector2.new(32, 16), 6, Color3.new(1, 0, 0), 0)
        local c, t = img:GetPixel(Vector2.new(32, 16))
        assert(c.R > 0.99 and c.G < 0.01 and t == 0, "the circle's middle is red")
        local corner = img:GetPixel(Vector2.new(0, 0))
        assert(corner.G > 0.99, "the corner stayed white")
        img:DrawLine(Vector2.new(0, 30), Vector2.new(63, 30), Color3.new(0, 0, 1), 0, Enum.ImageCombineType.Overwrite, 1)
        assert(img:GetPixel(Vector2.new(40, 30)).B > 0.99, "the line is blue")
        local px = img:ReadPixels(Vector2.new(0, 0), Vector2.new(2, 1))
        assert(#px == 8 and px[1] == 1 and px[4] == 1)
        img:WritePixels(Vector2.new(0, 0), Vector2.new(1, 1), { 0, 1, 0, 1 })
        assert(img:GetPixel(Vector2.new(0, 0)).G == 1 and img:GetPixel(Vector2.new(0, 0)).R == 0)
        img:Clear()
        local _, see = img:GetPixel(Vector2.new(32, 16))
        assert(see == 1, "Clear makes it see-through")
        img:Destroy()
        assert(not pcall(function() img:GetPixel(Vector2.new(0, 0)) end), "a destroyed one says so")
        assert(not pcall(function() AS:CreateEditableImage({ Size = Vector2.new(5000, 5) }) end), "too big says so")
        assert(not pcall(function() AS:SomethingElse() end), "unknown calls say so")
    )");

    test(session, "ask to publish, the player says no", R"(
        local AS = game:GetService("AssetService")
        _G.canvas = AS:CreateEditableImage({ Size = Vector2.new(8, 8) })
        _G.canvas:DrawRectangle(Vector2.new(0, 0), Vector2.new(8, 8), Color3.new(0, 1, 0), 0)
        _G.answer = nil
        task.spawn(function()
            _G.answer = { AS:PromptPublishImageAsync(game.Players.LocalPlayer, _G.canvas, { Name = "Green" }) }
        end)
    )");
    step(session, 2);
    const int ticket = ImagePublish::pending();
    check(ticket > 0, "the question shows");
    ImagePublish::answer(ticket, false);
    step(session, 20);
    test(session, "the game hears Cancelled", R"(
        assert(_G.answer and _G.answer[1] == "Cancelled", _G.answer and _G.answer[1] or "no answer yet")
    )");

    test(session, "ask again, the player says yes", R"(
        _G.answer = nil
        task.spawn(function()
            _G.answer = { game:GetService("AssetService"):PromptPublishImageAsync(game.Players.LocalPlayer, _G.canvas, { Name = "Green" }) }
        end)
    )");
    step(session, 2);
    ImagePublish::answer(ImagePublish::pending(), true);
    step(session, 20);
    test(session, "not signed in: Failed, and it says why", R"(
        assert(_G.answer and _G.answer[1] == "Failed", _G.answer and _G.answer[1] or "no answer yet")
        assert(_G.answer[3]:find("Sign in"), _G.answer[3])
    )");

    test(session, "only an EditableImage can be published, by the player on this computer", R"(
        local AS = game:GetService("AssetService")
        assert(not pcall(function() AS:PromptPublishImageAsync(game.Players.LocalPlayer, {}, {}) end))
        assert(not pcall(function() AS:PromptPublishImageAsync("someone", _G.canvas, {}) end))
        assert(not pcall(function() game:GetService("AssetService"):PromptPublishImageAsync(nil, {}, {}) end))
    )");

    session.stop();
    std::cout << (failures ? "FAILED: " : "all passed: ") << failures << " problem(s)\n";
    return failures ? 1 : 0;
}
