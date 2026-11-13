cmake_minimum_required(VERSION 3.20)
project(GutsAndBolts VERSION 0.7.9 LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows resource compilation (embeds the application icon). The -I lets the
# resource compiler resolve "engine.ico" referenced from resources/app.rc.
if(WIN32)
    enable_language(RC)
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} -I \"${CMAKE_CURRENT_SOURCE_DIR}/resources\"")
endif()

include(FetchContent)

# The server's source files (used by both the full build and GB_SERVER_ONLY).
set(GB_SERVER_SOURCES
    src/server/ServerMain.cpp
    src/server/Server.cpp
    src/server/ServerGroups.cpp
    src/server/ServerSupport.cpp
    src/server/ServerPromos.cpp
    src/server/ServerGameWeb.cpp
    src/server/ServerChats.cpp
    src/server/ServerRoadmap.cpp
    src/server/ServerExtras.cpp
    src/server/ServerBanLists.cpp
    src/server/ServerTesters.cpp
    src/server/ServerRatings.cpp
    src/server/ServerBackgrounds.cpp
    src/server/ServerArt.cpp
    src/server/ServerPaste.cpp
    src/server/ServerConnect.cpp
    src/server/ServerFriends.cpp
    src/server/ServerSocial.cpp
    src/server/ServerAccounts.cpp
    src/server/ServerRelay.cpp
    src/server/ServerData.cpp
    src/server/ServerSafety.cpp
    src/server/ServerShield.cpp
    src/server/ServerForum.cpp
    src/server/ServerItems.cpp
    src/online/Protocol.cpp
    src/online/MemoryStore.cpp
    src/core/Account.cpp
    src/core/TextFilter.cpp
    src/core/Paths.cpp
    src/core/Log.cpp
    src/net/Socket.cpp
)

# GB_SERVER_ONLY builds just GutsAndBoltsServer: no graphics libraries needed,
# so it builds on a bare Linux box / VPS (cmake -DGB_SERVER_ONLY=ON).
option(GB_SERVER_ONLY "Build only the Guts&Bolts server (no graphics libraries needed)" OFF)
if(GB_SERVER_ONLY)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(json)
    FetchContent_Declare(monocypher
        GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
        GIT_TAG        ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f
        SOURCE_SUBDIR  cmake-build-disabled)
    FetchContent_MakeAvailable(monocypher)
    add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
    target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src)
    find_package(Threads REQUIRED)
    set(GB_GENERATED "${CMAKE_BINARY_DIR}/generated")
    file(MAKE_DIRECTORY "${GB_GENERATED}")
    include(cmake/Embed.cmake)
    # The emotes and animation packs that come with Guts&Bolts (tools/make_emotes.py): the
    # engine plays the built-in ones, and both servers put the starter ones in the catalog.
    gb_embed("${CMAKE_SOURCE_DIR}/assets/emotes/emotes.gbemotes" kEmotesData "${GB_GENERATED}/EmotesData.h")
    gb_embed("${CMAKE_SOURCE_DIR}/assets/emotes/pictures.gbemotes" kEmotePictures "${GB_GENERATED}/EmotePictures.h")
    add_custom_target(gb_version ALL
        COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${GB_GENERATED}
                -DVERSION=${PROJECT_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/GenVersion.cmake
        BYPRODUCTS "${GB_GENERATED}/Version.h"
        COMMENT "Checking version")
    add_executable(GutsAndBoltsServer ${GB_SERVER_SOURCES})
    target_include_directories(GutsAndBoltsServer PRIVATE src/ ${GB_GENERATED})
    add_dependencies(GutsAndBoltsServer gb_version)
    target_link_libraries(GutsAndBoltsServer PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    if(WIN32)
        target_link_libraries(GutsAndBoltsServer PRIVATE ws2_32 bcrypt shell32)
    endif()
    return()
endif()

# GB_MOBILE builds the phone / tablet version of the Player (SDL2 + OpenGL ES 3,
# touch controls). It's on automatically for Android; turning it on for Linux
# lets you try the phone version on a computer.
option(GB_MOBILE "Build the phone/tablet Player (SDL2 + OpenGL ES 3)" OFF)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EMSCRIPTEN)
    set(GB_MOBILE ON)
endif()
if(EMSCRIPTEN)
    # Let C++ errors (try / catch) work in the browser too. Without this a thrown error
    # just stops the page with "Uncaught <number>" and nobody can tell what went wrong.
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum iOS deployment target" FORCE)

    # miniaudio includes Apple AVFoundation headers for its iOS backend. Those
    # headers contain Objective-C declarations, so compile its implementation
    # translation unit as Objective-C++ rather than plain C++ on iOS.
    enable_language(OBJCXX)
    set_source_files_properties(src/core/Audio.cpp PROPERTIES LANGUAGE OBJCXX)
endif()

# ImGui (docking branch — includes the full editor UI system)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        d682dc41f58b98c9343ffbaa7d94f07e9024b3ca
)
FetchContent_MakeAvailable(imgui)

# ImGuizmo (move/rotate/scale gizmos drawn over the ImGui viewport).
# SOURCE_SUBDIR points at a non-existent dir so MakeAvailable only downloads the
# sources — it does NOT run ImGuizmo's own CMakeLists (which expects vcpkg).
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        105e4fde45e4e74aae8973d98750820ef7aca7e2
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(imguizmo)

# imnodes (MIT): the "blocks joined by lines" board in Studio's Visual Scripting
# window (src/editor/EditorBlocks.cpp). Studio only, so phones and the web Player
# never build it. Only downloaded; its own CMakeLists isn't run.
FetchContent_Declare(
    imnodes
    GIT_REPOSITORY https://github.com/Nelarius/imnodes.git
    GIT_TAG        2d0c280b07b2f62b120034811cc6e41251673961
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(imnodes)

# Lua 5.4 — the scripting language used by Script objects. The official repo
# has no CMake build, so only download it and compile the core library below.
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(lua)

# Lua's default os.execute implementation calls system(), which the iOS SDK
# marks unavailable. Replace only that shell hook on iOS; os.execute then
# reports failure instead of trying to launch a shell in the app sandbox.
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(LUA_LOSLIB "${lua_SOURCE_DIR}/loslib.c")
    file(READ "${LUA_LOSLIB}" LUA_LOSLIB_ORIGINAL)
    # Temporarily rename the macro identifier before replacing the call.
    # Otherwise a plain "system(cmd)" replacement also corrupts
    # "l_system(cmd)" itself into the invalid token "l_(-1)".
    string(REPLACE "l_system(cmd)" "l_system__ios(cmd)"
           LUA_LOSLIB_CONTENT "${LUA_LOSLIB_ORIGINAL}")
    string(REPLACE "system(cmd)" "(-1)"
           LUA_LOSLIB_CONTENT "${LUA_LOSLIB_CONTENT}")
    string(REPLACE "l_system__ios(cmd)" "l_system(cmd)"
           LUA_LOSLIB_CONTENT "${LUA_LOSLIB_CONTENT}")
    if("${LUA_LOSLIB_CONTENT}" STREQUAL "${LUA_LOSLIB_ORIGINAL}")
        message(FATAL_ERROR "Could not patch Lua os.execute for iOS: system(cmd) token not found")
    endif()
    file(WRITE "${LUA_LOSLIB}" "${LUA_LOSLIB_CONTENT}")
endif()

file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES
    "${lua_SOURCE_DIR}/lua.c"       # stand-alone interpreter
    "${lua_SOURCE_DIR}/luac.c"      # stand-alone compiler
    "${lua_SOURCE_DIR}/onelua.c"    # amalgamation (would duplicate symbols)
    "${lua_SOURCE_DIR}/ltests.c")   # internal test hooks
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES C_STANDARD 99)
# Lua's POSIX helpers include system()/popen(), which iOS explicitly marks
# unavailable. Keep the POSIX extension enabled on desktop Unix, but use Lua's
# built-in unsupported-command fallback on iOS (os.execute cannot launch shells
# inside an iOS app sandbox anyway).
if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)   # safe temp files, etc.
endif()

# miniaudio — sound on every platform, in a single header.
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        9634bedb5b5a2ca38c1ee7108a9358a4e233f14d
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(miniaudio)

# Opus — squeezes voice chat down small enough to send over the internet.
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    set(OPUS_DISABLE_INTRINSICS ON CACHE BOOL "" FORCE)   # (no x86 or ARM tricks in a browser)
    set(OPUS_STACK_PROTECTOR OFF CACHE BOOL "" FORCE)     # (the browser has no stack guard to check)
endif()
FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        ddbe48383984d56acd9e1ab6a090c54ca6b735a6
)
FetchContent_MakeAvailable(opus)

# JSON (scene files, undo history, copy / paste).
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# Monocypher — small, audited crypto: signs accounts, official badges and
# catalog items so they can't be faked.
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src)

# stb_image: reading PNG / JPG pictures (decals).
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5
)
FetchContent_MakeAvailable(stb)

# pl_mpeg: plays MPEG-1 videos (VideoFrame), picture and sound, in one small file.
FetchContent_Declare(
    plmpeg
    GIT_REPOSITORY https://github.com/phoboslab/pl_mpeg.git
    GIT_TAG        c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
)
FetchContent_MakeAvailable(plmpeg)

# portable-file-dialogs: the system's own "Open file" window (Browse... buttons).
FetchContent_Declare(
    pfd
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG        c12ea8c9a727f5320a2b4570aee863bbede2a204
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(pfd)

# zstd (decoder only): newer Roblox .rbxl / .rbxm files are compressed with it.
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG        v1.5.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(zstd)
file(GLOB ZSTD_SOURCES "${zstd_SOURCE_DIR}/lib/common/*.c" "${zstd_SOURCE_DIR}/lib/decompress/*.c")
add_library(gb_zstd STATIC ${ZSTD_SOURCES})
target_include_directories(gb_zstd PUBLIC ${zstd_SOURCE_DIR}/lib)
target_compile_definitions(gb_zstd PRIVATE ZSTD_DISABLE_ASM)
set_target_properties(gb_zstd PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

if(GB_MOBILE)
    # SDL2: window, OpenGL ES context, multi-touch, the Android activity.
    FetchContent_Declare(
        SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    if(ANDROID)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    else()
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
    endif()
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
    if(ANDROID)
        set(GB_SDL_LIBS SDL2::SDL2 SDL2::SDL2main)
    else()
        set(GB_SDL_LIBS SDL2::SDL2-static SDL2::SDL2main)
    endif()

    # glm is header-only; phones don't have it installed.
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        0af55ccecd98d4e5a8d1fad7de25ba429d60e863
        SOURCE_SUBDIR  cmake-build-disabled
    )
    FetchContent_MakeAvailable(glm)
    add_library(gb_glm INTERFACE)
    target_include_directories(gb_glm INTERFACE ${glm_SOURCE_DIR})
    set(GB_GLM gb_glm)

    if(ANDROID)
        set(GB_GL_LIBS GLESv3 EGL android log)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(GB_GL_LIBS "-framework OpenGLES" "-framework UIKit" "-framework Foundation" "-framework CoreText")
    else()
        set(GB_GL_LIBS GLESv2 EGL)
    endif()
else()
    find_package(glfw3  REQUIRED)
    find_package(GLEW   REQUIRED)
    find_package(glm    REQUIRED)
    find_package(OpenGL REQUIRED)
    set(GB_GLM glm::glm)
endif()

# ---------------------------------------------------------------------------
# Dear ImGui + ImGuizmo (UI for both apps)
# ---------------------------------------------------------------------------
# imgui.cpp and imgui_draw.cpp are copies with hooks for translations (see the file).
include(cmake/PatchImGui.cmake)
gb_patch_imgui("${CMAKE_BINARY_DIR}/imgui_i18n")
add_library(gb_imgui STATIC
    ${CMAKE_BINARY_DIR}/imgui_i18n/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${CMAKE_BINARY_DIR}/imgui_i18n/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
if(GB_MOBILE)
    target_sources(gb_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp)
    target_compile_definitions(gb_imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)
else()
    target_sources(gb_imgui PRIVATE ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp)
endif()
target_include_directories(gb_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
)
if(GB_MOBILE)
    target_link_libraries(gb_imgui PUBLIC ${GB_SDL_LIBS} ${GB_GL_LIBS})
else()
    target_link_libraries(gb_imgui PUBLIC glfw OpenGL::GL)
endif()

# Version.h: which commit this build is (for the update checker).
set(GB_GENERATED "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GB_GENERATED}")
# The default character (assets/models/player.obj), baked into the program.
include(cmake/Embed.cmake)
gb_embed("${CMAKE_SOURCE_DIR}/assets/models/player.obj" kPlayerObj "${GB_GENERATED}/PlayerObj.h")
# Translations (website/lang/*.json, shared with the website): one header each,
# plus LangData.h listing them all for src/core/I18n.cpp.
file(GLOB GB_LANG_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/website/lang/*.json")
set(GB_LANG_LIST "")
set(GB_LANG_INCLUDES "")
foreach(lang_file ${GB_LANG_FILES})
    get_filename_component(lang_code "${lang_file}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${lang_code}" lang_id)
    gb_embed("${lang_file}" kLang_${lang_id} "${GB_GENERATED}/Lang_${lang_id}.h")
    string(APPEND GB_LANG_INCLUDES "#include \"Lang_${lang_id}.h\"\n")
    string(APPEND GB_LANG_LIST "    {\"${lang_code}\", kLang_${lang_id}, kLang_${lang_id}Size},\n")
endforeach()
set(lang_text "#pragma once\n// Made by CMakeLists.txt from website/lang/*.json. Don't edit.\n#include <cstddef>\n${GB_LANG_INCLUDES}struct GbLangFile { const char* code; const unsigned char* data; std::size_t size; };\ninline const GbLangFile kLangFiles[] = {\n${GB_LANG_LIST}    {nullptr, nullptr, 0},\n};\n")
if(EXISTS "${GB_GENERATED}/LangData.h")
    file(READ "${GB_GENERATED}/LangData.h" lang_old)
endif()
if(NOT "${lang_old}" STREQUAL "${lang_text}")
    file(WRITE "${GB_GENERATED}/LangData.h" "${lang_text}")
endif()
gb_embed("${CMAKE_SOURCE_DIR}/assets/templates/shirt_template.png" kShirtTemplate "${GB_GENERATED}/ShirtTemplate.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/templates/pants_template.png" kPantsTemplate "${GB_GENERATED}/PantsTemplate.h")
# The Guts & Bolts logo picture (the Player's banner and loading screen).
gb_embed("${CMAKE_SOURCE_DIR}/assets/logo/logo.png" kLogoPng "${GB_GENERATED}/LogoPng.h")
# The UI font for Studio and the Player (Inter Medium, free and made for screens; see assets/fonts/OFL.txt).
gb_embed("${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Medium.otf" kUiFontData "${GB_GENERATED}/UiFont.h")
# Recorded sound effects (the rest are made in code, see Audio.cpp).
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/jump.wav"    kSoundJump    "${GB_GENERATED}/SoundJump.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/respawn.wav" kSoundRespawn "${GB_GENERATED}/SoundRespawn.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/spawn.wav"   kSoundSpawn   "${GB_GENERATED}/SoundSpawn.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/footsteps.mp3" kSoundFootsteps "${GB_GENERATED}/SoundFootsteps.h")
# Real recorded sound effects (CC0, see assets/sounds/real/CREDITS.md): one header each,
# and RealSounds.h lists them all by name ("hit_metal_2" is the 2nd take of "hit_metal").
file(GLOB GB_REAL_SOUNDS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/sounds/real/*.ogg")
list(SORT GB_REAL_SOUNDS)
set(GB_REAL_SOUND_INCLUDES "")
set(GB_REAL_SOUND_ROWS "")
foreach(f ${GB_REAL_SOUNDS})
    get_filename_component(n "${f}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${n}" id)
    gb_embed("${f}" kReal_${id} "${GB_GENERATED}/real_sounds/${n}.h")
    string(APPEND GB_REAL_SOUND_INCLUDES "#include \"real_sounds/${n}.h\"\n")
    string(APPEND GB_REAL_SOUND_ROWS "    {\"${n}\", kReal_${id}, kReal_${id}Size},\n")
endforeach()
set(GB_REAL_SOUNDS_TEXT "#pragma once\n// Made by CMakeLists.txt from assets/sounds/real. Don't edit.\n#include <cstddef>\n${GB_REAL_SOUND_INCLUDES}struct RealSound { const char* name; const unsigned char* data; std::size_t size; };\ninline const RealSound kRealSounds[] = {\n${GB_REAL_SOUND_ROWS}};\n")
if(EXISTS "${GB_GENERATED}/RealSounds.h")
    file(READ "${GB_GENERATED}/RealSounds.h" GB_REAL_OLD)
endif()
if(NOT "${GB_REAL_OLD}" STREQUAL "${GB_REAL_SOUNDS_TEXT}")
    file(WRITE "${GB_GENERATED}/RealSounds.h" "${GB_REAL_SOUNDS_TEXT}")
endif()
# The emotes and animation packs that come with Guts&Bolts (tools/make_emotes.py): the
# engine plays the built-in ones, and both servers put the starter ones in the catalog.
gb_embed("${CMAKE_SOURCE_DIR}/assets/emotes/emotes.gbemotes" kEmotesData "${GB_GENERATED}/EmotesData.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/emotes/pictures.gbemotes" kEmotePictures "${GB_GENERATED}/EmotePictures.h")
add_custom_target(gb_version ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${GB_GENERATED}
            -DVERSION=${PROJECT_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/GenVersion.cmake
    BYPRODUCTS "${GB_GENERATED}/Version.h"
    COMMENT "Checking version")

# Roblox FileMesh v7 uses Draco for compressed CORE MESH chunks. This is
# an engine dependency (not editor-only): the Player also needs to decode Roblox
# meshes from imported models. Keep it before gb_engine so all shared-engine
# targets can compile and link the same parser.
set(DRACO_JS_GLUE OFF CACHE BOOL "" FORCE)
set(DRACO_TESTS OFF CACHE BOOL "" FORCE)
set(DRACO_INSTALL OFF CACHE BOOL "" FORCE)
set(DRACO_TRANSCODER_SUPPORTED OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)   # (Draco checks these before building for the web)
    set(PYTHONINTERP_FOUND TRUE)
    set(ENV{EMSCRIPTEN} "${EMSCRIPTEN_ROOT_PATH}")
endif()
FetchContent_Declare(
    draco
    GIT_REPOSITORY https://github.com/google/draco.git
    GIT_TAG        8786740086a9f4d83f44aa83badfbea4dce7a1b5
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(draco)

# Draco's exported target does not propagate its source-tree header root on all
# supported toolchains (notably Android and MinGW). The public decoder headers
# live under src/draco, so expose that include root to the shared engine.
set(GB_DRACO_INCLUDE_DIR "${draco_SOURCE_DIR}/src")

# ---------------------------------------------------------------------------
# The engine: scene, physics, scripting, rendering, game session.
# Shared by the editor (GutsAndBolts) and the player app (GutsAndBoltsPlayer).
# ---------------------------------------------------------------------------
add_library(gb_engine STATIC
    src/core/CrashHandler.cpp
    src/core/Log.cpp
    src/core/Settings.cpp
    src/core/Scalability.cpp
    src/core/DeviceFeatures.cpp
    src/core/KeyBinds.cpp
    src/core/I18n.cpp
    src/core/Pad.cpp
    src/core/Vr.cpp
    src/core/Paths.cpp
    src/core/FileDialog.cpp
    src/core/Audio.cpp
    src/core/SoundFx.cpp
    src/core/Voice.cpp
    src/core/UpdateChecker.cpp
    src/core/Account.cpp
    src/core/TextFilter.cpp
    src/core/Language.cpp
    src/scene/ObjImport.cpp
    src/scene/PlayerModel.cpp
    src/renderer/Shader.cpp
    src/renderer/Mesh.cpp
    src/renderer/MeshLod.cpp
    src/renderer/Textures.cpp
    src/renderer/Camera.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/ShadowMap.cpp
    src/renderer/SceneRenderer.cpp
    src/renderer/SceneRendererTemporal.cpp
    src/renderer/SceneRendererFog.cpp
    src/renderer/Benchmark.cpp
    src/renderer/GpuCaps.cpp
    src/renderer/ViewportRender.cpp
    src/scene/SceneNode.cpp
    src/scene/Scene.cpp
    src/scene/Player.cpp
    src/scene/Outfits.cpp
    src/scene/PlayerVrBody.cpp
    src/scene/Emotes.cpp
    src/scene/Physics.cpp
    src/scene/LegacyJoints.cpp
    src/scene/NavMesh.cpp
    src/scene/Npc.cpp
    src/scene/Water.cpp
    src/scene/Terrain.cpp
    src/scene/Liquid.cpp
    src/scene/Flood.cpp
    src/scene/RigidBodies.cpp
    src/scene/Particles.cpp
    src/scene/Weather.cpp
    src/scene/Vehicles.cpp
    src/scene/IkControls.cpp
    src/scene/LinkedModels.cpp
    src/scene/MaterialGraph.cpp
    src/scene/Animation.cpp
    src/scene/AnimFromText.cpp
    src/scene/Ragdoll.cpp
    src/scene/Effects.cpp
    src/scene/SoundMix.cpp
    src/scene/Blast.cpp
    src/scene/Serializer.cpp
    src/scene/RobloxFile.cpp
    src/scene/RobloxAnim.cpp
    src/scene/RobloxMesh.cpp
    src/scene/EditMesh.cpp
    src/scene/MeshTools.cpp
    src/scene/Skin.cpp
    src/scene/BundleRig.cpp
    src/scene/Cloth.cpp
    src/scene/Gore.cpp
    src/scene/SoftTissue.cpp
    src/scene/Shatter.cpp
    src/scene/Realism.cpp
    src/scene/Ballistics.cpp
    src/scene/Acoustics.cpp
    src/scene/Impacts.cpp
    src/scene/FireSpread.cpp
    src/scene/Tracks.cpp
    src/scene/BounceLight.cpp
    src/scene/LipSync.cpp
    src/scene/MeshUV.cpp
    src/scene/Csg.cpp
    src/scripting/Luau.cpp
    src/scripting/ScriptEngine.cpp
    src/scripting/ScriptHistory.cpp
    src/scripting/LuaTypes.cpp
    src/scripting/LuaInstance.cpp
    src/scripting/ScriptRemotes.cpp
    src/scripting/SpatialQueries.cpp
    src/scripting/LuaRealism.cpp
    src/scripting/ServerMemory.cpp
    src/scripting/ScriptActions.cpp
    src/scripting/ScriptHelp.cpp
    src/scripting/ScriptCheck.cpp
    src/game/GameSession.cpp
    src/game/LiveProfiler.cpp
    src/game/Videos.cpp
    src/core/FrameProfile.cpp
    src/game/Hud.cpp
    src/game/EmoteMenu.cpp
    src/game/GameGui.cpp
    src/game/RichText.cpp
    src/game/Prompts.cpp
    src/game/ClickDetectors.cpp
    src/game/DragDetectors.cpp
    src/game/PlayCamera.cpp
    src/game/SettingsWindow.cpp
    src/game/Profile.cpp
    src/game/UpdateToast.cpp
    src/game/Badges.cpp
    src/game/Bolts.cpp
    src/game/ClipRecorder.cpp
    src/online/Protocol.cpp
    src/online/MemoryStore.cpp
    src/online/OnlineClient.cpp
    src/online/AssetCache.cpp
    src/game/Catalog.cpp
    src/game/TouchControls.cpp
    src/net/Socket.cpp
    src/net/NetGame.cpp
    src/net/Streaming.cpp
    src/net/MoveGuard.cpp
    src/net/ChatTranslate.cpp
    src/editor/Theme.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS" OR EMSCRIPTEN)
    target_sources(gb_engine PRIVATE src/scene/LiquidGpuIOS.cpp)
else()
    target_sources(gb_engine PRIVATE src/scene/LiquidGpu.cpp)
endif()
if(GB_MOBILE)
    target_sources(gb_engine PRIVATE src/core/AppWindowSDL.cpp)
    target_compile_definitions(gb_engine PUBLIC GB_MOBILE GB_GLES)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_compile_definitions(gb_engine PUBLIC GB_IOS GB_APP_VERSION="${PROJECT_VERSION}")
        # Notch / home bar insets and turning to landscape for games (UIKit).
        target_sources(gb_engine PRIVATE src/core/IosPlatform.mm)
        set_source_files_properties(src/core/IosPlatform.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
else()
    target_sources(gb_engine PRIVATE src/core/AppWindow.cpp)
endif()
# mbedTLS: secure connections (https:// and wss://) to a Guts&Bolts server on
# Cloudflare. Only the apps need it; the C++ server speaks plain TCP.
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    mbedtls
    URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(mbedtls)
foreach(t mbedtls mbedx509 mbedcrypto)
    if(TARGET ${t})
        set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()
# The trusted certificate authorities (Mozilla's list, from https://curl.se/ca/cacert.pem).
gb_embed("${CMAKE_SOURCE_DIR}/assets/certs/cacert.pem" kCaCerts "${GB_GENERATED}/CaCerts.h")
target_compile_definitions(gb_engine PRIVATE GB_WEBSOCKETS)

target_include_directories(gb_engine PUBLIC src/ ${miniaudio_SOURCE_DIR} ${GB_GENERATED} ${stb_SOURCE_DIR} ${plmpeg_SOURCE_DIR} ${pfd_SOURCE_DIR} ${GB_DRACO_INCLUDE_DIR} ${CMAKE_BINARY_DIR})
add_dependencies(gb_engine gb_version)
find_package(Threads REQUIRED)
target_link_libraries(gb_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS} opus)
if(APPLE)
    target_link_libraries(gb_engine PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
target_link_libraries(gb_engine PUBLIC
    gb_imgui
    ${GB_GLM}
    draco::draco
    lua_static
    nlohmann_json::nlohmann_json
    monocypher
    gb_zstd
    mbedtls mbedx509 mbedcrypto
)
if(NOT GB_MOBILE)
    target_link_libraries(gb_engine PUBLIC glfw GLEW::GLEW OpenGL::GL)
endif()

# OpenXR: VR headsets (SteamVR, Quest Link / Air Link, Windows Mixed Reality...)
# in the computer Player. Only the loader is built: it finds the headset's own
# software when the game starts, so nobody needs a headset to build or play.
option(GB_VR "VR headset support (OpenXR) in the computer apps" ON)
# GB_QUEST: the Meta Quest version of the Android Player (android/app's "quest"
# flavour). The game runs on the headset itself, always in VR.
option(GB_QUEST "Build the Android Player for Meta Quest headsets (OpenXR)" OFF)
if((GB_VR AND NOT GB_MOBILE AND NOT APPLE) OR (GB_QUEST AND ANDROID))
    set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
    set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
    set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        openxr
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG        c15d38cb4bb10a5b7e075f74493ff13896e2597a
    )
    FetchContent_MakeAvailable(openxr)
    target_link_libraries(gb_engine PUBLIC openxr_loader)
    target_compile_definitions(gb_engine PUBLIC GB_VR)
    if(GB_QUEST)
        target_compile_definitions(gb_engine PUBLIC GB_QUEST)
    elseif(NOT WIN32)
        find_package(X11 REQUIRED)
        target_link_libraries(gb_engine PUBLIC X11::X11)
        if(TARGET OpenGL::GLX)
            target_link_libraries(gb_engine PUBLIC OpenGL::GLX)
        endif()
    endif()
endif()
# Tracy (BSD-3): a very detailed profiler for people working on the engine itself
# (game makers use the Live Profiler instead). OFF by default, so normal builds
# and players never have it. Turn on with -DGB_TRACY=ON, run Studio or the Player,
# and open the Tracy viewer on the same computer. See docs/engine/tracy.md.
option(GB_TRACY "Build with the Tracy profiler (for engine developers)" OFF)
if(GB_TRACY)
    # Only records while the viewer is connected, and only lets this computer connect.
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    set(TRACY_NO_BROADCAST ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG        05cceee0df3b8d7c6fa87e9638af311dbabc63cb   # v0.13.1
    )
    FetchContent_MakeAvailable(tracy)
    target_link_libraries(gb_engine PUBLIC Tracy::TracyClient)
    target_compile_definitions(gb_engine PUBLIC GB_TRACY)
endif()
target_compile_definitions(gb_engine PUBLIC GLM_ENABLE_EXPERIMENTAL)
if(WIN32)
    target_link_libraries(gb_engine PUBLIC dbghelp ws2_32 bcrypt)
endif()

if(GB_MOBILE)
    # Phones and tablets get the Player only. On Android it's a library that
    # SDL's Java activity loads (it must be called "main").
    set(PLAYER_SOURCES src/player/main.cpp src/player/PlayerApp.cpp src/player/PlayerOnline.cpp src/player/PlayerSocial.cpp src/player/PlayerForum.cpp src/player/ItemRenders.cpp src/player/PlayerMail.cpp src/player/PlayerLibrary.cpp src/player/PlayerDevConsole.cpp src/player/PlayerPasses.cpp src/player/PlayerPhoto.cpp src/player/PlayerBreak.cpp src/player/PlayerSpectate.cpp src/player/PlayerClips.cpp src/player/WebKeyboard.cpp src/player/PlayerFriends.cpp src/player/PlayerLogin.cpp src/player/PlayerSafety.cpp src/player/PlayerVr.cpp src/player/LaunchLink.cpp)
    if(ANDROID)
        add_library(main SHARED ${PLAYER_SOURCES})
        target_link_libraries(main PRIVATE gb_engine)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # The home-screen icon (ios/Assets.xcassets, compiled by Xcode).
        set(GB_IOS_ASSETS "${CMAKE_SOURCE_DIR}/ios/Assets.xcassets")
        add_executable(GutsAndBoltsPlayer MACOSX_BUNDLE ${PLAYER_SOURCES} ${GB_IOS_ASSETS})
        set_source_files_properties(${GB_IOS_ASSETS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_compile_definitions(GutsAndBoltsPlayer PRIVATE GB_IOS GB_APP_VERSION=\"${PROJECT_VERSION}\")
        # Installing with a free Apple ID needs an app id nobody else has taken, and
        # your own Team ID (see ios/README.md): -DGB_IOS_BUNDLE_ID=... -DGB_IOS_TEAM=...
        set(GB_IOS_BUNDLE_ID "net.gutsandbolts.player" CACHE STRING "iOS app id (bundle identifier)")
        set(GB_IOS_TEAM "" CACHE STRING "Apple Team ID to sign with (empty = pick it in Xcode)")
        set_target_properties(GutsAndBoltsPlayer PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "${GB_IOS_BUNDLE_ID}"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GB_IOS_BUNDLE_ID}"
            MACOSX_BUNDLE_BUNDLE_NAME "Guts & Bolts"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/ios/Info.plist"
            MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "15.0"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE "NO")
        if(GB_IOS_TEAM)
            set_target_properties(GutsAndBoltsPlayer PROPERTIES
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${GB_IOS_TEAM}"
                XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic")
        endif()
        target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)
        add_custom_command(TARGET GutsAndBoltsPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:GutsAndBoltsPlayer>/Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games" "$<TARGET_BUNDLE_DIR:GutsAndBoltsPlayer>/Resources/games"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/catalog" "$<TARGET_BUNDLE_DIR:GutsAndBoltsPlayer>/Resources/catalog"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets" "$<TARGET_BUNDLE_DIR:GutsAndBoltsPlayer>/Resources/assets"
            VERBATIM)
    elseif(EMSCRIPTEN)
        # The web Player (web/README.md): WebAssembly + WebGL 2, for browsers like Safari on iPhone.
        # The games, catalog and pictures that come with it are packed into GutsAndBoltsPlayer.data.
        add_executable(GutsAndBoltsPlayer ${PLAYER_SOURCES})
        target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)
        set_target_properties(GutsAndBoltsPlayer PROPERTIES SUFFIX ".js")
        target_link_options(GutsAndBoltsPlayer PRIVATE
            -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1
            -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=134217728 -sTOTAL_STACK=4194304
            -sFETCH=1 -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0 -lwebsocket.js -lidbfs.js
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/games@/gb/games"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/catalog@/gb/catalog"
            "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/assets@/gb/assets")
    else()
        add_executable(GutsAndBoltsPlayer ${PLAYER_SOURCES})
        target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)
        add_custom_command(TARGET GutsAndBoltsPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games" "$<TARGET_FILE_DIR:GutsAndBoltsPlayer>/games"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/catalog" "$<TARGET_FILE_DIR:GutsAndBoltsPlayer>/catalog"
            VERBATIM)
    endif()
    return()
endif()

# ---------------------------------------------------------------------------
# GutsAndBolts — the editor (Studio)
# ---------------------------------------------------------------------------
# ufbx (.fbx and .obj) and cgltf (.gltf / .glb): importing 3D models in Studio.
FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG        26a482ae66871d7de36eb722aa060bce95bce274
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(ufbx)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        360db1a95480fe102ae9c69b27c5d101167ff5ba
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(cgltf)
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        4affad044571506a5724c9a6f15424f43e86f731
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(meshoptimizer)

# Assimp: every other 3D format worth having (.dae, .3ds, .x, .3mf, .lwo, .ac,
# .off, .dxf, ...). Its readers, plus the few writers File > Export As uses
# (.obj .fbx .gltf/.glb .stl .ply .dae). Not its tools.
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
foreach(fmt OBJ FBX GLTF STL PLY COLLADA)
    set(ASSIMP_BUILD_${fmt}_EXPORTER ON CACHE BOOL "" FORCE)
endforeach()
# (The glTF and FBX writers need their readers' code to build; Studio still
# opens .gltf / .glb with cgltf and .fbx with ufbx, see ModelImport.cpp.)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
# Assimp's own copy of zlib is too old for the newest macOS SDK, and every Mac
# has zlib built in, so Macs use that one.
if(APPLE)
    set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
else()
    set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
endif()
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
foreach(fmt COLLADA 3DS X 3MF LWO LWS AC OFF DXF B3D MS3D MD2 MD3 MD5 ASE SMD X3D AMF OGRE OPENGEX
            IRRMESH IRR COB Q3D NFF RAW TERRAGEN XGL NDO HMP MDL MMD SIB)
    set(ASSIMP_BUILD_${fmt}_IMPORTER ON CACHE BOOL "" FORCE)
endforeach()
set(_gb_shared ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG        c35200e38ea8f058812b83de2ef32c6093b0ece2
)
FetchContent_MakeAvailable(assimp)
set(BUILD_SHARED_LIBS ${_gb_shared})

# libwebp (reading .webp pictures) and nanosvg (drawing .svg pictures): Studio
# turns them into PNGs when they're imported, so every Player can show them.
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG        845d5476a866141ba35ac133f856fa62f0b7445f
)
FetchContent_MakeAvailable(libwebp)
FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        239e102ec2c691f2902e20ace2ed36ee4a35cfe6
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(nanosvg)

file(GLOB MESHOPT_SOURCES "${meshoptimizer_SOURCE_DIR}/src/*.cpp")
add_library(gb_models STATIC ${ufbx_SOURCE_DIR}/ufbx.c ${MESHOPT_SOURCES})
target_include_directories(gb_models PUBLIC ${ufbx_SOURCE_DIR} ${cgltf_SOURCE_DIR} ${meshoptimizer_SOURCE_DIR}/src
                           ${draco_SOURCE_DIR}/src ${CMAKE_BINARY_DIR} ${nanosvg_SOURCE_DIR}/src
                           ${libwebp_SOURCE_DIR}/src)
target_link_libraries(gb_models PUBLIC draco::draco assimp::assimp webpdecoder)

set(EDITOR_SOURCES
    src/main.cpp
    src/Application.cpp
    src/editor/Editor.cpp
    src/editor/ModelImport.cpp
    src/editor/ModelExport.cpp
    src/editor/EditorExport.cpp
    src/editor/EditorImport.cpp
    src/editor/PictureImport.cpp
    src/editor/AiTools.cpp
    src/editor/FyxaTaskStore.cpp
    src/editor/FyxaSkills.cpp
    src/editor/FyxaToolSelection.cpp
    src/editor/FyxaProviderCapabilities.cpp
    src/editor/AiToolsRun.cpp
    src/editor/AiMeshTools.cpp
    src/editor/AiGameTools.cpp
    src/core/CredentialStore.cpp
    src/editor/StudioMcp.cpp
    src/editor/StudioRelay.cpp
    src/editor/panels/AssistantPanel.cpp
    src/editor/EditorRibbon.cpp
    src/editor/Icons.cpp
    src/editor/panels/ViewportModeling.cpp
    src/editor/panels/ViewportObjectModal.cpp
    src/editor/panels/ViewportHeader.cpp
    src/editor/panels/ViewportBones.cpp
    src/editor/panels/ViewportTerrain.cpp
    src/editor/EditorModes.cpp
    src/editor/EditorMeshMenus.cpp
    src/editor/EditorBundles.cpp
    src/editor/EditorArmature.cpp
    src/editor/EditorUV.cpp
    src/editor/EditorSolid.cpp
    src/editor/EditorAutosave.cpp
    src/editor/EditorTimeMachine.cpp
    src/editor/EditorMaterials.cpp
    src/editor/EditorMaterialGraph.cpp
    src/editor/EditorAiActivity.cpp
    src/editor/EditorLevelGen.cpp
    src/editor/LevelGen.cpp
    src/editor/GameTemplates.cpp
    src/editor/EditorTemplates.cpp
    src/editor/EditorCutscenes.cpp
    src/editor/EditorBulkEdit.cpp
    src/editor/EditorBlender.cpp
    src/editor/EditorWorkspace.cpp
    src/editor/EditorSimple.cpp
    src/editor/EditorBlocks.cpp
    src/editor/VisualScript.cpp
    src/editor/EditorScriptHistory.cpp
    src/editor/EditorPhysicsReplay.cpp
    src/editor/PhysicsReplay.cpp
    src/editor/EditorCompare.cpp
    src/editor/SceneDiff.cpp
    src/editor/EditorOnline.cpp
    src/editor/EditorPlatform.cpp
    src/editor/EditorPhoneTest.cpp
    src/editor/EditorPluginPublish.cpp
    src/editor/Thumbnailer.cpp
    src/editor/Plugins.cpp
    src/editor/Premades.cpp
    src/editor/TeamCreate.cpp
    src/editor/MultiTest.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/AnimationEditor.cpp
    src/editor/panels/OutlinerPanel.cpp
    src/editor/panels/PropertiesPanel.cpp
    src/editor/panels/EnvironmentPanel.cpp
    src/editor/panels/ToolboxPanel.cpp
    src/editor/panels/PlayerPanel.cpp
    src/editor/panels/OutputPanel.cpp
    src/editor/panels/ScriptEditorPanel.cpp
    src/editor/panels/ScriptAnalysis.cpp
)
if(WIN32)
    list(APPEND EDITOR_SOURCES resources/app.rc)
endif()
add_executable(GutsAndBolts ${EDITOR_SOURCES})
add_library(gb_imnodes STATIC ${imnodes_SOURCE_DIR}/imnodes.cpp)
target_include_directories(gb_imnodes PUBLIC ${imnodes_SOURCE_DIR})
target_link_libraries(gb_imnodes PUBLIC gb_imgui)
target_link_libraries(GutsAndBolts PRIVATE gb_engine gb_models gb_imnodes)
# Linux desktops may provide Secret Service through libsecret (GNOME Keyring,
# KWallet's Secret Service compatibility, etc.). Keep it optional for minimal
# distributions and headless builds; those retain session-only credentials.
if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND NOT GB_MOBILE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
        if(TARGET PkgConfig::LIBSECRET)
            target_compile_definitions(GutsAndBolts PRIVATE GB_HAVE_LIBSECRET=1)
            target_link_libraries(GutsAndBolts PRIVATE PkgConfig::LIBSECRET)
        endif()
    endif()
endif()
if(WIN32)
    target_link_libraries(GutsAndBolts PRIVATE Advapi32 shell32)
elseif(APPLE)
    target_link_libraries(GutsAndBolts PRIVATE "-framework Security")
endif()

# ---------------------------------------------------------------------------
# GutsAndBoltsPlayer — the platform app for finding and playing games
# ---------------------------------------------------------------------------
set(PLAYER_SOURCES
    src/player/main.cpp
    src/player/PlayerApp.cpp
    src/player/PlayerOnline.cpp
    src/player/PlayerSocial.cpp
    src/player/PlayerForum.cpp
    src/player/ItemRenders.cpp
    src/player/PlayerMail.cpp
    src/player/PlayerLibrary.cpp
    src/player/PlayerDevConsole.cpp
    src/player/PlayerPasses.cpp
    src/player/PlayerPhoto.cpp
    src/player/PlayerBreak.cpp
    src/player/PlayerSpectate.cpp
    src/player/PlayerClips.cpp
    src/player/WebKeyboard.cpp
    src/player/PlayerFriends.cpp
    src/player/PlayerLogin.cpp
    src/player/PlayerSafety.cpp
    src/player/PlayerVr.cpp
    src/player/LaunchLink.cpp
)
if(WIN32)
    list(APPEND PLAYER_SOURCES resources/app.rc)
endif()
add_executable(GutsAndBoltsPlayer ${PLAYER_SOURCES})
target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)
if(APPLE)   # gutsandbolts:// links (src/player/LaunchLink.cpp)
    target_link_libraries(GutsAndBoltsPlayer PRIVATE "-framework CoreServices")
    # Says why we want the microphone (voice chat) when macOS asks.
    target_link_options(GutsAndBoltsPlayer PRIVATE
        "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_SOURCE_DIR}/cmake/PlayerInfo.plist")
endif()

# ---------------------------------------------------------------------------
# GutsAndBoltsGameServer — runs online games by itself (no window, nobody playing
# on it), so a game keeps going whoever leaves. See src/gameserver/main.cpp.
# ---------------------------------------------------------------------------
add_executable(GutsAndBoltsGameServer src/gameserver/main.cpp)
target_link_libraries(GutsAndBoltsGameServer PRIVATE gb_engine)

# ---------------------------------------------------------------------------
# GutsAndBoltsServer — keeps accounts, Bolts, badges and uploads (no graphics)
# ---------------------------------------------------------------------------
add_executable(GutsAndBoltsServer ${GB_SERVER_SOURCES})
target_include_directories(GutsAndBoltsServer PRIVATE src/ ${GB_GENERATED})
add_dependencies(GutsAndBoltsServer gb_version)
target_link_libraries(GutsAndBoltsServer PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
if(WIN32)
    target_link_libraries(GutsAndBoltsServer PRIVATE ws2_32 bcrypt shell32)
endif()

# The server's public roadmap, with signed requests like the apps send.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32 AND NOT GB_MOBILE)
    set(GB_SERVER_TEST_SOURCES ${GB_SERVER_SOURCES})
    list(FILTER GB_SERVER_TEST_SOURCES EXCLUDE REGEX "ServerMain\\.cpp$")
    add_executable(ServerRoadmapTests tests/ServerRoadmapTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerRoadmapTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerRoadmapTests gb_version)
    target_link_libraries(ServerRoadmapTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerRoadmapTests COMMAND ServerRoadmapTests)
endif()

# The server's favourite catalog items (item.favorite), with signed requests like the apps send.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32 AND NOT GB_MOBILE)
    add_executable(ServerItemFavoritesTests tests/ServerItemFavoritesTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerItemFavoritesTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerItemFavoritesTests gb_version)
    target_link_libraries(ServerItemFavoritesTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerItemFavoritesTests COMMAND ServerItemFavoritesTests)

    add_executable(ServerWishlistAlertTests tests/ServerWishlistAlertTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerWishlistAlertTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerWishlistAlertTests gb_version)
    target_link_libraries(ServerWishlistAlertTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerWishlistAlertTests COMMAND ServerWishlistAlertTests)

    # Achievements, game update logs and events (ServerExtras.cpp).
    add_executable(ServerExtrasTests tests/ServerExtrasTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerExtrasTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerExtrasTests gb_version)
    target_link_libraries(ServerExtrasTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerExtrasTests COMMAND ServerExtrasTests)

    # Creators' ban lists, used across their games (ServerBanLists.cpp).
    add_executable(ServerBanListsTests tests/ServerBanListsTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerBanListsTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerBanListsTests gb_version)
    target_link_libraries(ServerBanListsTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerBanListsTests COMMAND ServerBanListsTests)
    add_executable(ServerAssetVersionsTests tests/ServerAssetVersionsTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerAssetVersionsTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerAssetVersionsTests gb_version)
    target_link_libraries(ServerAssetVersionsTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerAssetVersionsTests COMMAND ServerAssetVersionsTests)

    # The Art portal: posting, staff check, faves, comments (ServerArt.cpp).
    add_executable(ServerArtTests tests/ServerArtTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerArtTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerArtTests gb_version)
    target_link_libraries(ServerArtTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerArtTests COMMAND ServerArtTests)

    # G&B Paste: making, listing, running out, deleting, reporting (ServerPaste.cpp).
    add_executable(ServerPasteTests tests/ServerPasteTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerPasteTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerPasteTests gb_version)
    target_link_libraries(ServerPasteTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerPasteTests COMMAND ServerPasteTests)

    # Early testers: a testers-only game, tester notes, going public (ServerTesters.cpp).
    add_executable(ServerTestersTests tests/ServerTestersTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerTestersTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerTestersTests gb_version)
    target_link_libraries(ServerTestersTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerTestersTests COMMAND ServerTestersTests)
    # Age ratings: questions -> rating, history, players' rating limit (ServerRatings.cpp).
    add_executable(ServerRatingsTests tests/ServerRatingsTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerRatingsTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerRatingsTests gb_version)
    target_link_libraries(ServerRatingsTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerRatingsTests COMMAND ServerRatingsTests)
    # Profile backgrounds: a passed decal behind your avatar (ServerBackgrounds.cpp).
    add_executable(ServerBackgroundsTests tests/ServerBackgroundsTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerBackgroundsTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerBackgroundsTests gb_version)
    target_link_libraries(ServerBackgroundsTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerBackgroundsTests COMMAND ServerBackgroundsTests)

    # Studio Learn progress: account validation, persistence across server restarts and guest denial.
    add_executable(ServerLearnProgressTests tests/ServerLearnProgressTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerLearnProgressTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerLearnProgressTests gb_version)
    target_link_libraries(ServerLearnProgressTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerLearnProgressTests COMMAND ServerLearnProgressTests)
endif()

# The server's OrderedDataStore:GetSortedAsync (data.sorted), with signed requests like the apps send.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32 AND NOT GB_MOBILE)
    add_executable(ServerOrderedDataTests tests/ServerOrderedDataTests.cpp ${GB_SERVER_TEST_SOURCES})
    target_include_directories(ServerOrderedDataTests PRIVATE src/ ${GB_GENERATED})
    add_dependencies(ServerOrderedDataTests gb_version)
    target_link_libraries(ServerOrderedDataTests PRIVATE nlohmann_json::nlohmann_json monocypher Threads::Threads)
    add_test(NAME ServerOrderedDataTests COMMAND ServerOrderedDataTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene")
endif()

# Studio's sample plugins.
add_custom_command(TARGET GutsAndBolts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GutsAndBolts>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/plugins"
            "$<TARGET_FILE_DIR:GutsAndBolts>/plugins"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GutsAndBolts>/assets/fyxa/skills"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/fyxa/skills"
            "$<TARGET_FILE_DIR:GutsAndBolts>/assets/fyxa/skills"
    VERBATIM)

# Ship the sample games and official catalog items next to the programs.
foreach(app GutsAndBolts GutsAndBoltsPlayer)
    add_custom_command(TARGET ${app} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app}>/games"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games"
                "$<TARGET_FILE_DIR:${app}>/games"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${app}>/catalog"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
                "$<TARGET_FILE_DIR:${app}>/catalog"
        VERBATIM)
endforeach()

# When building with MinGW, the executable depends on runtime DLLs that live in
# the compiler's bin directory (libstdc++, libgcc, glfw3, glew). Copy them next
# to the binary after each build so it can be launched without a configured PATH.
if(MINGW)
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(RUNTIME_DLLS
        libstdc++-6.dll
        libgcc_s_seh-1.dll
        libwinpthread-1.dll
        glfw3.dll
        glew32.dll
    )
    foreach(dll ${RUNTIME_DLLS})
        if(EXISTS "${MINGW_BIN}/${dll}")
            foreach(app GutsAndBolts GutsAndBoltsPlayer GutsAndBoltsServer)
            add_custom_command(TARGET ${app} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${MINGW_BIN}/${dll}" "$<TARGET_FILE_DIR:${app}>"
                VERBATIM)
        endforeach()
        endif()
    endforeach()
endif()


# The website's gutsandbolts://learn/<id> route is parsed safely by the Player.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND UNIX AND NOT APPLE AND NOT GB_MOBILE)
    add_executable(LaunchLinkTests tests/LaunchLinkTests.cpp src/player/LaunchLink.cpp)
    target_include_directories(LaunchLinkTests PRIVATE src/)
    add_test(NAME LaunchLinkTests COMMAND LaunchLinkTests)
endif()

# Ollama capabilities are read from /api/show, never guessed from model names.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(FyxaProviderCapabilitiesTests tests/FyxaProviderCapabilitiesTests.cpp src/editor/FyxaProviderCapabilities.cpp)
    target_include_directories(FyxaProviderCapabilitiesTests PRIVATE src/)
    target_link_libraries(FyxaProviderCapabilitiesTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME FyxaProviderCapabilitiesTests COMMAND FyxaProviderCapabilitiesTests)
endif()

# Fyxa exposes only relevant native and external tools, with explicit discovery for omitted capabilities.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(FyxaToolSelectionTests tests/FyxaToolSelectionTests.cpp src/editor/FyxaToolSelection.cpp)
    target_include_directories(FyxaToolSelectionTests PRIVATE src/)
    target_link_libraries(FyxaToolSelectionTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME FyxaToolSelectionTests COMMAND FyxaToolSelectionTests)
endif()

# Fyxa's shared HTTP client must interrupt stalled requests when cancellation is signalled.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT GB_MOBILE)
    add_executable(NetHttpCancellationTests tests/NetHttpCancellationTests.cpp)
    target_link_libraries(NetHttpCancellationTests PRIVATE gb_engine)
    add_test(NAME NetHttpCancellationTests COMMAND NetHttpCancellationTests)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(FyxaSkillsTests tests/FyxaSkillsTests.cpp src/editor/FyxaSkills.cpp)
    target_include_directories(FyxaSkillsTests PRIVATE src/)
    target_link_libraries(FyxaSkillsTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME FyxaSkillsTests COMMAND FyxaSkillsTests)
endif()

# Approval grant schema for additive, per-tool/per-server, expiring trust tiers.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(FyxaApprovalRecordTests tests/FyxaApprovalRecordTests.cpp)
    target_include_directories(FyxaApprovalRecordTests PRIVATE src/)
    target_link_libraries(FyxaApprovalRecordTests PRIVATE nlohmann_json::nlohmann_json)
    add_test(NAME FyxaApprovalRecordTests COMMAND FyxaApprovalRecordTests)
endif()

# Fyxa task tracking is a small standalone JSON store used by the assistant.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(FyxaTaskStoreTests tests/FyxaTaskStoreTests.cpp src/editor/FyxaTaskStore.cpp)
    target_include_directories(FyxaTaskStoreTests PRIVATE src/)
    target_link_libraries(FyxaTaskStoreTests PRIVATE nlohmann_json::nlohmann_json Threads::Threads)
    add_test(NAME FyxaTaskStoreTests COMMAND FyxaTaskStoreTests)
endif()

# Connected-app relay capabilities are restricted by an explicit host-side allowlist.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(StudioMcpAccessTests tests/StudioMcpAccessTests.cpp)
    target_include_directories(StudioMcpAccessTests PRIVATE src/)
    add_test(NAME StudioMcpAccessTests COMMAND StudioMcpAccessTests)
    add_executable(StudioRelayApprovalTests tests/StudioRelayApprovalTests.cpp)
    target_include_directories(StudioRelayApprovalTests PRIVATE src/)
    add_test(NAME StudioRelayApprovalTests COMMAND StudioRelayApprovalTests)
endif()

# MCP HTTP wire-envelope compatibility: stateless 2026-07-28 and legacy sessions.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(ExternalMcpProtocolTests tests/ExternalMcpProtocolTests.cpp)
    target_include_directories(ExternalMcpProtocolTests PRIVATE src/)
    target_link_libraries(ExternalMcpProtocolTests PRIVATE nlohmann_json::nlohmann_json monocypher)
    add_test(NAME ExternalMcpProtocolTests COMMAND ExternalMcpProtocolTests)
endif()

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(ExternalMcpOAuthTests tests/ExternalMcpOAuthTests.cpp)
    target_include_directories(ExternalMcpOAuthTests PRIVATE src/)
    target_link_libraries(ExternalMcpOAuthTests PRIVATE nlohmann_json::nlohmann_json monocypher mbedcrypto)
    add_test(NAME ExternalMcpOAuthTests COMMAND ExternalMcpOAuthTests)
endif()
if(WIN32 AND TARGET ExternalMcpOAuthTests)
    target_link_libraries(ExternalMcpOAuthTests PRIVATE bcrypt)
endif()

# Fyxa credentials must live in the OS vault, or explicitly fall back to session-only storage.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING AND NOT GB_MOBILE)
    add_executable(CredentialStoreTests tests/CredentialStoreTests.cpp src/core/CredentialStore.cpp)
    target_include_directories(CredentialStoreTests PRIVATE src/)
    if(WIN32)
        target_link_libraries(CredentialStoreTests PRIVATE Advapi32)
    elseif(APPLE)
        target_link_libraries(CredentialStoreTests PRIVATE "-framework Security" "-framework CoreFoundation")   # (CFRelease)
    elseif(UNIX AND NOT EMSCRIPTEN AND NOT GB_MOBILE)
        if(PkgConfig_FOUND AND TARGET PkgConfig::LIBSECRET)
            target_compile_definitions(CredentialStoreTests PRIVATE GB_HAVE_LIBSECRET=1)
            target_link_libraries(CredentialStoreTests PRIVATE PkgConfig::LIBSECRET)
        endif()
    endif()
    add_test(NAME CredentialStoreTests COMMAND CredentialStoreTests)
endif()

# Format-level regression fixtures for Roblox's binary mesh files. These
# tests build tiny v1-v7 payloads in memory, including a generated Draco stream,
# so no proprietary user assets are needed.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(RobloxMeshFormatTests tests/RobloxMeshFormatTests.cpp)
    target_link_libraries(RobloxMeshFormatTests PRIVATE gb_engine)
    add_test(NAME RobloxMeshFormatTests COMMAND RobloxMeshFormatTests)

    # The updater: a failed update doesn't loop, and its script really replaces the files.
    add_executable(UpdaterTests tests/UpdaterTests.cpp)
    target_link_libraries(UpdaterTests PRIVATE gb_engine)
    add_test(NAME UpdaterTests COMMAND UpdaterTests)

    # Every sample game in games/ loads and runs for a few seconds with no script errors.
    add_executable(SampleGamesTests tests/SampleGamesTests.cpp)
    target_link_libraries(SampleGamesTests PRIVATE gb_engine)
    add_test(NAME SampleGamesTests COMMAND SampleGamesTests "${CMAKE_SOURCE_DIR}/games" 5
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Graphics quality presets, and Auto stepping down when a game is laggy.
    add_executable(GraphicsQualityTests tests/GraphicsQualityTests.cpp)
    target_link_libraries(GraphicsQualityTests PRIVATE gb_engine)
    add_test(NAME GraphicsQualityTests COMMAND GraphicsQualityTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Scalability: quality groups Low..Epic, device caps and the startup benchmark's pick.
    add_executable(ScalabilityTests tests/ScalabilityTests.cpp)
    target_link_libraries(ScalabilityTests PRIVATE gb_engine)
    add_test(NAME ScalabilityTests COMMAND ScalabilityTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Your own keys (Settings > Keys): moving, swapping, AZERTY, saving.
    add_executable(KeyBindsTests tests/KeyBindsTests.cpp)
    target_link_libraries(KeyBindsTests PRIVATE gb_engine)
    add_test(NAME KeyBindsTests COMMAND KeyBindsTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The take-a-break reminder's choices (Settings > Break Reminder).
    add_executable(BreakReminderTests tests/BreakReminderTests.cpp)
    target_link_libraries(BreakReminderTests PRIVATE gb_engine)
    add_test(NAME BreakReminderTests COMMAND BreakReminderTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Outfits from scripts: HumanoidDescription, ApplyDescription, GetAppliedDescription.
    add_executable(OutfitsTests tests/OutfitsTests.cpp)
    target_link_libraries(OutfitsTests PRIVATE gb_engine)
    add_test(NAME OutfitsTests COMMAND OutfitsTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Drawing many copies of one mesh at once: which parts can be copies.
    add_executable(InstancingTests tests/InstancingTests.cpp)
    target_link_libraries(InstancingTests PRIVATE gb_engine)
    add_test(NAME InstancingTests COMMAND InstancingTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Packed material pictures: occlusion, roughness and metalness in one (OrmMap).
    add_executable(OrmPackTests tests/OrmPackTests.cpp)
    target_link_libraries(OrmPackTests PRIVATE gb_engine)
    add_test(NAME OrmPackTests COMMAND OrmPackTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Sun shadow cascades: how many maps, how wide, snapping and taking turns.
    add_executable(ShadowCascadesTests tests/ShadowCascadesTests.cpp)
    target_link_libraries(ShadowCascadesTests PRIVATE gb_engine)
    add_test(NAME ShadowCascadesTests COMMAND ShadowCascadesTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Ambient occlusion (GTAO): the settings for each level and the slice maths.
    add_executable(GtaoTests tests/GtaoTests.cpp)
    target_link_libraries(GtaoTests PRIVATE gb_engine)
    add_test(NAME GtaoTests COMMAND GtaoTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Groundwork: frame memory (no asking the system mid-frame) and what each device can do.
    add_executable(GroundworkTests tests/GroundworkTests.cpp)
    target_link_libraries(GroundworkTests PRIVATE gb_engine)
    add_test(NAME GroundworkTests COMMAND GroundworkTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Only drawing what the camera can see.
    add_executable(FrustumTests tests/FrustumTests.cpp)
    target_link_libraries(FrustumTests PRIVATE gb_engine)
    add_test(NAME FrustumTests COMMAND FrustumTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Lighting.Weather: scripts, saving, and rain stopping on roofs.
    add_executable(WeatherTests tests/WeatherTests.cpp)
    target_link_libraries(WeatherTests PRIVATE gb_engine)
    add_test(NAME WeatherTests COMMAND WeatherTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Per-player PlayerGui, RichText, team:GetPlayers, BrickColor, typeof, tree events, respawn settings.
    add_executable(CopsAndRobbersApiTests tests/CopsAndRobbersApiTests.cpp)
    target_link_libraries(CopsAndRobbersApiTests PRIVATE gb_engine)
    add_test(NAME CopsAndRobbersApiTests COMMAND CopsAndRobbersApiTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Weld / Motor6D / character joints, hinge limits, mesh stand-ins, fast bullets, the Blaster gun.
    add_executable(WeaponsAndJointsTests tests/WeaponsAndJointsTests.cpp src/editor/Premades.cpp)
    target_include_directories(WeaponsAndJointsTests PRIVATE src/)
    target_link_libraries(WeaponsAndJointsTests PRIVATE gb_engine)
    add_test(NAME WeaponsAndJointsTests COMMAND WeaponsAndJointsTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # workspace:GetPartsInPart / GetPartBoundsInBox / GetPartBoundsInRadius and OverlapParams.
    add_executable(SpatialQueriesTests tests/SpatialQueriesTests.cpp)
    target_link_libraries(SpatialQueriesTests PRIVATE gb_engine)
    add_test(NAME SpatialQueriesTests COMMAND SpatialQueriesTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Cutscenes: the camera flies through the shots in order and comes back.
    add_executable(CutsceneTests tests/CutsceneTests.cpp)
    target_link_libraries(CutsceneTests PRIVATE gb_engine)
    add_test(NAME CutsceneTests COMMAND CutsceneTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Game templates: each builds, saves, loads and plays with no script errors.
    add_executable(GameTemplatesTests tests/GameTemplatesTests.cpp src/editor/GameTemplates.cpp src/editor/LevelGen.cpp src/editor/Premades.cpp)
    target_include_directories(GameTemplatesTests PRIVATE src/)
    target_link_libraries(GameTemplatesTests PRIVATE gb_engine)
    add_test(NAME GameTemplatesTests COMMAND GameTemplatesTests 4 WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The clip recorder: keeps the last 30 seconds and writes a GIF that reads back the same.
    add_executable(ClipRecorderTests tests/ClipRecorderTests.cpp)
    target_link_libraries(ClipRecorderTests PRIVATE gb_engine)
    add_test(NAME ClipRecorderTests COMMAND ClipRecorderTests)

    # Basic anti-cheat: zooming, teleporting and flying get caught; normal play doesn't.
    add_executable(MoveGuardTests tests/MoveGuardTests.cpp)
    target_link_libraries(MoveGuardTests PRIVATE gb_engine)
    add_test(NAME MoveGuardTests COMMAND MoveGuardTests)

    # Bridging games to other sites: PlayerChatted, DisplaySystemMessage, game.GameId, HttpService Secrets.
    add_executable(GameBridgeTests tests/GameBridgeTests.cpp)
    target_link_libraries(GameBridgeTests PRIVATE gb_engine)
    add_test(NAME GameBridgeTests COMMAND GameBridgeTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Roblox chat-bridge modules (like Lyreto's Game Bridge) running as they are.
    add_executable(RobloxChatBridgeTests tests/RobloxChatBridgeTests.cpp)
    target_link_libraries(RobloxChatBridgeTests PRIVATE gb_engine)
    add_test(NAME RobloxChatBridgeTests COMMAND RobloxChatBridgeTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The character: held-down ragdolls, knocked off seats, swimming.
    add_executable(CharacterTests tests/CharacterTests.cpp)
    target_link_libraries(CharacterTests PRIVATE gb_engine)
    add_test(NAME CharacterTests COMMAND CharacterTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # A character's footing: speeds up to WalkSpeed, brakes on the ground, and
    # keeps its momentum in the air, like a Roblox Humanoid.
    add_executable(PlayerFootingTests tests/PlayerFootingTests.cpp)
    target_link_libraries(PlayerFootingTests PRIVATE gb_engine)
    add_test(NAME PlayerFootingTests COMMAND PlayerFootingTests)

    # Roblox-strength gravity: new games, and old saves scaled up when they load.
    add_executable(RobloxGravityTests tests/RobloxGravityTests.cpp)
    target_link_libraries(RobloxGravityTests PRIVATE gb_engine)
    add_test(NAME RobloxGravityTests COMMAND RobloxGravityTests)

    # The Material Graph: patterns tile, wires can't loop, saving and baking.
    add_executable(MaterialGraphTests tests/MaterialGraphTests.cpp)
    target_link_libraries(MaterialGraphTests PRIVATE gb_engine)
    add_test(NAME MaterialGraphTests COMMAND MaterialGraphTests)

    # MaterialVariants to Roblox files and back.
    add_executable(MaterialVariantRobloxTests tests/MaterialVariantRobloxTests.cpp)
    target_link_libraries(MaterialVariantRobloxTests PRIVATE gb_engine)
    add_test(NAME MaterialVariantRobloxTests COMMAND MaterialVariantRobloxTests)

    # Linked models: one copy changes, the others follow.
    add_executable(LinkedModelsTests tests/LinkedModelsTests.cpp)
    target_link_libraries(LinkedModelsTests PRIVATE gb_engine)
    add_test(NAME LinkedModelsTests COMMAND LinkedModelsTests)

    # File > Export As: each 3D model type is written, read back, and still matches.
    add_executable(ModelExportTests tests/ModelExportTests.cpp src/editor/ModelExport.cpp src/editor/ModelImport.cpp)
    target_include_directories(ModelExportTests PRIVATE src/)
    target_link_libraries(ModelExportTests PRIVATE gb_engine gb_models)
    add_test(NAME ModelExportTests COMMAND ModelExportTests)

    # Vehicles: a VehicleSeat drives a car's motor wheels and pushes a boat.
    add_executable(VehiclesTests tests/VehiclesTests.cpp)
    target_link_libraries(VehiclesTests PRIVATE gb_engine)
    add_test(NAME VehiclesTests COMMAND VehiclesTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # DragDetectors: grab a part with the pointer and drag it (plane, line, spin, throw).
    add_executable(DragDetectorTests tests/DragDetectorTests.cpp)
    target_link_libraries(DragDetectorTests PRIVATE gb_engine)
    add_test(NAME DragDetectorTests COMMAND DragDetectorTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # IKControl: the normal character's arms reach for things and its head looks at them.
    add_executable(IkControlTests tests/IkControlTests.cpp)
    target_link_libraries(IkControlTests PRIVATE gb_engine)
    add_test(NAME IkControlTests COMMAND IkControlTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Loading big maps in pieces (StreamingEnabled): which pieces a joined player gets.
    add_executable(StreamingTests tests/StreamingTests.cpp)
    target_link_libraries(StreamingTests PRIVATE gb_engine)
    add_test(NAME StreamingTests COMMAND StreamingTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Network ownership: a joined player's car moves on their computer; the host waits.
    add_executable(OwnershipTests tests/OwnershipTests.cpp)
    target_link_libraries(OwnershipTests PRIVATE gb_engine)
    add_test(NAME OwnershipTests COMMAND OwnershipTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The auto UV map for bundle body parts: clothing templates fit any shape.
    add_executable(ClothingUVTests tests/ClothingUVTests.cpp)
    target_link_libraries(ClothingUVTests PRIVATE gb_engine)
    add_test(NAME ClothingUVTests COMMAND ClothingUVTests)

    # Jiggle, LookAt and IK bones, saving them, scripts, and the weight brush.
    # Bones in bundles, jiggle and cloth, gore upgrades, breaking things, a faster
    # broadphase, lamp shadows, bouncy light and lip sync.
    add_executable(RealismTests tests/RealismTests.cpp)
    target_link_libraries(RealismTests PRIVATE gb_engine)
    add_test(NAME RealismTests COMMAND RealismTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene")
    # game.ServerLowMemoryWarning and Stats' memory numbers (src/scripting/ServerMemory.cpp).
    add_executable(ServerMemoryTests tests/ServerMemoryTests.cpp)
    target_link_libraries(ServerMemoryTests PRIVATE gb_engine)
    add_test(NAME ServerMemoryTests COMMAND ServerMemoryTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene")
    add_executable(BodiesGoreLightTests tests/BodiesGoreLightTests.cpp)
    target_link_libraries(BodiesGoreLightTests PRIVATE gb_engine)
    add_test(NAME BodiesGoreLightTests COMMAND BodiesGoreLightTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
    # Shiny parts mirroring the world, height fog, round cylinders that roll, air and wind.
    add_executable(MirrorsMistRollingTests tests/MirrorsMistRollingTests.cpp)
    target_link_libraries(MirrorsMistRollingTests PRIVATE gb_engine)
    add_test(NAME MirrorsMistRollingTests COMMAND MirrorsMistRollingTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    add_executable(BoneRigTests tests/BoneRigTests.cpp)
    target_link_libraries(BoneRigTests PRIVATE gb_engine)
    add_test(NAME BoneRigTests COMMAND BoneRigTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Roblox animations (KeyframeSequence) fitted to our characters: R6 and R15, facing and size.
    add_executable(RobloxAnimTests tests/RobloxAnimTests.cpp)
    target_link_libraries(RobloxAnimTests PRIVATE gb_engine)
    add_test(NAME RobloxAnimTests COMMAND RobloxAnimTests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Script help (the autocomplete's "how to use it"), wrong-case names, mouse button binds.
    add_executable(ScriptingHelpTests tests/ScriptingHelpTests.cpp)
    target_link_libraries(ScriptingHelpTests PRIVATE gb_engine)
    add_test(NAME ScriptingHelpTests COMMAND ScriptingHelpTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # OrderedDataStore:GetSortedAsync from a script, played offline.
    add_executable(OrderedDataStoreTests tests/OrderedDataStoreTests.cpp)
    target_link_libraries(OrderedDataStoreTests PRIVATE gb_engine)
    add_test(NAME OrderedDataStoreTests COMMAND OrderedDataStoreTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The Script editor's wavy lines: script mistakes found without running anything.
    add_executable(ScriptCheckTests tests/ScriptCheckTests.cpp)
    target_link_libraries(ScriptCheckTests PRIVATE gb_engine)
    add_test(NAME ScriptCheckTests COMMAND ScriptCheckTests "${CMAKE_SOURCE_DIR}/games" WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # VideoFrame: videos on screens and parts.
    add_executable(VideoFrameTests tests/VideoFrameTests.cpp)
    target_link_libraries(VideoFrameTests PRIVATE gb_engine)
    add_test(NAME VideoFrameTests COMMAND VideoFrameTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             "${CMAKE_SOURCE_DIR}/tests/media/red-then-blue.mpg" WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The Live Profiler: times the parts of a frame and each script, only while it's open.
    add_executable(LiveProfilerTests tests/LiveProfilerTests.cpp)
    target_link_libraries(LiveProfilerTests PRIVATE gb_engine)
    add_test(NAME LiveProfilerTests COMMAND LiveProfilerTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # MemoryStoreService: the store's rules and the script side, played offline.
    add_executable(MemoryStoreTests tests/MemoryStoreTests.cpp)
    target_link_libraries(MemoryStoreTests PRIVATE gb_engine)
    add_test(NAME MemoryStoreTests COMMAND MemoryStoreTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Detail levels for far meshes (MeshLod) and thunderstorms (Lighting.Weather = "Storm").
    add_executable(StormAndDetailTests tests/StormAndDetailTests.cpp)
    target_link_libraries(StormAndDetailTests PRIVATE gb_engine)
    add_test(NAME StormAndDetailTests COMMAND StormAndDetailTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
