// The Physics Replay window: records what the physics does from how the scene is now,
// lets you scrub through it (outlines in the viewport show where every loose part was),
// keep any moment of it, and save it as a test that fails if the physics acts differently.
#include "Editor.h"
#include "PhysicsReplay.h"
#include "../scene/Scene.h"
#include "../core/Log.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

using nlohmann::json;

namespace {

const char* const kTestsFolder = "PhysicsTests";

glm::mat4 ghostMatrix(const SceneNode* n, const PhysicsReplay::PartState& s) {
    Transform t = n->transform;
    t.position = s.position;
    t.rotation = s.rotation;
    const glm::mat4 parent = n->parent ? n->parent->worldMatrix() : glm::mat4(1.0f);
    return parent * t.matrix();
}

} // namespace

json Editor::recordPhysics(float seconds) {
    m_replay = PhysicsReplay::record(Serializer::saveScene(*m_scene), seconds);
    m_replayFrame = 0;
    m_replayPlaying = false;
    m_replayClock = 0.0f;
    json d = {{"frames", (int)m_replay.frames.size()}, {"seconds", m_replay.seconds()}, {"parts", (int)m_replay.ids.size()},
              {"ms", std::round(m_replay.ms)}};
    if (!m_replay.frames.empty()) {
        const PhysicsReplay::Frame& last = m_replay.frames.back();
        d["fell_out"] = (int)(m_replay.frames.front().parts.size() - last.parts.size());
        int asleep = 0;
        for (const auto& p : last.parts) if (p.sleepTime >= 0.6f) ++asleep;
        d["asleep_at_end"] = asleep;
    }
    return d;
}

void Editor::showReplayFrame(int frame) {
    m_state.replayGhosts.clear();
    m_state.replayTrail.clear();
    if (frame < 0 || frame >= (int)m_replay.frames.size() || m_playing) return;
    for (const auto& s : m_replay.frames[frame].parts)
        if (SceneNode* n = m_scene->findById(s.id)) m_state.replayGhosts.push_back(ghostMatrix(n, s));
    // The selected part's path, from the start to now.
    SceneNode* sel = m_scene->selected();
    if (!m_replayTrail || !sel) return;
    for (int i = 0; i <= frame; i += 2)
        if (const auto* s = m_replay.frames[i].find(sel->id)) m_state.replayTrail.push_back(glm::vec3(ghostMatrix(sel, *s)[3]));
}

std::vector<SceneNode*> Editor::physicsTests() {
    std::vector<SceneNode*> out;
    SceneNode* storage = m_scene->serviceFolder("ServerStorage", false);
    if (!storage) return out;
    for (auto& f : storage->children)
        if (f->name == kTestsFolder)
            for (auto& c : f->children)
                if (c->isValue() && c->value.type == Attribute::String) out.push_back(c.get());
    return out;
}

SceneNode* Editor::savePhysicsTest(const std::string& name, float seconds, float tolerance, std::string* error) {
    PhysicsReplay::Test t;
    t.name = name.empty() ? "Physics test" : name;
    t.seconds = std::clamp(seconds, 0.25f, 30.0f);
    t.tolerance = std::clamp(tolerance, 0.001f, 10.0f);
    t.expected = PhysicsReplay::record(Serializer::saveScene(*m_scene), t.seconds);
    if (t.expected.ids.empty()) {
        if (error) *error = "There are no loose (unanchored) parts to test: untick Anchored on the parts that should move.";
        return nullptr;
    }
    SceneNode* storage = m_scene->serviceFolder("ServerStorage", true);
    SceneNode* folder = nullptr;
    for (auto& c : storage->children) if (c->name == kTestsFolder) folder = c.get();
    if (!folder) folder = m_scene->insert(std::make_unique<SceneNode>(kTestsFolder, NodeKind::Model), storage);
    SceneNode* node = nullptr;
    for (auto& c : folder->children) if (c->name == t.name && c->isValue()) node = c.get();   // (saving again replaces it)
    if (!node) {
        auto v = std::make_unique<SceneNode>(t.name, NodeKind::Value);
        v->value.type = Attribute::String;
        node = m_scene->insert(std::move(v), folder);
    }
    node->value.s = PhysicsReplay::testToJson(t).dump();
    m_replayResults.erase(node->id);
    m_scene->markDirty();
    return node;
}

json Editor::runPhysicsTest(SceneNode* node) {
    PhysicsReplay::Test t;
    json d = {{"name", node->name}, {"object_id", node->id}};
    if (!PhysicsReplay::testFromJson(json::parse(node->value.s, nullptr, false), t)) {
        d["passed"] = false;
        d["message"] = "This isn't a physics test (its Value isn't one).";
    } else {
        const PhysicsReplay::Result r = PhysicsReplay::run(Serializer::saveScene(*m_scene), t);
        d["passed"] = r.passed;
        d["message"] = r.message;
        d["worst"] = std::round(std::min(r.worst, 1e6f) * 1000.0f) / 1000.0f;
        if (!r.worstPart.empty()) { d["part"] = r.worstPart; d["at"] = r.worstTime; }
        d["seconds"] = t.seconds;
        d["tolerance"] = t.tolerance;
    }
    m_replayResults[node->id] = d;
    return d;
}

void Editor::renderPhysicsReplay() {
    if (!m_showReplay) { if (!m_state.replayGhosts.empty() || !m_state.replayTrail.empty()) showReplayFrame(-1); return; }
    ImGui::SetNextWindowSize(ImVec2(460, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Physics Replay", &m_showReplay)) { ImGui::End(); showReplayFrame(-1); return; }
    ImGui::TextWrapped("Runs the physics from how your scene is now (scripts and players don't run), so you can watch it again frame by frame.");
    ImGui::Spacing();
    ImGui::BeginDisabled(m_playing);
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Seconds", &m_replaySeconds, 1.0f, 30.0f, "%.0f");
    ImGui::SameLine();
    if (ImGui::Button("Record")) recordPhysics(m_replaySeconds);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Your scene doesn't change: this runs on a copy of it.");
    ImGui::EndDisabled();

    if (!m_replay.empty() && !m_playing) {
        const int last = (int)m_replay.frames.size() - 1;
        ImGui::TextDisabled("%d loose parts, %.1f seconds, recorded in %.0f ms.", (int)m_replay.ids.size(), m_replay.seconds(), m_replay.ms);
        // Playing it back: the clock moves the frame along.
        if (m_replayPlaying) {
            m_replayClock += ImGui::GetIO().DeltaTime * m_replaySpeed;
            m_replayFrame = std::min(last, (int)(m_replayClock / m_replay.step));
            if (m_replayFrame >= last) m_replayPlaying = false;
        }
        if (ImGui::Button("|<")) { m_replayFrame = 0; m_replayPlaying = false; }
        ImGui::SameLine();
        if (ImGui::Button("<")) { m_replayFrame = std::max(0, m_replayFrame - 1); m_replayPlaying = false; }
        ImGui::SameLine();
        if (ImGui::Button(m_replayPlaying ? "Pause" : "Play")) {
            if (!m_replayPlaying && m_replayFrame >= last) m_replayFrame = 0;
            m_replayPlaying = !m_replayPlaying;
            m_replayClock = m_replayFrame * m_replay.step;
        }
        ImGui::SameLine();
        if (ImGui::Button(">")) { m_replayFrame = std::min(last, m_replayFrame + 1); m_replayPlaying = false; }
        ImGui::SameLine();
        if (ImGui::Button(">|")) { m_replayFrame = last; m_replayPlaying = false; }
        ImGui::SameLine();
        static const float speeds[] = {0.1f, 0.25f, 0.5f, 1.0f};
        static const char* const speedNames[] = {"0.1x", "0.25x", "0.5x", "1x"};
        int sp = 3;
        for (int i = 0; i < 4; ++i) if (std::abs(m_replaySpeed - speeds[i]) < 1e-3f) sp = i;
        ImGui::SetNextItemWidth(70);
        if (ImGui::Combo("##speed", &sp, speedNames, 4)) m_replaySpeed = speeds[sp];
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow motion");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##frame", &m_replayFrame, 0, last, "")) m_replayPlaying = false;
        ImGui::Text("Frame %d of %d, %.2f s", m_replayFrame, last, m_replayFrame * m_replay.step);
        ImGui::Checkbox("Show the selected part's path", &m_replayTrail);
        if (ImGui::Button("Keep this frame")) {
            PhysicsReplay::apply(*m_scene, m_replay.frames[m_replayFrame]);
            char text[96];
            std::snprintf(text, sizeof text, "Physics Replay: the parts are now where they were at %.2f s.", m_replayFrame * m_replay.step);
            Log::system(text);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Moves the parts in your scene to where they are in this frame (one Ctrl+Z puts them back). Handy for letting a pile settle.");
        showReplayFrame(m_replayFrame);
    } else {
        showReplayFrame(-1);
    }

    ImGui::SeparatorText("Physics tests");
    ImGui::TextWrapped("A test remembers where the parts end up. Run it after changing the physics: it fails if they act differently.");
    ImGui::BeginDisabled(m_playing);
    ImGui::SetNextItemWidth(170);
    ImGui::InputText("##testname", m_replayTestName, sizeof m_replayTestName);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat("Allowed", &m_replayTolerance, 0.01f, 0.01f, 5.0f, "%.2f studs");
    ImGui::SameLine();
    static std::string saveError;
    if (ImGui::Button("Save as test")) {
        saveError.clear();
        if (SceneNode* n = savePhysicsTest(m_replayTestName, m_replaySeconds, m_replayTolerance, &saveError)) m_scene->select(n);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Records the seconds above and keeps it in ServerStorage > PhysicsTests (saved with your game).");
    if (!saveError.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", saveError.c_str());
    const std::vector<SceneNode*> tests = physicsTests();
    if (tests.empty()) {
        ImGui::TextDisabled("No tests yet.");
    } else {
        if (ImGui::Button("Run all")) for (SceneNode* t : tests) runPhysicsTest(t);
        for (SceneNode* t : tests) {
            ImGui::PushID((int)t->id);
            if (ImGui::SmallButton("Run")) runPhysicsTest(t);
            ImGui::SameLine();
            if (ImGui::SmallButton("Save again")) {
                PhysicsReplay::Test old;   // (keeps its own seconds and allowance)
                if (!PhysicsReplay::testFromJson(json::parse(t->value.s, nullptr, false), old)) old.seconds = m_replaySeconds;
                savePhysicsTest(t->name, old.seconds, old.tolerance, nullptr);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("The new way it acts is right: remember it instead.");
            ImGui::SameLine();
            ImGui::TextUnformatted(t->name.c_str());
            auto it = m_replayResults.find(t->id);
            if (it != m_replayResults.end()) {
                const bool ok = it->second.value("passed", false);
                ImGui::Indent();
                if (ok) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Passed");
                else    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Failed");
                ImGui::SameLine();
                ImGui::TextWrapped("%s", it->second.value("message", std::string()).c_str());
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndDisabled();
    ImGui::End();
}
