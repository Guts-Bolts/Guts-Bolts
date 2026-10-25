// Project Time Machine: local scene checkpoints, change summaries, and recovery.
#include "Editor.h"
#include "../core/Paths.h"
#include "../core/Log.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <ctime>
#include <map>
#include <nlohmann/json.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaxCheckpointsPerProject = 15;
constexpr const char* kStoreFile = "ProjectTimeMachine.json";

std::string clockLabel(long long stamp) {
    std::time_t t = static_cast<std::time_t>(stamp);
    std::tm* tm = std::localtime(&t);
    if (!tm) return "Unknown date";
    char buf[64]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return buf;
}

void collectNodes(const Json& node, std::map<std::string, Json>& out) {
    if (!node.is_object()) return;
    if (node.contains("id")) {
        Json shallow = node;
        shallow.erase("children");
        out[shallow["id"].dump()] = std::move(shallow);
    }
    if (node.contains("children") && node["children"].is_array())
        for (const auto& child : node["children"]) collectNodes(child, out);
}

struct DiffSummary { int added=0, removed=0, changed=0; };
DiffSummary compareSnapshots(const std::string& oldText, const std::string& newText) {
    DiffSummary d;
    Json oldJ=Json::parse(oldText,nullptr,false), newJ=Json::parse(newText,nullptr,false);
    if (!oldJ.is_object() || !newJ.is_object() || !oldJ.contains("workspace") || !newJ.contains("workspace")) return d;
    std::map<std::string,Json> oldNodes,newNodes;
    collectNodes(oldJ["workspace"],oldNodes);
    collectNodes(newJ["workspace"],newNodes);
    for (const auto& [id,node] : newNodes) {
        auto it=oldNodes.find(id);
        if(it==oldNodes.end()) ++d.added;
        else if(it->second!=node) ++d.changed;
    }
    for (const auto& [id,node] : oldNodes) if(!newNodes.count(id)) ++d.removed;
    return d;
}

Json readStore() {
    std::string text;
    if (!Serializer::readFile(Paths::file(kStoreFile).string(), text)) return Json::object();
    Json j=Json::parse(text,nullptr,false);
    return j.is_object() ? j : Json::object();
}
}

void Editor::renderProjectTimeMachine() {
    if (!m_showProjectTimeMachine) return;
    const std::string key = m_path.empty()
        ? std::string("untitled:") + m_scene->info().title
        : std::filesystem::path(m_path).lexically_normal().string();

    if (!m_timeMachineLoaded || m_timeMachineProjectKey != key) {
        Json store=readStore();
        m_timeMachineCheckpoints.clear();
        if (store.contains("checkpoints") && store["checkpoints"].is_array()) {
            for (const auto& item : store["checkpoints"]) {
                if (!item.is_object() || item.value("project",std::string()) != key) continue;
                TimeMachineCheckpoint cp;
                cp.project=item.value("project",std::string());
                cp.name=item.value("name",std::string("Checkpoint"));
                cp.snapshot=item.value("snapshot",std::string());
                cp.time=item.value("time",0LL);
                if (!cp.snapshot.empty()) m_timeMachineCheckpoints.push_back(std::move(cp));
            }
        }
        std::sort(m_timeMachineCheckpoints.begin(),m_timeMachineCheckpoints.end(),
                  [](const auto& a,const auto& b){return a.time>b.time;});
        m_timeMachineProjectKey=key;
        m_timeMachineLoaded=true;
        m_timeMachineSelected=m_timeMachineCheckpoints.empty() ? -1 : 0;
    }

    auto prune = [&]() {
        std::sort(m_timeMachineCheckpoints.begin(),m_timeMachineCheckpoints.end(),
                  [](const auto& a,const auto& b){return a.time>b.time;});
        int kept=0;
        m_timeMachineCheckpoints.erase(std::remove_if(m_timeMachineCheckpoints.begin(),m_timeMachineCheckpoints.end(),
            [&](const auto& cp){ return cp.project==key && ++kept>static_cast<int>(kMaxCheckpointsPerProject); }),
            m_timeMachineCheckpoints.end());
    };
    auto persist = [&]() {
        Json store=readStore();
        Json all=Json::array();
        if (store.contains("checkpoints") && store["checkpoints"].is_array())
            for (const auto& item : store["checkpoints"])
                if (item.is_object() && item.value("project",std::string()) != key) all.push_back(item);
        for (const auto& cp : m_timeMachineCheckpoints)
            all.push_back({{"project",cp.project},{"name",cp.name},{"snapshot",cp.snapshot},{"time",cp.time}});
        store["version"]=1;
        store["checkpoints"]=std::move(all);
        if (!Serializer::writeFile(Paths::file(kStoreFile).string(),store.dump()))
            Log::error("Project Time Machine couldn't save its local checkpoint file.");
    };

    ImGui::SetNextWindowSize(ImVec2(760,580),ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Project Time Machine",&m_showProjectTimeMachine)) { ImGui::End(); return; }
    ImGui::TextWrapped("Named checkpoints, object-level change summaries, and recovery. Checkpoints stay on this computer and are not cloud-synced.");
    ImGui::Separator();
    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("##checkpoint-name","Checkpoint name (e.g. before mesh cleanup)",&m_timeMachineCheckpointName);
    ImGui::SameLine();
    if (ImGui::Button("Create checkpoint") && !m_playing) {
        TimeMachineCheckpoint cp;
        cp.project=key;
        cp.name=m_timeMachineCheckpointName.empty() ? "Checkpoint" : m_timeMachineCheckpointName;
        cp.snapshot=Serializer::saveScene(*m_scene);
        cp.time=static_cast<long long>(std::time(nullptr));
        m_timeMachineCheckpoints.insert(m_timeMachineCheckpoints.begin(),std::move(cp));
        prune();
        m_timeMachineSelected=0;
        m_timeMachineCheckpointName.clear();
        persist();
        Log::system("Project Time Machine checkpoint created.");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %d checkpoints",(int)m_timeMachineCheckpoints.size(),(int)kMaxCheckpointsPerProject);
    ImGui::Separator();
    ImGui::BeginChild("##time-machine-list",ImVec2(280,0),ImGuiChildFlags_Borders);
    for (int i=0;i<(int)m_timeMachineCheckpoints.size();++i) {
        const auto& cp=m_timeMachineCheckpoints[i];
        if (ImGui::Selectable((cp.name+"##tm"+std::to_string(i)).c_str(),m_timeMachineSelected==i))
            m_timeMachineSelected=i;
        ImGui::TextDisabled("%s",clockLabel(cp.time).c_str());
    }
    if (m_timeMachineCheckpoints.empty()) ImGui::TextDisabled("No checkpoints yet.");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##time-machine-detail",ImVec2(0,0),ImGuiChildFlags_Borders);
    if (m_timeMachineSelected>=0 && m_timeMachineSelected<(int)m_timeMachineCheckpoints.size()) {
        const auto cp=m_timeMachineCheckpoints[m_timeMachineSelected];
        ImGui::TextWrapped("%s",cp.name.c_str());
        ImGui::TextDisabled("%s",clockLabel(cp.time).c_str());
        const std::string current=Serializer::saveScene(*m_scene);
        const DiffSummary diff=compareSnapshots(cp.snapshot,current);
        ImGui::SeparatorText("Changes since this checkpoint");
        ImGui::Text("Added objects: %d",diff.added);
        ImGui::Text("Removed objects: %d",diff.removed);
        ImGui::Text("Modified objects: %d",diff.changed);
        ImGui::TextDisabled("Comparison uses saved object properties and mesh data by object ID.");
        ImGui::Separator();
        ImGui::BeginDisabled(m_playing);
        if (ImGui::Button("Restore checkpoint",ImVec2(180,30))) {
            TimeMachineCheckpoint safety;
            safety.project=key;
            safety.name="Before restore";
            safety.snapshot=current;
            safety.time=static_cast<long long>(std::time(nullptr));
            m_timeMachineCheckpoints.insert(m_timeMachineCheckpoints.begin(),std::move(safety));
            prune();
            persist();
            restore(cp.snapshot);
            m_timeMachineSelected=-1;
            Log::system("Restored checkpoint '"+cp.name+"'. Save the project to keep the recovered version.");
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Delete checkpoint")) {
            m_timeMachineCheckpoints.erase(m_timeMachineCheckpoints.begin()+m_timeMachineSelected);
            m_timeMachineSelected=m_timeMachineCheckpoints.empty() ? -1 : std::min(m_timeMachineSelected,(int)m_timeMachineCheckpoints.size()-1);
            persist();
        }
    } else {
        ImGui::TextWrapped("Select a checkpoint to compare it with the current scene or restore it.");
    }
    ImGui::EndChild();
    ImGui::End();
}
