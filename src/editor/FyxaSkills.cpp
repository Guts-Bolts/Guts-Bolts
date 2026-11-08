#include "FyxaSkills.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace {
using json = nlohmann::json;
constexpr std::uintmax_t kMaxSkillBytes = 64 * 1024;

bool validName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (unsigned char ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) return false;
    return true;
}

bool regularSkill(const std::filesystem::path& path, std::uintmax_t& size) {
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status)) return false;
    size = std::filesystem::file_size(path, ec);
    return !ec && size <= kMaxSkillBytes;
}

std::string firstLine(const std::string& content) {
    std::istringstream input(content);
    std::string line;
    while (std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;
        line.erase(0, first);
        if (!line.empty() && line[0] == '#') {
            const auto title = line.find_first_not_of("# ");
            line = title == std::string::npos ? std::string() : line.substr(title);
        }
        if (line.size() > 200) line.resize(200);
        return line;
    }
    return {};
}

bool readSkill(const std::filesystem::path& path, std::string& content, std::string& error) {
    std::uintmax_t size = 0;
    if (!regularSkill(path, size)) {
        error = "Skill file is missing, not a regular file, or exceeds the 64 KiB limit.";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "Cannot open skill file."; return false; }
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (content.size() != size || content.size() > kMaxSkillBytes) {
        content.clear();
        error = "Skill file could not be read completely or exceeds the 64 KiB limit.";
        return false;
    }
    return true;
}

void collect(const std::filesystem::path& dir, const char* source, std::map<std::string, json>& skills) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const auto& entry = *it;
        if (entry.path().extension() != ".md") continue;
        const std::string name = entry.path().stem().string();
        if (!validName(name)) continue;
        std::uintmax_t size = 0;
        if (!regularSkill(entry.path(), size)) continue;
        std::string content, error;
        if (!readSkill(entry.path(), content, error)) continue;
        skills.emplace(name, json{{"name", name}, {"description", firstLine(content)}, {"source", source}});
    }
}
} // namespace

FyxaSkills::FyxaSkills(std::filesystem::path bundledDir, std::filesystem::path userDir)
    : m_bundledDir(std::move(bundledDir)), m_userDir(std::move(userDir)) {}

nlohmann::json FyxaSkills::list() const {
    std::map<std::string, json> skills;
    // User skills are inserted first and therefore override bundled names.
    collect(m_userDir, "user", skills);
    collect(m_bundledDir, "bundled", skills);
    json result = json::array();
    for (const auto& [name, item] : skills) result.push_back(item);
    return result;
}

bool FyxaSkills::load(const std::string& name, std::string& content, std::string& error) const {
    content.clear();
    if (!validName(name)) {
        error = "Skill name must contain only lowercase letters, digits, and hyphens.";
        return false;
    }
    const auto userPath = m_userDir / (name + ".md");
    std::error_code ec;
    const auto userStatus = std::filesystem::symlink_status(userPath, ec);
    if (!ec && std::filesystem::exists(userStatus))
        return readSkill(userPath, content, error);
    ec.clear();
    return readSkill(m_bundledDir / (name + ".md"), content, error);
}
