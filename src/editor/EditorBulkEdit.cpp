// Find and Change (Edit > Find and Change, Ctrl+Shift+E): pick out objects by kind,
// name, tag or a property's value ("parts with Mass under 2, but not tagged
// Lightweight"), then change one property on all of them at once: set it, add to it
// or multiply it. Preview shows every old and new value first. It all happens in one
// frame, so one Ctrl+Z puts everything back. Searches can be saved and used again.
//
// Reading and changing values goes through Lua (the same names and rules a script has),
// so every property a script can set works here too.

#include "Editor.h"
#include "../core/I18n.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/Serializer.h"

#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <nlohmann/json.hpp>
#include <sstream>

namespace {

const char* const kWhereOps[] = {"=", "is not", "<", ">", "<=", ">=", "contains"};
const char* const kChangeOps[] = {"Set to", "Add", "Multiply by", "Add tag", "Remove tag"};

std::string q(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c != '\r') out += c;
    }
    return out + "\"";
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

// Runs for each object: is it the right kind, does the "where" rule match, and (with a
// change) what's the new value. Prints one line per match: id, old, new, error.
const char* const kBulkLua = R"LUA(
local P = __bulk
local function trim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
local function nums(s) local t = {} for x in s:gmatch("[-+]?[%d%.]+[eE]?[-+]?%d*") do t[#t + 1] = tonumber(x) end return t end
local function kindOf(v)
  local t = type(v)
  if t ~= "userdata" then return t end
  local ok, r = pcall(function() return v.ClassName end)
  if ok and type(r) == "string" then return "Instance" end
  ok, r = pcall(function() return v.LookVector end)
  if ok and r ~= nil then return "CFrame" end
  ok, r = pcall(function() return v.R end)
  if ok and type(r) == "number" then return "Color3" end
  ok, r = pcall(function() return v.Z end)
  if ok and type(r) == "number" then return "Vector3" end
  ok, r = pcall(function() return v.Y end)
  if ok and type(r) == "number" then return "Vector2" end
  return "other"
end
local function fmt(n) local s = string.format("%.3f", n):gsub("0+$", ""):gsub("%.$", "") return s end
local function show(v)
  local k = kindOf(v)
  if k == "number" then return fmt(v) end
  if k == "boolean" or k == "string" then return tostring(v) end
  if k == "Vector3" then return fmt(v.X) .. ", " .. fmt(v.Y) .. ", " .. fmt(v.Z) end
  if k == "Vector2" then return fmt(v.X) .. ", " .. fmt(v.Y) end
  if k == "Color3" then return math.floor(v.R * 255 + 0.5) .. ", " .. math.floor(v.G * 255 + 0.5) .. ", " .. math.floor(v.B * 255 + 0.5) end
  if k == "Instance" then return v.Name end
  return tostring(v)
end
-- Text typed in the window, read as the same type as `cur`.
local function parse(text, cur)
  local k = kindOf(cur)
  if k == "number" then local n = tonumber(trim(text)) if not n then error("\"" .. text .. "\" isn't a number", 0) end return n end
  if k == "boolean" then local s = trim(text):lower() return s == "true" or s == "yes" or s == "1" or s == "on" end
  if k == "Vector3" or k == "Color3" or k == "Vector2" then
    local t = nums(text)
    if #t == 0 then error("type numbers like 1, 2, 3", 0) end
    if #t == 1 then t = {t[1], t[1], t[1]} end
    if k == "Vector2" then return Vector2.new(t[1], t[2] or 0) end
    if k == "Color3" then return Color3.fromRGB(t[1], t[2] or 0, t[3] or 0) end
    return Vector3.new(t[1], t[2] or 0, t[3] or 0)
  end
  return text
end
local function clamp01(x) return math.max(0, math.min(1, x)) end
local function changed(cur, op, text)
  local k = kindOf(cur)
  if op == "Set to" then return parse(text, cur) end
  if k == "string" and op == "Add" then return cur .. text end
  local n = tonumber(trim(text))
  if op == "Multiply by" then
    if not n then error("multiply by a number, like 1.5", 0) end
    if k == "number" then return cur * n end
    if k == "Vector3" then return Vector3.new(cur.X * n, cur.Y * n, cur.Z * n) end
    if k == "Vector2" then return Vector2.new(cur.X * n, cur.Y * n) end
    if k == "Color3" then return Color3.new(clamp01(cur.R * n), clamp01(cur.G * n), clamp01(cur.B * n)) end
  else -- Add
    if k == "number" then if not n then error("add a number", 0) end return cur + n end
    local d = parse(text, cur)
    if k == "Vector3" then return Vector3.new(cur.X + d.X, cur.Y + d.Y, cur.Z + d.Z) end
    if k == "Vector2" then return Vector2.new(cur.X + d.X, cur.Y + d.Y) end
    if k == "Color3" then return Color3.new(clamp01(cur.R + d.R), clamp01(cur.G + d.G), clamp01(cur.B + d.B)) end
  end
  error("can't " .. op:lower() .. " a " .. k, 0)
end
local function matches(cur, op, text)
  local k = kindOf(cur)
  if k == "number" then
    local n = tonumber(trim(text))
    if not n then return false end
    if op == "=" then return math.abs(cur - n) < 1e-4 elseif op == "is not" then return math.abs(cur - n) >= 1e-4
    elseif op == "<" then return cur < n elseif op == ">" then return cur > n
    elseif op == "<=" then return cur <= n + 1e-4 elseif op == ">=" then return cur >= n - 1e-4 end
    return false
  end
  local a, b = show(cur):lower(), trim(text):lower()
  if k ~= "string" and k ~= "boolean" then local ok, v = pcall(parse, text, cur) if ok then b = show(v):lower() end end
  if op == "=" then return a == b elseif op == "is not" then return a ~= b
  elseif op == "contains" then return a:find(b, 1, true) ~= nil end
  return false
end
local function get(o, prop) local ok, v = pcall(function() return o[prop] end) if ok and v ~= nil and type(v) ~= "function" then return true, v end return false end
for _, id in ipairs(P.ids) do
  local o = __gb_byId(id)
  local keep = o ~= nil
  if keep and P.class ~= "" then
    local ok, isA = pcall(function() return o:IsA(P.class) end)
    keep = (ok and isA) or o.ClassName == P.class
  end
  local whereOld
  if keep and P.whereProp ~= "" then
    local has, v = get(o, P.whereProp)
    keep = has and matches(v, P.whereOp, P.whereVal)
    if keep then whereOld = show(v) end
  end
  if keep then
    local old, new, err = whereOld or "", "", ""
    if P.op == "Add tag" or P.op == "Remove tag" then
      local tag = trim(P.value)
      old = o:HasTag(tag) and "tagged" or "not tagged"
      new = P.op == "Add tag" and "tagged" or "not tagged"
      if P.apply and tag ~= "" then if P.op == "Add tag" then o:AddTag(tag) else o:RemoveTag(tag) end end
    elseif P.setProp ~= "" then
      local has, v = get(o, P.setProp)
      if not has then
        old, err = "", "has no " .. P.setProp
      else
        old = show(v)
        local ok, nv = pcall(changed, v, P.op, P.value)
        if not ok then err = tostring(nv)
        else
          new = show(nv)
          if P.apply then
            local ok2, e = pcall(function() o[P.setProp] = nv end)
            if not ok2 then err = tostring(e) else local _, now = get(o, P.setProp) new = show(now) end
          end
        end
      end
    end
    print("\1GBBULK" .. id .. "\t" .. old .. "\t" .. new .. "\t" .. err)
  end
end
)LUA";

nlohmann::json toJson(const Editor::BulkQuery& b) {
    return {{"name", b.name}, {"class", b.cls}, {"nameHas", b.nameHas}, {"tag", b.tag}, {"notTag", b.notTag},
            {"whereProp", b.whereProp}, {"whereOp", b.whereOp}, {"whereVal", b.whereVal}, {"setProp", b.setProp},
            {"op", b.op}, {"value", b.value}, {"inSelection", b.inSelection}};
}
Editor::BulkQuery fromJson(const nlohmann::json& j) {
    Editor::BulkQuery b;
    b.name = j.value("name", ""); b.cls = j.value("class", ""); b.nameHas = j.value("nameHas", "");
    b.tag = j.value("tag", ""); b.notTag = j.value("notTag", ""); b.whereProp = j.value("whereProp", "");
    b.whereOp = j.value("whereOp", "="); b.whereVal = j.value("whereVal", ""); b.setProp = j.value("setProp", "");
    b.op = j.value("op", "Set to"); b.value = j.value("value", ""); b.inSelection = j.value("inSelection", false);
    return b;
}
std::string savedFile() { return Paths::file("FindAndChange.json").string(); }

bool combo(const char* label, std::string& value, const char* const* items, int count, float width) {
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, value.c_str())) {
        for (int i = 0; i < count; ++i)
            if (ImGui::Selectable(items[i], value == items[i])) { value = items[i]; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

// Which objects match, and (with apply) changes them. Fills m_bulkRows.
void Editor::bulkRun(bool apply) {
    BulkQuery& b = m_bulk;
    std::vector<uint64_t> ids;
    const std::string nameHas = lower(trim(b.nameHas)), tag = trim(b.tag), notTag = trim(b.notTag);
    auto inSelection = [&](SceneNode* n) {
        for (SceneNode* p = n; p; p = p->parent)
            if (std::find(m_scene->selection().begin(), m_scene->selection().end(), p) != m_scene->selection().end()) return true;
        return false;
    };
    auto hasTag = [](SceneNode* n, const std::string& t) { return std::find(n->tags.begin(), n->tags.end(), t) != n->tags.end(); };
    m_scene->forEach([&](SceneNode* n) {
        if (n == m_scene->root() || n->internal || m_scene->isCharacterPart(n)) return;
        if (!nameHas.empty() && lower(n->name).find(nameHas) == std::string::npos) return;
        if (!tag.empty() && !hasTag(n, tag)) return;
        if (!notTag.empty() && hasTag(n, notTag)) return;
        if (b.inSelection && !inSelection(n)) return;
        ids.push_back(n->id);
    });
    m_bulkRows.clear();
    m_bulkStatus.clear();
    m_bulkChanged = -1;
    m_bulkBad = 0;
    const bool tagOp = b.op == "Add tag" || b.op == "Remove tag";
    if (apply && !tagOp && trim(b.setProp).empty()) { m_bulkStatus = "Type which property to change first."; return; }
    if (apply && tagOp && trim(b.value).empty()) { m_bulkStatus = "Type the tag to add or remove first."; return; }
    if (ids.empty()) { m_bulkStatus = "Nothing matches."; return; }

    std::ostringstream code;
    code << "__bulk = {ids = {";
    for (uint64_t id : ids) code << id << ",";
    code << "}, class = " << q(trim(b.cls)) << ", whereProp = " << q(trim(b.whereProp)) << ", whereOp = " << q(b.whereOp)
         << ", whereVal = " << q(b.whereVal) << ", setProp = " << q(trim(b.setProp)) << ", op = " << q(b.op)
         << ", value = " << q(b.value) << ", apply = " << (apply ? "true" : "false") << "}\n" << kBulkLua;

    std::vector<Log::Entry> got;
    Log::setCapture(&got);
    runCommand(code.str());
    Log::setCapture(nullptr);
    std::string luaError;
    for (const Log::Entry& e : got) {
        if (e.text.rfind("\1GBBULK", 0) == 0) {
            std::vector<std::string> f;
            std::stringstream ss(e.text.substr(7));
            for (std::string part; std::getline(ss, part, '\t');) f.push_back(part);
            while (f.size() < 4) f.emplace_back();
            m_bulkRows.push_back({std::stoull(f[0]), f[1], f[2], f[3]});
        } else if (e.level == Log::Level::Error && luaError.empty()) {
            luaError = e.text;
        }
    }
    m_bulkBad = 0;
    for (const BulkRow& r : m_bulkRows) m_bulkBad += !r.error.empty();
    m_bulkChanged = apply ? (int)m_bulkRows.size() - m_bulkBad : -1;
    if (!luaError.empty()) m_bulkStatus = luaError;
    else if (m_bulkRows.empty()) m_bulkStatus = "Nothing matches.";
    m_bulkPreviewed = !apply && (tagOp || !trim(b.setProp).empty());
    if (apply) {
        m_bulkPreviewed = false;
        Log::info("Find and Change: " + std::to_string(m_bulkChanged) + " objects changed.");
    }
}

void Editor::renderBulkEdit() {
    if (!m_showBulkEdit) return;
    if (!m_bulkLoaded) {
        m_bulkLoaded = true;
        std::string text;
        if (Serializer::readFile(savedFile(), text))
            if (auto j = nlohmann::json::parse(text, nullptr, false); j.is_array())
                for (const auto& e : j) if (e.is_object()) m_bulkSaved.push_back(fromJson(e));
    }
    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin("Find and Change", &m_showBulkEdit)) { ImGui::End(); return; }
    BulkQuery& b = m_bulk;
    bool edited = false;
    const float w = ImGui::GetFontSize() * 11;

    ImGui::SeparatorText("Find");
    ImGui::SetNextItemWidth(w);
    edited |= ImGui::InputTextWithHint("Kind", "Part, PointLight, Script... (empty = anything)", &b.cls);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("The class name scripts use. BasePart finds every kind of part.");
    ImGui::SetNextItemWidth(w);
    edited |= ImGui::InputTextWithHint("Name contains", "(empty = any name)", &b.nameHas);
    ImGui::SetNextItemWidth(w);
    edited |= ImGui::InputTextWithHint("Has tag", "(empty = any)", &b.tag);
    ImGui::SetNextItemWidth(w);
    edited |= ImGui::InputTextWithHint("Doesn't have tag", "(empty = any)", &b.notTag);
    ImGui::TextUnformatted("Where");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.8f);
    edited |= ImGui::InputTextWithHint("##whereProp", "property (empty = skip)", &b.whereProp);
    ImGui::SameLine();
    edited |= combo("##whereOp", b.whereOp, kWhereOps, IM_ARRAYSIZE(kWhereOps), ImGui::GetFontSize() * 5);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.7f);
    edited |= ImGui::InputTextWithHint("##whereVal", "value", &b.whereVal);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Like: Mass < 2, Material = Neon, Anchored = false, Name contains door");
    edited |= ImGui::Checkbox("Only inside what's selected", &b.inSelection);

    ImGui::SeparatorText("Change (optional)");
    combo("##op", b.op, kChangeOps, IM_ARRAYSIZE(kChangeOps), ImGui::GetFontSize() * 7);
    const bool tagOp = b.op == "Add tag" || b.op == "Remove tag";
    if (!tagOp) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * 0.8f);
        edited |= ImGui::InputTextWithHint("##setProp", "property", &b.setProp);
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(tagOp ? "" : b.op == "Set to" ? "=" : b.op == "Add" ? "+" : "x");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.8f);
    edited |= ImGui::InputTextWithHint("##value", tagOp ? "tag" : "value", &b.value);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Numbers: 1.5. Yes/no: true or false. Positions and sizes: 1, 2, 3. Colours: 255, 0, 0.\n"
                          "Material, Shape and other choices: their name, like Neon.");
    if (lower(trim(b.setProp)) == "mass")
        ImGui::TextDisabled("Mass comes from Size and Density: to make things heavier, multiply Density instead.");
    if (edited) { m_bulkPreviewed = false; m_bulkChanged = -1; }

    ImGui::Spacing();
    if (ImGui::Button("Find")) bulkRun(false);
    ImGui::SameLine();
    ImGui::BeginDisabled(m_playing);
    if (ImGui::Button(m_bulkPreviewed ? "Apply" : "Preview change")) bulkRun(m_bulkPreviewed);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(m_playing ? "Stop the playtest first." : m_bulkPreviewed ? "Change them all now (one Ctrl+Z undoes it)."
                                                                                   : "Show what each one would become, without changing anything.");
    ImGui::SameLine();
    ImGui::BeginDisabled(m_bulkRows.empty());
    if (ImGui::Button("Select these")) {
        m_scene->deselect();
        for (const BulkRow& r : m_bulkRows) if (SceneNode* n = m_scene->findById(r.id)) m_scene->addToSelection(n);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save...")) { m_bulkSaveName = b.name; ImGui::OpenPopup("Save search"); }
    if (!m_bulkSaved.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Saved")) ImGui::OpenPopup("Saved searches");
    }
    if (ImGui::BeginPopup("Save search")) {
        ImGui::SetNextItemWidth(w);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ImGui::InputTextWithHint("##saveName", "Name, like Heavier props", &m_bulkSaveName, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Save") || enter) && !trim(m_bulkSaveName).empty()) {
            b.name = trim(m_bulkSaveName);
            auto same = std::find_if(m_bulkSaved.begin(), m_bulkSaved.end(), [&](const BulkQuery& s) { return s.name == b.name; });
            if (same != m_bulkSaved.end()) *same = b; else m_bulkSaved.push_back(b);
            nlohmann::json all = nlohmann::json::array();
            for (const BulkQuery& s : m_bulkSaved) all.push_back(toJson(s));
            Serializer::writeFile(savedFile(), all.dump(1));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("Saved searches")) {
        int remove = -1;
        for (size_t i = 0; i < m_bulkSaved.size(); ++i) {
            ImGui::PushID((int)i);
            {
                I18n::Pause keep;   // (names people typed)
                if (ImGui::Selectable(m_bulkSaved[i].name.c_str(), false, 0, ImVec2(w, 0))) { b = m_bulkSaved[i]; m_bulkRows.clear(); m_bulkStatus.clear(); m_bulkChanged = -1; m_bulkBad = 0; m_bulkPreviewed = false; }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) remove = (int)i;
            ImGui::PopID();
        }
        if (remove >= 0) {
            m_bulkSaved.erase(m_bulkSaved.begin() + remove);
            nlohmann::json all = nlohmann::json::array();
            for (const BulkQuery& s : m_bulkSaved) all.push_back(toJson(s));
            Serializer::writeFile(savedFile(), all.dump(1));
        }
        ImGui::EndPopup();
    }

    if (!m_bulkStatus.empty()) ImGui::TextWrapped("%s", I18n::tr(m_bulkStatus.c_str()));
    else if (m_bulkChanged >= 0) ImGui::Text("Changed %d (Ctrl+Z puts them all back).", m_bulkChanged);
    else if (!m_bulkRows.empty()) ImGui::Text(m_bulkPreviewed ? "%d to change. Check them, then press Apply." : "%d found.", (int)m_bulkRows.size());
    if (m_bulkBad > 0 && m_bulkStatus.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%d can't be changed: see the red lines.", m_bulkBad);

    // The list: click a line to select (and see) that object.
    const bool changing = m_bulkPreviewed || std::any_of(m_bulkRows.begin(), m_bulkRows.end(), [](const BulkRow& r) { return !r.now.empty(); });
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (!m_bulkRows.empty() && ImGui::BeginTable("bulkrows", changing ? 3 : 2, flags, ImVec2(0, ImGui::GetContentRegionAvail().y))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Object");
        ImGui::TableSetupColumn(changing ? "Now" : "Value");
        if (changing) ImGui::TableSetupColumn("Becomes");
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;
        clip.Begin((int)m_bulkRows.size());
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const BulkRow& r = m_bulkRows[i];
                SceneNode* n = m_scene->findById(r.id);
                I18n::Pause keep;   // object names and values stay as they are
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushID(i);
                if (ImGui::Selectable(n ? n->name.c_str() : "(gone)", n && n->selected, ImGuiSelectableFlags_SpanAllColumns) && n)
                    m_scene->select(n);
                if (n && ImGui::IsItemHovered() && n->parent) ImGui::SetTooltip("in %s", n->parent->name.c_str());
                ImGui::PopID();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(r.old.c_str());
                if (changing) {
                    ImGui::TableNextColumn();
                    if (!r.error.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s", r.error.c_str());
                    else ImGui::TextUnformatted(r.now.c_str());
                }
            }
        ImGui::EndTable();
    }
    ImGui::End();
}
