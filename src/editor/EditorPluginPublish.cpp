// Making and sharing Studio plugins.
//
// The old way: write a .lua file in a hidden folder with some other program, then find
// PLUGINS > Library > "Publish a plugin", where it could only take the file name, had no
// description, picture or version, and publishing again made a second copy (and cost the
// fee again). Now:
//   - PLUGINS > New makes a plugin as a Script (in ServerStorage, switched off so it never
//     runs in games) that you change in Studio's own script editor.
//   - Right-click a Script > "Save as Plugin" tries it: its buttons show up in the PLUGINS tab.
//   - Right-click > "Publish as Plugin..." (or PLUGINS > Publish) opens the Publish Plugin
//     window: name, description, version, an optional picture, and the price for Verified
//     creators. It checks the plugin works first, and if you published it before it updates
//     that one (free) instead of making a copy.
#include "Editor.h"
#include "Plugins.h"
#include "../core/Account.h"
#include "../core/FileDialog.h"
#include "../core/Log.h"
#include "../game/Bolts.h"
#include "../online/OnlineClient.h"
#include "../online/Protocol.h"
#include "../scene/Scene.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

using json = nlohmann::json;

namespace {

const char* kNewPlugin =
    "-- A Studio plugin: it adds buttons to the PLUGINS tab.\n"
    "-- Right-click this Script in the Explorer:\n"
    "--   \"Save as Plugin\" to try it (its buttons show up in the PLUGINS tab),\n"
    "--   \"Publish as Plugin...\" to share it in the Library.\n"
    "--\n"
    "-- You get everything scripts have (workspace, Instance.new, Color3...) plus:\n"
    "--   plugin:Button(name, tip, function)   a button in the PLUGINS tab\n"
    "--   Selection:Get() / Selection:Set({...})   what's selected in Studio\n"
    "-- Anything a button changes can be undone with Ctrl+Z.\n"
    "\n"
    "plugin:Button(\"Paint Blue\", \"Make the selected parts blue\", function()\n"
    "    for _, thing in ipairs(Selection:Get()) do\n"
    "        if thing:IsA(\"BasePart\") then\n"
    "            thing.Color = Color3.fromRGB(40, 120, 255)\n"
    "        end\n"
    "    end\n"
    "end)\n";

std::string readAll(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

std::string lowerText(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// "1.2" -> "1.3", "v2" -> "v3", "" -> "1.1": the next version, for an update.
std::string nextVersion(const std::string& v) {
    if (v.empty()) return "1.1";
    size_t end = v.size();
    while (end > 0 && !std::isdigit((unsigned char)v[end - 1])) --end;
    size_t start = end;
    while (start > 0 && std::isdigit((unsigned char)v[start - 1])) --start;
    if (start == end || end - start > 9) return v;
    return v.substr(0, start) + std::to_string(std::stoll(v.substr(start, end - start)) + 1) + v.substr(end);
}

bool g_mineAsked = false;   // (asked the server for your plugins this time the window opened)

const ImVec4 kGood(0.4f, 0.85f, 0.45f, 1), kBad(1.0f, 0.5f, 0.45f, 1), kWarn(1.0f, 0.8f, 0.35f, 1);

} // namespace

// ---------------------------------------------------------------------------
// Plugins as Scripts: write them in Studio, try them, publish them
// ---------------------------------------------------------------------------

void Editor::newPluginScript() {
    SceneNode* storage = m_scene->serviceFolder("ServerStorage", true);
    std::string name = "My Plugin";
    for (int n = 2; std::filesystem::exists(Plugins::folder() / (name + ".lua")); ++n) name = "My Plugin " + std::to_string(n);
    auto s = std::make_unique<SceneNode>(name, NodeKind::Script);
    s->source = kNewPlugin;
    s->enabled = false;   // a plugin, not part of the game: never runs when you press Play
    SceneNode* raw = m_scene->insert(std::move(s), storage);
    m_scene->select(raw);
    openScript(raw);
    Log::info("Made \"" + name + "\" in ServerStorage. Change it, then right-click it > Save as Plugin to try it.");
}

void Editor::editPluginInStudio(const std::string& file) {
    SceneNode* storage = m_scene->serviceFolder("ServerStorage", true);
    for (auto& c : storage->children)
        if (c->isScript() && Plugins::fileNameFor(c->name) == file) { openScript(c.get()); return; }
    auto s = std::make_unique<SceneNode>(file, NodeKind::Script);
    s->source = readAll(Plugins::folder() / (file + ".lua"));
    s->enabled = false;
    SceneNode* raw = m_scene->insert(std::move(s), storage);
    m_scene->select(raw);
    openScript(raw);
    Log::info("\"" + file + "\" is in ServerStorage now. Change it, then right-click it > Save as Plugin.");
}

bool Editor::savePluginScript(SceneNode* script, std::string& file, std::string& error) {
    error.clear();
    if (!script || !script->isScript()) { error = "Pick a Script first."; return false; }
    file = Plugins::fileNameFor(script->name);
    const auto path = Plugins::folder() / (file + ".lua");
    // Don't write over a plugin someone else made that you installed from the Library.
    if (std::filesystem::exists(path) && !Plugins::installedId(file).empty() && Plugins::publishedId(file).empty()) {
        error = "You already have a plugin called \"" + file + "\" from the Library. Rename this Script first.";
        return false;
    }
    {
        std::ofstream out(path, std::ios::binary);
        out << script->source;
        if (!out) { error = "Couldn't save " + path.string() + "."; return false; }
    }
    m_plugins->reload();
    for (auto& p : m_plugins->list())
        if (p->name == file) {
            if (!p->error.empty()) error = "It has an error: " + p->error;
            else if (p->buttons.empty()) error = "It doesn't add any buttons yet. Add plugin:Button(...) so people can use it.";
        }
    return true;
}

void Editor::pluginMenuItems() {
    SceneNode* sel = m_scene->selected();
    if (!sel || !sel->isScript() || sel->isModule) return;
    // (After the menu: loading a plugin runs it, and it may change the scene the Explorer is drawing.)
    if (ImGui::MenuItem("Save as Plugin")) m_deferred = [this, sel] {
        std::string file, error;
        if (savePluginScript(sel, file, error)) {
            m_ribbonTab = 4;   // PLUGINS
            if (error.empty()) Log::info("Saved \"" + file + "\" as a plugin. Its buttons are in the PLUGINS tab.");
            else Log::error("Plugin " + file + ": " + error);
        } else {
            Log::error(error);
        }
    };
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Try this Script as a Studio plugin: its buttons show up in the PLUGINS tab.");
    if (ImGui::MenuItem("Publish as Plugin...")) m_deferred = [this, sel] {
        std::string file, error;
        if (savePluginScript(sel, file, error)) openPublishPlugin(file);
        else Log::error(error);
    };
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Share this Script in the Library as a plugin anyone can install.");
}

// ---------------------------------------------------------------------------
// The Publish Plugin window
// ---------------------------------------------------------------------------

void Editor::openPublishPlugin(const std::string& file) {
    m_showPublishPlugin = true;
    m_ppMineLoaded = false;
    g_mineAsked = false;
    m_ppMsg.clear();
    if (!file.empty() || m_ppFile.empty()) {
        std::string pick = file;
        if (pick.empty() && !m_plugins->list().empty()) pick = m_plugins->list().front()->name;
        pickPluginToPublish(pick);
    }
    ImGui::SetWindowFocus("Publish Plugin");
}

// Fill the form for this plugin: from the Library copy you published before, if there is one.
void Editor::pickPluginToPublish(const std::string& file) {
    m_ppFile = file;
    m_ppName = file;
    m_ppDesc.clear();
    m_ppVersion = "1.0";
    m_ppPicture.clear();
    m_ppTarget.clear();
    m_ppFilled = m_ppMineLoaded;
    const std::string id = Plugins::publishedId(file);
    const json* match = nullptr;
    for (const auto& a : m_ppMine) if (!id.empty() && a.value("id", std::string()) == id) match = &a;
    if (!match)   // (published from another computer: the same name)
        for (const auto& a : m_ppMine) if (lowerText(a.value("name", std::string())) == lowerText(file)) { match = &a; break; }
    if (match) {
        m_ppTarget = match->value("id", std::string());
        m_ppName = match->value("name", file);
        m_ppDesc = match->value("description", std::string());
        const json meta = match->value("meta", json::object());
        m_ppVersion = nextVersion(meta.is_object() ? meta.value("version", std::string()) : std::string());
        if (Online::verified()) m_publishPluginPrice = (int)match->value("price", 0LL);
    }
}

void Editor::renderPublishPluginWindow() {
    // (Tests: GB_TEST_PUBLISHPLUGIN=1 starts with it showing; =script makes a new plugin Script,
    // saves it as a plugin like right-click > Publish as Plugin... does, and shows it here.)
    static const char* test = std::getenv("GB_TEST_PUBLISHPLUGIN");
    if (test) {
        std::string file, error;
        m_ribbonTab = 4;
        if (std::string(test) == "script") {
            newPluginScript();
            if (!savePluginScript(m_scene->selected(), file, error) || !error.empty()) Log::error(error);
        }
        openPublishPlugin(file);
        test = nullptr;
    }
    if (!m_showPublishPlugin) return;
    ImGui::SetNextWindowSize(ImVec2(580, 600), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Publish Plugin", &m_showPublishPlugin)) { ImGui::End(); return; }

    const bool signedIn = !Account::id().empty() && Online::online() && Online::me().value("userId", 0LL) > 0;
    const std::string myId = Online::me().value("id", std::string());
    if (signedIn && !g_mineAsked && !myId.empty()) {   // your plugins already in the Library (to update them)
        g_mineAsked = true;
        Online::request("list", {{"kind", "plugin"}, {"creator", myId}, {"limit", 100}}, [this](const json& r) {
            m_ppMine = r.value("ok", false) ? r.value("assets", json::array()) : json::array();
            m_ppMineLoaded = true;
        });
    }
    if (m_ppMineLoaded && !m_ppFilled && !m_ppFile.empty()) pickPluginToPublish(m_ppFile);

    ImGui::TextWrapped("Share a plugin so anyone can install it from PLUGINS > Library.");
    auto& list = m_plugins->list();

    // --- 1. Which plugin ---
    ImGui::SeparatorText("1. Your plugin");
    Plugins::Plugin* plugin = nullptr;
    for (auto& p : list) if (p->name == m_ppFile) plugin = p.get();
    if (!plugin && !list.empty()) { pickPluginToPublish(list.front()->name); plugin = list.front().get(); }
    if (list.empty()) {
        ImGui::TextWrapped("You don't have any plugins yet. Press New plugin to make one: it's a Script you "
                           "change right here in Studio.");
    } else {
        ImGui::SetNextItemWidth(220);
        if (ImGui::BeginCombo("##plugin", m_ppFile.c_str())) {
            for (auto& p : list)
                if (ImGui::Selectable(p->name.c_str(), p->name == m_ppFile)) { pickPluginToPublish(p->name); m_ppMsg.clear(); }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    if (ImGui::Button("New plugin")) newPluginScript();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Make a new plugin as a Script you can change in Studio.");
    if (plugin) {
        ImGui::SameLine();
        if (ImGui::Button("Change it")) editPluginInStudio(plugin->name);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open it as a Script in Studio. When you're done, right-click the Script > Save as Plugin.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Open folder")) Plugins::showFolder();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Plugins::folder().string().c_str());

    // Does it work? (it was loaded when Studio started, or when it was saved)
    bool works = false;
    if (plugin) {
        if (!plugin->error.empty()) {
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kBad, "It has an error, so it can't be published yet:\n%s", plugin->error.c_str());
            ImGui::PopTextWrapPos();
        } else if (plugin->buttons.empty()) {
            ImGui::TextColored(kWarn, "It doesn't add any buttons yet. Add plugin:Button(...) so people can use it.");
        } else {
            std::string names;
            for (const auto& b : plugin->buttons) names += (names.empty() ? "" : ", ") + b.label;
            ImGui::PushTextWrapPos(0);
            ImGui::TextColored(kGood, "Works! It adds %d button%s: %s", (int)plugin->buttons.size(),
                               plugin->buttons.size() == 1 ? "" : "s", names.c_str());
            ImGui::PopTextWrapPos();
            works = true;
        }
    }

    // --- 2. About it ---
    ImGui::SeparatorText("2. About it");
    ImGui::BeginDisabled(!plugin);
    ImGui::SetNextItemWidth(-90);
    ImGui::InputText("Name", &m_ppName);
    ImGui::InputTextMultiline("Description", &m_ppDesc, ImVec2(-90, 70));
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("Version", &m_ppVersion);
    ImGui::SameLine();
    ImGui::TextDisabled("(like 1.0, then 1.1 for an update)");
    ImGui::SetNextItemWidth(FileDialog::available() ? -180 : -90);
    ImGui::InputTextWithHint("##picture", "A .png or .jpg (optional)", &m_ppPicture);
    if (FileDialog::available()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(82, 0))) {
            const std::string p = FileDialog::openImage("Pick a picture for your plugin");
            if (!p.empty()) m_ppPicture = p;
        }
    }
    ImGui::SameLine();
    ImGui::TextUnformatted("Picture");
    const bool verified = Online::verified();
    if (verified) {
        ImGui::SetNextItemWidth(160);
        ImGui::InputInt("Price (Bolts)", &m_publishPluginPrice, 5, 50);
        m_publishPluginPrice = std::clamp(m_publishPluginPrice, 0, 1000000);
    }
    ImGui::EndDisabled();

    // --- 3. Publish ---
    ImGui::SeparatorText("3. Publish");
    const json* target = nullptr;
    for (const auto& a : m_ppMine) if (!m_ppTarget.empty() && a.value("id", std::string()) == m_ppTarget) target = &a;
    if (!target) m_ppTarget.clear();
    if (signedIn && plugin && !m_ppMine.empty()) {
        // New, or an update of one you already published.
        std::string current = target ? "Update \"" + target->value("name", std::string()) + "\"" : "A new plugin";
        ImGui::SetNextItemWidth(300);
        if (ImGui::BeginCombo("Publish as", current.c_str())) {
            if (ImGui::Selectable("A new plugin", !target)) m_ppTarget.clear();
            for (const auto& a : m_ppMine) {
                const json meta = a.value("meta", json::object());
                std::string v = meta.is_object() ? meta.value("version", std::string()) : std::string();
                std::string label = "Update \"" + a.value("name", std::string()) + "\"" + (v.empty() ? "" : " (now v" + v + ")");
                ImGui::PushID(a.value("id", std::string()).c_str());
                if (ImGui::Selectable(label.c_str(), a.value("id", std::string()) == m_ppTarget)) {
                    m_ppTarget = a.value("id", std::string());
                    m_ppVersion = nextVersion(v);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    const bool update = target != nullptr;
    const long long fee = (update || verified) ? 0 : Online::uploadFee("plugin");
    if (update) ImGui::TextDisabled("Updating is free. Everyone who installed it gets an Update button in their Library.");
    else if (verified) ImGui::TextDisabled("You're Verified: publishing is free, and you can sell it.");
    else ImGui::TextDisabled("Publishing a new plugin costs %lld Bolts (free for Verified creators). Updating it later is free.", fee);

    // Why it can't be published (yet), in plain words.
    std::string why;
    if (!plugin) why = "Make a plugin first.";
    else if (!works) why = plugin->error.empty() ? "Add at least one button first." : "Fix the error first.";
    else if (m_ppName.find_first_not_of(' ') == std::string::npos) why = "Give it a name.";
    else if (signedIn && fee > 0 && Online::bolts() < fee) why = "You need " + std::to_string(fee) + " Bolts to publish a new plugin.";

    if (!signedIn) {
        if (ImGui::Button("Sign in to publish", ImVec2(180, 30))) requireStudioLogin("publish this plugin");
    } else {
        ImGui::BeginDisabled(m_onlineBusy || !why.empty());
        if (ImGui::Button(update ? "Update plugin" : "Publish plugin", ImVec2(160, 30))) {
            m_ppBad = false;
            std::string picture;
            if (!m_ppPicture.empty()) {
                std::error_code ec;
                const auto size = std::filesystem::file_size(m_ppPicture, ec);
                picture = ec ? std::string() : readAll(m_ppPicture);
                const bool png = picture.size() > 8 && picture.compare(0, 4, "\x89PNG") == 0;
                const bool jpg = picture.size() > 3 && (unsigned char)picture[0] == 0xff && (unsigned char)picture[1] == 0xd8;
                if (ec || picture.empty()) { m_ppMsg = "Couldn't open the picture " + m_ppPicture + "."; m_ppBad = true; }
                else if (!png && !jpg) { m_ppMsg = "The picture has to be a .png or .jpg."; m_ppBad = true; }
                else if (size > 400 * 1024) { m_ppMsg = "The picture is too big (the most is 400 KB)."; m_ppBad = true; }
            }
            if (!m_ppBad) {
                json args = {{"name", m_ppName}, {"description", m_ppDesc}, {"version", m_ppVersion},
                             {"data", Online::base64Encode(readAll(plugin->file))}};
                if (verified) args["price"] = m_publishPluginPrice;
                if (update) args["id"] = m_ppTarget;
                else args["kind"] = "plugin";
                m_onlineBusy = true;
                m_ppMsg = update ? "Updating..." : "Publishing...";
                const std::string file = plugin->name, version = m_ppVersion;
                Online::request(update ? "update" : "upload", args, [this, file, version, picture, update](const json& r) {
                    m_onlineBusy = false;
                    if (!r.value("ok", false)) {
                        m_ppBad = true;
                        m_ppMsg = (update ? "Couldn't update it: " : "Couldn't publish it: ") +
                                  r.value("error", std::string("the server didn't answer. Check your internet and try again."));
                        return;
                    }
                    const std::string id = r.value("asset", json::object()).value("id", std::string());
                    Plugins::setPublished(file, id);
                    if (!picture.empty())
                        Online::request("thumb.set", {{"id", id}, {"data", Online::base64Encode(picture)}}, [this](const json& t) {
                            if (!t.value("ok", false)) { m_ppBad = true; m_ppMsg += " (The picture didn't go up: " + t.value("error", std::string()) + ")"; }
                        }, 60);
                    m_ppMsg = update ? "Updated to version " + version + "! Everyone who has it can get the new one from the Library."
                                     : "Published! Anyone can install it from PLUGINS > Library now.";
                    // It's yours to update from now on (and the next update gets the next version).
                    const json asset = r.value("asset", json::object());
                    bool found = false;
                    for (auto& a : m_ppMine) if (a.value("id", std::string()) == id) { a = asset; found = true; }
                    if (!found) m_ppMine.push_back(asset);
                    m_ppTarget = id;
                    m_ppVersion = nextVersion(version);
                    m_marketLoaded = false;
                    Online::connect();   // (your Bolts)
                }, 120);
            }
        }
        ImGui::EndDisabled();
        if (!why.empty()) { ImGui::SameLine(); ImGui::TextColored(kWarn, "%s", why.c_str()); }
    }
    if (!m_ppMsg.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(m_ppBad ? kBad : kGood, "%s", m_ppMsg.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}
