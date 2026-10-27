#pragma once
// The Material Graph: make a material's pictures from nodes instead of photos. Boxes
// (noise, bricks, cells, stripes...) are joined by wires and mixed, tinted and warped,
// and the Output box says what becomes the colours, the bumps, the rough spots and
// the metal. Baking turns it into the four pictures a MaterialVariant uses, so it looks
// the same everywhere a material does (and costs nothing extra to draw).
//
// Everything it makes tiles: the right edge matches the left and the top the bottom.
// Saved as JSON in the MaterialVariant (MaterialVariantProps::graph).
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace MaterialGraph {

enum class Type {
    Output, Color, Number, Noise, Cells, Bricks, Checker, Stripes, Gradient,
    Mix, Multiply, Add, Levels, Invert, Colorize, Warp, Blur, Count
};

inline constexpr int kMaxInputs = 4, kMaxParams = 5;

// What each kind of box has: its inputs (wires in) and its settings.
struct Param { const char* name; float def, min, max; bool whole; const char* tip; };
struct TypeInfo {
    const char* name;
    const char* help;
    int         inputs;
    const char* inputNames[kMaxInputs];
    int         params;
    Param       param[kMaxParams];
    int         colors;             // how many colour settings (Color: 1, Colorize: 2)
    const char* colorNames[2];
};
const TypeInfo& info(Type t);

struct Node {
    int       id = 0;
    Type      type = Type::Noise;
    glm::vec2 pos{0.0f};             // where the box is on the canvas
    float     p[kMaxParams] = {};
    glm::vec3 c[2] = {glm::vec3(1.0f), glm::vec3(0.0f)};
    int       in[kMaxInputs] = {-1, -1, -1, -1};   // the node wired into each input (-1 = none)
};

struct Graph {
    std::vector<Node> nodes;
    int nextId = 1;
    Node*       find(int id);
    const Node* find(int id) const;
    Node&       add(Type t, glm::vec2 pos);       // with its settings at their defaults
    void        remove(int id);                    // (and the wires to it)
    // Wire `from`'s picture into input `input` of `to` (false: it would make a loop).
    bool        connect(int from, int to, int input);
    const Node* output() const;                    // the Output box (every graph has one)
};

nlohmann::json toJson(const Graph& g);
bool           fromJson(const nlohmann::json& j, Graph& g);   // (fixes up anything odd)
Graph          starter();                                      // a simple one to start from
// Ready-made graphs to start from: "Bricks", "Marble", "Wood", "Rusty Metal", "Stone Tiles".
const std::vector<std::string>& examples();
Graph          example(const std::string& name);              // (unknown name: starter())

// A square picture (rows top first), 0..1 per channel. Plain values are grey (r = g = b).
struct Image {
    int size = 0;
    std::vector<glm::vec4> px;
    float     value(int x, int y) const { return px[(size_t)y * size + x].r; }
};
// One box's picture (for its preview).
Image evaluate(const Graph& g, int nodeId, int size);

// The four pictures for the material. A "has" flag is false when nothing is wired into
// that part of the Output (the material's own slider is used instead).
struct Baked {
    Image color, normal, roughness, metalness;
    bool  hasColor = false, hasNormal = false, hasRoughness = false, hasMetalness = false;
};
Baked bake(const Graph& g, int size);
// 8-bit RGBA (rows top first), for PNG files and previews.
std::vector<unsigned char> rgba(const Image& img);

} // namespace MaterialGraph
