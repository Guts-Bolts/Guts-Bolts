// Studio's MCP server and the 2026-07-28 rules (src/editor/McpServerProtocol.h): which requests use the
// new rules, the headers that must match the body, base64 names, server/discover, and answers that
// carry resultType. Old (2025) requests still pass as before.
//
//   McpServerProtocolTests
#include "editor/McpServerProtocol.h"

#include <iostream>
#include <map>
#include <string>

using nlohmann::json;
namespace P = McpServerProtocol;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
P::HeaderFn headers(const std::map<std::string, std::string>& h) {
    return [h](const std::string& name, bool& present) {
        auto it = h.find(name);
        present = it != h.end();
        return present ? it->second : std::string();
    };
}
json modernBody(const std::string& method, json params = json::object(), bool withId = true) {
    params["_meta"] = {{P::kMetaVersion, P::kModern}, {P::kMetaCapabilities, json::object()}};
    json m = {{"jsonrpc", "2.0"}, {"method", method}, {"params", params}};
    if (withId) m["id"] = 1;
    return m;
}
int code(const P::Check& c) { return c.ok ? 0 : c.error.value("code", 0); }
} // namespace

int main() {
    std::map<std::string, std::string> h = {{"mcp-protocol-version", P::kModern}, {"mcp-method", "tools/call"}, {"mcp-name", "get_game_tree"}};
    json call = modernBody("tools/call", {{"name", "get_game_tree"}});
    auto ok = P::checkRequest(headers(h), call);
    check(ok.ok && ok.modern, "a proper 2026-07-28 tools/call passes");

    auto h2 = h; h2["mcp-name"] = "screenshot";
    check(code(P::checkRequest(headers(h2), call)) == P::kHeaderMismatch, "Mcp-Name that doesn't match the body: -32020");
    h2 = h; h2.erase("mcp-name");
    check(code(P::checkRequest(headers(h2), call)) == P::kHeaderMismatch, "missing Mcp-Name: -32020");
    h2 = h; h2["mcp-method"] = "tools/list";
    check(code(P::checkRequest(headers(h2), call)) == P::kHeaderMismatch, "Mcp-Method that doesn't match: -32020");
    h2 = h; h2.erase("mcp-protocol-version");
    auto noVersion = P::checkRequest(headers(h2), call);
    check(code(noVersion) == P::kHeaderMismatch && noVersion.status == 400, "missing MCP-Protocol-Version: 400 and -32020");
    h2 = h; h2["mcp-protocol-version"] = "2025-06-18";
    check(code(P::checkRequest(headers(h2), call)) == P::kHeaderMismatch, "header version different from _meta: -32020");

    json noCaps = call; noCaps["params"]["_meta"].erase(P::kMetaCapabilities);
    check(code(P::checkRequest(headers(h), noCaps)) == P::kInvalidParams, "missing clientCapabilities: -32602");

    auto bad = P::checkRequest(headers({{"mcp-protocol-version", "1900-01-01"}}), json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    check(code(bad) == P::kUnsupportedVersion && bad.error["data"]["requested"] == "1900-01-01" &&
          bad.error["data"]["supported"][0] == "2026-07-28", "unknown version: -32022 with the supported list");

    auto legacy = P::checkRequest(headers({{"mcp-protocol-version", "2025-06-18"}}), json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    check(legacy.ok && !legacy.modern, "a 2025-06-18 request uses the old rules");
    auto init = P::checkRequest(headers({{"mcp-protocol-version", P::kModern}}), json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"}});
    check(init.ok && !init.modern, "initialize always means the old rules");
    auto none = P::checkRequest(headers({}), json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}});
    check(none.ok && !none.modern, "no version at all: the old rules");

    auto batch = P::checkRequest(headers({{"mcp-protocol-version", P::kModern}}), json::array({call}));
    check(code(batch) == P::kInvalidRequest, "batches aren't part of 2026-07-28");

    json note = modernBody("notifications/cancelled", {{"requestId", 1}}, false);
    check(P::checkRequest(headers({{"mcp-protocol-version", P::kModern}, {"mcp-method", "notifications/cancelled"}}), note).ok,
          "a notification needs no Mcp-Name or capabilities");

    // base64 names, both ways (the stdio bridge writes them, the server reads them).
    json odd = modernBody("tools/call", {{"name", "h\xC3\xA9llo,tool"}});
    std::string hdrs = P::headersFor(odd);
    size_t at = hdrs.find("Mcp-Name: ");
    std::string sent = at == std::string::npos ? "" : hdrs.substr(at + 10, hdrs.find("\r\n", at) - at - 10);
    check(sent.rfind("=?base64?", 0) == 0, "an unsafe name is sent as =?base64?...?=");
    bool decoded = false;
    check(P::decodeHeaderValue(sent, decoded) == "h\xC3\xA9llo,tool" && decoded, "and decodes back to the same name");
    check(P::checkRequest(headers({{"mcp-protocol-version", P::kModern}, {"mcp-method", "tools/call"}, {"mcp-name", sent}}), odd).ok,
          "the server accepts the base64 name");
    P::decodeHeaderValue("=?base64?/w==?=", decoded);
    check(!decoded, "base64 that isn't UTF-8 is refused");
    check(P::decodeHeaderValue("=?base64?!!!?=", decoded) == "=?base64?!!!?=" && decoded, "not the base64 pattern: taken as is");
    check(P::headersFor(json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}}).empty(), "old-style stdio messages get no new headers");
    check(P::headersFor(modernBody("resources/read", {{"uri", "gutsbolts://guide"}})).find("Mcp-Name: gutsbolts://guide\r\n") != std::string::npos,
          "resources/read sends its address as Mcp-Name");

    // Answers.
    json info = {{"name", "guts-and-bolts-studio"}, {"version", "test"}};
    json reply = {{"jsonrpc", "2.0"}, {"id", 1}, {"result", {{"tools", json::array()}}}};
    P::modernize(reply, info);
    check(reply["result"]["resultType"] == "complete" && reply["result"]["_meta"][P::kMetaServerInfo] == info, "results get resultType and serverInfo");
    json gone = {{"jsonrpc", "2.0"}, {"id", 1}, {"error", {{"code", -32002}, {"message", "no"}}}};
    P::modernize(gone, info);
    check(gone["error"]["code"] == P::kInvalidParams, "a missing resource is -32602, not -32002");
    json d = P::discoverResult({{"tools", json::object()}}, "guide", info);
    check(d["resultType"] == "complete" && d["supportedVersions"].size() == 4 && d["instructions"] == "guide" &&
          d["_meta"][P::kMetaServerInfo] == info, "server/discover lists versions, the guide and who we are");

    std::cout << (failures ? "MCP server protocol tests FAILED\n" : "MCP server protocol tests passed\n");
    return failures ? 1 : 0;
}
