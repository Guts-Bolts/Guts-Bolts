// The Material Graph: nodes make pictures that tile, wires can't loop, it saves and loads,
// and baking makes the right pictures.
#include "scene/MaterialGraph.h"
#include "scene/SceneNode.h"
#include "scene/Serializer.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace MaterialGraph;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
// The biggest jump across the picture's wrap-around edges, against the biggest jump inside.
bool tiles(const Image& img) {
    const int n = img.size;
    float edge = 0.0f, inside = 0.0f;
    for (int i = 0; i < n; ++i) {
        edge = std::max({edge, std::abs(img.value(0, i) - img.value(n - 1, i)), std::abs(img.value(i, 0) - img.value(i, n - 1))});
        for (int k = 1; k < n; ++k) inside = std::max(inside, std::abs(img.value(k, i) - img.value(k - 1, i)));
    }
    return edge <= inside + 1e-4f;
}
// How far apart the darkest and brightest spots are.
float spread(const Image& img) {
    float lo = 1.0f, hi = 0.0f;
    for (const auto& p : img.px) {
        const float v = (p.r + p.g + p.b) / 3.0f;
        lo = std::min(lo, v); hi = std::max(hi, v);
    }
    return hi - lo;
}
} // namespace

int main() {
    // Every pattern tiles, and actually has a pattern.
    for (Type t : {Type::Noise, Type::Cells, Type::Bricks, Type::Checker, Type::Stripes}) {
        Graph g;
        const int id = g.add(t, glm::vec2(0.0f)).id;
        const Image img = evaluate(g, id, 128);
        check(img.size == 128 && tiles(img) && spread(img) > 0.2f, std::string(info(t).name) + " tiles and isn't flat");
    }
    {   // Cells' other looks, and seeds.
        Graph g;
        Node& c = g.add(Type::Cells, glm::vec2(0.0f));
        c.p[2] = 1.0f;
        const int id = c.id;
        check(tiles(evaluate(g, id, 96)), "Cells (edges) tiles");
        Node& noise = g.add(Type::Noise, glm::vec2(0.0f));
        const int nid = noise.id;
        const Image a = evaluate(g, nid, 32);
        g.find(nid)->p[3] = 5.0f;
        const Image b = evaluate(g, nid, 32);
        check(a.px[100] != b.px[100], "a different Seed is a different pattern");
    }

    // Wiring: things flow through, loops aren't allowed.
    Graph g = starter();
    const Node* out = g.output();
    check(out && out->in[0] >= 0 && out->in[1] >= 0, "the starter graph is wired to the Output");
    Node& inv = g.add(Type::Invert, glm::vec2(0.0f));
    const int invId = inv.id;
    Node& lev = g.add(Type::Levels, glm::vec2(0.0f));
    const int levId = lev.id;
    check(g.connect(invId, levId, 0), "wire Invert into Levels");
    check(!g.connect(levId, invId, 0), "...but not Levels back into Invert (a loop)");
    check(!g.connect(levId, levId, 0), "...or a box into itself");
    check(!g.connect(levId, invId, 3), "...or into an input it doesn't have");
    {
        Graph m;
        Node& a = m.add(Type::Number, glm::vec2(0.0f)); a.p[0] = 0.2f; const int aid = a.id;
        Node& inv2 = m.add(Type::Invert, glm::vec2(0.0f)); const int iid = inv2.id;
        m.connect(aid, iid, 0);
        check(std::abs(evaluate(m, iid, 8).value(3, 3) - 0.8f) < 1e-3f, "Number 0.2 inverted is 0.8");
        Node& mix = m.add(Type::Mix, glm::vec2(0.0f)); const int mid = mix.id;
        m.connect(aid, mid, 0); m.connect(iid, mid, 1);
        m.find(mid)->p[0] = 0.25f;
        check(std::abs(evaluate(m, mid, 8).value(1, 1) - 0.35f) < 1e-3f, "Mix a quarter of the way: 0.35");
        Node& col = m.add(Type::Colorize, glm::vec2(0.0f)); const int cid = col.id;
        m.connect(aid, cid, 0);
        m.find(cid)->c[0] = glm::vec3(0.0f); m.find(cid)->c[1] = glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec4 px = evaluate(m, cid, 8).px[0];
        check(std::abs(px.r - 0.2f) < 1e-3f && px.g == 0.0f, "Colorize turns grey into colour");
    }

    // Saved and loaded (and kept in a MaterialVariant).
    const std::string text = toJson(g).dump();
    Graph back;
    check(fromJson(nlohmann::json::parse(text), back) && toJson(back).dump() == text, "a graph saves and loads the same");
    Graph bad;
    fromJson(nlohmann::json::parse(R"({"nodes":[{"id":1,"type":"noise","p":[999,-5]},{"id":2,"type":"invert","in":[3]},
                                                {"id":3,"type":"invert","in":[2]},{"id":4,"type":"lava"}]})"), bad);
    check(bad.output() && bad.find(1)->p[0] == 32.0f && bad.find(1)->p[1] == 1.0f && bad.nodes.size() == 4 &&
          !(bad.find(2)->in[0] == 3 && bad.find(3)->in[0] == 2), "a broken graph is fixed up (Output added, numbers kept in range, no loops)");
    MaterialVariantProps v;
    v.graph = text;
    MaterialVariantProps v2;
    Serializer::variantFromJson(Serializer::variantToJson(v), v2);
    check(v2.graph == text, "the graph is saved with its MaterialVariant");

    // Baking.
    Baked b = bake(g, 64);
    check(b.hasColor && b.hasNormal && !b.hasRoughness && !b.hasMetalness, "starter bakes colour and bumps only");
    check(b.color.size == 64 && b.normal.size == 64, "...at the asked size");
    float zmin = 1.0f;
    for (const auto& p : b.normal.px) zmin = std::min(zmin, p.b);
    check(zmin > 0.5f, "the bumps face outwards");
    Graph flat;
    flat.add(Type::Output, glm::vec2(0.0f));
    Baked none = bake(flat, 32);
    check(!none.hasColor && !none.hasNormal, "an empty Output bakes nothing (the material's own settings stay)");

    // The ready-made ones all make something.
    for (const std::string& name : examples()) {
        const Graph e = example(name);
        const Baked eb = bake(e, 64);
        check(eb.hasColor && spread(eb.color) > 0.1f && tiles(eb.color), name + " makes a tiling picture");
    }

    std::cout << (failures ? "material graph tests FAILED\n" : "material graph tests passed\n");
    return failures ? 1 : 0;
}
