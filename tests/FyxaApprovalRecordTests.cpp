#include "../src/editor/FyxaApprovalRecord.h"

#include <cstdlib>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "FAILED line " << __LINE__ << ": " #condition "\n"; return EXIT_FAILURE; } } while (false)

int main() {
    FyxaApprovalRecord grant;
    grant.id = "grant-1";
    grant.scope = "tool";
    grant.serverId = "github";
    grant.serverFingerprint = "server-fp";
    grant.toolName = "create_issue";
    grant.toolFingerprint = "tool-fp";
    grant.permission = "write";
    grant.issuedAtMs = 1000;
    grant.expiresAtMs = 2000;
    grant.grantedBy = "user";
    grant.auditEventId = "audit-1";

    CHECK(grant.validAt(1500));
    CHECK(grant.matches("github", "server-fp", "create_issue", "tool-fp", "write", false, 1500));
    CHECK(!grant.matches("github", "server-fp", "delete_repo", "tool-fp", "write", 1500));
    CHECK(!grant.matches("github", "changed-server", "create_issue", "tool-fp", "write", 1500));
    CHECK(!grant.matches("github", "server-fp", "create_issue", "changed-tool", "write", 1500));
    CHECK(!grant.matches("github", "server-fp", "create_issue", "tool-fp", "read", 1500));
    CHECK(!grant.validAt(2000));

    FyxaApprovalRecord decoded;
    CHECK(FyxaApprovalRecord::fromJson(grant.toJson(), decoded));
    CHECK(decoded.matches("github", "server-fp", "create_issue", "tool-fp", "write", false, 1500));
    decoded.revokedAtMs = 1600;
    CHECK(!decoded.validAt(1700));

    FyxaApprovalRecord readGrant;
    readGrant.id = "grant-2";
    readGrant.scope = "server_read";
    readGrant.serverId = "github";
    readGrant.serverFingerprint = "server-fp";
    readGrant.permission = "read";
    readGrant.issuedAtMs = 1000;
    readGrant.expiresAtMs = 2000;
    readGrant.grantedBy = "user";
    readGrant.auditEventId = "audit-2";
    CHECK(readGrant.validAt(1500));
    CHECK(readGrant.matches("github", "server-fp", "list_repos", "any-tool-fp", "read", true, 1500));
    CHECK(!readGrant.matches("github", "server-fp", "list_repos", "any-tool-fp", "read", false, 1500));
    CHECK(!readGrant.matches("github", "server-fp", "create_issue", "any-tool-fp", "write", false, 1500));
    readGrant.permission = "write";
    CHECK(!readGrant.validAt(1500));
    CHECK(!FyxaApprovalRecord::fromJson(readGrant.toJson(), decoded));

    std::cout << "Fyxa approval record tests passed\n";
    return EXIT_SUCCESS;
}
