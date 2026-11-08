#include "../src/net/Socket.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main() {
    Net::HttpServer server;
    std::string error;
    constexpr int port = 49381;
    assert(server.open(port, error));

    std::atomic_bool serverStop{false};
    std::atomic_bool requestReceived{false};
    std::thread serverThread([&] {
        while (!serverStop.load()) {
            server.poll();
            Net::HttpServer::Request request{};
            if (server.next(request)) requestReceived.store(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::atomic_bool cancelled{false};
    std::thread cancelThread([&] {
        while (!requestReceived.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        cancelled.store(true);
    });

    std::string reply, requestError;
    int status = 0;
    const auto started = std::chrono::steady_clock::now();
    const bool ok = Net::httpPost("127.0.0.1", port, "/stall", "{}", reply, requestError,
                                  10000, "", &status, nullptr, &cancelled);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    cancelThread.join();
    serverStop.store(true);
    serverThread.join();

    assert(requestReceived.load());
    assert(!ok);
    assert(requestError == "Request cancelled.");
    assert(elapsed < std::chrono::seconds(1));

    std::cout << "HTTP request cancellation test passed\\n";
    return 0;
}
