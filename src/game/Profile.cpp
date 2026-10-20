#include "Profile.h"
#include "../core/Paths.h"
#include "../core/Account.h"
#include "../online/Protocol.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace {
nlohmann::json vec(const glm::vec3& v) { return {v.x, v.y, v.z}; }
glm::vec3 vec(const nlohmann::json& j, const char* k, glm::vec3 fb) {
    if (!j.contains(k) || !j[k].is_array() || j[k].size() != 3) return fb;
    return {j[k][0].get<float>(), j[k][1].get<float>(), j[k][2].get<float>()};
}
} // namespace

Profile& Profile::get() {
    static Profile p = [] { Profile q; q.load(); return q; }();
    return p;
}

void Profile::load() {
    std::ifstream f(Paths::file("profile.json"));
    if (!f) { server = Online::kOfficialServer; return; }   // a new player: straight onto the official server
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    if (j.contains("name") && j["name"].is_string()) name = j["name"].get<std::string>();
    if (j.contains("hat") && j["hat"].is_number_integer()) hat = (HatStyle)std::clamp(j["hat"].get<int>(), 0, kHatStyleCount - 1);
    if (j.contains("recent") && j["recent"].is_array())
        for (auto& r : j["recent"]) if (r.is_string()) recent.push_back(r.get<std::string>());
    if (j.contains("grants") && j["grants"].is_array())
        for (auto& g : j["grants"])
            if (g.is_array() && g.size() == 2 && g[0].is_string() && g[1].is_string())
                grants.push_back({g[0].get<std::string>(), g[1].get<std::string>()});
    auto strings = [&](const char* k, std::vector<std::string>& out) {
        if (j.contains(k) && j[k].is_array())
            for (auto& v : j[k]) if (v.is_string()) out.push_back(v.get<std::string>());
    };
    strings("inventory", inventory);
    if (j.contains("server") && j["server"].is_string()) server = j["server"].get<std::string>();
    // Before the official server existed, "no server" was everyone's starting point: move
    // those players onto it once (choosing "Go offline" afterwards sticks).
    if (!j.value("serverChecked", false) && server.empty()) server = Online::kOfficialServer;
    strings("wearing", wearing);
    avatarUpdated = j.value("avatarUpdated", 0LL);
    auto safeImage = [](std::string s) {   // only server clothing pictures
        return s.rfind("gb:", 0) == 0 && s.size() < 64 ? s : std::string();
    };
    shirtImage = safeImage(j.value("shirtImage", std::string()));
    pantsImage = safeImage(j.value("pantsImage", std::string()));
    tshirtImage = safeImage(j.value("tshirtImage", std::string()));
    faceImage = safeImage(j.value("faceImage", std::string()));
    headless = j.value("headless", false) == true;
    body = safeImage(j.value("body", std::string()));
    if (j.contains("accessories") && j["accessories"].is_object())
        for (auto& [k, v] : j["accessories"].items())
            if (v.is_string() && !safeImage(v.get<std::string>()).empty()) accessories[k] = v.get<std::string>();
    // Nobody else gets to be called Guts, even by editing profile.json.
    if (Account::nameIsReserved(name) && !Account::iAmStaff()) name = "Player";
    hatColor        = vec(j, "hatColor", hatColor);
    colors.head     = vec(j, "head", colors.head);
    colors.torso    = vec(j, "torso", colors.torso);
    colors.leftArm  = vec(j, "leftArm", colors.leftArm);
    colors.rightArm = vec(j, "rightArm", colors.rightArm);
    colors.leftLeg  = vec(j, "leftLeg", colors.leftLeg);
    colors.rightLeg = vec(j, "rightLeg", colors.rightLeg);
}

void Profile::save() const {
    nlohmann::json j = {
        {"name", name}, {"hat", (int)hat}, {"hatColor", vec(hatColor)},
        {"head", vec(colors.head)}, {"torso", vec(colors.torso)},
        {"leftArm", vec(colors.leftArm)}, {"rightArm", vec(colors.rightArm)},
        {"leftLeg", vec(colors.leftLeg)}, {"rightLeg", vec(colors.rightLeg)},
        {"recent", recent}, {"grants", grants}, {"inventory", inventory}, {"wearing", wearing}, {"server", server}, {"serverChecked", true},
        {"avatarUpdated", avatarUpdated}, {"shirtImage", shirtImage}, {"pantsImage", pantsImage}, {"tshirtImage", tshirtImage},
        {"faceImage", faceImage}, {"headless", headless}, {"body", body}, {"accessories", accessories},
    };
    std::ofstream f(Paths::file("profile.json"));
    if (f) f << j.dump(2);
}

bool Profile::rename(const std::string& newName, std::string& error) {
    std::string n;
    for (char c : newName) if (c >= 32 && c < 127) n += c;
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    if (n.empty()) { error = "Your name can't be empty."; return false; }
    if (n.size() > 20) { error = "Names can be up to 20 letters long."; return false; }
    if (Account::nameIsReserved(n) && !Account::iAmStaff()) {
        error = "That name belongs to the Guts&Bolts staff account.";
        return false;
    }
    name = n;
    save();
    return true;
}
