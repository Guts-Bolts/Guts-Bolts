#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace FyxaToolSelection {

// Provider-neutral tool entries: {name, description, input_schema, domain}.
nlohmann::json selectNative(const nlohmann::json& catalog, const std::string& prompt,
                             const std::vector<std::string>& discovered = {}, size_t limit = 24);

// Provider-neutral tool entries: {name, description, input_schema, domain}.
nlohmann::json selectExternal(const nlohmann::json& catalog, const std::string& prompt,
                               const std::vector<std::string>& discovered = {}, size_t limit = 12);

// Search the complete native and connected external catalogs. Results are metadata only;
// callers decide which returned names to expose as callable tools on the next model turn.
nlohmann::json discover(const nlohmann::json& nativeCatalog, const nlohmann::json& externalCatalog,
                        const std::string& query, size_t limit = 20);

}
