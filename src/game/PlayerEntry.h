#pragma once
#include <string>
#include <utility>
#include <vector>

// One row of the in-game player list.
struct PlayerEntry {
    std::string name;
    bool        admin = false;   // the official staff account (checked with its signature)
    bool        verified = false;   // has the (signed) Verified badge
    std::vector<std::pair<std::string, std::string>> stats;   // leaderstats: ("Coins", "12")...
    bool        creator = false;   // made this game (a little hammer shows next to their name)
    int         voice = 0;         // voice chat: 0 quiet, 1 talking right now, 2 muted by you
    std::string team;              // their team's name ("" = no team / Neutral)
    float       teamColor[3] = {1.0f, 1.0f, 1.0f};
};
