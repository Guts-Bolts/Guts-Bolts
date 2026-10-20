// GutsAndBoltsServer: keeps accounts, Bolts, badges and uploads for everyone.
//
//   GutsAndBoltsServer [--port 7780] [--data server_data] [--official <staff account id>] [--name "My Server"] [--behind-worker]
//
// --behind-worker: the website's Cloudflare worker passes requests on to this server (its
// GB_SERVER setting), and says who really sent each one, for the limits on expensive requests.
// Only use it when nobody can reach this server's port directly.
//
// Then, in the Guts&Bolts apps, connect to this computer's address (the site's
// "Server" button, or Studio's File > Guts&Bolts Server).
#include "Server.h"
#include "../online/Protocol.h"
#include "../core/Account.h"
#include "../net/Socket.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {
GbServer* g_server = nullptr;
void onSignal(int) { if (g_server) g_server->stop(); }
} // namespace

int main(int argc, char** argv) {
    GbServer::Options opts;
    opts.port = Online::kDefaultPort;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--port"))     opts.port = std::atoi(next());
        else if (!std::strcmp(argv[i], "--data"))     opts.data = next();
        else if (!std::strcmp(argv[i], "--official")) opts.official = next();
        else if (!std::strcmp(argv[i], "--name"))     opts.name = next();
        else if (!std::strcmp(argv[i], "--behind-worker")) opts.behindWorker = true;
        else if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("GutsAndBoltsServer [--port %d] [--data server_data] [--official <staff account id>] [--name \"My Server\"] [--behind-worker]\n",
                        Online::kDefaultPort);
            return 0;
        }
    }
    // Remember the settings next to the data, so double-clicking the server later just works.
    std::filesystem::path cfg = opts.data / "server.json";
    {
        std::ifstream f(cfg);
        nlohmann::json j = f ? nlohmann::json::parse(f, nullptr, false) : nlohmann::json();
        if (j.is_object()) {
            if (opts.official.empty()) opts.official = j.value("official", std::string());
            if (opts.name == "Guts&Bolts") opts.name = j.value("name", opts.name);
        }
    }
    if (!opts.official.empty()) Account::setOfficialId(opts.official);
    if (Account::officialId().empty()) {
        std::printf("Warning: no staff account is set, so nobody can use the staff tools.\n"
                    "Run with --official <your account ID> (from the site's Avatar page).\n\n");
    }

    GbServer server(opts);
    std::string error;
    if (!server.start(error)) {
        std::fprintf(stderr, "Couldn't start the server: %s\n", error.c_str());
        return 1;
    }
    {
        std::error_code ec;
        std::filesystem::create_directories(opts.data, ec);
        std::ofstream f(cfg);
        f << nlohmann::json{{"official", Account::officialId()}, {"name", opts.name}, {"port", opts.port}}.dump(2);
    }
    g_server = &server;
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    std::printf("Guts&Bolts server \"%s\" is running on port %d.\n", opts.name.c_str(), opts.port);
    std::string here = Net::localAddresses();
    if (!here.empty()) std::printf("People on your network can connect to: %s (port %d)\n", here.c_str(), opts.port);
    std::printf("Staff account: %s\n", Account::officialId().empty() ? "(none)" : Account::officialId().substr(0, 8).c_str());
    std::printf("Saving everything in: %s\n", std::filesystem::absolute(opts.data).string().c_str());
    std::printf("Press Ctrl+C to stop.\n\n");
    std::fflush(stdout);
    server.runForever();
    std::printf("Server stopped.\n");
    return 0;
}
