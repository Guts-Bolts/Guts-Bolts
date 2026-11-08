#include "FyxaTaskStore.h"

FyxaTaskStore::FyxaTaskStore(std::filesystem::path file) : m_file(std::move(file)) {}

nlohmann::json FyxaTaskStore::list(std::string& error) const {
    error = "Task store is not implemented.";
    return nlohmann::json::array();
}

nlohmann::json FyxaTaskStore::create(const std::string&, const std::string&, std::string& error) {
    error = "Task store is not implemented.";
    return nlohmann::json::object();
}

bool FyxaTaskStore::update(const std::string&, const std::string&, const std::string&, std::string& error) {
    error = "Task store is not implemented.";
    return false;
}
