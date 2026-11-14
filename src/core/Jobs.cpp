#include "Jobs.h"

#include <algorithm>

namespace Jobs {

namespace detail {

namespace {
thread_local bool t_inside = false;   // this thread is running a piece of a job
}

bool insideJob() { return t_inside; }

Team& Team::get() {
    static Team team;
    return team;
}

Team::Team() {
#ifdef __EMSCRIPTEN__
    const unsigned helpers = 0;   // (a web page has one thread: everything runs on it)
#else
    const unsigned cores = std::thread::hardware_concurrency();
    const unsigned helpers = cores > 1 ? std::min(cores - 1, 7u) : 0u;
#endif
    m_helpers.reserve(helpers);
    for (unsigned i = 0; i < helpers; ++i) m_helpers.emplace_back([this] { loop(); });
}

Team::~Team() {
    { std::lock_guard<std::mutex> l(m_state); m_quit = true; }
    m_wake.notify_all();
    for (std::thread& t : m_helpers) t.join();
}

void Team::work() {
    t_inside = true;
    int piece;
    while ((piece = m_next.fetch_add(1)) < m_pieces) {
        const size_t a = m_count * (size_t)piece / (size_t)m_pieces;
        const size_t b = m_count * (size_t)(piece + 1) / (size_t)m_pieces;
        if (b > a) m_task.call(m_task.job, a, b);
    }
    t_inside = false;
}

void Team::loop() {
    uint64_t seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> l(m_state);
            m_wake.wait(l, [&] { return m_quit || m_generation != seen; });
            if (m_quit) return;
            seen = m_generation;
            ++m_busy;
        }
        work();
        {
            std::lock_guard<std::mutex> l(m_state);
            --m_busy;
        }
        m_done.notify_all();
    }
}

bool Team::run(size_t count, const Task& task) {
    if (m_helpers.empty()) return false;
    std::lock_guard<std::mutex> one(m_mutex);   // (two threads asking at once take turns)
    {
        std::lock_guard<std::mutex> l(m_state);
        m_task = task;
        m_count = count;
        // A few pieces per thread, so a slow piece doesn't hold everyone up.
        m_pieces = (int)std::min<size_t>(count, (size_t)threads() * 4);
        m_next = 0;
        ++m_generation;
    }
    m_wake.notify_all();
    work();
    {
        std::unique_lock<std::mutex> l(m_state);
        m_done.wait(l, [&] { return m_busy == 0 && m_next.load() >= m_pieces; });
    }
    m_shared.fetch_add(1);
    return true;
}

} // namespace detail

int threads() { return detail::Team::get().threads(); }
uint64_t sharedCount() { return detail::Team::get().shared(); }

} // namespace Jobs
