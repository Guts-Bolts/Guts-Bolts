#pragma once
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include "../scene/Player.h"

// The avatar shop. Items are small signed files in the "catalog" folder; only
// ones signed by the official staff account show up, so only staff can add them.
namespace Catalog {

// (Gear: a Tool for games, not worn. Emote / AnimPack: moves for your character, equipped not worn.)
enum class Type { Hat, Shirt, Pants, Hair, FaceAcc, Neck, Shoulder, Waist, Face, TShirt, Gear, Emote, AnimPack, Count };
inline bool isMove(Type t) { return t == Type::Emote || t == Type::AnimPack; }   // an emote or an animation pack
inline bool isEquipped(Type t) { return t == Type::Gear || isMove(t); }        // equipped (not worn)
const char* typeName(Type t);

struct Item {
    std::string id;            // e.g. "red-cap-1a2b3c4d"
    std::string name;
    std::string description;
    Type        type  = Type::Hat;
    HatStyle    hat   = HatStyle::Cap;     // hats only
    glm::vec3   color = glm::vec3(1.0f);
    std::string image;                     // shirts / pants / faces: "gb:<id>" when it has a picture
    std::string model;                     // accessories made in Studio: "gb:<id>" (the placed model)
    std::string kind;                      // the server's kind: "hat", "hair", "faceacc", "neck", ...
    long long   created = 0;               // unix time
    long long   price = 0;                 // in Bolts; 0 = free
    std::string signature;
};

// Every properly signed item, newest first.
std::vector<Item> load();

// Staff only: sign and save a new item (into the app's catalog folder and,
// when building from source, the repo's catalog folder so it can be shared).
bool create(Item& item, std::string& message);
// Staff only: remove an item file.
bool remove(const Item& item, std::string& message);

// Do you own it? Free items: once you've worn them. Paid items: only if your
// (signed) Bolts history shows you bought it, so editing files can't fake it.
bool owns(const Item& item);
// Pay for it (free items just get added). False + a message if you can't afford it.
bool buy(const Item& item, std::string& message);
// Put an item on the profile. Paid items must be bought first.
void wear(const Item& item);
// Put it on without checking who owns it (the server checked that already).
void applyLook(const Item& item);
// A catalog item from a server upload (hat / shirt / pants).
Item fromServer(const nlohmann::json& asset);
bool isWearing(const Item& item);
// Take it off (shirts and pants go back to the body colour, hats to none).
void takeOff(const Item& item);

} // namespace Catalog
