#pragma once
// Script History: while it's recording, a playtest writes down what its scripts did -
// every property a script set (with the value before and after) and every event that ran
// a script's function. Studio's Script History window shows it, so you can see what
// changed a part and why. Off until you press Record; it keeps the newest kMax entries.
#include <cstdint>
#include <deque>
#include <string>

enum class SignalKind : int;

namespace ScriptHistory {

constexpr size_t kMax = 5000;

struct Entry {
    enum Kind { Set, Event } kind = Set;
    double      time = 0.0;          // seconds into the playtest
    uint64_t    object = 0;          // what was changed / what the event was on (0 = a service)
    std::string objectName;          // its full name back then (it may be gone later)
    uint64_t    script = 0;          // the Script that did it / that ran
    std::string scriptName;
    int         line = 0;            // the line in that script (Set only)
    std::string what;                // the property, or the event's name
    std::string before, after;       // Set: the value before and after
};

bool recording();
void setRecording(bool on);
void add(Entry e);                   // (only call it while recording)
void clear();
const std::deque<Entry>& entries();
uint64_t added();                    // how many were ever added (to see when it changes)
const char* signalName(SignalKind kind);

} // namespace ScriptHistory
