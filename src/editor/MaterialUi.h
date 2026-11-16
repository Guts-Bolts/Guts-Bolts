#pragma once
// The settings of a MaterialVariant (your own material), as Properties and the Material
// Editor show them (EditorMaterials.cpp).
struct MaterialVariantProps;

namespace MaterialUi {

// Base material, the four pictures (with a Choose... button each), size and the sliders.
// True if anything changed.
// surface: a SurfaceAppearance's (no BaseMaterial or StudsPerTile: it follows the mesh's UVs).
bool edit(MaterialVariantProps& v, bool surface = false);

} // namespace MaterialUi
