#pragma once
// Scratch: borrow a list for a moment, and give it back (with its room) when done.
//
// Code that runs every frame often needs a short list for a moment (the parts still to
// look at, the corners of a shape...). Making a new std::vector each time asks for
// memory each time. A Scratch list is kept after it's given back, so the next time
// there's already room in it:
//
//   Scratch<SceneNode*> stack;            // an empty list, ready to use
//   stack->push_back(scene.root());
//   while (!stack->empty()) { ... }
//
// (FrameArena.h does a similar job for the renderer's lists of plain data that last one
// frame; a Scratch list can hold anything and lives across frames.)
//
// Each thread has its own lists, and borrowing one while another is still out (a
// function that calls itself, or calls another that uses one) just takes the next one.
#include <cstddef>
#include <deque>
#include <vector>

template <typename T> class Scratch {
public:
    Scratch() : m_list(take()) { m_list.clear(); }
    ~Scratch() { --used(); }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    std::vector<T>& operator*() { return m_list; }
    std::vector<T>* operator->() { return &m_list; }

private:
    static std::deque<std::vector<T>>& lists() { thread_local std::deque<std::vector<T>> l; return l; }   // (a deque: lists don't move when it grows)
    static size_t& used() { thread_local size_t n = 0; return n; }
    static std::vector<T>& take() {
        std::deque<std::vector<T>>& l = lists();
        if (used() == l.size()) l.emplace_back();
        return l[used()++];
    }
    std::vector<T>& m_list;
};
