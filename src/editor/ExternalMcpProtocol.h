#pragma once

#include <nlohmann/json.hpp>
#include <monocypher.h>
#include <cstdint>
#include <string>

namespace McpHttpProtocol {
using nlohmann::json;

// Prepare the HTTP envelope for one negotiated MCP request. Modern requests are
// stateless and self-describing; legacy requests retain the session header.
inline bool prepareRequest(json& request, std::string& headers,
                           const std::string& protocolVersion,
                           const std::string& session,
                           const std::string& token,
                           std::string& error) {
    if (token.find_first_of("\r\n") != std::string::npos ||
        session.find_first_of("\r\n") != std::string::npos ||
        protocolVersion.find_first_of("\r\n") != std::string::npos ||
        protocolVersion.size() > 64) {
        error = "The MCP credential, session identifier, or protocol version is invalid.";
        return false;
    }

    headers = "Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n";
    if (!protocolVersion.empty()) headers += "MCP-Protocol-Version: " + protocolVersion + "\r\n";
    if (protocolVersion == "2026-07-28") {
        if (!request.contains("method") || !request["method"].is_string()) {
            error = "A modern MCP request must include a JSON-RPC method.";
            return false;
        }
        const std::string method = request["method"].get<std::string>();
        if (!request.contains("params") || !request["params"].is_object())
            request["params"] = json::object();
        json& params = request["params"];
        json meta = params.value("_meta", json::object());
        if (!meta.is_object()) {
            error = "MCP request metadata must be an object.";
            return false;
        }
        meta["io.modelcontextprotocol/protocolVersion"] = protocolVersion;
        meta["io.modelcontextprotocol/clientCapabilities"] = json::object();
        meta["io.modelcontextprotocol/clientInfo"] = {{"name", "fyxa"}, {"version", "0.1"}};
        params["_meta"] = std::move(meta);
        headers += "Mcp-Method: " + method + "\r\n";

        std::string routeName;
        if (params.contains("name") && params["name"].is_string())
            routeName = params["name"].get<std::string>();
        else if (params.contains("uri") && params["uri"].is_string())
            routeName = params["uri"].get<std::string>();
        const bool requiresName = method == "tools/call" || method == "prompts/get" || method == "resources/read";
        if (requiresName && routeName.empty()) {
            error = "This modern MCP method requires a name or URI for HTTP routing.";
            return false;
        }
        if (!routeName.empty()) {
            // Use only an unambiguous visible ASCII value. Non-ASCII or separator
            // values require the protocol's encoded-header form, not raw headers.
            for (unsigned char ch : routeName) {
                if (ch < 0x21 || ch > 0x7e || ch == ',' || ch == ';') {
                    error = "The MCP tool name or resource URI cannot be represented safely in a routing header.";
                    return false;
                }
            }
            headers += "Mcp-Name: " + routeName + "\r\n";
        }
    } else if (!session.empty()) {
        headers += "Mcp-Session-Id: " + session + "\r\n";
    }
    if (!token.empty()) headers += "Authorization: Bearer " + token + "\r\n";
    return true;
}
inline std::string definitionFingerprint(const std::string& serverName,
                                            const std::string& endpoint,
                                            const json& tool) {
    const json identity = {
        {"server", serverName},
        {"endpoint", endpoint},
        {"name", tool.value("name", std::string())},
        {"description", tool.value("description", std::string())},
        {"inputSchema", tool.value("inputSchema", json::object())},
        {"annotations", tool.value("annotations", json::object())}
    };
    const std::string canonical = identity.dump();
    std::uint8_t digest[32]{};
    crypto_blake2b(digest, sizeof(digest),
        reinterpret_cast<const std::uint8_t*>(canonical.data()), canonical.size());
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(sizeof(digest) * 2);
    for (std::uint8_t byte : digest) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

inline bool legacyFallbackAllowed(const json& response) {
    if (!response.is_object() || !response.contains("error") || !response["error"].is_object())
        return false;
    const json& rpcError = response["error"];
    if (!rpcError.contains("code") || !rpcError["code"].is_number_integer()) return false;
    const int code = rpcError["code"].get<int>();
    return code == -32022 || code == -32601;
}

} // namespace McpHttpProtocol
