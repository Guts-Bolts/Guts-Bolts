#include "scene/RobloxMesh.h"

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <string>

namespace {
void u8(std::string& out, uint8_t v) { out.push_back((char)v); }
void u16(std::string& out, uint16_t v) { u8(out, (uint8_t)v); u8(out, (uint8_t)(v >> 8)); }
void u32(std::string& out, uint32_t v) {
    u8(out, (uint8_t)v); u8(out, (uint8_t)(v >> 8)); u8(out, (uint8_t)(v >> 16)); u8(out, (uint8_t)(v >> 24));
}
void f32(std::string& out, float v) { u32(out, std::bit_cast<uint32_t>(v)); }

std::string fixture(int version) {
    std::string out = version == 4 ? "version 4.00\n" : "version 5.00\n";
    const uint16_t headerSize = version == 4 ? 24 : 32;
    u16(out, headerSize);             // header size
    u16(out, 0);                      // LOD type
    u32(out, 3);                      // vertices
    u32(out, 1);                      // faces
    u16(out, 0);                      // LOD offsets
    u16(out, 1);                      // bones
    u32(out, 5);                      // bytes in bone-name table
    u16(out, 1);                      // subsets
    u8(out, 0); u8(out, 0);           // HQ LODs / reserved
    if (version == 5) { u32(out, 0); u32(out, 0); } // FACS format / payload size

    const std::array<std::array<float, 8>, 3> vertices{{
        {{-1, 0, 0, 0, 0, 1, 0, 0}},
        {{ 1, 0, 0, 0, 0, 1, 1, 0}},
        {{ 0, 1, 0, 0, 0, 1, 0.5f, 1}}
    }};
    for (const auto& v : vertices) {
        for (int i = 0; i < 8; ++i) f32(out, v[i]);
        u8(out, 127); u8(out, 0); u8(out, 0); u8(out, 127); // tangent
        u8(out, 255); u8(out, 128); u8(out, 64); u8(out, 255); // RGBA
    }
    for (int i = 0; i < 3; ++i) {
        u8(out, 0); u8(out, 0); u8(out, 0); u8(out, 0); // subset bone slots
        u8(out, 255); u8(out, 0); u8(out, 0); u8(out, 0); // four weights
    }
    u32(out, 0); u32(out, 1); u32(out, 2); // one triangle

    u32(out, 0);             // bone name offset
    u16(out, 0xffff);        // root bone
    u16(out, 0xffff);        // LOD parent
    f32(out, 0.0f);          // culling
    for (float v : std::array<float, 9>{1,0,0, 0,1,0, 0,0,1}) f32(out, v);
    f32(out, 0); f32(out, 0); f32(out, 0); // bone position
    out += std::string("Root\0", 5);

    u32(out, 0); u32(out, 1); // face begin / length
    u32(out, 0); u32(out, 3); // vertex begin / length
    u32(out, 1);              // number of bone indices
    for (int i = 0; i < 26; ++i) u16(out, i == 0 ? 0 : 0);
    return out;
}

bool run(int version) {
    std::string error;
    RobloxMesh::Shape shape = RobloxMesh::parse(fixture(version), &error);
    if (!shape.mesh) {
        std::cerr << "v" << version << " parse failed: " << error << "\n";
        return false;
    }
    const auto& mesh = *shape.mesh;
    if (mesh.faces.size() != 1 || mesh.verts.size() != 3) {
        std::cerr << "v" << version << " geometry counts were incorrect\n";
        return false;
    }
    if (shape.bones.size() != 1 || shape.bones[0].name != "Root" || !mesh.skinned()) {
        std::cerr << "v" << version << " embedded rig was not retained\n";
        return false;
    }
    if (mesh.weights.size() != 3 || mesh.weights[0].size() != 1 ||
        mesh.weights[0][0].bone != 0 || mesh.weights[0][0].w < 0.99f) {
        std::cerr << "v" << version << " skin weights were not decoded\n";
        return false;
    }
    if (mesh.colors.size() != 3 || mesh.tangents.size() != 3 ||
        mesh.colors[0].g < 0.49f || mesh.colors[0].g > 0.51f ||
        mesh.tangents[0].x < 0.99f) {
        std::cerr << "v" << version << " vertex color/tangent data was not retained\n";
        return false;
    }
    return true;
}
} // namespace

int main() {
    if (!run(4) || !run(5)) return 1;
    std::cout << "Roblox FileMesh v4/v5 synthetic format tests passed\n";
    return 0;
}
