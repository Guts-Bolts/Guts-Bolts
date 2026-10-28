// How to use the script functions: what the Script editor's autocomplete shows next to a
// name ("getmouse3d(distance?, ignore?) -> Vector3, BasePart?, Vector3" and one line on
// what it does), and what it shows above the cursor while you type the ( ) of a call.
#pragma once

#include <string>
#include <vector>

namespace ScriptHelp {

struct Entry {
    const char* name;    // as you'd type it: "getmouse3d", "task.wait", or a method / event like "FindFirstChild"
    const char* usage;   // "task.wait(seconds?) -> number"  (a ? means you can leave it out)
    const char* about;   // one short line
    bool global;         // true: a name you can type on its own (the tests check it really exists)
};

const std::vector<Entry>& all();

// The help for a name. Takes "task.wait", "workspace.Part:FindFirstChild" (uses the part after
// the last . or :) and ignores upper / lower case. nullptr if there's none.
const Entry* find(const std::string& name);

// A name written with the wrong upper / lower case ("getMouse3D", "Task.Wait"): the right
// spelling, or "" when the name is fine or unknown.
std::string rightCase(const std::string& name);

} // namespace ScriptHelp
