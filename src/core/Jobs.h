#pragma once
// Jobs: sharing work out over every processor core.
//
// One small team of helper threads is made the first time it's needed (one fewer than
// the computer has cores, at most 7; none on the web, where a page has one thread) and
// kept for the whole run. Code that has a long list of things that don't depend on
// each other (every drop of liquid, every soft body, every part to check against the
// camera) hands the list to Jobs::forEach and gets it back when every piece is done.
//
//   Jobs::forEach(count, [&](size_t first, size_t last) {
//       for (size_t i = first; i < last; ++i) work(i);
//   });
//
// Rules:
//  * Each i must only touch its own things (no two pieces writing the same memory).
//  * Nothing that talks to the graphics card: only the main thread may do that.
//  * A job may start another forEach inside it; that one just runs on the spot
//    (helpers never wait for helpers, so nothing can get stuck).
//
// Handing work out asks for no memory: the job is passed by reference, not copied
// into a std::function, and the team's lists are made once.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace Jobs {

// How many threads work on a forEach (the helpers plus the one that asked).
int threads();

// Split 0..count into pieces and run fn(first, last) on each, on every core. Returns
// when all are done. Lists shorter than `minimum` aren't worth splitting and run here.
template <typename Fn> void forEach(size_t count, Fn&& fn, size_t minimum = 64);

// The same, one call per item: fn(i).
template <typename Fn> void forEachItem(size_t count, Fn&& fn, size_t minimum = 8) {
    forEach(count, [&](size_t a, size_t b) { for (size_t i = a; i < b; ++i) fn(i); }, minimum);
}

// For tests: how many forEach calls were really shared out (not run on the spot).
uint64_t sharedCount();

namespace detail {

// A job seen without its type: a pointer to it and a function that knows how to call it.
struct Task {
    void* job = nullptr;
    void (*call)(void* job, size_t first, size_t last) = nullptr;
};

class Team {
public:
    static Team& get();
    int threads() const { return (int)m_helpers.size() + 1; }
    // True if it was shared out; false if the caller should just run it (no helpers,
    // or already inside a job).
    bool run(size_t count, const Task& task);
    uint64_t shared() const { return m_shared.load(); }

private:
    Team();
    ~Team();
    void work();
    void loop();
    std::vector<std::thread> m_helpers;
    std::mutex m_mutex;               // (one forEach at a time from outside a job)
    std::mutex m_state;
    std::condition_variable m_wake, m_done;
    Task m_task;
    size_t m_count = 0;
    int m_pieces = 0, m_busy = 0;
    uint64_t m_generation = 0;
    bool m_quit = false;
    std::atomic<int> m_next{0};
    std::atomic<uint64_t> m_shared{0};
};

bool insideJob();

} // namespace detail

template <typename Fn> void forEach(size_t count, Fn&& fn, size_t minimum) {
    if (count == 0) return;
    using F = std::remove_reference_t<Fn>;
    detail::Task task;
    task.job = const_cast<void*>(static_cast<const void*>(&fn));
    task.call = [](void* job, size_t a, size_t b) { (*static_cast<F*>(job))(a, b); };
    if (count < minimum || detail::insideJob() || !detail::Team::get().run(count, task)) fn((size_t)0, count);
}

} // namespace Jobs
