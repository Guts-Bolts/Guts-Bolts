#include "../src/editor/ExternalMcpOAuth.h"
#include <mbedtls/sha256.h>
#include <cstdlib>
#include <iostream>
#include <map>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAILED line " << __LINE__ << ": " #condition << "\n"; return EXIT_FAILURE; } } while (false)

int main() {
    using namespace McpOAuth;
    HttpsUrl parsed;
    CHECK(parseHttpsUrl("https://auth.example.com/tenant", parsed));
    CHECK(parsed.origin == "https://auth.example.com");
    CHECK(parsed.path == "/tenant");
    CHECK(!parseHttpsUrl("http://auth.example.com", parsed));
    CHECK(!parseHttpsUrl("https://user@auth.example.com", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com:99999", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com/%0d%0aHost", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com/%00", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com/%01", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com/%1f", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com/%7f", parsed));
    CHECK(!parseHttpsUrl("https://auth.example.com/%GG", parsed));
    CHECK(parseHttpsUrl("https://auth.example.com/path%20segment", parsed));

    const auto resourcePaths = protectedResourceMetadataUrls("https://mcp.example.com/api/mcp");
    CHECK(resourcePaths.size() == 2);
    CHECK(resourcePaths[0] == "https://mcp.example.com/.well-known/oauth-protected-resource/api/mcp");
    CHECK(resourcePaths[1] == "https://mcp.example.com/.well-known/oauth-protected-resource");
    const auto issuerPaths = authorizationServerMetadataUrls("https://auth.example.com/tenant");
    CHECK(issuerPaths.size() == 3);
    CHECK(issuerPaths[0] == "https://auth.example.com/.well-known/oauth-authorization-server/tenant");
    CHECK(issuerPaths[1] == "https://auth.example.com/.well-known/openid-configuration/tenant");
    CHECK(issuerPaths[2] == "https://auth.example.com/tenant/.well-known/openid-configuration");

    json metadata = {{"issuer", "https://auth.example.com/tenant"}};
    CHECK(metadataIssuerMatches(metadata, "https://auth.example.com/tenant"));
    CHECK(!metadataIssuerMatches(metadata, "https://auth.example.com/tenant/"));
    const std::string vaultKey = credentialBindingKey("Docs", "https://auth.example.com",
        "https://mcp.example.com/mcp", "client-123");
    CHECK(vaultKey.rfind("mcp-oauth-", 0) == 0 && vaultKey.size() == 74);
    CHECK(vaultKey != credentialBindingKey("Docs", "https://other.example.com",
        "https://mcp.example.com/mcp", "client-123"));
    CHECK(vaultKey != credentialBindingKey("Docs", "https://auth.example.com",
        "https://other.example.com/mcp", "client-123"));
    CHECK(vaultKey != credentialBindingKey("Docs", "https://auth.example.com",
        "https://mcp.example.com/mcp", "other-client"));

    std::string resourceMetadata;
    CHECK(extractQuotedParameter("Bearer realm=\"mcp\", resource_metadata=\"https://mcp.example/.well-known/oauth-protected-resource\", scope=\"files:read\"", "resource_metadata", resourceMetadata));
    CHECK(resourceMetadata == "https://mcp.example/.well-known/oauth-protected-resource");
    CHECK(!extractQuotedParameter("Bearer error=\"invalid_token\"", "resource_metadata", resourceMetadata));

    Pkce pkce;
    CHECK(createPkce(pkce));
    CHECK(pkce.verifier.size() >= 43 && pkce.verifier.size() <= 128);
    CHECK(pkce.challenge.size() == 43);
    CHECK(pkce.state.size() >= 40);
    CHECK(pkce.verifier.find_first_of("+/= ") == std::string::npos);
    std::uint8_t expectedDigest[32]{};
    CHECK(mbedtls_sha256(reinterpret_cast<const unsigned char*>(pkce.verifier.data()), pkce.verifier.size(), expectedDigest, 0) == 0);
    CHECK(pkce.challenge == base64Url(expectedDigest, sizeof(expectedDigest)));
    crypto_wipe(expectedDigest, sizeof(expectedDigest));
    CHECK(formEncode("a b&c") == "a%20b%26c");
    std::map<std::string, std::string> callback;
    CHECK(parseQuery("/oauth/callback?code=abc%2B123&state=s%20t", callback));
    CHECK(callback["code"] == "abc+123" && callback["state"] == "s t");
    CHECK(!parseQuery("/oauth/callback?state=one&state=two", callback));
    CHECK(!parseQuery("/oauth/callback?code=%0d%0a", callback));
    std::cout << "MCP OAuth discovery and PKCE tests passed\n";
    return EXIT_SUCCESS;
}
