#include "FyxaMemoryStore.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <system_error>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
using json = nlohmann::json;
std::mutex g_memoryMutex;
std::atomic<unsigned long long> g_memorySeq{0};
class MemoryFileLock {
public:
    explicit MemoryFileLock(const std::filesystem::path& file, std::string& error) {
        auto path = std::filesystem::path(file.string() + ".lock");
        std::error_code ec;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) { error = "Cannot create memory lock directory: " + ec.message(); return; }
#ifdef _WIN32
        m_handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (m_handle == INVALID_HANDLE_VALUE) { error = "Cannot open memory lock file (Windows error " + std::to_string(GetLastError()) + ")."; return; }
        if (!LockFileEx(m_handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &m_overlapped)) {
            error = "Cannot lock project memory (Windows error " + std::to_string(GetLastError()) + ").";
            CloseHandle(m_handle); m_handle = INVALID_HANDLE_VALUE; return;
        }
#elif defined(__EMSCRIPTEN__)
        (void)path; // Browser builds serialize in-process with g_memoryMutex.
#else
        m_fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0600);
        if (m_fd < 0) { error = "Cannot open memory lock file."; return; }
        if (::flock(m_fd, LOCK_EX) != 0) { error = "Cannot lock project memory."; ::close(m_fd); m_fd = -1; return; }
#endif
        m_locked = true;
    }
    ~MemoryFileLock() {
#ifdef _WIN32
        if (m_handle != INVALID_HANDLE_VALUE) { if (m_locked) UnlockFileEx(m_handle, 0, MAXDWORD, MAXDWORD, &m_overlapped); CloseHandle(m_handle); }
#elif !defined(__EMSCRIPTEN__)
        if (m_fd >= 0) { if (m_locked) ::flock(m_fd, LOCK_UN); ::close(m_fd); }
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

constexpr std::size_t kMaxProject = 120, kMaxTitle = 160, kMaxContent = 4096, kMaxEntries = 500;
std::string timestamp() { return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()); }
std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}
bool validText(const std::string& value, std::size_t max) { return !trim(value).empty() && value.size() <= max; }
bool readStore(const std::filesystem::path& file, json& entries, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) { if (ec) { error = "Cannot check memory file: " + ec.message(); return false; } entries = json::array(); return true; }
    const auto size = std::filesystem::file_size(file, ec);
    if (ec || size > 2 * 1024 * 1024) { error = "Memory file is unreadable or exceeds 2 MiB; it was not modified."; return false; }
    std::ifstream in(file, std::ios::binary);
    if (!in) { error = "Cannot open project memory."; return false; }
    json root = json::parse(in, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root["version"].is_number_integer() || root["version"] != 1 ||
        !root.contains("entries") || !root["entries"].is_array()) { error = "Project memory is invalid; it was left untouched."; return false; }
    for (const auto& item : root["entries"]) {
        if (!item.is_object() || !item.contains("id") || !item["id"].is_string() ||
            !item.contains("project") || !item["project"].is_string() ||
            !item.contains("title") || !item["title"].is_string() ||
            !item.contains("content") || !item["content"].is_string()) {
            error = "Project memory contains an invalid record; it was left untouched."; return false;
        }
    }
    entries = root["entries"];
    return true;
}
bool writeStore(const std::filesystem::path& file, const json& entries, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) { error = "Cannot create memory directory: " + ec.message(); return false; }
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(g_memorySeq.fetch_add(1));
    const auto temp = std::filesystem::path(file.string() + ".tmp-" + suffix);
    const std::string data = json{{"version",1},{"entries",entries}}.dump(2) + "\n";
    { std::ofstream out(temp, std::ios::binary | std::ios::trunc); if (!out) { error = "Cannot create memory temp file."; return false; } out.write(data.data(), (std::streamsize)data.size()); out.flush(); if (!out) { error = "Cannot write memory temp file."; out.close(); std::filesystem::remove(temp,ec); return false; } }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { error = "Cannot replace project memory (Windows error " + std::to_string(GetLastError()) + ")."; std::filesystem::remove(temp,ec); return false; }
#else
    std::filesystem::rename(temp, file, ec); if (ec) { error = "Cannot replace project memory: " + ec.message(); std::filesystem::remove(temp,ec); return false; }
#endif
    return true;
}
std::vector<std::string> terms(const std::string& text) {
    std::vector<std::string> out; std::string word;
    auto flush=[&]{ if(word.size()>1) out.push_back(word); word.clear(); };
    for(unsigned char ch:text){ if(std::isalnum(ch)) word.push_back((char)std::tolower(ch)); else flush(); } flush(); return out;
}
bool matchesProject(const json& entry,const std::string& project){return entry.value("project",std::string())==project;}
}
FyxaMemoryStore::FyxaMemoryStore(std::filesystem::path file):m_file(std::move(file)){}
json FyxaMemoryStore::list(const std::string& project,std::string& error)const{
    error.clear(); if(!validText(project,kMaxProject)){error="Project name must be 1 to 120 characters.";return json::array();}
    std::lock_guard lock(g_memoryMutex); MemoryFileLock fileLock(m_file,error); if(!fileLock.locked()) return json::array(); json entries; if(!readStore(m_file,entries,error))return json::array(); json out=json::array();
    for(const auto& e:entries)if(matchesProject(e,project))out.push_back(e);return out;
}
json FyxaMemoryStore::search(const std::string& project,const std::string& query,std::string& error)const{
    error.clear();if(!validText(project,kMaxProject)||!validText(query,512)){error="Project and search query must be non-empty and within their size limits.";return json::array();}
    const auto q=terms(query);if(q.empty()){error="Search query has no searchable terms.";return json::array();}
    std::lock_guard lock(g_memoryMutex);MemoryFileLock fileLock(m_file,error);if(!fileLock.locked())return json::array();json entries;if(!readStore(m_file,entries,error))return json::array();
    struct Item{json record;std::size_t score=0;};std::vector<Item> found;
    for(const auto&e:entries){if(!matchesProject(e,project))continue;const auto t=terms(e.value("title",std::string())+" "+e.value("content",std::string()));std::set<std::string> unique(t.begin(),t.end());std::size_t score=0;for(const auto& term:q)if(unique.count(term))score++;if(score)found.push_back({e,score});}
    std::stable_sort(found.begin(),found.end(),[](const Item&a,const Item&b){return a.score>b.score;});json out=json::array();for(const auto& item:found){if(out.size()>=20)break;out.push_back(item.record);}return out;
}
json FyxaMemoryStore::save(const std::string& project,const std::string& title,const std::string& content,std::string& error){
    error.clear();if(!validText(project,kMaxProject)||!validText(title,kMaxTitle)||!validText(content,kMaxContent)){error="Project, title, and memory content must be non-empty and within size limits.";return nullptr;}
    std::lock_guard lock(g_memoryMutex);MemoryFileLock fileLock(m_file,error);if(!fileLock.locked())return nullptr;json entries;if(!readStore(m_file,entries,error))return nullptr;if(entries.size()>=kMaxEntries){error="Project memory has reached its 500-entry limit.";return nullptr;}
    const auto id="memory-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(g_memorySeq.fetch_add(1));json item={{"id",id},{"project",trim(project)},{"title",trim(title)},{"content",trim(content)},{"created_at",timestamp()}};entries.push_back(item);if(!writeStore(m_file,entries,error))return nullptr;return item;
}
bool FyxaMemoryStore::remove(const std::string& project,const std::string& id,std::string& error){
    error.clear();if(!validText(project,kMaxProject)||!validText(id,96)){error="Project and memory ID are required.";return false;}
    std::lock_guard lock(g_memoryMutex);MemoryFileLock fileLock(m_file,error);if(!fileLock.locked())return false;json entries;if(!readStore(m_file,entries,error))return false;auto it=std::find_if(entries.begin(),entries.end(),[&](const json&e){return matchesProject(e,project)&&e.value("id",std::string())==id;});if(it==entries.end()){error="Memory was not found in this project.";return false;}entries.erase(it);return writeStore(m_file,entries,error);
}
