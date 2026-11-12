#pragma once
// Frame memory: one big block that is handed out piece by piece while a frame is
// drawn, and taken back all at once when the next frame starts.
//
// Why: asking the system for memory (new / malloc, which is what a std::vector does
// when it grows) is slow and not the same speed every time, so doing it every frame
// makes the frame time wobble. Here a piece of memory is just "move a pointer along":
// no search, no lock, and everything a frame used sits next to each other.
//
// Rules for what goes in:
//  * only plain data (positions, matrices, pointers, numbers): nothing whose
//    destructor has to run, because reset() never runs one;
//  * nothing kept past the frame: after reset() the memory is used again.
//
// If a frame needs more than the block holds, the extra comes from the system that
// one time (so nothing breaks), and reset() makes the block big enough for next time.
// After the first few frames the block has found its size and never asks again.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

class FrameArena {
public:
    explicit FrameArena(size_t bytes = 256 * 1024) { grow(bytes); }
    ~FrameArena() { freeSpill(); std::free(m_block); }
    FrameArena(const FrameArena&)            = delete;
    FrameArena& operator=(const FrameArena&) = delete;

    // `bytes` of memory lined up to `align` (a power of two). Never null.
    void* allocate(size_t bytes, size_t align = alignof(std::max_align_t)) {
        // (Lined up by the real address, not by how far into the block we are.)
        const uintptr_t base = reinterpret_cast<uintptr_t>(m_block);
        const size_t at = (size_t)(((base + m_used + (align - 1)) & ~uintptr_t(align - 1)) - base);
        if (at + bytes <= m_size) {
            m_used = at + bytes;
            if (m_used > m_most) m_most = m_used;
            return m_block + at;
        }
        return spill(bytes, align);
    }

    // Room for `count` things of one kind (not filled in).
    template <typename T> T* make(size_t count = 1) {
        static_assert(std::is_trivially_destructible<T>::value, "frame memory never runs destructors");
        return static_cast<T*>(allocate(sizeof(T) * count, alignof(T)));
    }

    // A new frame: everything handed out is free again. (The only place the block may
    // be made bigger, and only when the frame before didn't fit.)
    void reset() {
        if (m_spill) {
            const size_t want = m_most + m_spilled + m_spilled / 2;
            freeSpill();
            std::free(m_block);
            m_block = nullptr;
            grow(want);
            ++m_grown;
        }
        m_used = 0;
        m_most = 0;
    }

    size_t used() const     { return m_used; }
    size_t capacity() const { return m_size; }
    int    timesGrown() const { return m_grown; }                 // how often reset() had to make it bigger
    bool   overflowed() const { return m_spill != nullptr; }      // this frame didn't fit (yet)

private:
    struct Spill { Spill* next; };   // extra pieces, each with this in front
    void grow(size_t bytes) {
        m_size = (bytes + 63) & ~size_t(63);
        m_block = static_cast<uint8_t*>(std::malloc(m_size));
        if (!m_block) throw std::bad_alloc();
    }
    void* spill(size_t bytes, size_t align) {
        const size_t head = (sizeof(Spill) + (align - 1)) & ~(align - 1);
        void* raw = std::malloc(head + bytes + align);
        if (!raw) throw std::bad_alloc();
        auto* s = static_cast<Spill*>(raw);
        s->next = m_spill;
        m_spill = s;
        m_spilled += bytes + align;
        const uintptr_t p = (reinterpret_cast<uintptr_t>(raw) + sizeof(Spill) + (align - 1)) & ~uintptr_t(align - 1);
        return reinterpret_cast<void*>(p);
    }
    void freeSpill() {
        while (m_spill) { Spill* n = m_spill->next; std::free(m_spill); m_spill = n; }
        m_spilled = 0;
    }

    uint8_t* m_block = nullptr;
    size_t   m_size = 0, m_used = 0, m_most = 0, m_spilled = 0;
    Spill*   m_spill = nullptr;
    int      m_grown = 0;
};

// A list that lives in frame memory: use it like a std::vector for a frame's worth of
// plain things (the lights near the camera, what to draw...). Growing takes a bigger
// piece of the arena and copies across; nothing is given back until the frame ends.
template <typename T> class FrameList {
    static_assert(std::is_trivially_copyable<T>::value, "FrameList holds plain data only");
public:
    explicit FrameList(FrameArena& arena, size_t room = 16) : m_arena(&arena) { reserve(room); }

    void push_back(const T& v) {
        if (m_count == m_room) reserve(m_room ? m_room * 2 : 16);
        m_items[m_count++] = v;
    }
    void reserve(size_t room) {
        if (room <= m_room) return;
        T* bigger = m_arena->template make<T>(room);
        if (m_count) std::memcpy(static_cast<void*>(bigger), m_items, m_count * sizeof(T));
        m_items = bigger;
        m_room = room;
    }
    void resize(size_t count) {            // (new ones are zeroed)
        reserve(count);
        if (count > m_count) std::memset(static_cast<void*>(m_items + m_count), 0, (count - m_count) * sizeof(T));
        m_count = count;
    }
    void clear() { m_count = 0; }

    size_t size() const  { return m_count; }
    bool   empty() const { return m_count == 0; }
    T*       data()       { return m_items; }
    const T* data() const { return m_items; }
    T&       operator[](size_t i)       { return m_items[i]; }
    const T& operator[](size_t i) const { return m_items[i]; }
    T*       begin()       { return m_items; }
    T*       end()         { return m_items + m_count; }
    const T* begin() const { return m_items; }
    const T* end() const   { return m_items + m_count; }

private:
    FrameArena* m_arena;
    T*          m_items = nullptr;
    size_t      m_count = 0, m_room = 0;
};
