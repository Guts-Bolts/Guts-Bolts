#pragma once
// "What's in this space?" for scripts: workspace:GetPartsInPart, :GetPartBoundsInBox,
// :GetPartBoundsInRadius and OverlapParams (SpatialQueries.cpp).
struct lua_State;

namespace SpatialQueries {
// Adds the methods and OverlapParams. Runs after the prelude (it needs
// GB.InstanceMethods and Enum).
void install(lua_State* L);
}
