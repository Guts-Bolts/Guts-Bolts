#include "editor/FyxaKnowledge.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using json = nlohmann::json;
int failures = 0;
void check(bool ok, const char* label) { std::cout << (ok ? "ok    " : "FAIL  ") << label << "\n"; if (!ok) ++failures; }
void put(const fs::path& p, const std::string& text) { fs::create_directories(p.parent_path()); std::ofstream o(p, std::ios::binary); o << text; }
int main() {
    const fs::path root = fs::temp_directory_path() / ("fyxa-knowledge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    put(root / "docs" / "lua.md", "Lua 5.4 scripts use ServerStorage and validated RemoteEvent requests.\n");
    put(root / "docs" / "physics.md", "Physics queries include bounding box overlap and actual part geometry.\n");
    put(root / "outside.md", "This must never be indexed because it is not in the manifest.\n");
    put(root / "sources.json", R"({"schema_version":1,"purpose":"reference_only","sources":[
        {"source_id":"lua","path":"docs/lua.md","authority":"first_party_repository","index_policy":"reference_only"},
        {"source_id":"phys","path":"docs/physics.md","authority":"first_party_repository","index_policy":"reference_only"},
        {"source_id":"bad","path":"../outside.md","authority":"first_party_repository","index_policy":"reference_only"},
        {"source_id":"private","path":"outside.md","authority":"creator_upload","index_policy":"reference_only"}
    ]})");
    FyxaKnowledge knowledge(root, root / "sources.json");
    std::string error;
    const auto lua = knowledge.search("RemoteEvent ServerStorage Lua", 5, error);
    check(error.empty() && !lua.empty() && lua[0].value("path", "") == "docs/lua.md", "retrieves relevant allowlisted source chunk");
    const auto phys = knowledge.search("geometry overlap physics", 5, error);
    check(error.empty() && !phys.empty() && phys[0].value("path", "") == "docs/physics.md", "ranks matching physics reference");
    const auto none = knowledge.search("outside manifest excluded", 5, error);
    check(none.empty(), "never indexes sources outside first-party allowlist");
    const auto invalid = knowledge.search("../secrets", 5, error);
    check(!error.empty() && invalid.empty(), "rejects invalid retrieval query");
    fs::remove_all(root);
    std::cout << (failures ? "FyxaKnowledge tests FAILED\n" : "FyxaKnowledge tests passed\n");
    return failures ? 1 : 0;
}
