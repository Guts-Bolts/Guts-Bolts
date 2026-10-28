#pragma once
// The web Player on phones (web/README.md): a browser only opens the phone's keyboard
// for a real text box on the page, so web/index.html keeps a hidden one. This tells
// the page when one of our text boxes wants typing, and the page sends the letters
// back. Does nothing outside a browser.
namespace WebKeyboard {
void update();   // once a frame
}
