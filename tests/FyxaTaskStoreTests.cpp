#include "editor/FyxaTaskStore.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <atomic>

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
        check(tasks[0].contains("notes") && tasks[0]["notes"].size() == 1 &&
              tasks[0]["notes"][0].value("text", "") == "Inspected the scene",
              "persists task progress notes");
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
    {
        const fs::path evidenceFile = dir / "evidence.json";
        FyxaTaskStore store(evidenceFile);
        std::string error;
        const json taskA = store.create("Verify build", "", error);
        const json taskB = store.create("Verify deployment", "", error);
        const std::string idA = taskA.value("id", "");
        const std::string idB = taskB.value("id", "");
        check(store.update(idA, "in_progress", "", error), "starts the evidence-bound task");
        const std::string evidence = store.issueEvidence(idA, error);
        check(error.empty() && !evidence.empty(), "issues evidence only for the active task");
        check(!store.completeWithEvidence(idB, evidence, "wrong task", error),
              "rejects evidence presented for a different task");
        check(store.completeWithEvidence(idA, evidence, "Build verification passed", error),
              "atomically completes a task with valid evidence and a note");
        check(!store.completeWithEvidence(idA, evidence, "replay", error),
              "rejects replay of consumed evidence");
        check(!store.update(idB, "completed", "bypass", error),
              "prevents bypassing evidence through the ordinary update API");
        const json tasks = store.list(error);
        bool completed = false;
        for (const auto& task : tasks) {
            if (task.value("id", "") == idA) {
                completed = task.value("status", "") == "completed" &&
                    task.value("pending_evidence", json::array()).empty() &&
                    task.value("notes", json::array()).size() == 1;
            }
        }
        check(completed, "persists completion, verification note, and token consumption together");
    }
    {
        const fs::path raceFile = dir / "evidence-race.json";
        FyxaTaskStore store(raceFile);
        std::string error;
        const json task = store.create("Concurrent completion", "", error);
        const std::string id = task.value("id", "");
        check(store.update(id, "in_progress", "", error), "starts the concurrent completion task");
        const std::string evidence = store.issueEvidence(id, error);
        std::atomic<int> successes{0};
        auto complete = [&]() {
            FyxaTaskStore contender(raceFile);
            std::string localError;
            if (contender.completeWithEvidence(id, evidence, "Verified once", localError))
                ++successes;
        };
        std::thread first(complete), second(complete);
        first.join();
        second.join();
        check(successes.load() == 1, "allows exactly one winner for concurrent evidence completion");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "Fyxa task store tests FAILED\n" : "Fyxa task store tests passed\n");
    return failures ? 1 : 0;
}
