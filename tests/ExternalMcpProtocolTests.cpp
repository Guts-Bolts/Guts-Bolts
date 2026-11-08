#include "../src/editor/ExternalMcpProtocol.h"

#include <cassert>
#include <iostream>
#include <cstdlib>
#include <string>

#undef assert
#define assert(condition) do { if (!(condition)) { \
    std::cerr << "FAILED at line " << __LINE__ << ": " #condition << "\\n"; \
    return EXIT_FAILURE; \
} } while (false)

int main() {
    using nlohmann::json;
    std::string headers, error;

    json modern = {{"jsonrpc", "2.0"}, {"id", 7}, {"method", "tools/call"},
                   {"params", {{"name", "search"}, {"arguments", {{"q", "otters"}}}}}};
    assert(McpHttpProtocol::prepareRequest(modern, headers, "2026-07-28", "ignored-session", "token", error));
    assert(headers.find("Content-Type:") == std::string::npos); // Net::httpPost adds it exactly once.
    assert(headers.find("MCP-Protocol-Version: 2026-07-28\r\n") != std::string::npos);
    assert(headers.find("Mcp-Method: tools/call\r\n") != std::string::npos);
    assert(headers.find("Mcp-Name: search\r\n") != std::string::npos);
    assert(headers.find("Mcp-Session-Id:") == std::string::npos);
    assert(headers.find("Authorization: Bearer token\r\n") != std::string::npos);
    const json meta = modern["params"]["_meta"];
    assert(meta["io.modelcontextprotocol/protocolVersion"] == "2026-07-28");
    assert(meta["io.modelcontextprotocol/clientCapabilities"].is_object());
    assert(meta["io.modelcontextprotocol/clientInfo"]["name"] == "fyxa");

    json list = {{"jsonrpc", "2.0"}, {"id", 8}, {"method", "tools/list"}};
    assert(McpHttpProtocol::prepareRequest(list, headers, "2026-07-28", "", "", error));
    assert(headers.find("Mcp-Method: tools/list\r\n") != std::string::npos);
    assert(headers.find("Mcp-Name:") == std::string::npos);
    assert(list["params"]["_meta"].is_object());

    json legacy = {{"jsonrpc", "2.0"}, {"id", 9}, {"method", "tools/call"},
                   {"params", {{"name", "legacy_search"}, {"arguments", json::object()}}}};
    assert(McpHttpProtocol::prepareRequest(legacy, headers, "2025-06-18", "session-123", "", error));
    assert(headers.find("Mcp-Session-Id: session-123\r\n") != std::string::npos);
    assert(headers.find("Mcp-Method:") == std::string::npos);
    assert(!legacy["params"].contains("_meta"));

    json missingName = {{"jsonrpc", "2.0"}, {"id", 10}, {"method", "tools/call"},
                        {"params", {{"arguments", json::object()}}}};
    assert(!McpHttpProtocol::prepareRequest(missingName, headers, "2026-07-28", "", "", error));

    json badMeta = {{"jsonrpc", "2.0"}, {"id", 11}, {"method", "tools/list"},
                    {"params", {{"_meta", "not-an-object"}}}};
    assert(!McpHttpProtocol::prepareRequest(badMeta, headers, "2026-07-28", "", "", error));

    json headerInjection = {{"jsonrpc", "2.0"}, {"id", 12}, {"method", "tools/call"},
                            {"params", {{"name", "search\r\nAuthorization: evil"}, {"arguments", json::object()}}}};
    assert(!McpHttpProtocol::prepareRequest(headerInjection, headers, "2026-07-28", "", "", error));

    json invalidToken = {{"jsonrpc", "2.0"}, {"id", 13}, {"method", "tools/list"}};
    assert(!McpHttpProtocol::prepareRequest(invalidToken, headers, "2025-06-18", "", "x\r\nY: z", error));

    std::string toolError;
    assert(!McpHttpProtocol::toolResultIsError(json::object(), toolError));
    assert(!McpHttpProtocol::toolResultIsError({{"content", json::array()}, {"isError", false}}, toolError));
    assert(McpHttpProtocol::toolResultIsError({{"content", json::array()}, {"isError", true}}, toolError));
    assert(!toolError.empty());
    assert(McpHttpProtocol::toolResultIsError({{"isError", "true"}}, toolError));
    assert(!toolError.empty());

    // MCP routing values must not be smuggled through comma/semicolon separators.
    json commaName = {{ "jsonrpc", "2.0" }, { "id", 14 }, { "method", "tools/call" },
                      { "params", {{ "name", "search,admin" }, { "arguments", json::object() }} }};
    assert(!McpHttpProtocol::prepareRequest(commaName, headers, "2026-07-28", "", "", error));
    json semicolonUri = {{ "jsonrpc", "2.0" }, { "id", 15 }, { "method", "resources/read" },
                         { "params", {{ "uri", "file://host/path;admin" }} }};
    assert(!McpHttpProtocol::prepareRequest(semicolonUri, headers, "2026-07-28", "", "", error));

    // Definition fingerprints bind approval to the exact server, endpoint and schema.
    const json definition = {
        {"name", "write_file"},
        {"description", "Write a project file"},
        {"inputSchema", {{"type", "object"}, {"properties", {{"path", {{"type", "string"}}}}}}},
        {"annotations", {{"destructiveHint", true}}}
    };
    const std::string fingerprint = McpHttpProtocol::definitionFingerprint("workspace", "https://mcp.example/tools", definition);
    assert(fingerprint.size() == 64);
    assert(fingerprint == McpHttpProtocol::definitionFingerprint("workspace", "https://mcp.example/tools", definition));
    assert(fingerprint != McpHttpProtocol::definitionFingerprint("other-server", "https://mcp.example/tools", definition));
    assert(fingerprint != McpHttpProtocol::definitionFingerprint("workspace", "https://other.example/tools", definition));
    json changedDescription = definition;
    changedDescription["description"] = "Read a project file";
    assert(fingerprint != McpHttpProtocol::definitionFingerprint("workspace", "https://mcp.example/tools", changedDescription));
    json changedSchema = definition;
    changedSchema["inputSchema"]["properties"]["path"]["pattern"] = "^[a-z]+$";
    assert(fingerprint != McpHttpProtocol::definitionFingerprint("workspace", "https://mcp.example/tools", changedSchema));
    json changedAnnotation = definition;
    changedAnnotation["annotations"]["destructiveHint"] = false;
    assert(fingerprint != McpHttpProtocol::definitionFingerprint("workspace", "https://mcp.example/tools", changedAnnotation));

    // A server's legacy fallback is restricted to explicit protocol errors.
    assert(McpHttpProtocol::legacyFallbackAllowed({{"error", {{"code", -32601}}}}));
    assert(McpHttpProtocol::legacyFallbackAllowed({{"error", {{"code", -32022}}}}));
    assert(!McpHttpProtocol::legacyFallbackAllowed({{"error", {{"code", -32600}}}}));
    assert(!McpHttpProtocol::legacyFallbackAllowed({{"error", {{"code", -32000}}}}));
    assert(!McpHttpProtocol::legacyFallbackAllowed({{"error", {{"code", " -32601 "}}}}));
    assert(!McpHttpProtocol::legacyFallbackAllowed({{"error", "not-an-object"}}));

    std::cout << "MCP HTTP protocol envelope tests passed\n";
    return 0;
}
