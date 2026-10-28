#include "ModelExport.h"
#include "../scene/EditMesh.h"
#include "../scene/SceneNode.h"

#include <assimp/Exporter.hpp>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace fs = std::filesystem;

namespace ModelExport {

const std::vector<Format>& formats() {
    static const std::vector<Format> list = {
        {".rbxmx", "Roblox model",            "Everything you picked (scripts too). Drag it into Roblox Studio.", true, false},
        {".rbxlx", "Roblox place",            "The whole game (scripts, lights, settings). Open it in Roblox Studio.", true, true},
        {".obj",   "Wavefront OBJ",           "Shapes and colours. Opens almost anywhere (Blender, Maya, 3ds Max).", false, false},
        {".fbx",   "Autodesk FBX",            "Shapes, colours and how they're grouped. Unity, Unreal, Blender, Maya.", false, false},
        {".glb",   "glTF binary",             "One small file with everything. Web viewers, Godot, Blender, three.js.", false, false},
        {".gltf",  "glTF (text + .bin file)", "Same as .glb, as a text file plus a data file.", false, false},
        {".mesh",  "Roblox mesh",             "Roblox's own mesh file: one shape, colours baked in. For a MeshPart.", false, false},
        {".stl",   "STL for 3D printing",     "Just the shape, one piece. For 3D printers and CAD.", false, false},
        {".ply",   "PLY",                     "One piece, with the colours on its corners. For MeshLab and scanning tools.", false, false},
        {".dae",   "Collada",                 "Older, but SketchUp and many tools open it.", false, false},
    };
    return list;
}

const Format* formatFor(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    for (const Format& f : formats()) if (ext == f.ext) return &f;
    return nullptr;
}

namespace {

// Assimp's name for each writer.
const char* assimpId(const std::string& ext) {
    if (ext == ".obj")  return "obj";
    if (ext == ".fbx")  return "fbx";
    if (ext == ".glb")  return "glb2";
    if (ext == ".gltf") return "gltf2";
    if (ext == ".stl")  return "stlb";
    if (ext == ".ply")  return "plyb";
    if (ext == ".dae")  return "collada";
    return nullptr;
}

struct Corner { glm::vec3 p, n; glm::vec2 uv; };

// One part's shape, ready to write.
struct Shape {
    const SceneNode*      node = nullptr;
    glm::mat4             world{1.0f};   // where the part is: turned and moved, not stretched
    std::vector<Corner>   corners;       // in the part's own space, in studs
    std::vector<uint32_t> tris;
    int                   material = 0;
};

// A group in the file (a Model, a Folder, a part with parts inside...).
struct Group {
    const SceneNode*   node = nullptr;
    glm::mat4          world{1.0f};
    int                shape = -1;
    std::vector<Group> children;
};

struct Look {
    glm::vec3 color;
    float     transparency;
    Material  material;
    bool operator<(const Look& o) const {
        return std::tie(color.x, color.y, color.z, transparency, material) <
               std::tie(o.color.x, o.color.y, o.color.z, o.transparency, o.material);
    }
};

struct Collected {
    std::vector<Shape> shapes;
    std::vector<Look>  looks;
    std::vector<Group> groups;
};

bool hasShape(const SceneNode& n) {
    return n.kind == NodeKind::Part && n.primitiveType != PrimitiveType::None && n.visible && !n.negated &&
           n.transparency < 0.999f;
}

// A matrix without its stretch, plus the stretch on its own.
glm::mat4 unstretched(const glm::mat4& m, glm::vec3* stretch = nullptr) {
    glm::mat4 r = m;
    for (int i = 0; i < 3; ++i) {
        float len = glm::length(glm::vec3(m[i]));
        if (stretch) (*stretch)[i] = len;
        if (len > 1e-9f) r[i] = glm::vec4(glm::vec3(m[i]) / len, 0.0f);
    }
    return r;
}

// The part's faces as triangles, the way Studio draws them (the same normals and
// the same box-style texture coordinates when the mesh has none of its own).
Shape shapeOf(const SceneNode& n) {
    Shape s;
    s.node = &n;
    glm::vec3 size(1.0f);
    s.world = unstretched(n.worldMatrix(), &size);
    std::shared_ptr<EditMesh> keep = n.primitiveType == PrimitiveType::Mesh && n.editMesh ? n.editMesh
                                                                                          : MeshEdit::fromPrimitive(n.primitiveType);
    const EditMesh& m = *keep;
    const glm::vec3 inv(size.x > 1e-9f ? 1.0f / size.x : 0.0f, size.y > 1e-9f ? 1.0f / size.y : 0.0f,
                        size.z > 1e-9f ? 1.0f / size.z : 0.0f);
    auto normalOut = [&](glm::vec3 nrm) {   // a normal from the -0.5..0.5 box to the stretched part
        nrm *= inv;
        return glm::length(nrm) > 1e-12f ? glm::normalize(nrm) : glm::vec3(0, 1, 0);
    };
    std::vector<glm::vec3> smoothN;
    if (m.smooth) {
        smoothN.assign(m.verts.size(), glm::vec3(0.0f));
        for (const auto& f : m.faces)
            if (f.size() >= 3) for (uint32_t v : f) if (v < m.verts.size()) smoothN[v] += MeshEdit::faceNormal(m, f);
    }
    const bool ownUVs = m.uvs.size() == m.faces.size();
    const bool ownNormals = m.normals.size() == m.verts.size();
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& f = m.faces[fi];
        if (f.size() < 3) continue;
        if (std::any_of(f.begin(), f.end(), [&](uint32_t v) { return v >= m.verts.size(); })) continue;
        const glm::vec3 fn = MeshEdit::faceNormal(m, f);
        const glm::vec3 an = glm::abs(fn);
        const int ax = an.x > an.y && an.x > an.z ? 0 : (an.y > an.z ? 1 : 2);
        const int u = ax == 0 ? 2 : 0, v = ax == 1 ? 2 : 1;
        const bool faceUVs = ownUVs && m.uvs[fi].size() == f.size();
        const uint32_t base = (uint32_t)s.corners.size();
        for (size_t c = 0; c < f.size(); ++c) {
            const uint32_t i = f[c];
            glm::vec3 nrm = ownNormals && glm::length(m.normals[i]) > 1e-6f ? m.normals[i]
                          : m.smooth && glm::length(smoothN[i]) > 1e-12f ? smoothN[i] : fn;
            const glm::vec3& p = m.verts[i];
            s.corners.push_back({p * size, normalOut(nrm), faceUVs ? m.uvs[fi][c] : glm::vec2(p[u] + 0.5f, p[v] + 0.5f)});
        }
        for (uint32_t k = 1; k + 1 < (uint32_t)f.size(); ++k) s.tris.insert(s.tris.end(), {base, base + k, base + k + 1});
    }
    return s;
}

// Everything with a shape in `n` (and `n` itself). False if there's nothing.
bool collect(const SceneNode& n, Collected& out, Group& g) {
    g.node = &n;
    g.world = unstretched(n.worldMatrix());
    if (hasShape(n)) {
        Shape s = shapeOf(n);
        if (!s.tris.empty()) {
            const Look look{n.color, n.transparency, n.material};
            auto it = std::find_if(out.looks.begin(), out.looks.end(), [&](const Look& l) { return !(l < look) && !(look < l); });
            s.material = (int)(it - out.looks.begin());
            if (it == out.looks.end()) out.looks.push_back(look);
            g.world = s.world;
            g.shape = (int)out.shapes.size();
            out.shapes.push_back(std::move(s));
        }
    }
    for (const auto& c : n.children) {
        if (c->internal || !c->visible || c->kind == NodeKind::Gui || c->bone) continue;
        Group child;
        if (collect(*c, out, child)) g.children.push_back(std::move(child));
    }
    return g.shape >= 0 || !g.children.empty();
}

Collected collectAll(const std::vector<const SceneNode*>& roots) {
    Collected c;
    for (const SceneNode* r : roots) {
        if (!r) continue;
        Group g;
        if (collect(*r, c, g)) c.groups.push_back(std::move(g));
    }
    return c;
}

int triangleCount(const Collected& c) {
    int t = 0;
    for (const Shape& s : c.shapes) t += (int)(s.tris.size() / 3);
    return t;
}

aiMatrix4x4 toAi(const glm::mat4& m) {
    return aiMatrix4x4(m[0][0], m[1][0], m[2][0], m[3][0],
                       m[0][1], m[1][1], m[2][1], m[3][1],
                       m[0][2], m[1][2], m[2][2], m[3][2],
                       m[0][3], m[1][3], m[2][3], m[3][3]);
}

// Names in the file are kept apart ("Wall", "Wall 2", ...): some programs mix up
// two objects with the same name.
std::string uniqueName(std::string n, std::set<std::string>& used) {
    if (n.empty()) n = "Part";
    std::string tryName = n;
    for (int i = 2; !used.insert(tryName).second; ++i) tryName = n + " " + std::to_string(i);
    return tryName;
}

aiNode* buildNode(const Group& g, const glm::mat4& parentWorld, std::set<std::string>& names, const Collected& c,
                  std::vector<aiMesh*>& meshes) {
    auto* node = new aiNode(uniqueName(g.node->name, names));
    node->mTransformation = toAi(glm::inverse(parentWorld) * g.world);
    if (g.shape >= 0) {
        const Shape& s = c.shapes[g.shape];
        auto* mesh = new aiMesh();
        mesh->mName = node->mName;
        mesh->mMaterialIndex = (unsigned)s.material;
        mesh->mPrimitiveTypes = aiPrimitiveType_TRIANGLE;
        mesh->mNumVertices = (unsigned)s.corners.size();
        mesh->mVertices = new aiVector3D[mesh->mNumVertices];
        mesh->mNormals = new aiVector3D[mesh->mNumVertices];
        mesh->mTextureCoords[0] = new aiVector3D[mesh->mNumVertices];
        mesh->mNumUVComponents[0] = 2;
        mesh->mColors[0] = new aiColor4D[mesh->mNumVertices];   // the part's colour on every corner (for .ply)
        const aiColor4D tint(s.node->color.r, s.node->color.g, s.node->color.b, 1.0f - s.node->transparency);
        for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
            const Corner& k = s.corners[i];
            mesh->mVertices[i] = aiVector3D(k.p.x, k.p.y, k.p.z);
            mesh->mNormals[i] = aiVector3D(k.n.x, k.n.y, k.n.z);
            mesh->mTextureCoords[0][i] = aiVector3D(k.uv.x, k.uv.y, 0.0f);
            mesh->mColors[0][i] = tint;
        }
        mesh->mNumFaces = (unsigned)(s.tris.size() / 3);
        mesh->mFaces = new aiFace[mesh->mNumFaces];
        for (unsigned f = 0; f < mesh->mNumFaces; ++f) {
            mesh->mFaces[f].mNumIndices = 3;
            mesh->mFaces[f].mIndices = new unsigned[3]{s.tris[f * 3], s.tris[f * 3 + 1], s.tris[f * 3 + 2]};
        }
        node->mNumMeshes = 1;
        node->mMeshes = new unsigned[1]{(unsigned)meshes.size()};
        meshes.push_back(mesh);
    }
    if (!g.children.empty()) {
        node->mNumChildren = (unsigned)g.children.size();
        node->mChildren = new aiNode*[node->mNumChildren];
        for (size_t i = 0; i < g.children.size(); ++i) {
            node->mChildren[i] = buildNode(g.children[i], g.world, names, c, meshes);
            node->mChildren[i]->mParent = node;
        }
    }
    return node;
}

aiMaterial* materialOf(const Look& l) {
    auto* m = new aiMaterial();
    const int ci = std::clamp((int)l.material, 0, kMaterialCount - 1);
    char hex[8];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X", (int)std::lround(std::clamp(l.color.r, 0.0f, 1.0f) * 255),
                  (int)std::lround(std::clamp(l.color.g, 0.0f, 1.0f) * 255), (int)std::lround(std::clamp(l.color.b, 0.0f, 1.0f) * 255));
    const aiString name{std::string(kMaterialNames[ci]) + "_" + hex};
    m->AddProperty(&name, AI_MATKEY_NAME);
    const float opacity = 1.0f - l.transparency;
    aiColor4D color(l.color.r, l.color.g, l.color.b, opacity);
    m->AddProperty(&color, 1, AI_MATKEY_COLOR_DIFFUSE);
    m->AddProperty(&color, 1, AI_MATKEY_BASE_COLOR);
    m->AddProperty(&opacity, 1, AI_MATKEY_OPACITY);
    const bool metal = l.material == Material::Metal || l.material == Material::Foil || l.material == Material::DiamondPlate;
    const bool shiny = metal || l.material == Material::Glass || l.material == Material::Ice || l.material == Material::Marble ||
                       l.material == Material::SmoothPlastic;
    const float metallic = metal ? 1.0f : 0.0f, roughness = shiny ? 0.25f : 0.7f;
    m->AddProperty(&metallic, 1, AI_MATKEY_METALLIC_FACTOR);
    m->AddProperty(&roughness, 1, AI_MATKEY_ROUGHNESS_FACTOR);
    if (l.material == Material::Neon) {
        aiColor3D glow(l.color.r, l.color.g, l.color.b);
        m->AddProperty(&glow, 1, AI_MATKEY_COLOR_EMISSIVE);
    }
    return m;
}

void put(std::string& out, const void* p, size_t n) { out.append((const char*)p, n); }
template <class T> void put(std::string& out, T v) { put(out, &v, sizeof v); }   // (little-endian, like every PC and phone)

} // namespace

std::string robloxMesh(const std::vector<const SceneNode*>& roots, int* parts, int* triangles) {
    Collected c = collectAll(roots);
    if (parts) *parts = (int)c.shapes.size();
    if (triangles) *triangles = triangleCount(c);
    // Every corner in the world, then centred on the middle of them all.
    struct V { glm::vec3 p, n; glm::vec2 uv; glm::vec3 color; float alpha; };
    std::vector<V> verts;
    std::vector<uint32_t> tris;
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (const Shape& s : c.shapes) {
        const uint32_t base = (uint32_t)verts.size();
        const glm::mat3 turn(s.world);
        for (const Corner& k : s.corners) {
            const glm::vec3 p = glm::vec3(s.world * glm::vec4(k.p, 1.0f));
            lo = glm::min(lo, p); hi = glm::max(hi, p);
            verts.push_back({p, glm::normalize(turn * k.n), k.uv, s.node->color, 1.0f - s.node->transparency});
        }
        for (uint32_t t : s.tris) tris.push_back(base + t);
    }
    const glm::vec3 mid = verts.empty() ? glm::vec3(0.0f) : (lo + hi) * 0.5f;

    std::string out = "version 2.00\n";
    put<uint16_t>(out, 12);   // header size
    put<uint8_t>(out, 40);    // bytes per corner: position, normal, uv, tangent, colour
    put<uint8_t>(out, 12);    // bytes per triangle
    put<uint32_t>(out, (uint32_t)verts.size());
    put<uint32_t>(out, (uint32_t)(tris.size() / 3));
    auto byte = [](float f) { return (uint8_t)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
    for (const V& v : verts) {
        const glm::vec3 p = v.p - mid;
        const float f[8] = {p.x, p.y, p.z, v.n.x, v.n.y, v.n.z, v.uv.x, 1.0f - v.uv.y};   // Roblox counts v from the top
        put(out, f, sizeof f);
        const int8_t tangent[4] = {127, 0, 0, 127};
        put(out, tangent, 4);
        const uint8_t rgba[4] = {byte(v.color.r), byte(v.color.g), byte(v.color.b), byte(v.alpha)};
        put(out, rgba, 4);
    }
    put(out, tris.data(), tris.size() * sizeof(uint32_t));
    return out;
}

bool write(const std::vector<const SceneNode*>& roots, const std::string& path, std::string& error, int* parts, int* triangles) {
    const Format* fmt = formatFor(path);
    if (!fmt) { error = "Export As can't write that type of file."; return false; }
    if (fmt->roblox) { error = "Roblox files are written by RobloxFile, not ModelExport."; return false; }
    std::error_code ec;
    if (fs::path(path).has_parent_path()) fs::create_directories(fs::path(path).parent_path(), ec);

    if (std::string(fmt->ext) == ".mesh") {
        int p = 0, t = 0;
        const std::string bytes = robloxMesh(roots, &p, &t);
        if (parts) *parts = p;
        if (triangles) *triangles = t;
        if (p == 0) { error = "There's nothing with a shape to export (pick parts, or a Model with parts in it)."; return false; }
        std::ofstream f(path, std::ios::binary);
        if (!f || !f.write(bytes.data(), (std::streamsize)bytes.size())) { error = "Couldn't write " + path + "."; return false; }
        return true;
    }

    Collected c = collectAll(roots);
    if (parts) *parts = (int)c.shapes.size();
    if (triangles) *triangles = triangleCount(c);
    if (c.shapes.empty()) { error = "There's nothing with a shape to export (pick parts, or a Model with parts in it)."; return false; }

    auto scene = std::make_unique<aiScene>();
    std::set<std::string> names;
    std::vector<aiMesh*> meshes;
    const std::string rootName = fs::path(path).stem().string();
    names.insert(rootName);
    scene->mRootNode = new aiNode(rootName);
    scene->mRootNode->mNumChildren = (unsigned)c.groups.size();
    scene->mRootNode->mChildren = new aiNode*[c.groups.size()];
    for (size_t i = 0; i < c.groups.size(); ++i) {
        scene->mRootNode->mChildren[i] = buildNode(c.groups[i], glm::mat4(1.0f), names, c, meshes);
        scene->mRootNode->mChildren[i]->mParent = scene->mRootNode;
    }
    scene->mNumMeshes = (unsigned)meshes.size();
    scene->mMeshes = new aiMesh*[meshes.size()];
    std::copy(meshes.begin(), meshes.end(), scene->mMeshes);
    scene->mNumMaterials = (unsigned)c.looks.size();
    scene->mMaterials = new aiMaterial*[c.looks.size()];
    for (size_t i = 0; i < c.looks.size(); ++i) scene->mMaterials[i] = materialOf(c.looks[i]);

    // FBX counts in centimetres unless told otherwise: say 1 unit is 1 metre, so a
    // 4 stud part comes into Blender or Unity 4 big, not 0.04 (glTF is metres already).
    scene->mMetaData = new aiMetadata();
    scene->mMetaData->Add("UnitScaleFactor", 100.0);
    scene->mMetaData->Add("OriginalUnitScaleFactor", 100.0);

    Assimp::Exporter exporter;
    if (exporter.Export(scene.get(), assimpId(fmt->ext), path) != aiReturn_SUCCESS) {
        error = std::string("Couldn't write ") + fmt->label + ": " + exporter.GetErrorString();
        return false;
    }
    return true;
}

} // namespace ModelExport
