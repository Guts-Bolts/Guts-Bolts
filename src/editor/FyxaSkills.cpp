#include "FyxaSkills.h"

FyxaSkills::FyxaSkills(std::filesystem::path bundledDir, std::filesystem::path userDir)
    : m_bundledDir(std::move(bundledDir)), m_userDir(std::move(userDir)) {}

nlohmann::json FyxaSkills::list() const {
    return nlohmann::json::array();
}

bool FyxaSkills::load(const std::string&, std::string& content, std::string& error) const {
    content.clear();
    error = "Skill loader is not implemented.";
    return false;
}
