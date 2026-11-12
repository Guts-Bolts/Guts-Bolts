#pragma once
#include "../core/DeviceFeatures.h"

// What the graphics chip we're drawing with can do (see DeviceFeatures.h), asked from
// the driver the first time and remembered. Needs an OpenGL context.
namespace GpuCapsQuery {
const GpuCaps& get();
void forget();   // the context went away (asked again next time)
// Shorthand: can this device run that feature?
inline bool can(DeviceFeatures::Feature f) { return DeviceFeatures::can(get(), f); }
}
