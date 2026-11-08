#include "editor/FyxaTaskStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    std::cout << (ok ? "ok    " : "FAIL  ") << message << "\n";
    if (!ok) ++failures;
}
}

int main() {
    const fs::path dir = fs::temp_directory_path() /
        ("gb-fyxa-tasks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const fs::path file = dir / "tasks.json";
    {
        FyxaTaskStore store(file);
        std::string error;
        const json created = store.create("Build the lobby", "Create the starting area", error);
        check(error.empty() && created.value("title", "") == "Build the lobby", "creates a task");
        check(created.value("status", "") == "pending" && !created.value("id", "").empty(), "new task has an id and pending status");
        const json tasks = store.list(error);
        check(error.empty() && tasks.is_array() && tasks.size() == 1, "lists the created task");
        check(store.update(created.value("id", ""), "in_progress", "Inspected the scene", error), "updates task status and note");
        check(error.empty(), "status update has no error");
    }
    {
        FyxaTaskStore reopened(file);
        std::string error;
        const json tasks = reopened.list(error);
        check(error.empty() && tasks.size() == 1 && tasks[0].value("status", "") == "in_progress",
              "persists tasks across store instances");
        check(!reopened.update("missing-task", "completed", "done", error), "rejects an unknown task id");
        check(!error.empty(), "unknown task reports an error");
        check(!reopened.update(tasks[0].value("id", ""), "finished", "", error), "rejects an invalid status");
    }
    {
        FyxaTaskStore store(dir / "invalid.json");
        std::string error;
        check(store.create(" ", "description", error).is_null(), "rejects an empty title");
        check(!error.empty(), "empty title reports an error");
    }
    {
        const fs::path broken = dir / "broken.json";
        { std::ofstream out(broken); out << "{not-json"; }
        FyxaTaskStore store(broken);
        std::string error;
        const json tasks = store.list(error);
        check(tasks.empty() && !error.empty(), "reports malformed task storage");
        check(!store.create("Do not overwrite", "", error).is_object(), "does not overwrite malformed task storage");
        std::ifstream in(broken);
        std::string preserved((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        check(preserved == "{not-json", "leaves malformed task file untouched");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "Fyxa task store tests FAILED\n" : "Fyxa task store tests passed\n");
    return failures ? 1 : 0;
}
