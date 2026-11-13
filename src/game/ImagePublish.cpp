#include "ImagePublish.h"
#include "../core/I18n.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../renderer/Mesh.h"
#include "../renderer/Textures.h"

#include <imgui.h>
#include <stb_image.h>   // (its code is in Textures.cpp)
#include <algorithm>
#include <map>
#include <memory>

namespace ImagePublish {
namespace {

struct Ask {
    std::string png, name, description, game;
    int w = 0, h = 0;
    Status status = Status::Waiting;
    bool sending = false;
    unsigned tex = 0;   // the preview on the graphics card
    std::string assetId, message;
};
std::map<int, std::shared_ptr<Ask>>& asks() {
    static std::map<int, std::shared_ptr<Ask>> m;
    return m;
}
int& showing() {   // the ticket whose window is up (0 = none)
    static int t = 0;
    return t;
}
int& nextTicket() {
    static int n = 1;
    return n;
}
std::string previewId(int ticket) { return "publish-preview:" + std::to_string(ticket); }

} // namespace

int ask(const std::string& png, int width, int height, const std::string& name, const std::string& description,
        const std::string& game) {
    if (showing()) return 0;
    const int t = nextTicket()++;
    auto a = std::make_shared<Ask>();
    a->png = png;
    a->w = width;
    a->h = height;
    a->name = name;
    a->description = description;
    a->game = game;
    asks()[t] = a;
    showing() = t;
    return t;
}

Status result(int ticket, std::string& assetId, std::string& message) {
    auto it = asks().find(ticket);
    if (it == asks().end()) { message = "There's no such question."; return Status::Failed; }
    const Ask& a = *it->second;
    if (a.status == Status::Waiting) return Status::Waiting;
    assetId = a.assetId;
    message = a.message;
    const Status s = a.status;
    asks().erase(it);
    return s;
}

void answer(int ticket, bool publish) {
    auto it = asks().find(ticket);
    if (it == asks().end() || it->second->status != Status::Waiting || it->second->sending) return;
    std::shared_ptr<Ask> a = it->second;
    if (showing() == ticket) showing() = 0;
    if (!Mesh::headless) Textures::forget(previewId(ticket));
    if (!publish) { a->status = Status::Cancelled; a->message = "The player said no."; return; }
    if (!Online::online() || Online::isGuest()) {
        a->status = Status::Failed;
        a->message = "Sign in to Guts&Bolts to publish pictures.";
        return;
    }
    a->sending = true;
    nlohmann::json meta = {{"drawing", true}};
    if (!a->game.empty()) meta["game"] = a->game;
    std::weak_ptr<Ask> weak = a;
    Online::request("upload", {{"kind", "decal"}, {"name", a->name}, {"description", a->description}, {"access", "public"},
                               {"data", Online::base64Encode(a->png)}, {"meta", meta}},
                    [weak](const nlohmann::json& r) {
                        auto x = weak.lock();
                        if (!x) return;
                        x->sending = false;
                        if (r.value("ok", false)) {
                            x->status = Status::Published;
                            x->assetId = r.contains("asset") ? r["asset"].value("id", std::string()) : std::string();
                            x->message = r.contains("asset") && r["asset"].contains("review")
                                             ? "Published. It shows for others once staff have checked it."
                                             : "Published.";
                        } else {
                            x->status = Status::Failed;
                            x->message = r.value("error", std::string("Couldn't publish it."));
                        }
                    });
}

int pending() { return showing(); }

void reset() {
    for (auto& [t, a] : asks())
        if (!Mesh::headless) Textures::forget(previewId(t));
    asks().clear();
    showing() = 0;
}

void draw() {
    const int t = showing();
    if (!t) return;
    auto it = asks().find(t);
    if (it == asks().end()) { showing() = 0; return; }
    Ask& a = *it->second;
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Always);
    ImGui::SetNextWindowFocus();
    if (ImGui::Begin(I18n::tr("Publish your picture"), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        // The picture itself (made into a texture the first time).
        if (!a.tex && !a.png.empty()) {
            int w = 0, h = 0, n = 0;
            if (unsigned char* px = stbi_load_from_memory((const unsigned char*)a.png.data(), (int)a.png.size(), &w, &h, &n, 4)) {
                a.tex = Textures::put(previewId(t), px, w, h);
                stbi_image_free(px);
            }
        }
        const unsigned tex = a.tex;
        if (tex && a.w > 0 && a.h > 0) {
            const float scale = std::min(1.0f, std::min(360.0f / a.w, 220.0f / a.h));
            const ImVec2 size(a.w * scale, a.h * scale);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - size.x) * 0.5f);
            ImGui::Image((ImTextureID)(intptr_t)tex, size, ImVec2(0, 1), ImVec2(1, 0));
        }
        ImGui::TextWrapped("\"%s\"", a.name.c_str());
        ImGui::TextWrapped("%s", I18n::tr("This game wants to put your picture in your inventory as a decal. It's yours, and "
                                          "anyone can use it in their own games. New creators' pictures are checked by staff first."));
        ImGui::Spacing();
        if (ImGui::Button(I18n::tr("Publish"), ImVec2(120, 0))) answer(t, true);
        ImGui::SameLine();
        if (ImGui::Button(I18n::tr("Not now"), ImVec2(120, 0))) answer(t, false);
    }
    ImGui::End();
}

} // namespace ImagePublish
