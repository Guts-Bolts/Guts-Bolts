#pragma once
#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

// User-approved project notes. Stored locally in the app data directory only.
class FyxaMemoryStore {
public:
    explicit FyxaMemoryStore(std::filesystem::path file);
    nlohmann::json list(const std::string& project, std::string& error) const;
    nlohmann::json search(const std::string& project, const std::string& query, std::string& error) const;
    nlohmann::json save(const std::string& project, const std::string& title,
                        const std::string& content, std::string& error);
    bool remove(const std::string& project, const std::string& id, std::string& error);
private:
    std::filesystem::path m_file;
};
