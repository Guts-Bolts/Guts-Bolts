#include "AssetCache.h"
#include "OnlineClient.h"
#include "Protocol.h"
#include "../core/Paths.h"
#include "../scene/Scene.h"
#include "../scene/SceneNode.h"

#include <fstream>
#include <algorithm>
#include <set>
#include <cctype>
#include <cstdio>

namespace Online {

namespace {
std::set<std::string>& inFlight() { static std::set<std::string> s; return s; }

// Asset IDs become local filenames on every platform, including iOS. Never let
// a malformed server/user ID escape the downloads directory.
bool safeAssetId(const std::string& id) {
    if (id.empty() || id == "." || id == "..") return false;
    for (unsigned char c : id)
        if (!std::isalnum(c) && c != '-' && c != '_') return false;
    return true;
}

std::string extensionFor(const nlohmann::json& asset) {
    std::string kind = asset.value("kind", std::string());
    if (kind == "game") return ".gbscene";
    if (kind == "plugin") return ".lua";
    if (kind == "shirt" || kind == "pants" || kind == "face" || kind == "tshirt") return ".png";
    if (Online::isAccessory(kind)) return ".json";
    if (kind == "audio" || kind == "decal") {
        std::string ext = asset.contains("meta") && asset["meta"].is_object()
            ? asset["meta"].value("ext", std::string(kind == "decal" ? "png" : "mp3"))
            : std::string(kind == "decal" ? "png" : "mp3");
        // Metadata is untrusted. Only accept a short extension, not a path.
        if (ext.empty() || ext.size() > 8 ||
            !std::all_of(ext.begin(), ext.end(), [](unsigned char c) { return std::isalnum(c); }))
            ext = kind == "decal" ? "png" : "mp3";
        return "." + ext;
    }
    return ".json";
}
} // namespace

void download(const std::string& id, Downloaded done, bool redownload) {
    if (!safeAssetId(id)) {
        if (done) done(false, {}, nlohmann::json{{"error", "Invalid asset ID."}});
        return;
    }
    if (!redownload) {
        std::filesystem::path have = Paths::downloaded(id);
        if (!have.empty()) { if (done) done(true, have, nlohmann::json::object()); return; }
    }
    // Background prefetches are deliberately deduplicated. A callback-bearing
    // request may still be made separately so its caller gets a result.
    if (inFlight().count(id) && !done) return;
    inFlight().insert(id);
    request("get", {{"id", id}}, [id, done](const nlohmann::json& r) {
        inFlight().erase(id);
        std::string bytes;
        if (!r.value("ok", false) || !r.contains("asset") || !r["asset"].is_object() ||
            !r.contains("data") || !r["data"].is_string() ||
            !base64Decode(r.value("data", std::string()), bytes)) {
            if (done) done(false, {}, nlohmann::json{{"error", r.value("error", std::string("Download failed."))}});
            return;
        }

        const std::filesystem::path folder = Paths::downloadsFolder();
        const std::filesystem::path out = folder / (id + extensionFor(r["asset"]));
        // Write to a temporary file first: interrupted downloads must never be
        // mistaken for a complete cached asset on the next launch.
        const std::filesystem::path temp = out.string() + ".part";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        bool ok = !ec;
        if (ok) {
            std::ofstream f(temp, std::ios::binary | std::ios::trunc);
            ok = bool(f);
            if (ok && !bytes.empty()) {
                f.write(bytes.data(), (std::streamsize)bytes.size());
                ok = bool(f);
            }
            f.close();
            ok = ok && !f.fail();
        }
        if (ok) {
            std::filesystem::rename(temp, out, ec);
            if (ec) {
                // Windows won't replace an existing destination atomically.
                ec.clear();
                std::filesystem::remove(out, ec);
                ec.clear();
                std::filesystem::rename(temp, out, ec);
            }
            ok = !ec;
        }
        if (!ok) {
            std::filesystem::remove(temp, ec);
            if (done) done(false, {}, nlohmann::json{{"error", "Could not save downloaded asset to app storage."}});
            return;
        }
        if (done) done(true, out, r["asset"]);
    }, 60);
}

void fetchSounds(Scene& scene) {
    if (!online()) return;
    scene.forEach([](SceneNode* n) {
        const std::string* id = n->isSound() ? &n->soundId : (n->isDecal() || n->isPart()) && !n->texture.empty() ? &n->texture : nullptr;
        if (id && id->rfind("gb:", 0) == 0 && Paths::downloaded(id->substr(3)).empty())
            download(id->substr(3));
    });
}

} // namespace Online
