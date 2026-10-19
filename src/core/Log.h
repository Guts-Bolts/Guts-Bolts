#pragma once
#include <string>
#include <vector>

// Engine-wide message log, shown in the Output panel. Scripts write to it with
// print() / warn(), script errors land here in red, and the editor reports
// things like "Saved scene".
namespace Log {

enum class Level { Info, Warn, Error, System };

struct Entry {
    Level       level;
    std::string text;
    std::string time;   // "HH:MM:SS"
    int         run = 0;   // which play session (playtest) it came from: 1, 2, 3...; 0 = while editing
};

void info  (const std::string& text);
void warn  (const std::string& text);
void error (const std::string& text);
void system(const std::string& text);   // editor messages (dimmed)

const std::vector<Entry>& entries();
// How many entries were ever added (old ones get dropped, so this can be more than entries().size()).
unsigned long long count();
void clear();

// Play sessions, so old errors can be told apart from the current run: between
// beginRun() and endRun() every line is marked with that run's number.
int  beginRun();      // a playtest starts: returns its number (1, 2, 3...)
void endRun();        // it stopped: lines are "editing" (run 0) again
int  currentRun();    // the running session's number, 0 if none
int  lastRun();       // the newest session's number (running or finished), 0 if there was none

// While a capture is set (on this thread), lines go into it instead of the Output
// (Studio's AI tools read engine replies this way without filling the Output).
void setCapture(std::vector<Entry>* sink);

} // namespace Log
