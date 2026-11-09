#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

class FyxaSkills {
public:
    FyxaSkills(std::filesystem::path bundledDir, std::filesystem::path userDir);

    nlohmann::json list() const;
    bool load(const std::string& name, std::string& content, std::string& error) const;

private:
    std::filesystem::path m_bundledDir;
    std::filesystem::path m_userDir;
};
