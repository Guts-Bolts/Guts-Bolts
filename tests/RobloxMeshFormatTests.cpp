#include "scene/RobloxMesh.h"
#include <draco/attributes/geometry_attribute.h>
#include <draco/compression/encode.h>
#include <draco/core/encoder_buffer.h>
#include <draco/mesh/mesh.h>

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

namespace {
void u8(std::string& out, uint8_t v) { out.push_back((char)v); }
void u16(std::string& out, uint16_t v) { u8(out, (uint8_t)v); u8(out, (uint8_t)(v >> 8)); }
void u32(std::string& out, uint32_t v) {
    u8(out, (uint8_t)v); u8(out, (uint8_t)(v >> 8)); u8(out, (uint8_t)(v >> 16)); u8(out, (uint8_t)(v >> 24));
}
void f32(std::string& out, float v) { u32(out, std::bit_cast<uint32_t>(v)); }

std::string fixture(int version, bool minorOne = false) {
    std::string out = version == 4 ? (minorOne ? "version 4.01\n" : "version 4.00\n") : "version 5.00\n";
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
    for (int i = 0; i < 26; ++i) u16(out, 0);
    return out;
}

std::string textFixture(bool oldVersion) {
    std::string out = oldVersion ? "version 1.00\n1\n" : "version 1.01\n1\n";
    out += "[-1,0,0][0,0,1][0,0,0]";
    out += "[1,0,0][0,0,1][1,0,0]";
    out += "[0,1,0][0,0,1][0.5,1,0]";
    return out;
}

std::string binaryStaticFixture(int version, bool minorOne = false, uint8_t vertexStride = 36) {
    std::string out = version == 2 ? "version 2.00\n" :
        (minorOne ? "version 3.01\n" : "version 3.00\n");
    if (version == 2) {
        u16(out, 12); u8(out, vertexStride); u8(out, 12);
        u32(out, 3); u32(out, 1);
    } else {
        u16(out, 16); u8(out, vertexStride); u8(out, 12);
        u16(out, 4); u16(out, 0);
        u32(out, 3); u32(out, 1);
    }
    const std::array<std::array<float, 8>, 3> vertices{{
        {{-1, 0, 0, 0, 0, 1, 0, 0}},
        {{ 1, 0, 0, 0, 0, 1, 1, 0}},
        {{ 0, 1, 0, 0, 0, 1, 0.5f, 1}}
    }};
    for (const auto& v : vertices) {
        for (float x : v) f32(out, x);
        if (vertexStride >= 36) { u8(out, 127); u8(out, 0); u8(out, 0); u8(out, 127); }
        if (vertexStride >= 40) { u8(out, 255); u8(out, 128); u8(out, 64); u8(out, 255); }
    }
    u32(out, 0); u32(out, 1); u32(out, 2);
    return out;
}

bool runStatic(const std::string& bytes, const char* label) {
    std::string error;
    RobloxMesh::Shape shape = RobloxMesh::parse(bytes, &error);
    if (!shape.mesh || shape.mesh->faces.size() != 1 || shape.mesh->verts.size() != 3) {
        std::cerr << label << " geometry parse failed: " << error << "\n";
        return false;
    }
    return true;
}

void chunk(std::string& out, const char* name, const std::string& payload, uint32_t version = 1) {
    out.append(name, 8);
    u32(out, version);
    u32(out, (uint32_t)payload.size());
    out += payload;
}

std::string skinPayload() {
    std::string skin;
    u32(skin, 3);
    for (int i = 0; i < 3; ++i) {
        u8(skin, 0); u8(skin, 0); u8(skin, 0); u8(skin, 0);
        u8(skin, 255); u8(skin, 0); u8(skin, 0); u8(skin, 0);
    }
    u32(skin, 1);
    u32(skin, 0); u16(skin, 0xffff); u16(skin, 0xffff); f32(skin, 0);
    for (float v : std::array<float, 9>{1,0,0, 0,1,0, 0,0,1}) f32(skin, v);
    f32(skin, 0); f32(skin, 0); f32(skin, 0);
    u32(skin, 5); skin += std::string("Root\0", 5);
    u32(skin, 1);
    u32(skin, 0); u32(skin, 1); u32(skin, 0); u32(skin, 3); u32(skin, 1);
    for (int i = 0; i < 26; ++i) u16(skin, 0);
    return skin;
}

std::string dracoCorePayload() {
    draco::Mesh mesh;
    mesh.set_num_points(3);
    mesh.SetNumFaces(1);
    auto addAttribute = [&](draco::GeometryAttribute::Type type, int components,
                            draco::DataType dataType, bool normalized, int stride) {
        draco::GeometryAttribute attr;
        attr.Init(type, nullptr, (int8_t)components, dataType, normalized, stride, 0);
        return mesh.AddAttribute(attr, true, 3);
    };
    const int posId = addAttribute(draco::GeometryAttribute::POSITION, 3, draco::DT_FLOAT32, false, 12);
    const int normalId = addAttribute(draco::GeometryAttribute::NORMAL, 3, draco::DT_FLOAT32, false, 12);
    const int uvId = addAttribute(draco::GeometryAttribute::TEX_COORD, 2, draco::DT_FLOAT32, false, 8);
    const int tangentId = addAttribute(draco::GeometryAttribute::GENERIC, 4, draco::DT_UINT8, false, 4);
    const int colorId = addAttribute(draco::GeometryAttribute::COLOR, 4, draco::DT_UINT8, true, 4);
    const std::array<std::array<float, 3>, 3> positions{{{{-1,0,0}},{{1,0,0}},{{0,1,0}}}};
    const std::array<float, 3> normal{0,0,1};
    const std::array<std::array<float, 2>, 3> uv{{{{0,0}},{{1,0}},{{0.5f,1}}}};
    const std::array<uint8_t, 4> tangent{127,0,0,127}, color{255,128,64,255};
    for (int i = 0; i < 3; ++i) {
        mesh.attribute(posId)->SetAttributeValue(draco::AttributeValueIndex(i), positions[i].data());
        mesh.attribute(normalId)->SetAttributeValue(draco::AttributeValueIndex(i), normal.data());
        mesh.attribute(uvId)->SetAttributeValue(draco::AttributeValueIndex(i), uv[i].data());
        mesh.attribute(tangentId)->SetAttributeValue(draco::AttributeValueIndex(i), tangent.data());
        mesh.attribute(colorId)->SetAttributeValue(draco::AttributeValueIndex(i), color.data());
    }
    draco::Mesh::Face face;
    face[0] = draco::PointIndex(0); face[1] = draco::PointIndex(1); face[2] = draco::PointIndex(2);
    mesh.SetFace(draco::FaceIndex(0), face);
    draco::Encoder encoder;
    draco::EncoderBuffer buffer;
    const draco::Status status = encoder.EncodeMeshToBuffer(mesh, &buffer);
    if (!status.ok()) return {};
    std::string payload;
    u32(payload, (uint32_t)buffer.size());
    payload.append(buffer.data(), buffer.size());
    return payload;
}

std::string chunkedV7Fixture() {
    std::string out = "version 7.00\n";
    const std::string core = dracoCorePayload();
    if (core.empty()) return {};
    chunk(out, "COREMESH", core, 2);
    chunk(out, "SKINNING", skinPayload());
    return out;
}

std::string chunkedV6Fixture() {
    std::string out = "version 6.00\n";
    std::string core;
    u32(core, 3);
    const std::array<std::array<float, 8>, 3> vertices{{
        {{-1, 0, 0, 0, 0, 1, 0, 0}},
        {{ 1, 0, 0, 0, 0, 1, 1, 0}},
        {{ 0, 1, 0, 0, 0, 1, 0.5f, 1}}
    }};
    for (const auto& v : vertices) {
        for (int i = 0; i < 8; ++i) f32(core, v[i]);
        u8(core, 127); u8(core, 0); u8(core, 0); u8(core, 127);
        u8(core, 255); u8(core, 128); u8(core, 64); u8(core, 255);
    }
    u32(core, 1); u32(core, 0); u32(core, 1); u32(core, 2);
    chunk(out, "COREMESH", core);

    chunk(out, "SKINNING", skinPayload());
    return out;
}

bool runChunkedV7() {
    std::string error;
    const std::string bytes = chunkedV7Fixture();
    if (bytes.empty()) { std::cerr << "v7 Draco fixture encoding failed\n"; return false; }
    RobloxMesh::Shape shape = RobloxMesh::parse(bytes, &error);
    if (!shape.mesh) {
        std::cerr << "v7 parse failed: " << error << "\n";
        return false;
    }
    const auto& mesh = *shape.mesh;
    if (mesh.faces.size() != 1 || shape.bones.size() != 1 || !mesh.skinned() ||
        mesh.weights.empty() || mesh.weights[0].empty() || mesh.weights[0][0].w < 0.99f ||
        mesh.colors.empty() || mesh.colors[0].g < 0.49f || mesh.colors[0].g > 0.51f ||
        mesh.tangents.empty() || mesh.tangents[0].x < 0.99f) {
        std::cerr << "v7 Draco geometry, skinning, or attributes were not retained\n";
        return false;
    }
    return true;
}

bool runChunkedV6() {
    std::string error;
    RobloxMesh::Shape shape = RobloxMesh::parse(chunkedV6Fixture(), &error);
    if (!shape.mesh) {
        std::cerr << "v6 parse failed: " << error << "\n";
        return false;
    }
    const auto& mesh = *shape.mesh;
    if (mesh.faces.size() != 1 || mesh.verts.size() != 3 || shape.bones.size() != 1 ||
        shape.bones[0].name != "Root" || !mesh.skinned() || mesh.weights[0].empty() ||
        mesh.weights[0][0].w < 0.99f || mesh.colors.size() != 3 || mesh.tangents.size() != 3) {
        std::cerr << "v6 chunked geometry / skinning / attributes were not retained\n";
        return false;
    }
    return true;
}

bool run(int version, bool minorOne = false) {
    std::string error;
    RobloxMesh::Shape shape = RobloxMesh::parse(fixture(version, minorOne), &error);
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
    if (!runStatic(textFixture(true), "v1.00") ||
        !runStatic(textFixture(false), "v1.01") ||
        !runStatic(binaryStaticFixture(2), "v2.00") ||
        !runStatic(binaryStaticFixture(3, false, 40), "v3.00") ||
        !runStatic(binaryStaticFixture(3, true, 36), "v3.01") ||
        !run(4) || !run(4, true) || !run(5) || !runChunkedV6() || !runChunkedV7()) return 1;
    std::cout << "Roblox FileMesh v1.00/v1.01/v2/v3.00/v3.01/v4.00/v4.01/v5/v6/v7 synthetic tests passed\n";
    return 0;
}
