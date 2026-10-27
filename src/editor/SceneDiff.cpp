#include "SceneDiff.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"
#include "../scene/Serializer.h"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

using nlohmann::json;

namespace SceneDiff {

namespace {

// One object of a version: itself without children, where it is, and how many are inside it.
struct Info {
    json        shallow;
    const json* full = nullptr;
    uint64_t    parent = 0;
    std::string path;
    int         inside = 0;
};
struct Index {
    uint64_t                 root = 0;
    std::map<uint64_t, Info> nodes;
    json                     settings;   // everything but the object tree
    bool                     ok = false;
};

int collect(const json& node, uint64_t parent, const std::string& parentPath, Index& out) {
    if (!node.is_object() || !node.contains("id")) return 0;
    const uint64_t id = node["id"].get<uint64_t>();
    Info info;
    info.full = &node;
    info.parent = parent;
    info.shallow = node;
    info.shallow.erase("children");
    info.path = parentPath + "." + node.value("name", std::string("?"));
    int inside = 0;
    if (node.contains("children") && node["children"].is_array())
        for (const auto& c : node["children"]) inside += collect(c, id, info.path, out);
    info.inside = inside;
    out.nodes[id] = std::move(info);
    return inside + 1;
}

Index indexOf(const json& scene) {
    Index ix;
    if (!scene.is_object() || !scene.contains("workspace")) return ix;
    const json& ws = scene["workspace"];
    ix.root = ws.value("id", (uint64_t)0);
    if (ws.contains("children") && ws["children"].is_array())
        for (const auto& c : ws["children"]) collect(c, ix.root, "Workspace", ix);
    ix.settings = scene;
    ix.settings.erase("workspace");
    ix.ok = true;
    return ix;
}

std::string shortText(const json& v) {
    if (v.is_null()) return "(none)";
    if (v.is_string()) {
        const std::string& s = v.get_ref<const std::string&>();
        const size_t lines = std::count(s.begin(), s.end(), '\n');
        if (lines > 0) return std::to_string(lines + 1) + " lines";
        return s.size() > 60 ? "\"" + s.substr(0, 57) + "...\"" : "\"" + s + "\"";
    }
    std::string t = v.dump();
    if (v.is_array() && v.size() <= 4) {   // [1.0,2.5,0.0] -> 1, 2.5, 0
        t.clear();
        for (size_t i = 0; i < v.size(); ++i) {
            char b[32];
            if (v[i].is_number()) std::snprintf(b, sizeof b, "%g", v[i].get<double>());
            else std::snprintf(b, sizeof b, "%s", v[i].dump().c_str());
            t += (i ? ", " : "") + std::string(b);
        }
    }
    return t.size() > 60 ? t.substr(0, 57) + "..." : t;
}

std::string fieldLabel(const std::string& k) {
    static const std::map<std::string, std::string> names = {
        {"pos", "Position"}, {"rot", "Rotation"}, {"size", "Size"}, {"color", "Color"}, {"name", "Name"},
        {"parent", "Parent"}, {"source", "Code"}, {"anchored", "Anchored"}, {"canCollide", "CanCollide"},
        {"transparency", "Transparency"}, {"material", "Material"}, {"attributes", "Attributes"}, {"tags", "Tags"}};
    auto it = names.find(k);
    return it == names.end() ? k : it->second;
}

std::string pathOf(const Index& ix, uint64_t id) {
    if (id == ix.root) return "Workspace";
    auto it = ix.nodes.find(id);
    return it == ix.nodes.end() ? "(gone)" : it->second.path;
}

// What's different about one object between two versions (empty = the same).
std::vector<Field> fieldsOf(const Index& a, const Info& x, const Index& b, const Info& y) {
    std::vector<Field> out;
    std::set<std::string> keys;
    for (auto it = x.shallow.begin(); it != x.shallow.end(); ++it) keys.insert(it.key());
    for (auto it = y.shallow.begin(); it != y.shallow.end(); ++it) keys.insert(it.key());
    for (const std::string& k : keys) {
        const json va = x.shallow.contains(k) ? x.shallow[k] : json();
        const json vb = y.shallow.contains(k) ? y.shallow[k] : json();
        if (va != vb) out.push_back({fieldLabel(k), shortText(va), shortText(vb)});
    }
    if (x.parent != y.parent) out.push_back({"Parent", pathOf(a, x.parent), pathOf(b, y.parent)});
    return out;
}

bool sameObject(const Info& x, const Info& y) { return x.parent == y.parent && x.shallow == y.shallow; }

Change make(Change::Kind kind, uint64_t id, const Info& in, bool full) {
    Change c;
    c.kind = kind;
    c.id = id;
    c.name = in.shallow.value("name", std::string("?"));
    c.className = in.shallow.value("kind", std::string("Part"));
    c.path = in.path;
    c.parent = in.parent;
    c.theirs = full ? *in.full : in.shallow;
    return c;
}

// Everything that turns version `from` into version `to`.
std::vector<Change> diff(const Index& from, const Index& to) {
    std::vector<Change> out;
    if (from.settings != to.settings) {
        Change c;
        c.id = 0;
        c.name = "Game settings";
        c.className = "Settings";
        c.path = "(lighting, game info, rules...)";
        for (auto it = to.settings.begin(); it != to.settings.end(); ++it)
            if (!from.settings.contains(it.key()) || from.settings[it.key()] != it.value())
                c.fields.push_back({fieldLabel(it.key()), shortText(from.settings.contains(it.key()) ? from.settings[it.key()] : json()),
                                    shortText(it.value())});
        out.push_back(std::move(c));
    }
    for (const auto& [id, y] : to.nodes) {
        auto it = from.nodes.find(id);
        if (it == from.nodes.end()) {
            if (to.nodes.count(y.parent) && !from.nodes.count(y.parent)) continue;   // (inside something added)
            Change c = make(Change::Added, id, y, true);
            c.inside = y.inside;
            out.push_back(std::move(c));
            continue;
        }
        std::vector<Field> f = fieldsOf(from, it->second, to, y);
        if (f.empty()) continue;
        const bool moved = std::all_of(f.begin(), f.end(), [](const Field& x) {
            return x.name == "Position" || x.name == "Rotation" || x.name == "Parent";
        });
        Change c = make(moved ? Change::Moved : Change::Changed, id, y, false);
        c.fields = std::move(f);
        out.push_back(std::move(c));
    }
    for (const auto& [id, x] : from.nodes) {
        if (to.nodes.count(id)) continue;
        if (from.nodes.count(x.parent) && !to.nodes.count(x.parent)) continue;   // (inside something removed)
        Change c = make(Change::Removed, id, x, false);
        c.inside = x.inside;
        out.push_back(std::move(c));
    }
    return out;
}

// Inside an object being brought in: leave out what the scene already has (it's moved
// there by its own change).
void dropExisting(json& node, Scene& scene) {
    if (!node.contains("children") || !node["children"].is_array()) return;
    json kept = json::array();
    for (auto& c : node["children"]) {
        if (c.contains("id") && scene.findById(c["id"].get<uint64_t>())) continue;
        dropExisting(c, scene);
        kept.push_back(std::move(c));
    }
    node["children"] = std::move(kept);
}

} // namespace

const char* kindName(Change::Kind k) {
    switch (k) {
        case Change::Added:   return "Added";
        case Change::Removed: return "Removed";
        case Change::Moved:   return "Moved";
        default:              return "Changed";
    }
}

int Comparison::count(Change::Kind k) const {
    int n = 0;
    for (const auto& c : changes) if (c.kind == k) ++n;
    return n;
}

Comparison compare(const std::string& mine, const std::string& other, const std::string* base) {
    Comparison out;
    const json jm = json::parse(mine, nullptr, false), jo = json::parse(other, nullptr, false);
    const Index im = indexOf(jm), io = indexOf(jo);
    if (!im.ok || !io.ok) { out.error = "That isn't a Guts and Bolts game."; return out; }
    out.otherText = other;
    if (!base) {
        out.changes = diff(im, io);
        return out;
    }
    const json jb = json::parse(*base, nullptr, false);
    const Index ib = indexOf(jb);
    if (!ib.ok) { out.error = "The starting version isn't a Guts and Bolts game."; return out; }
    for (Change& c : diff(ib, io)) {
        if (c.id == 0) {
            if (im.settings != ib.settings) {
                if (im.settings == io.settings) continue;
                c.conflict = true;
                c.conflictWhy = "You changed the game settings too.";
            }
            out.changes.push_back(std::move(c));
            continue;
        }
        auto mineIt = im.nodes.find(c.id);
        auto baseIt = ib.nodes.find(c.id);
        if (c.kind == Change::Added) {
            out.changes.push_back(std::move(c));   // (an id you also used: it's brought in as a copy)
            continue;
        }
        if (c.kind == Change::Removed) {
            if (mineIt == im.nodes.end()) continue;   // you deleted it too
            if (baseIt != ib.nodes.end() && !sameObject(mineIt->second, baseIt->second)) {
                c.conflict = true;
                c.conflictWhy = "You changed it, but the other version deleted it.";
            }
            out.changes.push_back(std::move(c));
            continue;
        }
        if (mineIt == im.nodes.end()) {
            c.conflict = true;
            c.conflictWhy = "You deleted it, but the other version changed it.";
        } else if (baseIt != ib.nodes.end() && !sameObject(mineIt->second, baseIt->second)) {
            if (sameObject(mineIt->second, io.nodes.at(c.id))) continue;   // you both made the same change
            c.conflict = true;
            c.conflictWhy = "You changed it too.";
        }
        out.changes.push_back(std::move(c));
    }
    return out;
}

int apply(Scene& scene, const Comparison& c, const std::vector<int>& chosen, std::vector<std::string>& problems) {
    std::vector<const Change*> picked;
    for (int i : chosen)
        if (i >= 0 && i < (int)c.changes.size()) picked.push_back(&c.changes[i]);
    // Settings and new objects first (something may move into them), then changes, then deletes.
    auto order = [](const Change* x) { return x->id == 0 ? 0 : x->kind == Change::Added ? 1 : x->kind == Change::Removed ? 3 : 2; };
    std::stable_sort(picked.begin(), picked.end(), [&](const Change* a, const Change* b) { return order(a) < order(b); });
    int done = 0;
    for (const Change* ch : picked) {
        if (ch->id == 0) {
            Serializer::settingsFromString(scene, c.otherText);
            ++done;
            continue;
        }
        SceneNode* node = scene.findById(ch->id);
        switch (ch->kind) {
            case Change::Added: {
                json sub = ch->theirs;
                dropExisting(sub, scene);
                auto made = Serializer::nodeFromString(sub.dump(), node != nullptr);   // (id taken here: a copy)
                if (!made) { problems.push_back(ch->name + " couldn't be read."); break; }
                SceneNode* parent = ch->parent ? scene.findById(ch->parent) : nullptr;
                if (ch->parent && !parent) problems.push_back(ch->name + " was put in Workspace: what it was in isn't here.");
                scene.insert(std::move(made), parent);
                ++done;
                break;
            }
            case Change::Removed:
                if (!node) break;   // (already gone)
                scene.removeNode(node);
                ++done;
                break;
            default: {
                if (!node) { problems.push_back(ch->name + " isn't in this version any more."); break; }
                Serializer::applyNodeShallow(*node, ch->theirs.dump());
                const bool reparented = std::any_of(ch->fields.begin(), ch->fields.end(), [](const Field& f) { return f.name == "Parent"; });
                if (reparented) {
                    SceneNode* parent = scene.findById(ch->parent);
                    if (!parent && ch->parent) problems.push_back(ch->name + " stayed where it is: what it moved into isn't here.");
                    else scene.reparent(node, parent ? parent : scene.root());
                }
                ++done;
                break;
            }
        }
    }
    scene.markDirty();
    return done;
}

std::string loadText(const std::string& path, std::string* error) {
    Scene tmp;
    if (!Serializer::loadGameFile(tmp, path, error)) return std::string();
    return Serializer::saveScene(tmp);
}

} // namespace SceneDiff
