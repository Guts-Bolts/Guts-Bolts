#include "../src/editor/StudioRelayApproval.h"
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "FAILED line " << __LINE__ << ": " #x "\n"; return EXIT_FAILURE; } } while (false)

int main() {
    using StudioRelayApproval::Decision;
    using StudioRelayApproval::evaluate;
    constexpr double expires = 110.0;
    const std::string fingerprint = "sha256:tool-definition";

    CHECK(evaluate(50, expires, true, true, fingerprint, fingerprint, false, false) == Decision::Execute);
    CHECK(evaluate(110, expires, true, true, fingerprint, fingerprint, false, false) == Decision::Expired);
    CHECK(evaluate(50, expires, true, false, fingerprint, fingerprint, false, false) == Decision::Denied);
    CHECK(evaluate(50, expires, false, true, fingerprint, fingerprint, false, false) == Decision::Disconnected);
    CHECK(evaluate(50, expires, true, true, fingerprint, "sha256:changed", false, false) == Decision::DefinitionChanged);
    CHECK(evaluate(50, expires, true, true, fingerprint, fingerprint, true, false) == Decision::PermissionRevoked);
    CHECK(evaluate(50, expires, true, true, fingerprint, fingerprint, true, true) == Decision::Execute);
    CHECK(evaluate(50, expires, true, true, "", fingerprint, false, false) == Decision::DefinitionChanged);
    return EXIT_SUCCESS;
}
