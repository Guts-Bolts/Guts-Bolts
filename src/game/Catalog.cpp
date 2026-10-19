#include "Catalog.h"
#include "Profile.h"
#include "Bolts.h"
#include "../core/Account.h"
#include "../core/Paths.h"
#include "Version.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>

namespace Catalog {

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

int byte(float v) { return (int)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); }

// The exact text that gets signed (keys are sorted, colours are whole numbers,
// so it comes out the same on every computer).
json body(const Item& it) {
    json j = {{"id", it.id}, {"name", it.name}, {"description", it.description}, {"type", typeName(it.type)},
              {"hat", (int)it.hat}, {"color", {byte(it.color.r), byte(it.color.g), byte(it.color.b)}},
              {"created", it.created}};
    if (it.price > 0) j["price"] = it.price;   // signed too, so nobody can change the price (free items leave it out)
    return j;
}
std::string message(const Item& it) { return "gb-item:" + body(it).dump(); }

fs::path appFolder() { return Paths::appFolder() / "catalog"; }
fs::path repoFolder() { return fs::path(GB_SOURCE_DIR) / "catalog"; }

bool parse(const json& j, Item& it) {
    if (!j.is_object()) return false;
    it.id = j.value("id", std::string());
    it.name = j.value("name", std::string());
    it.description = j.value("description", std::string());
    std::string t = j.value("type", std::string());
    it.type = Type::Count;
    for (int i = 0; i < (int)Type::Count; ++i) if (t == typeName((Type)i)) it.type = (Type)i;
    it.hat = (HatStyle)std::clamp(j.value("hat", 0), 0, kHatStyleCount - 1);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 3)
        it.color = {j["color"][0].get<int>() / 255.0f, j["color"][1].get<int>() / 255.0f, j["color"][2].get<int>() / 255.0f};
    it.created = j.value("created", 0LL);
    it.price = std::max(0LL, j.value("price", 0LL));
    it.signature = j.value("sig", std::string());
    return !it.id.empty() && it.type != Type::Count;
}

std::string slug(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum((unsigned char)c)) out += (char)std::tolower((unsigned char)c);
        else if (!out.empty() && out.back() != '-') out += '-';
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    if (out.size() > 24) out.resize(24);
    return out.empty() ? "item" : out;
}
} // namespace

const char* typeName(Type t) {
    switch (t) {
        case Type::Hat:   return "Hat";
        case Type::Shirt: return "Shirt";
        case Type::Pants: return "Pants";
        case Type::Hair:     return "Hair";
        case Type::FaceAcc:  return "FaceAcc";
        case Type::Neck:     return "Neck";
        case Type::Shoulder: return "Shoulder";
        case Type::Waist:    return "Waist";
        case Type::Face:     return "Face";
        case Type::TShirt:   return "TShirt";
        case Type::Gear:     return "Gear";
        case Type::Emote:    return "Emote";
        case Type::AnimPack: return "AnimPack";
        default:          return "?";
    }
}

std::vector<Item> load() {
    std::vector<Item> out;
    std::string official = Account::officialId();
    std::error_code ec;
    if (official.empty() || !fs::is_directory(appFolder(), ec)) return out;
    for (const auto& e : fs::directory_iterator(appFolder(), ec)) {
        if (e.path().extension() != ".gbitem") continue;
        std::ifstream f(e.path());
        json j = json::parse(f, nullptr, false);
        Item it;
        if (!parse(j, it)) continue;
        if (!Account::verify(official, message(it), it.signature)) continue;   // not made by staff
        out.push_back(it);
    }
    std::sort(out.begin(), out.end(), [](const Item& a, const Item& b) { return a.created > b.created; });
    return out;
}

bool create(Item& it, std::string& msg) {
    if (!Account::iAmStaff()) { msg = "Only the staff account can make catalog items."; return false; }
    if (it.name.empty()) { msg = "Give the item a name."; return false; }
    it.created = (long long)std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();
    it.id = slug(it.name) + "-" + Account::randomHex(4);
    it.signature = Account::sign(message(it));
    json j = body(it);
    j["sig"] = it.signature;

    std::error_code ec;
    fs::create_directories(appFolder(), ec);
    std::ofstream f(appFolder() / (it.id + ".gbitem"));
    if (!f) { msg = "Couldn't save the item."; return false; }
    f << j.dump(2);
    f.close();
    msg = "Made \"" + it.name + "\"!";
    if (fs::is_directory(fs::path(GB_SOURCE_DIR), ec)) {
        fs::create_directories(repoFolder(), ec);
        std::ofstream r(repoFolder() / (it.id + ".gbitem"));
        if (r) {
            r << j.dump(2);
            msg += " It's also in " + repoFolder().string() + " - commit that file to give it to everyone.";
        }
    }
    return true;
}

bool remove(const Item& it, std::string& msg) {
    if (!Account::iAmStaff()) { msg = "Only the staff account can remove catalog items."; return false; }
    std::error_code ec;
    fs::remove(appFolder() / (it.id + ".gbitem"), ec);
    fs::remove(repoFolder() / (it.id + ".gbitem"), ec);
    msg = "Removed \"" + it.name + "\".";
    return true;
}

Item fromServer(const json& a) {
    Item it;
    it.id = a.value("id", std::string());
    it.name = a.value("name", std::string());
    it.description = a.value("description", std::string());
    std::string kind = a.value("kind", std::string());
    it.kind = kind;
    it.type = kind == "hat" ? Type::Hat : kind == "shirt" ? Type::Shirt : kind == "pants" ? Type::Pants
            : kind == "hair" ? Type::Hair : kind == "faceacc" ? Type::FaceAcc : kind == "neck" ? Type::Neck
            : kind == "shoulder" ? Type::Shoulder : kind == "waist" ? Type::Waist : kind == "tshirt" ? Type::TShirt : kind == "gear" ? Type::Gear
            : kind == "emote" ? Type::Emote : kind == "animpack" ? Type::AnimPack : Type::Face;
    it.price = a.value("price", 0LL);
    it.created = a.value("created", 0LL);
    if (a.contains("meta") && a["meta"].is_object()) {
        const json& m = a["meta"];
        it.hat = (HatStyle)std::clamp(m.value("style", 2), 1, 3);
        if (m.value("image", false) && it.type != Type::Hat) it.image = "gb:" + it.id;
        if (m.value("model", false)) it.model = "gb:" + it.id;
        if (m.contains("color") && m["color"].is_array() && m["color"].size() == 3)
            it.color = {m["color"][0].get<int>() / 255.0f, m["color"][1].get<int>() / 255.0f, m["color"][2].get<int>() / 255.0f};
    }
    return it;
}

bool owns(const Item& it) {
    if (it.price > 0) return Bolts::has("item:" + it.id);
    const auto& inv = Profile::get().inventory;
    return std::find(inv.begin(), inv.end(), it.id) != inv.end();
}

bool buy(const Item& it, std::string& msg) {
    if (owns(it)) { msg = "You already own that."; return true; }
    if (it.price > 0 && !Bolts::spend(it.price, "Bought " + it.name, "item:" + it.id, msg)) return false;
    Profile& me = Profile::get();
    if (std::find(me.inventory.begin(), me.inventory.end(), it.id) == me.inventory.end()) me.inventory.push_back(it.id);
    me.save();
    if (it.price == 0) msg = "Got " + it.name + "!";
    return true;
}

void wear(const Item& it) {
    if (it.price > 0 && !owns(it)) return;   // buy it first
    Profile& me = Profile::get();
    if (std::find(me.inventory.begin(), me.inventory.end(), it.id) == me.inventory.end()) me.inventory.push_back(it.id);
    applyLook(it);
}

void applyLook(const Item& it) {
    if (isEquipped(it.type)) return;   // gear, emotes and packs aren't worn (they're equipped on the server)
    Profile& me = Profile::get();
    switch (it.type) {
        case Type::Hat:
            if (!it.model.empty()) { me.hat = HatStyle::None; me.accessories["hat"] = it.model; }
            else { me.hat = it.hat; me.hatColor = it.color; me.accessories.erase("hat"); }
            break;
        case Type::Hair: case Type::FaceAcc: case Type::Neck: case Type::Shoulder: case Type::Waist:
            me.accessories[it.kind] = it.model;
            break;
        case Type::Face:
            me.faceImage = it.image;
            break;
        case Type::TShirt:
            me.tshirtImage = it.image;
            break;
        case Type::Shirt:
            if (it.image.empty()) me.colors.torso = me.colors.leftArm = me.colors.rightArm = it.color;
            me.shirtImage = it.image;
            break;
        case Type::Pants:
            if (it.image.empty()) me.colors.leftLeg = me.colors.rightLeg = it.color;
            me.pantsImage = it.image;
            break;
        default: break;
    }
    // Only one item of each type at a time.
    std::vector<std::string> keep;
    const std::string prefix = std::string(typeName(it.type)) + "-";   // server items: "hat-1a2b3c"
    auto lowerPrefix = [&](const std::string& w) {
        std::string p;
        for (char c : prefix) p += (char)std::tolower((unsigned char)c);
        return w.rfind(p, 0) == 0;
    };
    for (const std::string& w : me.wearing) {
        bool sameType = lowerPrefix(w);
        for (const Item& other : load()) if (other.id == w && other.type == it.type) sameType = true;
        if (!sameType && w != it.id) keep.push_back(w);
    }
    keep.push_back(it.id);
    me.wearing = keep;
    me.save();
}

void takeOff(const Item& it) {
    Profile& me = Profile::get();
    switch (it.type) {
        case Type::Hat:
            if (!it.model.empty()) me.accessories.erase("hat");
            else { me.hat = HatStyle::None; me.hatColor = glm::vec3(-1.0f); }
            break;
        case Type::Hair: case Type::FaceAcc: case Type::Neck: case Type::Shoulder: case Type::Waist:
            if (auto a = me.accessories.find(it.kind); a != me.accessories.end() && a->second == it.model) me.accessories.erase(a);
            break;
        case Type::Face:   if (me.faceImage == it.image) me.faceImage.clear(); break;
        case Type::TShirt: if (me.tshirtImage == it.image) me.tshirtImage.clear(); break;
        case Type::Shirt:  if (me.shirtImage == it.image) me.shirtImage.clear(); break;
        case Type::Pants:  if (me.pantsImage == it.image) me.pantsImage.clear(); break;
        default: break;
    }
    me.wearing.erase(std::remove(me.wearing.begin(), me.wearing.end(), it.id), me.wearing.end());
    me.save();
}

bool isWearing(const Item& it) {
    if (isEquipped(it.type)) return false;
    const auto& w = Profile::get().wearing;
    return std::find(w.begin(), w.end(), it.id) != w.end();
}

} // namespace Catalog
