#pragma once
// Checks a script for mistakes without running it, for the Script editor's red and orange
// wavy lines (and View > Script Analysis):
//
//   errors    Lua can't read it at all: a missing "end", "=" where "==" was meant, a string
//             with no closing quote. The line is underlined where the mistake starts.
//   warnings  it reads fine but would break once the game runs that line:
//             - a name that isn't defined anywhere ("pirnt"), with "did you mean print?"
//             - a part of a built-in library that doesn't exist (math.flor, task.wiat)
//             - game:GetService("Plyers"), Instance.new("Prat"), Enum.KeyCode.Spase
//             - part.Destroy() with a dot, where it needs a colon: part:Destroy()
//
// Simple on purpose: it would rather miss a mistake than underline good code.
#include <string>
#include <vector>

namespace ScriptCheck {

struct Mark {
    int line = 1;          // 1 = the first line
    int col = 0;           // where the underline starts on that line (bytes from the line's start)
    int len = 1;           // how long it is (bytes)
    bool error = false;    // true: red (Lua can't read it); false: orange (a likely mistake)
    std::string text;      // what's wrong, in plain words
    std::string fix;       // what to put in place of the underlined text, or "" if there's no quick fix
};

// Every mistake found, in the order they come in the script. A script Lua can't read
// gets just its one error (the names in it can't be made sense of).
std::vector<Mark> check(const std::string& source);

// The closest of `names` to `word` ("pirnt" -> "print"), or "" if none is close enough.
std::string closest(const std::string& word, const std::vector<std::string>& names);

} // namespace ScriptCheck
