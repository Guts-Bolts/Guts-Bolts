#include "ModelImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include <stb_image.h>
#include <ufbx.h>
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <meshoptimizer.h>
#include <draco/compression/decode.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <webp/decode.h>

namespace fs = std::filesystem;

namespace ModelImport {

namespace {

constexpr size_t kMaxFaces = 1500000;   // more than this and Studio would crawl

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string extOf(const std::string& path) { return lower(fs::path(path).extension().string()); }

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(fs::path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// The average colour of a picture (textures: a part has one colour, so a
// textured model gets the colour its texture mostly is).
bool averageColor(const unsigned char* data, size_t size, glm::vec3& out) {
    if (!data || size == 0) return false;
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
    bool webp = false;
    if (!px && (px = WebPDecodeRGBA(data, size, &w, &h))) webp = true;   // .webp textures (Sketchfab uses them a lot)
    if (!px) return false;
    glm::dvec3 sum(0.0);
    double count = 0.0;
    const int step = std::max(1, (int)std::sqrt((double)w * h / 65536.0));   // about 64k samples at most
    for (int y = 0; y < h; y += step)
        for (int x = 0; x < w; x += step) {
            const unsigned char* p = px + ((size_t)y * w + x) * 4;
            if (p[3] < 16) continue;   // see-through bits don't count
            // Average in linear light, so a red and green checker doesn't come out muddy.
            for (int c = 0; c < 3; ++c) sum[c] += std::pow(p[c] / 255.0, 2.2);
            count += 1.0;
        }
    if (webp) WebPFree(px); else stbi_image_free(px);
    if (count <= 0.0) return false;
    for (int c = 0; c < 3; ++c) out[c] = (float)std::pow(sum[c] / count, 1.0 / 2.2);
    return true;
}

bool averageColorFile(const std::string& path, glm::vec3& out) {
    std::string bytes;
    if (path.empty() || !readFile(path, bytes)) return false;
    return averageColor((const unsigned char*)bytes.data(), bytes.size(), out);
}

// One piece while it's being read: corners in world space (studs), faces made of corners.
struct Builder {
    std::string                        name;
    glm::vec3                          color{0.65f, 0.65f, 0.80f};
    float                              transparency = 0.0f;
    std::vector<glm::vec3>             pos;
    std::vector<glm::vec3>             nrm;       // one per corner, or empty (no normals in the file)
    std::vector<std::vector<uint32_t>> faces;
};

struct Key {
    int64_t a, b, c, d;
    bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c && d == o.d; }
};
struct KeyHash {
    size_t operator()(const Key& k) const {
        uint64_t h = 1469598103934665603ull;
        for (int64_t v : {k.a, k.b, k.c, k.d}) { h ^= (uint64_t)v; h *= 1099511628211ull; }
        return (size_t)h;
    }
};

glm::vec3 newell(const std::vector<glm::vec3>& p, const std::vector<uint32_t>& f) {
    glm::vec3 n(0.0f);
    for (size_t i = 0; i < f.size(); ++i) {
        const glm::vec3& a = p[f[i]];
        const glm::vec3& b = p[f[(i + 1) % f.size()]];
        n += glm::vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    }
    return n;
}

// Corners -> a proper mesh. Corners in the same spot are joined so the shape is
// shaded smoothly, except where the file says the surface has a hard edge (the
// normals differ there). Files with no normals get hard edges wherever faces
// meet at more than 40 degrees (like Blender's Auto Smooth).
std::shared_ptr<EditMesh> build(const Builder& b) {
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const auto& p : b.pos) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    const float q = std::max(glm::length(hi - lo), 1e-6f) * 1e-5f;
    const bool hasN = b.nrm.size() == b.pos.size();

    auto mesh = std::make_shared<EditMesh>();
    mesh->smooth = true;
    std::unordered_map<Key, uint32_t, KeyHash> joined;
    std::vector<uint32_t> remap(b.pos.size());
    for (size_t i = 0; i < b.pos.size(); ++i) {
        const glm::vec3& p = b.pos[i];
        Key k{(int64_t)std::llround(p.x / q), (int64_t)std::llround(p.y / q), (int64_t)std::llround(p.z / q), 0};
        if (hasN) {
            glm::vec3 n = b.nrm[i];
            float l = glm::length(n);
            if (l > 1e-8f) n /= l;
            k.d = (std::llround(n.x * 40.0f) + 64) | ((std::llround(n.y * 40.0f) + 64) << 8) | ((std::llround(n.z * 40.0f) + 64) << 16);
        }
        auto [it, fresh] = joined.try_emplace(k, (uint32_t)mesh->verts.size());
        if (fresh) mesh->verts.push_back(p);
        remap[i] = it->second;
    }
    for (const auto& f : b.faces) {
        std::vector<uint32_t> nf;
        for (uint32_t c : f) {
            uint32_t v = remap[c];
            if (nf.empty() || nf.back() != v) nf.push_back(v);   // two corners joined into one: drop the copy
        }
        while (nf.size() > 1 && nf.back() == nf.front()) nf.pop_back();
        if (nf.size() >= 3 && glm::length(newell(mesh->verts, nf)) > 1e-12f) mesh->faces.push_back(std::move(nf));
    }

    if (!hasN && !mesh->faces.empty()) {
        // Auto Smooth: around each point, faces that face nearly the same way share
        // a copy of it; a face turned more than 40 degrees gets its own copy.
        const float cosLimit = std::cos(glm::radians(40.0f));
        std::vector<glm::vec3> fn(mesh->faces.size());
        std::vector<std::vector<std::pair<uint32_t, uint32_t>>> around(mesh->verts.size());   // (face, corner)
        for (uint32_t fi = 0; fi < mesh->faces.size(); ++fi) {
            fn[fi] = glm::normalize(newell(mesh->verts, mesh->faces[fi]));
            for (uint32_t c = 0; c < mesh->faces[fi].size(); ++c) around[mesh->faces[fi][c]].push_back({fi, c});
        }
        const size_t original = mesh->verts.size();
        for (size_t v = 0; v < original; ++v) {
            std::vector<std::pair<glm::vec3, uint32_t>> groups;   // (first face's direction, vertex)
            for (auto [fi, c] : around[v]) {
                uint32_t use = UINT32_MAX;
                for (auto& [dir, vi] : groups) if (glm::dot(dir, fn[fi]) >= cosLimit) { use = vi; break; }
                if (use == UINT32_MAX) {
                    use = groups.empty() ? (uint32_t)v : (uint32_t)mesh->verts.size();
                    if (!groups.empty()) mesh->verts.push_back(mesh->verts[v]);
                    groups.push_back({fn[fi], use});
                }
                mesh->faces[fi][c] = use;
            }
        }
    }
    return mesh->faces.empty() ? nullptr : mesh;
}

// ---------------------------------------------------------------------------
// FBX and OBJ (ufbx reads both, with their materials)
// ---------------------------------------------------------------------------

glm::vec3 materialColor(const ufbx_material* m, const std::string& folder, float& transparency) {
    glm::vec3 c(0.65f, 0.65f, 0.80f);
    if (!m) return c;
    const ufbx_material_map* base = m->pbr.base_color.has_value ? &m->pbr.base_color : &m->fbx.diffuse_color;
    if (base->has_value) c = {(float)base->value_vec4.x, (float)base->value_vec4.y, (float)base->value_vec4.z};
    if (m->pbr.base_factor.has_value && base == &m->pbr.base_color) c *= (float)m->pbr.base_factor.value_real;
    const ufbx_texture* tex = m->pbr.base_color.texture ? m->pbr.base_color.texture : m->fbx.diffuse_color.texture;
    if (tex) {
        glm::vec3 t;
        bool got = tex->content.size > 0 && averageColor((const unsigned char*)tex->content.data, tex->content.size, t);
        if (!got && tex->absolute_filename.length) got = averageColorFile(std::string(tex->absolute_filename.data, tex->absolute_filename.length), t);
        if (!got && tex->filename.length) {
            fs::path f(std::string(tex->filename.data, tex->filename.length));
            got = averageColorFile((fs::path(folder) / f).string(), t) ||
                  averageColorFile((fs::path(folder) / f.filename()).string(), t);
        }
        if (got) c = base->has_value && !(c.x > 0.99f && c.y > 0.99f && c.z > 0.99f) ? c * t : t;
    }
    if (m->pbr.opacity.has_value) transparency = 1.0f - (float)m->pbr.opacity.value_real;
    else if (m->fbx.transparency_factor.has_value) transparency = (float)m->fbx.transparency_factor.value_real;
    transparency = std::clamp(transparency, 0.0f, 1.0f);
    if (transparency > 0.98f) transparency = 0.0f;   // exporters often write "fully clear" by mistake
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

bool loadUfbx(const std::string& path, std::vector<Builder>& out, std::string& err, std::vector<std::string>& notes) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    opts.ignore_missing_external_files = true;
    opts.ignore_animation = true;
    ufbx_error error;
    ufbx_scene* scene = ufbx_load_file(path.c_str(), &opts, &error);
    if (!scene) {
        err = std::string("couldn't read it (") + std::string(error.description.data, error.description.length) + ")";
        return false;
    }
    const std::string folder = fs::path(path).parent_path().string();
    bool skinned = false;
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh || mesh->num_faces == 0) continue;
        if (mesh->skin_deformers.count) skinned = true;
        const ufbx_matrix& toWorld = node->geometry_to_world;
        const ufbx_matrix normalM = ufbx_matrix_for_normals(&toWorld);
        const bool mirrored = ufbx_matrix_determinant(&toWorld) < 0.0;   // a negative scale turns faces inside out
        const size_t nMats = std::max<size_t>(1, std::max(node->materials.count, mesh->materials.count));
        std::vector<int> slot(nMats, -1);
        std::string baseName = node->name.length ? std::string(node->name.data, node->name.length) : "Mesh";
        for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
            const ufbx_face face = mesh->faces.data[fi];
            if (face.num_indices < 3) continue;
            size_t mi = mesh->face_material.count ? mesh->face_material.data[fi] : 0;
            if (mi >= nMats) mi = 0;
            if (slot[mi] < 0) {
                const ufbx_material* mat = mi < node->materials.count ? node->materials.data[mi]
                                         : mi < mesh->materials.count ? mesh->materials.data[mi] : nullptr;
                Builder b;
                b.name = baseName;
                if (nMats > 1 && mat && mat->name.length) b.name += " (" + std::string(mat->name.data, mat->name.length) + ")";
                b.color = materialColor(mat, folder, b.transparency);
                slot[mi] = (int)out.size();
                out.push_back(std::move(b));
            }
            Builder& b = out[(size_t)slot[mi]];
            std::vector<uint32_t> f;
            for (uint32_t k = 0; k < face.num_indices; ++k) {
                const size_t ix = face.index_begin + k;
                ufbx_vec3 p = ufbx_transform_position(&toWorld, ufbx_get_vertex_vec3(&mesh->vertex_position, ix));
                glm::vec3 n(0.0f);
                if (mesh->vertex_normal.exists) {
                    ufbx_vec3 nn = ufbx_transform_direction(&normalM, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix));
                    n = {(float)nn.x, (float)nn.y, (float)nn.z};
                }
                f.push_back((uint32_t)b.pos.size());
                b.pos.push_back({(float)p.x, (float)p.y, (float)p.z});
                b.nrm.push_back(n);
            }
            if (mirrored) std::reverse(f.begin(), f.end());
            b.faces.push_back(std::move(f));
        }
    }
    if (skinned) notes.push_back("It has a skeleton: it comes in standing in its rest pose, without the bones.");
    ufbx_free_scene(scene);
    return true;
}

// ---------------------------------------------------------------------------
// glTF / GLB
// ---------------------------------------------------------------------------

// EXT_meshopt_compression (gltfpack, gltf-transform): the squashed bytes are
// unpacked into each buffer view's own copy, which cgltf then reads from.
bool unpackMeshopt(cgltf_data* data) {
    for (cgltf_size i = 0; i < data->buffer_views_count; ++i) {
        cgltf_buffer_view& view = data->buffer_views[i];
        if (!view.has_meshopt_compression || view.data) continue;
        const cgltf_meshopt_compression& mc = view.meshopt_compression;
        if (!mc.buffer || !mc.buffer->data || mc.offset + mc.size > mc.buffer->size) return false;
        const unsigned char* src = (const unsigned char*)mc.buffer->data + mc.offset;
        void* dst = std::malloc(mc.count * mc.stride);   // cgltf_free() frees it
        if (!dst) return false;
        int rc = -1;
        switch (mc.mode) {
        case cgltf_meshopt_compression_mode_attributes: rc = meshopt_decodeVertexBuffer(dst, mc.count, mc.stride, src, mc.size); break;
        case cgltf_meshopt_compression_mode_triangles:  rc = meshopt_decodeIndexBuffer(dst, mc.count, mc.stride, src, mc.size); break;
        case cgltf_meshopt_compression_mode_indices:    rc = meshopt_decodeIndexSequence(dst, mc.count, mc.stride, src, mc.size); break;
        default: break;
        }
        if (rc != 0) { std::free(dst); return false; }
        switch (mc.filter) {
        case cgltf_meshopt_compression_filter_octahedral:  meshopt_decodeFilterOct(dst, mc.count, mc.stride); break;
        case cgltf_meshopt_compression_filter_quaternion:  meshopt_decodeFilterQuat(dst, mc.count, mc.stride); break;
        case cgltf_meshopt_compression_filter_exponential: meshopt_decodeFilterExp(dst, mc.count, mc.stride); break;
        default: break;
        }
        view.data = dst;
    }
    return true;
}

// KHR_draco_mesh_compression (Sketchfab, Blender's "Compression" tick box): the
// points, normals and triangles of one primitive, unpacked by Google's Draco.
bool readDraco(const cgltf_data* data, const cgltf_primitive& prim, std::vector<glm::vec3>& P,
               std::vector<glm::vec3>& N, std::vector<uint32_t>& idx) {
    const cgltf_draco_mesh_compression& dc = prim.draco_mesh_compression;
    if (!dc.buffer_view) return false;
    const uint8_t* bytes = cgltf_buffer_view_data(dc.buffer_view);
    if (!bytes) return false;
    draco::DecoderBuffer buf;
    buf.Init((const char*)bytes, dc.buffer_view->size);
    draco::Decoder decoder;
    auto got = decoder.DecodeMeshFromBuffer(&buf);
    if (!got.ok() || !got.value()) return false;
    const draco::Mesh& mesh = *got.value();
    // The file says which Draco attribute is which: cgltf keeps that number as
    // an index into the accessor list.
    const draco::PointAttribute* posA = nullptr;
    const draco::PointAttribute* nrmA = nullptr;
    for (cgltf_size i = 0; i < dc.attributes_count; ++i) {
        const int id = (int)(dc.attributes[i].data - data->accessors);
        if (dc.attributes[i].type == cgltf_attribute_type_position) posA = mesh.GetAttributeByUniqueId(id);
        if (dc.attributes[i].type == cgltf_attribute_type_normal) nrmA = mesh.GetAttributeByUniqueId(id);
    }
    if (!posA) return false;
    for (draco::PointIndex i(0); i < mesh.num_points(); ++i) {
        float v[3] = {0, 0, 0};
        posA->ConvertValue<float, 3>(posA->mapped_index(i), v);
        P.push_back({v[0], v[1], v[2]});
        if (nrmA) {
            float n[3] = {0, 0, 0};
            nrmA->ConvertValue<float, 3>(nrmA->mapped_index(i), n);
            N.push_back({n[0], n[1], n[2]});
        }
    }
    for (draco::FaceIndex f(0); f < mesh.num_faces(); ++f) {
        const draco::Mesh::Face& face = mesh.face(f);
        for (int k = 0; k < 3; ++k) idx.push_back(face[k].value());
    }
    return true;
}

bool loadGltf(const std::string& path, std::vector<Builder>& out, std::string& err, std::vector<std::string>& notes) {
    cgltf_options opts = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opts, path.c_str(), &data) != cgltf_result_success) { err = "that isn't a glTF file we can read"; return false; }
    if (cgltf_load_buffers(&opts, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        err = "its data (.bin) file is missing: keep it next to the .gltf";
        return false;
    }
    if (!unpackMeshopt(data)) {
        cgltf_free(data);
        err = "its squashed (meshopt) data is broken";
        return false;
    }
    const std::string folder = fs::path(path).parent_path().string();
    std::map<const cgltf_material*, std::pair<glm::vec3, float>> matColor;
    auto colorOf = [&](const cgltf_material* m) {
        if (!m) return std::make_pair(glm::vec3(0.65f, 0.65f, 0.80f), 0.0f);
        if (auto it = matColor.find(m); it != matColor.end()) return it->second;
        glm::vec3 c(1.0f);
        float alpha = 1.0f;
        if (m->has_pbr_metallic_roughness) {
            const float* f = m->pbr_metallic_roughness.base_color_factor;
            c = {f[0], f[1], f[2]};
            alpha = f[3];
            const cgltf_texture* t = m->pbr_metallic_roughness.base_color_texture.texture;
            const cgltf_image* img = t ? (t->image ? t->image : t->webp_image) : nullptr;
            if (img) {
                glm::vec3 tc;
                bool got = false;
                if (img->buffer_view)
                    got = averageColor(cgltf_buffer_view_data(img->buffer_view), img->buffer_view->size, tc);
                else if (img->uri && std::strncmp(img->uri, "data:", 5) != 0) {
                    std::string uri = img->uri;
                    uri.resize(cgltf_decode_uri(uri.data()));
                    got = averageColorFile((fs::path(folder) / uri).string(), tc);
                }
                if (got) c *= tc;
            }
        }
        auto r = std::make_pair(glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f)),
                                m->alpha_mode == cgltf_alpha_mode_blend ? std::clamp(1.0f - alpha, 0.0f, 0.95f) : 0.0f);
        matColor[m] = r;
        return r;
    };

    bool skipped = false, brokenDraco = false;
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float mf[16];
        cgltf_node_transform_world(node, mf);
        glm::mat4 world;
        std::memcpy(&world[0][0], mf, sizeof(mf));
        glm::mat3 normalM = glm::transpose(glm::inverse(glm::mat3(world)));
        const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0f;
        const cgltf_mesh* mesh = node->mesh;
        std::string baseName = node->name ? node->name : (mesh->name ? mesh->name : "Mesh");
        for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles && prim.type != cgltf_primitive_type_triangle_strip &&
                prim.type != cgltf_primitive_type_triangle_fan) { skipped = true; continue; }
            std::vector<glm::vec3> P, N;
            std::vector<uint32_t> idx;
            if (prim.has_draco_mesh_compression) {
                if (!readDraco(data, prim, P, N, idx)) { brokenDraco = true; continue; }
            } else {
                const cgltf_accessor* posA = nullptr;
                const cgltf_accessor* nrmA = nullptr;
                for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                    if (prim.attributes[ai].type == cgltf_attribute_type_position) posA = prim.attributes[ai].data;
                    if (prim.attributes[ai].type == cgltf_attribute_type_normal) nrmA = prim.attributes[ai].data;
                }
                if (!posA) continue;
                for (cgltf_size i = 0; i < posA->count; ++i) {
                    float p[3] = {0, 0, 0}, n[3] = {0, 0, 0};
                    cgltf_accessor_read_float(posA, i, p, 3);
                    P.push_back({p[0], p[1], p[2]});
                    if (nrmA) {
                        cgltf_accessor_read_float(nrmA, i, n, 3);
                        N.push_back({n[0], n[1], n[2]});
                    }
                }
                const cgltf_size count = prim.indices ? prim.indices->count : posA->count;
                for (cgltf_size i = 0; i < count; ++i)
                    idx.push_back(prim.indices ? (uint32_t)cgltf_accessor_read_index(prim.indices, i) : (uint32_t)i);
            }
            Builder b;
            b.name = baseName;
            if (mesh->primitives_count > 1)
                b.name += prim.material && prim.material->name ? " (" + std::string(prim.material->name) + ")" : " " + std::to_string(pi + 1);
            auto [col, tr] = colorOf(prim.material);
            b.color = col;
            b.transparency = tr;
            for (const glm::vec3& p : P) b.pos.push_back(glm::vec3(world * glm::vec4(p, 1.0f)));
            if (N.size() == P.size()) for (const glm::vec3& n : N) b.nrm.push_back(normalM * n);
            auto tri = [&](uint32_t a, uint32_t c1, uint32_t c2) {
                if (a >= b.pos.size() || c1 >= b.pos.size() || c2 >= b.pos.size()) return;
                if (mirrored) std::swap(c1, c2);
                b.faces.push_back({a, c1, c2});
            };
            if (prim.type == cgltf_primitive_type_triangles)
                for (size_t i = 0; i + 2 < idx.size(); i += 3) tri(idx[i], idx[i + 1], idx[i + 2]);
            else if (prim.type == cgltf_primitive_type_triangle_strip)
                for (size_t i = 0; i + 2 < idx.size(); ++i)
                    i % 2 ? tri(idx[i + 1], idx[i], idx[i + 2]) : tri(idx[i], idx[i + 1], idx[i + 2]);
            else
                for (size_t i = 1; i + 1 < idx.size(); ++i) tri(idx[0], idx[i], idx[i + 1]);
            out.push_back(std::move(b));
        }
    }
    if (skipped) notes.push_back("Lines and points in the file were skipped (only surfaces come in).");
    if (brokenDraco) notes.push_back("Some squashed (Draco) pieces couldn't be unpacked and were left out.");
    cgltf_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// STL (3D printing): usually millimetres and Z pointing up
// ---------------------------------------------------------------------------

bool loadStl(const std::string& path, std::vector<Builder>& out, std::string& err) {
    std::string bytes;
    if (!readFile(path, bytes)) { err = "couldn't open it"; return false; }
    Builder b;
    b.name = fs::path(path).stem().string();
    auto addTri = [&](glm::vec3 a, glm::vec3 c1, glm::vec3 c2) {
        auto up = [](glm::vec3 p) { return glm::vec3(p.x, p.z, -p.y); };   // Z-up -> Y-up
        uint32_t base = (uint32_t)b.pos.size();
        b.pos.insert(b.pos.end(), {up(a), up(c1), up(c2)});
        b.faces.push_back({base, base + 1, base + 2});
    };
    uint32_t triCount = 0;
    if (bytes.size() >= 84) std::memcpy(&triCount, bytes.data() + 80, 4);
    const bool binary = bytes.size() >= 84 && (size_t)84 + (size_t)triCount * 50 == bytes.size();
    if (binary) {
        for (uint32_t i = 0; i < triCount; ++i) {
            float v[12];
            std::memcpy(v, bytes.data() + 84 + (size_t)i * 50, sizeof(v));
            addTri({v[3], v[4], v[5]}, {v[6], v[7], v[8]}, {v[9], v[10], v[11]});
        }
    } else {
        std::istringstream in(bytes);
        std::string word;
        std::vector<glm::vec3> corners;
        while (in >> word) {
            if (word == "vertex") {
                glm::vec3 p;
                in >> p.x >> p.y >> p.z;
                corners.push_back(p);
            } else if (word == "endfacet") {
                for (size_t i = 1; i + 1 < corners.size(); ++i) addTri(corners[0], corners[i], corners[i + 1]);
                corners.clear();
            }
        }
    }
    if (b.faces.empty()) { err = "there are no triangles in it"; return false; }
    out.push_back(std::move(b));
    return true;
}

// ---------------------------------------------------------------------------
// PLY (scans and science tools): text or binary, with vertex colours
// ---------------------------------------------------------------------------

bool loadPly(const std::string& path, std::vector<Builder>& out, std::string& err) {
    std::string bytes;
    if (!readFile(path, bytes)) { err = "couldn't open it"; return false; }
    size_t headerEnd = bytes.find("end_header");
    if (bytes.compare(0, 3, "ply") != 0 || headerEnd == std::string::npos) { err = "that isn't a PLY file"; return false; }
    size_t dataStart = bytes.find('\n', headerEnd);
    if (dataStart == std::string::npos) { err = "the file is cut short"; return false; }
    ++dataStart;

    struct Prop { std::string name, type, countType; bool list = false; };
    struct Element { std::string name; size_t count = 0; std::vector<Prop> props; };
    std::vector<Element> elements;
    std::string format;
    {
        std::istringstream hin(bytes.substr(0, headerEnd));
        std::string line;
        while (std::getline(hin, line)) {
            std::istringstream ls(line);
            std::string w;
            ls >> w;
            if (w == "format") ls >> format;
            else if (w == "element") { Element e; ls >> e.name >> e.count; elements.push_back(e); }
            else if (w == "property" && !elements.empty()) {
                Prop p;
                ls >> p.type;
                if (p.type == "list") { p.list = true; ls >> p.countType >> p.type; }
                ls >> p.name;
                elements.back().props.push_back(p);
            }
        }
    }
    if (format != "ascii" && format != "binary_little_endian") { err = "only text and little-endian PLY files are supported"; return false; }
    const bool ascii = format == "ascii";

    size_t at = dataStart;
    std::istringstream tin(ascii ? bytes.substr(dataStart) : std::string());
    auto sizeOf = [](const std::string& t) -> size_t {
        if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
        if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
        if (t == "double" || t == "float64") return 8;
        return 4;
    };
    bool bad = false;
    auto read = [&](const std::string& t) -> double {
        if (ascii) { double v = 0; tin >> v; return v; }
        size_t n = sizeOf(t);
        if (at + n > bytes.size()) { bad = true; return 0; }
        const char* p = bytes.data() + at;
        at += n;
        if (t == "char" || t == "int8") return (double)*(const int8_t*)p;
        if (t == "uchar" || t == "uint8") return (double)*(const uint8_t*)p;
        int16_t s; uint16_t us; int32_t i; uint32_t u; float f; double d;
        if (t == "short" || t == "int16") { std::memcpy(&s, p, 2); return s; }
        if (t == "ushort" || t == "uint16") { std::memcpy(&us, p, 2); return us; }
        if (t == "int" || t == "int32") { std::memcpy(&i, p, 4); return i; }
        if (t == "uint" || t == "uint32") { std::memcpy(&u, p, 4); return u; }
        if (t == "double" || t == "float64") { std::memcpy(&d, p, 8); return d; }
        std::memcpy(&f, p, 4); return f;
    };

    Builder b;
    b.name = fs::path(path).stem().string();
    glm::dvec3 colorSum(0.0);
    size_t colored = 0;
    for (const Element& e : elements) {
        for (size_t i = 0; i < e.count && !bad; ++i) {
            glm::vec3 p(0.0f), c(-1.0f);
            std::vector<uint32_t> face;
            for (const Prop& pr : e.props) {
                if (pr.list) {
                    size_t n = (size_t)read(pr.countType);
                    for (size_t k = 0; k < n && !bad; ++k) face.push_back((uint32_t)read(pr.type));
                    continue;
                }
                double v = read(pr.type);
                if (pr.name == "x") p.x = (float)v;
                else if (pr.name == "y") p.y = (float)v;
                else if (pr.name == "z") p.z = (float)v;
                else if (pr.name == "red") c.x = (float)v;
                else if (pr.name == "green") c.y = (float)v;
                else if (pr.name == "blue") c.z = (float)v;
            }
            if (e.name == "vertex") {
                b.pos.push_back(p);
                if (c.x >= 0 && c.y >= 0 && c.z >= 0) {
                    float scale = std::max({c.x, c.y, c.z}) > 1.0f ? 1.0f / 255.0f : 1.0f;
                    colorSum += glm::dvec3(c * scale);
                    ++colored;
                }
            } else if (e.name == "face" && face.size() >= 3) {
                bool ok = true;
                for (uint32_t v : face) ok = ok && v < b.pos.size();
                if (ok) b.faces.push_back(std::move(face));
            }
        }
    }
    if (bad) { err = "the file is cut short"; return false; }
    if (b.faces.empty()) { err = "it has points but no surfaces (a point cloud) - only surfaces can be parts"; return false; }
    if (colored) b.color = glm::vec3(colorSum / (double)colored);
    out.push_back(std::move(b));
    return true;
}

// ---------------------------------------------------------------------------
// Everything else (Collada .dae, 3ds Max .3ds, DirectX .x, 3MF, LightWave, ...): Assimp
// ---------------------------------------------------------------------------

// The endings Assimp reads for us (the readers turned on in CMakeLists.txt).
const char* const kAssimpExts[] = {
    ".dae", ".3ds", ".x", ".3mf", ".lwo", ".lws", ".lxo", ".ac", ".ac3d", ".acc", ".off", ".dxf", ".b3d", ".ms3d",
    ".md2", ".md3", ".md5mesh", ".ase", ".ask", ".smd", ".vta", ".x3d", ".x3db", ".amf", ".mesh.xml", ".ogex",
    ".irrmesh", ".irr", ".cob", ".scn", ".q3o", ".q3s", ".nff", ".enff", ".raw", ".ter", ".xgl", ".zgl", ".ndo",
    ".hmp", ".mdl", ".pmx", ".sib"};

// "thing.mesh.xml" has two dots: check the whole end of the name, not just the last part.
bool isAssimpFile(const std::string& path) {
    const std::string name = lower(fs::path(path).filename().string());
    for (const char* e : kAssimpExts) {
        const size_t n = std::strlen(e);
        if (name.size() > n && name.compare(name.size() - n, n, e) == 0) return true;
    }
    return false;
}

glm::vec3 assimpColor(const aiScene* scene, const aiMaterial* m, const std::string& folder, float& transparency) {
    glm::vec3 c(0.65f, 0.65f, 0.80f);
    if (!m) return c;
    aiColor4D col;
    if (aiGetMaterialColor(m, AI_MATKEY_BASE_COLOR, &col) == AI_SUCCESS ||
        aiGetMaterialColor(m, AI_MATKEY_COLOR_DIFFUSE, &col) == AI_SUCCESS)
        c = {col.r, col.g, col.b};
    aiString tex;
    if (m->GetTexture(aiTextureType_BASE_COLOR, 0, &tex) == AI_SUCCESS || m->GetTexture(aiTextureType_DIFFUSE, 0, &tex) == AI_SUCCESS) {
        glm::vec3 t;
        bool got = false;
        if (const aiTexture* em = scene->GetEmbeddedTexture(tex.C_Str())) {
            if (em->mHeight == 0) got = averageColor((const unsigned char*)em->pcData, em->mWidth, t);
            else if (em->mWidth && em->mHeight) {   // plain pixels
                glm::dvec3 sum(0.0);
                const size_t n = (size_t)em->mWidth * em->mHeight;
                for (size_t i = 0; i < n; ++i) sum += glm::dvec3(em->pcData[i].r, em->pcData[i].g, em->pcData[i].b);
                t = glm::vec3(sum / (255.0 * (double)n));
                got = true;
            }
        } else {
            fs::path f(tex.C_Str());
            got = averageColorFile((fs::path(folder) / f).string(), t) ||
                  averageColorFile((fs::path(folder) / f.filename()).string(), t) ||
                  averageColorFile(f.string(), t);
        }
        if (got) c = c.x > 0.99f && c.y > 0.99f && c.z > 0.99f ? t : c * t;
    }
    float opacity = 1.0f;
    if (aiGetMaterialFloat(m, AI_MATKEY_OPACITY, &opacity) == AI_SUCCESS) transparency = 1.0f - opacity;
    transparency = std::clamp(transparency, 0.0f, 1.0f);
    if (transparency > 0.98f) transparency = 0.0f;   // same exporter mistake as FBX
    return glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
}

void addAssimpNode(const aiScene* scene, const aiNode* node, const aiMatrix4x4& parent, const std::string& folder,
                   std::vector<Builder>& out, bool& skipped) {
    const aiMatrix4x4 toWorld = parent * node->mTransformation;
    glm::mat4 world;
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) world[c][r] = toWorld[r][c];   // Assimp's rows are glm's columns
    const glm::mat3 normalM = glm::transpose(glm::inverse(glm::mat3(world)));
    const bool mirrored = glm::determinant(glm::mat3(world)) < 0.0f;
    for (unsigned mi = 0; mi < node->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[mi]];
        if (!(mesh->mPrimitiveTypes & aiPrimitiveType_POLYGON) && !(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE)) {
            skipped = true;
            continue;
        }
        Builder b;
        b.name = node->mName.length ? node->mName.C_Str() : (mesh->mName.length ? mesh->mName.C_Str() : "Mesh");
        if (node->mNumMeshes > 1) {
            const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
            aiString mname;
            b.name += mat && mat->Get(AI_MATKEY_NAME, mname) == AI_SUCCESS && mname.length ? " (" + std::string(mname.C_Str()) + ")"
                                                                                        : " " + std::to_string(mi + 1);
        }
        b.color = assimpColor(scene, scene->mMaterials[mesh->mMaterialIndex], folder, b.transparency);
        for (unsigned v = 0; v < mesh->mNumVertices; ++v) {
            const aiVector3D& p = mesh->mVertices[v];
            b.pos.push_back(glm::vec3(world * glm::vec4(p.x, p.y, p.z, 1.0f)));
            if (mesh->mNormals) b.nrm.push_back(normalM * glm::vec3(mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z));
        }
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices < 3) { skipped = true; continue; }
            std::vector<uint32_t> nf(face.mIndices, face.mIndices + face.mNumIndices);
            bool ok = true;
            for (uint32_t i : nf) ok = ok && i < mesh->mNumVertices;
            if (!ok) continue;
            if (mirrored) std::reverse(nf.begin(), nf.end());
            b.faces.push_back(std::move(nf));
        }
        if (!b.faces.empty()) out.push_back(std::move(b));
    }
    for (unsigned i = 0; i < node->mNumChildren; ++i) addAssimpNode(scene, node->mChildren[i], toWorld, folder, out, skipped);
}

bool loadAssimp(const std::string& path, std::vector<Builder>& out, std::string& err, std::vector<std::string>& notes) {
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_NO_SKELETON_MESHES, true);   // no bone sticks
    const aiScene* scene = importer.ReadFile(path, aiProcess_SortByPType | aiProcess_FindInvalidData | aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) {
        err = std::string("couldn't read it (") + importer.GetErrorString() + ")";
        return false;
    }
    // 3D printing formats stand on Z, like STL: turn them so Y is up.
    aiMatrix4x4 root;
    const std::string e = extOf(path);
    if (e == ".3mf" || e == ".amf") root = aiMatrix4x4(1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1);
    bool skipped = false;
    addAssimpNode(scene, scene->mRootNode, root, fs::path(path).parent_path().string(), out, skipped);
    bool skinned = false;
    for (unsigned i = 0; i < scene->mNumMeshes; ++i) skinned = skinned || scene->mMeshes[i]->HasBones();
    if (skinned) notes.push_back("It has a skeleton: it comes in standing in its rest pose, without the bones.");
    if (skipped) notes.push_back("Lines and points in the file were skipped (only surfaces come in).");
    return true;
}

// Files Studio can't open, with what to do instead.
std::string cantOpen(const std::string& e) {
    if (e == ".blend")
        return "Blender's own .blend files can't be opened directly. In Blender, use File > Export > glTF 2.0 (.glb) and import that";
    if (e == ".max" || e == ".ma" || e == ".mb" || e == ".c4d" || e == ".skp" || e == ".3dm" || e == ".hip" || e == ".ztl" || e == ".zpr")
        return "that's a program's own save file (3ds Max, Maya, Cinema 4D, SketchUp, Rhino, Houdini or ZBrush), which only that "
               "program can open. Export it from there as .fbx, .glb or .obj and import that";
    if (e == ".usd" || e == ".usda" || e == ".usdc" || e == ".usdz")
        return "USD files can't be read yet. Export as .glb or .fbx instead (Blender can turn a USD into a .glb)";
    if (e == ".step" || e == ".stp" || e == ".iges" || e == ".igs" || e == ".sldprt" || e == ".f3d" || e == ".ipt")
        return "CAD files (STEP, IGES, SolidWorks, Fusion, Inventor) describe exact curved shapes, not meshes. Export as .stl, .3mf or .obj from your CAD program and import that";
    if (e == ".abc" || e == ".vdb")
        return "Alembic and VDB files are for animation caches and smoke, not models. Export as .fbx or .glb instead";
    return std::string("Studio can't read ") + e + " models (it can read " + formatList() + ")";
}

} // namespace

bool isModelFile(const std::string& path) {
    const std::string e = extOf(path);
    return e == ".fbx" || e == ".obj" || e == ".gltf" || e == ".glb" || e == ".stl" || e == ".ply" || isAssimpFile(path);
}

// Not quite models we can read, but someone dropping one wants to hear how to get it in.
bool isOtherModelFile(const std::string& path) {
    static const char* const kOther[] = {".blend", ".max", ".ma", ".mb", ".c4d", ".skp", ".3dm", ".hip", ".ztl", ".zpr",
                                         ".usd", ".usda", ".usdc", ".usdz", ".step", ".stp", ".iges", ".igs", ".sldprt",
                                         ".f3d", ".ipt", ".abc", ".vdb"};
    const std::string e = extOf(path);
    for (const char* o : kOther) if (e == o) return true;
    return false;
}

const char* patterns() {
    return "*.fbx *.obj *.gltf *.glb *.stl *.ply *.dae *.3ds *.x *.3mf *.lwo *.lws *.lxo *.ac *.ac3d *.off *.dxf *.b3d "
           "*.ms3d *.md2 *.md3 *.md5mesh *.ase *.smd *.x3d *.amf *.ogex *.irrmesh *.irr *.cob *.q3o *.nff *.raw *.ter "
           "*.xgl *.zgl *.ndo *.hmp *.mdl *.pmx *.sib";
}
const char* formatList() {
    return ".fbx, .obj, .gltf, .glb (also Draco and meshopt squashed ones), .stl, .ply, .dae, .3ds, .x, .3mf, .lwo, "
           ".ac, .off, .dxf, .b3d, .ms3d, .md2, .md3, .md5mesh, .ase, .smd, .x3d, .amf, .ogex, .irrmesh, .cob, .xgl, "
           ".pmx and a few more";
}

bool load(const std::string& path, Result& out, std::string& err) {
    out = Result{};
    const std::string e = extOf(path);
    std::vector<Builder> raw;
    bool ok = false;
    if (e == ".fbx" || e == ".obj") ok = loadUfbx(path, raw, err, out.notes);
    else if (e == ".gltf" || e == ".glb") ok = loadGltf(path, raw, err, out.notes);
    else if (e == ".stl") ok = loadStl(path, raw, err);
    else if (e == ".ply") ok = loadPly(path, raw, err);
    else if (isAssimpFile(path)) ok = loadAssimp(path, raw, err, out.notes);
    else { err = cantOpen(e); return false; }
    if (!ok) return false;

    size_t faces = 0;
    for (const Builder& b : raw) faces += b.faces.size();
    if (faces == 0) { err = "there's nothing to see in it (no surfaces)"; return false; }
    if (faces > kMaxFaces) {
        err = "it's too detailed (" + std::to_string(faces) + " faces; the most is " + std::to_string(kMaxFaces) +
              "). Lower the detail first (Blender: the Decimate modifier)";
        return false;
    }

    // Size: keep the file's own size unless it's silly.
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const Builder& b : raw) for (const auto& p : b.pos) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
    const float biggest = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z});
    float scale = 1.0f;
    if (biggest > 400.0f || biggest < 0.2f) {
        scale = biggest > 1e-6f ? 10.0f / biggest : 1.0f;
        std::ostringstream s;
        s.precision(3);
        s << "It was " << (biggest > 400.0f ? "huge" : "tiny") << " (" << biggest << " units across), so it was "
          << (biggest > 400.0f ? "shrunk" : "grown") << " to 10 studs. Resize it with the Scale tool if you like.";
        out.notes.push_back(s.str());
    }
    // The model's bottom middle is its "feet": it lands standing on the spot you pick.
    const glm::vec3 feet((lo.x + hi.x) * 0.5f, lo.y, (lo.z + hi.z) * 0.5f);

    std::map<std::string, int> names;
    for (Builder& b : raw) {
        for (auto& p : b.pos) p = (p - feet) * scale;
        auto mesh = build(b);
        if (!mesh) continue;
        glm::vec3 mlo(1e30f), mhi(-1e30f);
        for (const auto& p : mesh->verts) { mlo = glm::min(mlo, p); mhi = glm::max(mhi, p); }
        Piece piece;
        piece.center = (mlo + mhi) * 0.5f;
        piece.size = mhi - mlo;
        glm::vec3 div = piece.size;
        for (int i = 0; i < 3; ++i) if (div[i] < 1e-4f) { div[i] = 1.0f; piece.size[i] = 0.05f; }   // flat: keep a thin slab
        for (auto& p : mesh->verts) p = (p - piece.center) / div;
        piece.name = b.name.empty() ? "Mesh" : b.name;
        if (int n = ++names[piece.name]; n > 1) piece.name += " " + std::to_string(n);
        piece.color = b.color;
        piece.transparency = b.transparency;
        out.faces += mesh->faces.size();
        piece.mesh = std::move(mesh);
        out.pieces.push_back(std::move(piece));
    }
    if (out.pieces.empty()) { err = "there's nothing to see in it (no surfaces)"; return false; }
    out.size = (hi - lo) * scale;
    return true;
}

} // namespace ModelImport
