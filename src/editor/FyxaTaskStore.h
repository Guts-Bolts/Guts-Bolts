#pragma once

#include <filesystem>
#include <string>
#include <nlohmann/json.hpp>

class FyxaTaskStore {
public:
    explicit FyxaTaskStore(std::filesystem::path file);

    nlohmann::json list(std::string& error) const;
    nlohmann::json create(const std::string& title, const std::string& description, std::string& error);
    bool update(const std::string& id, const std::string& status, const std::string& note, std::string& error);
    std::string issueEvidence(const std::string& taskId, std::string& error);
    bool completeWithEvidence(const std::string& taskId, const std::string& evidenceId,
                              const std::string& note, std::string& error);

private:
    std::filesystem::path m_file;
};
