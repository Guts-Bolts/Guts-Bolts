#pragma once
#include <functional>
#include <string>
#include <vector>
#include "../../scene/SceneNode.h"   // PrimitiveType
#include "../Icons.h"
#include "../Premades.h"

class Scene;

// One square in the Toolbox: a picture (or an icon), the name under it, and
// what happens when it's clicked.
struct ToolboxTile {
    std::string key;                    // unique (for its picture and Recent)
    std::string name;
    std::string tip;                    // shown on hover
    std::string creator;                // "" for things built into Studio
    std::function<unsigned()> picture;  // a texture, or 0 (then the icon is drawn)
    Icons::Id icon = Icons::Id::Model;
    bool official = false;              // made by Guts&Bolts staff: safe to use
    // Library models: how much people like it, like "92%" ("" = no votes yet), and the
    // longer line for the tooltip ("92% liked (25 votes)").
    std::string rating, ratingTip;
    std::function<void()> use;
};

// A palette of insertable things, laid out like Roblox's Toolbox: Library /
// Inventory / Recent tabs, a category menu with a search box, and a grid of
// pictures with blue names. Official things carry a gold badge.
class ToolboxPanel {
public:
    struct Actions {
        std::function<void(PrimitiveType)> spawnPart;
        std::function<void()>              addScript;
        std::function<void()>              addModel;
        std::function<void(LightType)>     addLight;
        std::function<void()>              addSound;
        std::function<void(int)>           startConnect;   // ConstraintType (5 = motor, 6.. = ConstraintType 5..)
        std::function<void(Premade)>       spawnPremade;
        // A picture of what `build` makes (made once per key; 0 while it's being made).
        std::function<unsigned(const std::string& key, const std::function<void(Scene&)>& build)> thumbnail;
        // The online Library: kind 0 models, 1 decals, 2 audio. `mine` = only your own
        // (private ones too). `bestRated` = sort by thumbs up instead of most used.
        // `reload` = the search changed. `status` gets any message.
        std::function<std::vector<ToolboxTile>(bool mine, int kind, const std::string& query, bool bestRated,
                                               bool reload, std::string& status)> library;
    };

    explicit ToolboxPanel(Actions actions);
    void render();

private:
    std::vector<ToolboxTile> builtIn(int category);
    void drawTabs();
    void drawSearchRow(const char* const* names, int count);
    void drawGrid(std::vector<ToolboxTile>& tiles);
    void remember(const ToolboxTile& t);

    Actions m_do;
    int m_tab = 0;             // 0 Library, 1 Inventory, 2 Recent
    int m_category = 0;        // Library: see kCategories; Inventory: 0..2
    int m_invCategory = 0;
    std::string m_query, m_status;
    bool m_reload = true;
    bool m_bestRated = false;  // Library > Models: "Best rated" instead of "Most popular"
    std::vector<ToolboxTile> m_recent;
};
