// Studio's multiplayer test (see MultiTest.h): a local test server and a few
// Player windows, started as separate programs whose output we read.
#include "MultiTest.h"
#include "../core/Log.h"
#include "../core/Paths.h"
#include "../net/NetGame.h"
#include "../net/Socket.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
double seconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

constexpr double kReadyWait = 30.0;    // the server has this long to load the game
constexpr size_t kMaxOutput = 4000;    // lines of the test's output kept for the AI tools
constexpr int    kFirstPort = 7790;    // test servers use 7790..7809 (7777 stays free for Host on LAN)
const char*      kReadyLine = "Test server running";

#ifdef _WIN32
int currentPid() { return (int)GetCurrentProcessId(); }
#else
int currentPid() { return (int)getpid(); }
#endif

// A free port on this computer for the test server.
int freePort() {
    for (int p = kFirstPort; p < kFirstPort + 20; ++p) {
        Net::Listener l;
        std::string err;
        if (l.open(p, err)) return p;   // (closed again straight away)
    }
    return 0;
}
} // namespace

// A program we started, with its stdout and stderr coming back through a pipe.
// A reader thread collects whole lines; update() takes them on the main thread.
struct MultiTest::Process {
    std::string name;   // "Server", "Player1"...
#ifdef _WIN32
    HANDLE proc = nullptr;
    HANDLE out = nullptr;
#else
    pid_t pid = -1;
    int   out = -1;
#endif
    std::thread reader;
    std::mutex  lock;
    std::vector<std::string> lines;   // waiting for update()
    bool        ended = false;        // it exited and we've said so

    bool start(const std::filesystem::path& program, const std::vector<std::string>& args, std::string& error);
    bool alive();
    void kill();          // asks it to stop (the window closes)
    void wait();          // waits for it to be gone (forcing it after a moment), then for the reader
    void read();          // (reader thread)
    std::vector<std::string> take() {
        std::lock_guard<std::mutex> g(lock);
        return std::move(lines);
    }
    ~Process() { kill(); wait(); }
};

void MultiTest::Process::read() {
    std::string partial;
    char buf[4096];
    while (true) {
#ifdef _WIN32
        DWORD n = 0;
        if (!ReadFile(out, buf, sizeof(buf), &n, nullptr) || n == 0) break;
#else
        ssize_t n = ::read(out, buf, sizeof(buf));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
#endif
        partial.append(buf, (size_t)n);
        size_t nl;
        std::lock_guard<std::mutex> g(lock);
        while ((nl = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            partial.erase(0, nl + 1);
        }
    }
    std::lock_guard<std::mutex> g(lock);
    if (!partial.empty()) lines.push_back(partial);
}

#ifdef _WIN32
namespace {
std::wstring quoted(const std::wstring& s) { return L"\"" + s + L"\""; }
}

bool MultiTest::Process::start(const std::filesystem::path& program, const std::vector<std::string>& args, std::string& error) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { error = "Couldn't make a pipe."; return false; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    // Only the pipe and NUL go to the new program (not Studio's other files and sockets).
    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    std::vector<char> attrs(size);
    auto list = (LPPROC_THREAD_ATTRIBUTE_LIST)attrs.data();
    InitializeProcThreadAttributeList(list, 1, 0, &size);
    HANDLE handles[2] = {wr, nul};
    UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                              (nul != INVALID_HANDLE_VALUE ? 2 : 1) * sizeof(HANDLE), nullptr, nullptr);

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = wr;
    si.StartupInfo.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : nullptr;
    si.lpAttributeList = list;
    std::wstring cmd = quoted(program.wstring());
    for (const auto& a : args) cmd += L" " + quoted(std::filesystem::path(a).wstring());
    std::wstring dir = program.parent_path().wstring();
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                             nullptr, dir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(list);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); error = "Couldn't start " + program.filename().string() + "."; return false; }
    CloseHandle(pi.hThread);
    proc = pi.hProcess;
    out = rd;
    reader = std::thread([this] { read(); });
    return true;
}

bool MultiTest::Process::alive() { return proc && WaitForSingleObject(proc, 0) == WAIT_TIMEOUT; }
void MultiTest::Process::kill() { if (alive()) TerminateProcess(proc, 0); }
void MultiTest::Process::wait() {
    if (proc) { WaitForSingleObject(proc, 3000); CloseHandle(proc); proc = nullptr; }
    if (reader.joinable()) reader.join();
    if (out) { CloseHandle(out); out = nullptr; }
}
#else
bool MultiTest::Process::start(const std::filesystem::path& program, const std::vector<std::string>& args, std::string& error) {
    int fds[2];
    if (pipe(fds) != 0) { error = "Couldn't make a pipe."; return false; }
    // Everything the child needs, made before fork (after it, only plain system calls are safe).
    std::vector<std::string> all = {program.string()};
    all.insert(all.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : all) argv.push_back(s.data());
    argv.push_back(nullptr);
    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 65536) maxFd = 65536;
    const std::string dir = program.parent_path().string();

    pid_t child = fork();
    if (child < 0) { close(fds[0]); close(fds[1]); error = "Couldn't start " + program.filename().string() + "."; return false; }
    if (child == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        int nul = open("/dev/null", O_RDONLY);
        if (nul >= 0) dup2(nul, 0);
        for (int fd = 3; fd < maxFd; ++fd) close(fd);   // not Studio's files and sockets (the MCP port...)
        if (chdir(dir.c_str()) != 0) {}
        execv(argv[0], argv.data());
        _exit(127);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFD, FD_CLOEXEC);
    pid = child;
    out = fds[0];
    reader = std::thread([this] { read(); });
    return true;
}

bool MultiTest::Process::alive() {
    if (pid <= 0) return false;
    int status = 0;
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) return true;
    if (r == pid || (r < 0 && errno == ECHILD)) pid = -1;   // gone (and reaped)
    return false;
}
void MultiTest::Process::kill() { if (alive()) ::kill(pid, SIGTERM); }
void MultiTest::Process::wait() {
    for (int i = 0; i < 150 && alive(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (alive()) { ::kill(pid, SIGKILL); waitpid(pid, nullptr, 0); pid = -1; }
    if (reader.joinable()) reader.join();
    if (out >= 0) { close(out); out = -1; }
}
#endif

// ---------------------------------------------------------------------------

MultiTest::MultiTest() = default;
MultiTest::~MultiTest() { stop(); }

bool MultiTest::start(const std::string& place, int players, std::string& error) {
    stop();
    m_output.clear();
    m_placeSnapshot.clear();
    m_sessionStartedEpoch = 0;
    m_sessionEndedEpoch = 0;
    m_lastDuration = 0.0;
    m_playerWindowsOpened = 0;
    const auto serverProgram = Paths::sibling("GutsAndBoltsGameServer");
    const auto playerProgram = Paths::sibling("GutsAndBoltsPlayer");
    std::error_code ec;
    if (!std::filesystem::exists(serverProgram, ec)) { error = "GutsAndBoltsGameServer isn't next to Studio (reinstall Guts&Bolts)."; return false; }
    if (!std::filesystem::exists(playerProgram, ec)) { error = "GutsAndBoltsPlayer isn't next to Studio (reinstall Guts&Bolts)."; return false; }
    m_port = freePort();
    if (!m_port) { error = "No free network port for the test server (ports " + std::to_string(kFirstPort) + " and up are in use)."; return false; }

    // The game as it is right now in Studio (saved or not), in a file the server can open.
    m_placeFile = (std::filesystem::temp_directory_path(ec) /
                   ("gutsandbolts-multiplayer-test-" + std::to_string(currentPid()) + ".gbscene")).string();
    {
        std::ofstream f(m_placeFile, std::ios::binary);
        f << place;
        if (!f) { error = "Couldn't write the game for the test server to " + m_placeFile; m_placeFile.clear(); return false; }
    }

    m_server = std::make_unique<Process>();
    m_server->name = "Server";
    if (!m_server->start(serverProgram, {"--place", m_placeFile, "--port", std::to_string(m_port)}, error)) {
        m_server.reset();
        stop();
        return false;
    }
    m_wanted = std::clamp(players, 1, kMostPlayers);
    m_ready = false;
    m_startedAt = seconds();
    m_sessionStartedEpoch = static_cast<int64_t>(std::time(nullptr));
    m_sessionEndedEpoch = 0;
    m_placeSnapshot = place;
    Log::system("Multiplayer test: starting a test server for " + std::to_string(m_wanted) + " players (port " +
                std::to_string(m_port) + ")...");
    return true;
}

bool MultiTest::startPlayer(std::string& error) {
    auto p = std::make_unique<Process>();
    p->name = "Player" + std::to_string(m_players.size() + 1);
    if (!p->start(Paths::sibling("GutsAndBoltsPlayer"),
                  {"--studio-test", "127.0.0.1:" + std::to_string(m_port), "--name", p->name}, error))
        return false;
    m_players.push_back(std::move(p));
    ++m_playerWindowsOpened;
    return true;
}

bool MultiTest::addPlayer(std::string& error) {
    if (!running()) { error = "There's no multiplayer test running."; return false; }
    if ((int)m_players.size() + m_wanted >= kMostPlayers) { error = "That's the most players a test can have."; return false; }
    if (!m_ready) { ++m_wanted; return true; }   // (opens with the others)
    return startPlayer(error);
}

void MultiTest::stop() {
    if (!m_server && m_players.empty() && m_placeFile.empty()) return;
    const bool was = running();
    if (was) {
        m_lastDuration = std::max(0.0, seconds() - m_startedAt);
        m_sessionEndedEpoch = static_cast<int64_t>(std::time(nullptr));
    }
    // Ask everything to stop at once, then wait for each (so it's quick).
    if (m_server) m_server->kill();
    for (auto& p : m_players) p->kill();
    for (auto& p : m_players) { p->wait(); take(*p); }
    if (m_server) { m_server->wait(); take(*m_server); }
    m_players.clear();
    m_server.reset();
    m_wanted = 0;
    m_ready = false;
    if (!m_placeFile.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_placeFile, ec);
        m_placeFile.clear();
    }
    if (was) Log::system("Multiplayer test stopped: the test server and the player windows are closed.");
}

void MultiTest::take(Process& p) {
    for (std::string& text : p.take()) {
        // Each program echoes its log with these tags (core/Log.cpp).
        Log::Level level = Log::Level::Info;
        const char* levelName = "info";
        auto strip = [&](const char* tag, Log::Level l, const char* n) {
            const size_t len = std::char_traits<char>::length(tag);
            if (text.compare(0, len, tag) != 0) return false;
            text.erase(0, len);
            level = l;
            levelName = n;
            return true;
        };
        strip("[warn] ", Log::Level::Warn, "warning") || strip("[error] ", Log::Level::Error, "error") ||
            strip("[editor] ", Log::Level::System, "system");
        if (&p == m_server.get() && text.rfind(kReadyLine, 0) == 0) m_ready = true;
        const std::string shown = "[" + p.name + "] " + text;
        switch (level) {
            case Log::Level::Warn:   Log::warn(shown); break;
            case Log::Level::Error:  Log::error(shown); break;
            case Log::Level::System: Log::system(shown); break;
            default:                 Log::info(shown); break;
        }
        const std::string time = Log::entries().empty() ? "" : Log::entries().back().time;
        m_output.push_back({p.name, levelName, text, time, std::max(0.0, seconds() - m_startedAt)});
        if (m_output.size() > kMaxOutput) m_output.pop_front();
    }
}

void MultiTest::update() {
    if (!m_server) return;
    take(*m_server);
    for (auto& p : m_players) {
        take(*p);
        if (!p->ended && !p->alive()) {
            p->ended = true;
            Log::system("[" + p->name + "] The window closed.");
        }
    }
    if (m_ready && m_wanted > 0) {
        std::string err;
        const int n = m_wanted;
        m_wanted = 0;
        for (int i = 0; i < n; ++i)
            if (!startPlayer(err)) { Log::error("Multiplayer test: " + err); break; }
        Log::system("Multiplayer test: " + std::to_string(m_players.size()) + " player windows are joining as Player1" +
                    (m_players.size() > 1 ? "..Player" + std::to_string(m_players.size()) : std::string()) + ".");
    }
    if (!m_server->alive()) {
        m_server->wait();
        take(*m_server);
        Log::error("Multiplayer test: the test server stopped" + std::string(m_ready ? "." : " before the game loaded."));
        stop();
        return;
    }
    if (!m_ready && seconds() - m_startedAt > kReadyWait) {
        Log::error("Multiplayer test: the test server didn't start in time.");
        stop();
    }
}

std::vector<MultiTest::Line> MultiTest::output(int lines, const std::string& from) const {
    std::vector<Line> out;
    for (auto it = m_output.rbegin(); it != m_output.rend() && (int)out.size() < lines; ++it)
        if (from.empty() || it->from == from) out.push_back(*it);
    std::reverse(out.begin(), out.end());
    return out;
}

bool MultiTest::exportSession(std::string& path, std::string& error) const {
    if (!hasSession()) {
        error = "There isn't a multiplayer test session to export yet.";
        return false;
    }

    std::error_code ec;
    const auto directory = std::filesystem::temp_directory_path(ec) / "GutsAndBoltsTestSessions";
    if (ec) {
        error = "Couldn't find a writable temporary folder for the session bundle.";
        return false;
    }
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "Couldn't create the session folder: " + directory.string();
        return false;
    }

    const int64_t ended = m_sessionEndedEpoch > 0 ? m_sessionEndedEpoch : static_cast<int64_t>(std::time(nullptr));
    const double duration = running() ? std::max(0.0, seconds() - m_startedAt) : m_lastDuration;
    nlohmann::json lines = nlohmann::json::array();
    for (const auto& line : m_output) {
        lines.push_back({
            {"elapsed_seconds", line.elapsedSeconds},
            {"source", line.from},
            {"level", line.level},
            {"text", line.text},
            {"display_time", line.time}
        });
    }
    std::sort(lines.begin(), lines.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.at("elapsed_seconds").get<double>() < b.at("elapsed_seconds").get<double>();
    });

    nlohmann::json bundle = {
        {"format", "gutsandbolts-multiplayer-session"},
        {"format_version", 1},
        {"started_at_epoch", m_sessionStartedEpoch},
        {"ended_at_epoch", ended},
        {"duration_seconds", duration},
        {"port", m_port},
        {"player_windows_opened", m_playerWindowsOpened},
        {"scene_snapshot", m_placeSnapshot},
        {"timeline", lines},
        {"limitations", {
            "This bundle captures the scene snapshot and timestamped process output for diagnosis.",
            "It does not record inputs or physics state and cannot deterministically replay gameplay.",
            "The scene snapshot may contain private scripts or unpublished game content; review before sharing."
        }}
    };

    path = (directory / ("multiplayer-session-" + std::to_string(m_sessionStartedEpoch) + ".json")).string();
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "Couldn't write the session bundle to " + path;
        return false;
    }
    file << bundle.dump(2) << '\n';
    if (!file) {
        error = "Writing the session bundle failed: " + path;
        return false;
    }
    return true;
}

nlohmann::json MultiTest::status() const {
    nlohmann::json players = nlohmann::json::array();
    for (auto& p : m_players) players.push_back({{"name", p->name}, {"running", !p->ended}});
    return {{"running", running()}, {"ready", m_ready}, {"port", running() ? m_port : 0},
            {"server", {{"running", running()}}}, {"players", players},
            {"players_waiting", m_ready ? 0 : m_wanted}};
}
