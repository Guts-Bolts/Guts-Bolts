#include "RobloxMesh.h"
#include <draco/compression/decode.h>
#include <draco/mesh/mesh.h>
#include <draco/attributes/geometry_attribute.h>
#include "../core/Paths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <tuple>
#include <vector>

namespace RobloxMesh {

namespace {

struct Corner {
    glm::vec3 pos, normal;
    glm::vec2 uv;
    glm::vec4 color{1.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// Raw triangles (three corners each) -> an EditMesh. Corners at the same spot with
// the same normal become one point, so smooth shading works but hard edges (where
// Roblox split the point on purpose) stay hard.
Shape build(const std::vector<Corner>& verts, const std::vector<uint32_t>& tris, bool flipV, std::string* err = nullptr,
            const std::vector<std::vector<EditMesh::Weight>>* skinWeights = nullptr,
            const std::vector<Bone>* bones = nullptr) {
    Shape s;
    // Reject corrupt/non-finite coordinates before they reach bounding boxes, UVs,
    // the editor mesh, or GPU buffers. The same validation applies to every format.
    for (const Corner& c : verts) {
        const float values[] = {c.pos.x, c.pos.y, c.pos.z, c.normal.x, c.normal.y, c.normal.z, c.uv.x, c.uv.y};
        for (float v : values) {
            if (!std::isfinite(v) || std::abs(v) > 1.0e8f) {
                if (err) *err = "the mesh contains a non-finite or out-of-range vertex attribute";
                return s;
            }
        }
    }
    auto mesh = std::make_shared<EditMesh>();
    mesh->smooth = true;
    std::map<std::tuple<long, long, long, int, int, int>, uint32_t> weld;
    auto q = [](float f, float k) { return (long)std::lround(f * k); };
    std::vector<uint32_t> remap(verts.size());
    const bool preserveSkinVertices = skinWeights && skinWeights->size() == verts.size() && bones && !bones->empty();
    const bool preserveVertexAttributes = std::any_of(verts.begin(), verts.end(), [](const Corner& c) {
        return glm::length(c.color - glm::vec4(1.0f)) > 1e-5f ||
               glm::length(c.tangent - glm::vec4(1, 0, 0, 1)) > 1e-5f;
    });
    for (size_t i = 0; i < verts.size(); ++i) {
        const Corner& c = verts[i];
        if (preserveSkinVertices || preserveVertexAttributes) {
            // Welding vertices with different bone weights, colours, or tangents
            // would corrupt their per-vertex attributes.
            remap[i] = (uint32_t)mesh->verts.size();
            mesh->verts.push_back(c.pos);
            mesh->colors.push_back(c.color);
            mesh->tangents.push_back(c.tangent);
            continue;
        }
        auto key = std::make_tuple(q(c.pos.x, 1e4f), q(c.pos.y, 1e4f), q(c.pos.z, 1e4f),
                                   (int)q(c.normal.x, 20.0f), (int)q(c.normal.y, 20.0f), (int)q(c.normal.z, 20.0f));
        auto [it, fresh] = weld.try_emplace(key, (uint32_t)mesh->verts.size());
        if (fresh) {
            mesh->verts.push_back(c.pos);
            mesh->colors.push_back(c.color);
            mesh->tangents.push_back(c.tangent);
        }
        remap[i] = it->second;
    }
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        uint32_t a = tris[t], b = tris[t + 1], c = tris[t + 2];
        if (a >= verts.size() || b >= verts.size() || c >= verts.size()) continue;
        if (remap[a] == remap[b] || remap[b] == remap[c] || remap[a] == remap[c]) continue;   // squashed flat
        mesh->faces.push_back({remap[a], remap[b], remap[c]});
        // Roblox counts the picture's rows from the top; we count from the bottom
        // (except the old text meshes, which were already stored the other way up).
        auto uv = [&](uint32_t k) { return glm::vec2(verts[k].uv.x, flipV ? 1.0f - verts[k].uv.y : verts[k].uv.y); };
        mesh->uvs.push_back({uv(a), uv(b), uv(c)});
    }
    if (mesh->faces.empty()) return s;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const auto& p : mesh->verts) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    s.center = (lo + hi) * 0.5f;
    s.size = hi - lo;
    glm::vec3 div = glm::max(s.size, glm::vec3(1e-6f));   // a flat shape stays flat
    for (auto& p : mesh->verts) p = (p - s.center) / div;
    if (preserveSkinVertices) {
        mesh->weights = *skinWeights;
        for (const Bone& b : *bones) mesh->bones.push_back(b.name);
        mesh->bind.assign(mesh->bones.size(), glm::mat4(1.0f));
        s.bones = *bones;
    }
    s.mesh = std::move(mesh);
    return s;
}

// Little-endian reading with bounds checks.
struct Bytes {
    const uint8_t* p;
    size_t n, pos = 0;
    bool bad = false;
    bool has(size_t k) { if (pos > n || k > n - pos) { bad = true; return false; } return true; }
    uint8_t  u8()  { return has(1) ? p[pos++] : 0; }
    uint16_t u16() { if (!has(2)) return 0; uint16_t v; std::memcpy(&v, p + pos, 2); pos += 2; return v; }
    uint32_t u32() { if (!has(4)) return 0; uint32_t v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
    float    f32(size_t at) const { float v; std::memcpy(&v, p + at, 4); return v; }
};

// `count` points of `stride` bytes each: position, normal, uv (the same start in every version).
bool readVerts(Bytes& r, uint32_t count, size_t stride, std::vector<Corner>& out) {
    if (stride < 32 || count > 5'000'000 || !r.has((size_t)count * stride)) return false;
    out.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        size_t at = r.pos + (size_t)i * stride;
        out[i].pos    = {r.f32(at), r.f32(at + 4), r.f32(at + 8)};
        out[i].normal = {r.f32(at + 12), r.f32(at + 16), r.f32(at + 20)};
        out[i].uv     = {r.f32(at + 24), r.f32(at + 28)};
        if (stride >= 40) {
            const auto* tangent = reinterpret_cast<const int8_t*>(r.p + at + 32);
            out[i].tangent = {
                std::clamp(tangent[0] / 127.0f, -1.0f, 1.0f),
                std::clamp(tangent[1] / 127.0f, -1.0f, 1.0f),
                std::clamp(tangent[2] / 127.0f, -1.0f, 1.0f),
                tangent[3] < 0 ? -1.0f : 1.0f
            };
            const uint8_t* color = r.p + at + 36;
            out[i].color = glm::vec4(color[0], color[1], color[2], color[3]) / 255.0f;
        }
    }
    r.pos += (size_t)count * stride;
    return true;
}

bool readFaces(Bytes& r, uint32_t count, size_t stride, std::vector<uint32_t>& out) {
    if (stride < 12 || count > 10'000'000 || !r.has((size_t)count * stride)) return false;
    out.resize((size_t)count * 3);
    for (uint32_t i = 0; i < count; ++i) std::memcpy(&out[(size_t)i * 3], r.p + r.pos + (size_t)i * stride, 12);
    r.pos += (size_t)count * stride;
    return true;
}

// Only the first (most detailed) level of detail: faces lods[0]..lods[1].
void firstLod(std::vector<uint32_t>& tris, const std::vector<uint32_t>& lods) {
    if (lods.size() < 2) return;
    size_t a = (size_t)lods[0] * 3, b = (size_t)lods[1] * 3;
    if (a < b && b <= tris.size()) tris = std::vector<uint32_t>(tris.begin() + (long)a, tris.begin() + (long)b);
}

// Roblox FileMesh v7 stores its CORE MESH as a length-prefixed Draco bitstream.
// Roblox assigns stable unique IDs: 0=position, 1=normal, 2=UV. Draco point
// indices are not necessarily attribute-value indices, so map them first.
bool readDracoCore(Bytes& c, std::vector<Corner>& verts, std::vector<uint32_t>& tris,
                   std::string* err) {
    const uint32_t bitstreamSize = c.u32();
    if (bitstreamSize == 0 || bitstreamSize > 256u * 1024u * 1024u || !c.has(bitstreamSize)) {
        if (err) *err = "the Draco mesh payload is empty, too large, or truncated";
        return false;
    }

    draco::DecoderBuffer buffer;
    buffer.Init(reinterpret_cast<const char*>(c.p + c.pos), bitstreamSize);
    draco::Decoder decoder;
    auto decoded = decoder.DecodeMeshFromBuffer(&buffer);
    if (!decoded.ok()) {
        if (err) *err = "Draco couldn't decode the Roblox mesh: " + decoded.status().error_msg_string();
        return false;
    }
    std::unique_ptr<draco::Mesh> mesh = std::move(decoded).value();
    if (!mesh || mesh->num_points() == 0 || mesh->num_faces() == 0 ||
        mesh->num_points() > 5'000'000 || mesh->num_faces() > 10'000'000) {
        if (err) *err = "the Draco mesh has no geometry or exceeds the safe import limits";
        return false;
    }

    const draco::PointAttribute* positions = mesh->GetAttributeByUniqueId(0);
    if (!positions) positions = mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
    const draco::PointAttribute* normals = mesh->GetAttributeByUniqueId(1);
    if (!normals) normals = mesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
    const draco::PointAttribute* texcoords = mesh->GetAttributeByUniqueId(2);
    if (!texcoords) texcoords = mesh->GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
    if (!positions) {
        if (err) *err = "the Draco mesh has no position attribute";
        return false;
    }

    std::vector<Corner> points((size_t)mesh->num_points());
    for (int32_t pointIndex = 0; pointIndex < mesh->num_points(); ++pointIndex) {
        const draco::PointIndex pi(pointIndex);
        Corner& out = points[(size_t)pointIndex];
        float pos[3] = {};
        if (!positions->ConvertValue(positions->mapped_index(pi), 3, draco::DT_FLOAT32, pos)) {
            if (err) *err = "the Draco position attribute could not be converted to float3";
            return false;
        }
        out.pos = {pos[0], pos[1], pos[2]};
        out.normal = {0.0f, 0.0f, 0.0f};
        out.uv = {0.0f, 0.0f};
        if (normals) {
            float n[3] = {};
            if (normals->ConvertValue(normals->mapped_index(pi), 3, draco::DT_FLOAT32, n))
                out.normal = glm::vec3(n[0], n[1], n[2]);
        }
        if (texcoords) {
            float uv[2] = {};
            if (texcoords->ConvertValue(texcoords->mapped_index(pi), 2, draco::DT_FLOAT32, uv))
                out.uv = glm::vec2(uv[0], uv[1]);
        }
    }

    verts.clear();
    tris.clear();
    verts.reserve((size_t)mesh->num_faces() * 3);
    tris.reserve((size_t)mesh->num_faces() * 3);
    for (int32_t faceIndex = 0; faceIndex < mesh->num_faces(); ++faceIndex) {
        const auto& face = mesh->face(draco::FaceIndex(faceIndex));
        const uint32_t base = (uint32_t)verts.size();
        bool valid = true;
        for (int corner = 0; corner < 3; ++corner) {
            const uint32_t point = (uint32_t)face[corner].value();
            if (point >= points.size()) { valid = false; break; }
            verts.push_back(points[point]);
            tris.push_back(base + (uint32_t)corner);
        }
        if (!valid) {
            verts.resize(base);
            tris.resize(base);
        }
    }

    // Some legacy streams omit normals. Derive flat normals so lighting remains valid.
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        Corner& a = verts[tris[i]];
        Corner& b = verts[tris[i + 1]];
        Corner& d = verts[tris[i + 2]];
        glm::vec3 n = glm::cross(b.pos - a.pos, d.pos - a.pos);
        if (glm::length(n) > 1e-8f) n = glm::normalize(n);
        if (glm::length(a.normal) < 1e-6f) a.normal = n;
        if (glm::length(b.normal) < 1e-6f) b.normal = n;
        if (glm::length(d.normal) < 1e-6f) d.normal = n;
    }
    return !tris.empty();
}

// "version 1.00" / "1.01": text. A face count, then nine [x,y,z] groups per
// triangle (position, normal, uv for each corner).
Shape parseText(const std::string& text, size_t start, bool halve, std::string* err) {
    std::vector<float> nums;
    nums.reserve(text.size() / 6);
    size_t i = text.find('[', start);
    while (i != std::string::npos && i < text.size()) {
        const char* s = text.c_str() + i + 1;
        for (int k = 0; k < 3; ++k) {
            char* e = nullptr;
            float f = std::strtof(s, &e);
            if (e == s) break;
            nums.push_back(f);
            s = e;
            while (*s == ',' || *s == ' ') ++s;
        }
        i = text.find('[', i + 1);
    }
    size_t corners = nums.size() / 9;
    std::vector<Corner> verts(corners);
    std::vector<uint32_t> tris(corners);
    for (size_t c = 0; c < corners; ++c) {
        const float* f = &nums[c * 9];
        verts[c].pos = glm::vec3(f[0], f[1], f[2]) * (halve ? 0.5f : 1.0f);   // 1.00 files are twice too big
        verts[c].normal = {f[3], f[4], f[5]};
        verts[c].uv = {f[6], f[7]};
        tris[c] = (uint32_t)c;
    }
    tris.resize(corners / 3 * 3);
    Shape s = build(verts, tris, false, err);
    if (!s.mesh && err) *err = "the mesh has no triangles";
    return s;
}

} // namespace

Shape parse(const std::string& bytes, std::string* err) {
    if (bytes.rfind("version ", 0) != 0) { if (err) *err = "not a Roblox mesh file"; return {}; }
    size_t eol = bytes.find('\n');
    if (eol == std::string::npos) { if (err) *err = "the mesh file is cut short"; return {}; }
    std::string ver = bytes.substr(8, std::min<size_t>(eol - 8, 8));
    while (!ver.empty() && std::isspace((unsigned char)ver.back())) ver.pop_back();
    if (ver == "1.00" || ver == "1.01") return parseText(bytes, eol + 1, ver == "1.00", err);

    Bytes r{(const uint8_t*)bytes.data(), bytes.size(), eol + 1};
    std::vector<Corner> verts;
    std::vector<uint32_t> tris, lods;
    const int major = std::atoi(ver.c_str());
    if (major == 2 || major == 3) {
        size_t start = r.pos;
        uint16_t headerSize = r.u16();
        uint8_t vertSize = r.u8(), faceSize = r.u8();
        uint16_t lodSize = 0, lodCount = 0;
        if (major == 3) { lodSize = r.u16(); lodCount = r.u16(); }
        uint32_t nv = r.u32(), nf = r.u32();
        r.pos = start + headerSize;
        if (!readVerts(r, nv, vertSize, verts) || !readFaces(r, nf, faceSize, tris)) { if (err) *err = "the mesh file is cut short"; return {}; }
        for (uint16_t i = 0; i < lodCount && lodSize >= 4 && r.has(lodSize); ++i) { uint32_t v; std::memcpy(&v, r.p + r.pos, 4); lods.push_back(v); r.pos += lodSize; }
    } else if (major == 4 || major == 5) {
        size_t start = r.pos;
        uint16_t headerSize = r.u16();
        r.u16();
        uint32_t nv = r.u32(), nf = r.u32();
        uint16_t lodCount = r.u16(), boneCount = r.u16();
        uint32_t boneNamesSize = r.u32();
        uint16_t subsetCount = r.u16();
        r.u8(); r.u8();
        uint32_t facsFormat = 0, facsSize = 0;
        if (major == 5) { facsFormat = r.u32(); facsSize = r.u32(); }
        if (r.bad || headerSize < (major == 4 ? 24 : 32) || headerSize > bytes.size() - start ||
            nv > 5'000'000 || nf > 10'000'000 || boneCount > 4096 ||
            boneNamesSize > 16u * 1024u * 1024u || facsSize > 64u * 1024u * 1024u) {
            if (err) *err = "the FileMesh v4/v5 header contains invalid sizes";
            return {};
        }
        r.pos = start + headerSize;
        if (!readVerts(r, nv, 40, verts)) { if (err) *err = "the mesh file is cut short"; return {}; }

        std::vector<std::array<uint8_t, 4>> vertexSubsetIndices(nv), vertexWeights(nv);
        if (boneCount > 0) {
            if (!r.has((size_t)nv * 8)) { if (err) *err = "the mesh skin-weight table is truncated"; return {}; }
            for (uint32_t i = 0; i < nv; ++i) {
                for (int j = 0; j < 4; ++j) vertexSubsetIndices[i][j] = r.u8();
                for (int j = 0; j < 4; ++j) vertexWeights[i][j] = r.u8();
            }
        }
        if (!readFaces(r, nf, 12, tris)) { if (err) *err = "the mesh file is cut short"; return {}; }
        for (uint16_t i = 0; i < lodCount; ++i) {
            if (!r.has(4)) { if (err) *err = "the mesh LOD table is truncated"; return {}; }
            lods.push_back(r.u32());
        }

        std::vector<uint32_t> boneNameOffsets;
        std::vector<Bone> embeddedBones;
        boneNameOffsets.reserve(boneCount);
        embeddedBones.reserve(boneCount);
        for (uint16_t i = 0; i < boneCount; ++i) {
            if (!r.has(60)) { if (err) *err = "the mesh bone table is truncated"; return {}; }
            uint32_t nameOffset = r.u32();
            uint16_t parentIndex = r.u16();
            r.u16();
            r.f32(r.pos); r.pos += 4;
            Bone b;
            b.parent = parentIndex == 0xffffu ? -1 : (parentIndex < boneCount ? (int)parentIndex : -1);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) b.rotation[col][row] = r.f32(r.pos + (size_t)(row * 3 + col) * 4);
            r.pos += 36;
            b.position = {r.f32(r.pos), r.f32(r.pos + 4), r.f32(r.pos + 8)};
            r.pos += 12;
            if (r.bad) { if (err) *err = "the mesh bone table is truncated"; return {}; }
            boneNameOffsets.push_back(nameOffset);
            embeddedBones.push_back(b);
        }
        if (!r.has(boneNamesSize)) { if (err) *err = "the mesh bone-name table is truncated"; return {}; }
        std::string boneNameTable((const char*)r.p + r.pos, boneNamesSize);
        r.pos += boneNamesSize;
        for (size_t i = 0; i < embeddedBones.size(); ++i) {
            size_t off = boneNameOffsets[i];
            if (off >= boneNameTable.size()) {
                embeddedBones[i].name = "Bone" + std::to_string(i);
                continue;
            }
            size_t end = boneNameTable.find('\0', off);
            if (end == std::string::npos) end = boneNameTable.size();
            embeddedBones[i].name = boneNameTable.substr(off, end - off);
            if (embeddedBones[i].name.empty()) embeddedBones[i].name = "Bone" + std::to_string(i);
        }

        struct Subset {
            uint32_t vertsBegin = 0, vertsLength = 0;
            uint32_t boneIndexCount = 0;
            uint16_t boneIndices[26]{};
        };
        std::vector<Subset> subsets;
        subsets.reserve(subsetCount);
        for (uint16_t i = 0; i < subsetCount; ++i) {
            if (!r.has(72)) { if (err) *err = "the mesh subset table is truncated"; return {}; }
            r.u32(); r.u32();
            Subset s;
            s.vertsBegin = r.u32();
            s.vertsLength = r.u32();
            s.boneIndexCount = std::min<uint32_t>(r.u32(), 26);
            for (int j = 0; j < 26; ++j) s.boneIndices[j] = r.u16();
            subsets.push_back(s);
        }
        std::vector<std::vector<EditMesh::Weight>> skinWeights(nv);
        if (boneCount > 0 && subsetCount > 0) {
            for (const Subset& subset : subsets) {
                const size_t end = std::min<size_t>((size_t)subset.vertsBegin + subset.vertsLength, nv);
                for (size_t v = subset.vertsBegin; v < end; ++v) {
                    auto& weights = skinWeights[v];
                    for (int j = 0; j < 4; ++j) {
                        const uint8_t subsetIndex = vertexSubsetIndices[v][j];
                        const float weight = vertexWeights[v][j] / 255.0f;
                        if (weight <= 0.0f || subsetIndex >= subset.boneIndexCount) continue;
                        const uint16_t boneIndex = subset.boneIndices[subsetIndex];
                        if (boneIndex >= boneCount) continue;
                        auto found = std::find_if(weights.begin(), weights.end(), [&](const EditMesh::Weight& w) { return w.bone == boneIndex; });
                        if (found == weights.end()) weights.push_back({boneIndex, weight});
                        else found->w += weight;
                    }
                }
            }
            for (auto& weights : skinWeights) {
                float sum = 0.0f;
                for (const auto& w : weights) sum += w.w;
                if (sum > 1e-6f) for (auto& w : weights) w.w /= sum;
            }
        }
        if (major == 5 && facsSize > 0) {
            if (!r.has(facsSize)) { if (err) *err = "the mesh FACS payload is truncated"; return {}; }
            r.pos += facsSize;
            (void)facsFormat;
        }
        if (boneCount > 0 && !embeddedBones.empty() &&
            std::any_of(skinWeights.begin(), skinWeights.end(), [](const auto& w) { return !w.empty(); })) {
            firstLod(tris, lods);
            Shape s = build(verts, tris, true, err, &skinWeights, &embeddedBones);
            if (!s.mesh && err) *err = "the mesh has no triangles";
            return s;
        }
    } else if (major == 6 || major == 7) {
        // Labelled chunks: "COREMESH" holds the points and triangles, "LODS" the detail levels.
        bool core = false;
        while (r.has(16)) {
            std::string kind((const char*)r.p + r.pos, 8);
            r.pos += 8;
            uint32_t chunkVer = r.u32(), size = r.u32();
            if (!r.has(size)) break;
            Bytes c{r.p, r.pos + size, r.pos};
            if (kind == "COREMESH") {
                if (chunkVer == 1) {
                    uint32_t nv = c.u32();
                    if (!readVerts(c, nv, 40, verts)) break;
                    uint32_t nf = c.u32();
                    if (!readFaces(c, nf, 12, tris)) break;
                    core = true;
                } else if (chunkVer == 2) {
                    if (!readDracoCore(c, verts, tris, err)) return {};
                    core = true;
                } else {
                    if (err) *err = "COREMESH chunk version " + std::to_string(chunkVer) + " isn't supported yet";
                    return {};
                }
            } else if (kind.rfind("LODS", 0) == 0) {
                c.u16(); c.u8();
                uint32_t n = c.u32();
                for (uint32_t i = 0; i < n && c.has(4); ++i) lods.push_back(c.u32());
            }
            r.pos += size;
        }
        if (!core) { if (err) *err = "the mesh file has no shape in it"; return {}; }
    } else {
        if (err) *err = "mesh version " + ver + " isn't supported yet";
        return {};
    }
    firstLod(tris, lods);
    Shape s = build(verts, tris, true, err);
    if (!s.mesh && err) *err = "the mesh has no triangles";
    return s;
}

std::string assetId(const std::string& content) {
    std::string low = content;
    for (char& ch : low) ch = (char)std::tolower((unsigned char)ch);
    if (low.rfind("rbxasset://", 0) == 0) return {};   // a file built into Roblox, not on the asset server
    size_t at = std::string::npos;
    if (low.rfind("rbxassetid://", 0) == 0) at = 13;
    else if (size_t k = low.find("id="); k != std::string::npos && low.find("roblox.com") != std::string::npos) at = k + 3;
    else if (!low.empty() && std::all_of(low.begin(), low.end(), [](char ch) { return std::isdigit((unsigned char)ch); })) at = 0;
    if (at == std::string::npos) return {};
    std::string id;
    while (at < low.size() && std::isdigit((unsigned char)low[at])) id += low[at++];
    return id;
}

std::string fetch(const std::string& id, const char* ext) {
    // Asset IDs become part of a shell command below; accept decimal IDs only.
    // Restrict extensions too, so this public helper cannot be used for command injection.
    if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char ch) { return std::isdigit(ch); })) return {};
    if (!ext || (std::string(ext) != ".mesh" && std::string(ext) != ".png" &&
                 std::string(ext) != ".jpg" && std::string(ext) != ".jpeg" &&
                 std::string(ext) != ".webp")) return {};
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::string rel = std::string("roblox/") + id + ext;
    fs::path file = Paths::gamesFolder() / "roblox" / (id + ext);
    if (fs::exists(file, ec) && fs::file_size(file, ec) > 0) return rel;
#if defined(GB_IOS)
    // iOS cannot launch shell commands. Cached meshes remain usable; uncached
    // downloads require a native HTTP client, so report a cache miss.
    return {};
#else
    fs::create_directories(file.parent_path(), ec);
    fs::path part = file;
    part += ".part";
    // The public asset server sends a redirect to its file store; curl follows it.
    std::string cmd = "curl -fsSL --compressed -m 30 -o \"" + part.string() +
                      "\" \"https://assetdelivery.roblox.com/v1/asset/?id=" + id + "\"";
#ifdef _WIN32
    cmd += " 2>NUL";
#else
    cmd += " 2>/dev/null";
#endif
    int rc = std::system(cmd.c_str());
    if (rc != 0 || !fs::exists(part, ec) || fs::file_size(part, ec) == 0) { fs::remove(part, ec); return {}; }
    fs::rename(part, file, ec);
    return ec ? std::string() : rel;
#endif
}

Shape loadAsset(const std::string& content, std::string* err) {
    const std::string id = assetId(content);
    if (id.empty()) { if (err) *err = "not a downloadable Roblox asset ID"; return {}; }
    const std::string rel = fetch(id, ".mesh");
    if (rel.empty()) { if (err) *err = "could not download or find the Roblox mesh asset in the local cache"; return {}; }
    const std::filesystem::path path = Paths::gamesFolder() / rel;
    std::ifstream file(path, std::ios::binary);
    if (!file) { if (err) *err = "could not open the cached Roblox mesh asset"; return {}; }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.empty()) { if (err) *err = "the downloaded Roblox mesh asset is empty"; return {}; }
    return parse(bytes, err);
}

} // namespace RobloxMesh
