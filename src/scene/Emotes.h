#pragma once
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

#include "Animation.h"

// Emotes and animation packs, as real keyframes (Animation.h clips) for the classic
// character. They come from assets/emotes/emotes.gbemotes (tools/make_emotes.py),
// baked into the program:
//   - the built-in emotes everyone has ("/e dance", "/e wave"...),
//   - the starter Emote and Animation pack items the servers sell in the catalog.
// Emotes people own from the catalog arrive as files (an emote is an animation file,
// {"format":"gbanim","clip":...}; a pack is {"format":"gbanimpack","clips":{"idle":...}}).
namespace Emotes {

inline constexpr int kSlots = 8;   // emotes you can equip at once (the emote menu's slots)

// The parts of a character's animations an animation pack replaces.
inline const char* const kPackStates[] = {"idle", "walk", "run", "jump", "fall"};

struct Emote {
    std::string name;      // what you type after "/e": "dance"
    std::string title;     // what the menu shows: "Dance"
    std::shared_ptr<const Anim::Clip> clip;
    float picture = 0.0f;  // the moment its picture shows
};
const std::vector<Emote>& builtIn();
const Emote* findBuiltIn(const std::string& name);   // "Dance", "dance1" and "dance" all work

// "Jumping Jacks" -> "jumpingjacks" (what "/e" and PlayEmote match on); "dance1" -> "dance".
std::string normalize(const std::string& name);

// The starter catalog items (the same ones the servers seed).
struct StarterItem {
    std::string id, kind, name, description;   // kind: "emote" or "animpack"
    long long price = 0;
    nlohmann::json picture;                    // emote: a time; pack: ["walk", time]
    nlohmann::json file;                       // what the server stores as the item's file
};
const std::vector<StarterItem>& starterItems();

// An item's file -> its keyframes. False if it isn't one.
bool parseEmoteFile(const std::string& text, Anim::Clip& out);
using Pack = std::map<std::string, std::shared_ptr<const Anim::Clip>>;   // state -> clip
bool parsePackFile(const std::string& text, Pack& out);

// Pictures of emote items: the moment to show, and the character posed at it.
// `when` is the item's picture setting (emote: a time; pack: ["walk", time]); null = a
// moment a third of the way in (a pack: its walk). False if the file isn't one.
bool pictureClip(const std::string& fileText, const nlohmann::json& when, Anim::Clip& clip, float& t);
// Pose a character's parts (fresh from Player::build) as `clip` has them at `t` seconds.
void poseRig(SceneNode* rig, const Anim::Clip& clip, float t);

// What this computer's player has equipped from their account: up to 8 emotes (in
// their menu slots) and one animation pack. The app fills it in before a game starts.
struct Slot {
    std::string id, name;                      // the item, and its name ("Robot")
    std::shared_ptr<const Anim::Clip> clip;    // null = an empty slot
};
struct Loadout {
    std::array<Slot, kSlots> slots;
    std::string packId;
    Pack pack;
    int version = 0;                           // goes up with every change
    bool anyEmote() const { for (const Slot& s : slots) if (s.clip) return true; return false; }
};
Loadout& mine();

// What the emote menu shows: your equipped emotes in their slots, or (when you haven't
// equipped any) the built-in ones. name = what to play ("" = an empty slot).
struct MenuEntry { std::string name, title; };
std::vector<MenuEntry> menu();

} // namespace Emotes
