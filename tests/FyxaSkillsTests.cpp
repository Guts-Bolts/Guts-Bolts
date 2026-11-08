#include "editor/FyxaSkills.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
namespace {
int failures = 0;
void check(bool ok, const char* message) {
    std::cout << (ok ? "ok    " : "FAIL  ") << message << "\n";
    if (!ok) ++failures;
}
void put(const fs::path& path, const std::string& data) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << data;
}
}

int main() {
    const fs::path root = fs::temp_directory_path() /
        ("gb-fyxa-skills-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path bundled = root / "bundled", user = root / "user";
    put(bundled / "studio-basics.md", "# Studio Basics\nUse the editor tools.");
    put(bundled / "lua-scripting.md", "# Lua Scripting\nUse Lua 5.4.");
    put(user / "studio-basics.md", "# My Studio Rules\nPrefer small edits.");
    put(user / "too-large.md", std::string(65537, 'x'));

    FyxaSkills skills(bundled, user);
    const auto entries = skills.list();
    check(entries.is_array() && entries.size() == 3, "lists bundled skills and user additions without duplicate names");
    bool overrideFound = false;
    for (const auto& item : entries)
        if (item.value("name", "") == "studio-basics" && item.value("source", "") == "user") overrideFound = true;
    check(overrideFound, "user skill overrides a bundled skill with the same name");

    std::string content, error;
    check(skills.load("studio-basics", content, error) && content.find("My Studio Rules") != std::string::npos,
          "loads user skill content");
    check(skills.load("lua-scripting", content, error) && content.find("Lua 5.4") != std::string::npos,
          "loads bundled skill content");
    check(!skills.load("../secret", content, error), "rejects path traversal");
    check(!skills.load("too-large", content, error), "rejects oversized skill files");

    std::error_code ec;
    fs::remove_all(root, ec);
    std::cout << (failures ? "Fyxa skills tests FAILED\n" : "Fyxa skills tests passed\n");
    return failures ? 1 : 0;
}
