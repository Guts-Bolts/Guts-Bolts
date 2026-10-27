#include "LinkedModels.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Serializer.h"

#include <map>
#include <random>
#include <vector>

namespace LinkedModels {

std::string linkOf(const SceneNode* n) {
    if (!n || n->kind != NodeKind::Model) return {};
    const Attribute* a = n->findAttribute(kAttribute);
    return a && a->type == Attribute::String ? a->s : std::string();
}

std::string newLink() {
    static const char* hex = "0123456789abcdef";
    std::random_device rd;
    std::string id;
    for (int i = 0; i < 16; ++i) id += hex[rd() % 16];
    return id;
}

void link(SceneNode* model, const std::string& id) {
    if (!model || model->kind != NodeKind::Model) return;
    unlink(model);
    Attribute a;
    a.name = kAttribute;
    a.type = Attribute::String;
    a.s = id;
    model->attributes.push_back(a);
}

void unlink(SceneNode* model) {
    if (!model) return;
    auto& as = model->attributes;
    for (auto it = as.begin(); it != as.end();)
        it = it->name == kAttribute ? as.erase(it) : std::next(it);
}

namespace {
// Everything inside a model (not the model itself: where it is, its name).
std::string insides(const SceneNode* model) {
    std::string out;
    for (const auto& c : model->children) { out += Serializer::nodeToString(*c); out += '\n'; }
    return out;
}

// Every linked model in the scene, by link (outermost first, as found).
std::map<std::string, std::vector<SceneNode*>> groups(Scene& scene) {
    std::map<std::string, std::vector<SceneNode*>> out;
    std::vector<SceneNode*> stack{scene.root()};
    while (!stack.empty()) {
        SceneNode* n = stack.back();
        stack.pop_back();
        const std::string l = linkOf(n);
        if (!l.empty()) out[l].push_back(n);
        for (auto it = n->children.rbegin(); it != n->children.rend(); ++it) stack.push_back(it->get());
    }
    return out;
}
} // namespace

void Tracker::remember(Scene& scene) {
    m_seen.clear();
    for (auto& [l, models] : groups(scene))
        for (SceneNode* m : models) m_seen[m->id] = insides(m);
}

int Tracker::sync(Scene& scene, std::string* name) {
    int updated = 0;
    std::vector<std::string> links;
    for (auto& [l, models] : groups(scene)) if (models.size() > 1) links.push_back(l);
    for (const std::string& l : links) {
        // (Looked up again each time: copying one set may have replaced models of another.)
        const std::vector<SceneNode*> models = groups(scene)[l];
        if (models.size() < 2) continue;
        SceneNode* changed = nullptr;
        int count = 0;
        for (SceneNode* m : models) {
            auto it = m_seen.find(m->id);
            if (it != m_seen.end() && it->second != insides(m)) { changed = m; ++count; }
        }
        if (count != 1) continue;   // nothing changed, or several did (all edited together): leave them
        for (SceneNode* m : models) {
            if (m == changed || m->isAncestorOf(changed) || changed->isAncestorOf(m)) continue;
            while (!m->children.empty()) scene.removeNode(m->children.back().get());
            for (const auto& c : changed->children) scene.insert(Serializer::clone(*c), m);
            ++updated;
        }
        if (name && updated) *name = changed->name;
    }
    remember(scene);
    return updated;
}

} // namespace LinkedModels
