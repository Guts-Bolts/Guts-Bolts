# gb_patch_imgui(<out dir>): copies imgui.cpp and imgui_draw.cpp into <out dir>
# with a few one-line hooks added, so the apps can show their text in other
# languages (src/core/I18n.cpp). The hooks swap the English words for the
# translation just before ImGui measures or draws them, so every button, menu
# and window title gets translated without changing each line of UI code.
#
# ImGui's own files aren't touched. If a newer ImGui moves things around and a
# hook can't be placed, the build still works (that text just stays English)
# and cmake prints a warning.
function(gb_patch_one src dst)
    file(READ "${src}" text)
    set(i 2)
    while(i LESS ARGC)
        # (ARGVn, not list(GET): the code has semicolons in it)
        set(find "${ARGV${i}}")
        math(EXPR j "${i} + 1")
        set(add "${ARGV${j}}")
        string(FIND "${text}" "${find}" at)
        if(at EQUAL -1)
            message(WARNING "Translations: couldn't hook ImGui at \"${find}\" (newer ImGui?). Some text stays English.")
        else()
            # (just the first one: imgui_internal.h is included more than once)
            string(LENGTH "${find}" n)
            math(EXPR cut "${at} + ${n}")
            string(SUBSTRING "${text}" 0 ${cut} head)
            string(SUBSTRING "${text}" ${cut} -1 tail)
            set(text "${head}${add}${tail}")
        endif()
        math(EXPR i "${i} + 2")
    endwhile()
    set(text "// Patched copy made by cmake/PatchImGui.cmake. Don't edit.\n${text}")
    if(EXISTS "${dst}")
        file(READ "${dst}" old)
    endif()
    if(NOT "${old}" STREQUAL "${text}")
        file(WRITE "${dst}" "${text}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${src}" "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
endfunction()

function(gb_patch_imgui out)
    file(MAKE_DIRECTORY "${out}")
    # The hooks themselves (set by I18n.cpp; null = English, nothing happens).
    set(decl "\nextern bool (*GImGuiTextHook)(const char** text, const char** text_end, bool hide_after_hash);\nextern const char* (*GImGuiFormatHook)(const char* fmt);\n")
    set(hook "\n    if (GImGuiTextHook) GImGuiTextHook(&")
    gb_patch_one("${imgui_SOURCE_DIR}/imgui.cpp" "${out}/imgui.cpp"
        "\n#include \"imgui_internal.h\"\n"
        "${decl}bool (*GImGuiTextHook)(const char**, const char**, bool) = nullptr;\nconst char* (*GImGuiFormatHook)(const char*) = nullptr;\n"
        "ImVec2 ImGui::CalcTextSize(const char* text, const char* text_end, bool hide_text_after_double_hash, float wrap_width)\n{"
        "${hook}text, &text_end, hide_text_after_double_hash);"
        "void ImGui::RenderTextEllipsis(ImDrawList* draw_list, const ImVec2& pos_min, const ImVec2& pos_max, float ellipsis_max_x, const char* text, const char* text_end_full, const ImVec2* text_size_if_known)\n{"
        "${hook}text, &text_end_full, true);"
        "void ImFormatStringToTempBufferV(const char** out_buf, const char** out_buf_end, const char* fmt, va_list args)\n{"
        "\n    if (GImGuiFormatHook) fmt = GImGuiFormatHook(fmt);")
    gb_patch_one("${imgui_SOURCE_DIR}/imgui_draw.cpp" "${out}/imgui_draw.cpp"
        "\n#include \"imgui_internal.h\"\n"
        "${decl}"
        "void ImDrawList::AddText(ImFont* font, float font_size, const ImVec2& pos, ImU32 col, const char* text_begin, const char* text_end, float wrap_width, const ImVec4* cpu_fine_clip_rect)\n{"
        "${hook}text_begin, &text_end, false);")
endfunction()
