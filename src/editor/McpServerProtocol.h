#pragma once
// The MCP "2026-07-28" rules for Studio's own MCP server (StudioMcp.cpp). The website's endpoint
// follows the same rules in worker/mcp-protocol.js (worker/test/mcp-2026.test.mjs checks that the
// version list and error numbers here match it).
//
// In short: no handshake (no initialize, no Mcp-Session-Id). Every request carries its protocol
// version and client details in params._meta, and HTTP headers that repeat the body
// (MCP-Protocol-Version, Mcp-Method, and Mcp-Name for tools/call, resources/read, prompts/get).
// Headers that don't match the body: 400 and -32020. server/discover says what we support.
// Old clients (2025-06-18 and before, starting with `initialize`) are answered the old way.

#include <nlohmann/json.hpp>
#include <cstdint>
#include <functional>
#include <string>

namespace McpServerProtocol {
using nlohmann::json;

constexpr const char* kModern = "2026-07-28";
constexpr const char* kSupportedVersions[] = {"2026-07-28", "2025-06-18", "2025-03-26", "2024-11-05"};
constexpr int kHeaderMismatch = -32020;
constexpr int kUnsupportedVersion = -32022;
constexpr int kInvalidParams = -32602;
constexpr int kInvalidRequest = -32600;
constexpr const char* kMetaVersion = "io.modelcontextprotocol/protocolVersion";
constexpr const char* kMetaCapabilities = "io.modelcontextprotocol/clientCapabilities";
constexpr const char* kMetaServerInfo = "io.modelcontextprotocol/serverInfo";

inline json supportedVersions() {
    json out = json::array();
    for (const char* v : kSupportedVersions) out.push_back(v);
    return out;
}
inline bool isLegacy(const std::string& v) {
    for (const char* s : kSupportedVersions) if (v == s && v != kModern) return true;
    return false;
}

// =?base64?<base64 of UTF-8>?= -> the text. Anything else comes back as is. `ok` is false if broken.
inline std::string decodeHeaderValue(const std::string& value, bool& ok) {
    ok = true;
    const std::string pre = "=?base64?", post = "?=";
    if (value.size() < pre.size() + post.size() || value.compare(0, pre.size(), pre) != 0 ||
        value.compare(value.size() - post.size(), post.size(), post) != 0) return value;
    const std::string b64 = value.substr(pre.size(), value.size() - pre.size() - post.size());
    auto sextet = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    uint32_t acc = 0;
    int bits = 0;
    size_t pad = 0;
    for (char c : b64) {
        if (c == '=') { ++pad; continue; }
        int s = sextet(c);
        if (s < 0 || pad) return value;   // not the pattern: taken as is (same as the website)
        acc = (acc << 6) | (uint32_t)s;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((char)((acc >> bits) & 0xFF)); }
    }
    if (pad > 2) return value;
    // Must be UTF-8.
    for (size_t i = 0; i < out.size();) {
        unsigned char c = (unsigned char)out[i];
        size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (!n || i + n > out.size()) { ok = false; return std::string(); }
        for (size_t k = 1; k < n; ++k) if (((unsigned char)out[i + k] >> 6) != 0x2) { ok = false; return std::string(); }
        i += n;
    }
    return out;
}

struct Check {
    bool ok = true;
    bool modern = false;
    int status = 200;
    json error;   // {code, message, data?} when !ok
};

// header(name) gives a request header (name in lower case), or "" with present=false if missing.
using HeaderFn = std::function<std::string(const std::string& name, bool& present)>;

inline Check checkRequest(const HeaderFn& header, const json& body) {
    auto fail = [](int code, const std::string& message, json data = json()) {
        Check c; c.ok = false; c.status = 400;
        c.error = {{"code", code}, {"message", message}};
        if (!data.is_null()) c.error["data"] = std::move(data);
        return c;
    };
    auto cut = [](const std::string& s, size_t n) { return s.size() > n ? s.substr(0, n) : s; };
    bool hasVersion = false;
    const std::string headerVersion = header("mcp-protocol-version", hasVersion);
    const json* msg = body.is_object() ? &body : nullptr;
    const json* params = msg && msg->contains("params") && (*msg)["params"].is_object() ? &(*msg)["params"] : nullptr;
    const json* meta = params && params->contains("_meta") && (*params)["_meta"].is_object() ? &(*params)["_meta"] : nullptr;
    const bool hasMetaVersion = meta && meta->contains(kMetaVersion);
    const std::string metaVersion = hasMetaVersion && (*meta)[kMetaVersion].is_string() ? (*meta)[kMetaVersion].get<std::string>() : std::string();

    if (msg && msg->value("method", json()) == json("initialize")) return Check{};   // initialize = old rules
    const bool modern = (hasVersion && headerVersion == kModern) || metaVersion == kModern;
    if (!modern) {
        const std::string asked = hasVersion ? headerVersion : metaVersion;
        if (!asked.empty() && !isLegacy(asked))
            return fail(kUnsupportedVersion, "Unsupported protocol version", {{"supported", supportedVersions()}, {"requested", cut(asked, 64)}});
        return Check{};
    }
    if (!msg) return fail(kInvalidRequest, std::string("Send one JSON-RPC message per request (batches aren't part of ") + kModern + ").");
    if (!msg->contains("method") || !(*msg)["method"].is_string()) return fail(kInvalidRequest, "Not a JSON-RPC 2.0 request.");
    const std::string method = (*msg)["method"].get<std::string>();
    if (!hasVersion) return fail(kHeaderMismatch, "Header mismatch: the MCP-Protocol-Version header is missing.");
    if (!hasMetaVersion) return fail(kInvalidParams, std::string("params._meta[\"") + kMetaVersion + "\"] is required.");
    if (headerVersion != metaVersion)
        return fail(kHeaderMismatch, "Header mismatch: MCP-Protocol-Version header value '" + cut(headerVersion, 64) +
                                     "' does not match body value '" + cut(metaVersion, 64) + "'");
    bool hasMethod = false;
    const std::string methodHeader = header("mcp-method", hasMethod);
    if (!hasMethod) return fail(kHeaderMismatch, "Header mismatch: the Mcp-Method header is missing.");
    if (methodHeader != method)
        return fail(kHeaderMismatch, "Header mismatch: Mcp-Method header value '" + cut(methodHeader, 60) +
                                     "' does not match body value '" + cut(method, 60) + "'");
    Check good; good.modern = true;
    if (!msg->contains("id")) return good;   // notifications: nothing more to check
    if (!meta->contains(kMetaCapabilities) || !(*meta)[kMetaCapabilities].is_object())
        return fail(kInvalidParams, std::string("params._meta[\"") + kMetaCapabilities + "\"] is required.");
    const char* field = method == "tools/call" || method == "prompts/get" ? "name" : method == "resources/read" ? "uri" : nullptr;
    if (field) {
        bool hasName = false;
        const std::string raw = header("mcp-name", hasName);
        if (!hasName) return fail(kHeaderMismatch, "Header mismatch: the Mcp-Name header is missing.");
        bool decoded = true;
        const std::string name = decodeHeaderValue(raw, decoded);
        const bool wantOk = params && params->contains(field) && (*params)[field].is_string();
        const std::string want = wantOk ? (*params)[field].get<std::string>() : std::string();
        if (!decoded || !wantOk || name != want)
            return fail(kHeaderMismatch, "Header mismatch: Mcp-Name header value '" + cut(raw, 80) +
                                         "' does not match body value '" + cut(want, 80) + "'");
    }
    return good;
}

// Make an answer follow the new rules: results say resultType and who answered; -32002 becomes -32602.
inline void modernize(json& reply, const json& serverInfo) {
    if (!reply.is_object()) return;
    if (reply.contains("result") && reply["result"].is_object()) {
        json& r = reply["result"];
        if (!r.contains("resultType")) r["resultType"] = "complete";
        if (!r.contains("_meta") || !r["_meta"].is_object()) r["_meta"] = json::object();
        r["_meta"][kMetaServerInfo] = serverInfo;
    } else if (reply.contains("error") && reply["error"].is_object() && reply["error"].value("code", 0) == -32002) {
        reply["error"]["code"] = kInvalidParams;
    }
}

inline json discoverResult(const json& capabilities, const std::string& instructions, const json& serverInfo) {
    json r = {{"resultType", "complete"}, {"supportedVersions", supportedVersions()}, {"capabilities", capabilities},
              {"ttlMs", 3600000}, {"cacheScope", "public"}};
    if (!instructions.empty()) r["instructions"] = instructions;
    r["_meta"] = {{kMetaServerInfo, serverInfo}};
    return r;
}

// The headers a 2026-07-28 message needs, for passing a stdio message on over HTTP (--mcp bridge).
// Empty if the message isn't a 2026-07-28 one.
inline std::string headersFor(const json& m) {
    if (!m.is_object() || !m.contains("method") || !m["method"].is_string()) return std::string();
    const json params = m.value("params", json::object());
    if (!params.is_object()) return std::string();
    const json meta = params.value("_meta", json::object());
    if (!meta.is_object() || meta.value(kMetaVersion, json()) != json(kModern)) return std::string();
    const std::string method = m["method"].get<std::string>();
    auto safe = [](const std::string& s) {
        for (unsigned char c : s) if (c < 0x21 || c > 0x7e) return false;
        return true;
    };
    if (!safe(method)) return std::string();
    std::string h = std::string("MCP-Protocol-Version: ") + kModern + "\r\nMcp-Method: " + method + "\r\n";
    const char* field = method == "tools/call" || method == "prompts/get" ? "name" : method == "resources/read" ? "uri" : nullptr;
    if (field && params.contains(field) && params[field].is_string()) {
        const std::string name = params[field].get<std::string>();
        if (safe(name)) h += "Mcp-Name: " + name + "\r\n";
        else {
            static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string b;
            uint32_t acc = 0;
            int bits = 0;
            for (unsigned char c : name) {
                acc = (acc << 8) | c; bits += 8;
                while (bits >= 6) { bits -= 6; b.push_back(abc[(acc >> bits) & 63]); }
            }
            if (bits) b.push_back(abc[(acc << (6 - bits)) & 63]);
            while (b.size() % 4) b.push_back('=');
            h += "Mcp-Name: =?base64?" + b + "?=\r\n";
        }
    }
    return h;
}

} // namespace McpServerProtocol
