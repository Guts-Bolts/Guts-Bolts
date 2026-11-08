#pragma once

#include <nlohmann/json.hpp>
#include <monocypher.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace McpOAuth {
using nlohmann::json;

struct HttpsUrl {
    std::string origin;
    std::string path;
    std::string host;
    int port = 443;
};

inline bool parseHttpsUrl(const std::string& value, HttpsUrl& out) {
    if (value.rfind("https://", 0) != 0 || value.size() > 8192 ||
        value.find_first_of("\r\n\t #") != std::string::npos) return false;
    const size_t authorityStart = 8;
    const size_t pathStart = value.find_first_of("/?", authorityStart);
    const std::string authority = value.substr(authorityStart,
        pathStart == std::string::npos ? std::string::npos : pathStart - authorityStart);
    if (authority.empty() || authority.find('@') != std::string::npos ||
        authority.find_first_of("%\\") != std::string::npos) return false;
    const size_t colon = authority.rfind(':');
    out.host = colon == std::string::npos ? authority : authority.substr(0, colon);
    out.port = 443;
    if (colon != std::string::npos) {
        const std::string portText = authority.substr(colon + 1);
        if (portText.empty() || !std::all_of(portText.begin(), portText.end(), [](unsigned char c) { return std::isdigit(c); }))
            return false;
        try { out.port = std::stoi(portText); } catch (...) { return false; }
        if (out.port < 1 || out.port > 65535) return false;
    }
    if (out.host.empty() || out.host.size() > 253) return false;
    for (unsigned char c : out.host)
        if (!(std::isalnum(c) || c == '.' || c == '-')) return false;
    for (const auto& label : [&]() {
        std::vector<std::string> labels;
        size_t at = 0;
        while (at <= out.host.size()) {
            size_t end = out.host.find('.', at);
            labels.push_back(out.host.substr(at, end == std::string::npos ? std::string::npos : end - at));
            if (end == std::string::npos) break;
            at = end + 1;
        }
        return labels;
    }()) {
        if (label.empty() || label.front() == '-' || label.back() == '-') return false;
    }
    out.origin = "https://" + out.host + (out.port == 443 ? "" : ":" + std::to_string(out.port));
    out.path = pathStart == std::string::npos ? "/" :
        value[pathStart] == '?' ? "/" + value.substr(pathStart) : value.substr(pathStart);
    return true;
}

inline std::vector<std::string> protectedResourceMetadataUrls(const std::string& resource) {
    HttpsUrl parsed;
    if (!parseHttpsUrl(resource, parsed)) return {};
    std::vector<std::string> paths;
    const std::string wellKnown = "/.well-known/oauth-protected-resource";
    std::string pathOnly = parsed.path.substr(0, parsed.path.find('?'));
    if (pathOnly.empty() || pathOnly == "/") paths.push_back(wellKnown);
    else paths.push_back(wellKnown + pathOnly);
    if (paths.empty() || paths.back() != wellKnown) paths.push_back(wellKnown);
    std::vector<std::string> urls;
    for (const auto& path : paths) urls.push_back(parsed.origin + path);
    return urls;
}

inline std::vector<std::string> authorizationServerMetadataUrls(const std::string& issuer) {
    HttpsUrl parsed;
    if (!parseHttpsUrl(issuer, parsed)) return {};
    std::string path = parsed.path.substr(0, parsed.path.find('?'));
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    const std::string oauth = "/.well-known/oauth-authorization-server";
    const std::string oidc = "/.well-known/openid-configuration";
    if (path == "/") return {parsed.origin + oauth, parsed.origin + oidc};
    return {parsed.origin + oauth + path, parsed.origin + oidc + path, parsed.origin + path + oidc};
}

inline bool metadataIssuerMatches(const json& metadata, const std::string& expectedIssuer) {
    return metadata.is_object() && metadata.contains("issuer") &&
        metadata["issuer"].is_string() && metadata["issuer"].get<std::string>() == expectedIssuer;
}

inline bool extractQuotedParameter(const std::string& header, const std::string& wanted, std::string& value) {
    std::string low = header, key = wanted;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    for (char& c : key) c = (char)std::tolower((unsigned char)c);
    size_t at = 0;
    while ((at = low.find(key, at)) != std::string::npos) {
        const bool boundary = at == 0 || low[at - 1] == ',' || low[at - 1] == ' ' || low[at - 1] == '\t';
        size_t eq = at + key.size();
        while (eq < low.size() && (low[eq] == ' ' || low[eq] == '\t')) ++eq;
        if (!boundary || eq >= low.size() || low[eq] != '=') { at += key.size(); continue; }
        ++eq;
        while (eq < header.size() && (header[eq] == ' ' || header[eq] == '\t')) ++eq;
        if (eq >= header.size() || header[eq] != '"') return false;
        const size_t end = header.find('"', eq + 1);
        if (end == std::string::npos) return false;
        value = header.substr(eq + 1, end - eq - 1);
        return !value.empty() && value.find_first_of("\r\n") == std::string::npos;
    }
    return false;
}

inline std::string base64Url(const std::uint8_t* data, size_t size) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((size * 4 + 2) / 3);
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t a = data[i];
        const uint32_t b = i + 1 < size ? data[i + 1] : 0;
        const uint32_t c = i + 2 < size ? data[i + 2] : 0;
        const uint32_t n = (a << 16) | (b << 8) | c;
        out.push_back(alphabet[(n >> 18) & 63]);
        out.push_back(alphabet[(n >> 12) & 63]);
        if (i + 1 < size) out.push_back(alphabet[(n >> 6) & 63]);
        if (i + 2 < size) out.push_back(alphabet[n & 63]);
    }
    return out;
}

inline bool secureRandom(std::uint8_t* output, size_t size) {
    try {
        std::random_device random;
        if (random.entropy() == 0.0) return false;
        for (size_t i = 0; i < size; i += sizeof(unsigned int)) {
            const unsigned int value = random();
            for (size_t j = 0; j < sizeof(value) && i + j < size; ++j)
                output[i + j] = static_cast<std::uint8_t>(value >> (j * 8));
        }
        return true;
    } catch (...) { return false; }
}

struct Pkce {
    std::string verifier;
    std::string challenge;
    std::string state;
};

inline bool createPkce(Pkce& out) {
    std::uint8_t verifier[32]{}, state[32]{}, digest[32]{};
    if (!secureRandom(verifier, sizeof(verifier)) || !secureRandom(state, sizeof(state))) return false;
    out.verifier = base64Url(verifier, sizeof(verifier));
    out.state = base64Url(state, sizeof(state));
    crypto_sha256(digest, reinterpret_cast<const std::uint8_t*>(out.verifier.data()), out.verifier.size());
    out.challenge = base64Url(digest, sizeof(digest));
    crypto_wipe(verifier, sizeof(verifier));
    crypto_wipe(state, sizeof(state));
    crypto_wipe(digest, sizeof(digest));
    return out.verifier.size() >= 43 && out.verifier.size() <= 128;
}

inline std::string formEncode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out.push_back((char)c);
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    }
    return out;
}

} // namespace McpOAuth
