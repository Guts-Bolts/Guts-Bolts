#pragma once
#include "SceneNode.h"

// Game UI helpers shared by scripts (Instance.new), Studio's Insert menu and
// Roblox file import.
namespace Guis {

// What a freshly made UI object looks like (Roblox's defaults). `n.gui.type` must be set.
inline void setDefaults(SceneNode& n) {
    GuiProps& g = n.gui;
    const GuiType t = g.type;
    g = GuiProps{};
    g.type = t;
    n.enabled = true;
    n.visible = true;
    switch (t) {
        case GuiType::TextLabel:  g.size = {0, 200, 0, 50}; g.text = "Label"; break;
        case GuiType::TextButton: g.size = {0, 200, 0, 50}; g.text = "Button"; break;
        case GuiType::UICorner:   g.corner = {0, 8, 0, 0}; break;
        case GuiType::UIStroke:   g.borderColor = {0, 0, 0}; g.bgTransparency = 0; g.thickness = 1; break;
        case GuiType::UIShadow:   g.bg = {0, 0, 0}; g.bgTransparency = 0.5f; break;
        case GuiType::TextBox:    g.size = {0, 200, 0, 50}; g.placeholder = "Type here"; break;
        case GuiType::UIGridLayout: g.fill = 1; g.padding = {0, 5, 0, 5}; break;
        case GuiType::BillboardGui: g.size = {0, 200, 0, 50}; g.studsOffset = {0, 2, 0}; break;
        case GuiType::VideoFrame: g.size = {0, 320, 0, 180}; g.bg = {0, 0, 0}; g.border = 0; g.volume = 0.5f; break;
        default: break;   // Frame / ImageLabel / ImageButton / ScrollingFrame: 100 x 100
    }
}

// The class of a UI object from its Roblox name ("TextButton"...); false if it isn't one.
inline bool typeFromName(const std::string& cls, GuiType& out) {
    for (int i = 0; i < kGuiTypeCount; ++i)
        if (cls == kGuiClassNames[i]) { out = (GuiType)i; return true; }
    return false;
}

} // namespace Guis
