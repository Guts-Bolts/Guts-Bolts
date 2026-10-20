// Catalog item pictures: real renders, never drawings. Each item is shown worn by a
// plain grey mannequin (clothes, faces, hats, hair, accessories), the mannequin mid-move
// (emotes, animation packs), or on its own (gear),
// drawn by the game's own renderer into a small picture once and kept.
//
// dressPlayer() is also what puts other people's outfits on their characters (profiles,
// friends), so what you see there is what they look like in a game.
#include "PlayerApp.h"
#include "../game/Profile.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../renderer/SceneRenderer.h"
#include "../scene/Emotes.h"
#include "../scene/Physics.h"
#include "../scene/Serializer.h"
#include "../core/Paths.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace {
// Is this picture / model here yet? ("gb:<id>" = from the server, else a file.)
bool haveSource(const std::string& src) {
    if (src.empty()) return true;
    if (src.rfind("gb:", 0) == 0) return !Paths::downloaded(src.substr(3)).empty();
    std::error_code ec;
    return std::filesystem::exists(src, ec);
}
void fetchSource(const std::string& src) {
    if (src.rfind("gb:", 0) == 0 && Paths::downloaded(src.substr(3)).empty()) Online::download(src.substr(3));
}
// What an item needs downloaded before it can be shown.
std::vector<std::string> sourcesOf(const Catalog::Item& it) {
    std::vector<std::string> s;
    if (!it.image.empty()) s.push_back(it.image);
    if (!it.model.empty()) s.push_back(it.model);
    if (it.type == Catalog::Type::Gear || Catalog::isMove(it.type)) s.push_back("gb:" + it.id);
    return s;
}
const glm::vec3 kGrey(0.80f, 0.81f, 0.83f), kGreyLegs(0.74f, 0.75f, 0.78f);
} // namespace

// Put items on a character the way a game does (Profile::applyTo), downloading what's
// missing. Returns true when everything was here.
bool PlayerApp::dressPlayer(Player& p, BodyColors bc, HatStyle hat, glm::vec3 hatTint,
                            const std::vector<Catalog::Item>& items) {
    Profile look;
    look.colors = bc;
    look.hat = hat;
    look.hatColor = hatTint;
    bool all = true;
    for (const Catalog::Item& it : items) {
        for (const std::string& src : sourcesOf(it))
            if (!haveSource(src)) { fetchSource(src); all = false; }
        switch (it.type) {
            case Catalog::Type::Shirt:
                if (it.image.empty()) look.colors.torso = look.colors.leftArm = look.colors.rightArm = it.color;
                look.shirtImage = it.image;
                break;
            case Catalog::Type::Pants:
                if (it.image.empty()) look.colors.leftLeg = look.colors.rightLeg = it.color;
                look.pantsImage = it.image;
                break;
            case Catalog::Type::TShirt: look.tshirtImage = it.image; break;
            case Catalog::Type::Face:   look.faceImage = it.image; break;
            case Catalog::Type::Bundle: look.headless = look.headless || it.headless; break;
            case Catalog::Type::Hat:
                if (!it.model.empty()) { look.hat = HatStyle::None; look.accessories["hat"] = it.model; }
                else { look.hat = it.hat; look.hatColor = it.color; }
                break;
            case Catalog::Type::Hair: case Catalog::Type::FaceAcc: case Catalog::Type::Neck:
            case Catalog::Type::Shoulder: case Catalog::Type::Waist:
                if (!it.model.empty()) look.accessories[it.kind] = it.model;
                break;
            default: break;   // gear isn't worn
        }
    }
    look.applyTo(p);
    return all;
}

// Draw an item's picture centred at `c`, `s` across: its render once it's made, and a
// plain card until then.
void PlayerApp::itemPicture(ImDrawList* dl, ImVec2 c, float s, const Catalog::Item& it) {
    std::string key = it.id + "|" + it.image + "|" + it.model + "|" + std::to_string((int)it.hat) + "|" +
                      std::to_string((int)(it.color.r * 255)) + "," + std::to_string((int)(it.color.g * 255)) + "," +
                      std::to_string((int)(it.color.b * 255));
    auto& e = m_itemRenders[key];
    if (!e.item) e.item = std::make_unique<Catalog::Item>(it);
    e.lastUsed = ImGui::GetTime();
    const ImVec2 a(c.x - s * 0.5f, c.y - s * 0.5f), b(c.x + s * 0.5f, c.y + s * 0.5f);
    if (e.fb && e.done) {
        dl->AddImage((ImTextureID)(intptr_t)e.fb->colorTexture(), a, b, ImVec2(0, 1), ImVec2(1, 0));
    } else {
        dl->AddRectFilled(a, b, IM_COL32(236, 238, 242, 255), 6);
        const float t = (float)ImGui::GetTime() * 4.0f;   // a little "loading" spinner
        dl->PathArcTo(c, s * 0.12f, t, t + 4.2f, 16);
        dl->PathStroke(IM_COL32(150, 160, 175, 255), 0, std::max(1.5f, s * 0.025f));
    }
}

// Make a few pictures each frame (they're small and kept), and forget unused ones.
void PlayerApp::updateItemRenders() {
    if (!m_renderer) return;
    const double now = ImGui::GetTime();
    int made = 0;
    for (auto it = m_itemRenders.begin(); it != m_itemRenders.end();) {
        auto& e = it->second;
        if (now - e.lastUsed > 120.0) { it = m_itemRenders.erase(it); continue; }   // not shown for 2 minutes
        if (!e.done && made < 2 && now >= e.retryAt) {
            if (renderItem(e)) e.done = true;
            else e.retryAt = now + 0.5;   // waiting for downloads
            ++made;
        }
        ++it;
    }
}

bool PlayerApp::renderItem(ItemRender& e) {
    const Catalog::Item& it = *e.item;
    if (e.firstTry == 0.0) e.firstTry = ImGui::GetTime();
    const bool giveUp = ImGui::GetTime() - e.firstTry > 15.0;   // draw what's here, after a while
    bool ready = true;
    for (const std::string& src : sourcesOf(it))
        if (!haveSource(src)) { fetchSource(src); ready = false; }
    for (const Catalog::Item& w : e.wearing)   // a saved outfit: everything in it
        for (const std::string& src : sourcesOf(w))
            if (!haveSource(src)) { fetchSource(src); ready = false; }
    if (!ready && !giveUp) return false;

    Scene scene;
    std::vector<SceneNode*> remove;
    for (auto& c : scene.root()->children)
        if (!scene.isProtected(c.get())) remove.push_back(c.get());
    for (auto* r : remove) scene.removeNode(r);
    Environment& env = scene.environment();
    env.fogEnabled = false;
    env.sunAzimuth = 70.0f;
    env.sunElevation = 40.0f;
    Camera cam;
    cam.fov = 35.0f;
    cam.yaw = 70.0f;
    cam.pitch = 8.0f;

    if (e.outfit) {
        // A saved outfit: its colours, hat and clothes on the character, the whole body.
        Player* p = scene.player();
        if (!p) return true;
        p->setSpawn({0, 0, 0});
        p->build();
        dressPlayer(*p, e.colors, e.hat, e.hatTint, e.wearing);
        cam.pivot = {0, 1.35f, 0};
        cam.distance = 7.2f;
    } else if (it.type == Catalog::Type::Gear) {
        // Gear on its own: the tool, framed to fit.
        if (Player* p = scene.player()) if (SceneNode* r = p->root()) r->visible = false;
        std::string text;
        if (auto file = Paths::downloaded(it.id); !file.empty()) { std::ifstream f(file, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); text = ss.str(); }
        nlohmann::json m = nlohmann::json::parse(text, nullptr, false);
        SceneNode* tool = nullptr;
        if (m.is_object() && m.contains("nodes") && m["nodes"].is_array() && !m["nodes"].empty())
            if (auto n = Serializer::nodeFromString(m["nodes"][0].dump(), true)) tool = scene.insert(std::move(n));
        glm::vec3 lo(1e9f), hi(-1e9f);
        scene.forEach([&](SceneNode* n) {
            if (!tool || !n->isPart()) return;
            for (const SceneNode* q = n; q; q = q->parent) if (q == tool) {
                AABB bb = Physics::worldBounds(n);
                lo = glm::min(lo, bb.min); hi = glm::max(hi, bb.max);
                break;
            }
        });
        if (lo.x > hi.x) { lo = glm::vec3(-1); hi = glm::vec3(1); }
        cam.pivot = (lo + hi) * 0.5f;
        cam.distance = std::max(1.5f, glm::length(hi - lo) * 1.9f);
        cam.pitch = 20.0f;
    } else {
        Player* p = scene.player();
        if (!p) return true;
        p->setSpawn({0, 0, 0});
        p->build();
        BodyColors bc;
        bc.head = bc.torso = bc.leftArm = bc.rightArm = kGrey;
        bc.leftLeg = bc.rightLeg = kGreyLegs;
        dressPlayer(*p, bc, HatStyle::None, glm::vec3(-1.0f), {it});
        // Emotes and animation packs: the mannequin mid-move.
        if (Catalog::isMove(it.type))
            if (auto file = Paths::downloaded(it.id); !file.empty()) {
                std::ifstream f(file, std::ios::binary);
                std::stringstream ss;
                ss << f.rdbuf();
                Anim::Clip clip;
                float t = 0.0f;
                if (Emotes::pictureClip(ss.str(), nullptr, clip, t)) Emotes::poseRig(p->root(), clip, t);
            }
        // Frame what the item is on: the head for hats / hair / faces, else the body.
        switch (it.type) {
            case Catalog::Type::Hat: case Catalog::Type::Hair: case Catalog::Type::Face: case Catalog::Type::FaceAcc:
                cam.pivot = {0, 2.35f, 0}; cam.distance = 3.4f; break;
            case Catalog::Type::Neck: case Catalog::Type::Shoulder:
                cam.pivot = {0, 1.95f, 0}; cam.distance = 4.4f; break;
            default:
                cam.pivot = {0, 1.35f, 0}; cam.distance = 7.2f; break;
        }
    }
    if (!e.fb) e.fb = std::make_unique<Framebuffer>();
    e.fb->resize(192, 192);
    cam.resize(192, 192);
    Online::fetchSounds(scene);   // (any other pictures on it)
    m_renderer->render(scene, cam, *e.fb, false);
    return true;
}
