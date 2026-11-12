#pragma once
// The property grid: Studio's Properties, Lighting and Player panels laid out like
// Roblox Studio's Properties window. The name is on the left, the value on the right,
// with a thin line under each row. Numbers are plain boxes (click one and type, or
// drag it sideways) instead of slider bars.
//
// Each function here has the same name and arguments as the ImGui one it wraps, so a
// panel only swaps "ImGui::" for "PG::" on its property widgets.
//
// PG::filter(): while it holds some text, only the rows whose name has that text are
// drawn (the Properties panel's "Filter Properties" box).
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>

namespace PG {

inline std::string& filter() {
    static std::string text;   // lower case; empty = show everything
    return text;
}
inline bool filtering() { return !filter().empty(); }

inline bool matches(const std::string& label) {
    if (!filtering()) return true;
    std::string l = label;
    for (char& c : l) c = (char)std::tolower((unsigned char)c);
    return l.find(filter()) != std::string::npos;
}

// Something that isn't drawn: an item nothing can hover, so the tooltip written after
// it in the panel's code doesn't show up on the row above.
inline void nothing() {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const ImVec2 pos = w->DC.CursorPos, prev = w->DC.CursorPosPrevLine;
    const bool sameLine = w->DC.IsSameLine;
    ImGui::ItemSize(ImVec2(0, 0));
    ImGui::ItemAdd(ImRect(ImVec2(-10, -10), ImVec2(-10, -10)), 0);
    w->DC.CursorPos = pos;
    w->DC.CursorPosPrevLine = prev;
    w->DC.IsSameLine = sameLine;
}

// One row: draws the name on the left and leaves the cursor where the value goes.
struct Row {
    std::string id;      // the ImGui label to give the widget ("##Transparency")
    bool skip = false;   // filtered out: draw nothing
    explicit Row(const char* label) {
        const char* hash = std::strstr(label, "##");
        const std::string name = hash ? std::string(label, hash) : std::string(label);
        if (name.empty()) { id = label; return; }   // (no name: the caller laid it out itself)
        if (!matches(name)) { skip = true; nothing(); return; }
        id = std::string("##") + label;

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        const float lineX = w->WorkRect.Min.x + w->DC.Indent.x;
        const float full = w->WorkRect.Max.x - lineX;
        const float split = lineX + std::max(86.0f, std::floor(full * 0.36f));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 line = IM_COL32(24, 24, 24, 255);
        dl->AddLine(ImVec2(w->WorkRect.Min.x, p.y + h + 1), ImVec2(w->WorkRect.Max.x, p.y + h + 1), line);
        dl->AddLine(ImVec2(split - 5, p.y - 1), ImVec2(split - 5, p.y + h + 1), line);

        ImGui::AlignTextToFramePadding();
        const float room = split - 9 - p.x;
        if (room > 8) {
            ImGui::PushClipRect(p, ImVec2(split - 9, p.y + h), true);
            ImGui::TextUnformatted(name.c_str());
            ImGui::PopClipRect();
            if (ImGui::CalcTextSize(name.c_str()).x > room && ImGui::IsMouseHoveringRect(p, ImVec2(split - 9, p.y + h)) &&
                ImGui::IsWindowHovered())
                ImGui::SetTooltip("%s", name.c_str());
            ImGui::SameLine(0, 0);
        }
        ImGui::SetCursorScreenPos(ImVec2(std::max(split, p.x), p.y));
        ImGui::SetNextItemWidth(-1.0f);
    }
};

// --- Values ---------------------------------------------------------------

inline bool Checkbox(const char* label, bool* v) {
    Row r(label);
    return r.skip ? false : ImGui::Checkbox(r.id.c_str(), v);
}

template <class... A> bool DragFloat(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::DragFloat(r.id.c_str(), std::forward<A>(a)...);
}
// (Two or three numbers side by side: less padding, so more of each number fits.)
template <class... A> bool DragFloat2(const char* label, A&&... a) {
    Row r(label);
    if (r.skip) return false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(2, 0));
    const bool changed = ImGui::DragFloat2(r.id.c_str(), std::forward<A>(a)...);
    ImGui::PopStyleVar(2);
    return changed;
}
template <class... A> bool DragFloat3(const char* label, A&&... a) {
    Row r(label);
    if (r.skip) return false;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, ImGui::GetStyle().FramePadding.y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(2, 0));
    const bool changed = ImGui::DragFloat3(r.id.c_str(), std::forward<A>(a)...);
    ImGui::PopStyleVar(2);
    return changed;
}
template <class... A> bool DragInt(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::DragInt(r.id.c_str(), std::forward<A>(a)...);
}

// What used to be a slider bar: a number box that stays between `lo` and `hi`.
inline bool SliderFloat(const char* label, float* v, float lo, float hi, const char* format = "%.3f", ImGuiSliderFlags flags = 0) {
    Row r(label);
    if (r.skip) return false;
    const float speed = (flags & ImGuiSliderFlags_Logarithmic) ? 0.01f : std::max(0.001f, (hi - lo) / 250.0f);
    return ImGui::DragFloat(r.id.c_str(), v, speed, lo, hi, format, flags | ImGuiSliderFlags_AlwaysClamp);
}
inline bool SliderInt(const char* label, int* v, int lo, int hi, const char* format = "%d", ImGuiSliderFlags flags = 0) {
    Row r(label);
    if (r.skip) return false;
    return ImGui::DragInt(r.id.c_str(), v, std::max(0.1f, (float)(hi - lo) / 250.0f), lo, hi, format, flags | ImGuiSliderFlags_AlwaysClamp);
}

inline bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0) {
    Row r(label);
    return r.skip ? false : ImGui::ColorEdit3(r.id.c_str(), col, flags | ImGuiColorEditFlags_NoLabel);
}

template <class... A> bool Combo(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::Combo(r.id.c_str(), std::forward<A>(a)...);
}
inline bool BeginCombo(const char* label, const char* preview, ImGuiComboFlags flags = 0) {
    Row r(label);
    return r.skip ? false : ImGui::BeginCombo(r.id.c_str(), preview, flags);
}

template <class... A> bool InputText(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::InputText(r.id.c_str(), std::forward<A>(a)...);
}
template <class... A> bool InputTextWithHint(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::InputTextWithHint(r.id.c_str(), std::forward<A>(a)...);
}
template <class... A> bool InputInt(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::InputInt(r.id.c_str(), std::forward<A>(a)...);
}
template <class... A> bool InputDouble(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::InputDouble(r.id.c_str(), std::forward<A>(a)...);
}
template <class... A> bool InputScalar(const char* label, A&&... a) {
    Row r(label);
    return r.skip ? false : ImGui::InputScalar(r.id.c_str(), std::forward<A>(a)...);
}

// --- Around the rows ------------------------------------------------------

// A category ("Appearance", "Transform"): a quiet header with an arrow, like Roblox's.
// While filtering there are no headers: every matching row shows.
inline bool CollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0) {
    if (filtering()) return true;
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.145f, 0.145f, 0.145f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
    const bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(3);
    return open;
}

inline void SeparatorText(const char* label) {
    if (filtering()) return;
    ImGui::SeparatorText(label);
}
inline void Spacing() {
    if (!filtering()) ImGui::Spacing();
}
inline void Separator() {
    if (!filtering()) ImGui::Separator();
}
// Notes and help text: hidden while filtering.
template <class... A> void TextDisabled(const char* fmt, A&&... a) {
    if (filtering()) return;
    ImGui::TextDisabled(fmt, std::forward<A>(a)...);
}
template <class... A> void TextWrapped(const char* fmt, A&&... a) {
    if (filtering()) return;
    ImGui::TextWrapped(fmt, std::forward<A>(a)...);
}
inline bool Button(const char* label, const ImVec2& size = ImVec2(0, 0)) {
    if (filtering()) {
        const char* hash = std::strstr(label, "##");
        if (!matches(hash ? std::string(label, hash) : std::string(label))) { nothing(); return false; }
    }
    return ImGui::Button(label, size);
}

// The look of the grid while a panel draws its rows: flat boxes, rows close together.
struct Style {
    Style() {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5, 3));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.118f, 0.118f, 0.118f, 1.0f));
    }
    ~Style() {
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
    }
};

} // namespace PG
