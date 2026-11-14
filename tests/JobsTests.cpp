// Sharing work over every core (src/core/Jobs.h).
//
//   JobsTests
#include "core/Jobs.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    std::cout << "(" << Jobs::threads() << " threads)\n";
    check(Jobs::threads() >= 1 && Jobs::threads() <= 8, "a team of one to eight");

    // Every item is done exactly once.
    {
        std::vector<int> hits(100000, 0);
        Jobs::forEachItem(hits.size(), [&](size_t i) { hits[i] += 1; });
        bool once = true;
        for (int h : hits) if (h != 1) once = false;
        check(once, "every item is done exactly once");
    }
    // Pieces cover the whole list without overlapping, for awkward sizes too.
    for (size_t n : {1u, 2u, 7u, 63u, 64u, 65u, 1000u, 4097u}) {
        std::vector<std::atomic<int>> hits(n);
        for (auto& h : hits) h = 0;
        std::atomic<size_t> total{0};
        Jobs::forEach(n, [&](size_t a, size_t b) {
            for (size_t i = a; i < b; ++i) hits[i].fetch_add(1);
            total.fetch_add(b - a);
        }, 1);
        bool once = true;
        for (auto& h : hits) if (h.load() != 1) once = false;
        check(once && total.load() == n, "a list of " + std::to_string(n) + " is covered once");
    }
    // Nothing to do is fine.
    {
        bool called = false;
        Jobs::forEach(0, [&](size_t, size_t) { called = true; });
        check(!called, "an empty list does nothing");
    }
    // Short lists run on the spot.
    {
        const uint64_t before = Jobs::sharedCount();
        std::thread::id who;
        Jobs::forEach(10, [&](size_t, size_t) { who = std::this_thread::get_id(); }, 64);
        check(who == std::this_thread::get_id() && Jobs::sharedCount() == before, "a short list just runs here");
    }
    // Long lists really are shared out (when there are helpers).
    if (Jobs::threads() > 1) {
        const uint64_t before = Jobs::sharedCount();
        std::vector<std::thread::id> who(Jobs::threads() * 64);
        Jobs::forEachItem(who.size(), [&](size_t i) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
            who[i] = std::this_thread::get_id();
        }, 1);
        bool others = false;
        for (auto& id : who) if (id != std::this_thread::get_id()) others = true;
        check(Jobs::sharedCount() == before + 1 && others, "a long list is shared with the helpers");
        // And it's faster.
        auto time = [&](bool share) {
            const auto t0 = std::chrono::steady_clock::now();
            std::vector<double> out(Jobs::threads() * 2000);
            auto work = [&](size_t i) { double x = (double)i; for (int k = 0; k < 4000; ++k) x = std::sqrt(x + k); out[i] = x; };
            if (share) Jobs::forEachItem(out.size(), work, 1);
            else for (size_t i = 0; i < out.size(); ++i) work(i);
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        };
        time(true);
        const double alone = time(false), shared = time(true);
        std::cout << "  (alone " << alone * 1000.0 << " ms, shared " << shared * 1000.0 << " ms)\n";
        check(shared < alone * 0.85, "sharing makes it faster");
    } else {
        check(true, "(one core: nothing to share)");
    }
    // A job inside a job runs on the spot (and doesn't get stuck).
    {
        std::vector<int> grid(64 * 64, 0);
        Jobs::forEachItem(64, [&](size_t row) {
            Jobs::forEachItem(64, [&](size_t col) { grid[row * 64 + col] += 1; }, 1);
        }, 1);
        bool once = true;
        for (int g : grid) if (g != 1) once = false;
        check(once, "a job inside a job works");
    }
    // Two threads asking at once take turns.
    {
        std::vector<int> a(50000, 0), b(50000, 0);
        std::thread other([&] { for (int k = 0; k < 20; ++k) Jobs::forEachItem(b.size(), [&](size_t i) { b[i] += 1; }); });
        for (int k = 0; k < 20; ++k) Jobs::forEachItem(a.size(), [&](size_t i) { a[i] += 1; });
        other.join();
        bool right = true;
        for (size_t i = 0; i < a.size(); ++i) if (a[i] != 20 || b[i] != 20) right = false;
        check(right, "two threads asking at once both get the right answer");
    }
    // Many small rounds in a row (the way a frame uses it).
    {
        long long sum = 0;
        std::vector<long long> part(1000);
        for (int round = 0; round < 2000; ++round) {
            Jobs::forEachItem(part.size(), [&](size_t i) { part[i] = (long long)i * round; }, 1);
            sum += part[999];
        }
        long long want = 0;
        for (int round = 0; round < 2000; ++round) want += 999LL * round;
        check(sum == want, "2000 rounds in a row all come out right");
    }

    std::cout << (failures ? "FAILED" : "All jobs tests passed") << "\n";
    return failures ? 1 : 0;
}
