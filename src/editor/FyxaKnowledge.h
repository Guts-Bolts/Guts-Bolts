#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

// Small dependency-free, source-allowlisted retrieval index for Fyxa.
// The manifest must label its corpus reference_only and first_party_repository.
class FyxaKnowledge {
public:
    explicit FyxaKnowledge(std::filesystem::path repositoryRoot,
                          std::filesystem::path manifestPath);
    nlohmann::json search(const std::string& query, std::size_t limit = 8,
                         std::string& error) const;
private:
    std::filesystem::path m_root;
    std::filesystem::path m_manifest;
};
