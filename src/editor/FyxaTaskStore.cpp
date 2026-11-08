#include "FyxaTaskStore.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
using json = nlohmann::json;
std::mutex g_taskFileMutex;
std::atomic<unsigned long long> g_taskSequence{0};
constexpr std::size_t kMaxTitle = 160;
constexpr std::size_t kMaxText = 4096;

std::string now() {
    return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

bool validStatus(const std::string& status) {
    return status == "pending" || status == "in_progress" ||
           status == "blocked" || status == "completed";
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

bool readTasks(const std::filesystem::path& file, json& tasks, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        if (ec) { error = "Cannot inspect task file: " + ec.message(); return false; }
        tasks = json::array();
        return true;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) { error = "Cannot open task file."; return false; }
    json root = json::parse(in, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains("version") ||
        !root["version"].is_number_integer() || root["version"].get<int>() != 1 ||
        !root.contains("tasks") || !root["tasks"].is_array()) {
        error = "Task file is invalid. It was left untouched.";
        return false;
    }
    for (const auto& task : root["tasks"]) {
        if (!task.is_object() || !task.contains("id") || !task["id"].is_string() ||
            !task.contains("title") || !task["title"].is_string() ||
            !task.contains("status") || !task["status"].is_string() ||
            (task.contains("notes") && !task["notes"].is_array())) {
            error = "Task file contains an invalid task. It was left untouched.";
            return false;
        }
    }
    tasks = std::move(root["tasks"]);
    return true;
}

bool writeTasks(const std::filesystem::path& file, const json& tasks, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) { error = "Cannot create task directory: " + ec.message(); return false; }

    const auto suffix = std::to_string(g_taskSequence.fetch_add(1));
    const std::filesystem::path temp = file.string() + ".tmp-" + suffix;
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Cannot create temporary task file."; return false; }
        const json root = {{"version", 1}, {"tasks", tasks}};
        const std::string data = root.dump(2) + "\n";
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            error = "Cannot write temporary task file.";
            out.close();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Cannot replace task file (Windows error " + std::to_string(GetLastError()) + ").";
        std::filesystem::remove(temp, ec);
        return false;
    }
#else
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        error = "Cannot replace task file: " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
#endif
    return true;
}
} // namespace

FyxaTaskStore::FyxaTaskStore(std::filesystem::path file) : m_file(std::move(file)) {}

nlohmann::json FyxaTaskStore::list(std::string& error) const {
    error.clear();
    std::lock_guard lock(g_taskFileMutex);
    json tasks;
    if (!readTasks(m_file, tasks, error)) return json::array();
    return tasks;
}

nlohmann::json FyxaTaskStore::create(const std::string& title, const std::string& description, std::string& error) {
    error.clear();
    const std::string cleanTitle = trim(title);
    if (cleanTitle.empty() || cleanTitle.size() > kMaxTitle) {
        error = "Task title must contain 1 to 160 characters.";
        return nullptr;
    }
    if (description.size() > kMaxText) {
        error = "Task description must be at most 4096 characters.";
        return nullptr;
    }

    std::lock_guard lock(g_taskFileMutex);
    json tasks;
    if (!readTasks(m_file, tasks, error)) return nullptr;
    const std::string id = "task-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(g_taskSequence.fetch_add(1));
    const std::string timestamp = now();
    json task = {
        {"id", id}, {"title", cleanTitle}, {"description", description},
        {"status", "pending"}, {"created_at", timestamp}, {"updated_at", timestamp},
        {"notes", json::array()}
    };
    tasks.push_back(task);
    if (!writeTasks(m_file, tasks, error)) return nullptr;
    return task;
}

bool FyxaTaskStore::update(const std::string& id, const std::string& status,
                          const std::string& note, std::string& error) {
    error.clear();
    if (id.empty()) { error = "Task id is required."; return false; }
    if (!validStatus(status)) {
        error = "Status must be pending, in_progress, blocked, or completed.";
        return false;
    }
    if (note.size() > kMaxText) { error = "Task note must be at most 4096 characters."; return false; }

    std::lock_guard lock(g_taskFileMutex);
    json tasks;
    if (!readTasks(m_file, tasks, error)) return false;
    auto found = tasks.end();
    for (auto it = tasks.begin(); it != tasks.end(); ++it) {
        if (it->is_object() && it->value("id", std::string()) == id) { found = it; break; }
    }
    if (found == tasks.end()) { error = "Task not found."; return false; }

    (*found)["status"] = status;
    (*found)["updated_at"] = now();
    if (!note.empty()) {
        if (!found->contains("notes") || !(*found)["notes"].is_array()) (*found)["notes"] = json::array();
        (*found)["notes"].push_back({{"text", note}, {"created_at", now()}});
    }
    return writeTasks(m_file, tasks, error);
}
