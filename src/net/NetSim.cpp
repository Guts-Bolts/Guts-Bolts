#include "NetSim.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace NetSim {

const std::vector<Preset>& presets() {
    // Round trip lag, wobble, lost packets, speed: everyday numbers for each kind of connection.
    static const std::vector<Preset> list = {
        {"Off",        {"Off", 0, 0, 0.0f, 0},            "Your own connection, nothing added."},
        {"Wired",      {"Wired", 10, 2, 0.0f, 0},         "A cable to the router: barely any lag."},
        {"Wi-Fi",      {"Wi-Fi", 40, 10, 0.5f, 20000},    "Home Wi-Fi, a room away from the router."},
        {"4G",         {"4G", 80, 25, 1.0f, 8000},        "A phone on 4G with good signal."},
        {"3G",         {"3G", 250, 60, 2.0f, 750},        "An older phone network: slow and laggy."},
        {"Bad signal", {"Bad signal", 500, 200, 5.0f, 250}, "The edge of coverage, on a train, in a basement."},
    };
    return list;
}

bool preset(const std::string& name, Conditions& out) {
    for (const Preset& p : presets())
        if (name == p.name) { out = p.c; return true; }
    return false;
}

Conditions clamp(Conditions c) {
    c.latencyMs = std::clamp(c.latencyMs, 0, 3000);
    c.jitterMs = std::clamp(c.jitterMs, 0, 1000);
    if (!std::isfinite(c.lossPercent)) c.lossPercent = 0;
    c.lossPercent = std::clamp(c.lossPercent, 0.0f, 50.0f);
    c.kbps = std::clamp(c.kbps, 0, 1000000);
    std::string name;
    for (char ch : c.name)
        if (ch != '|' && ch != '\n' && ch != '\r' && name.size() < 30) name += ch;
    c.name = name.empty() ? "Custom" : name;
    return c;
}

std::string toText(const Conditions& in) {
    const Conditions c = clamp(in);
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s|%d|%d|%.2f|%d", c.name.c_str(), c.latencyMs, c.jitterMs, c.lossPercent, c.kbps);
    return buf;
}

bool fromText(const std::string& text, Conditions& out) {
    std::vector<std::string> parts;
    std::stringstream ss(text);
    std::string bit;
    while (std::getline(ss, bit, '|')) parts.push_back(bit);
    if (parts.size() != 5) return false;
    Conditions c;
    c.name = parts[0];
    c.latencyMs = std::atoi(parts[1].c_str());
    c.jitterMs = std::atoi(parts[2].c_str());
    c.lossPercent = (float)std::atof(parts[3].c_str());
    c.kbps = std::atoi(parts[4].c_str());
    out = clamp(c);
    return true;
}

bool writeFile(const std::string& path, const Conditions& c) {
    // Write next to it, then swap in: a player reading at that moment never sees half a line.
    const std::string tmp = path + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << toText(c) << "\n";
        if (!f) return false;
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string statsPath(const std::string& file, const std::string& who) {
    std::string clean;
    for (char ch : who) clean += (std::isalnum((unsigned char)ch) ? ch : '_');
    return file + "." + clean + ".json";
}

namespace {
struct Live {
    std::string file, who;
    Conditions now;
    double readAt = -1.0;
};
Live& live() { static Live l; return l; }
double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
} // namespace

void useFile(const std::string& path, const std::string& who) {
    live().file = path;
    live().who = who.empty() ? "Player" : who;
    live().readAt = -1.0;
}

bool active() { return !live().file.empty(); }

const Conditions& current() {
    Live& l = live();
    if (l.file.empty()) return l.now;
    const double t = seconds();
    if (l.readAt < 0 || t - l.readAt >= 0.5) {
        l.readAt = t;
        std::ifstream f(l.file, std::ios::binary);
        std::string line;
        Conditions c;
        if (f && std::getline(f, line) && fromText(line, c)) l.now = c;
        else if (!f) l.now = Conditions{};   // (Studio took the file away: the test is over)
    }
    return l.now;
}

void writeStats(const std::string& jsonText) {
    if (!active()) return;
    const std::string path = statsPath(live().file, live().who), tmp = path + ".new";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << jsonText;
    }
    std::remove(path.c_str());
    std::rename(tmp.c_str(), path.c_str());
}

// ---------------------------------------------------------------------------

Pipe::Pipe() : m_rng(std::random_device{}()) {}

// When `bytes` may get through: half the lag plus or minus half the wobble, maybe a resend,
// squeezed through the speed limit, and never before the message ahead of it (TCP keeps order).
double Pipe::when(size_t bytes, double now, double& last) {
    const Conditions& c = current();
    double d = c.latencyMs / 2000.0;
    if (c.jitterMs > 0) {
        std::uniform_real_distribution<double> wobble(-c.jitterMs / 2000.0, c.jitterMs / 2000.0);
        d = std::max(0.0, d + wobble(m_rng));
    }
    if (c.lossPercent > 0) {
        std::uniform_real_distribution<double> roll(0.0, 100.0);
        // A big message is several packets: each one can be lost.
        const int packets = 1 + (int)(bytes / 1400);
        for (int i = 0; i < packets && i < 50; ++i)
            if (roll(m_rng) < c.lossPercent) { d += 0.2 + c.latencyMs / 1000.0; ++m_count.resent; break; }
    }
    double at = now + d;
    if (c.kbps > 0) {
        const double squeeze = (double)bytes * 8.0 / (c.kbps * 1000.0);
        at = std::max(at, std::max(last, now) + squeeze);
    }
    at = std::max(at, last);
    last = at;
    return at;
}

void Pipe::send(std::string bytes, double now) {
    ++m_count.msgsOut;
    m_count.bytesOut += bytes.size();
    const double at = when(bytes.size(), now, m_lastOut);
    m_out.push_back({at, std::move(bytes)});
}

void Pipe::releaseOut(std::string& out, double now) {
    while (!m_out.empty() && m_out.front().at <= now) {
        out += m_out.front().data;
        m_out.pop_front();
    }
}

void Pipe::arrive(std::string message, double now) {
    ++m_count.msgsIn;
    m_count.bytesIn += message.size() + 4;
    const double at = when(message.size() + 4, now, m_lastIn);
    m_in.push_back({at, std::move(message)});
}

bool Pipe::popIn(std::string& message, double now) {
    if (m_in.empty() || m_in.front().at > now) return false;
    message = std::move(m_in.front().data);
    m_in.pop_front();
    return true;
}

Pipe::Counters Pipe::counters() const {
    Counters c = m_count;
    c.waitingIn = m_in.size();
    c.waitingOut = m_out.size();
    return c;
}

} // namespace NetSim
