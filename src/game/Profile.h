#pragma once
#include <map>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>
#include "../scene/Player.h"

// Your account on this computer: display name and avatar. The Player app
// dresses your character with it in every game you join.
struct Profile {
    std::string name = "Player";
    BodyColors  colors = Player::colorPresets()[0].second;
    HatStyle    hat    = HatStyle::None;
    glm::vec3   hatColor = glm::vec3(-1.0f);   // negative = the hat's normal colours
    std::vector<std::string> recent;   // file names of recently played games, newest first
    std::vector<std::pair<std::string, std::string>> grants;   // official badges: {badge, signature}
    std::vector<std::string> inventory;   // catalog item ids you own
    std::vector<std::string> wearing;     // catalog item ids you have on
    std::string shirtImage, pantsImage;   // clothing pictures being worn ("gb:<id>"), "" = none
    std::string tshirtImage;              // a T-shirt: a picture on the front of the torso
    std::string faceImage;                // a face from the catalog ("gb:<id>"), "" = the smiley
    bool        headless = false;         // wearing a Headless Head bundle: no head (or face) shows
    std::string body;                     // wearing a bundle with a new body: "gb:<id>" of it, "" = the normal body
    std::map<std::string, std::string> bodyParts; // independently equipped body-part slot -> "gb:<id>"
    std::map<std::string, std::string> accessories;   // kind -> "gb:<id>" (made in Studio)
    long long   avatarUpdated = 0;        // when the avatar was last saved on the server (to keep the website in step)
    std::string server;                   // Guts&Bolts server to use ("" = offline); new players get the official one

    // Change your display name. "Guts" is kept for the staff account.
    bool rename(const std::string& newName, std::string& error);

    void played(const std::string& game) {
        recent.erase(std::remove(recent.begin(), recent.end(), game), recent.end());
        recent.insert(recent.begin(), game);
        if (recent.size() > 12) recent.resize(12);
        save();
    }

    void applyTo(Player& player) const {
        player.setBodyColors(colors);
        player.setHat(hat, hatColor);
        player.setClothing(shirtImage, pantsImage, tshirtImage);
        player.setAccessories(accessories);
        player.setFace(faceImage);
        player.setBody(body);
        player.setBodyParts(bodyParts);
        player.setHeadless(headless);
    }

    static Profile& get();
    void load();
    void save() const;
};
