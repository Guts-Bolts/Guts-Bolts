#include "MaterialGraph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>

using nlohmann::json;

namespace MaterialGraph {

namespace {

const TypeInfo kTypes[(int)Type::Count] = {
    {"Output", "What becomes the material: its colours, its bumps (from a height picture: white is high), "
               "where it's rough and where it's metal. Leave one empty to use the material's own setting.",
     4, {"Color", "Height", "Roughness", "Metalness"},
     1, {{"Bumpiness", 1.0f, 0.0f, 5.0f, false, "How deep the bumps from Height look"}}, 0, {}},
    {"Color", "One colour all over.", 0, {}, 0, {}, 1, {"Color"}},
    {"Number", "One grey all over (0 black .. 1 white).", 0, {},
     1, {{"Value", 0.5f, 0.0f, 1.0f, false, ""}}, 0, {}},
    {"Noise", "Soft cloudy noise, like dirt, stone or clouds.", 0, {},
     4, {{"Scale", 4.0f, 1.0f, 32.0f, true, "How many blobs across"},
         {"Detail", 5.0f, 1.0f, 8.0f, true, "Layers of finer and finer noise on top"},
         {"Roughness", 0.5f, 0.0f, 1.0f, false, "How strong the fine layers are"},
         {"Seed", 0.0f, 0.0f, 100.0f, true, "Change it for a different pattern"}}, 0, {}},
    {"Cells", "Cells like stones, scales or cracked mud.", 0, {},
     4, {{"Scale", 6.0f, 1.0f, 32.0f, true, "How many cells across"},
         {"Randomness", 1.0f, 0.0f, 1.0f, false, "0: a neat grid, 1: all over the place"},
         {"Look", 0.0f, 0.0f, 2.0f, true, "0: round (dark middles), 1: just the edges, 2: each cell its own grey"},
         {"Seed", 0.0f, 0.0f, 100.0f, true, "Change it for a different pattern"}}, 0, {}},
    {"Bricks", "Rows of bricks or tiles: white bricks, black gaps.", 0, {},
     5, {{"Rows", 8.0f, 1.0f, 32.0f, true, "Rows of bricks"},
         {"Columns", 4.0f, 1.0f, 16.0f, true, "Bricks in a row"},
         {"Gap", 0.1f, 0.0f, 0.4f, false, "How wide the gaps between them are (part of a brick's height)"},
         {"Shift", 0.5f, 0.0f, 1.0f, false, "How far every other row slides along (0.5 = like a wall)"},
         {"Variation", 0.3f, 0.0f, 1.0f, false, "How much each brick's grey differs"}}, 0, {}},
    {"Checker", "A chessboard.", 0, {},
     1, {{"Count", 4.0f, 1.0f, 32.0f, true, "Squares across"}}, 0, {}},
    {"Stripes", "Stripes or waves.", 0, {},
     3, {{"Count", 6.0f, 1.0f, 32.0f, true, "How many stripes"},
         {"Direction", 0.0f, 0.0f, 2.0f, true, "0: up and down, 1: across, 2: slanted"},
         {"Sharpness", 0.0f, 0.0f, 1.0f, false, "0: soft waves .. 1: hard stripes"}}, 0, {}},
    {"Gradient", "From black to white across the picture (this one doesn't tile).", 0, {},
     1, {{"Direction", 0.0f, 0.0f, 2.0f, true, "0: left to right, 1: top to bottom, 2: round from the middle"}}, 0, {}},
    {"Mix", "Blends A into B: where Amount is white you see B.", 3, {"A", "B", "Amount"},
     1, {{"Amount", 0.5f, 0.0f, 1.0f, false, "Used when nothing is wired into Amount"}}, 0, {}},
    {"Multiply", "A times B: darkens A where B is dark.", 2, {"A", "B"}, 0, {}, 0, {}},
    {"Add", "A plus B: lightens A where B is light.", 2, {"A", "B"},
     1, {{"Strength", 1.0f, 0.0f, 1.0f, false, "How much of B is added"}}, 0, {}},
    {"Levels", "Makes it darker, lighter or more contrasty.", 1, {"In"},
     3, {{"Low", 0.0f, 0.0f, 1.0f, false, "This grey and darker become black"},
         {"High", 1.0f, 0.0f, 1.0f, false, "This grey and lighter become white"},
         {"Gamma", 1.0f, 0.1f, 5.0f, false, "Above 1: lighter in the middle, below 1: darker"}}, 0, {}},
    {"Invert", "Black becomes white and white black.", 1, {"In"}, 0, {}, 0, {}},
    {"Colorize", "Turns greys into colours: black becomes Dark, white becomes Light.", 1, {"In"},
     0, {}, 2, {"Dark", "Light"}},
    {"Warp", "Pushes In around by the greys of By (wobbly marble, wood grain).", 2, {"In", "By"},
     1, {{"Amount", 0.1f, 0.0f, 0.5f, false, "How far it's pushed"}}, 0, {}},
    {"Blur", "Softens it.", 1, {"In"},
     1, {{"Radius", 0.01f, 0.0f, 0.1f, false, "How soft (part of the picture's width)"}}, 0, {}},
};

const char* kTypeIds[(int)Type::Count] = {"output", "color", "number", "noise", "cells", "bricks", "checker",
                                          "stripes", "gradient", "mix", "multiply", "add", "levels", "invert",
                                          "colorize", "warp", "blur"};

// --- Tiling random numbers and noise ---

uint32_t hash3(int x, int y, int seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
float rand01(int x, int y, int seed) { return (hash3(x, y, seed) & 0xffffff) / 16777215.0f; }
int wrap(int v, int n) { return ((v % n) + n) % n; }

// Value noise on a grid `period` cells across that repeats (so the picture tiles).
float valueNoise(float u, float v, int period, int seed) {
    const float x = u * period, y = v * period;
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const float fx = x - ix, fy = y - iy;
    auto s = [](float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); };
    auto at = [&](int a, int b) { return rand01(wrap(a, period), wrap(b, period), seed); };
    const float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    const float sx = s(fx), sy = s(fy);
    return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}

float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
glm::vec4 grey(float v) { return glm::vec4(v, v, v, 1.0f); }

// Read a picture anywhere (wrapping round, smoothly between pixels).
glm::vec4 sample(const Image& img, float u, float v) {
    const int n = img.size;
    const float x = u * n - 0.5f, y = v * n - 0.5f;
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const float fx = x - ix, fy = y - iy;
    auto at = [&](int a, int b) { return img.px[(size_t)wrap(b, n) * n + wrap(a, n)]; };
    return glm::mix(glm::mix(at(ix, iy), at(ix + 1, iy), fx), glm::mix(at(ix, iy + 1), at(ix + 1, iy + 1), fx), fy);
}

struct Evaluator {
    const Graph& g;
    int size;
    std::map<int, Image> done;
    int depth = 0;

    Image flat(glm::vec4 c) {
        Image img;
        img.size = size;
        img.px.assign((size_t)size * size, c);
        return img;
    }
    // What's wired into input i of n (or `fallback` all over).
    const Image* input(const Node& n, int i) {
        if (n.in[i] < 0) return nullptr;
        return node(n.in[i]);
    }
    Image inputOr(const Node& n, int i, glm::vec4 fallback) {
        const Image* img = input(n, i);
        return img ? *img : flat(fallback);
    }
    template <class F> Image each(F f) {
        Image img;
        img.size = size;
        img.px.resize((size_t)size * size);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                img.px[(size_t)y * size + x] = f(x, y, (x + 0.5f) / size, (y + 0.5f) / size);
        return img;
    }

    const Image* node(int id) {
        auto it = done.find(id);
        if (it != done.end()) return &it->second;
        const Node* n = g.find(id);
        if (!n || depth > 64) return nullptr;
        ++depth;
        Image img = make(*n);
        --depth;
        return &(done[id] = std::move(img));
    }

    Image make(const Node& n) {
        const float* p = n.p;
        switch (n.type) {
        case Type::Output: return inputOr(n, 0, glm::vec4(1.0f));
        case Type::Color: return flat(glm::vec4(n.c[0], 1.0f));
        case Type::Number: return flat(grey(p[0]));
        case Type::Noise: {
            const int scale = std::max(1, (int)p[0]), octaves = std::clamp((int)p[1], 1, 8), seed = (int)p[3];
            return each([&](int, int, float u, float v) {
                float sum = 0.0f, amp = 1.0f, total = 0.0f;
                for (int o = 0; o < octaves; ++o) {
                    sum += valueNoise(u, v, scale << o, seed + o * 17) * amp;
                    total += amp;
                    amp *= p[2];
                }
                return grey(total > 0.0f ? sum / total : 0.0f);
            });
        }
        case Type::Cells: {
            const int scale = std::max(1, (int)p[0]), look = (int)p[2], seed = (int)p[3];
            return each([&](int, int, float u, float v) {
                const float x = u * scale, y = v * scale;
                const int ix = (int)std::floor(x), iy = (int)std::floor(y);
                float f1 = 9.0f, f2 = 9.0f, id = 0.0f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int cx = ix + dx, cy = iy + dy, wx = wrap(cx, scale), wy = wrap(cy, scale);
                        const float px = cx + 0.5f + (rand01(wx, wy, seed) - 0.5f) * p[1];
                        const float py = cy + 0.5f + (rand01(wx, wy, seed + 7) - 0.5f) * p[1];
                        const float d = std::hypot(px - x, py - y);
                        if (d < f1) { f2 = f1; f1 = d; id = rand01(wx, wy, seed + 13); }
                        else if (d < f2) f2 = d;
                    }
                if (look == 1) return grey(clamp01((f2 - f1) * 2.0f));
                if (look == 2) return grey(id);
                return grey(clamp01(f1));
            });
        }
        case Type::Bricks: {
            const int rows = std::max(1, (int)p[0]), cols = std::max(1, (int)p[1]);
            return each([&](int, int, float u, float v) {
                const float y = v * rows;
                const int row = (int)std::floor(y);
                const float x = u * cols + ((row & 1) ? p[3] : 0.0f);
                const int col = (int)std::floor(x);
                // How far from the brick's edge, in brick heights (so the gap is even all round).
                const float ex = std::min(x - col, col + 1 - x) * rows / cols, ey = std::min(y - row, row + 1 - y);
                const float edge = std::min(ex, ey);
                const float half = p[2] * 0.5f;
                const float inside = half <= 0.0f ? 1.0f : clamp01((edge - half) / std::max(half, 1e-4f) * 2.0f);
                const float shade = 1.0f - p[4] * rand01(wrap(col, cols), row, 3);
                return grey(inside * shade);
            });
        }
        case Type::Checker: {
            const int count = std::max(1, (int)p[0]);
            return each([&](int, int, float u, float v) {
                return grey((((int)std::floor(u * count) + (int)std::floor(v * count)) & 1) ? 1.0f : 0.0f);
            });
        }
        case Type::Stripes: {
            const int count = std::max(1, (int)p[0]), dir = (int)p[1];
            return each([&](int, int, float u, float v) {
                const float t = dir == 0 ? u : dir == 1 ? v : u + v;
                float w = 0.5f + 0.5f * std::sin(t * count * 6.2831853f);
                if (p[2] > 0.0f) {
                    const float k = 1.0f + p[2] * 40.0f;
                    w = clamp01((w - 0.5f) * k + 0.5f);
                }
                return grey(w);
            });
        }
        case Type::Gradient: {
            const int dir = (int)p[0];
            return each([&](int, int, float u, float v) {
                if (dir == 1) return grey(v);
                if (dir == 2) return grey(clamp01(1.0f - std::hypot(u - 0.5f, v - 0.5f) * 2.0f));
                return grey(u);
            });
        }
        case Type::Mix: {
            const Image a = inputOr(n, 0, glm::vec4(0, 0, 0, 1)), b = inputOr(n, 1, glm::vec4(1.0f));
            const Image* t = input(n, 2);
            return each([&](int x, int y, float, float) {
                const size_t i = (size_t)y * size + x;
                return glm::mix(a.px[i], b.px[i], t ? clamp01(t->px[i].r) : p[0]);
            });
        }
        case Type::Multiply: {
            const Image a = inputOr(n, 0, glm::vec4(1.0f)), b = inputOr(n, 1, glm::vec4(1.0f));
            return each([&](int x, int y, float, float) { const size_t i = (size_t)y * size + x; return a.px[i] * b.px[i]; });
        }
        case Type::Add: {
            const Image a = inputOr(n, 0, glm::vec4(0, 0, 0, 1)), b = inputOr(n, 1, glm::vec4(0, 0, 0, 1));
            return each([&](int x, int y, float, float) {
                const size_t i = (size_t)y * size + x;
                glm::vec4 c = a.px[i] + b.px[i] * p[0];
                return glm::vec4(clamp01(c.r), clamp01(c.g), clamp01(c.b), 1.0f);
            });
        }
        case Type::Levels: {
            const Image a = inputOr(n, 0, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            const float lo = p[0], hi = std::max(p[1], lo + 1e-3f), gamma = std::max(p[2], 0.01f);
            return each([&](int x, int y, float, float) {
                glm::vec4 c = a.px[(size_t)y * size + x];
                for (int k = 0; k < 3; ++k) c[k] = std::pow(clamp01((c[k] - lo) / (hi - lo)), 1.0f / gamma);
                return c;
            });
        }
        case Type::Invert: {
            const Image a = inputOr(n, 0, glm::vec4(0, 0, 0, 1));
            return each([&](int x, int y, float, float) {
                const glm::vec4 c = a.px[(size_t)y * size + x];
                return glm::vec4(1.0f - c.r, 1.0f - c.g, 1.0f - c.b, 1.0f);
            });
        }
        case Type::Colorize: {
            const Image a = inputOr(n, 0, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            return each([&](int x, int y, float, float) {
                return glm::vec4(glm::mix(n.c[0], n.c[1], clamp01(a.px[(size_t)y * size + x].r)), 1.0f);
            });
        }
        case Type::Warp: {
            const Image* in = input(n, 0);
            if (!in) return flat(glm::vec4(0, 0, 0, 1));
            const Image a = *in;
            const Image* by = input(n, 1);
            if (!by) return a;
            const Image b = *by;
            return each([&](int x, int y, float u, float v) {
                // Pushed along the slope of By (like water flowing downhill), by its grey.
                const float h = b.px[(size_t)y * size + x].r;
                const float gx = b.px[(size_t)y * size + wrap(x + 1, size)].r - b.px[(size_t)y * size + wrap(x - 1, size)].r;
                const float gy = b.px[(size_t)wrap(y + 1, size) * size + x].r - b.px[(size_t)wrap(y - 1, size) * size + x].r;
                const float push = p[0];
                return sample(a, u + (h - 0.5f) * push + gx * push * 4.0f, v + (h - 0.5f) * push + gy * push * 4.0f);
            });
        }
        case Type::Blur: {
            const Image a = inputOr(n, 0, glm::vec4(0, 0, 0, 1));
            const int r = std::clamp((int)std::lround(p[0] * size), 0, size / 4);
            if (r == 0) return a;
            // A box blur, across then down (twice, which looks close to a smooth one).
            Image cur = a, tmp = a;
            for (int pass = 0; pass < 2; ++pass) {
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        glm::vec4 s(0.0f);
                        for (int k = -r; k <= r; ++k) s += cur.px[(size_t)y * size + wrap(x + k, size)];
                        tmp.px[(size_t)y * size + x] = s / (float)(2 * r + 1);
                    }
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        glm::vec4 s(0.0f);
                        for (int k = -r; k <= r; ++k) s += tmp.px[(size_t)wrap(y + k, size) * size + x];
                        cur.px[(size_t)y * size + x] = s / (float)(2 * r + 1);
                    }
            }
            return cur;
        }
        case Type::Count: break;
        }
        return flat(glm::vec4(0, 0, 0, 1));
    }
};

} // namespace

const TypeInfo& info(Type t) { return kTypes[std::clamp((int)t, 0, (int)Type::Count - 1)]; }

Node* Graph::find(int id) {
    for (Node& n : nodes) if (n.id == id) return &n;
    return nullptr;
}
const Node* Graph::find(int id) const {
    for (const Node& n : nodes) if (n.id == id) return &n;
    return nullptr;
}

Node& Graph::add(Type t, glm::vec2 pos) {
    Node n;
    n.id = nextId++;
    n.type = t;
    n.pos = pos;
    const TypeInfo& ti = info(t);
    for (int i = 0; i < ti.params; ++i) n.p[i] = ti.param[i].def;
    if (t == Type::Color) n.c[0] = glm::vec3(0.8f, 0.5f, 0.3f);
    if (t == Type::Colorize) { n.c[0] = glm::vec3(0.15f, 0.1f, 0.08f); n.c[1] = glm::vec3(0.75f, 0.6f, 0.45f); }
    nodes.push_back(n);
    return nodes.back();
}

void Graph::remove(int id) {
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.id == id && n.type != Type::Output; }),
                nodes.end());
    for (Node& n : nodes)
        for (int& i : n.in) if (i == id && !find(id)) i = -1;
}

bool Graph::connect(int from, int to, int input) {
    Node* t = find(to);
    const Node* f = find(from);
    if (!t || !f || input < 0 || input >= info(t->type).inputs || f->type == Type::Output) return false;
    // Would `to` end up feeding itself? (Walk back from `from`.)
    std::vector<int> stack{from};
    int guard = 0;
    while (!stack.empty() && ++guard < 10000) {
        const int id = stack.back();
        stack.pop_back();
        if (id == to) return false;
        if (const Node* n = find(id)) for (int i : n->in) if (i >= 0) stack.push_back(i);
    }
    t->in[input] = from;
    return true;
}

const Node* Graph::output() const {
    for (const Node& n : nodes) if (n.type == Type::Output) return &n;
    return nullptr;
}

json toJson(const Graph& g) {
    json nodes = json::array();
    for (const Node& n : g.nodes) {
        const TypeInfo& ti = info(n.type);
        json j = {{"id", n.id}, {"type", kTypeIds[(int)n.type]}, {"x", n.pos.x}, {"y", n.pos.y}};
        if (ti.params) { json p = json::array(); for (int i = 0; i < ti.params; ++i) p.push_back(n.p[i]); j["p"] = p; }
        if (ti.colors) { json c = json::array(); for (int i = 0; i < ti.colors; ++i) c.push_back({n.c[i].r, n.c[i].g, n.c[i].b}); j["c"] = c; }
        if (ti.inputs) { json in = json::array(); for (int i = 0; i < ti.inputs; ++i) in.push_back(n.in[i]); j["in"] = in; }
        nodes.push_back(j);
    }
    return {{"nodes", nodes}};
}

bool fromJson(const json& j, Graph& g) {
    g = Graph{};
    if (!j.is_object() || !j.contains("nodes") || !j["nodes"].is_array()) return false;
    for (const json& e : j["nodes"]) {
        if (!e.is_object() || g.nodes.size() >= 200) continue;
        const std::string type = e.value("type", std::string());
        int t = -1;
        for (int i = 0; i < (int)Type::Count; ++i) if (type == kTypeIds[i]) t = i;
        if (t < 0) continue;
        Node n;
        n.id = e.value("id", 0);
        if (n.id <= 0 || g.find(n.id)) continue;
        n.type = (Type)t;
        n.pos = glm::vec2(e.value("x", 0.0f), e.value("y", 0.0f));
        const TypeInfo& ti = info(n.type);
        for (int i = 0; i < ti.params; ++i) {
            float v = ti.param[i].def;
            if (e.contains("p") && e["p"].is_array() && i < (int)e["p"].size() && e["p"][i].is_number()) v = e["p"][i].get<float>();
            n.p[i] = std::clamp(v == v ? v : ti.param[i].def, ti.param[i].min, ti.param[i].max);
        }
        for (int i = 0; i < ti.colors; ++i)
            if (e.contains("c") && e["c"].is_array() && i < (int)e["c"].size() && e["c"][i].is_array() && e["c"][i].size() == 3)
                for (int k = 0; k < 3; ++k) n.c[i][k] = e["c"][i][k].is_number() ? clamp01(e["c"][i][k].get<float>()) : 0.0f;
        for (int i = 0; i < ti.inputs; ++i)
            if (e.contains("in") && e["in"].is_array() && i < (int)e["in"].size() && e["in"][i].is_number_integer())
                n.in[i] = e["in"][i].get<int>();
        g.nodes.push_back(n);
        g.nextId = std::max(g.nextId, n.id + 1);
    }
    // Wires to nothing, or that make loops, go; there's always exactly one Output.
    bool hasOutput = false;
    for (auto it = g.nodes.begin(); it != g.nodes.end();) {
        if (it->type == Type::Output) {
            if (hasOutput) { it = g.nodes.erase(it); continue; }
            hasOutput = true;
        }
        ++it;
    }
    if (!hasOutput) g.add(Type::Output, glm::vec2(600.0f, 120.0f));
    for (Node& n : g.nodes) {
        int keep[kMaxInputs];
        for (int i = 0; i < kMaxInputs; ++i) { keep[i] = n.in[i]; n.in[i] = -1; }
        for (int i = 0; i < info(n.type).inputs; ++i)
            if (keep[i] >= 0) g.connect(keep[i], n.id, i);
    }
    return true;
}

Graph starter() {
    Graph g;
    // (Ids, not references: adding a box can move the others in memory.)
    const int n = g.add(Type::Noise, glm::vec2(40.0f, 40.0f)).id;
    const int t = g.add(Type::Colorize, glm::vec2(320.0f, 40.0f)).id;
    const int o = g.add(Type::Output, glm::vec2(600.0f, 120.0f)).id;
    g.connect(n, t, 0);
    g.connect(t, o, 0);
    g.connect(n, o, 1);
    return g;
}

const std::vector<std::string>& examples() {
    static const std::vector<std::string> names = {"Bricks", "Marble", "Wood", "Rusty Metal", "Stone Tiles"};
    return names;
}

Graph example(const std::string& name) {
    Graph g;
    // Add a box with some settings / colours; returns its id.
    auto box = [&](Type t, float x, float y, std::vector<float> p = {}, std::vector<glm::vec3> c = {}) {
        Node& n = g.add(t, glm::vec2(x, y));
        for (size_t i = 0; i < p.size() && i < (size_t)kMaxParams; ++i) n.p[i] = p[i];
        for (size_t i = 0; i < c.size() && i < 2; ++i) n.c[i] = c[i];
        return n.id;
    };
    auto wire = [&](int from, int to, int input) { g.connect(from, to, input); };
    if (name == "Bricks") {
        const int bricks = box(Type::Bricks, 20, 20, {8, 4, 0.14f, 0.5f, 0.35f});
        const int noise = box(Type::Noise, 20, 300, {8, 6, 0.55f, 3});
        const int grime = box(Type::Levels, 220, 300, {0.0f, 0.8f, 1.6f});
        const int dirty = box(Type::Multiply, 420, 20);
        const int tint = box(Type::Colorize, 620, 20, {}, {glm::vec3(0.16f, 0.07f, 0.05f), glm::vec3(0.74f, 0.34f, 0.25f)});
        const int soft = box(Type::Blur, 420, 300, {0.004f});
        const int rough = box(Type::Number, 620, 300, {0.85f});
        const int out = box(Type::Output, 840, 120, {1.5f});
        wire(noise, grime, 0); wire(bricks, dirty, 0); wire(grime, dirty, 1); wire(dirty, tint, 0);
        wire(bricks, soft, 0); wire(tint, out, 0); wire(soft, out, 1); wire(rough, out, 2);
    } else if (name == "Marble") {
        const int noise = box(Type::Noise, 20, 20, {4, 6, 0.5f, 11});
        const int stripes = box(Type::Stripes, 20, 300, {3, 2, 0.0f});
        const int warp = box(Type::Warp, 220, 120, {0.3f});
        const int veins = box(Type::Levels, 420, 120, {0.05f, 0.95f, 0.6f});
        const int tint = box(Type::Colorize, 620, 20, {}, {glm::vec3(0.32f, 0.33f, 0.37f), glm::vec3(0.96f, 0.96f, 0.94f)});
        const int rough = box(Type::Number, 620, 300, {0.12f});
        const int out = box(Type::Output, 840, 120, {0.0f});
        wire(stripes, warp, 0); wire(noise, warp, 1); wire(warp, veins, 0); wire(veins, tint, 0);
        wire(tint, out, 0); wire(rough, out, 2);
    } else if (name == "Wood") {
        const int noise = box(Type::Noise, 20, 20, {2, 4, 0.5f, 5});
        const int rings = box(Type::Stripes, 20, 300, {12, 0, 0.15f});
        const int warp = box(Type::Warp, 220, 120, {0.15f});
        const int tint = box(Type::Colorize, 420, 20, {}, {glm::vec3(0.33f, 0.19f, 0.09f), glm::vec3(0.62f, 0.42f, 0.23f)});
        const int rough = box(Type::Number, 420, 300, {0.7f});
        const int out = box(Type::Output, 640, 120, {0.4f});
        wire(rings, warp, 0); wire(noise, warp, 1); wire(warp, tint, 0);
        wire(tint, out, 0); wire(warp, out, 1); wire(rough, out, 2);
    } else if (name == "Rusty Metal") {
        const int noise = box(Type::Noise, 20, 20, {6, 7, 0.6f, 21});
        const int rust = box(Type::Levels, 220, 20, {0.45f, 0.62f, 1.0f});
        const int rustColor = box(Type::Colorize, 220, 300, {}, {glm::vec3(0.22f, 0.1f, 0.05f), glm::vec3(0.62f, 0.3f, 0.12f)});
        const int steel = box(Type::Color, 420, 300, {}, {glm::vec3(0.55f, 0.55f, 0.56f)});
        const int color = box(Type::Mix, 620, 20);
        const int shiny = box(Type::Invert, 420, 20);
        const int smooth = box(Type::Number, 420, 520, {0.5f});
        const int rough = box(Type::Number, 620, 520, {0.9f});
        const int roughMix = box(Type::Mix, 820, 420);
        const int out = box(Type::Output, 1040, 120, {0.6f});
        wire(noise, rust, 0); wire(noise, rustColor, 0);
        wire(steel, color, 0); wire(rustColor, color, 1); wire(rust, color, 2);
        wire(rust, shiny, 0);
        wire(smooth, roughMix, 0); wire(rough, roughMix, 1); wire(rust, roughMix, 2);
        wire(color, out, 0); wire(noise, out, 1); wire(roughMix, out, 2); wire(shiny, out, 3);
    } else if (name == "Stone Tiles") {
        const int edges = box(Type::Cells, 20, 20, {5, 0.8f, 1, 9});
        const int mask = box(Type::Levels, 220, 20, {0.0f, 0.15f, 1.0f});
        const int shades = box(Type::Cells, 20, 300, {5, 0.8f, 2, 9});
        const int tint = box(Type::Colorize, 220, 300, {}, {glm::vec3(0.36f, 0.35f, 0.33f), glm::vec3(0.64f, 0.61f, 0.56f)});
        const int color = box(Type::Multiply, 440, 120);
        const int rough = box(Type::Number, 440, 380, {0.8f});
        const int out = box(Type::Output, 660, 120, {1.2f});
        wire(edges, mask, 0); wire(shades, tint, 0); wire(tint, color, 0); wire(mask, color, 1);
        wire(color, out, 0); wire(mask, out, 1); wire(rough, out, 2);
    } else {
        return starter();
    }
    return g;
}

Image evaluate(const Graph& g, int nodeId, int size) {
    Evaluator e{g, std::max(1, size), {}};
    const Image* img = e.node(nodeId);
    return img ? *img : e.flat(glm::vec4(0, 0, 0, 1));
}

Baked bake(const Graph& g, int size) {
    Baked b;
    const Node* out = g.output();
    if (!out) return b;
    Evaluator e{g, std::max(4, size), {}};
    auto in = [&](int i) { return out->in[i] >= 0 ? e.node(out->in[i]) : nullptr; };
    if (const Image* c = in(0)) { b.color = *c; b.hasColor = true; }
    if (const Image* h = in(1)) {
        // Bumps: the slope of the height picture, as a normal map (+Y up, like Roblox's).
        const int n = h->size;
        const float k = out->p[0] * n / 64.0f;
        b.normal.size = n;
        b.normal.px.resize((size_t)n * n);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) {
                const float dx = h->value(wrap(x + 1, n), y) - h->value(wrap(x - 1, n), y);
                const float dy = h->value(x, wrap(y + 1, n)) - h->value(x, wrap(y - 1, n));
                const glm::vec3 nm = glm::normalize(glm::vec3(-dx * k, dy * k, 1.0f));
                b.normal.px[(size_t)y * n + x] = glm::vec4(nm * 0.5f + 0.5f, 1.0f);
            }
        b.hasNormal = true;
    }
    if (const Image* r = in(2)) { b.roughness = *r; b.hasRoughness = true; }
    if (const Image* m = in(3)) { b.metalness = *m; b.hasMetalness = true; }
    return b;
}

std::vector<unsigned char> rgba(const Image& img) {
    std::vector<unsigned char> out(img.px.size() * 4);
    for (size_t i = 0; i < img.px.size(); ++i)
        for (int k = 0; k < 4; ++k) out[i * 4 + k] = (unsigned char)std::lround(clamp01(img.px[i][k]) * 255.0f);
    return out;
}

} // namespace MaterialGraph
