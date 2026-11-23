#pragma once

// How full the battery is, for battery saver (Settings.h). Phones and tablets (SDL),
// Windows laptops and Linux laptops; everything else (desktops, Macs for now, the web
// Player) says "unknown", and battery saver's Auto then never turns on by itself.
namespace Battery {

// percent 0..100 and whether it's charging (or full on the charger). False when there's
// no battery or it can't tell.
bool read(int& percent, bool& charging);

} // namespace Battery
