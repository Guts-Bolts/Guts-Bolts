#include "ToolboxPanel.h"
#include "../../renderer/MeshLibrary.h"
#include "../../scene/Scene.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
// Library categories: the first three come from the online Library, the rest are built into Studio.
const char* const kCategories[] = {"Models", "Decals", "Audio", "Parts", "Ready-made", "Objects", "Lights", "Constraints"};
const int kCategoryCount = 8;
const char* const kInventory[] = {"My Models", "My Decals", "My Audio"};

const ImU32 kTileBg    = IM_COL32(236, 237, 240, 255);
const ImU32 kTileHover = IM_COL32(66, 150, 250, 255);
const ImU32 kLink      = IM_COL32(88, 166, 255, 255);

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The gold "official" badge (like Roblox's): a shield with a circle, a
// triangle and a square on it.
void officialBadge(ImDrawList* dl, ImVec2 c, float s) {
    const ImU32 gold = IM_COL32(245, 172, 38, 255), rim = IM_COL32(255, 255, 255, 255);
    ImVec2 pts[] = {{c.x - s * 0.5f, c.y - s * 0.46f}, {c.x + s * 0.5f, c.y - s * 0.46f}, {c.x + s * 0.5f, c.y + s * 0.08f},
                    {c.x, c.y + s * 0.55f}, {c.x - s * 0.5f, c.y + s * 0.08f}};
    // A white outline first, so it stands out on any picture.
    ImVec2 big[5];
    for (int i = 0; i < 5; ++i) big[i] = ImVec2(c.x + (pts[i].x - c.x) * 1.18f, c.y + (pts[i].y - c.y) * 1.18f);
    dl->AddConvexPolyFilled(big, 5, rim);
    dl->AddConvexPolyFilled(pts, 5, gold);
    const ImU32 w = IM_COL32(255, 255, 255, 255);
    dl->AddCircleFilled(ImVec2(c.x - s * 0.17f, c.y - s * 0.17f), s * 0.13f, w, 12);
    dl->AddTriangleFilled(ImVec2(c.x + s * 0.18f, c.y - s * 0.32f), ImVec2(c.x + s * 0.34f, c.y - s * 0.04f),
                          ImVec2(c.x + s * 0.02f, c.y - s * 0.04f), w);
    dl->AddRectFilled(ImVec2(c.x - s * 0.1f, c.y + s * 0.0f), ImVec2(c.x + s * 0.14f, c.y + s * 0.24f), w);
}

// The name under a tile: up to two lines, then "...".
void tileName(ImDrawList* dl, ImVec2 p, float width, const std::string& name, ImU32 col) {
    auto fits = [&](const std::string& t) { return ImGui::CalcTextSize(t.c_str()).x <= width; };
    // The first line: as much as fits, broken at a space when there is one.
    size_t n = 0;
    while (n < name.size() && fits(name.substr(0, n + 1))) ++n;
    if (n < name.size()) {
        size_t space = name.rfind(' ', n);
        if (space != std::string::npos && space > 0) n = space;
    }
    std::string first = name.substr(0, n), rest = n < name.size() ? name.substr(n) : "";
    while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
    if (!fits(rest)) {
        while (!rest.empty() && !fits(rest + "...")) rest.pop_back();
        rest += "...";
    }
    dl->AddText(p, col, first.c_str());
    if (!rest.empty()) dl->AddText(ImVec2(p.x, p.y + ImGui::GetTextLineHeight()), col, rest.c_str());
}

// The three tab icons: a book (Library), a grid of boxes (Inventory), a clock (Recent).
void tabIcon(ImDrawList* dl, ImVec2 c, float s, int which, ImU32 col) {
    if (which == 0) {   // an open book: two pages with a spine
        dl->AddQuadFilled(ImVec2(c.x - s * 0.48f, c.y - s * 0.3f), ImVec2(c.x - s * 0.04f, c.y - s * 0.22f),
                          ImVec2(c.x - s * 0.04f, c.y + s * 0.42f), ImVec2(c.x - s * 0.48f, c.y + s * 0.34f), col);
        dl->AddQuadFilled(ImVec2(c.x + s * 0.04f, c.y - s * 0.22f), ImVec2(c.x + s * 0.48f, c.y - s * 0.3f),
                          ImVec2(c.x + s * 0.48f, c.y + s * 0.34f), ImVec2(c.x + s * 0.04f, c.y + s * 0.42f), col);
    } else if (which == 1) {
        float g = s * 0.08f, h = s * 0.4f;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x) {
                ImVec2 a(c.x - h + x * (h + g), c.y - h + y * (h + g));
                dl->AddRectFilled(a, ImVec2(a.x + h - g, a.y + h - g), col, 1);
            }
    } else {
        dl->AddCircleFilled(c, s * 0.45f, col, 20);
        const ImU32 bg = ImGui::GetColorU32(ImGuiCol_WindowBg);
        dl->AddLine(c, ImVec2(c.x, c.y - s * 0.3f), bg, std::max(1.5f, s * 0.1f));
        dl->AddLine(c, ImVec2(c.x + s * 0.2f, c.y + s * 0.1f), bg, std::max(1.5f, s * 0.1f));
    }
}

// A magnifying glass.
void searchIcon(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    dl->AddCircle(ImVec2(c.x - s * 0.1f, c.y - s * 0.1f), s * 0.3f, col, 16, std::max(1.5f, s * 0.11f));
    dl->AddLine(ImVec2(c.x + s * 0.12f, c.y + s * 0.12f), ImVec2(c.x + s * 0.42f, c.y + s * 0.42f), col, std::max(2.0f, s * 0.13f));
}
} // namespace

ToolboxPanel::ToolboxPanel(Actions actions) : m_do(std::move(actions)) {}

void ToolboxPanel::remember(const ToolboxTile& t) {
    m_recent.erase(std::remove_if(m_recent.begin(), m_recent.end(), [&](const ToolboxTile& r) { return r.key == t.key; }),
                   m_recent.end());
    m_recent.insert(m_recent.begin(), t);
    if (m_recent.size() > 30) m_recent.pop_back();
}

// Things built into Studio. They're Guts&Bolts' own, so they're official.
std::vector<ToolboxTile> ToolboxPanel::builtIn(int category) {
    std::vector<ToolboxTile> out;
    auto pictureOf = [this](const std::string& key, std::function<void(Scene&)> build) {
        return [this, key, build]() -> unsigned { return m_do.thumbnail ? m_do.thumbnail(key, build) : 0; };
    };
    auto add = [&](std::string key, std::string name, std::string tip, Icons::Id icon, std::function<void()> use,
                   std::function<unsigned()> picture = nullptr) {
        ToolboxTile t;
        t.key = std::move(key); t.name = std::move(name); t.tip = std::move(tip); t.icon = icon;
        t.official = true; t.use = std::move(use); t.picture = std::move(picture);
        out.push_back(std::move(t));
    };
    switch (category) {
    case 3: {   // Parts
        struct P { const char* name; PrimitiveType type; Icons::Id icon; glm::vec3 size; };
        static const P parts[] = {
            {"Cube", PrimitiveType::Cube, Icons::Id::Part, {1, 1, 1}},
            {"Sphere", PrimitiveType::Sphere, Icons::Id::Sphere, {1, 1, 1}},
            {"Plane", PrimitiveType::Plane, Icons::Id::Plane, {2, 1, 2}},
            {"Cylinder", PrimitiveType::Cylinder, Icons::Id::Cylinder, {1, 1, 1}},
        };
        for (const P& p : parts) {
            PrimitiveType type = p.type; glm::vec3 size = p.size;
            add(std::string("part:") + p.name, p.name, "A plain part to build with.", p.icon,
                [this, type] { if (m_do.spawnPart) m_do.spawnPart(type); },
                pictureOf(std::string("part:") + p.name, [type, size](Scene& s) {
                    SceneNode* n = s.addNode("Part", type, MeshLibrary::get(type));
                    n->transform.scale = size;
                    n->color = {0.64f, 0.64f, 0.66f};
                }));
        }
        break;
    }
    case 4:   // Ready-made
        for (const PremadeInfo& p : premadeList()) {
            Premade kind = p.kind;
            std::string key = std::string("premade:") + p.name;
            add(key, p.name, p.tip, Icons::Id::Model, [this, kind] { if (m_do.spawnPremade) m_do.spawnPremade(kind); },
                pictureOf(key, [kind](Scene& s) { buildPremade(s, kind, glm::vec3(0.0f)); }));
        }
        break;
    case 5:   // Objects
        add("obj:script", "Script", "A Script inside the selected object (or the Workspace).", Icons::Id::Script,
            [this] { if (m_do.addScript) m_do.addScript(); });
        add("obj:model", "Model", "An empty folder for grouping objects.", Icons::Id::Model,
            [this] { if (m_do.addModel) m_do.addModel(); });
        add("obj:sound", "Sound", "A sound effect or music. Inside a part, it plays from that spot.\n"
            "Tick Autoplay + Looped for background music.", Icons::Id::Sound, [this] { if (m_do.addSound) m_do.addSound(); });
        break;
    case 6:   // Lights
        add("light:point", "Point Light", "Shines in every direction, like a light bulb.\nGoes inside the selected part.",
            Icons::Id::Light, [this] { if (m_do.addLight) m_do.addLight(LightType::Point); });
        add("light:spot", "Spot Light", "Shines in a cone (downwards - rotate it to aim).\nGoes inside the selected part.",
            Icons::Id::Light, [this] { if (m_do.addLight) m_do.addLight(LightType::Spot); });
        break;
    case 7: {   // Constraints
        struct C { const char* name; int type; const char* tip; };
        static const C items[] = {
            {"Rope", 0, "Holds parts within a distance - can go slack"},
            {"Rod", 1, "Keeps parts at an exact distance"},
            {"Spring", 2, "A bouncy spring between two parts"},
            {"Weld", 3, "Glues two parts together"},
            {"Hinge", 4, "Lets a part swing around a point (like a door)"},
            {"Motor", 5, "A hinge that spins by itself (wheels, fans)"},
            {"Ball Socket", 6, "Joins two parts at a point and lets them turn any way (ragdolls, chains)"},
            {"Prismatic", 7, "A slider: the second part slides along a line (drawers, lifts, doors)"},
            {"Cylindrical", 8, "Slides along a line and spins around it (pistons, drills)"},
        };
        for (const C& c : items) {
            int type = c.type;
            add(std::string("constraint:") + c.name, c.name, std::string(c.tip) + "\nClick it, then click two parts.",
                Icons::Id::Constraint, [this, type] { if (m_do.startConnect) m_do.startConnect(type); });
        }
        break;
    }
    default: break;
    }
    // The search box filters these too.
    if (!m_query.empty()) {
        std::string q = lower(m_query);
        out.erase(std::remove_if(out.begin(), out.end(), [&](const ToolboxTile& t) {
            return lower(t.name).find(q) == std::string::npos && lower(t.tip).find(q) == std::string::npos;
        }), out.end());
    }
    return out;
}

void ToolboxPanel::drawTabs() {
    static const char* names[] = {"Library", "Inventory", "Recent"};
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight() + 10.0f;
    const float w = ImGui::GetContentRegionAvail().x / 3.0f;
    const bool narrow = w < ImGui::CalcTextSize("Inventory").x + 34.0f;   // icons only
    ImVec2 start = ImGui::GetCursorScreenPos();
    for (int i = 0; i < 3; ++i) {
        ImGui::PushID(i);
        ImVec2 a(start.x + w * i, start.y), b(a.x + w, a.y + h);
        ImGui::SetCursorScreenPos(a);
        if (ImGui::InvisibleButton("##tab", ImVec2(w, h))) { m_tab = i; m_reload = true; }
        bool on = m_tab == i, hover = ImGui::IsItemHovered();
        if (on) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg));
        if (on) dl->AddRectFilled(a, ImVec2(b.x, a.y + 3), kTileHover);
        ImU32 col = on || hover ? IM_COL32(240, 240, 242, 255) : IM_COL32(150, 152, 158, 255);
        float iconS = h * 0.5f;
        if (narrow) {
            tabIcon(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + 1), iconS, i, col);
            if (hover) ImGui::SetTooltip("%s", names[i]);
        } else {
            ImVec2 ts = ImGui::CalcTextSize(names[i]);
            float x = (a.x + b.x) * 0.5f - (ts.x + iconS + 6) * 0.5f;
            tabIcon(dl, ImVec2(x + iconS * 0.5f, (a.y + b.y) * 0.5f + 1), iconS, i, col);
            dl->AddText(ImVec2(x + iconS + 6, (a.y + b.y - ts.y) * 0.5f), col, names[i]);
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + h));
    ImGui::Separator();
}

void ToolboxPanel::drawSearchRow(const char* const* names, int count) {
    int& cat = m_tab == 1 ? m_invCategory : m_category;
    ImGui::SetNextItemWidth(std::min(130.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    if (ImGui::BeginCombo("##cat", names[cat])) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(names[i], cat == i)) { cat = i; m_reload = true; }
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, 6);
    const float btn = ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - btn - 2));
    if (ImGui::InputTextWithHint("##q", "Search", &m_query, ImGuiInputTextFlags_EnterReturnsTrue)) m_reload = true;
    // A clear (x) inside the box while there's something typed.
    if (!m_query.empty()) {
        ImVec2 r = ImGui::GetItemRectMax(), l = ImGui::GetItemRectMin();
        ImVec2 c(r.x - btn * 0.5f, (l.y + r.y) * 0.5f);
        ImVec2 keep = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(c.x - btn * 0.4f, c.y - btn * 0.4f));
        if (ImGui::InvisibleButton("##clear", ImVec2(btn * 0.8f, btn * 0.8f))) { m_query.clear(); m_reload = true; }
        ImU32 col = ImGui::IsItemHovered() ? IM_COL32(240, 240, 240, 255) : IM_COL32(160, 160, 165, 255);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircle(c, btn * 0.28f, col, 16, 1.5f);
        float k = btn * 0.12f;
        dl->AddLine(ImVec2(c.x - k, c.y - k), ImVec2(c.x + k, c.y + k), col, 1.5f);
        dl->AddLine(ImVec2(c.x - k, c.y + k), ImVec2(c.x + k, c.y - k), col, 1.5f);
        ImGui::SetCursorScreenPos(keep);
    }
    ImGui::SameLine(0, 2);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##go", ImVec2(btn, btn))) m_reload = true;
    searchIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + btn * 0.5f, p.y + btn * 0.5f), btn * 0.62f, IM_COL32(220, 222, 228, 255));

    // "< All Models / Search Results for car"
    if (!m_query.empty()) {
        std::string all = std::string("< All ") + names[cat];
        ImGui::TextColored(ImVec4(0.35f, 0.65f, 1.0f, 1), "%s", all.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) { m_query.clear(); m_reload = true; }
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("/");
        ImGui::SameLine(0, 4);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(("Search Results for " + m_query).c_str());   // wraps in a narrow Toolbox
        ImGui::PopTextWrapPos();
    }
}

void ToolboxPanel::drawGrid(std::vector<ToolboxTile>& tiles) {
    const float gap = 10.0f, minTile = 78.0f;
    const float avail = ImGui::GetContentRegionAvail().x;
    const int cols = std::max(1, (int)((avail + gap) / (minTile + gap)));
    const float size = std::floor((avail - gap * (cols - 1)) / cols);
    const float line = ImGui::GetTextLineHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < tiles.size(); ++i) {
        ToolboxTile& t = tiles[i];
        ImGui::PushID(t.key.c_str());
        if (i % cols) ImGui::SameLine(0, gap);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##tile", ImVec2(size, size + line * 2 + 8));
        bool hover = ImGui::IsItemHovered();
        ImVec2 a = p, b(p.x + size, p.y + size);
        const float round = 4.0f;
        dl->AddRectFilled(a, b, kTileBg, round);
        unsigned tex = t.picture ? t.picture() : 0;
        if (tex) dl->AddImageRounded((ImTextureID)(intptr_t)tex, a, b, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, round);   // pictures load bottom row first
        else Icons::draw(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), size * 0.45f, t.icon, IM_COL32(70, 74, 84, 255));
        if (hover) dl->AddRect(a, b, kTileHover, round, 0, 2.0f);
        if (t.official) officialBadge(dl, ImVec2(b.x - size * 0.13f, b.y - size * 0.12f), size * 0.2f);
        tileName(dl, ImVec2(a.x, b.y + 4), size, t.name, hover ? IM_COL32(150, 200, 255, 255) : kLink);
        if (hover) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(t.name.c_str());
            if (!t.creator.empty()) ImGui::TextDisabled("by %s", t.creator.c_str());
            if (t.official) ImGui::TextColored(ImVec4(0.96f, 0.68f, 0.15f, 1), "Official: made by Guts&Bolts staff, safe to use");
            if (!t.tip.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22);
                ImGui::TextUnformatted(t.tip.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTooltip();
        }
        if (clicked && t.use) { t.use(); remember(t); }
        ImGui::PopID();
    }
}

void ToolboxPanel::render() {
    ImGui::Begin("Toolbox");
    drawTabs();
    ImGui::Spacing();

    std::vector<ToolboxTile> tiles;
    if (m_tab == 2) {
        ImGui::TextDisabled("What you've inserted lately.");
        tiles = m_recent;
        if (!m_query.empty()) m_query.clear();
    } else if (m_tab == 1) {
        drawSearchRow(kInventory, 3);
        if (m_do.library) tiles = m_do.library(true, m_invCategory, m_query, m_reload, m_status);
    } else {
        drawSearchRow(kCategories, kCategoryCount);
        if (m_category < 3) { if (m_do.library) tiles = m_do.library(false, m_category, m_query, m_reload, m_status); }
        else tiles = builtIn(m_category);
    }
    m_reload = false;
    ImGui::Separator();

    ImGui::BeginChild("##tiles", ImVec2(0, 0), false);
    if (!m_status.empty() && m_tab != 2 && !(m_tab == 0 && m_category >= 3)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", m_status.c_str());
        ImGui::PopTextWrapPos();
    }
    if (tiles.empty()) {
        ImGui::TextDisabled(m_tab == 2 ? "Nothing yet: things you insert show up here."
                            : !m_query.empty() ? "Nothing matches that search." : "Nothing here yet.");
        if (m_tab == 0 && m_category < 3) {
            ImGui::TextDisabled("Try");
            ImGui::SameLine();
            if (ImGui::SmallButton("Ready-made")) m_category = 4;
            ImGui::SameLine();
            if (ImGui::SmallButton("Parts")) m_category = 3;
        }
    } else {
        ImGui::Dummy(ImVec2(0, 2));
        drawGrid(tiles);
    }
    ImGui::EndChild();
    ImGui::End();
}
