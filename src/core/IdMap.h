#pragma once
// IdMap: a part's id (or any 64-bit number) -> a value, kept in one flat list.
//
// It does the same job as a std::unordered_map<uint64_t, V>, but a std::unordered_map
// asks for memory for every single thing put in it, so filling one again every frame
// (every part, every step) asks for memory thousands of times a frame. This one keeps
// its list when it's emptied, so once it has grown big enough it never asks again.
//
//   IdMap<SceneNode*> byId;
//   byId.set(part->id, part);
//   if (SceneNode* const* p = byId.find(id)) use(*p);
//   byId.clear();   // (keeps its room for next time)
//
// How: "open addressing". Each id is turned into a spot in the list; if that spot is
// taken, it tries the next one along. The list is always at least twice as big as
// what's in it, so the search is short.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

template <typename V> class IdMap {
public:
    void clear() {
        if (m_count == 0) return;
        for (Slot& s : m_slots) s.used = false;
        m_count = 0;
    }
    size_t size() const { return m_count; }
    // Make room for `n` things at once (so filling it doesn't grow it bit by bit).
    void reserve(size_t n) { while (m_slots.size() < n * 2) grow(); }

    void set(uint64_t key, const V& value) {
        if ((m_count + 1) * 2 > m_slots.size()) grow();
        Slot& s = slotFor(key);
        if (!s.used) { s.used = true; s.key = key; ++m_count; }
        s.value = value;
    }
    const V* find(uint64_t key) const {
        if (m_slots.empty()) return nullptr;
        const size_t mask = m_slots.size() - 1;
        for (size_t i = hash(key) & mask;; i = (i + 1) & mask) {
            const Slot& s = m_slots[i];
            if (!s.used) return nullptr;
            if (s.key == key) return &s.value;
        }
    }
    bool contains(uint64_t key) const { return find(key) != nullptr; }
    void swap(IdMap& other) { m_slots.swap(other.m_slots); std::swap(m_count, other.m_count); }

private:
    struct Slot { uint64_t key = 0; V value{}; bool used = false; };
    static size_t hash(uint64_t k) {
        k *= 0x9E3779B97F4A7C15ull;   // (spreads ids that are next to each other far apart)
        return (size_t)(k ^ (k >> 29));
    }
    Slot& slotFor(uint64_t key) {
        const size_t mask = m_slots.size() - 1;
        for (size_t i = hash(key) & mask;; i = (i + 1) & mask)
            if (!m_slots[i].used || m_slots[i].key == key) return m_slots[i];
    }
    void grow() {
        std::vector<Slot> old;
        old.swap(m_slots);
        m_slots.assign(std::max<size_t>(64, old.size() * 2), Slot{});   // (always a power of two)
        m_count = 0;
        for (const Slot& s : old)
            if (s.used) set(s.key, s.value);
    }
    std::vector<Slot> m_slots;
    size_t m_count = 0;
};
