#pragma once

#include <string>

namespace StudioRelayApproval {

enum class Decision {
    Execute,
    Denied,
    Expired,
    Disconnected,
    DefinitionChanged,
    PermissionRevoked
};

// Pure decision gate shared by the desktop relay and its regression tests.
// The caller must supply the current connection, fingerprint, and live permission state.
inline Decision evaluate(double now, double expiresAt, bool connected, bool approved,
                         const std::string& pinnedFingerprint,
                         const std::string& currentFingerprint,
                         bool requiresWrite, bool currentlyHasWrite) {
    if (!connected) return Decision::Disconnected;
    if (now >= expiresAt) return Decision::Expired;
    if (!approved) return Decision::Denied;
    if (pinnedFingerprint.empty() || currentFingerprint.empty() ||
        pinnedFingerprint != currentFingerprint) return Decision::DefinitionChanged;
    if (requiresWrite && !currentlyHasWrite) return Decision::PermissionRevoked;
    return Decision::Execute;
}

} // namespace StudioRelayApproval
