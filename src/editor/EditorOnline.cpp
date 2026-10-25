// Studio's online side: connecting to a Guts&Bolts server, publishing the
// game, and the Library (plugins and audio people uploaded).
#include "Editor.h"
#include "panels/ViewportPanel.h"
#include "Plugins.h"
#include "Thumbnailer.h"
#include "panels/ToolboxPanel.h"
#include "../core/Account.h"
#include "../core/Audio.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../game/Badges.h"
#include "../game/Bolts.h"
#include "../online/AssetCache.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"
#include "../scene/Physics.h"
#include "../scene/Player.h"
#include "../renderer/Textures.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace {

void byLine(const json& a) {
    ImGui::TextDisabled("by %s", a.value("creatorName", std::string("?")).c_str());
    if (a.value("creatorVerified", false)) { ImGui::SameLine(0, 3); Badges::check(); }
}

// A safe file name for an installed plugin.
std::string fileName(const std::string& name) {
    std::string out;
    for (char c : name) out += (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out.empty() ? "Plugin" : out;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

void onlineLine() {
    ImVec4 col = Online::online() ? ImVec4(0.4f, 0.85f, 0.45f, 1) : ImVec4(1.0f, 0.5f, 0.45f, 1);
    ImGui::TextColored(col, "%s", Online::configured() ? Online::statusText().c_str() : "Not connected to a server");
}

} // namespace

// ---------------------------------------------------------------------------
// File > Guts&Bolts Server...
// ---------------------------------------------------------------------------

void Editor::renderServerDialog() {
    if (m_openServer) { ImGui::OpenPopup("Guts&Bolts Server"); m_openServer = false; }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Guts&Bolts Server", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("Studio uses the official Guts&Bolts server for publishing games, plugins, audio, "
                       "Bolts and badges. You can still build and play-test without it.");
    ImGui::Spacing();
    if (ImGui::Button("Reconnect", ImVec2(120, 0))) { Online::connect(); m_marketLoaded = false; }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
    ImGui::Spacing();
    onlineLine();
    if (Online::online()) {
        const json& me = Online::me();
        ImGui::Text("Signed in as %s", me.value("name", std::string()).c_str());
        if (me.value("verified", false)) { ImGui::SameLine(0, 4); Badges::check(); }
        ImGui::SameLine();
        ImGui::TextDisabled("(%s Bolts)", Bolts::format(Online::bolts()).c_str());
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// File > Publish to Guts&Bolts...
// ---------------------------------------------------------------------------

void Editor::publishGame(const std::string& name, const std::string& description, bool update,
                         std::function<void(const json&)> done) {
    std::string data = Serializer::saveScene(*m_scene);
    std::string picture = m_viewport->snapshotPng(480, 270);   // for the game's card on the site
    json args = {{"name", name}, {"description", description}, {"data", Online::base64Encode(data)}};
    if (update) args["id"] = m_scene->info().publishedId;
    else args["kind"] = "game";
    m_onlineBusy = true;
    Online::request(update ? "update" : "upload", args, [this, picture, name, description, done](const json& r) {
        m_onlineBusy = false;
        if (r.value("ok", false)) {
            m_scene->info().publishedId = r["asset"].value("id", std::string());
            if (!picture.empty())
                Online::request("thumb.set", {{"id", m_scene->info().publishedId}, {"data", Online::base64Encode(picture)}},
                                [](const json& t) {
                    if (!t.value("ok", false)) Log::warn("The game's picture didn't upload: " + t.value("error", std::string()));
                }, 60);
            m_scene->info().title = name;
            m_scene->info().description = description;
            if (!m_path.empty()) saveFile(m_path, false);   // remember it's published (the server already has this version)
            Log::system("Published \"" + name + "\" to " + Online::serverInfo().value("name", std::string("the server")));
        }
        if (done) done(r);
    }, 120);
}

void Editor::renderPublishDialog() {
    if (m_openPublish) {
        ImGui::OpenPopup("Publish to Guts&Bolts");
        m_openPublish = false;
        m_publishName = m_scene->info().title;
        m_publishDesc = m_scene->info().description;
        m_publishMsg.clear();
    }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Publish to Guts&Bolts", nullptr, ImGuiWindowFlags_NoResize)) return;
    if (!Online::online()) {
        ImGui::TextWrapped("Publishing puts your game on a Guts&Bolts server, where everyone can find and play it "
                           "from the site's home page.");
        onlineLine();
        if (ImGui::Button("Pick a server...")) { ImGui::CloseCurrentPopup(); m_openServer = true; }
        ImGui::SameLine();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextWrapped("Everyone on %s will be able to find and play this game. Publishing games is free.",
                       Online::serverInfo().value("name", std::string("the server")).c_str());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("Name", &m_publishName);
    ImGui::InputTextMultiline("Description", &m_publishDesc, ImVec2(-90, 80));
    const std::string& published = m_scene->info().publishedId;
    auto send = [this](bool update) {
        m_publishMsg = "Publishing...";
        publishGame(m_publishName, m_publishDesc, update, [this](const json& r) {
            if (!r.value("ok", false)) { m_publishMsg = r.value("error", std::string("Publishing didn't work.")); return; }
            m_publishMsg = m_path.empty() ? "Published! It's on the website now. Save your game so Studio remembers it's published."
                                          : "Published! It's on the website now, and saving updates it there too.";
            // Old versions are kept: the game's Configure page on the website can put one back.
            if (r.value("version", 0LL) > 1)
                m_publishMsg += " This is version " + std::to_string(r.value("version", 0LL)) +
                                " (older ones are under Version history on the game's Configure page on the website).";
        });
    };
    ImGui::BeginDisabled(m_onlineBusy);
    if (!published.empty()) {
        if (ImGui::Button("Update published game", ImVec2(200, 30))) send(true);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Publish this as the newest version (the old ones are kept in the game's Version history on the website)");
        ImGui::SameLine();
        if (ImGui::Button("Publish as a new game", ImVec2(180, 30))) send(false);
    } else {
        if (ImGui::Button("Publish", ImVec2(140, 30))) send(false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
    if (!m_publishMsg.empty()) ImGui::TextWrapped("%s", m_publishMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// The plugin and audio Library: plugins and audio from the server
// ---------------------------------------------------------------------------

void Editor::renderPluginLibrary() {
    if (!m_showPluginLibrary) return;
    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Plugin & Audio Library", &m_showPluginLibrary)) { ImGui::End(); return; }
    onlineLine();
    if (!Online::online()) {
        if (ImGui::Button("Pick a server...")) m_openServer = true;
        ImGui::End();
        return;
    }
    auto load = [this]() {
        m_marketLoaded = true;
        Online::request("list", {{"kind", "plugin"}, {"query", m_marketQuery}}, [this](const json& r) {
            if (r.value("ok", false)) m_marketPlugins = r["assets"];
        });
        Online::request("list", {{"kind", "audio"}, {"query", m_marketQuery}}, [this](const json& r) {
            if (r.value("ok", false)) m_marketAudio = r["assets"];
        });
    };
    if (!m_marketLoaded) load();
    ImGui::SetNextItemWidth(260);
    if (ImGui::InputTextWithHint("##q", "Search", &m_marketQuery, ImGuiInputTextFlags_EnterReturnsTrue)) load();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) load();
    ImGui::SameLine();
    ImGui::TextDisabled("You have");
    ImGui::SameLine();
    Bolts::amount(Online::bolts());
    if (!m_marketMsg.empty()) ImGui::TextWrapped("%s", m_marketMsg.c_str());

    if (ImGui::BeginTabBar("##market")) {
        // --- Plugins ---
        if (ImGui::BeginTabItem("Plugins")) {
            if (m_marketPlugins.empty()) ImGui::TextDisabled("No plugins yet. Be the first - see the Publish tab!");
            for (size_t i = 0; i < m_marketPlugins.size(); ++i) {
                const json a = m_marketPlugins[i];
                std::string id = a.value("id", std::string()), name = a.value("name", std::string());
                ImGui::PushID((int)i);
                ImGui::Separator();
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                byLine(a);
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("%s", a.value("description", std::string()).c_str());
                ImGui::PopTextWrapPos();
                long long price = a.value("price", 0LL);
                bool installed = std::filesystem::exists(Plugins::folder() / (fileName(name) + ".lua"));
                bool owned = price == 0 || Online::owns(id) || a.value("creator", std::string()) == Account::id();
                if (!owned) { Bolts::amount(price); ImGui::SameLine(); }
                ImGui::BeginDisabled(m_onlineBusy);
                std::string label = installed ? "Reinstall" : owned ? "Install" : "Buy & install";
                if (ImGui::Button(label.c_str())) {
                    m_onlineBusy = true;
                    auto install = [this, id, name]() {
                        Online::download(id, [this, name](bool ok, const std::filesystem::path& file, const json& info) {
                            m_onlineBusy = false;
                            if (!ok) { m_marketMsg = info.value("error", std::string("Download failed.")); return; }
                            std::error_code ec;
                            std::filesystem::copy_file(file, Plugins::folder() / (fileName(name) + ".lua"),
                                                       std::filesystem::copy_options::overwrite_existing, ec);
                            m_plugins->reload();
                            m_marketMsg = "Installed " + name + " - look in the PLUGINS tab.";
                            m_ribbonTab = 4;
                        }, true);
                    };
                    if (owned) install();
                    else Online::request("buy", {{"id", id}}, [this, install](const json& r) {
                        if (r.value("ok", false)) install();
                        else { m_onlineBusy = false; m_marketMsg = r.value("error", std::string()); }
                    });
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        // --- Audio ---
        if (ImGui::BeginTabItem("Audio")) {
            if (m_marketAudio.empty()) ImGui::TextDisabled("No audio yet. Upload some on the site's Create page!");
            for (size_t i = 0; i < m_marketAudio.size(); ++i) {
                const json a = m_marketAudio[i];
                std::string id = a.value("id", std::string()), name = a.value("name", std::string());
                ImGui::PushID((int)i);
                ImGui::Separator();
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                byLine(a);
                long long price = a.value("price", 0LL);
                bool owned = price == 0 || Online::owns(id) || a.value("creator", std::string()) == Account::id();
                if (!owned) { Bolts::amount(price); ImGui::SameLine(); }
                if (ImGui::SmallButton("Preview")) {
                    Online::download(id, [this, id](bool ok, const std::filesystem::path&, const json& info) {
                        if (ok) Audio::play("gb:" + id, 0.8f, 1.0f, false);
                        else m_marketMsg = info.value("error", std::string());
                    });
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(owned ? "Insert" : "Buy & insert")) {
                    auto insert = [this, id, name]() {
                        Online::download(id);
                        SceneNode* parent = m_scene->selected();
                        if (parent && !parent->isPart()) parent = nullptr;
                        auto s = std::make_unique<SceneNode>(name.empty() ? "Sound" : name, NodeKind::Sound);
                        s->soundId = "gb:" + id;
                        SceneNode* raw = m_scene->insert(std::move(s), parent);
                        m_scene->select(raw);
                        m_marketMsg = "Added the sound \"" + name + "\". Scripts can play it with :Play().";
                    };
                    if (owned) insert();
                    else Online::request("buy", {{"id", id}}, [this, insert](const json& r) {
                        if (r.value("ok", false)) insert();
                        else m_marketMsg = r.value("error", std::string());
                    });
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        // --- Publish a plugin ---
        if (ImGui::BeginTabItem("Publish a plugin")) {
            auto& list = m_plugins->list();
            if (list.empty()) {
                ImGui::TextWrapped("Put a .lua plugin in %s first.", Plugins::folder().string().c_str());
            } else {
                m_publishPluginIndex = std::clamp(m_publishPluginIndex, 0, (int)list.size() - 1);
                if (ImGui::BeginCombo("Plugin", list[m_publishPluginIndex]->name.c_str())) {
                    for (int k = 0; k < (int)list.size(); ++k)
                        if (ImGui::Selectable(list[k]->name.c_str(), k == m_publishPluginIndex)) m_publishPluginIndex = k;
                    ImGui::EndCombo();
                }
                const bool verified = Online::verified();
                if (verified) {
                    ImGui::SetNextItemWidth(160);
                    ImGui::InputInt("Price (Bolts)", &m_publishPluginPrice, 5, 50);
                    m_publishPluginPrice = std::clamp(m_publishPluginPrice, 0, 1000000);
                }
                ImGui::TextDisabled(verified ? "You're Verified: publishing is free, and you can sell it."
                                             : "Publishing a plugin costs 20 Bolts (free for Verified creators).");
                ImGui::BeginDisabled(m_onlineBusy);
                if (ImGui::Button("Publish plugin")) {
                    auto& p = *list[m_publishPluginIndex];
                    m_onlineBusy = true;
                    Online::request("upload", {{"kind", "plugin"}, {"name", p.name},
                                               {"price", verified ? m_publishPluginPrice : 0},
                                               {"data", Online::base64Encode(readFile(p.file))}},
                                    [this](const json& r) {
                        m_onlineBusy = false;
                        m_marketMsg = r.value("ok", false) ? "Published! Others can install it from the Library."
                                                           : r.value("error", std::string());
                        m_marketLoaded = false;
                    });
                }
                ImGui::EndDisabled();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Editor::testSnapshot(const std::string& file) {
    std::string png = m_viewport->snapshotPng(480, 270);
    std::ofstream(file, std::ios::binary).write(png.data(), (std::streamsize)png.size());
    Log::system("Snapshot: " + std::to_string(png.size()) + " bytes to " + file);
}

// ---------------------------------------------------------------------------
// File > Publish Selection to Library: share objects as a model, public or private
// ---------------------------------------------------------------------------

// The right-click menus' way in (Explorer and viewport): what's selected, as a model.
void Editor::publishMenuItem() {
    ImGui::Separator();
    if (ImGui::MenuItem("Publish to Guts&Bolts...", nullptr, false, m_scene->selected() != nullptr))
        m_openPublishModel = true;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Put the selection in the Library as a model, public (anyone can use it) or private (only you).");
}

void Editor::renderPublishModelDialog() {
    if (m_openPublishModel) {
        ImGui::OpenPopup("Publish to Library");
        m_openPublishModel = false;
        SceneNode* sel = m_scene->selected();
        m_modelName = sel ? sel->name : "Model";
        m_modelDesc.clear();
        m_modelMsg.clear();
        m_modelAsGear = m_openPublishGear;   // (from Make Gear: already ticked)
        m_openPublishGear = false;
    }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Publish to Library", nullptr, ImGuiWindowFlags_NoResize)) return;
    std::vector<SceneNode*> items;
    for (SceneNode* n : m_scene->selectionRoots())
        if (n != m_scene->root() && !m_scene->isCharacterPart(n)) items.push_back(n);
    if (!Online::online()) {
        onlineLine();
    } else if (items.empty()) {
        ImGui::TextWrapped("Select the objects you want to share first (a Model, some parts, a scripted tool...).");
    } else {
        ImGui::TextWrapped("Put %d selected object%s in the Guts&Bolts Library so you (or everyone) can insert "
                           "them from the Toolbox.", (int)items.size(), items.size() == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("Name", &m_modelName);
        ImGui::InputTextMultiline("Description", &m_modelDesc, ImVec2(-90, 60));
        ImGui::Spacing();
        const bool canGear = items.size() == 1 && items[0]->isTool() && Online::staff();
        if (!canGear) m_modelAsGear = false;
        if (canGear) {   // like Roblox's old gear: a Tool people buy and bring into games that allow gear
            ImGui::Checkbox("Sell it in the catalog as Gear", &m_modelAsGear);
            if (m_modelAsGear) {
                ImGui::SetNextItemWidth(120);
                ImGui::InputInt("Price in Bolts (0 = free)", &m_gearPrice, 10, 100);
                m_gearPrice = std::clamp(m_gearPrice, 0, 1000000);
                ImGui::TextDisabled("People equip it on their Avatar page and get it in games that allow gear.");
            }
        }
        if (!m_modelAsGear) {
        ImGui::TextUnformatted("Who can find it?");
        if (ImGui::RadioButton("Public: everyone can find and use it", m_modelPublic)) m_modelPublic = true;
        if (ImGui::RadioButton("Private: only you", !m_modelPublic)) m_modelPublic = false;
        }
        int left = m_modelAsGear ? -1 : Online::me().value("publicModelsLeft", -1);
        if (left >= 0)
            ImGui::TextDisabled("You can make %d more model%s public this week (Verified creators have no limit).",
                                left, left == 1 ? "" : "s");
        ImGui::Spacing();
        ImGui::BeginDisabled(m_onlineBusy || m_modelName.empty());
        if (ImGui::Button("Publish", ImVec2(140, 30))) {
            // The objects, and a picture framed on them for the Library.
            json nodes = json::array();
            for (SceneNode* n : items) nodes.push_back(json::parse(Serializer::nodeToString(*n)));
            // A picture of just these objects (not the map around them) for the Library.
            std::string picture = m_thumbnailer->png(std::vector<const SceneNode*>(items.begin(), items.end()), 256);
            json model = {{"format", "gbmodel"}, {"version", 1}, {"nodes", nodes}};
            json args = {{"kind", "model"}, {"name", m_modelName}, {"description", m_modelDesc},
                         {"access", m_modelPublic ? "public" : "private"}, {"data", Online::base64Encode(model.dump())}};
            const bool gear = m_modelAsGear;
            if (gear) { args["kind"] = "gear"; args["price"] = m_gearPrice; args.erase("access"); }
            m_onlineBusy = true;
            m_modelMsg = "Publishing...";
            // Pictures on its parts that only live on this computer go up first (as decals).
            uploadLocalTextures(model, m_modelName, [this, args, picture, gear](json withPics, std::string error) mutable {
            if (!error.empty()) { m_onlineBusy = false; m_modelMsg = error; return; }
            args["data"] = Online::base64Encode(withPics.dump());
            Online::request("upload", args, [this, picture, gear](const json& r) {
                m_onlineBusy = false;
                if (!r.value("ok", false)) { m_modelMsg = r.value("error", std::string("Publishing didn't work.")); return; }
                std::string id = r["asset"].value("id", std::string());
                // (Gear in the catalog is drawn from the item itself, so it takes no picture.)
                if (!gear && !picture.empty()) Online::request("thumb.set", {{"id", id}, {"data", Online::base64Encode(picture)}}, nullptr, 60);
                m_modelMsg = gear ? "Published! It's in the catalog under Gear." : "Published! Find it in the Toolbox's Library.";
                m_libraryLoaded = false;
                Online::connect();   // refresh "public models left"
            }, 120);
            });
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    if (ImGui::Button("Close", ImVec2(90, 30))) ImGui::CloseCurrentPopup();
    if (!m_modelMsg.empty()) ImGui::TextWrapped("%s", m_modelMsg.c_str());
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// File > Make Gear (staff): gear is a Tool people buy in the catalog and bring into
// games that allow gear, like Roblox's classic gear. Start from a ready-made one (or
// your own Tool), change it however you like, test it with Play, then publish it.
// ---------------------------------------------------------------------------

void Editor::fixItemPictures() {
    const std::string mine = Online::me().value("id", std::string());
    if (mine.empty()) { Log::warn("Sign in first."); return; }
    Log::system("Looking for your items whose pictures are only on this computer...");
    auto fixOne = [this](const json& a) {
        const std::string id = a.value("id", std::string()), name = a.value("name", std::string());
        Online::download(id, [this, id, name](bool ok, const std::filesystem::path& file, const json&) {
            if (!ok) return;
            std::ifstream f(file, std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            json model = json::parse(text, nullptr, false);
            if (!model.is_object()) return;
            const std::string before = model.dump();
            uploadLocalTextures(model, name, [id, name, before](json fixed, std::string error) {
                if (!error.empty()) { Log::warn(name + ": " + error); return; }
                if (fixed.dump() == before) return;   // nothing local in it
                Online::request("update", {{"id", id}, {"data", Online::base64Encode(fixed.dump())}}, [name](const json& r) {
                    if (r.value("ok", false)) Log::info("Uploaded the pictures for \"" + name + "\". Everyone sees them now.");
                    else Log::warn(name + ": " + r.value("error", std::string("couldn't update it.")));
                });
            });
        }, true);
    };
    for (const char* kind : {"clothing", "gear"})
        Online::request("list", {{"kind", kind}, {"creator", mine}, {"limit", 200}}, [fixOne](const json& r) {
            if (!r.value("ok", false)) return;
            for (const auto& a : r.value("assets", json::array()))
                if (a.value("kind", std::string()) == "gear" || (a.contains("meta") && a["meta"].value("model", false))) fixOne(a);
        });
}

void Editor::uploadLocalTextures(json node, const std::string& name, std::function<void(json, std::string)> done) {
    // Every local picture used (once each).
    auto local = [](const std::string& t) -> std::filesystem::path {
        if (t.empty() || t.rfind("gb:", 0) == 0 || t.rfind("builtin:", 0) == 0 || t.find("://") != std::string::npos) return {};
        std::error_code ec;
        std::filesystem::path p(t);
        if (std::filesystem::is_regular_file(p, ec)) return p;
        p = Paths::gamesFolder() / t;
        return std::filesystem::is_regular_file(p, ec) ? p : std::filesystem::path();
    };
    std::vector<std::string> todo;
    std::function<void(const json&)> find = [&](const json& n) {
        if (!n.is_object()) return;
        if (n.contains("texture") && n["texture"].is_string() && !local(n["texture"].get<std::string>()).empty())
            if (std::find(todo.begin(), todo.end(), n["texture"].get<std::string>()) == todo.end()) todo.push_back(n["texture"].get<std::string>());
        if (n.contains("children")) for (const auto& c : n["children"]) find(c);
        if (n.contains("nodes")) for (const auto& c : n["nodes"]) find(c);
        if (n.contains("node")) find(n["node"]);   // an accessory file: {"format":"gbaccessory","node":{...}}
        for (const char* k : {"particles", "effect"}) if (n.contains(k)) find(n[k]);   // a ParticleEmitter's, Trail's or Beam's picture
    };
    find(node);
    if (todo.empty()) { done(node, ""); return; }
    // One at a time, then swap them all in.
    struct State { json node; std::vector<std::string> todo; std::map<std::string, std::string> refs; size_t at = 0; };
    auto st = std::make_shared<State>();
    st->node = std::move(node);
    st->todo = std::move(todo);
    auto step = std::make_shared<std::function<void()>>();
    *step = [this, st, step, local, name, done]() {
        if (st->at >= st->todo.size()) {
            std::function<void(json&)> swap = [&](json& n) {
                if (!n.is_object()) return;
                if (n.contains("texture") && n["texture"].is_string())
                    if (auto it = st->refs.find(n["texture"].get<std::string>()); it != st->refs.end()) n["texture"] = it->second;
                if (n.contains("children")) for (auto& c : n["children"]) swap(c);
                if (n.contains("nodes")) for (auto& c : n["nodes"]) swap(c);
                if (n.contains("node")) swap(n["node"]);
                for (const char* k : {"particles", "effect"}) if (n.contains(k)) swap(n[k]);
            };
            swap(st->node);
            done(st->node, "");
            return;
        }
        const std::string path = st->todo[st->at];
        std::ifstream f(local(path), std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        json args = {{"kind", "decal"}, {"name", (name.empty() ? std::string("Texture") : name + " texture").substr(0, 50)},
                     {"description", "A picture used by " + name + "."}, {"data", Online::base64Encode(bytes)}};
        Online::request("upload", args, [this, st, step, path, done](const json& r) {
            if (!r.value("ok", false)) { done(json(), "Couldn't upload a picture it uses: " + r.value("error", std::string())); return; }
            st->refs[path] = "gb:" + r["asset"].value("id", std::string());
            if (r["asset"].value("review", std::string()) == "pending")
                Log::info("\"" + r["asset"].value("name", std::string()) + "\" shows up for other players once staff check it "
                          "(Verified creators skip this).");
            ++st->at;
            (*step)();
        }, 120);
    };
    (*step)();
}

void Editor::renderMakeGearDialog() {
    if (m_openMakeGear) { ImGui::OpenPopup("Make Gear"); m_openMakeGear = false; }
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    if (!ImGui::BeginPopupModal("Make Gear", nullptr, ImGuiWindowFlags_NoResize)) return;
    ImGui::TextWrapped("Gear is a Tool people buy in the catalog. They equip it on their Avatar page and get it in "
                       "every game whose creator ticked Allow gear.");
    ImGui::Spacing();
    ImGui::SeparatorText("1. Start from");
    int k = 0;
    for (const PremadeInfo& p : premadeList()) {
        if (!isGearPremade(p.kind)) continue;
        if (k++ % 3) ImGui::SameLine();
        if (ImGui::Button(p.name, ImVec2(160, 30))) spawnPremade(p.kind);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.tip);
    }
    ImGui::TextDisabled("...or build your own: Insert Object > Tool (it comes with a Handle to hold).");
    ImGui::SeparatorText("2. Make it yours");
    ImGui::TextWrapped("Change the parts, colours and the script inside it. Press Play to try it: it's in your "
                       "backpack, so press 1 to hold it and click to use it.");
    ImGui::SeparatorText("3. Sell it");
    SceneNode* sel = m_scene->selected();
    while (sel && !sel->isTool()) sel = sel->parent;   // (a part inside the tool counts)
    if (!sel) {
        ImGui::TextDisabled("Select the Tool to publish it.");
    } else {
        ImGui::Text("Selected: %s", sel->name.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Publish as Gear...")) {
            m_scene->select(sel);
            m_openPublishGear = true;
            m_openPublishModel = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(90, 28))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// The Toolbox's Library: everyone's public models, decals and audio, with pictures
// ---------------------------------------------------------------------------

std::vector<ToolboxTile> Editor::libraryTiles(bool mine, int kind, const std::string& query, bool bestRated, bool reload, std::string& status) {
    static const char* kinds[] = {"model", "decal", "audio"};
    std::vector<ToolboxTile> out;
    kind = std::clamp(kind, 0, 2);
    if (!Online::online()) { status = "Connecting to the Library..."; return out; }
    const std::string myId = Online::me().value("id", std::string());
    if (mine && myId.empty()) { status = "Log in to see your own models, decals and audio."; return out; }
    bestRated = bestRated && !mine && kind == 0;   // (only models have thumbs up / down)
    std::string key = std::string(mine ? "mine:" : "all:") + kinds[kind] + (bestRated ? ":rated:" : ":") + query;
    if (reload || key != m_libraryKey || !m_libraryLoaded) {
        m_libraryKey = key;
        m_libraryLoaded = true;
        m_library = json::array();
        m_libraryMsg = "Loading...";
        json args = {{"kind", kinds[kind]}, {"query", query}, {"limit", 60}};
        if (mine) args["creator"] = myId;
        else args["sort"] = bestRated ? "rated" : "popular";
        Online::request("list", args, [this, key](const json& r) {
            if (key != m_libraryKey) return;   // an older search
            if (r.value("ok", false)) { m_library = r["assets"]; m_libraryMsg.clear(); }
            else m_libraryMsg = r.value("error", std::string("The Library didn't load."));
        });
    }
    status = m_libraryMsg;

    // One downloaded model a frame gets its picture taken (older uploads have none).
    if (!m_thumbJobs.empty()) {
        json job = m_thumbJobs.front();
        m_thumbJobs.erase(m_thumbJobs.begin());
        std::ifstream in(job.value("file", std::string()), std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        json model = json::parse(ss.str(), nullptr, false);
        std::string id = job.value("id", std::string());
        if (model.is_object() && model.contains("nodes")) {
            std::string png = m_thumbnailer->png([&](Scene& scene) {
                for (const json& nj : model["nodes"])
                    if (auto n = Serializer::nodeFromString(nj.dump(), true)) scene.insert(std::move(n));
            }, 256);
            std::filesystem::path f = Paths::downloadsFolder() / ("thumb-" + id + ".png");
            std::ofstream(f, std::ios::binary).write(png.data(), (std::streamsize)png.size());
            m_libraryThumbs[id] = f.string();
            // Your own (or, for staff, anyone's): the website and everyone else get the picture too.
            if (job.value("creator", std::string()) == myId || Online::staff())
                Online::request("thumb.set", {{"id", id}, {"data", Online::base64Encode(png)}}, nullptr, 60);
        }
    }

    for (const json& a : m_library) {
        ToolboxTile t;
        std::string k = a.value("kind", std::string());
        t.key = "gb:" + a.value("id", std::string());
        t.name = a.value("name", std::string("?"));
        t.creator = a.value("creatorName", std::string("?"));
        t.official = a.value("creatorStaff", false);
        t.icon = k == "decal" ? Icons::Id::Decal : k == "audio" ? Icons::Id::Sound : Icons::Id::Model;
        std::string access = a.value("access", std::string("public"));
        t.tip = k == "model" ? (mine ? "Click to insert it." : "Click to insert it. Once you have, you can rate it on its Library page on the website.")
              : k == "decal" ? "Click to put it on the selected part."
                                                              : "Click to add it as a Sound.";
        if (mine && access == "private") t.tip += "\n(Private: only you can see it.)";
        if (k == "model") {   // thumbs up / down (people rate it on its Library page after inserting it)
            const long long up = a.value("likes", 0LL), votes = up + a.value("dislikes", 0LL);
            if (votes > 0) {
                t.rating = std::to_string((up * 100 + votes / 2) / votes) + "%";
                t.ratingTip = t.rating + " liked (" + std::to_string(votes) + (votes == 1 ? " vote)" : " votes)");
            } else t.ratingTip = "No ratings yet";
        }
        t.picture = [this, a]() { return libraryPicture(a); };
        t.use = [this, a]() { useLibraryAsset(a); };
        out.push_back(std::move(t));
    }
    return out;
}

// A picture for a Library item: models have a snapshot (or get one made here),
// decals are pictures themselves, and sounds show a speaker.
unsigned Editor::libraryPicture(const json& a) {
    std::string id = a.value("id", std::string()), kind = a.value("kind", std::string());
    auto it = m_libraryThumbs.find(id);
    if (it != m_libraryThumbs.end()) return it->second.empty() ? 0 : Textures::get(it->second);
    m_libraryThumbs[id] = "";
    if (kind == "decal") {
        Online::download(id, [this, id](bool ok, const std::filesystem::path& f, const json&) { if (ok) m_libraryThumbs[id] = f.string(); });
    } else if (kind == "model" && a.value("thumb", 0LL) > 0) {
        Online::request("thumb.get", {{"id", id}}, [this, id](const json& r) {
            std::string bytes;
            if (!r.value("ok", false) || !Online::base64Decode(r.value("data", std::string()), bytes) || bytes.empty()) return;
            std::filesystem::path f = Paths::downloadsFolder() / ("thumb-" + id + ".png");
            std::ofstream(f, std::ios::binary).write(bytes.data(), (std::streamsize)bytes.size());
            m_libraryThumbs[id] = f.string();
        });
    } else if (kind == "model") {
        // No picture yet: get the model and take one.
        std::string creator = a.value("creator", std::string());
        Online::download(id, [this, id, creator](bool ok, const std::filesystem::path& f, const json&) {
            if (ok) m_thumbJobs.push_back({{"id", id}, {"file", f.string()}, {"creator", creator}});
        });
    }
    return 0;
}

void Editor::useLibraryAsset(const json& a) {
    std::string id = a.value("id", std::string()), name = a.value("name", std::string()), kind = a.value("kind", std::string());
    if (kind == "model") {
        m_libraryMsg = "Getting " + name + "...";
        Online::download(id, [this, name](bool ok, const std::filesystem::path& f, const json& info) {
            if (!ok) { m_libraryMsg = info.value("error", std::string("Download failed.")); return; }
            std::ifstream in(f, std::ios::binary);
            std::stringstream ss; ss << in.rdbuf();
            json model = json::parse(ss.str(), nullptr, false);
            if (!model.is_object() || !model.contains("nodes")) { m_libraryMsg = "That model looks broken."; return; }
            m_scene->deselect();
            for (const json& nj : model["nodes"]) {
                auto n = Serializer::nodeFromString(nj.dump(), true);
                if (n) m_scene->addToSelection(m_scene->insert(std::move(n), m_scene->root()));
            }
            m_libraryMsg = "Inserted " + name + ".";
            Online::fetchSounds(*m_scene);   // its decals and sounds
        });
        return;
    }
    Online::download(id);
    SceneNode* parent = m_scene->selected();
    if (parent && !parent->isPart()) parent = nullptr;
    if (kind == "decal" && !parent) { m_libraryMsg = "Select a part first, then click the decal."; return; }
    auto n = std::make_unique<SceneNode>(name.empty() ? (kind == "decal" ? "Decal" : "Sound") : name,
                                         kind == "decal" ? NodeKind::Decal : NodeKind::Sound);
    if (kind == "decal") n->texture = "gb:" + id;
    else n->soundId = "gb:" + id;
    m_scene->insert(std::move(n), parent);
    m_libraryMsg = "Added " + name + ".";
}

// ---------------------------------------------------------------------------
// AVATAR > Accessories: make hats and other accessories (Verified creators)
// ---------------------------------------------------------------------------
//
// Put a Model on the mannequin where it should sit, press Save position (it's
// remembered on the Model, so it survives saving the place), then Upload: the
// catalog item carries the Model and its spot, relative to the character's feet.

namespace {
struct AccessoryKind { const char* key; const char* title; const char* where; glm::vec3 spot; };
const AccessoryKind kAccessoryKinds[] = {
    {"hat", "Hat", "on top of the head", {0.0f, 2.75f, 0.0f}},
    {"hair", "Hair", "on the head", {0.0f, 2.55f, -0.05f}},
    {"faceacc", "Face accessory", "on the front of the face", {0.0f, 2.35f, 0.4f}},
    {"neck", "Neck accessory", "around the neck", {0.0f, 2.0f, 0.1f}},
    {"shoulder", "Shoulder accessory", "on a shoulder", {-0.75f, 2.05f, 0.0f}},
    {"waist", "Waist accessory", "around the waist", {0.0f, 1.05f, 0.0f}},
};

// The transform that puts `m` (a world matrix) in `root`'s space (rotation order Z*Y*X).
Transform relativeTo(const glm::mat4& root, const glm::mat4& m) {
    glm::mat4 r = glm::inverse(root) * m;
    Transform t;
    t.position = glm::vec3(r[3]);
    for (int i = 0; i < 3; ++i) t.scale[i] = glm::length(glm::vec3(r[i]));
    glm::mat3 R(glm::vec3(r[0]) / t.scale.x, glm::vec3(r[1]) / t.scale.y, glm::vec3(r[2]) / t.scale.z);
    float sy = std::clamp(-R[0][2], -1.0f, 1.0f);
    t.rotation.y = glm::degrees(std::asin(sy));
    if (std::abs(sy) < 0.9999f) {
        t.rotation.x = glm::degrees(std::atan2(R[1][2], R[2][2]));
        t.rotation.z = glm::degrees(std::atan2(R[0][1], R[0][0]));
    } else {   // straight up or down: put all the turn in X
        t.rotation.x = glm::degrees(std::atan2(-R[2][1], R[1][1]));
        t.rotation.z = 0.0f;
    }
    return t;
}

void setString(SceneNode& n, const std::string& name, const std::string& value) {
    for (auto& a : n.attributes) if (a.name == name) { a.type = Attribute::String; a.s = value; return; }
    Attribute a; a.name = name; a.type = Attribute::String; a.s = value;
    n.attributes.push_back(a);
}
} // namespace

void Editor::renderAccessoryWindow() {
    if (!m_showAccessory) return;
    // Opens over the right of the viewport, big enough to see every step.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 550, vp->WorkPos.y + 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 650), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Accessories", &m_showAccessory)) { ImGui::End(); return; }
    onlineLine();
    const bool allowed = Online::online() && (Online::verified() || Online::staff());
    if (!allowed) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("Hats and accessories are made by Verified creators. Get Verified to make your own!");
        ImGui::PopTextWrapPos();
        ImGui::End();
        return;
    }
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("1. Add the mannequin.  2. Build your accessory as a Model and select it.  "
                        "3. Move it into place on the mannequin and press Save position.  4. Upload it to the catalog.");
    ImGui::PopTextWrapPos();

    // 1. The mannequin.
    SceneNode* mannequin = m_scene->root()->findChild("Accessory Mannequin");
    ImGui::SeparatorText("Mannequin");
    if (!mannequin) {
        if (ImGui::Button("Add mannequin")) {
            SceneNode* rig = Player::buildRig(*m_scene, "Accessory Mannequin", glm::vec3(0.0f));
            m_scene->select(rig);
            Log::system("Added the accessory mannequin. Build your accessory next to it.");
        }
    } else {
        ImGui::TextDisabled("\"Accessory Mannequin\" is in the Workspace.");
    }

    // Load an existing catalog accessory so it can be repaired in place.
    ImGui::SeparatorText("Edit an existing catalog item");
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("Catalog item ID", "Paste the item's ID", &m_accessoryEditId);
    ImGui::SameLine();
    if (ImGui::Button("Load item") && !m_accessoryEditId.empty() && !m_onlineBusy) {
        const std::string id = m_accessoryEditId;
        m_onlineBusy = true;
        m_accessoryMsg = "Loading catalog item...";
        Online::request("get", {{"id", id}}, [this, id](const json& r) {
            m_onlineBusy = false;
            if (!r.value("ok", false)) { m_accessoryMsg = r.value("error", std::string("Couldn't load that item.")); return; }
            const json asset = r.value("asset", json::object());
            const std::string kind = asset.value("kind", std::string());
            if (kind != "hat" && kind != "hair" && kind != "faceacc" &&
                kind != "neck" && kind != "shoulder" && kind != "waist") {
                m_accessoryMsg = "That item isn't a 3D accessory supported by this editor.";
                return;
            }
            std::string bytes;
            if (!Online::base64Decode(r.value("data", std::string()), bytes)) {
                m_accessoryMsg = "The catalog item's data could not be decoded.";
                return;
            }
            json acc = json::parse(bytes, nullptr, false);
            if (!acc.is_object() || acc.value("format", std::string()) != "gbaccessory" ||
                !acc.contains("node") || !acc["node"].is_object()) {
                m_accessoryMsg = "That item doesn't contain an editable Studio accessory model.";
                return;
            }
            auto node = Serializer::nodeFromString(acc["node"].dump(), true);
            if (!node || node->kind != NodeKind::Model) {
                m_accessoryMsg = "The accessory model couldn't be opened.";
                return;
            }
            const json raw = acc["node"];
            auto vec3 = [](const json& value, glm::vec3 fallback) {
                if (!value.is_array() || value.size() != 3) return fallback;
                try { return glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()); }
                catch (...) { return fallback; }
            };
            const glm::vec3 pos = vec3(raw.value("pos", json()), glm::vec3(0.0f));
            const glm::vec3 rot = vec3(raw.value("rot", json()), glm::vec3(0.0f));
            const glm::vec3 scale = vec3(raw.value("size", json()), glm::vec3(1.0f));
            json placement = {{"kind", kind}, {"position", {pos.x, pos.y, pos.z}},
                              {"rotation", {rot.x, rot.y, rot.z}}, {"scale", {scale.x, scale.y, scale.z}}};
            setString(*node, "AccessoryPlacement", placement.dump());
            setString(*node, "CatalogItemId", id);
            setString(*node, "CatalogItemKind", kind);
            m_accessoryName = asset.value("name", node->name);
            m_accessoryDesc = asset.value("description", std::string());
            m_accessoryPrice = (int)std::clamp((long long)asset.value("price", 0LL), 0LL, 1000000LL);
            for (int i = 0; i < (int)std::size(kAccessoryKinds); ++i)
                if (kind == kAccessoryKinds[i].key) { m_accessoryKind = i; break; }
            SceneNode* loaded = m_scene->insert(std::move(node), m_scene->root());
            if (loaded) m_scene->select(loaded);
            m_scene->markDirty();
            m_accessoryMsg = "Loaded item for editing. Change its placement or mesh, then choose Update existing item.";
        }, 120);
    }
    ImGui::TextDisabled("The server checks the original creator before accepting an update.");

    // 2. What kind, and which Model.
    ImGui::SeparatorText("Accessory");
    ImGui::SetNextItemWidth(220);
    if (ImGui::BeginCombo("Type", kAccessoryKinds[m_accessoryKind].title)) {
        for (int i = 0; i < (int)std::size(kAccessoryKinds); ++i)
            if (ImGui::Selectable(kAccessoryKinds[i].title, i == m_accessoryKind)) m_accessoryKind = i;
        ImGui::EndCombo();
    }
    const AccessoryKind& kind = kAccessoryKinds[m_accessoryKind];
    SceneNode* model = m_scene->selected();
    while (model && model->kind != NodeKind::Model && model->parent && model->parent != m_scene->root()) model = model->parent;
    if (model && (model->kind != NodeKind::Model || model == mannequin || m_scene->isCharacterPart(model))) model = nullptr;
    if (!model) {
        ImGui::TextDisabled("Select your accessory: a Model (Ctrl+G groups parts into one).");
        ImGui::End();
        return;
    }
    ImGui::Text("Accessory: %s", model->name.c_str());
    const Attribute* saved = model->findAttribute("AccessoryPlacement");

    // 3. Placing it.
    ImGui::BeginDisabled(!mannequin);
    if (ImGui::Button("Move to the spot")) {   // a starting point: then fine-tune with the Move / Rotate tools
        glm::mat4 root = mannequin->worldMatrix();
        glm::vec3 world = glm::vec3(root * glm::vec4(kind.spot, 1.0f));
        glm::vec3 local = model->parent ? glm::vec3(glm::inverse(model->parent->worldMatrix()) * glm::vec4(world, 1.0f)) : world;
        model->transform.position = local;
        m_scene->markDirty();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Puts it %s. Then move and turn it until it looks right.", kind.where);
    ImGui::SameLine();
    if (ImGui::Button("Save position")) {
        Transform rel = relativeTo(mannequin->worldMatrix(), model->worldMatrix());
        json j = {{"kind", kind.key}, {"position", {rel.position.x, rel.position.y, rel.position.z}},
                  {"rotation", {rel.rotation.x, rel.rotation.y, rel.rotation.z}}, {"scale", {rel.scale.x, rel.scale.y, rel.scale.z}}};
        setString(*model, "AccessoryPlacement", j.dump());
        m_scene->markDirty();
        m_accessoryMsg = "Saved where it sits (" + std::string(kind.title) + "). Save your place to keep it.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!saved);
    if (ImGui::Button("Put it back")) {   // back to the saved spot, if it got moved
        json j = json::parse(saved->s, nullptr, false);
        if (j.is_object()) {
            Transform rel;
            for (int i = 0; i < 3; ++i) {
                rel.position[i] = j["position"][i].get<float>();
                rel.rotation[i] = j["rotation"][i].get<float>();
                rel.scale[i] = j["scale"][i].get<float>();
            }
            glm::mat4 world = mannequin->worldMatrix() * rel.matrix();
            model->transform = relativeTo(model->parent ? model->parent->worldMatrix() : glm::mat4(1.0f), world);
            m_scene->markDirty();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    // Precise offsets are much easier than repeatedly dragging a tiny accessory in 3D.
    // These values are relative to the mannequin, independent of the accessory's parent.
    if (mannequin) {
        Transform rel = relativeTo(mannequin->worldMatrix(), model->worldMatrix());
        ImGui::SeparatorText("Fine positioning");
        ImGui::TextDisabled("Offset, rotation and scale relative to the mannequin.");
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Offset", &rel.position.x, 0.01f, -100.0f, 100.0f, "%.3f");
        transformChanged |= ImGui::DragFloat3("Rotation", &rel.rotation.x, 0.5f, -360.0f, 360.0f, "%.1f deg");
        transformChanged |= ImGui::DragFloat3("Scale", &rel.scale.x, 0.01f, 0.001f, 100.0f, "%.3f");
        if (ImGui::Button("Center offset")) { rel.position = kind.spot; transformChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button("Reset rotation")) { rel.rotation = glm::vec3(0.0f); transformChanged = true; }
        ImGui::SameLine();
        if (ImGui::Button("Reset scale")) { rel.scale = glm::vec3(1.0f); transformChanged = true; }
        if (transformChanged) {
            rel.scale = glm::max(rel.scale, glm::vec3(0.001f));
            const glm::mat4 world = mannequin->worldMatrix() * rel.matrix();
            model->transform = relativeTo(model->parent ? model->parent->worldMatrix() : glm::mat4(1.0f), world);
            m_scene->markDirty();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Use these numeric controls for exact placement; the Move / Rotate tools still work in the viewport.");
    }

    if (saved) {
        json j = json::parse(saved->s, nullptr, false);
        std::string k = j.is_object() ? j.value("kind", std::string()) : std::string();
        for (const auto& ak : kAccessoryKinds) if (k == ak.key) ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.4f, 1), "Position saved as a %s.", ak.title);
    }

    // 4. Uploading.
    ImGui::SeparatorText("Upload to the catalog");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("Name", model->name.c_str(), &m_accessoryName);
    ImGui::InputTextMultiline("Description", &m_accessoryDesc, ImVec2(-90, 50));
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputInt("Price (Bolts)", &m_accessoryPrice, 5, 50)) m_accessoryPrice = std::clamp(m_accessoryPrice, 0, 1000000);
    ImGui::BeginDisabled(!saved || m_onlineBusy);
    if (ImGui::Button("Upload", ImVec2(140, 30))) {
        json place = json::parse(saved->s, nullptr, false);
        json node = json::parse(Serializer::nodeToString(*model), nullptr, false);
        if (place.is_object() && node.is_object()) {
            // The Model's own transform becomes "where it sits, from the feet".
            node["pos"] = place["position"];
            node["rot"] = place["rotation"];
            node["size"] = place["scale"];
            std::string name = m_accessoryName.empty() ? model->name : m_accessoryName;
            const std::string kind = place.value("kind", std::string("hat"));
            m_onlineBusy = true;
            m_accessoryMsg = "Uploading...";
            // Its pictures first (an imported hat's texture), so the website and other players see them.
            uploadLocalTextures(node, name, [this, name, kind](json node, std::string error) {
            if (!error.empty()) { m_onlineBusy = false; m_accessoryMsg = error; return; }
            json acc = {{"format", "gbaccessory"}, {"version", 1}, {"kind", kind}, {"node", node}};
            json args = {{"kind", kind}, {"name", name}, {"description", m_accessoryDesc}, {"price", m_accessoryPrice},
                         {"data", Online::base64Encode(acc.dump())}};
            Online::request("upload", args, [this, name](const json& r) {
                m_onlineBusy = false;
                if (!r.value("ok", false)) { m_accessoryMsg = r.value("error", std::string("The upload didn't work.")); return; }
                std::string id = r["asset"].value("id", std::string());
                m_accessoryMsg = "Uploaded \"" + name + "\" to the catalog!";
                Log::system("Uploaded the accessory \"" + name + "\" (" + id + ")");
            }, 120);
            });
        }
    }
    ImGui::EndDisabled();
    const Attribute* catalogId = model->findAttribute("CatalogItemId");
    const std::string existingId = catalogId && catalogId->type == Attribute::String ? catalogId->s : std::string();
    ImGui::BeginDisabled(existingId.empty() || !saved || m_onlineBusy);
    if (ImGui::Button("Update existing item", ImVec2(190, 30))) {
        json place = json::parse(saved->s, nullptr, false);
        json node = json::parse(Serializer::nodeToString(*model), nullptr, false);
        if (place.is_object() && node.is_object()) {
            node["pos"] = place["position"];
            node["rot"] = place["rotation"];
            node["size"] = place["scale"];
            const std::string name = m_accessoryName.empty() ? model->name : m_accessoryName;
            const std::string kind = place.value("kind", std::string("hat"));
            m_onlineBusy = true;
            m_accessoryMsg = "Updating existing catalog item...";
            uploadLocalTextures(node, name, [this, existingId, name, kind](json node, std::string error) {
                if (!error.empty()) { m_onlineBusy = false; m_accessoryMsg = error; return; }
                json acc = {{"format", "gbaccessory"}, {"version", 1}, {"kind", kind}, {"node", node}};
                json args = {{"id", existingId}, {"name", name}, {"description", m_accessoryDesc},
                             {"price", m_accessoryPrice}, {"data", Online::base64Encode(acc.dump())}};
                Online::request("update", args, [this, name](const json& r) {
                    m_onlineBusy = false;
                    if (!r.value("ok", false)) { m_accessoryMsg = r.value("error", std::string("The update didn't work.")); return; }
                    m_accessoryMsg = "Updated catalog item \"" + name + "\". The existing item ID and listing are preserved.";
                    Log::system(m_accessoryMsg);
                }, 120);
            });
        }
    }
    ImGui::EndDisabled();
    if (!existingId.empty()) ImGui::TextDisabled("Item ID: %s. This updates the existing listing.", existingId.c_str());
    if (!saved) { ImGui::SameLine(); ImGui::TextDisabled("Save its position first."); }
    if (!m_accessoryMsg.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextUnformatted(m_accessoryMsg.c_str()); ImGui::PopTextWrapPos(); }
    ImGui::End();
}
