#include <string>

#pragma once

// Updates. On launch, if a newer version is out and this copy can update itself
// (the source folder + install.py are next to it), the app is blocked by an
// "Updating..." screen that runs the updater and reopens - everyone stays on the
// newest version so games work the same for everybody. Later, or when the copy
// can't update itself, it's the little "Update available" card at the bottom-right.
// `appName` is which program to reopen after updating.
namespace UpdateToast {
// `canForce`: false while forcing would lose something (Studio with unsaved work, a
// game being played) - then it waits. Returns true if the app should close to update.
bool draw(const char* appName, bool canForce = true);
// Open a URL using the current platform's normal browser handler.
void openUrl(const std::string& url);
}
