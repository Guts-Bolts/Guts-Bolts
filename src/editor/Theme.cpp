#include "Theme.h"

#include <imgui.h>
#include <cstdio>
#include <iterator>
#include <vector>

namespace EditorTheme {

namespace {
// Palette ---------------------------------------------------------------------
constexpr ImVec4 kAccent      = {0.26f, 0.55f, 0.96f, 1.00f};
constexpr ImVec4 kAccentHover = {0.33f, 0.62f, 1.00f, 1.00f};

constexpr ImVec4 kText        = {0.90f, 0.91f, 0.93f, 1.00f};
constexpr ImVec4 kTextDim     = {0.46f, 0.49f, 0.55f, 1.00f};

constexpr ImVec4 kBgDarkest   = {0.094f, 0.102f, 0.122f, 1.00f}; // menu/title bars
constexpr ImVec4 kBgWindow    = {0.122f, 0.133f, 0.157f, 1.00f};
constexpr ImVec4 kBgPopup     = {0.137f, 0.149f, 0.176f, 1.00f};
constexpr ImVec4 kFrame       = {0.169f, 0.184f, 0.216f, 1.00f};
constexpr ImVec4 kFrameHover  = {0.212f, 0.231f, 0.271f, 1.00f};
constexpr ImVec4 kFrameActive = {0.247f, 0.271f, 0.318f, 1.00f};
constexpr ImVec4 kBorder      = {1.00f, 1.00f, 1.00f, 0.055f};

ImVec4 withAlpha(ImVec4 c, float a) { c.w = a; return c; }

ImFont* g_codeFont = nullptr;
} // namespace

void loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    // Crisp settings for a UI font: no oversampling + horizontal pixel snap.
    // (Heavy oversampling enlarges the atlas and proved unstable on some
    // drivers, so keep the atlas modest.)
    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    // Prefer a clean system UI font; fall back to ImGui's built-in bitmap font.
    const char* candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/SegoeUI.ttf",
#ifdef GB_MOBILE
        // Phones: Android's own font, or a common Linux one for the desktop test build.
        "/system/fonts/Roboto-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    };
    ImFont* loaded = nullptr;
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            loaded = io.Fonts->AddFontFromFileTTF(path, 17.0f * scale, &cfg);
            if (loaded) break;
        }
    }
    if (!loaded) {
        ImFontConfig def;
        def.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&def);
    }

    // Letters the font above doesn't have (Cyrillic, Polish, accents in the
    // built-in font) come from a bigger system font, so every language shows.
    const char* moreLetters[] = {
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/system/fonts/Roboto-Regular.ttf",
    };
    for (const char* path : moreLetters) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            ImFontConfig merge = cfg;
            merge.MergeMode = true;
            if (io.Fonts->AddFontFromFileTTF(path, (loaded ? 17.0f : 13.0f) * scale, &merge)) break;
        }
    }
    // Chinese, Japanese and Korean (chat in those, and translated text): the
    // first font found of each list. (Fonts only load the letters that show up.)
    const char* cjk[] = {
        "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc",
        "/System/Library/Fonts/PingFang.ttc", "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
    };
    const char* korean[] = {
        "C:/Windows/Fonts/malgun.ttf", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
    };
    for (const auto& list : {std::vector<const char*>(std::begin(cjk), std::end(cjk)),
                             std::vector<const char*>(std::begin(korean), std::end(korean))}) {
        for (const char* path : list) {
            if (FILE* f = std::fopen(path, "rb")) {
                std::fclose(f);
                ImFontConfig merge = cfg;
                merge.MergeMode = true;
                if (io.Fonts->AddFontFromFileTTF(path, (loaded ? 17.0f : 13.0f) * scale, &merge)) break;
            }
        }
    }

    // Monospace font for code. ImGui's built-in font is monospace too, so it
    // makes a fine fallback.
    const char* monoCandidates[] = {
        "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/cour.ttf",
        "/System/Library/Fonts/Menlo.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    };
    g_codeFont = nullptr;
    for (const char* path : monoCandidates) {
        if (FILE* f = std::fopen(path, "rb")) {
            std::fclose(f);
            g_codeFont = io.Fonts->AddFontFromFileTTF(path, 16.0f, &cfg);
            if (g_codeFont) break;
        }
    }
    if (!g_codeFont) g_codeFont = io.Fonts->AddFontDefault();
}

ImFont* codeFont() { return g_codeFont; }

void apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // Metrics -----------------------------------------------------------------
    s.WindowPadding     = {10, 10};
    s.FramePadding      = {8, 5};
    s.ItemSpacing       = {8, 7};
    s.ItemInnerSpacing  = {6, 5};
    s.CellPadding       = {6, 4};
    s.IndentSpacing     = 20;
    s.ScrollbarSize     = 12;
    s.GrabMinSize       = 10;

    s.WindowBorderSize  = 1;
    s.ChildBorderSize   = 1;
    s.PopupBorderSize   = 1;
    s.FrameBorderSize   = 0;

    s.WindowRounding    = 6;
    s.ChildRounding     = 6;
    s.FrameRounding     = 5;
    s.PopupRounding     = 5;
    s.ScrollbarRounding = 9;
    s.GrabRounding      = 5;
    s.TabRounding       = 6;

    s.WindowTitleAlign  = {0.0f, 0.5f};
    s.WindowMenuButtonPosition = ImGuiDir_None;

    // Colours -----------------------------------------------------------------
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = kText;
    c[ImGuiCol_TextDisabled]          = kTextDim;
    c[ImGuiCol_WindowBg]              = kBgWindow;
    c[ImGuiCol_ChildBg]               = {0, 0, 0, 0};
    c[ImGuiCol_PopupBg]               = kBgPopup;
    c[ImGuiCol_Border]                = kBorder;
    c[ImGuiCol_BorderShadow]          = {0, 0, 0, 0};

    c[ImGuiCol_FrameBg]               = kFrame;
    c[ImGuiCol_FrameBgHovered]        = kFrameHover;
    c[ImGuiCol_FrameBgActive]         = kFrameActive;

    c[ImGuiCol_TitleBg]               = kBgDarkest;
    c[ImGuiCol_TitleBgActive]         = kBgDarkest;
    c[ImGuiCol_TitleBgCollapsed]      = kBgDarkest;
    c[ImGuiCol_MenuBarBg]             = kBgDarkest;

    c[ImGuiCol_ScrollbarBg]           = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab]         = kFrameActive;
    c[ImGuiCol_ScrollbarGrabHovered]  = {0.30f, 0.33f, 0.39f, 1.0f};
    c[ImGuiCol_ScrollbarGrabActive]   = kAccent;

    c[ImGuiCol_CheckMark]             = kAccent;
    c[ImGuiCol_SliderGrab]            = kAccent;
    c[ImGuiCol_SliderGrabActive]      = kAccentHover;

    c[ImGuiCol_Button]                = kFrame;
    c[ImGuiCol_ButtonHovered]         = kFrameHover;
    c[ImGuiCol_ButtonActive]          = kFrameActive;

    c[ImGuiCol_Header]                = withAlpha(kAccent, 0.28f);
    c[ImGuiCol_HeaderHovered]         = withAlpha(kAccent, 0.45f);
    c[ImGuiCol_HeaderActive]          = withAlpha(kAccent, 0.65f);

    c[ImGuiCol_Separator]             = kBorder;
    c[ImGuiCol_SeparatorHovered]      = withAlpha(kAccent, 0.6f);
    c[ImGuiCol_SeparatorActive]       = kAccent;

    c[ImGuiCol_ResizeGrip]            = {1, 1, 1, 0.03f};
    c[ImGuiCol_ResizeGripHovered]     = withAlpha(kAccent, 0.5f);
    c[ImGuiCol_ResizeGripActive]      = kAccent;

    c[ImGuiCol_Tab]                   = kBgDarkest;
    c[ImGuiCol_TabHovered]            = kFrameHover;
    c[ImGuiCol_TabSelected]           = kFrame;
    c[ImGuiCol_TabSelectedOverline]   = kAccent;
    c[ImGuiCol_TabDimmed]             = kBgDarkest;
    c[ImGuiCol_TabDimmedSelected]     = {0.165f, 0.180f, 0.212f, 1.0f};

    c[ImGuiCol_DockingPreview]        = withAlpha(kAccent, 0.40f);
    c[ImGuiCol_DockingEmptyBg]        = kBgDarkest;

    c[ImGuiCol_TextSelectedBg]        = withAlpha(kAccent, 0.35f);
    c[ImGuiCol_NavCursor]             = kAccent;
}

void applyStudio() {
    apply();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0; s.ChildRounding = 0; s.FrameRounding = 2; s.PopupRounding = 2;
    s.TabRounding = 0; s.GrabRounding = 2; s.ScrollbarRounding = 2;
    s.WindowPadding = {8, 8};
    s.FramePadding = {6, 4};
    s.ItemSpacing = {6, 5};
    s.IndentSpacing = 16;
    s.TabBarBorderSize = 1;

    auto c8 = [](int r, int g, int b, int a = 255) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f); };
    const ImVec4 blue = c8(0, 162, 255);
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = c8(221, 221, 221);
    c[ImGuiCol_TextDisabled]         = c8(133, 133, 133);
    c[ImGuiCol_WindowBg]             = c8(46, 46, 46);
    c[ImGuiCol_PopupBg]              = c8(40, 40, 40);
    c[ImGuiCol_Border]               = c8(26, 26, 26);
    c[ImGuiCol_FrameBg]              = c8(37, 37, 37);
    c[ImGuiCol_FrameBgHovered]       = c8(58, 58, 58);
    c[ImGuiCol_FrameBgActive]        = c8(66, 66, 66);
    c[ImGuiCol_TitleBg]              = c8(37, 37, 37);
    c[ImGuiCol_TitleBgActive]        = c8(37, 37, 37);
    c[ImGuiCol_TitleBgCollapsed]     = c8(37, 37, 37);
    c[ImGuiCol_MenuBarBg]            = c8(37, 37, 37);
    c[ImGuiCol_ScrollbarGrab]        = c8(80, 80, 80);
    c[ImGuiCol_ScrollbarGrabHovered] = c8(100, 100, 100);
    c[ImGuiCol_ScrollbarGrabActive]  = c8(120, 120, 120);
    c[ImGuiCol_CheckMark]            = blue;
    c[ImGuiCol_SliderGrab]           = blue;
    c[ImGuiCol_SliderGrabActive]     = c8(60, 185, 255);
    c[ImGuiCol_Button]               = c8(60, 60, 60);
    c[ImGuiCol_ButtonHovered]        = c8(75, 75, 75);
    c[ImGuiCol_ButtonActive]         = c8(0, 120, 215);
    c[ImGuiCol_Header]               = c8(11, 90, 175);    // selected rows (Explorer)
    c[ImGuiCol_HeaderHovered]        = c8(66, 66, 66);
    c[ImGuiCol_HeaderActive]         = c8(11, 90, 175);
    c[ImGuiCol_Separator]            = c8(26, 26, 26);
    c[ImGuiCol_Tab]                  = c8(37, 37, 37);
    c[ImGuiCol_TabHovered]           = c8(66, 66, 66);
    c[ImGuiCol_TabSelected]          = c8(46, 46, 46);
    c[ImGuiCol_TabSelectedOverline]  = blue;
    c[ImGuiCol_TabDimmed]            = c8(37, 37, 37);
    c[ImGuiCol_TabDimmedSelected]    = c8(46, 46, 46);
    c[ImGuiCol_DockingPreview]       = ImVec4(blue.x, blue.y, blue.z, 0.4f);
    c[ImGuiCol_DockingEmptyBg]       = c8(30, 30, 30);
    c[ImGuiCol_TextSelectedBg]       = ImVec4(blue.x, blue.y, blue.z, 0.35f);
    c[ImGuiCol_NavCursor]            = blue;
}

} // namespace EditorTheme
