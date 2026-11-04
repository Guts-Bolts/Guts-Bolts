#include "LiveProfiler.h"
#include "../core/FrameProfile.h"

#include <imgui.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace LiveProfiler {

const char* niceName(const char* section, char* buffer, int size) {
    if (std::strncmp(section, "game: ", 6) == 0) {
        std::snprintf(buffer, (size_t)size, "%s", section + 6);
        buffer[0] = (char)std::toupper((unsigned char)buffer[0]);
    } else if (std::strncmp(section, "draw: ", 6) == 0) {
        std::snprintf(buffer, (size_t)size, "Drawing: %s", section + 6);
    } else {
        std::snprintf(buffer, (size_t)size, "%s", section);
    }
    return buffer;
}

namespace {

bool paused = false;
std::vector<FrameProfile::Row> heldSections, heldScripts;
std::vector<float> heldFrames;

// A bar as long as the time (a full frame at 60 fps = the whole width), red when it's a lot.
void bar(float ms, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight() * 0.7f, y = p.y + ImGui::GetTextLineHeight() * 0.15f;
    const float w = std::min(1.0f, ms / 16.7f) * width;
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + width, y + h), IM_COL32(255, 255, 255, 18), 3.0f);
    const ImU32 col = ms > 8.0f ? IM_COL32(235, 80, 70, 255) : ms > 3.0f ? IM_COL32(240, 170, 60, 255) : IM_COL32(90, 190, 110, 255);
    if (w > 0.5f) dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), col, 3.0f);
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
}

void table(const char* id, const std::vector<FrameProfile::Row>& rows, bool scripts) {
    if (!ImGui::BeginTable(id, 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) return;
    ImGui::TableSetupColumn(scripts ? "Script" : "Part of the frame", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, 58.0f);
    ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Slowest", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableHeadersRow();
    char name[160];
    for (const FrameProfile::Row& r : rows) {
        if (r.name == "frame (all of it)") continue;   // (that's the whole frame, shown at the top)
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(scripts ? r.name.c_str() : niceName(r.name.c_str(), name, sizeof(name)));
        if (scripts && r.avg > 4.0f && ImGui::IsItemHovered())
            ImGui::SetTooltip("This script takes a lot of each frame. Look for a loop that runs\n"
                              "many times without task.wait(), or work done in Heartbeat\n"
                              "that only needs doing once.");
        ImGui::TableNextColumn();
        ImGui::Text("%.2f ms", r.now);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f ms", r.avg);
        ImGui::TableNextColumn();
        ImGui::Text("%.2f ms", r.max);
        ImGui::TableNextColumn();
        bar(r.avg, 100.0f);
    }
    ImGui::EndTable();
}

} // namespace

void draw(bool* open) {
    FrameProfile::live() = open && *open;
    if (!open || !*open) return;
    ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Live Profiler", open)) {
        ImGui::End();
        return;
    }
    if (!paused) {
        heldSections = FrameProfile::sections();
        heldScripts = FrameProfile::scripts();
        heldFrames = FrameProfile::frames();
    }
    float avg = 0.0f, worst = 0.0f;
    for (float f : heldFrames) { avg += f; worst = std::max(worst, f); }
    if (!heldFrames.empty()) avg /= (float)heldFrames.size();
    const ImVec4 colour = avg > 33.0f ? ImVec4(0.95f, 0.35f, 0.3f, 1) : avg > 17.5f ? ImVec4(0.95f, 0.7f, 0.3f, 1) : ImVec4(0.45f, 0.85f, 0.45f, 1);
    ImGui::TextColored(colour, "%.0f frames a second", avg > 0.01f ? 1000.0f / avg : 0.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("(a frame takes %.1f ms on average, %.1f ms at worst)", avg, worst);
    if (!heldFrames.empty())
        ImGui::PlotLines("##frames", heldFrames.data(), (int)heldFrames.size(), 0, "frame time (16.7 ms = 60 fps)", 0.0f,
                         std::max(33.4f, worst * 1.1f), ImVec2(-1, 60));
    if (ImGui::Button(paused ? "Carry on" : "Pause")) paused = !paused;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Freeze the numbers to read them");
    ImGui::SameLine();
    if (ImGui::Button("Clear")) FrameProfile::clear();
    ImGui::SameLine();
    ImGui::TextDisabled("Measured over the last 2 seconds.");

    ImGui::SeparatorText("Where each frame goes");
    if (heldSections.empty()) ImGui::TextDisabled("Press Play to see what the game spends its time on.");
    else table("##sections", heldSections, false);

    ImGui::SeparatorText("Scripts");
    if (heldScripts.empty()) ImGui::TextDisabled("No script code has run yet.");
    else table("##scripts", heldScripts, true);
    ImGui::TextDisabled("Drawing times are the computer's side; the graphics card works in the background.");
    ImGui::End();
}

} // namespace LiveProfiler
