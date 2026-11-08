#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>

// Persistable shape for a future opt-in trust-tier system. Current production
// behavior remains per-call approval; this record never grants access by itself
// unless every binding and the expiry/revocation checks match.
struct FyxaApprovalRecord {
    std::string id;
    std::string scope = "tool";              // "tool" or "server_read"
    std::string serverId;
    std::string serverFingerprint;
    std::string toolName;                    // required for tool scope
    std::string toolFingerprint;             // required for tool scope
    std::string permission = "read";         // "read" or "write"
    std::int64_t issuedAtMs = 0;
    std::int64_t expiresAtMs = 0;
    std::int64_t revokedAtMs = 0;
    std::string grantedBy = "user";
    std::string auditEventId;

    bool validAt(std::int64_t nowMs) const {
        if (id.empty() || serverId.empty() || serverFingerprint.empty() ||
            issuedAtMs <= 0 || expiresAtMs <= issuedAtMs ||
            nowMs < issuedAtMs || nowMs >= expiresAtMs || revokedAtMs != 0)
            return false;
        if (permission != "read" && permission != "write") return false;
        if (scope == "server_read")
            return permission == "read" && toolName.empty() && toolFingerprint.empty();
        return scope == "tool" && !toolName.empty() && !toolFingerprint.empty();
    }

    bool matches(const std::string& requestedServerId,
                 const std::string& requestedServerFingerprint,
                 const std::string& requestedToolName,
                 const std::string& requestedToolFingerprint,
                 const std::string& requestedPermission,
                 std::int64_t nowMs) const {
        if (!validAt(nowMs) || serverId != requestedServerId ||
            serverFingerprint != requestedServerFingerprint ||
            permission != requestedPermission)
            return false;
        if (scope == "server_read") return requestedPermission == "read";
        return toolName == requestedToolName && toolFingerprint == requestedToolFingerprint;
    }

    nlohmann::json toJson() const {
        return {
            {"id", id}, {"scope", scope}, {"serverId", serverId},
            {"serverFingerprint", serverFingerprint}, {"toolName", toolName},
            {"toolFingerprint", toolFingerprint}, {"permission", permission},
            {"issuedAtMs", issuedAtMs}, {"expiresAtMs", expiresAtMs},
            {"revokedAtMs", revokedAtMs}, {"grantedBy", grantedBy},
            {"auditEventId", auditEventId}
        };
    }

    static bool fromJson(const nlohmann::json& value, FyxaApprovalRecord& out) {
        if (!value.is_object()) return false;
        const char* strings[] = {"id", "scope", "serverId", "serverFingerprint",
                                 "toolName", "toolFingerprint", "permission",
                                 "grantedBy", "auditEventId"};
        for (const char* key : strings)
            if (!value.contains(key) || !value[key].is_string()) return false;
        const char* numbers[] = {"issuedAtMs", "expiresAtMs", "revokedAtMs"};
        for (const char* key : numbers)
            if (!value.contains(key) || !value[key].is_number_integer()) return false;
        FyxaApprovalRecord parsed;
        parsed.id = value["id"].get<std::string>();
        parsed.scope = value["scope"].get<std::string>();
        parsed.serverId = value["serverId"].get<std::string>();
        parsed.serverFingerprint = value["serverFingerprint"].get<std::string>();
        parsed.toolName = value["toolName"].get<std::string>();
        parsed.toolFingerprint = value["toolFingerprint"].get<std::string>();
        parsed.permission = value["permission"].get<std::string>();
        parsed.issuedAtMs = value["issuedAtMs"].get<std::int64_t>();
        parsed.expiresAtMs = value["expiresAtMs"].get<std::int64_t>();
        parsed.revokedAtMs = value["revokedAtMs"].get<std::int64_t>();
        parsed.grantedBy = value["grantedBy"].get<std::string>();
        parsed.auditEventId = value["auditEventId"].get<std::string>();
        if (parsed.id.empty() || parsed.serverId.empty() || parsed.serverFingerprint.empty() ||
            parsed.issuedAtMs <= 0 || parsed.expiresAtMs <= parsed.issuedAtMs ||
            (parsed.permission != "read" && parsed.permission != "write") ||
            (parsed.scope != "tool" && parsed.scope != "server_read") ||
            (parsed.scope == "tool" && (parsed.toolName.empty() || parsed.toolFingerprint.empty())) ||
            (parsed.scope == "server_read" &&
             (parsed.permission != "read" || !parsed.toolName.empty() || !parsed.toolFingerprint.empty())))
            return false;
        out = std::move(parsed);
        return true;
    }
};
