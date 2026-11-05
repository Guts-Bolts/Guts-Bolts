// The take-a-break reminder's choices (Settings > Break Reminder): Off by default, and
// each menu choice maps to its minutes and back.
//
//   BreakReminderTests
#include "core/Settings.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << (ok ? "ok    " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}
} // namespace

int main() {
    GraphicsSettings fresh;
    check(fresh.breakMinutes == 0, "off unless you turn it on");
    check(std::string(GraphicsSettings::breakChoiceNames()[0]) == "Off" && GraphicsSettings::breakChoiceMinutes(0) == 0, "the first choice is Off");
    for (int i = 0; i < GraphicsSettings::kBreakChoices; ++i)
        check(GraphicsSettings::breakChoiceOf(GraphicsSettings::breakChoiceMinutes(i)) == i,
              std::string("\"") + GraphicsSettings::breakChoiceNames()[i] + "\" maps back to itself");
    check(GraphicsSettings::breakChoiceMinutes(3) == 60, "\"After 1 hour\" is 60 minutes");
    check(GraphicsSettings::breakChoiceOf(-5) == 0, "a broken number in the file shows as Off");
    std::cout << (failures ? "break reminder tests FAILED\n" : "break reminder tests passed\n");
    return failures ? 1 : 0;
}
