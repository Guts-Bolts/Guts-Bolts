#pragma once
#include <glm/glm.hpp>
#include <string>

// How a game's in-game player list (the leaderboard, top right) looks, and whether it
// shows at all. Set in Studio (Game Settings > Player list) or by a LocalScript:
//   StarterGui:SetCore("PlayerListStyle", { Title = "Cops & Robbers", BackgroundColor = Color3.new(0, 0, 0.2) })
//   StarterGui:SetCoreGuiEnabled(Enum.CoreGuiType.PlayerList, false)   -- hide it
struct PlayerListStyle {
    bool        show = true;                          // false = this game has no player list
    std::string title = "Players";                    // the words on its title bar
    glm::vec3   background{0.0f, 0.0f, 0.0f};
    float       transparency = 0.5f;                  // of the background (0 = solid, 1 = invisible)
    glm::vec3   titleColor{1.0f, 0.784f, 0.47f};      // the title and the stat names
    glm::vec3   textColor{1.0f, 1.0f, 1.0f};          // names and numbers
    bool        teams = true;                         // put players under their team's name, in its colour

    bool operator==(const PlayerListStyle&) const = default;
};
