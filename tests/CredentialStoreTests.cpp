#include "core/CredentialStore.h"
#include <iostream>
#include <string>

int main() {
    std::string error, value;
    if (!CredentialStore::persistentAvailable()) {
        if (CredentialStore::store("test", "secret", error)) return 1;
        std::cout << "Credential store correctly reports session-only support.\n";
        return 0;
    }
    if (!CredentialStore::store("test-roundtrip", "fyxa-test-secret", error)) {
        std::cerr << error << "\n"; return 1;
    }
    if (!CredentialStore::retrieve("test-roundtrip", value, error) || value != "fyxa-test-secret") {
        std::cerr << "Credential round-trip failed: " << error << "\n"; return 1;
    }
    if (!CredentialStore::erase("test-roundtrip", error)) {
        std::cerr << error << "\n"; return 1;
    }
    std::cout << "Credential store round-trip passed.\n";
    return 0;
}
