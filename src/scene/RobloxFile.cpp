#include "RobloxFile.h"
#include "Guis.h"
#include "Scene.h"
#include "SceneNode.h"
#include "Environment.h"
#include "Serializer.h"
#include "EditMesh.h"
#include "RobloxMesh.h"
#include "Physics.h"
#include "../core/Paths.h"
#include "../renderer/MeshLibrary.h"
#include "../scripting/Luau.h"

#include <zstd.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>

namespace RobloxFile {

namespace {

constexpr float kImportScale = 0.5f;          // Roblox characters are twice our size
constexpr float kExportScale = 1.0f / kImportScale;

// Roblox's Enum.KeyCode numbers <-> their names (ProximityPrompt keys).
std::string keyName(int code) {
    static const char* digits[] = {"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};
    if (code >= 97 && code <= 122) return std::string(1, (char)('A' + code - 97));
    if (code >= 48 && code <= 57) return digits[code - 48];
    if (code >= 282 && code <= 293) return "F" + std::to_string(code - 281);
    switch (code) {
        case 32: return "Space"; case 13: return "Return"; case 9: return "Tab";
        case 304: return "LeftShift"; case 306: return "LeftControl"; case 308: return "LeftAlt";
        default: return "E";
    }
}
int keyCode(const std::string& name) {
    for (int c = 0; c < 400; ++c)
        if (keyName(c) == name && !(name == "E" && c != 101)) return c;
    return 101;   // E
}
constexpr float kRobloxGravity = 196.2f;
// Movers (BodyVelocity, AlignPosition...) in Roblox numbers -> ours. Our world is half
// the size with gentler gravity (22 vs 196.2), so speeds, spins and forces all shrink.
// Plastic is 0.7 heavy in Roblox and 1 here.
constexpr float kAccelScale  = 22.0f / kRobloxGravity;
const float     kVelScale    = std::sqrt(kImportScale * kAccelScale);   // studs / second
const float     kSpinScale   = kVelScale / kImportScale;                 // radians / second (and 1 / seconds)
constexpr float kForceScale  = kImportScale * kImportScale * kImportScale / 0.7f * kAccelScale;
constexpr float kTorqueScale = kForceScale * kImportScale;

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool endsWith(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && lower(s.substr(s.size() - e.size())) == e;
}

// ===========================================================================
// A Roblox object as it is in the file, before converting it.
// ===========================================================================

struct Value {
    enum Kind { None, Str, Bool, Num, Vec3, CFrame, Color, Ref, Token, UDim, UDim2, Vec2, NumSeq, ColorSeq, Physics } kind = None;
    std::string s;
    bool        b = false;
    double      n = 0.0;
    glm::vec3   v{0.0f};          // Vector3 / Color3 (0..1) / CFrame position
    glm::mat3   r{1.0f};          // CFrame rotation (columns = right, up, back)
    int64_t     ref = -1;
    glm::vec4   q{0.0f};          // UDim2 (x scale, x offset, y scale, y offset), UDim (scale, offset), Vector2 (x, y)
    std::vector<float> seq;       // NumberSequence (time, value)..., ColorSequence (time, r, g, b)...,
                                  // PhysicalProperties (density, friction, elasticity, frictionWeight, elasticityWeight)
};

struct Inst {
    std::string className;
    int64_t     referent = -1;
    std::string xmlRef;
    std::map<std::string, Value> props;   // keys in lower case
    std::vector<Inst*> children;
    Inst*       parent = nullptr;

    const Value* get(const char* name) const {
        auto it = props.find(lower(name));
        return it == props.end() ? nullptr : &it->second;
    }
    std::string str(const char* name, const std::string& fb = "") const {
        const Value* v = get(name);
        return v && v->kind == Value::Str ? v->s : fb;
    }
    double num(const char* name, double fb) const {
        const Value* v = get(name);
        if (!v) return fb;
        if (v->kind == Value::Num || v->kind == Value::Token) return v->n;
        if (v->kind == Value::Bool) return v->b ? 1.0 : 0.0;
        return fb;
    }
    bool flag(const char* name, bool fb) const {
        const Value* v = get(name);
        return v && v->kind == Value::Bool ? v->b : fb;
    }
};

struct Document {
    std::vector<std::unique_ptr<Inst>> all;
    std::vector<Inst*> roots;
    std::string error;
};

// ===========================================================================
// Binary format (.rbxl / .rbxm)
// ===========================================================================

struct Reader {
    const uint8_t* p;
    size_t size, pos = 0;
    bool   bad = false;
    Reader(const uint8_t* d, size_t n) : p(d), size(n) {}
    bool has(size_t n) { if (pos + n > size) { bad = true; return false; } return true; }
    uint8_t  u8()  { if (!has(1)) return 0; return p[pos++]; }
    uint32_t u32() { if (!has(4)) return 0; uint32_t v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
    float    f32() { if (!has(4)) return 0; float v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
    double   f64() { if (!has(8)) return 0; double v; std::memcpy(&v, p + pos, 8); pos += 8; return v; }
    std::string str() {
        uint32_t n = u32();
        if (!has(n)) return {};
        std::string s((const char*)p + pos, n);
        pos += n;
        return s;
    }
    const uint8_t* take(size_t n) { if (!has(n)) return nullptr; const uint8_t* r = p + pos; pos += n; return r; }
};

// LZ4 block format (what Roblox used for years).
bool lz4Decompress(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out, size_t outLen) {
    out.clear();
    out.reserve(outLen);
    size_t i = 0;
    while (i < srcLen) {
        uint8_t token = src[i++];
        size_t lit = token >> 4;
        if (lit == 15) { uint8_t b; do { if (i >= srcLen) return false; b = src[i++]; lit += b; } while (b == 255); }
        if (i + lit > srcLen) return false;
        out.insert(out.end(), src + i, src + i + lit);
        i += lit;
        if (i >= srcLen) break;                      // last sequence has no match
        if (i + 2 > srcLen) return false;
        size_t offset = src[i] | (src[i + 1] << 8);
        i += 2;
        if (offset == 0 || offset > out.size()) return false;
        size_t len = (token & 15);
        if (len == 15) { uint8_t b; do { if (i >= srcLen) return false; b = src[i++]; len += b; } while (b == 255); }
        len += 4;
        size_t start = out.size() - offset;
        for (size_t k = 0; k < len; ++k) out.push_back(out[start + k]);   // may overlap: copy byte by byte
    }
    return out.size() == outLen;
}

// Arrays in the binary format are "interleaved": all first bytes, then all
// second bytes, ... (compresses better). Values are big-endian.
std::vector<uint32_t> interleaved32(Reader& r, size_t count) {
    std::vector<uint32_t> out(count, 0);
    const uint8_t* d = r.take(count * 4);
    if (!d) return out;
    for (size_t i = 0; i < count; ++i)
        out[i] = (uint32_t)d[i] << 24 | (uint32_t)d[count + i] << 16 | (uint32_t)d[2 * count + i] << 8 | d[3 * count + i];
    return out;
}
std::vector<int64_t> interleaved64(Reader& r, size_t count) {
    std::vector<int64_t> out(count, 0);
    const uint8_t* d = r.take(count * 8);
    if (!d) return out;
    for (size_t i = 0; i < count; ++i) {
        uint64_t v = 0;
        for (int b = 0; b < 8; ++b) v = (v << 8) | d[b * count + i];
        out[i] = (int64_t)((v >> 1) ^ (~(v & 1) + 1));   // zigzag
    }
    return out;
}
int32_t unzigzag(uint32_t v) { return (int32_t)((v >> 1) ^ (uint32_t)(-(int32_t)(v & 1))); }
float   robloxFloat(uint32_t v) { v = (v >> 1) | (v << 31); float f; std::memcpy(&f, &v, 4); return f; }

std::vector<int32_t> ints(Reader& r, size_t count) {
    auto raw = interleaved32(r, count);
    std::vector<int32_t> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = unzigzag(raw[i]);
    return out;
}
std::vector<float> floats(Reader& r, size_t count) {
    auto raw = interleaved32(r, count);
    std::vector<float> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = robloxFloat(raw[i]);
    return out;
}
std::vector<int64_t> referents(Reader& r, size_t count) {
    auto d = ints(r, count);
    std::vector<int64_t> out(count);
    int64_t acc = 0;
    for (size_t i = 0; i < count; ++i) { acc += d[i]; out[i] = acc; }
    return out;
}

// The 24 "tidy" rotations Roblox stores as one byte: axis ids 0..5 are
// +X +Y +Z -X -Y -Z (Roblox's NormalId order: Right, Top, Back, Left, Bottom, Front).
glm::mat3 rotationFromId(uint8_t id) {
    static const glm::vec3 axes[6] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    int x = (id - 1) / 6, y = (id - 1) % 6;
    if (x < 0 || x > 5) return glm::mat3(1.0f);
    glm::vec3 right = axes[x], up = axes[y];
    return glm::mat3(right, up, glm::cross(right, up));
}

// Roblox's row-major R00..R22 -> a matrix whose columns are right, up, back.
glm::mat3 fromRows(const float m[9]) {
    return glm::mat3(glm::vec3(m[0], m[3], m[6]), glm::vec3(m[1], m[4], m[7]), glm::vec3(m[2], m[5], m[8]));
}

struct ClassInfo {
    std::string name;
    std::vector<Inst*> objects;
};

bool readBinary(const std::vector<uint8_t>& data, Document& doc) {
    Reader head(data.data(), data.size());
    if (data.size() < 32 || std::memcmp(data.data(), "<roblox!", 8) != 0) { doc.error = "not a Roblox file"; return false; }
    head.pos = 32;
    std::map<uint32_t, ClassInfo> classes;
    std::unordered_map<int64_t, Inst*> byRef;
    std::vector<std::string> shared;

    while (head.pos + 16 <= data.size()) {
        char name[5] = {0};
        std::memcpy(name, data.data() + head.pos, 4);
        head.pos += 4;
        uint32_t clen = head.u32(), ulen = head.u32();
        head.u32();
        std::vector<uint8_t> chunk;
        if (clen == 0) {
            const uint8_t* d = head.take(ulen);
            if (!d) break;
            chunk.assign(d, d + ulen);
        } else {
            const uint8_t* d = head.take(clen);
            if (!d) break;
            if (clen >= 4 && d[0] == 0x28 && d[1] == 0xB5 && d[2] == 0x2F && d[3] == 0xFD) {
                chunk.resize(ulen);
                size_t got = ZSTD_decompress(chunk.data(), ulen, d, clen);
                if (ZSTD_isError(got) || got != ulen) { doc.error = "couldn't unpack part of the file (zstd)"; return false; }
            } else if (!lz4Decompress(d, clen, chunk, ulen)) {
                doc.error = "couldn't unpack part of the file (lz4)";
                return false;
            }
        }
        Reader r(chunk.data(), chunk.size());
        std::string kind(name);

        if (kind == "SSTR") {
            r.u32();                                   // version
            uint32_t n = r.u32();
            for (uint32_t i = 0; i < n && !r.bad; ++i) { r.take(16); shared.push_back(r.str()); }
        } else if (kind == "INST") {
            uint32_t classId = r.u32();
            ClassInfo& ci = classes[classId];
            ci.name = r.str();
            uint8_t isService = r.u8();
            uint32_t count = r.u32();
            auto refs = referents(r, count);
            for (uint32_t i = 0; i < count; ++i) {
                auto inst = std::make_unique<Inst>();
                inst->className = ci.name;
                inst->referent = refs[i];
                byRef[refs[i]] = inst.get();
                ci.objects.push_back(inst.get());
                doc.all.push_back(std::move(inst));
            }
            (void)isService;
        } else if (kind == "PROP") {
            uint32_t classId = r.u32();
            std::string prop = lower(r.str());
            uint8_t type = r.u8();
            auto it = classes.find(classId);
            if (it == classes.end()) continue;
            auto& objs = it->second.objects;
            size_t n = objs.size();
            auto set = [&](size_t i, Value v) { if (i < n) objs[i]->props[prop] = std::move(v); };
            switch (type) {
            case 0x01:                                             // String
            case 0x1D:                                             // Bytecode
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Str; v.s = r.str(); set(i, v); }
                break;
            case 0x02:                                             // Bool
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Bool; v.b = r.u8() != 0; set(i, v); }
                break;
            case 0x03: {                                           // Int32
                auto a = ints(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x04: {                                           // Float32
                auto a = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x05:                                             // Float64
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = r.f64(); set(i, v); }
                break;
            case 0x06: {                                           // UDim
                auto S = floats(r, n); auto O = ints(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::UDim; v.q = {S[i], (float)O[i], 0, 0}; set(i, v); }
                break;
            }
            case 0x07: {                                           // UDim2
                auto SX = floats(r, n), SY = floats(r, n); auto OX = ints(r, n), OY = ints(r, n);
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::UDim2; v.q = {SX[i], (float)OX[i], SY[i], (float)OY[i]}; set(i, v);
                }
                break;
            }
            case 0x0D: {                                           // Vector2
                auto X = floats(r, n), Y = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Vec2; v.q = {X[i], Y[i], 0, 0}; set(i, v); }
                break;
            }
            case 0x0C: {                                           // Color3
                auto R = floats(r, n), G = floats(r, n), B = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Color; v.v = {R[i], G[i], B[i]}; set(i, v); }
                break;
            }
            case 0x0E: {                                           // Vector3
                auto X = floats(r, n), Y = floats(r, n), Z = floats(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Vec3; v.v = {X[i], Y[i], Z[i]}; set(i, v); }
                break;
            }
            case 0x10: {                                           // CFrame
                std::vector<glm::mat3> rots(n, glm::mat3(1.0f));
                for (size_t i = 0; i < n && !r.bad; ++i) {
                    uint8_t id = r.u8();
                    if (id == 0) { float m[9]; for (float& f : m) f = r.f32(); rots[i] = fromRows(m); }
                    else rots[i] = rotationFromId(id);
                }
                auto X = floats(r, n), Y = floats(r, n), Z = floats(r, n);
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::CFrame; v.v = {X[i], Y[i], Z[i]}; v.r = rots[i]; set(i, v);
                }
                break;
            }
            case 0x0B: {                                           // BrickColor (its number)
                auto a = interleaved32(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x19:                                             // PhysicalProperties
                for (size_t i = 0; i < n && !r.bad; ++i) {
                    const uint8_t custom = r.u8();
                    Value v;
                    if (custom) {
                        v.kind = Value::Physics;
                        for (int k = 0; k < 5; ++k) v.seq.push_back(r.f32());
                        if (custom & 2) r.f32();                   // (newer files: AcousticAbsorption too)
                    }
                    set(i, v);
                }
                break;
            case 0x12: {                                           // Enum
                auto a = interleaved32(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Token; v.n = a[i]; set(i, v); }
                break;
            }
            case 0x13: {                                           // Referent
                auto a = referents(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Ref; v.ref = a[i]; set(i, v); }
                break;
            }
            case 0x15:                                             // NumberSequence
            case 0x16: {                                           // ColorSequence
                const bool color = type == 0x16;
                for (size_t i = 0; i < n && !r.bad; ++i) {
                    Value v; v.kind = color ? Value::ColorSeq : Value::NumSeq;
                    const uint32_t count = r.u32();
                    for (uint32_t k = 0; k < count && !r.bad; ++k) {
                        const float t = r.f32();
                        if (color) { const float R = r.f32(), G = r.f32(), B = r.f32(); r.f32(); if (k < 20) v.seq.insert(v.seq.end(), {t, R, G, B}); }
                        else       { const float val = r.f32(); r.f32(); if (k < 20) v.seq.insert(v.seq.end(), {t, val}); }
                    }
                    set(i, v);
                }
                break;
            }
            case 0x17: {                                           // NumberRange: min, max (kept like a Vector2)
                for (size_t i = 0; i < n && !r.bad; ++i) {
                    Value v; v.kind = Value::Vec2;
                    const float lo = r.f32(), hi = r.f32();
                    v.q = {lo, hi, 0, 0};
                    set(i, v);
                }
                break;
            }
            case 0x1A: {                                           // Color3uint8
                const uint8_t* R = r.take(n); const uint8_t* G = r.take(n); const uint8_t* B = r.take(n);
                if (!R || !G || !B) break;
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::Color; v.v = glm::vec3(R[i], G[i], B[i]) / 255.0f; set(i, v);
                }
                break;
            }
            case 0x1B: {                                           // Int64
                auto a = interleaved64(r, n);
                for (size_t i = 0; i < n; ++i) { Value v; v.kind = Value::Num; v.n = (double)a[i]; set(i, v); }
                break;
            }
            case 0x1C: {                                           // SharedString
                auto a = interleaved32(r, n);
                for (size_t i = 0; i < n; ++i) {
                    Value v; v.kind = Value::Str; if (a[i] < shared.size()) v.s = shared[a[i]]; set(i, v);
                }
                break;
            }
            default: break;                                         // a type we don't need
            }
        } else if (kind == "PRNT") {
            r.u8();
            uint32_t n = r.u32();
            auto kids = referents(r, n), parents = referents(r, n);
            for (uint32_t i = 0; i < n; ++i) {
                Inst* c = byRef.count(kids[i]) ? byRef[kids[i]] : nullptr;
                if (!c) continue;
                if (parents[i] >= 0 && byRef.count(parents[i])) {
                    c->parent = byRef[parents[i]];
                    c->parent->children.push_back(c);
                } else {
                    doc.roots.push_back(c);
                }
            }
        } else if (kind == "END") {
            break;
        }
    }
    return true;
}

// ===========================================================================
// XML format (.rbxlx / .rbxmx)
// ===========================================================================

struct XmlNode {
    std::string tag, text;
    std::map<std::string, std::string> attrs;
    std::vector<std::unique_ptr<XmlNode>> kids;
    const XmlNode* child(const char* t) const {
        for (auto& k : kids) if (k->tag == t) return k.get();
        return nullptr;
    }
};

std::string xmlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') { out += s[i]; continue; }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos) { out += s[i]; continue; }
        std::string e = s.substr(i + 1, semi - i - 1);
        if (e == "lt") out += '<'; else if (e == "gt") out += '>'; else if (e == "amp") out += '&';
        else if (e == "quot") out += '"'; else if (e == "apos") out += '\'';
        else if (!e.empty() && e[0] == '#') {
            unsigned long c = e.size() > 1 && (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16)
                                                                            : std::strtoul(e.c_str() + 1, nullptr, 10);
            if (c < 0x80) out += (char)c;
            else if (c < 0x800) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
            else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
        } else { out += '&' + e + ';'; }
        i = semi;
    }
    return out;
}

// A small XML reader: elements, attributes, text, CDATA. Enough for Roblox files.
std::unique_ptr<XmlNode> parseXml(const std::string& s, std::string& error) {
    auto root = std::make_unique<XmlNode>();
    std::vector<XmlNode*> stack{root.get()};
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] != '<') {
            size_t e = s.find('<', i);
            if (e == std::string::npos) e = s.size();
            stack.back()->text += xmlDecode(s.substr(i, e - i));
            i = e;
            continue;
        }
        if (s.compare(i, 9, "<![CDATA[") == 0) {
            size_t e = s.find("]]>", i + 9);
            if (e == std::string::npos) { error = "unfinished CDATA"; return nullptr; }
            stack.back()->text += s.substr(i + 9, e - i - 9);
            i = e + 3;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i); i = e == std::string::npos ? s.size() : e + 3; continue; }
        if (s.compare(i, 2, "<?") == 0 || s.compare(i, 2, "<!") == 0) { size_t e = s.find('>', i); i = e == std::string::npos ? s.size() : e + 1; continue; }
        if (s.compare(i, 2, "</") == 0) {
            size_t e = s.find('>', i);
            if (stack.size() > 1) stack.pop_back();
            i = e == std::string::npos ? s.size() : e + 1;
            continue;
        }
        // Opening tag.
        size_t j = i + 1;
        while (j < s.size() && !std::isspace((unsigned char)s[j]) && s[j] != '>' && s[j] != '/') ++j;
        auto node = std::make_unique<XmlNode>();
        node->tag = s.substr(i + 1, j - i - 1);
        bool selfClose = false;
        while (j < s.size() && s[j] != '>') {
            if (s[j] == '/') { selfClose = true; ++j; continue; }
            if (std::isspace((unsigned char)s[j])) { ++j; continue; }
            size_t eq = s.find('=', j);
            if (eq == std::string::npos) break;
            std::string key = s.substr(j, eq - j);
            while (!key.empty() && std::isspace((unsigned char)key.back())) key.pop_back();
            size_t q = eq + 1;
            while (q < s.size() && std::isspace((unsigned char)s[q])) ++q;
            char quote = s[q];
            size_t qe = s.find(quote, q + 1);
            if (qe == std::string::npos) break;
            node->attrs[key] = xmlDecode(s.substr(q + 1, qe - q - 1));
            j = qe + 1;
        }
        i = j + 1;
        XmlNode* raw = node.get();
        stack.back()->kids.push_back(std::move(node));
        if (!selfClose) stack.push_back(raw);
    }
    return root;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::vector<uint8_t> base64Decode(const std::string& in) {
    static const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> out;
    uint32_t buf = 0; int bits = 0;
    for (char c : in) {
        size_t v = chars.find(c);
        if (v == std::string::npos) continue;
        buf = (buf << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)(buf >> bits)); }
    }
    return out;
}

float fnum(const XmlNode* n) { return n ? (float)std::atof(trim(n->text).c_str()) : 0.0f; }

void readXmlItem(const XmlNode& x, Inst* parent, Document& doc, std::map<std::string, Inst*>& refs,
                 const std::map<std::string, std::string>& shared) {
    auto inst = std::make_unique<Inst>();
    inst->className = x.attrs.count("class") ? x.attrs.at("class") : "";
    inst->xmlRef = x.attrs.count("referent") ? x.attrs.at("referent") : "";
    Inst* raw = inst.get();
    if (!inst->xmlRef.empty()) refs[inst->xmlRef] = raw;
    if (const XmlNode* props = x.child("Properties")) {
        for (auto& p : props->kids) {
            std::string name = lower(p->attrs.count("name") ? p->attrs.at("name") : "");
            if (name.empty()) continue;
            Value v;
            const std::string& t = p->tag;
            std::string text = trim(p->text);
            if (t == "string" || t == "ProtectedString" || t == "Content" || t == "url") {
                v.kind = Value::Str; v.s = t == "ProtectedString" ? p->text : text;
                if (t == "Content") if (const XmlNode* u = p->child("url")) v.s = trim(u->text);
            } else if (t == "BinaryString") {
                auto bytes = base64Decode(text);
                v.kind = Value::Str; v.s.assign(bytes.begin(), bytes.end());
            } else if (t == "SharedString") {
                v.kind = Value::Str;
                auto it = shared.find(text);
                if (it != shared.end()) v.s = it->second;
            } else if (t == "bool") {
                v.kind = Value::Bool; v.b = text == "true";
            } else if (t == "int" || t == "int64" || t == "float" || t == "double") {
                v.kind = Value::Num; v.n = std::atof(text.c_str());
            } else if (t == "token") {
                v.kind = Value::Token; v.n = std::atof(text.c_str());
            } else if (t == "BrickColor") {
                v.kind = Value::Num; v.n = std::atof(text.c_str());
            } else if (t == "PhysicalProperties") {
                const XmlNode* on = p->child("CustomPhysics");
                if (on && trim(on->text) == "true") {
                    v.kind = Value::Physics;
                    for (const char* k : {"Density", "Friction", "Elasticity", "FrictionWeight", "ElasticityWeight"})
                        v.seq.push_back(p->child(k) ? fnum(p->child(k)) : 1.0f);
                }
            } else if (t == "Vector3") {
                v.kind = Value::Vec3; v.v = {fnum(p->child("X")), fnum(p->child("Y")), fnum(p->child("Z"))};
            } else if (t == "UDim2") {
                v.kind = Value::UDim2; v.q = {fnum(p->child("XS")), fnum(p->child("XO")), fnum(p->child("YS")), fnum(p->child("YO"))};
            } else if (t == "UDim") {
                v.kind = Value::UDim; v.q = {fnum(p->child("S")), fnum(p->child("O")), 0, 0};
            } else if (t == "Vector2") {
                v.kind = Value::Vec2; v.q = {fnum(p->child("X")), fnum(p->child("Y")), 0, 0};
            } else if (t == "NumberRange") {   // "min max" (kept like a Vector2)
                v.kind = Value::Vec2;
                char* end = nullptr;
                const float lo = std::strtof(text.c_str(), &end);
                const float hi = end && *end ? std::strtof(end, nullptr) : lo;
                v.q = {lo, hi, 0, 0};
            } else if (t == "Color3") {
                v.kind = Value::Color;
                if (p->child("R")) v.v = {fnum(p->child("R")), fnum(p->child("G")), fnum(p->child("B"))};
                else { uint32_t c = (uint32_t)std::strtoul(text.c_str(), nullptr, 10);
                       v.v = glm::vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.0f; }
            } else if (t == "NumberSequence" || t == "ColorSequence") {   // "t v envelope ..." / "t r g b envelope ..."
                const bool color = t == "ColorSequence";
                v.kind = color ? Value::ColorSeq : Value::NumSeq;
                std::vector<float> all;
                for (const char* c = text.c_str(); *c && all.size() < 200;) {
                    char* end = nullptr;
                    const float f = std::strtof(c, &end);
                    if (end == c) { ++c; continue; }
                    all.push_back(f);
                    c = end;
                }
                const size_t step = color ? 5 : 3;
                for (size_t k = 0; k + step <= all.size(); k += step)
                    v.seq.insert(v.seq.end(), all.begin() + k, all.begin() + k + step - 1);
            } else if (t == "Color3uint8") {
                uint32_t c = (uint32_t)std::strtoul(text.c_str(), nullptr, 10);
                v.kind = Value::Color; v.v = glm::vec3((c >> 16) & 255, (c >> 8) & 255, c & 255) / 255.0f;
            } else if (t == "CoordinateFrame" || t == "CFrame") {
                v.kind = Value::CFrame;
                v.v = {fnum(p->child("X")), fnum(p->child("Y")), fnum(p->child("Z"))};
                float m[9];
                const char* names[9] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
                for (int k = 0; k < 9; ++k) m[k] = p->child(names[k]) ? fnum(p->child(names[k])) : (k % 4 == 0 ? 1.0f : 0.0f);
                v.r = fromRows(m);
            } else if (t == "Ref") {
                v.kind = Value::Ref; v.s = text;
            } else {
                continue;
            }
            inst->props[name] = v;
        }
    }
    if (parent) { inst->parent = parent; parent->children.push_back(raw); }
    else doc.roots.push_back(raw);
    doc.all.push_back(std::move(inst));
    for (auto& k : x.kids)
        if (k->tag == "Item") readXmlItem(*k, raw, doc, refs, shared);
}

bool readXml(const std::string& text, Document& doc) {
    auto xml = parseXml(text, doc.error);
    if (!xml) return false;
    const XmlNode* roblox = xml->child("roblox");
    if (!roblox) { doc.error = "not a Roblox XML file"; return false; }
    std::map<std::string, std::string> shared;
    if (const XmlNode* ss = roblox->child("SharedStrings"))
        for (auto& k : ss->kids) if (k->attrs.count("md5")) {
            auto bytes = base64Decode(trim(k->text));
            shared[k->attrs.at("md5")] = std::string(bytes.begin(), bytes.end());
        }
    std::map<std::string, Inst*> refs;
    for (auto& k : roblox->kids) if (k->tag == "Item") readXmlItem(*k, nullptr, doc, refs, shared);
    // XML references are names ("RBX..."); give every object a number like the binary format.
    int64_t next = 1;
    std::map<std::string, int64_t> ids;
    for (auto& [name, inst] : refs) { inst->referent = next; ids[name] = next++; }
    for (auto& inst : doc.all)
        for (auto& [k, v] : inst->props)
            if (v.kind == Value::Ref) v.ref = ids.count(v.s) ? ids[v.s] : -1;
    return true;
}

bool readFile(const std::string& path, Document& doc) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { doc.error = "couldn't open the file"; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() >= 8 && std::memcmp(data.data(), "<roblox!", 8) == 0) return readBinary(data, doc);
    return readXml(std::string(data.begin(), data.end()), doc);
}

// ===========================================================================
// Roblox objects -> Guts and Bolts objects
// ===========================================================================

// Roblox's Enum.Material numbers. The ones we have are kept as they are; the rest get
// the closest look we do have.
Material materialFromRoblox(int m) {
    switch (m) {
        case 272:                                    return Material::SmoothPlastic;
        case 288:                                    return Material::Neon;
        case 512: case 528:                          return Material::Wood;          // Wood, WoodPlanks
        case 784:                                    return Material::Marble;
        case 800:                                    return Material::Slate;
        case 832:                                    return Material::Granite;
        case 848:                                    return Material::Brick;
        case 864: case 880:                          return Material::Cobblestone;   // Pebble, Cobblestone
        case 1056:                                   return Material::DiamondPlate;
        case 1072:                                   return Material::Foil;
        case 1040: case 1088:                        return Material::Metal;         // CorrodedMetal, Metal
        case 1280: case 1284:                        return Material::Grass;         // Grass, LeafyGrass
        case 1296: case 1392:                        return Material::Sand;          // Sand, Salt
        case 1312:                                   return Material::Fabric;
        case 1536: case 1552:                        return Material::Ice;           // Ice, Glacier
        case 1568:                                   return Material::Glass;
        // Basalt, Concrete, Limestone, Pavement, Rock, Sandstone, Asphalt
        case 788: case 816: case 820: case 836: case 896: case 912: case 1376:
                                                     return Material::Concrete;
        default:                                     return Material::Plastic;
    }
}
int materialToRoblox(Material m) {
    switch (m) {
        case Material::Plastic:       return 256;
        case Material::SmoothPlastic: return 272;
        case Material::Neon:          return 288;
        case Material::Wood:          return 512;
        case Material::Marble:        return 784;
        case Material::Slate:         return 800;
        case Material::Concrete:      return 816;
        case Material::Granite:       return 832;
        case Material::Brick:         return 848;
        case Material::Cobblestone:   return 880;
        case Material::DiamondPlate:  return 1056;
        case Material::Foil:          return 1072;
        case Material::Metal:         return 1088;
        case Material::Grass:         return 1280;
        case Material::Sand:          return 1296;
        case Material::Fabric:        return 1312;
        case Material::Ice:           return 1536;
        case Material::Glass:         return 1568;
    }
    return 256;
}

// BrickColor numbers (TeamColor) -> colours: the common ones; anything else is Medium stone grey.
struct Brick { int number; uint8_t r, g, b; };
const Brick kBricks[] = {
    {1, 242, 243, 243}, {2, 161, 165, 162}, {5, 215, 197, 154}, {9, 232, 186, 200}, {11, 128, 187, 219},
    {18, 204, 142, 105}, {21, 196, 40, 28}, {23, 13, 105, 172}, {24, 245, 205, 48}, {26, 27, 42, 53},
    {28, 40, 127, 71}, {29, 161, 196, 140}, {37, 75, 151, 75}, {38, 160, 95, 53}, {45, 180, 210, 228},
    {101, 218, 134, 122}, {102, 110, 153, 202}, {104, 107, 50, 124}, {105, 226, 155, 64}, {106, 218, 133, 65},
    {107, 0, 143, 156}, {119, 164, 189, 71}, {125, 234, 184, 146}, {135, 116, 134, 157}, {141, 39, 70, 45},
    {151, 120, 144, 130}, {153, 149, 121, 119}, {192, 105, 64, 40}, {194, 163, 162, 165}, {199, 99, 95, 98},
    {208, 229, 228, 223}, {217, 124, 92, 70}, {226, 253, 234, 141}, {1001, 248, 248, 248}, {1002, 205, 205, 205},
    {1003, 17, 17, 17}, {1004, 255, 0, 0}, {1005, 255, 176, 0}, {1006, 180, 128, 255}, {1007, 163, 75, 75},
    {1008, 193, 190, 66}, {1009, 255, 255, 0}, {1010, 0, 0, 255}, {1011, 0, 32, 96}, {1012, 33, 84, 185},
    {1013, 4, 175, 236}, {1014, 170, 85, 0}, {1015, 170, 0, 170}, {1016, 255, 102, 204}, {1017, 255, 175, 0},
    {1018, 18, 238, 212}, {1019, 0, 255, 255}, {1020, 0, 255, 0}, {1021, 58, 125, 21}, {1022, 127, 142, 100},
    {1023, 140, 91, 159}, {1024, 175, 221, 255}, {1025, 255, 201, 201}, {1026, 177, 167, 255}, {1027, 159, 243, 233},
    {1028, 204, 255, 204}, {1029, 255, 255, 204}, {1030, 255, 204, 153}, {1031, 98, 37, 209}, {1032, 255, 0, 191},
};
glm::vec3 brickColor(int number) {
    for (const Brick& b : kBricks)
        if (b.number == number) return glm::vec3(b.r, b.g, b.b) / 255.0f;
    return kDefaultTeamColor;
}
int brickNumber(glm::vec3 c) {   // the closest one
    int best = 194;
    float bestD = 1e9f;
    for (const Brick& b : kBricks) {
        const float d = glm::length(glm::vec3(b.r, b.g, b.b) / 255.0f - c);
        if (d < bestD) { bestD = d; best = b.number; }
    }
    return best;
}

// Roblox attributes are stored as a small binary blob.
void readAttributes(const std::string& blob, SceneNode& node) {
    if (blob.size() < 4) return;
    Reader r((const uint8_t*)blob.data(), blob.size());
    uint32_t count = r.u32();
    for (uint32_t i = 0; i < count && !r.bad; ++i) {
        Attribute a;
        a.name = r.str();
        uint8_t type = r.u8();
        switch (type) {
            case 0x02: a.type = Attribute::String; a.s = r.str(); break;
            case 0x03: a.type = Attribute::Bool; a.b = r.u8() != 0; break;
            case 0x05: a.type = Attribute::Number; a.n = r.f32(); break;
            case 0x06: a.type = Attribute::Number; a.n = r.f64(); break;
            case 0x0F: a.type = Attribute::Color3; a.v = {r.f32(), r.f32(), r.f32()}; break;
            case 0x11: a.type = Attribute::Vector3; a.v = {r.f32(), r.f32(), r.f32()}; break;
            default: return;   // a kind we can't read: stop (we don't know how long it is)
        }
        if (!r.bad) node.attributes.push_back(a);
    }
}

// The Workspace's collision groups as Roblox saves them: "Default^0^-1;Players^1^-3"
// (name ^ id ^ mask, the mask a signed 32-bit number: bit b = collides with group b).
CollisionGroups parseCollisionGroups(const std::string& text) {
    CollisionGroups g;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string entry = text.substr(start, end - start);
        start = end + 1;
        const size_t a = entry.find('^'), b = entry.find('^', a == std::string::npos ? a : a + 1);
        if (a == std::string::npos || b == std::string::npos) continue;
        std::string name = entry.substr(0, a).substr(0, CollisionGroups::kMaxNameLength);
        long long id = -1, mask = -1;
        try { id = std::stoll(entry.substr(a + 1, b - a - 1)); mask = std::stoll(entry.substr(b + 1)); } catch (...) { continue; }
        if (id < 0 || id >= CollisionGroups::kMax || name.empty()) continue;
        if (id == 0) name = CollisionGroups::kDefault;
        else if (name == CollisionGroups::kDefault || g.find(name) >= 0) continue;
        g.names[id] = name;
        g.masks[id] = (uint32_t)(int32_t)mask;
    }
    for (int x = 0; x < CollisionGroups::kMax; ++x)   // (both sides of a pair agree)
        for (int y = 0; y < x; ++y) g.set(x, y, ((g.masks[x] >> y) & 1u) && ((g.masks[y] >> x) & 1u));
    return g;
}

std::string writeCollisionGroups(const CollisionGroups& g) {
    std::string out;
    for (int i : g.slots()) {
        if (!out.empty()) out += ';';
        out += g.names[i] + "^" + std::to_string(i) + "^" + std::to_string((int32_t)g.masks[i]);
    }
    return out;
}

struct Converter {
    Scene& scene;
    Report& report;
    std::unordered_map<const Inst*, SceneNode*> made;
    CollisionGroups groups;   // the place's (to name parts saved with only a CollisionGroupId)

    static bool isPartClass(const std::string& c) {
        static const char* names[] = {"Part", "SpawnLocation", "Seat", "VehicleSeat", "TrussPart", "WedgePart",
                                      "CornerWedgePart", "MeshPart", "UnionOperation", "IntersectOperation",
                                      "SkateboardPlatform", "FlagStand", "PartOperation"};
        for (const char* n : names) if (c == n) return true;
        return false;
    }

    void note(const std::string& n) {
        if (std::find(report.notes.begin(), report.notes.end(), n) == report.notes.end() && report.notes.size() < 30)
            report.notes.push_back(n);
    }

    struct MeshLook {
        std::shared_ptr<EditMesh> shape;   // a downloaded mesh (else the primitive in `prim`)
        std::string texture;               // its picture ("roblox/<id>.png" in the games folder)
        glm::vec3 tint{1.0f};              // SpecialMesh.VertexColor
        bool special = false;              // sized by a SpecialMesh, not by the part
    };

    // MeshPart: the mesh is stretched to fill the part's Size.
    // SpecialMesh: a FileMesh keeps its own size (times Scale); the other kinds are
    // the part's shape times Scale. Both are moved by Offset.
    MeshLook meshLook(const Inst& in, const std::string& name, PrimitiveType& prim, glm::vec3& size, glm::vec3& pos,
                      const glm::mat3& rot) {
        MeshLook look;
        std::string meshUrl, texUrl;
        int type = -1;   // SpecialMesh.MeshType: 0 Head, 1 Torso, 2 Wedge, 3 Sphere, 4 Cylinder, 5 FileMesh, 6 Brick
        glm::vec3 scale(1.0f), offset(0.0f);
        if (in.className == "MeshPart") {
            meshUrl = in.str("MeshId");
            texUrl = in.str("TextureID");
        } else {
            for (const Inst* k : in.children) {
                const std::string& kc = k->className;
                if (kc != "SpecialMesh" && kc != "FileMesh" && kc != "BlockMesh" && kc != "CylinderMesh") continue;
                type = kc == "BlockMesh" ? 6 : kc == "CylinderMesh" ? 4 : kc == "FileMesh" ? 5 : (int)k->num("MeshType", 6);
                if (const Value* v = k->get("Scale")) scale = v->v;
                if (const Value* v = k->get("Offset")) offset = v->v;
                if (const Value* v = k->get("VertexColor")) look.tint = v->v;
                meshUrl = k->str("MeshId");
                texUrl = k->str("TextureId");
                break;
            }
            if (type < 0) return look;
            look.special = true;
            scale = glm::abs(scale);
        }
        if (in.className == "MeshPart" || type == 5) {
            const std::string id = RobloxMesh::assetId(meshUrl);
            const std::string file = RobloxMesh::fetch(id, ".mesh");
            std::string err;
            RobloxMesh::Shape s;
            if (!file.empty()) {
                std::ifstream f(Paths::gamesFolder() / file, std::ios::binary);
                std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                s = RobloxMesh::parse(bytes, &err);
            } else if (meshUrl.empty()) {
                return look;   // an empty MeshPart: just a block
            } else {
                err = id.empty() ? "its mesh (" + meshUrl + ") is built into Roblox" : "couldn't download its mesh (Roblox asset " + id + ")";
            }
            if (!s.mesh) {
                note(name + ": " + err + ", so it came in as a block.");
                return look;
            }
            look.shape = s.mesh;
            prim = PrimitiveType::Mesh;
            if (type == 5) {
                pos += rot * ((s.center * scale + offset) * kImportScale);
                size = glm::max(s.size * scale * kImportScale, glm::vec3(1e-3f));
            }
        } else {
            if (type == 3) prim = PrimitiveType::Sphere;
            else if (type == 0 || type == 4) prim = PrimitiveType::Cylinder;   // standing up, like ours
            else prim = PrimitiveType::Cube;
            if (type == 2) note("Wedges come in as blocks.");
            size *= scale;
            pos += rot * (offset * kImportScale);
        }
        if (const std::string tid = RobloxMesh::assetId(texUrl); !tid.empty()) {
            look.texture = RobloxMesh::fetch(tid, ".png");
            if (look.texture.empty()) note(name + ": couldn't download its picture (Roblox asset " + tid + ").");
        }
        return look;
    }

    // World CFrame -> a node placed under `parent` (our transforms are relative).
    static void place(SceneNode& n, SceneNode* parent, glm::vec3 pos, glm::mat3 rot, glm::vec3 size) {
        glm::mat4 world(1.0f);
        world[0] = glm::vec4(rot[0] * size.x, 0);
        world[1] = glm::vec4(rot[1] * size.y, 0);
        world[2] = glm::vec4(rot[2] * size.z, 0);
        world[3] = glm::vec4(pos, 1);
        glm::mat4 local = parent ? glm::inverse(parent->worldMatrix()) * world : world;
        glm::vec3 s(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])), glm::length(glm::vec3(local[2])));
        glm::mat4 r(1.0f);
        r[0] = glm::vec4(glm::vec3(local[0]) / std::max(s.x, 1e-6f), 0);
        r[1] = glm::vec4(glm::vec3(local[1]) / std::max(s.y, 1e-6f), 0);
        r[2] = glm::vec4(glm::vec3(local[2]) / std::max(s.z, 1e-6f), 0);
        float z, y, x;
        glm::extractEulerAngleZYX(r, z, y, x);          // our rotation is Rz * Ry * Rx
        n.transform.position = glm::vec3(local[3]);
        n.transform.rotation = glm::degrees(glm::vec3(x, y, z));
        n.transform.scale = s;
    }

    SceneNode* convert(const Inst& in, SceneNode* parent) {
        const std::string& c = in.className;
        std::string name = in.str("Name", c);
        std::unique_ptr<SceneNode> node;

        if (isPartClass(c)) {
            node = std::make_unique<SceneNode>(name, NodeKind::Part);
            int shape = (int)in.num("shape", in.num("Shape", 1));
            PrimitiveType prim = PrimitiveType::Cube;
            if (c == "Part" || c == "SpawnLocation" || c == "Seat") {
                if (shape == 0) prim = PrimitiveType::Sphere;
                else if (shape == 2) prim = PrimitiveType::Cylinder;
                else if (shape == 3 || shape == 4) note("Wedges come in as blocks.");
            }
            if (c == "WedgePart" || c == "CornerWedgePart") note("Wedges come in as blocks.");
            if (c == "Seat" || c == "VehicleSeat") {   // you sit on it (whatever it's called)
                node->tags.push_back("Seat");
                if (in.flag("Disabled", false)) {
                    Attribute a; a.name = "Disabled"; a.type = Attribute::Bool; a.b = true;
                    node->attributes.push_back(a);
                }
            }
            if (c.find("Operation") != std::string::npos)
                note("Unions come in as blocks of the same size.");
            glm::vec3 size(4, 1, 2);
            if (const Value* v = in.get("size")) size = v->v;
            glm::vec3 pos(0.0f);
            glm::mat3 rot(1.0f);
            if (const Value* v = in.get("CFrame")) { pos = v->v; rot = v->r; }
            size *= kImportScale;
            pos *= kImportScale;
            // Its real shape: a MeshPart's mesh, or a SpecialMesh / BlockMesh / CylinderMesh inside it.
            MeshLook look = meshLook(in, name, prim, size, pos, rot);
            node->primitiveType = prim;
            node->mesh = MeshLibrary::get(prim);
            if (prim == PrimitiveType::Sphere && !look.special) size = glm::vec3(std::min({size.x, size.y, size.z}));
            if (prim == PrimitiveType::Cylinder && !look.special) {
                // Roblox cylinders lie along X; ours stand along Y.
                rot = rot * glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1)));
                size = glm::vec3(size.y, size.x, size.z);
            }
            place(*node, parent, pos, rot, size);
            if (const Value* v = in.get("Color3uint8")) node->color = v->v;
            else if (const Value* v2 = in.get("Color")) node->color = v2->v;
            node->transparency = (float)in.num("Transparency", 0.0);
            node->anchored = in.flag("Anchored", false);
            node->canCollide = in.flag("CanCollide", true);
            // Its collision group: by name (newer files) or by number (older ones).
            if (std::string cg = in.str("CollisionGroup"); !cg.empty())
                node->collisionGroup = cg.substr(0, CollisionGroups::kMaxNameLength);
            else if (int id = (int)in.num("CollisionGroupId", 0); id > 0 && id < CollisionGroups::kMax && !groups.names[id].empty())
                node->collisionGroup = groups.names[id];
            node->castShadow = in.flag("CastShadow", true);
            node->locked = in.flag("Locked", false);
            node->material = materialFromRoblox((int)in.num("Material", 256));
            if (const Value* ph = in.get("CustomPhysicalProperties"); ph && ph->kind == Value::Physics && ph->seq.size() == 5) {
                node->density          = std::clamp(ph->seq[0], 0.01f, 100.0f);
                node->friction         = std::clamp(ph->seq[1], 0.0f, 2.0f);
                node->elasticity       = std::clamp(ph->seq[2], 0.0f, 1.0f);
                node->frictionWeight   = std::clamp(ph->seq[3], 0.0f, 100.0f);
                node->elasticityWeight = std::clamp(ph->seq[4], 0.0f, 100.0f);
            }
            if (c == "SpawnLocation") {   // (renamed ones too: the tag says what it is)
                if (name != "SpawnLocation") node->tags.push_back("SpawnLocation");
                node->spawnNeutral = in.flag("Neutral", true);
                node->allowTeamChange = in.flag("AllowTeamChangeOnTouch", false);
                node->teamColor = brickColor((int)in.num("TeamColor", 194));
            }
            if (look.shape) MeshEdit::attach(*node, look.shape);
            if (!look.texture.empty()) {
                node->texture = look.texture;
                if (look.special) node->color = look.tint;   // a SpecialMesh's picture replaces the part's colour
            }
            ++report.parts;
        } else if (c == "IntValue" || c == "NumberValue" || c == "StringValue" || c == "BoolValue") {
            node = std::make_unique<SceneNode>(name, NodeKind::Value);
            node->intValue = c == "IntValue";
            node->value.type = c == "StringValue" ? Attribute::String : c == "BoolValue" ? Attribute::Bool : Attribute::Number;
            node->value.n = in.num("Value", 0.0);
            node->value.b = in.num("Value", 0.0) != 0.0;
            node->value.s = in.str("Value");
        } else if (GuiType gt; Guis::typeFromName(c, gt)) {
            // Game UI (ScreenGui, Frame, TextLabel, TextButton, TextBox, ImageLabel, ImageButton, ScrollingFrame,
            // UICorner, UIStroke, UIListLayout, UIGridLayout, UIPadding).
            node = std::make_unique<SceneNode>(name, NodeKind::Gui);
            node->gui.type = gt;
            Guis::setDefaults(*node);
            GuiProps& g = node->gui;
            auto u2 = [&](const char* k, UDim2& out) {
                if (const Value* v = in.get(k); v && v->kind == Value::UDim2) out = {v->q.x, v->q.y, v->q.z, v->q.w};
            };
            auto col = [&](const char* k, glm::vec3& out) { if (const Value* v = in.get(k)) out = v->v; };
            auto f = [&](const char* k, float& out) { if (in.get(k)) out = (float)in.num(k, out); };
            u2("Position", g.pos);
            u2("Size", g.size);
            if (const Value* v = in.get("AnchorPoint"); v && v->kind == Value::Vec2) g.anchor = {v->q.x, v->q.y};
            col("BackgroundColor3", g.bg);
            f("BackgroundTransparency", g.bgTransparency);
            col("BorderColor3", g.borderColor);
            g.border = (int)in.num("BorderSizePixel", g.border);
            g.zIndex = (int)in.num("ZIndex", g.zIndex);
            g.clips = in.flag("ClipsDescendants", false);
            node->visible = in.flag("Visible", true);
            g.layoutOrder = (int)in.num("LayoutOrder", 0);
            auto udim = [&](const char* k, float& s, float& o) {
                if (const Value* v = in.get(k); v && v->kind == Value::UDim) { s = v->q.x; o = v->q.y; }
            };
            // Roblox's enum numbers -> ours.
            if (isGuiLayout(gt)) {
                g.fill = (int)in.num("FillDirection", gt == GuiType::UIGridLayout ? 0 : 1) == 0 ? 1 : 0;   // Horizontal 0, Vertical 1
                const int ha = (int)in.num("HorizontalAlignment", 1), va = (int)in.num("VerticalAlignment", 1);   // Center 0, Left/Top 1, Right/Bottom 2
                g.hAlign = ha == 0 ? 1 : ha == 2 ? 2 : 0;
                g.vAlign = va == 0 ? 1 : va == 2 ? 2 : 0;
                g.sortByName = (int)in.num("SortOrder", 2) == 0;   // Name 0, LayoutOrder 2
                if (gt == GuiType::UIListLayout) { udim("Padding", g.padding.xs, g.padding.xo); g.padding.ys = g.padding.xs; g.padding.yo = g.padding.xo; }
                else {
                    u2("CellSize", g.cellSize);
                    u2("CellPadding", g.padding);
                    g.maxCells = std::max(0, (int)in.num("FillDirectionMaxCells", 0));
                }
            }
            if (gt == GuiType::UIPadding) {
                udim("PaddingLeft", g.padScale.x, g.padPx.x);
                udim("PaddingTop", g.padScale.y, g.padPx.y);
                udim("PaddingRight", g.padScale.z, g.padPx.z);
                udim("PaddingBottom", g.padScale.w, g.padPx.w);
            }
            if (gt == GuiType::ScrollingFrame) {
                u2("CanvasSize", g.canvasSize);
                if (const Value* v = in.get("CanvasPosition"); v && v->kind == Value::Vec2) g.canvasPos = {v->q.x, v->q.y};
                g.scrollBar = std::clamp((int)in.num("ScrollBarThickness", 12), 0, 100);
                col("ScrollBarImageColor3", g.scrollColor);
                f("ScrollBarImageTransparency", g.scrollTransparency);
                const int sd = (int)in.num("ScrollingDirection", 4);   // X 1, Y 2, XY 4
                g.scrollDir = sd == 1 ? 1 : sd == 2 ? 2 : 3;
                g.autoCanvas = std::clamp((int)in.num("AutomaticCanvasSize", 0), 0, 3);
                g.scrolling = in.flag("ScrollingEnabled", true);
            }
            if (gt == GuiType::TextBox) {
                g.placeholder = in.str("PlaceholderText");
                col("PlaceholderColor3", g.placeholderColor);
                g.clearOnFocus = in.flag("ClearTextOnFocus", true);
                g.editable = in.flag("TextEditable", true);
                g.multiLine = in.flag("MultiLine", false);
            }
            if (guiHasText(gt)) {
                g.text = in.str("Text");
                col("TextColor3", g.textColor);
                f("TextSize", g.textSize);
                g.textScaled = in.flag("TextScaled", false);
                g.textWrapped = in.flag("TextWrapped", false);
                int xa = (int)in.num("TextXAlignment", 2), ya = (int)in.num("TextYAlignment", 1);   // Roblox: Left 0, Right 1, Center 2
                g.xAlign = xa == 0 ? 0 : xa == 1 ? 2 : 1;
                g.yAlign = std::clamp(ya, 0, 2);
                f("TextTransparency", g.textTransparency);
                col("TextStrokeColor3", g.strokeColor);
                f("TextStrokeTransparency", g.strokeTransparency);
            }
            if (gt == GuiType::ImageLabel || gt == GuiType::ImageButton) {
                g.image = in.str("Image");   // rbxassetid:// pictures won't load here, but are kept
                col("ImageColor3", g.imageColor);
                f("ImageTransparency", g.imageTransparency);
                if (g.image.find("rbxasset") != std::string::npos) note("Roblox pictures can't be downloaded; pick one for " + name + ".");
            }
            if (gt == GuiType::TextButton || gt == GuiType::ImageButton) g.autoButtonColor = in.flag("AutoButtonColor", true);
            if (gt == GuiType::ScreenGui) { node->enabled = in.flag("Enabled", true); g.displayOrder = (int)in.num("DisplayOrder", 0); }
            if (gt == GuiType::BillboardGui || gt == GuiType::SurfaceGui) {   // (Roblox studs are twice ours)
                node->enabled = in.flag("Enabled", true);
                g.alwaysOnTop = in.flag("AlwaysOnTop", false);
                const double md = in.num("MaxDistance", 0.0);
                g.maxDistance = md > 0 && md < 1e29 ? (float)md * kImportScale : 0.0f;
                if (gt == GuiType::BillboardGui) {
                    g.size.xs *= kImportScale;
                    g.size.ys *= kImportScale;
                    g.studsOffset = {0, 0, 0};
                    if (const Value* v = in.get("StudsOffset"); v && v->kind == Value::Vec3) g.studsOffset = v->v * kImportScale;
                    if (const Value* v = in.get("StudsOffsetWorldSpace"); v && v->kind == Value::Vec3) g.worldOffset = v->v * kImportScale;
                } else {
                    const int face = (int)in.num("Face", 5);
                    g.face = face >= 0 && face < 6 ? face : 5;
                    if (const Value* v = in.get("CanvasSize"); v && v->kind == Value::Vec2) g.surfaceCanvas = glm::max(glm::vec2(1.0f), glm::vec2(v->q.x, v->q.y));
                    g.perStud = (int)in.num("SizingMode", 0) == 1;   // FixedSize 0, PixelsPerStud 1
                    g.pixelsPerStud = std::clamp((float)in.num("PixelsPerStud", 50.0) / kImportScale, 1.0f, 1000.0f);
                }
            }
            if (gt == GuiType::UICorner)
                if (const Value* v = in.get("CornerRadius"); v && v->kind == Value::UDim) g.corner = {v->q.x, v->q.y, 0, 0};
            if (gt == GuiType::UIStroke) {
                col("Color", g.borderColor);
                f("Thickness", g.thickness);
                g.bgTransparency = (float)in.num("Transparency", 0.0);
                node->enabled = in.flag("Enabled", true);
            }
            if (gt == GuiType::UIGradient) {
                if (const Value* v = in.get("Color"); v && v->kind == Value::ColorSeq && v->seq.size() >= 4) {
                    g.gradColor.clear();
                    for (size_t k = 0; k + 4 <= v->seq.size() && g.gradColor.size() < 20; k += 4)
                        g.gradColor.push_back({std::clamp(v->seq[k], 0.0f, 1.0f), glm::clamp(glm::vec3(v->seq[k + 1], v->seq[k + 2], v->seq[k + 3]), 0.0f, 1.0f)});
                }
                if (const Value* v = in.get("Transparency"); v && v->kind == Value::NumSeq && v->seq.size() >= 2) {
                    g.gradTransparency.clear();
                    for (size_t k = 0; k + 2 <= v->seq.size() && g.gradTransparency.size() < 20; k += 2)
                        g.gradTransparency.push_back({std::clamp(v->seq[k], 0.0f, 1.0f), std::clamp(v->seq[k + 1], 0.0f, 1.0f)});
                }
                f("Rotation", g.gradRotation);
                if (const Value* v = in.get("Offset"); v && v->kind == Value::Vec2) g.gradOffset = {v->q.x, v->q.y};
                node->enabled = in.flag("Enabled", true);
            }
            if (gt == GuiType::UIScale) g.scale = std::clamp((float)in.num("Scale", 1.0), 0.0f, 100.0f);
            if (gt == GuiType::UIAspectRatioConstraint) {
                g.aspectRatio = std::clamp((float)in.num("AspectRatio", 1.0), 0.001f, 1000.0f);
                g.aspectType = (int)in.num("AspectType", 0) == 1 ? 1 : 0;       // FitWithinMaxSize 0, ScaleWithParentSize 1
                g.dominantAxis = (int)in.num("DominantAxis", 0) == 1 ? 1 : 0;   // Width 0, Height 1
            }
            if (gt == GuiType::ViewportFrame) {   // (CurrentCamera is linked in linkRefs)
                col("Ambient", g.ambient);
                col("LightColor", g.lightColor);
                if (const Value* v = in.get("LightDirection"); v && v->kind == Value::Vec3) g.lightDir = v->v;
                col("ImageColor3", g.imageColor);
                f("ImageTransparency", g.imageTransparency);
            }
        } else if (c == "Camera" && parent && (parent->isViewportFrame() || parent->inViewportFrame())) {
            // A ViewportFrame's camera (the Workspace's own Camera isn't kept: each player has theirs).
            node = std::make_unique<SceneNode>(name, NodeKind::Camera);
            glm::vec3 pos(0.0f);
            glm::mat3 rot(1.0f);
            if (const Value* v = in.get("CFrame"); v && v->kind == Value::CFrame) { pos = v->v * kImportScale; rot = v->r; }
            place(*node, parent, pos, rot, glm::vec3(1.0f));
            node->fieldOfView = std::clamp((float)in.num("FieldOfView", 70.0), 1.0f, 120.0f);
            ++report.other;
        } else if (c == "WorldModel") {   // (a Model in a ViewportFrame)
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
            Attribute a; a.name = "RobloxClass"; a.type = Attribute::String; a.s = c;
            node->attributes.push_back(a);
            ++report.models;
        } else if (c == "Decal" || c == "Texture") {
            node = std::make_unique<SceneNode>(name, NodeKind::Decal);
            node->texture = in.str("Texture");   // rbxassetid:// links won't load here, but are kept
            int f = (int)in.num("Face", 5);
            node->face = (Face)(f >= 0 && f < 6 ? f : 5);
            if (const Value* v = in.get("Color3")) node->color = v->v;
            else node->color = {1, 1, 1};
            node->transparency = (float)in.num("Transparency", 0.0);
        } else if (c == "Tool" || c == "HopperBin") {
            node = std::make_unique<SceneNode>(name, NodeKind::Tool);
            node->enabled = in.flag("Enabled", true);
            node->canBeDropped = in.flag("CanBeDropped", true);
            node->toolTip = in.str("ToolTip");
            // Tool.Grip: how it sits in the hand (same maths as Roblox, see Player::updateGrip).
            if (const Value* g = in.get("Grip"); g && g->kind == Value::CFrame) {
                node->gripRot = g->r;
                node->gripPos = g->v * kImportScale;
            } else {
                node->gripRot = glm::mat3(1.0f);   // a Roblox tool with no Grip: Roblox's default
            }
            ++report.models;
        } else if (c == "Team") {   // Teams service: a Team is a model with its settings as attributes
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
            Attribute cls; cls.name = "RobloxClass"; cls.type = Attribute::String; cls.s = "Team";
            Attribute col; col.name = "TeamColor"; col.type = Attribute::Color3; col.v = brickColor((int)in.num("TeamColor", 1));
            Attribute aa; aa.name = "AutoAssignable"; aa.type = Attribute::Bool; aa.b = in.flag("AutoAssignable", true);
            node->attributes = {cls, col, aa};
            ++report.other;
        } else if (c == "Model" || c == "Folder" || c == "Configuration" || c == "Accessory") {
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
            ++report.models;
        } else if (c == "Script" || c == "LocalScript" || c == "ModuleScript") {
            node = std::make_unique<SceneNode>(name, NodeKind::Script);
            std::vector<std::string> notes;
            node->source = Luau::toLua(in.str("Source"), &notes);
            for (auto& n : notes) note(name + ": " + n);
            node->isModule = c == "ModuleScript";
            node->enabled = !in.flag("Disabled", false) && in.flag("Enabled", true);
            node->isLocal = c == "LocalScript";
            ++report.scripts;
        } else if (c == "ProximityPrompt") {
            node = std::make_unique<SceneNode>(name, NodeKind::Prompt);
            PromptProps& p = node->prompt;
            p.action = in.str("ActionText", "Interact");
            p.object = in.str("ObjectText");
            if (const Value* k = in.get("KeyboardKeyCode")) p.key = keyName((int)k->n);
            p.hold = (float)in.num("HoldDuration", 0.0);
            p.range = (float)in.num("MaxActivationDistance", 10.0) * kImportScale;
            p.lineOfSight = in.flag("RequiresLineOfSight", true);
            p.clickable = in.flag("ClickablePrompt", true);
            node->enabled = in.flag("Enabled", true);
            ++report.other;
        } else if (c == "ClickDetector") {
            node = std::make_unique<SceneNode>(name, NodeKind::ClickDetector);
            node->click.range = std::max(0.0f, (float)in.num("MaxActivationDistance", 32.0) * kImportScale);
            node->click.cursor = in.str("CursorIcon");   // (an rbxassetid:// picture won't load here, but is kept)
            ++report.other;
        } else if (c == "Trail" || c == "Beam") {
            const bool beam = c == "Beam";
            node = std::make_unique<SceneNode>(name, beam ? NodeKind::Beam : NodeKind::Trail);
            EffectProps& e = node->effect;
            if (const Value* v = in.get("Color"); v && v->kind == Value::ColorSeq && v->seq.size() >= 4) {
                e.color.clear();
                for (size_t k = 0; k + 4 <= v->seq.size(); k += 4)
                    e.color.push_back({std::clamp(v->seq[k], 0.0f, 1.0f), glm::clamp(glm::vec3(v->seq[k + 1], v->seq[k + 2], v->seq[k + 3]), 0.0f, 1.0f)});
            }
            auto numSeq = [&](const char* key, std::vector<NumberKey>& out, float lo, float hi) {
                const Value* v = in.get(key);
                if (!v || v->kind != Value::NumSeq || v->seq.size() < 2) return;
                out.clear();
                for (size_t k = 0; k + 2 <= v->seq.size(); k += 2)
                    out.push_back({std::clamp(v->seq[k], 0.0f, 1.0f), std::clamp(v->seq[k + 1], lo, hi)});
            };
            numSeq("Transparency", e.transparency, 0.0f, 1.0f);
            e.texture = in.str("Texture");
            e.lightEmission = std::clamp((float)in.num("LightEmission", 0.0), 0.0f, 1.0f);
            e.faceCamera = in.flag("FaceCamera", false);
            e.textureWrap = (int)in.num("TextureMode", 0.0) != 0;   // 0 Stretch, 1 Wrap, 2 Static
            e.textureLength = std::max(0.001f, (float)in.num("TextureLength", 1.0) * (e.textureWrap ? kImportScale : 1.0f));
            if (beam) {
                e.width0 = (float)in.num("Width0", 1.0) * kImportScale;
                e.width1 = (float)in.num("Width1", 1.0) * kImportScale;
                e.curve0 = (float)in.num("CurveSize0", 0.0) * kImportScale;
                e.curve1 = (float)in.num("CurveSize1", 0.0) * kImportScale;
                e.segments = std::clamp((int)in.num("Segments", 10.0), 1, 1000);
                e.textureSpeed = (float)in.num("TextureSpeed", 1.0);
            } else {
                e.lifetime = std::clamp((float)in.num("Lifetime", 2.0), 0.0f, 20.0f);
                e.minLength = (float)in.num("MinLength", 0.1) * kImportScale;
                e.maxLength = (float)in.num("MaxLength", 0.0) * kImportScale;
                numSeq("WidthScale", e.widthScale, 0.0f, 100.0f);
            }
            node->enabled = in.flag("Enabled", true);
            ++report.other;
        } else if (std::find(std::begin(kPostEffectClassNames), std::end(kPostEffectClassNames), c) != std::end(kPostEffectClassNames)) {
            // Screen effects (they work in Lighting; distances are in studs).
            node = std::make_unique<SceneNode>(name, NodeKind::PostEffect);
            PostEffectType t = PostEffectType::ColorCorrection;
            for (int i = 0; i < kPostEffectCount; ++i) if (c == kPostEffectClassNames[i]) t = (PostEffectType)i;
            PostEffectProps& p = node->post;
            p = postEffectDefaults(t);
            p.brightness = (float)in.num("Brightness", p.brightness);
            p.contrast   = (float)in.num("Contrast", p.contrast);
            p.saturation = (float)in.num("Saturation", p.saturation);
            if (const Value* v = in.get("TintColor")) p.tint = v->v;
            p.intensity  = (float)in.num("Intensity", p.intensity);
            p.spread     = (float)in.num("Spread", p.spread);
            p.size       = (float)in.num("Size", p.size);
            p.threshold  = (float)in.num("Threshold", p.threshold);
            p.farIntensity  = (float)in.num("FarIntensity", p.farIntensity);
            p.nearIntensity = (float)in.num("NearIntensity", p.nearIntensity);
            p.focusDistance = (float)in.num("FocusDistance", 0.05) * kImportScale;
            p.inFocusRadius = (float)in.num("InFocusRadius", 10.0) * kImportScale;
            clampPostEffect(p);
            node->enabled = in.flag("Enabled", true);
            ++report.other;
        } else if (c == "Sky") {
            node = std::make_unique<SceneNode>(name, NodeKind::Sky);
            SkyProps& k = node->sky;
            // Roblox's own built-in pictures can't load here: "" = our own sky / sun / moon.
            auto pic = [&](const char* prop) {
                std::string v = in.str(prop);
                return v.rfind("rbxasset://", 0) == 0 ? std::string() : v;
            };
            k.bk = pic("SkyboxBk"); k.dn = pic("SkyboxDn"); k.ft = pic("SkyboxFt");
            k.lf = pic("SkyboxLf"); k.rt = pic("SkyboxRt"); k.up = pic("SkyboxUp");
            k.sun = pic("SunTextureId"); k.moon = pic("MoonTextureId");
            k.starCount = (int)in.num("StarCount", 3000);
            k.celestial = in.flag("CelestialBodiesShown", true);
            k.sunSize = (float)in.num("SunAngularSize", 21);
            k.moonSize = (float)in.num("MoonAngularSize", 11);
            clampSky(k);
            ++report.other;
        } else if (c == "ParticleEmitter" || c == "Fire" || c == "Smoke" || c == "Sparkles") {
            node = std::make_unique<SceneNode>(name, NodeKind::Particles);
            ParticleType type = ParticleType::ParticleEmitter;
            for (int i = 0; i < kParticleTypeCount; ++i) if (c == kParticleClassNames[i]) type = (ParticleType)i;
            ParticleProps& p = node->particle;
            p = particleDefaults(type);
            auto color = [&](const char* key, glm::vec3 fb) {
                const Value* v = in.get(key);
                return v && v->kind == Value::Color ? glm::clamp(v->v, 0.0f, 1.0f) : fb;
            };
            auto range = [&](const char* key, glm::vec2 fb, float scale) {
                const Value* v = in.get(key);
                if (v && v->kind == Value::Vec2) return glm::vec2(v->q.x, std::max(v->q.x, v->q.y)) * scale;
                if (v && v->kind == Value::Num) return glm::vec2((float)v->n) * scale;
                return fb * scale;
            };
            auto numSeq = [&](const char* key, std::vector<NumberKey>& out, float lo, float hi, float scale) {
                const Value* v = in.get(key);
                if (!v || v->kind != Value::NumSeq || v->seq.size() < 2) { for (NumberKey& k : out) k.v *= scale; return; }
                out.clear();
                for (size_t k = 0; k + 2 <= v->seq.size(); k += 2)
                    out.push_back({std::clamp(v->seq[k], 0.0f, 1.0f), std::clamp(v->seq[k + 1] * scale, lo, hi)});
            };
            switch (type) {
                case ParticleType::Fire:
                    p.color1 = color("Color", p.color1);
                    p.color2 = color("SecondaryColor", p.color2);
                    p.fxSize = std::clamp((float)in.num("size_xml", in.num("Size", 5.0)) * kImportScale, 2.0f, 30.0f);
                    p.rise   = std::clamp((float)in.num("heat_xml", in.num("Heat", 9.0)) * kImportScale, -25.0f, 25.0f);
                    break;
                case ParticleType::Smoke:
                    p.color1  = color("Color", p.color1);
                    p.opacity = std::clamp((float)in.num("opacity_xml", in.num("Opacity", 0.5)), 0.0f, 1.0f);
                    p.fxSize  = std::clamp((float)in.num("size_xml", in.num("Size", 1.0)) * kImportScale, 0.1f, 100.0f);
                    p.rise    = std::clamp((float)in.num("riseVelocity_xml", in.num("RiseVelocity", 1.0)) * kImportScale, -25.0f, 25.0f);
                    break;
                case ParticleType::Sparkles:
                    p.color1 = color("SparkleColor", color("Color", p.color1));
                    break;
                default: {
                    p.rate     = std::clamp((float)in.num("Rate", 20.0), 0.0f, 1e6f);
                    p.lifetime = glm::clamp(range("Lifetime", p.lifetime, 1.0f), 0.0f, 1000.0f);
                    p.speed    = range("Speed", p.speed, kImportScale);
                    if (const Value* v = in.get("SpreadAngle"); v && v->kind == Value::Vec2) p.spread = {v->q.x, v->q.y};
                    p.rotation = range("Rotation", p.rotation, 1.0f);
                    p.rotSpeed = range("RotSpeed", p.rotSpeed, 1.0f);
                    if (const Value* v = in.get("Color"); v && v->kind == Value::ColorSeq && v->seq.size() >= 4) {
                        p.color.clear();
                        for (size_t k = 0; k + 4 <= v->seq.size(); k += 4)
                            p.color.push_back({std::clamp(v->seq[k], 0.0f, 1.0f), glm::clamp(glm::vec3(v->seq[k + 1], v->seq[k + 2], v->seq[k + 3]), 0.0f, 1.0f)});
                    }
                    numSeq("Size", p.size, 0.0f, 1000.0f, kImportScale);
                    numSeq("Transparency", p.transparency, 0.0f, 1.0f, 1.0f);
                    p.texture = particleTexture(in.str("Texture", "rbxasset://textures/particles/sparkles_main.dds"));
                    p.lightEmission = std::clamp((float)in.num("LightEmission", 0.0), 0.0f, 1.0f);
                    if (const Value* v = in.get("Acceleration"); v && v->kind == Value::Vec3) p.acceleration = v->v * kImportScale;
                    p.drag = std::clamp((float)in.num("Drag", 0.0), -100.0f, 100.0f);
                    p.direction = (Face)std::clamp((int)in.num("EmissionDirection", 1.0), 0, 5);   // NormalId: Right, Top, Back, Left, Bottom, Front
                    p.locked = in.flag("LockedToPart", false);
                    break;
                }
            }
            node->enabled = in.flag("Enabled", true);
            ++report.other;
        } else if (c == "Highlight") {
            node = std::make_unique<SceneNode>(name, NodeKind::Highlight);
            HighlightProps& h = node->highlight;
            if (const Value* v = in.get("FillColor"))    h.fill = glm::clamp(v->v, 0.0f, 1.0f);
            if (const Value* v = in.get("OutlineColor")) h.outline = glm::clamp(v->v, 0.0f, 1.0f);
            h.fillTransparency = std::clamp((float)in.num("FillTransparency", 0.5), 0.0f, 1.0f);
            h.outlineTransparency = std::clamp((float)in.num("OutlineTransparency", 0.0), 0.0f, 1.0f);
            h.onTop = (int)in.num("DepthMode", 0) == 0;   // 0 AlwaysOnTop, 1 Occluded
            node->enabled = in.flag("Enabled", true);
            ++report.other;
        } else if (c == "RemoteEvent" || c == "RemoteFunction" || c == "UnreliableRemoteEvent") {
            node = std::make_unique<SceneNode>(name, NodeKind::Remote);
            node->remoteFunction = c == "RemoteFunction";
            ++report.other;
        } else if (c == "PointLight" || c == "SpotLight" || c == "SurfaceLight") {
            node = std::make_unique<SceneNode>(name, NodeKind::Light);
            node->lightType = c == "PointLight" ? LightType::Point : LightType::Spot;
            if (const Value* v = in.get("Color")) node->color = v->v;
            node->brightness = (float)in.num("Brightness", 1.0) * 2.0f;
            node->range = (float)in.num("Range", 8.0) * kImportScale;
            node->spotAngle = (float)in.num("Angle", 90.0);
            node->enabled = in.flag("Enabled", true);
            ++report.lights;
        } else if (c == "Sound") {
            node = std::make_unique<SceneNode>(name, NodeKind::Sound);
            std::string id = in.str("SoundId");
            node->soundId = id.find("rbxasset") != std::string::npos ? "" : id;
            if (!id.empty() && node->soundId.empty()) note("Roblox sound ids can't be downloaded; pick a sound in Properties.");
            node->volume = (float)in.num("Volume", 0.5);
            node->pitch = (float)in.num("PlaybackSpeed", 1.0);
            node->looped = in.flag("Looped", false);
            node->autoplay = in.flag("Playing", false);
            ++report.sounds;
        } else if (c == "Attachment") {
            node = std::make_unique<SceneNode>(name, NodeKind::Attachment);
            glm::vec3 pos(0.0f);
            glm::mat3 rot(1.0f);
            if (const Value* v = in.get("CFrame")) { pos = v->v * kImportScale; rot = v->r; }
            // Attachments are placed relative to their part (in studs).
            glm::vec3 psize = parent ? parent->transform.scale : glm::vec3(1.0f);
            node->transform.position = pos / glm::max(psize, glm::vec3(1e-4f));
            float z, y, x;
            glm::extractEulerAngleZYX(glm::mat4(rot), z, y, x);
            node->transform.rotation = glm::degrees(glm::vec3(x, y, z));
            ++report.constraints;
        } else if (c == "RopeConstraint" || c == "RodConstraint" || c == "SpringConstraint" || c == "HingeConstraint" ||
                   c == "WeldConstraint" || c == "Weld" || c == "ManualWeld" || c == "Snap") {
            node = std::make_unique<SceneNode>(name, NodeKind::Constraint);
            if (c == "RopeConstraint")   node->constraintType = ConstraintType::Rope;
            if (c == "RodConstraint")    node->constraintType = ConstraintType::Rod;
            if (c == "SpringConstraint") node->constraintType = ConstraintType::Spring;
            if (c == "HingeConstraint")  node->constraintType = ConstraintType::Hinge;
            if (c == "WeldConstraint" || c == "Weld" || c == "ManualWeld" || c == "Snap") node->constraintType = ConstraintType::Weld;
            node->length = c == "SpringConstraint" ? (float)in.num("FreeLength", 1.0) * kImportScale
                                                   : (float)in.num("Length", -2.0) * kImportScale;
            node->stiffness = (float)in.num("Stiffness", 200.0);
            node->damping = (float)in.num("Damping", 5.0);
            node->thickness = (float)in.num("Thickness", 0.2) * kImportScale;
            if (c == "HingeConstraint" && (int)in.num("ActuatorType", 0) == 1) {
                node->motorSpeed = (float)in.num("AngularVelocity", 0.0);
                node->motorTorque = std::min(20000.0f, (float)in.num("MotorMaxTorque", 1000.0));
            }
            node->enabled = in.flag("Enabled", true);
            node->visible = in.flag("Visible", c == "RopeConstraint" || c == "RodConstraint" || c == "SpringConstraint");
            node->color = {0.45f, 0.32f, 0.2f};
            ++report.constraints;
        } else if (c == "BallSocketConstraint" || c == "PrismaticConstraint" || c == "CylindricalConstraint") {
            // Joints: Roblox's numbers are turned into ours (studs are halved, forces scaled).
            node = std::make_unique<SceneNode>(name, NodeKind::Constraint);
            node->constraintType = c == "BallSocketConstraint" ? ConstraintType::BallSocket
                                 : c == "PrismaticConstraint"  ? ConstraintType::Prismatic : ConstraintType::Cylindrical;
            auto big = [](double v) { return std::isfinite(v) ? (float)std::clamp(v, -1e30, 1e30) : (v > 0 ? 1e30f : -1e30f); };
            auto act = [&](const char* k) { return (Actuator)std::clamp((int)in.num(k, 0), 0, 2); };
            JointProps& p = node->joint;
            p.limitsEnabled = in.flag("LimitsEnabled", false);
            p.restitution = std::clamp((float)in.num("Restitution", 0.0), 0.0f, 1.0f);
            if (node->constraintType == ConstraintType::BallSocket) {
                p.upperAngle = (float)in.num("UpperAngle", 45.0);
                p.twistLimitsEnabled = in.flag("TwistLimitsEnabled", false);
                p.twistLowerAngle = (float)in.num("TwistLowerAngle", -45.0);
                p.twistUpperAngle = (float)in.num("TwistUpperAngle", 45.0);
            } else {
                p.lowerLimit = (float)in.num("LowerLimit", 0.0) * kImportScale;
                p.upperLimit = (float)in.num("UpperLimit", 5.0) * kImportScale;
                p.actuator = act("ActuatorType");
                p.motorMaxForce = big(in.num("MotorMaxForce", 0.0) * kForceScale);
                p.velocity = (float)in.num("Velocity", 0.0) * kVelScale;
                p.speed = big(in.num("Speed", 0.0) * kVelScale);
                p.servoMaxForce = big(in.num("ServoMaxForce", 0.0) * kForceScale);
                p.targetPosition = (float)in.num("TargetPosition", 0.0) * kImportScale;
            }
            if (node->constraintType == ConstraintType::Cylindrical) {
                p.angularActuator = act("AngularActuatorType");
                p.angularVelocity = (float)in.num("AngularVelocity", 0.0) * kSpinScale;
                p.motorMaxTorque = big(in.num("MotorMaxTorque", 0.0) * kTorqueScale);
                p.angularSpeed = big(in.num("AngularSpeed", 0.0) * kSpinScale);
                p.targetAngle = (float)in.num("TargetAngle", 0.0);
                p.servoMaxTorque = big(in.num("ServoMaxTorque", 0.0) * kTorqueScale);
                p.angularLimitsEnabled = in.flag("AngularLimitsEnabled", false);
                p.lowerAngle = (float)in.num("LowerAngle", -45.0);
                p.upperAngle = (float)in.num("UpperAngle", 45.0);
                p.inclinationAngle = (float)in.num("InclinationAngle", 0.0);
            }
            node->enabled = in.flag("Enabled", true);
            node->visible = in.flag("Visible", false);
            node->thickness = 0.1f;
            node->color = {0.6f, 0.62f, 0.66f};
            ++report.constraints;
        } else if (auto mt = std::find(std::begin(kMoverClassNames), std::end(kMoverClassNames), c); mt != std::end(kMoverClassNames)) {
            // BodyVelocity, BodyGyro... and LinearVelocity, AlignPosition...: classic games'
            // vehicles and flying keep working. (Roblox's numbers are turned into ours.)
            node = std::make_unique<SceneNode>(name, NodeKind::Mover);
            const MoverType t = (MoverType)(mt - std::begin(kMoverClassNames));
            node->mover = moverDefaults(t);
            MoverProps& m = node->mover;
            auto v3 = [&](const char* k, glm::vec3 fb) {
                const Value* v = in.get(k);
                return v && v->kind == Value::Vec3 ? v->v : fb;
            };
            auto big = [](float v) { return std::isfinite(v) ? std::clamp(v, -1e30f, 1e30f) : (v > 0 ? 1e30f : -1e30f); };
            auto big3 = [&](glm::vec3 v) { for (int i = 0; i < 3; ++i) v[i] = big(v[i]); return v; };
            auto turn = [&](const char* k) {
                glm::vec3 deg(0.0f);
                if (const Value* v = in.get(k); v && v->kind == Value::CFrame) {
                    float z, y, x;
                    glm::extractEulerAngleZYX(glm::mat4(v->r), z, y, x);
                    deg = glm::degrees(glm::vec3(x, y, z));
                }
                return deg;
            };
            const float rbxMaxF = (float)in.num("MaxForce", 0.0);
            switch (t) {
                case MoverType::BodyVelocity:
                    m.value = v3("Velocity", glm::vec3(0, 2, 0)) * kVelScale;
                    m.maxAxes = big3(v3("MaxForce", glm::vec3(4000)) * kForceScale);
                    break;
                case MoverType::BodyPosition:
                    m.value = v3("Position", glm::vec3(0, 50, 0)) * kImportScale;
                    m.maxAxes = big3(v3("MaxForce", glm::vec3(4000)) * kForceScale);
                    m.p = big((float)in.num("P", 10000.0) * kForceScale / kImportScale);
                    m.d = big((float)in.num("D", 1250.0) * kForceScale / kVelScale);
                    break;
                case MoverType::BodyGyro:
                    m.rotation = turn("CFrame");
                    m.maxAxes = big3(v3("MaxTorque", glm::vec3(400000, 0, 400000)) * kTorqueScale);
                    m.p = big((float)in.num("P", 3000.0) * kTorqueScale);
                    m.d = big((float)in.num("D", 500.0) * kTorqueScale / kSpinScale);
                    break;
                case MoverType::BodyAngularVelocity:
                    m.value = v3("AngularVelocity", glm::vec3(0, 2, 0)) * kSpinScale;
                    m.maxAxes = big3(v3("MaxTorque", glm::vec3(4000)) * kTorqueScale);
                    break;
                case MoverType::BodyThrust:
                    m.value = v3("Force", glm::vec3(0)) * kForceScale;
                    m.location = v3("Location", glm::vec3(0)) * kImportScale;
                    break;
                case MoverType::BodyForce:
                case MoverType::VectorForce:
                    m.value = v3("Force", t == MoverType::VectorForce ? glm::vec3(1000, 0, 0) : glm::vec3(0)) * kForceScale;
                    m.atCenterOfMass = in.flag("ApplyAtCenterOfMass", false);
                    break;
                case MoverType::Torque:
                    m.value = v3("Torque", glm::vec3(1000, 0, 0)) * kTorqueScale;
                    break;
                case MoverType::LinearVelocity:
                    m.value = v3("VectorVelocity", glm::vec3(0)) * kVelScale;
                    m.maxForce = big((rbxMaxF > 0 ? rbxMaxF : 1000.0f) * kForceScale);
                    break;
                case MoverType::AngularVelocity:
                    m.value = v3("AngularVelocity", glm::vec3(0)) * kSpinScale;
                    m.maxForce = big((float)in.num("MaxTorque", 1000.0) * kTorqueScale);
                    break;
                case MoverType::AlignPosition:
                case MoverType::AlignOrientation: {
                    const bool pos = t == MoverType::AlignPosition;
                    if (pos) m.value = v3("Position", glm::vec3(0)) * kImportScale;
                    else m.rotation = turn("CFrame");
                    m.maxForce = big(pos ? (rbxMaxF > 0 ? rbxMaxF : 10000.0f) * kForceScale : (float)in.num("MaxTorque", 10000.0) * kTorqueScale);
                    const float mv = big((float)in.num(pos ? "MaxVelocity" : "MaxAngularVelocity", 1e30));
                    m.maxVelocity = mv >= 1e29f ? 0.0f : mv * (pos ? kVelScale : kSpinScale);
                    m.responsiveness = (float)in.num("Responsiveness", 10.0) * kSpinScale;
                    m.rigid = in.flag("RigidityEnabled", false);
                    break;
                }
            }
            if (!isBodyMover(t) && t != MoverType::AlignPosition && t != MoverType::AlignOrientation)   // RelativeTo: 0 Attachment0, 1 Attachment1, 2 World
                m.relativeToAttachment = (int)in.num("RelativeTo", t == MoverType::VectorForce || t == MoverType::Torque ? 0 : 2) != 2;
            node->enabled = in.flag("Enabled", true);
            ++report.constraints;
        } else if (c == "PathfindingModifier") {
            // Becomes attributes on its part, which the navmesh reads the same way.
            if (parent) {
                if (std::string label = in.str("Label"); !label.empty()) {
                    Attribute a; a.name = "PathfindingLabel"; a.type = Attribute::String; a.s = label;
                    parent->attributes.push_back(a);
                }
                if (in.flag("PassThrough", false)) {
                    Attribute a; a.name = "PathfindingPassThrough"; a.type = Attribute::Bool; a.b = true;
                    parent->attributes.push_back(a);
                }
            }
            ++report.other;
            return nullptr;
        } else if ((c == "SpecialMesh" || c == "FileMesh" || c == "BlockMesh" || c == "CylinderMesh") && parent) {
            // The part already took its shape from it (see meshLook); scripts still look
            // for it (Handle.Mesh), so keep a stand-in with its properties as attributes.
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
            auto attrS = [&](const char* k, const std::string& v) {
                Attribute a; a.name = k; a.type = Attribute::String; a.s = v; node->attributes.push_back(a);
            };
            auto attrV = [&](const char* k, glm::vec3 v, Attribute::Type t) {
                Attribute a; a.name = k; a.type = t; a.v = v; node->attributes.push_back(a);
            };
            attrS("RobloxClass", c);
            attrS("MeshId", in.str("MeshId"));
            attrS("TextureId", in.str("TextureId"));
            attrV("Scale", in.get("Scale") ? in.get("Scale")->v : glm::vec3(1.0f), Attribute::Vector3);
            attrV("Offset", in.get("Offset") ? in.get("Offset")->v : glm::vec3(0.0f), Attribute::Vector3);
            attrV("VertexColor", in.get("VertexColor") ? in.get("VertexColor")->v : glm::vec3(1.0f), Attribute::Vector3);
            { Attribute a; a.name = "MeshType"; a.type = Attribute::Number; a.n = in.num("MeshType", 5); node->attributes.push_back(a); }
        } else if (c == "Humanoid" || c == "Decal" || c == "Texture" || c == "SpecialMesh" || c == "TouchTransmitter" ||
                   c.find("Value") != std::string::npos || c == "Camera" || c == "Terrain") {
            if (c == "Decal" || c == "Texture") note("Decals and textures aren't supported yet.");
            if (c.find("Value") != std::string::npos && c != "Value") {
                // IntValue / StringValue ... become attributes on the parent.
                if (parent) {
                    Attribute a;
                    a.name = name;
                    const Value* v = in.get("Value");
                    if (v && v->kind == Value::Str) { a.type = Attribute::String; a.s = v->s; }
                    else if (v && v->kind == Value::Bool) { a.type = Attribute::Bool; a.b = v->b; }
                    else if (v && (v->kind == Value::Num || v->kind == Value::Token)) { a.type = Attribute::Number; a.n = v->n; }
                    else if (v && v->kind == Value::Vec3) { a.type = Attribute::Vector3; a.v = v->v; }
                    else if (v && v->kind == Value::Color) { a.type = Attribute::Color3; a.v = v->v; }
                    else return nullptr;
                    parent->attributes.push_back(a);
                    note("Value objects (IntValue, ...) become attributes on their parent.");
                }
            }
            ++report.other;
            return nullptr;
        } else {
            ++report.other;
            // Unknown containers still keep their children (e.g. a Folder-like service).
            if (in.children.empty()) return nullptr;
            node = std::make_unique<SceneNode>(name, NodeKind::Model);
        }

        if (const Value* t = in.get("Tags"); t && t->kind == Value::Str) {
            std::string cur;
            for (char ch : t->s) { if (ch == '\0') { if (!cur.empty()) node->tags.push_back(cur); cur.clear(); } else cur += ch; }
            if (!cur.empty()) node->tags.push_back(cur);
        }
        if (const Value* a = in.get("AttributesSerialize"); a && a->kind == Value::Str) readAttributes(a->s, *node);

        SceneNode* raw = parent ? parent->addChild(std::move(node)) : scene.insert(std::move(node));
        made[&in] = raw;
        for (const Inst* k : in.children) convert(*k, raw);
        return raw;
    }

    // Constraints point at attachments / parts: fill in the ids once everything exists.
    void linkRefs(const Document& doc) {
        std::unordered_map<int64_t, SceneNode*> byRef;
        for (auto& [inst, node] : made) byRef[inst->referent] = node;
        for (auto& [inst, node] : made) {
            if (node->isEffect()) {   // (a Trail's / Beam's Attachments)
                auto att = [&](const char* key) -> uint64_t {
                    const Value* v = inst->get(key);
                    if (!v || v->kind != Value::Ref) return 0;
                    auto it = byRef.find(v->ref);
                    return it == byRef.end() ? 0 : it->second->id;
                };
                node->effect.a0 = att("Attachment0");
                node->effect.a1 = att("Attachment1");
                continue;
            }
            if (node->kind == NodeKind::Model) {   // (Model.PrimaryPart)
                const Value* v = inst->get("PrimaryPart");
                if (v && v->kind == Value::Ref)
                    if (auto it = byRef.find(v->ref); it != byRef.end() && it->second->isPart()) node->primaryPart = it->second->id;
                continue;
            }
            if (node->isHighlight()) {   // (its Adornee)
                const Value* v = inst->get("Adornee");
                if (v && v->kind == Value::Ref)
                    if (auto it = byRef.find(v->ref); it != byRef.end()) node->highlight.adornee = it->second->id;
                continue;
            }
            if (node->isViewportFrame()) {   // (its CurrentCamera)
                const Value* v = inst->get("CurrentCamera");
                if (v && v->kind == Value::Ref)
                    if (auto it = byRef.find(v->ref); it != byRef.end() && it->second->isCamera()) node->gui.camera = it->second->id;
                continue;
            }
            if (!node->isConstraint() && !node->isMover()) continue;
            auto ref = [&](const char* a, const char* b) -> uint64_t {
                const Value* v = inst->get(a);
                if (!v) v = inst->get(b);
                if (!v || v->kind != Value::Ref) return 0;
                auto it = byRef.find(v->ref);
                return it == byRef.end() ? 0 : it->second->id;
            };
            if (node->isMover()) {
                node->ref0 = ref("Attachment0", "attachment0");
                node->ref1 = (int)inst->num("Mode", 1) == 0 ? 0 : ref("Attachment1", "attachment1");   // (OneAttachment: no Attachment1)
            } else if (node->constraintType == ConstraintType::Weld) { node->ref0 = ref("Part0", "part0"); node->ref1 = ref("Part1", "part1"); }
            else { node->ref0 = ref("Attachment0", "attachment0"); node->ref1 = ref("Attachment1", "attachment1"); }
        }
        (void)doc;
    }
};

const Inst* findChild(const std::vector<Inst*>& list, const char* cls) {
    for (const Inst* i : list) if (i->className == cls) return i;
    return nullptr;
}

// ===========================================================================
// Guts and Bolts objects -> Roblox XML
// ===========================================================================

struct XmlWriter {
    std::ostringstream o;
    Scene& scene;
    explicit XmlWriter(Scene& s) : scene(s) {}

    static std::string esc(const std::string& s) {
        std::string out;
        for (char c : s) {
            switch (c) {
                case '<': out += "&lt;"; break; case '>': out += "&gt;"; break;
                case '&': out += "&amp;"; break; case '"': out += "&quot;"; break;
                default: out += c;
            }
        }
        return out;
    }
    static std::string ref(uint64_t id) {
        char b[40]; std::snprintf(b, sizeof(b), "RBX%016llX", (unsigned long long)id); return b;
    }
    void str(const char* n, const std::string& v) { o << "<string name=\"" << n << "\">" << esc(v) << "</string>\n"; }
    void boolean(const char* n, bool v) { o << "<bool name=\"" << n << "\">" << (v ? "true" : "false") << "</bool>\n"; }
    void flt(const char* n, double v) { o << "<float name=\"" << n << "\">" << v << "</float>\n"; }
    void token(const char* n, int v) { o << "<token name=\"" << n << "\">" << v << "</token>\n"; }
    void vec3(const char* n, glm::vec3 v) {
        o << "<Vector3 name=\"" << n << "\"><X>" << v.x << "</X><Y>" << v.y << "</Y><Z>" << v.z << "</Z></Vector3>\n";
    }
    void udim2(const char* n, const UDim2& u) {
        o << "<UDim2 name=\"" << n << "\"><XS>" << u.xs << "</XS><XO>" << (int)std::lround(u.xo) << "</XO><YS>" << u.ys
          << "</YS><YO>" << (int)std::lround(u.yo) << "</YO></UDim2>\n";
    }
    void vec2(const char* n, glm::vec2 v) {
        o << "<Vector2 name=\"" << n << "\"><X>" << v.x << "</X><Y>" << v.y << "</Y></Vector2>\n";
    }
    void color3(const char* n, glm::vec3 c) {
        o << "<Color3 name=\"" << n << "\"><R>" << c.r << "</R><G>" << c.g << "</G><B>" << c.b << "</B></Color3>\n";
    }
    void color3u8(const char* n, glm::vec3 c) {
        auto b = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        uint32_t v = 0xFF000000u | b(c.r) << 16 | b(c.g) << 8 | b(c.b);
        o << "<Color3uint8 name=\"" << n << "\">" << v << "</Color3uint8>\n";
    }
    void cframe(const char* n, glm::vec3 p, glm::mat3 r) {
        o << "<CoordinateFrame name=\"" << n << "\"><X>" << p.x << "</X><Y>" << p.y << "</Y><Z>" << p.z << "</Z>";
        const char* names[9] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col) o << "<" << names[row * 3 + col] << ">" << r[col][row] << "</" << names[row * 3 + col] << ">";
        o << "</CoordinateFrame>\n";
    }
    void refProp(const char* n, uint64_t id) {
        o << "<Ref name=\"" << n << "\">" << (id ? ref(id) : std::string("null")) << "</Ref>\n";
    }
    void binary(const char* n, const std::string& data) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3) {
            uint32_t v = (uint8_t)data[i] << 16 | (uint8_t)data[i + 1] << 8 | (uint8_t)data[i + 2];
            out += chars[v >> 18]; out += chars[(v >> 12) & 63]; out += chars[(v >> 6) & 63]; out += chars[v & 63];
        }
        if (i < data.size()) {
            uint32_t v = (uint8_t)data[i] << 16 | (i + 1 < data.size() ? (uint8_t)data[i + 1] << 8 : 0);
            out += chars[v >> 18]; out += chars[(v >> 12) & 63];
            out += i + 1 < data.size() ? chars[(v >> 6) & 63] : '=';
            out += '=';
        }
        o << "<BinaryString name=\"" << n << "\">" << out << "</BinaryString>\n";
    }

    static std::string attributesBlob(const SceneNode& n) {
        std::string b;
        auto u32 = [&](uint32_t v) { b.append((const char*)&v, 4); };
        auto f32 = [&](float v) { b.append((const char*)&v, 4); };
        u32((uint32_t)n.attributes.size());
        for (const Attribute& a : n.attributes) {
            u32((uint32_t)a.name.size()); b += a.name;
            switch (a.type) {
                case Attribute::String:  b += (char)0x02; u32((uint32_t)a.s.size()); b += a.s; break;
                case Attribute::Bool:    b += (char)0x03; b += (char)(a.b ? 1 : 0); break;
                case Attribute::Number:  b += (char)0x06; b.append((const char*)&a.n, 8); break;
                case Attribute::Color3:  b += (char)0x0F; f32(a.v.x); f32(a.v.y); f32(a.v.z); break;
                case Attribute::Vector3: b += (char)0x11; f32(a.v.x); f32(a.v.y); f32(a.v.z); break;
            }
        }
        return b;
    }

    void common(const SceneNode& n) {
        str("Name", n.name);
        if (!n.attributes.empty()) binary("AttributesSerialize", attributesBlob(n));
        if (!n.tags.empty()) {
            std::string t;
            for (size_t i = 0; i < n.tags.size(); ++i) { if (i) t += '\0'; t += n.tags[i]; }
            binary("Tags", t);
        }
    }

    void item(const SceneNode& n) {
        if (n.internal || scene.isCharacterPart(&n) || scene.isProtected(&n)) return;
        const char* cls = nullptr;
        switch (n.kind) {
            case NodeKind::Part:       cls = n.isSpawnLocation() ? "SpawnLocation" : "Part"; break;
            case NodeKind::Model: {
                const Attribute* rc = n.findAttribute("RobloxClass");
                cls = rc && rc->type == Attribute::String && rc->s == "WorldModel" ? "WorldModel" : "Model";
                break;
            }
            case NodeKind::Camera:     cls = "Camera"; break;
            case NodeKind::Script:     cls = n.isModule ? "ModuleScript" : n.isLocal ? "LocalScript" : "Script"; break;
            case NodeKind::Remote:     cls = n.remoteFunction ? "RemoteFunction" : "RemoteEvent"; break;
            case NodeKind::Prompt:     cls = "ProximityPrompt"; break;
            case NodeKind::Highlight:  cls = "Highlight"; break;
            case NodeKind::PostEffect: cls = kPostEffectClassNames[(int)n.post.type]; break;
            case NodeKind::Sky:        cls = "Sky"; break;
            case NodeKind::ClickDetector: cls = "ClickDetector"; break;
            case NodeKind::Trail:      cls = "Trail"; break;
            case NodeKind::Beam:       cls = "Beam"; break;
            case NodeKind::Particles:  cls = n.particleClass(); break;
            case NodeKind::Light:      cls = n.lightType == LightType::Spot ? "SpotLight" : "PointLight"; break;
            case NodeKind::Sound:      cls = "Sound"; break;
            case NodeKind::Attachment: cls = "Attachment"; break;
            case NodeKind::ForceField: cls = "ForceField"; break;
            case NodeKind::Tool:       cls = "Tool"; break;
            case NodeKind::Decal:      cls = "Decal"; break;
            case NodeKind::Animation:  cls = nullptr; break;   // (Roblox keeps animations online)
            case NodeKind::FluidSystem: case NodeKind::FluidEmitter: cls = nullptr; break;   // (Guts&Bolts only)
            case NodeKind::Mover:      cls = kMoverClassNames[(int)n.mover.type]; break;
            case NodeKind::Gui:        cls = kGuiClassNames[(int)n.gui.type]; break;
            case NodeKind::Value:
                cls = n.value.type == Attribute::Vector3 || n.value.type == Attribute::Color3 ? nullptr : n.valueClass();
                break;
            case NodeKind::Constraint: {
                cls = kConstraintClassNames[(int)n.constraintType];
                break;
            }
        }
        if (!cls) return;
        o << "<Item class=\"" << cls << "\" referent=\"" << ref(n.id) << "\">\n<Properties>\n";
        common(n);
        if (n.kind == NodeKind::Model && n.primaryPart) refProp("PrimaryPart", n.primaryPart);
        switch (n.kind) {
        case NodeKind::Tool:
            cframe("Grip", n.gripPos * kExportScale, n.gripRot);
            boolean("CanBeDropped", n.canBeDropped);
            boolean("Enabled", n.enabled);
            str("ToolTip", n.toolTip);
            break;
        case NodeKind::Trail:
        case NodeKind::Beam: {
            const EffectProps& e = n.effect;
            const bool beam = n.kind == NodeKind::Beam;
            refProp("Attachment0", e.a0);
            refProp("Attachment1", e.a1);
            {
                std::ostringstream c;
                for (const ColorKey& k : e.color) c << k.t << " " << k.c.r << " " << k.c.g << " " << k.c.b << " 0 ";
                o << "<ColorSequence name=\"Color\">" << c.str() << "</ColorSequence>\n";
            }
            auto seq = [&](const char* name, const std::vector<NumberKey>& keys) {
                std::ostringstream c;
                for (const NumberKey& k : keys) c << k.t << " " << k.v << " 0 ";
                o << "<NumberSequence name=\"" << name << "\">" << c.str() << "</NumberSequence>\n";
            };
            seq("Transparency", e.transparency);
            str("Texture", e.texture);
            flt("LightEmission", e.lightEmission);
            boolean("FaceCamera", e.faceCamera);
            token("TextureMode", e.textureWrap ? 1 : 0);
            flt("TextureLength", e.textureLength * (e.textureWrap ? kExportScale : 1.0f));
            if (beam) {
                flt("Width0", e.width0 * kExportScale);
                flt("Width1", e.width1 * kExportScale);
                flt("CurveSize0", e.curve0 * kExportScale);
                flt("CurveSize1", e.curve1 * kExportScale);
                o << "<int name=\"Segments\">" << e.segments << "</int>\n";
                flt("TextureSpeed", e.textureSpeed);
            } else {
                flt("Lifetime", e.lifetime);
                flt("MinLength", e.minLength * kExportScale);
                flt("MaxLength", e.maxLength * kExportScale);
                seq("WidthScale", e.widthScale);
            }
            boolean("Enabled", n.enabled);
            break;
        }
        case NodeKind::PostEffect: {
            const PostEffectProps& p = n.post;
            switch (p.type) {
                case PostEffectType::ColorCorrection:
                    flt("Brightness", p.brightness); flt("Contrast", p.contrast); flt("Saturation", p.saturation);
                    color3("TintColor", p.tint);
                    break;
                case PostEffectType::SunRays: flt("Intensity", p.intensity); flt("Spread", p.spread); break;
                case PostEffectType::DepthOfField:
                    flt("FarIntensity", p.farIntensity); flt("NearIntensity", p.nearIntensity);
                    flt("FocusDistance", p.focusDistance * kExportScale); flt("InFocusRadius", p.inFocusRadius * kExportScale);
                    break;
                case PostEffectType::Bloom: flt("Intensity", p.intensity); flt("Size", p.size); flt("Threshold", p.threshold); break;
                case PostEffectType::Blur:  flt("Size", p.size); break;
            }
            boolean("Enabled", n.enabled);
            break;
        }
        case NodeKind::Sky: {
            const SkyProps& k = n.sky;
            // "" = ours, which on Roblox is its own default sky.
            auto pic = [&](const char* prop, const std::string& v, const char* fallback) {
                o << "<Content name=\"" << prop << "\"><url>" << esc(v.empty() ? std::string(fallback) : v) << "</url></Content>\n";
            };
            pic("SkyboxBk", k.bk, "rbxasset://textures/sky/sky512_bk.tex");
            pic("SkyboxDn", k.dn, "rbxasset://textures/sky/sky512_dn.tex");
            pic("SkyboxFt", k.ft, "rbxasset://textures/sky/sky512_ft.tex");
            pic("SkyboxLf", k.lf, "rbxasset://textures/sky/sky512_lf.tex");
            pic("SkyboxRt", k.rt, "rbxasset://textures/sky/sky512_rt.tex");
            pic("SkyboxUp", k.up, "rbxasset://textures/sky/sky512_up.tex");
            pic("SunTextureId", k.sun, "rbxasset://sky/sun.jpg");
            pic("MoonTextureId", k.moon, "rbxasset://sky/moon.jpg");
            o << "<int name=\"StarCount\">" << k.starCount << "</int>\n";
            boolean("CelestialBodiesShown", k.celestial);
            flt("SunAngularSize", k.sunSize);
            flt("MoonAngularSize", k.moonSize);
            break;
        }
        case NodeKind::Particles: {
            const ParticleProps& p = n.particle;
            boolean("Enabled", n.enabled);
            if (p.type == ParticleType::Fire) {
                color3("Color", p.color1);
                color3("SecondaryColor", p.color2);
                flt("size_xml", std::clamp(p.fxSize * kExportScale, 2.0f, 30.0f));
                flt("heat_xml", std::clamp(p.rise * kExportScale, -25.0f, 25.0f));
                break;
            }
            if (p.type == ParticleType::Smoke) {
                color3("Color", p.color1);
                flt("opacity_xml", p.opacity);
                flt("riseVelocity_xml", std::clamp(p.rise * kExportScale, -25.0f, 25.0f));
                flt("size_xml", std::clamp(p.fxSize * kExportScale, 0.1f, 100.0f));
                break;
            }
            if (p.type == ParticleType::Sparkles) { color3("SparkleColor", p.color1); break; }
            auto range = [&](const char* name, glm::vec2 r) { o << "<NumberRange name=\"" << name << "\">" << r.x << " " << r.y << " </NumberRange>\n"; };
            auto seq = [&](const char* name, const std::vector<NumberKey>& keys, float scale) {
                std::ostringstream c;
                for (const NumberKey& k : keys) c << k.t << " " << k.v * scale << " 0 ";
                o << "<NumberSequence name=\"" << name << "\">" << c.str() << "</NumberSequence>\n";
            };
            flt("Rate", p.rate);
            range("Lifetime", p.lifetime);
            range("Speed", p.speed * kExportScale);
            vec2("SpreadAngle", p.spread);
            range("Rotation", p.rotation);
            range("RotSpeed", p.rotSpeed);
            {
                std::ostringstream c;
                for (const ColorKey& k : p.color) c << k.t << " " << k.c.r << " " << k.c.g << " " << k.c.b << " 0 ";
                o << "<ColorSequence name=\"Color\">" << c.str() << "</ColorSequence>\n";
            }
            seq("Size", p.size, kExportScale);
            seq("Transparency", p.transparency, 1.0f);
            // The built-in looks as Roblox's own particle pictures; anything else as it is.
            const std::string tex = p.texture == "fire"  ? "rbxasset://textures/particles/fire_main.dds"
                                  : p.texture == "smoke" ? "rbxasset://textures/particles/smoke_main.dds"
                                  : p.texture.empty() || p.texture == "sparkle" ? "rbxasset://textures/particles/sparkles_main.dds"
                                  : p.texture;
            o << "<Content name=\"Texture\"><url>" << esc(tex) << "</url></Content>\n";
            flt("LightEmission", p.lightEmission);
            vec3("Acceleration", p.acceleration * kExportScale);
            flt("Drag", p.drag);
            token("EmissionDirection", (int)p.direction);
            boolean("LockedToPart", p.locked);
            break;
        }
        case NodeKind::Highlight:
            color3("FillColor", n.highlight.fill);
            color3("OutlineColor", n.highlight.outline);
            flt("FillTransparency", n.highlight.fillTransparency);
            flt("OutlineTransparency", n.highlight.outlineTransparency);
            token("DepthMode", n.highlight.onTop ? 0 : 1);
            refProp("Adornee", n.highlight.adornee);
            boolean("Enabled", n.enabled);
            break;
        case NodeKind::ClickDetector:
            flt("MaxActivationDistance", n.click.range * kExportScale);
            str("CursorIcon", n.click.cursor);
            break;
        case NodeKind::Prompt:
            str("ActionText", n.prompt.action);
            str("ObjectText", n.prompt.object);
            token("KeyboardKeyCode", keyCode(n.prompt.key));
            flt("HoldDuration", n.prompt.hold);
            flt("MaxActivationDistance", n.prompt.range * kExportScale);
            boolean("RequiresLineOfSight", n.prompt.lineOfSight);
            boolean("ClickablePrompt", n.prompt.clickable);
            boolean("Enabled", n.enabled);
            break;
        case NodeKind::Gui: {
            const GuiProps& g = n.gui;
            if (g.type == GuiType::ScreenGui) { boolean("Enabled", n.enabled); o << "<int name=\"DisplayOrder\">" << g.displayOrder << "</int>\n"; boolean("ResetOnSpawn", false); break; }
            if (g.type == GuiType::BillboardGui || g.type == GuiType::SurfaceGui) {
                boolean("Enabled", n.enabled);
                boolean("AlwaysOnTop", g.alwaysOnTop);
                flt("MaxDistance", g.maxDistance > 0 ? g.maxDistance * kExportScale : 1e30f);
                if (g.type == GuiType::BillboardGui) {
                    UDim2 s = g.size;
                    s.xs *= kExportScale;
                    s.ys *= kExportScale;
                    udim2("Size", s);
                    vec3("StudsOffset", g.studsOffset * kExportScale);
                    vec3("StudsOffsetWorldSpace", g.worldOffset * kExportScale);
                } else {
                    token("Face", g.face);
                    vec2("CanvasSize", g.surfaceCanvas);
                    token("SizingMode", g.perStud ? 1 : 0);
                    flt("PixelsPerStud", g.pixelsPerStud * kImportScale);
                }
                break;
            }
            if (g.type == GuiType::UICorner) {
                o << "<UDim name=\"CornerRadius\"><S>" << g.corner.xs << "</S><O>" << (int)std::lround(g.corner.xo) << "</O></UDim>\n";
                break;
            }
            if (g.type == GuiType::UIStroke) { color3("Color", g.borderColor); flt("Thickness", g.thickness); flt("Transparency", g.bgTransparency); boolean("Enabled", n.enabled); break; }
            if (g.type == GuiType::UIShadow || g.type == GuiType::UIBlur) break;   // (ours; Roblox skips classes it doesn't know)
            if (g.type == GuiType::UIGradient) {
                std::ostringstream c, t;
                for (const ColorKey& k : g.gradColor) c << k.t << " " << k.c.r << " " << k.c.g << " " << k.c.b << " 0 ";
                for (const NumberKey& k : g.gradTransparency) t << k.t << " " << k.v << " 0 ";
                o << "<ColorSequence name=\"Color\">" << c.str() << "</ColorSequence>\n";
                o << "<NumberSequence name=\"Transparency\">" << t.str() << "</NumberSequence>\n";
                flt("Rotation", g.gradRotation);
                vec2("Offset", g.gradOffset);
                boolean("Enabled", n.enabled);
                break;
            }
            if (g.type == GuiType::UIScale) { flt("Scale", g.scale); break; }
            if (g.type == GuiType::UIAspectRatioConstraint) {
                flt("AspectRatio", g.aspectRatio);
                token("AspectType", g.aspectType);
                token("DominantAxis", g.dominantAxis);
                break;
            }
            auto udim = [&](const char* k, float s, float off) {
                o << "<UDim name=\"" << k << "\"><S>" << s << "</S><O>" << (int)std::lround(off) << "</O></UDim>\n";
            };
            if (isGuiLayout(g.type)) {
                token("FillDirection", g.fill ? 0 : 1);
                token("HorizontalAlignment", g.hAlign == 1 ? 0 : g.hAlign == 2 ? 2 : 1);
                token("VerticalAlignment", g.vAlign == 1 ? 0 : g.vAlign == 2 ? 2 : 1);
                token("SortOrder", g.sortByName ? 0 : 2);
                if (g.type == GuiType::UIListLayout) udim("Padding", g.padding.xs, g.padding.xo);
                else {
                    udim2("CellSize", g.cellSize);
                    udim2("CellPadding", g.padding);
                    o << "<int name=\"FillDirectionMaxCells\">" << g.maxCells << "</int>\n";
                }
                break;
            }
            if (g.type == GuiType::UIPadding) {
                udim("PaddingLeft", g.padScale.x, g.padPx.x);
                udim("PaddingTop", g.padScale.y, g.padPx.y);
                udim("PaddingRight", g.padScale.z, g.padPx.z);
                udim("PaddingBottom", g.padScale.w, g.padPx.w);
                break;
            }
            o << "<int name=\"LayoutOrder\">" << g.layoutOrder << "</int>\n";
            if (g.type == GuiType::ScrollingFrame) {
                udim2("CanvasSize", g.canvasSize);
                vec2("CanvasPosition", g.canvasPos);
                o << "<int name=\"ScrollBarThickness\">" << g.scrollBar << "</int>\n";
                color3("ScrollBarImageColor3", g.scrollColor);
                flt("ScrollBarImageTransparency", g.scrollTransparency);
                token("ScrollingDirection", g.scrollDir == 1 ? 1 : g.scrollDir == 2 ? 2 : 4);
                token("AutomaticCanvasSize", g.autoCanvas);
                boolean("ScrollingEnabled", g.scrolling);
            }
            if (g.type == GuiType::TextBox) {
                str("PlaceholderText", g.placeholder);
                color3("PlaceholderColor3", g.placeholderColor);
                boolean("ClearTextOnFocus", g.clearOnFocus);
                boolean("TextEditable", g.editable);
                boolean("MultiLine", g.multiLine);
            }
            udim2("Position", g.pos);
            udim2("Size", g.size);
            vec2("AnchorPoint", g.anchor);
            color3("BackgroundColor3", g.bg);
            flt("BackgroundTransparency", g.bgTransparency);
            color3("BorderColor3", g.borderColor);
            o << "<int name=\"BorderSizePixel\">" << g.border << "</int>\n";
            o << "<int name=\"ZIndex\">" << g.zIndex << "</int>\n";
            boolean("ClipsDescendants", g.clips);
            boolean("Visible", n.visible);
            if (guiHasText(g.type)) {
                str("Text", g.text);
                color3("TextColor3", g.textColor);
                flt("TextSize", g.textSize);
                boolean("TextScaled", g.textScaled);
                boolean("TextWrapped", g.textWrapped);
                token("TextXAlignment", g.xAlign == 0 ? 0 : g.xAlign == 2 ? 1 : 2);
                token("TextYAlignment", g.yAlign);
                flt("TextTransparency", g.textTransparency);
                color3("TextStrokeColor3", g.strokeColor);
                flt("TextStrokeTransparency", g.strokeTransparency);
                token("Font", g.bold ? 4 : 3);   // SourceSansBold / SourceSans
            }
            if (g.type == GuiType::ImageLabel || g.type == GuiType::ImageButton) {
                o << "<Content name=\"Image\"><url>" << esc(g.image) << "</url></Content>\n";
                color3("ImageColor3", g.imageColor);
                flt("ImageTransparency", g.imageTransparency);
            }
            if (g.type == GuiType::TextButton || g.type == GuiType::ImageButton) boolean("AutoButtonColor", g.autoButtonColor);
            if (g.type == GuiType::ViewportFrame) {
                refProp("CurrentCamera", g.camera);
                color3("Ambient", g.ambient);
                color3("LightColor", g.lightColor);
                vec3("LightDirection", g.lightDir);
                color3("ImageColor3", g.imageColor);
                flt("ImageTransparency", g.imageTransparency);
            }
            break;
        }
        case NodeKind::Camera: {
            const glm::mat4 w = n.worldMatrix();
            glm::mat3 rot;
            for (int i = 0; i < 3; ++i) rot[i] = glm::normalize(glm::vec3(w[i]));
            cframe("CFrame", glm::vec3(w[3]) * kExportScale, rot);
            flt("FieldOfView", n.fieldOfView);
            break;
        }
        case NodeKind::Decal:
            o << "<Content name=\"Texture\"><url>" << n.texture << "</url></Content>\n";
            o << "<token name=\"Face\">" << (int)n.face << "</token>\n";
            flt("Transparency", n.transparency);
            break;
        case NodeKind::Value:
            if (n.value.type == Attribute::String) str("Value", n.value.s);
            else if (n.value.type == Attribute::Bool) boolean("Value", n.value.b);
            else if (n.intValue) o << "<int64 name=\"Value\">" << (long long)n.value.n << "</int64>\n";
            else o << "<double name=\"Value\">" << n.value.n << "</double>\n";
            break;
        case NodeKind::Part: {
            glm::mat4 w = n.worldMatrix();
            glm::vec3 size(glm::length(glm::vec3(w[0])), glm::length(glm::vec3(w[1])), glm::length(glm::vec3(w[2])));
            glm::mat3 rot(glm::vec3(w[0]) / std::max(size.x, 1e-6f), glm::vec3(w[1]) / std::max(size.y, 1e-6f),
                          glm::vec3(w[2]) / std::max(size.z, 1e-6f));
            int shape = 1;
            if (n.primitiveType == PrimitiveType::Sphere) shape = 0;
            if (n.primitiveType == PrimitiveType::Cylinder) {
                shape = 2;
                rot = rot * glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1)));
                size = glm::vec3(size.y, size.x, size.z);
            }
            if (n.primitiveType == PrimitiveType::Plane) size.y = std::max(size.y, 0.05f);
            cframe("CFrame", glm::vec3(w[3]) * kExportScale, rot);
            vec3("size", size * kExportScale);
            color3u8("Color3uint8", n.color);
            flt("Transparency", n.transparency);
            boolean("Anchored", n.anchored);
            boolean("CanCollide", n.canCollide);
            if (n.collisionGroup != CollisionGroups::kDefault) str("CollisionGroup", n.collisionGroup);
            boolean("CastShadow", n.castShadow);
            boolean("Locked", n.locked);
            token("Material", materialToRoblox(n.material));
            if (n.hasCustomPhysics())
                o << "<PhysicalProperties name=\"CustomPhysicalProperties\"><CustomPhysics>true</CustomPhysics><Density>"
                  << Physics::densityOf(&n) << "</Density><Friction>" << Physics::frictionOf(&n) << "</Friction><Elasticity>"
                  << Physics::elasticityOf(&n) << "</Elasticity><FrictionWeight>" << n.frictionWeight
                  << "</FrictionWeight><ElasticityWeight>" << n.elasticityWeight << "</ElasticityWeight></PhysicalProperties>\n";
            if (n.isSpawnLocation()) {
                boolean("Neutral", n.spawnNeutral);
                boolean("AllowTeamChangeOnTouch", n.allowTeamChange);
                o << "<BrickColor name=\"TeamColor\">" << brickNumber(n.teamColor) << "</BrickColor>\n";
            }
            token("shape", shape);
            token("TopSurface", 0);
            token("BottomSurface", 0);
            break;
        }
        case NodeKind::Script:
            boolean("Disabled", !n.enabled);
            {
                // ]]> can't appear inside CDATA: split it.
                std::string src = n.source;
                std::string safe;
                for (size_t i = 0; i < src.size(); ++i) {
                    if (src.compare(i, 3, "]]>") == 0) { safe += "]]]]><![CDATA[>"; i += 2; }
                    else safe += src[i];
                }
                o << "<ProtectedString name=\"Source\"><![CDATA[" << safe << "]]></ProtectedString>\n";
            }
            break;
        case NodeKind::Light:
            color3("Color", n.color);
            flt("Brightness", n.brightness / 2.0f);
            flt("Range", n.range * kExportScale);
            flt("Angle", n.spotAngle);
            boolean("Enabled", n.enabled);
            break;
        case NodeKind::Sound:
            o << "<Content name=\"SoundId\"><url>" << esc(n.soundId) << "</url></Content>\n";
            flt("Volume", n.volume);
            flt("PlaybackSpeed", n.pitch);
            boolean("Looped", n.looped);
            boolean("Playing", n.autoplay);
            break;
        case NodeKind::Attachment: {
            glm::vec3 psize = n.parent ? n.parent->transform.scale : glm::vec3(1.0f);
            glm::mat4 r = glm::eulerAngleZYX(glm::radians(n.transform.rotation.z), glm::radians(n.transform.rotation.y),
                                             glm::radians(n.transform.rotation.x));
            cframe("CFrame", n.transform.position * psize * kExportScale, glm::mat3(r));
            break;
        }
        case NodeKind::Mover: {
            const MoverProps& m = n.mover;
            const MoverType t = m.type;
            auto turn = [&](const char* k) {
                const glm::mat4 r = glm::eulerAngleZYX(glm::radians(m.rotation.z), glm::radians(m.rotation.y), glm::radians(m.rotation.x));
                cframe(k, glm::vec3(0.0f), glm::mat3(r));
            };
            switch (t) {
                case MoverType::BodyVelocity:
                    vec3("Velocity", m.value / kVelScale); vec3("MaxForce", m.maxAxes / kForceScale); flt("P", m.p); break;
                case MoverType::BodyPosition:
                    vec3("Position", m.value * kExportScale); vec3("MaxForce", m.maxAxes / kForceScale);
                    flt("P", m.p * kImportScale / kForceScale); flt("D", m.d * kVelScale / kForceScale); break;
                case MoverType::BodyGyro:
                    turn("CFrame"); vec3("MaxTorque", m.maxAxes / kTorqueScale);
                    flt("P", m.p / kTorqueScale); flt("D", m.d * kSpinScale / kTorqueScale); break;
                case MoverType::BodyAngularVelocity:
                    vec3("AngularVelocity", m.value / kSpinScale); vec3("MaxTorque", m.maxAxes / kTorqueScale); flt("P", m.p); break;
                case MoverType::BodyThrust:
                    vec3("Force", m.value / kForceScale); vec3("Location", m.location * kExportScale); break;
                case MoverType::BodyForce:
                case MoverType::VectorForce:
                    vec3("Force", m.value / kForceScale);
                    if (t == MoverType::VectorForce) boolean("ApplyAtCenterOfMass", m.atCenterOfMass);
                    break;
                case MoverType::Torque: vec3("Torque", m.value / kTorqueScale); break;
                case MoverType::LinearVelocity: vec3("VectorVelocity", m.value / kVelScale); flt("MaxForce", m.maxForce / kForceScale); break;
                case MoverType::AngularVelocity: vec3("AngularVelocity", m.value / kSpinScale); flt("MaxTorque", m.maxForce / kTorqueScale); break;
                case MoverType::AlignPosition:
                case MoverType::AlignOrientation: {
                    const bool pos = t == MoverType::AlignPosition;
                    if (pos) { vec3("Position", m.value * kExportScale); flt("MaxForce", m.maxForce / kForceScale); }
                    else { turn("CFrame"); flt("MaxTorque", m.maxForce / kTorqueScale); }
                    if (m.maxVelocity > 0) flt(pos ? "MaxVelocity" : "MaxAngularVelocity", m.maxVelocity / (pos ? kVelScale : kSpinScale));
                    flt("Responsiveness", m.responsiveness / kSpinScale);
                    boolean("RigidityEnabled", m.rigid);
                    token("Mode", n.ref1 ? 1 : 0);
                    break;
                }
            }
            if (!isBodyMover(t)) {
                refProp("Attachment0", n.ref0);
                refProp("Attachment1", n.ref1);
                boolean("Enabled", n.enabled);
                if (t != MoverType::AlignPosition && t != MoverType::AlignOrientation) token("RelativeTo", m.relativeToAttachment ? 0 : 2);
            }
            break;
        }
        case NodeKind::Constraint:
            if (n.constraintType == ConstraintType::Weld) { refProp("Part0", n.ref0); refProp("Part1", n.ref1); }
            else { refProp("Attachment0", n.ref0); refProp("Attachment1", n.ref1); }
            if (n.constraintType == ConstraintType::Rope || n.constraintType == ConstraintType::Rod)
                flt("Length", std::max(0.0f, n.length) * kExportScale);
            if (n.constraintType == ConstraintType::Spring) {
                flt("FreeLength", std::max(0.0f, n.length) * kExportScale);
                flt("Stiffness", n.stiffness);
                flt("Damping", n.damping);
            }
            if (n.constraintType == ConstraintType::Hinge && n.motorTorque > 0.0f) {
                token("ActuatorType", 1);
                flt("AngularVelocity", n.motorSpeed);
                flt("MotorMaxTorque", n.motorTorque);
            }
            if (usesJointProps(n.constraintType)) {
                const JointProps& p = n.joint;
                auto huge = [](float v, float scale) { return (double)v / scale; };   // ("no limit" stays very big)
                boolean("LimitsEnabled", p.limitsEnabled);
                flt("Restitution", p.restitution);
                if (n.constraintType == ConstraintType::BallSocket) {
                    flt("UpperAngle", p.upperAngle);
                    boolean("TwistLimitsEnabled", p.twistLimitsEnabled);
                    flt("TwistLowerAngle", p.twistLowerAngle);
                    flt("TwistUpperAngle", p.twistUpperAngle);
                } else {
                    flt("LowerLimit", p.lowerLimit * kExportScale);
                    flt("UpperLimit", p.upperLimit * kExportScale);
                    token("ActuatorType", (int)p.actuator);
                    flt("MotorMaxForce", huge(p.motorMaxForce, kForceScale));
                    flt("Velocity", p.velocity / kVelScale);
                    flt("Speed", huge(p.speed, kVelScale));
                    flt("ServoMaxForce", huge(p.servoMaxForce, kForceScale));
                    flt("TargetPosition", p.targetPosition * kExportScale);
                }
                if (n.constraintType == ConstraintType::Cylindrical) {
                    token("AngularActuatorType", (int)p.angularActuator);
                    flt("AngularVelocity", p.angularVelocity / kSpinScale);
                    flt("MotorMaxTorque", huge(p.motorMaxTorque, kTorqueScale));
                    flt("AngularSpeed", huge(p.angularSpeed, kSpinScale));
                    flt("TargetAngle", p.targetAngle);
                    flt("ServoMaxTorque", huge(p.servoMaxTorque, kTorqueScale));
                    boolean("AngularLimitsEnabled", p.angularLimitsEnabled);
                    flt("LowerAngle", p.lowerAngle);
                    flt("UpperAngle", p.upperAngle);
                    flt("InclinationAngle", p.inclinationAngle);
                }
            }
            flt("Thickness", n.thickness * kExportScale);
            boolean("Visible", n.visible);
            boolean("Enabled", n.enabled);
            break;
        default: break;
        }
        o << "</Properties>\n";
        for (auto& c : n.children) item(*c);
        o << "</Item>\n";
    }

    void begin() {
        o << "<roblox xmlns:xmime=\"http://www.w3.org/2005/05/xmlmime\" "
             "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
             "xsi:noNamespaceSchemaLocation=\"http://www.roblox.com/roblox.xsd\" version=\"4\">\n"
             "<External>null</External>\n<External>nil</External>\n";
    }
    void end() { o << "</roblox>\n"; }
};

bool writeText(const std::string& path, const std::string& text, std::string& error) {
    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "couldn't write " + path; return false; }
    f << text;
    return true;
}

} // namespace

// ===========================================================================

std::string Report::summary() const {
    std::string s = std::to_string(parts) + " parts, " + std::to_string(models) + " models, " +
                    std::to_string(scripts) + " scripts";
    if (lights) s += ", " + std::to_string(lights) + " lights";
    if (sounds) s += ", " + std::to_string(sounds) + " sounds";
    if (constraints) s += ", " + std::to_string(constraints) + " attachments/constraints";
    return s;
}

bool isRobloxFile(const std::string& path) {
    return endsWith(path, ".rbxl") || endsWith(path, ".rbxlx") || endsWith(path, ".rbxm") || endsWith(path, ".rbxmx");
}
bool isPlace(const std::string& path) { return endsWith(path, ".rbxl") || endsWith(path, ".rbxlx"); }

bool importPlace(Scene& scene, const std::string& path, Report& report, std::string& error) {
    Document doc;
    if (!readFile(path, doc)) { error = doc.error; return false; }
    const Inst* ws = findChild(doc.roots, "Workspace");
    if (!ws) { error = "this file has no Workspace (is it a model? use Insert from File)"; return false; }

    // Start from an empty world (keeping the player).
    scene.buildDefault();
    std::vector<SceneNode*> old;
    for (auto& c : scene.root()->children) if (!scene.isProtected(c.get())) old.push_back(c.get());
    for (SceneNode* n : old) scene.removeNode(n);

    Converter conv{scene, report, {}, {}};
    if (std::string cg = ws->str("CollisionGroups"); !cg.empty()) conv.groups = parseCollisionGroups(cg);
    scene.world().collisionGroups = conv.groups;
    for (const Inst* k : ws->children) conv.convert(*k, nullptr);
    // Scripts that run on the server live in ServerScriptService in Roblox.
    for (const char* svc : {"StarterGui", "ServerScriptService", "ReplicatedStorage", "ServerStorage", "StarterPlayer", "ReplicatedFirst",
                            "Teams"}) {
        const Inst* s = findChild(doc.roots, svc);
        if (!s || s->children.empty()) continue;
        auto folder = std::make_unique<SceneNode>(svc, NodeKind::Model);
        bool storage = std::string(svc) == "ReplicatedStorage" || std::string(svc) == "ServerStorage";
        if (storage) folder->visible = false;   // templates: hidden, not in the world
        SceneNode* f = scene.insert(std::move(folder));
        conv.made[s] = f;
        for (const Inst* k : s->children) conv.convert(*k, f);
    }

    // Workspace + Lighting settings.
    double g = ws->num("Gravity", kRobloxGravity);
    scene.world().gravity = (float)(22.0 * g / kRobloxGravity);
    if (const Inst* light = findChild(doc.roots, "Lighting")) {
        Environment& env = scene.environment();
        EnvironmentPresets::applyTimeOfDay(env, (float)light->num("ClockTime", 14.0));
        env.clockTime = (float)light->num("ClockTime", 14.0);
        double fogEnd = light->num("FogEnd", 100000.0);
        if (fogEnd < 5000.0) {
            env.fogEnabled = true;
            env.fogDensity = (float)std::clamp(2.0 / (fogEnd * kImportScale), 0.0005, 0.2);
            if (const Value* c = light->get("FogColor")) env.fogColor = c->v;
        }
        env.shadows = light->flag("GlobalShadows", true);
        if (const Value* a = light->get("AttributesSerialize"); a && a->kind == Value::Str) {
            SceneNode tmp("Lighting", NodeKind::Model);
            readAttributes(a->s, tmp);
            if (const Attribute* saved = tmp.findAttribute("GutsAndBoltsLighting"); saved && saved->type == Attribute::String)
                Serializer::environmentFromString(env, saved->s);   // made by Guts and Bolts: exact lighting
        }
        // What's inside Lighting: the Sky, screen effects (and anything else we can make).
        if (!light->children.empty()) {
            SceneNode* f = scene.lightingFolder(true);
            conv.made[light] = f;
            for (const Inst* k : light->children) conv.convert(*k, f);
        }
    }
    // Screen effects saved in the Workspace's Camera: Studio's camera holds nothing, so they
    // go in Lighting (where they look the same).
    if (const Inst* cam = findChild(ws->children, "Camera"); cam && cam->className == "Camera")
        for (const Inst* k : cam->children)
            if (std::find(std::begin(kPostEffectClassNames), std::end(kPostEffectClassNames), k->className) != std::end(kPostEffectClassNames))
                conv.convert(*k, scene.lightingFolder(true));
    conv.linkRefs(doc);
    scene.info().title = std::filesystem::path(path).stem().string();
    scene.markDirty();
    return true;
}

std::vector<SceneNode*> importModel(Scene& scene, SceneNode* parent, const std::string& path, Report& report,
                                    std::string& error) {
    std::vector<SceneNode*> out;
    Document doc;
    if (!readFile(path, doc)) { error = doc.error; return out; }
    std::vector<Inst*> roots = doc.roots;
    if (const Inst* ws = findChild(doc.roots, "Workspace")) roots = ws->children;   // a place: take its Workspace
    Converter conv{scene, report, {}, scene.world().collisionGroups};
    for (const Inst* r : roots)
        if (SceneNode* n = conv.convert(*r, parent)) out.push_back(n);
    conv.linkRefs(doc);
    scene.markDirty();
    if (out.empty()) error = "nothing in this file could be imported";
    return out;
}

bool exportPlace(Scene& scene, const std::string& path, std::string& error) {
    XmlWriter w(scene);
    w.begin();
    w.o << "<Item class=\"Workspace\" referent=\"RBXWORKSPACE\">\n<Properties>\n";
    w.str("Name", "Workspace");
    w.flt("Gravity", scene.world().gravity / 22.0f * kRobloxGravity);
    if (!scene.world().collisionGroups.isDefault()) w.str("CollisionGroups", writeCollisionGroups(scene.world().collisionGroups));
    w.o << "</Properties>\n";
    const SceneNode* ui = scene.root()->findChild("StarterGui");
    if (ui && ui->kind != NodeKind::Model) ui = nullptr;
    const SceneNode* lighting = scene.lightingFolder(false);   // (its things go in Roblox's Lighting below)
    for (auto& c : scene.root()->children) if (c.get() != ui && c.get() != lighting) w.item(*c);
    w.o << "</Item>\n";
    if (ui) {   // our StarterGui folder is Roblox's StarterGui service
        w.o << "<Item class=\"StarterGui\" referent=\"RBXSTARTERGUI\">\n<Properties>\n";
        w.str("Name", "StarterGui");
        w.o << "</Properties>\n";
        for (auto& c : ui->children) w.item(*c);
        w.o << "</Item>\n";
    }
    w.o << "<Item class=\"Lighting\" referent=\"RBXLIGHTING\">\n<Properties>\n";
    w.str("Name", "Lighting");
    {
        // Our full lighting setup rides along as an attribute, so a game that
        // goes to Roblox and back keeps its exact look.
        SceneNode holder("Lighting", NodeKind::Model);
        Attribute a;
        a.name = "GutsAndBoltsLighting";
        a.type = Attribute::String;
        a.s = Serializer::environmentToString(scene.environment());
        holder.attributes.push_back(a);
        w.binary("AttributesSerialize", XmlWriter::attributesBlob(holder));
    }
    w.flt("ClockTime", scene.environment().clockTime);
    w.boolean("GlobalShadows", scene.environment().shadows);
    w.o << "</Properties>\n";
    if (lighting) for (auto& c : lighting->children) w.item(*c);   // the Sky, screen effects
    w.o << "</Item>\n";
    w.end();
    return writeText(path, w.o.str(), error);
}

bool exportModel(Scene& scene, const std::vector<SceneNode*>& nodes, const std::string& path, std::string& error) {
    XmlWriter w(scene);
    w.begin();
    for (SceneNode* n : nodes) w.item(*n);
    w.end();
    return writeText(path, w.o.str(), error);
}

} // namespace RobloxFile
