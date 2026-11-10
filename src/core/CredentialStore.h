#pragma once
#include <string>

namespace CredentialStore {
// Uses the operating system's credential vault where supported. Unsupported
// platforms return false so callers can keep credentials in memory only.
bool store(const std::string& key, const std::string& secret, std::string& error);
bool retrieve(const std::string& key, std::string& secret, std::string& error);
bool erase(const std::string& key, std::string& error);
bool persistentAvailable();
}
