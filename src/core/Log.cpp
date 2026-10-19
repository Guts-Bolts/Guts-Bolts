#include "Log.h"
#include <chrono>
#include <ctime>
#include <cstdio>

namespace Log {

namespace {
std::vector<Entry> g_entries;
unsigned long long g_count = 0;
constexpr size_t kMaxEntries = 2000;
int g_run = 0, g_lastRun = 0;
thread_local std::vector<Entry>* g_capture = nullptr;

std::string timestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

void push(Level level, const std::string& text) {
    if (g_capture) { g_capture->push_back({level, text, timestamp(), g_run}); return; }
    if (g_entries.size() >= kMaxEntries)
        g_entries.erase(g_entries.begin(), g_entries.begin() + kMaxEntries / 4);
    g_entries.push_back({level, text, timestamp(), g_run});
    ++g_count;
    // Also echo to the terminal — handy when launching from a command line.
    static const char* tags[] = {"", "[warn] ", "[error] ", "[editor] "};
    std::printf("%s%s\n", tags[(int)level], text.c_str());
    std::fflush(stdout);
}
} // namespace

void info  (const std::string& text) { push(Level::Info,   text); }
void warn  (const std::string& text) { push(Level::Warn,   text); }
void error (const std::string& text) { push(Level::Error,  text); }
void system(const std::string& text) { push(Level::System, text); }

const std::vector<Entry>& entries() { return g_entries; }
unsigned long long count() { return g_count; }
void clear() { g_entries.clear(); }

int  beginRun()   { g_run = ++g_lastRun; return g_run; }
void endRun()     { g_run = 0; }
int  currentRun() { return g_run; }
int  lastRun()    { return g_lastRun; }
void setCapture(std::vector<Entry>* sink) { g_capture = sink; }

} // namespace Log
