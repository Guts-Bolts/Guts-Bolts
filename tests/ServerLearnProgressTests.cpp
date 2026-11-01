// The standalone C++ server's account-backed Studio Learn progress.
#include "core/Account.h"
#include "online/Protocol.h"
#include "server/Server.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
json ask(GbServer& s, const std::string& op, const json& args = json::object()) {
    const long long t = Online::unixNow();
    const std::string nonce = Account::randomHex(8);
    json req = {{"op", op}, {"account", Account::id()}, {"time", t}, {"nonce", nonce}, {"args", args},
                {"sig", Account::sign(Online::requestText(op, Account::id(), t, nonce, args))}};
    return s.handle(req);
}
std::string error(const json& r) { return r.value("error", std::string()); }
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / ("gb-learn-" + std::to_string(std::rand()) + std::to_string(Online::unixNow()));
    fs::create_directories(dir / "account");
    setenv("GB_ACCOUNT_DIR", (dir / "account").string().c_str(), 1);
    GbServer::Options opts;
    opts.data = dir / "data";
    opts.official = Account::id();
    std::string err;
    auto startServer = [&]() -> std::unique_ptr<GbServer> {
        for (int tries = 0; tries < 20; ++tries) {
            opts.port = 20000 + std::rand() % 40000;
            auto s = std::make_unique<GbServer>(opts);
            if (s->start(err)) return s;
        }
        return nullptr;
    };
    std::srand((unsigned)Online::unixNow() ^ (unsigned)getpid());
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't start: " << err << "\n"; return 1; }
        json r = ask(*server, "learn.progress.save", {
            {"completed", json::array({"first-build", "tiny-obby", "first-build"})},
            {"current", "first-script"}, {"step", 2}
        });
        check(r.value("ok", false) && r["progress"]["completed"].size() == 2 &&
              r["progress"]["current"] == "first-script" && r["progress"]["step"] == 2 &&
              r["progress"]["updatedAt"].get<long long>() > 0,
              "save valid progress, step, timestamp and de-duplicate lesson IDs");
        r = ask(*server, "learn.progress.save", {{"completed", json::array({"first-script"})}, {"current", "tiny-obby"}, {"step", 1}});
        check(r.value("ok", false) && r["progress"]["completed"].size() == 3 &&
              r["progress"]["current"] == "tiny-obby" && r["progress"]["step"] == 1,
              "merge new completions without erasing another device's progress");
        r = ask(*server, "learn.progress.get");
        check(r.value("ok", false) && r["progress"]["completed"].size() == 3, "read the merged progress");
        check(error(ask(*server, "learn.progress.save", {{"completed", json::array({"not-a-lesson"})}})).find("unknown lesson") != std::string::npos,
              "reject unknown lesson IDs");
        check(!ask(*server, "learn.progress.save", {{"completed", json::array({"first-build"})}, {"current", 42}}).value("ok", false),
              "reject a non-string current lesson");
        check(!ask(*server, "learn.progress.save", {{"completed", json::array({"first-build"})}, {"current", "first-build"}, {"step", 3}}).value("ok", false),
              "reject an out-of-range lesson step");
        json many = json::array();
        for (int i = 0; i < 21; ++i) many.push_back("first-build");
        check(!ask(*server, "learn.progress.save", {{"completed", many}}).value("ok", false),
              "reject oversized progress records");
    }
    {
        auto server = startServer();
        if (!server) { std::cout << "FAIL  couldn't restart: " << err << "\n"; return 1; }
        json r = ask(*server, "learn.progress.get");
        check(r.value("ok", false) && r["progress"]["completed"].size() == 3 &&
              r["progress"]["current"] == "tiny-obby" && r["progress"]["step"] == 1 &&
              r["progress"]["updatedAt"].get<long long>() > 0,
              "merged progress, lesson step and timestamp survive a server restart");
        Account::newKey();
        r = ask(*server, "learn.progress.get");
        check(!r.value("ok", false) && error(r).find("Sign in") != std::string::npos,
              "guests cannot read another account's progress");
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    std::cout << (failures ? "Studio Learn server tests FAILED\n" : "Studio Learn server tests passed\n");
    return failures ? 1 : 0;
}
