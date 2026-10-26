#pragma once
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "EditMesh.h"

// Roblox's own 3D model files (the ".mesh" behind every hat, gear and MeshPart),
// and fetching them (and their pictures) from Roblox's public asset server.
namespace RobloxMesh {

struct Shape {
    std::shared_ptr<EditMesh> mesh;   // points squeezed into the -0.5..0.5 box, like every part
    glm::vec3 center{0.0f};           // where the shape sat in the file (Roblox studs)...
    glm::vec3 size{1.0f};             // ...and how big it was
};

// Read a mesh file: every version, from the old text "version 1.00" ones to the
// newer binary 2.00 - 7.00 ones. Uses the most detailed level of detail. Keeps the
// texture coordinates, so the item's picture wraps the shape the way it does on Roblox.
// Empty mesh (and `error` filled in) if the file can't be read.
Shape parse(const std::string& bytes, std::string* error = nullptr);

// Resolve a Roblox mesh asset ID, use the on-disk cache or fetch it, then decode it.
// This is the common entry point for importers; it never treats a failed download as a valid mesh.
Shape loadAsset(const std::string& content, std::string* error = nullptr);

// The number in "rbxassetid://123", "http://www.roblox.com/asset/?id=123" and
// friends; "" for anything else (like built-in "rbxasset://" files).
std::string assetId(const std::string& content);

// Download asset `id` into the games folder ("roblox/<id><ext>"), unless we already
// have it. Returns the short path (relative to the games folder), or "" if it failed.
std::string fetch(const std::string& id, const char* ext);

} // namespace RobloxMesh
