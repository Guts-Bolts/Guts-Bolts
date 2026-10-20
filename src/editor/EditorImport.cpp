// Bringing files into Studio: File > Import, the Import button, and dragging
// files from the computer onto the window.
#include "Editor.h"
#include "ModelImport.h"
#include "PictureImport.h"
#include "panels/ViewportPanel.h"
#include "../core/AppWindow.h"
#include "../core/FileDialog.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../renderer/MeshLibrary.h"
#include "../scene/EditMesh.h"
#include "../scene/Physics.h"
#include "../scene/RobloxFile.h"
#include "../scene/Scene.h"
#include "../scripting/Luau.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

enum class FileType { Model, RobloxModel, Place, Picture, Sound, Script, Unknown };

FileType typeOf(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    if (ModelImport::isModelFile(path) || ModelImport::isOtherModelFile(path)) return FileType::Model;
    if (e == ".rbxm" || e == ".rbxmx") return FileType::RobloxModel;
    if (e == ".rbxl" || e == ".rbxlx" || e == ".gbscene") return FileType::Place;
    if (PictureImport::isPicture(path)) return FileType::Picture;
    if (e == ".wav" || e == ".mp3" || e == ".flac" || e == ".ogg" || e == ".oga") return FileType::Sound;
    if (e == ".lua" || e == ".luau") return FileType::Script;
    return FileType::Unknown;
}

// The side of a part that a point on it is on.
Face faceAt(const SceneNode& part, glm::vec3 worldPoint) {
    glm::vec3 p = glm::vec3(glm::inverse(part.worldMatrix()) * glm::vec4(worldPoint, 1.0f));
    glm::vec3 a = glm::abs(p);
    if (a.x >= a.y && a.x >= a.z) return p.x > 0 ? Face::Right : Face::Left;
    if (a.y >= a.z) return p.y > 0 ? Face::Top : Face::Bottom;
    return p.z > 0 ? Face::Back : Face::Front;
}

std::string stem(const std::string& path) {
    std::string s = fs::path(path).stem().string();
    return s.empty() ? "Imported" : s;
}

} // namespace

SceneNode* Editor::importModel(const std::string& path, glm::vec3 at, glm::vec3* size) {
    ModelImport::Result r;
    std::string err;
    if (!ModelImport::load(path, r, err)) {
        Log::error("Couldn't import " + fs::path(path).filename().string() + ": " + err + ".");
        return nullptr;
    }
    // One piece: just a mesh part. More: a Model holding them, like Roblox.
    SceneNode* holder = nullptr;
    if (r.pieces.size() > 1) {
        auto model = std::make_unique<SceneNode>(stem(path), NodeKind::Model);
        model->transform.position = at;
        holder = m_scene->insert(std::move(model), m_scene->root());
    }
    SceneNode* first = nullptr;
    for (ModelImport::Piece& piece : r.pieces) {
        auto part = std::make_unique<SceneNode>(r.pieces.size() > 1 ? piece.name : stem(path));
        part->color = piece.color;
        part->transparency = piece.transparency;
        part->anchored = true;
        MeshEdit::attach(*part, piece.mesh);
        part->transform.position = holder ? piece.center : at + piece.center;
        part->transform.scale = piece.size;
        SceneNode* p = m_scene->insert(std::move(part), holder ? holder : m_scene->root());
        if (!first) first = p;
    }
    SceneNode* result = holder ? holder : first;
    if (size) *size = r.size;
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(1);
    s << "Imported " << fs::path(path).filename().string() << ": " << r.pieces.size()
      << (r.pieces.size() == 1 ? " mesh part, " : " mesh parts, ") << r.faces << " faces, "
      << r.size.x << " x " << r.size.y << " x " << r.size.z << " studs.";
    Log::system(s.str());
    for (const std::string& n : r.notes) Log::warn(fs::path(path).filename().string() + ": " + n);
    return result;
}

void Editor::importFiles(const std::vector<std::string>& paths, ImVec2 mouse, bool dropped) {
    if (paths.empty()) return;
    if (m_playing) {
        Log::warn("Stop the playtest first, then drop or import files into your game.");
        return;
    }
    glm::vec3 at = spawnPoint();
    SceneNode* under = nullptr;
    bool overView = dropped && m_viewport->pointAt(mouse, at, under);
    if (!overView) under = nullptr;
    if (under && !canEdit(under)) under = nullptr;

    std::vector<SceneNode*> added;
    // Several things dropped at once go side by side, left to right as you look.
    glm::vec3 right = glm::cross(glm::normalize(at - m_viewport->cameraPosition() + glm::vec3(0, 1e-4f, 0)), glm::vec3(0, 1, 0));
    right.y = 0.0f;
    right = glm::length(right) > 1e-4f ? glm::normalize(right) : glm::vec3(1, 0, 0);
    float spread = 0.0f;
    for (const std::string& path : paths) {
        const std::string name = fs::path(path).filename().string();
        switch (typeOf(path)) {
        case FileType::Model: {
            // Peek at the size first, so it can sit next to the last one without overlapping.
            glm::vec3 size(0.0f);
            if (SceneNode* n = importModel(path, at, &size)) {
                float half = 0.5f * std::max(size.x, size.z);
                if (spread > 0.0f) spread += half;
                glm::vec3 shift = right * spread;
                n->transform.position += shift;
                added.push_back(n);
                spread += half + 2.0f;
            }
            break;
        }
        case FileType::RobloxModel: {
            RobloxFile::Report report;
            std::string err;
            auto got = RobloxFile::importModel(*m_scene, nullptr, path, report, err);
            if (got.empty()) { Log::error("Couldn't import " + name + ": " + err); break; }
            // Like Roblox Studio: the model lands sitting on whatever you dropped it on
            // (or in front of the camera), not wherever it was in the place it came from.
            AABB box{};
            bool any = false;
            std::vector<SceneNode*> stack(got.begin(), got.end());
            while (!stack.empty()) {
                SceneNode* k = stack.back(); stack.pop_back();
                if (k->isPart()) {
                    AABB b = Physics::worldBounds(k);
                    if (!any) box = b; else { box.min = glm::min(box.min, b.min); box.max = glm::max(box.max, b.max); }
                    any = true;
                }
                for (auto& c : k->children) stack.push_back(c.get());
            }
            if (any) {
                glm::vec3 d = glm::vec3(at.x - 0.5f * (box.min.x + box.max.x), at.y - box.min.y + 0.01f,
                                        at.z - 0.5f * (box.min.z + box.max.z)) + right * spread;
                for (SceneNode* n : got)
                    n->transform.position += n->parent ? glm::vec3(glm::inverse(n->parent->worldMatrix()) * glm::vec4(d, 0.0f)) : d;
                spread += std::max(box.max.x - box.min.x, box.max.z - box.min.z) + 2.0f;
            }
            added.insert(added.end(), got.begin(), got.end());
            Log::system("Inserted Roblox model " + name + ": " + report.summary());
            for (const std::string& n : report.notes) Log::warn("Roblox import: " + n);
            break;
        }
        case FileType::Place:
            // A whole game: it replaces what's open (after asking, if there are unsaved changes).
            if (m_dirty) { m_droppedGame = path; m_pending = Pending::OpenDropped; m_openDiscard = true; }
            else openFile(path);
            return;
        case FileType::Picture: {
            // .webp, .svg and friends become a PNG first, so every Player can show them.
            std::string err;
            const std::string pic = PictureImport::usable(path, err);
            if (pic.empty()) { Log::error("Couldn't import " + name + ": " + err + "."); break; }
            if (pic != path) Log::system("Saved " + name + " as a PNG: " + fs::path(pic).filename().string() + " in the Imported Pictures folder.");
            // Dropped on a part: a Decal on the side it landed on. Anywhere else: a sign
            // with the picture on it, facing you.
            auto d = std::make_unique<SceneNode>(stem(path), NodeKind::Decal);
            d->texture = Paths::relativeToGames(fs::path(pic));
            d->color = {1.0f, 1.0f, 1.0f};
            SceneNode* target = under && under->isPart() ? under : nullptr;
            if (target) {
                d->face = faceAt(*target, at);
                // A decal stretches over the whole side: on the ground or a huge wall
                // that would be one giant picture, so it gets a sign there instead.
                glm::vec3 sz = glm::abs(glm::vec3(target->worldMatrix() * glm::vec4(1, 1, 1, 0)));
                int ax = d->face == Face::Right || d->face == Face::Left ? 0 : (d->face == Face::Top || d->face == Face::Bottom ? 1 : 2);
                float area = 1.0f;
                for (int i = 0; i < 3; ++i) if (i != ax) area *= sz[i];
                if (area > 400.0f) target = nullptr;
            }
            if (!target) {
                SceneNode* sign = m_scene->addNode(stem(path) + " Sign", PrimitiveType::Cube, MeshLibrary::get(PrimitiveType::Cube));
                sign->transform.scale = {4.0f, 4.0f, 0.2f};
                sign->transform.position = at + right * (spread > 0.0f ? spread + 2.0f : 0.0f) + glm::vec3(0.0f, 2.0f, 0.0f);
                glm::vec3 toCam = m_viewport->cameraPosition() - sign->transform.position;
                sign->transform.rotation.y = glm::degrees(std::atan2(-toCam.x, -toCam.z));
                sign->color = {1.0f, 1.0f, 1.0f};
                d->face = Face::Front;
                target = sign;
                spread += spread > 0.0f ? 6.0f : 4.0f;
            }
            SceneNode* decal = m_scene->insert(std::move(d), target);
            added.push_back(target == under ? decal : target);
            Log::system("Added the picture " + name + (target == under ? " as a Decal on " + under->name + "." : " on a sign."));
            break;
        }
        case FileType::Sound: {
            auto s = std::make_unique<SceneNode>(stem(path), NodeKind::Sound);
            s->soundId = Paths::relativeToGames(fs::path(path));
            s->autoplay = false;
            SceneNode* parent = under ? under : m_scene->root();
            added.push_back(m_scene->insert(std::move(s), parent));
            Log::system("Added the sound " + name + (under ? " inside " + under->name : "") +
                        ". Tick Autoplay (or Looped for music) in Properties, or play it from a script.");
            break;
        }
        case FileType::Script: {
            std::ifstream f(fs::path(path), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            auto sc = std::make_unique<SceneNode>(stem(path), NodeKind::Script);
            std::vector<std::string> notes;
            sc->source = fs::path(path).extension() == ".luau" ? Luau::toLua(ss.str(), &notes) : ss.str();
            for (auto& n : notes) Log::warn(name + ": " + n);
            SceneNode* parent = under ? under : m_scene->root();
            added.push_back(m_scene->insert(std::move(sc), parent));
            Log::system("Added the script " + name + (under ? " inside " + under->name : "") + ".");
            break;
        }
        case FileType::Unknown: {
            std::string e = fs::path(path).extension().string();
            for (char& c : e) c = (char)std::tolower((unsigned char)c);
            std::string why = PictureImport::cantOpen(path);
            if (why.empty() && (e == ".m4a" || e == ".aac" || e == ".wma" || e == ".aif" || e == ".aiff" || e == ".opus" ||
                                e == ".mid" || e == ".midi"))
                why = "Studio can't play " + e + " sounds. Turn it into an .mp3 or .ogg first (Audacity, or any free online converter).";
            if (why.empty() && (e == ".mp4" || e == ".mov" || e == ".webm" || e == ".avi" || e == ".mkv"))
                why = "Studio can't play videos yet. A picture from the video, or its sound saved as an .mp3, can go in.";
            if (!why.empty()) { Log::warn(name + ": " + why); break; }
            Log::warn("Studio doesn't know what to do with " + name + ". It can take 3D models (" +
                      ModelImport::formatList() + "), Roblox files (.rbxm, .rbxl), games (.gbscene), "
                      "pictures (.png, .jpg, .webp, .svg, .gif, .bmp, .tga, .psd, .hdr), sounds (.wav, .mp3, .ogg, .flac) "
                      "and scripts (.lua, .luau).");
            break;
        }
        }
    }
    if (added.empty()) return;
    m_scene->deselect();
    for (SceneNode* n : added) m_scene->addToSelection(n);
    if (!dropped) m_viewport->focusSelected();   // from the Open window: show what came in
}

void Editor::importDialog() {
    if (m_playing) { Log::warn("Stop the playtest first, then import."); return; }
    if (!FileDialog::available()) { m_pending = Pending::Open; m_openOpen = true; return; }
    std::string pats = std::string(ModelImport::patterns()) + " *.rbxm *.rbxmx " + PictureImport::patterns() +
                       " *.wav *.mp3 *.ogg *.flac *.lua *.luau";
    std::string path = FileDialog::openAny("Import into your game", "Models, pictures, sounds and scripts", pats);
    if (!path.empty()) importFiles({path}, ImVec2(0, 0), false);
}

void Editor::testDrop(const std::string& files) {
    std::vector<std::string> paths;
    std::stringstream ss(files);
    std::string p;
    // The middle of the 3D view, where a real drop would most likely land
    // (or "@x,y;" first: that far across and down the window, 0 to 1).
    ImVec2 mouse = ImGui::GetMainViewport()->GetCenter();
    while (std::getline(ss, p, ';')) {
        float fx, fy;
        if (p.size() > 1 && p[0] == '@' && std::sscanf(p.c_str() + 1, "%f,%f", &fx, &fy) == 2)
            mouse = ImVec2(ImGui::GetMainViewport()->Size.x * fx, ImGui::GetMainViewport()->Size.y * fy);
        else if (!p.empty()) paths.push_back(p);
    }
    importFiles(paths, mouse, true);
    m_viewport->focusSelected();   // (tests: look at what came in)
}
