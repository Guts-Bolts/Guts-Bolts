#include "FyxaTaskStore.h"

#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace {
using json = nlohmann::json;
std::mutex g_taskFileMutex;

class TaskFileLock {
public:
    explicit TaskFileLock(const std::filesystem::path& file, std::string& error) {
        std::filesystem::path lockPath = file;
        lockPath += ".lock";
        std::error_code ec;
        if (!lockPath.parent_path().empty())
            std::filesystem::create_directories(lockPath.parent_path(), ec);
        if (ec) { error = "Cannot create task lock directory: " + ec.message(); return; }
#ifdef _WIN32
        m_handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) {
            error = "Cannot open task lock file (Windows error " + std::to_string(GetLastError()) + ").";
            return;
        }
        if (!LockFileEx(m_handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &m_overlapped)) {
            error = "Cannot lock task file (Windows error " + std::to_string(GetLastError()) + ").";
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
            return;
        }
#elif defined(__EMSCRIPTEN__)
        // The browser build has no cross-process filesystem locking primitive.
        // Its task store is serialized by the process mutex above.
        (void)lockPath;
#else
        m_fd = ::open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
        if (m_fd < 0) { error = "Cannot open task lock file."; return; }
        if (::flock(m_fd, LOCK_EX) != 0) {
            error = "Cannot lock task file.";
            ::close(m_fd);
            m_fd = -1;
            return;
        }
#endif
        m_locked = true;
    }
    ~TaskFileLock() {
#ifdef _WIN32
        if (m_handle != INVALID_HANDLE_VALUE) {
            if (m_locked) UnlockFileEx(m_handle, 0, MAXDWORD, MAXDWORD, &m_overlapped);
            CloseHandle(m_handle);
        }
#elif !defined(__EMSCRIPTEN__)
        if (m_fd >= 0) {
            if (m_locked) ::flock(m_fd, LOCK_UN);
            ::close(m_fd);
        }
#endif
    }
    bool locked() const { return m_locked; }
private:
    bool m_locked = false;
#ifdef _WIN32
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    OVERLAPPED m_overlapped{};
#else
    int m_fd = -1;
#endif
};
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
        !root["version"].is_number_integer() || root["version"] != 1 ||
        !root.contains("tasks") || !root["tasks"].is_array()) {
        error = "Task file is invalid. It was left untouched.";
        return false;
    }
    for (const auto& task : root["tasks"]) {
        if (!task.is_object() || !task.contains("id") || !task["id"].is_string() ||
            !task.contains("title") || !task["title"].is_string() ||
            !task.contains("status") || !task["status"].is_string() ||
            (task.contains("notes") && !task["notes"].is_array()) ||
            (task.contains("pending_evidence") && !task["pending_evidence"].is_array())) {
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

    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(g_taskSequence.fetch_add(1));
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
    TaskFileLock fileLock(m_file, error);
    if (!fileLock.locked()) return json::array();
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
    TaskFileLock fileLock(m_file, error);
    if (!fileLock.locked()) return nullptr;
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
    if (status == "completed") {
        error = "Task completion requires a valid one-time evidence ID. Use completeWithEvidence().";
        return false;
    }
    if (note.size() > kMaxText) { error = "Task note must be at most 4096 characters."; return false; }

    std::lock_guard lock(g_taskFileMutex);
    TaskFileLock fileLock(m_file, error);
    if (!fileLock.locked()) return false;
    json tasks;
    if (!readTasks(m_file, tasks, error)) return false;
    auto found = tasks.end();
    for (auto it = tasks.begin(); it != tasks.end(); ++it) {
        if (it->is_object() && it->value("id", std::string()) == id) { found = it; break; }
    }
    if (found == tasks.end()) { error = "Task not found."; return false; }
    if (status == "in_progress") {
        for (const auto& task : tasks) {
            if (task.is_object() && task.value("id", std::string()) != id &&
                task.value("status", std::string()) == "in_progress") {
                error = "Fyxa v1 supports one active task at a time. Pause or complete the active task before starting another.";
                return false;
            }
        }
    }

    (*found)["status"] = status;
    (*found)["updated_at"] = now();
    if (status != "in_progress") (*found)["pending_evidence"] = json::array();
    if (!note.empty()) {
        if (!found->contains("notes") || !(*found)["notes"].is_array()) (*found)["notes"] = json::array();
        (*found)["notes"].push_back({{"text", note}, {"created_at", now()}});
    }
    return writeTasks(m_file, tasks, error);
}


std::string FyxaTaskStore::issueEvidence(const std::string& taskId, std::string& error) {
    error.clear();
    if (taskId.empty()) { error = "An active task id is required to issue evidence."; return {}; }

    std::lock_guard lock(g_taskFileMutex);
    TaskFileLock fileLock(m_file, error);
    if (!fileLock.locked()) return {};
    json tasks;
    if (!readTasks(m_file, tasks, error)) return {};

    auto found = tasks.end();
    for (auto it = tasks.begin(); it != tasks.end(); ++it) {
        if (it->is_object() && it->value("id", std::string()) == taskId) { found = it; break; }
    }
    if (found == tasks.end()) { error = "Active task not found."; return {}; }
    if (found->value("status", std::string()) != "in_progress") {
        error = "Evidence can only be issued while the task is in_progress.";
        return {};
    }

    std::random_device random;
    std::ostringstream token;
    token << "fyxa-evidence-" << std::hex
          << std::chrono::steady_clock::now().time_since_epoch().count() << "-"
          << random() << "-" << random() << "-"
          << g_taskSequence.fetch_add(1);
    const std::string evidenceId = token.str();

    json pending = found->value("pending_evidence", json::array());
    if (!pending.is_array()) pending = json::array();
    if (pending.size() >= 64) pending.erase(pending.begin());
    pending.push_back({{"id", evidenceId}, {"created_at", now()}});
    (*found)["pending_evidence"] = std::move(pending);
    (*found)["updated_at"] = now();
    if (!writeTasks(m_file, tasks, error)) return {};
    return evidenceId;
}

bool FyxaTaskStore::completeWithEvidence(const std::string& taskId, const std::string& evidenceId,
                                         const std::string& note, std::string& error) {
    error.clear();
    if (taskId.empty() || evidenceId.empty()) {
        error = "Task id and evidence id are required.";
        return false;
    }
    if (note.empty() || note.size() > kMaxText) {
        error = "A verification note containing 1 to 4096 characters is required.";
        return false;
    }

    std::lock_guard lock(g_taskFileMutex);
    TaskFileLock fileLock(m_file, error);
    if (!fileLock.locked()) return false;
    json tasks;
    if (!readTasks(m_file, tasks, error)) return false;

    auto found = tasks.end();
    for (auto it = tasks.begin(); it != tasks.end(); ++it) {
        if (it->is_object() && it->value("id", std::string()) == taskId) { found = it; break; }
    }
    if (found == tasks.end()) { error = "Task not found."; return false; }
    if (found->value("status", std::string()) != "in_progress") {
        error = "The task must still be in_progress when it is completed.";
        return false;
    }
    const json pending = found->value("pending_evidence", json::array());
    bool validEvidence = false;
    if (pending.is_array()) {
        for (const auto& item : pending) {
            if (item.is_object() && item.value("id", std::string()) == evidenceId) {
                validEvidence = true;
                break;
            }
        }
    }
    if (!validEvidence) {
        error = "That evidence id is invalid, belongs to another task, or has already been consumed.";
        return false;
    }

    // Status transition, verification note, and one-time token consumption are
    // committed together while holding both the process mutex and OS file lock.
    (*found)["status"] = "completed";
    (*found)["updated_at"] = now();
    (*found)["pending_evidence"] = json::array();
    if (!found->contains("notes") || !(*found)["notes"].is_array()) (*found)["notes"] = json::array();
    (*found)["notes"].push_back({{"text", note}, {"created_at", now()}});
    return writeTasks(m_file, tasks, error);
}
