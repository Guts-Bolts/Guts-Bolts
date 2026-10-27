cmake_minimum_required(VERSION 3.20)
project(GutsAndBolts VERSION 0.7.6 LANGUAGES C CXX)

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
        GIT_TAG        4.0.3
        GIT_SHALLOW    TRUE
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
        target_link_libraries(GutsAndBoltsServer PRIVATE ws2_32 bcrypt)
    endif()
    return()
endif()

# GB_MOBILE builds the phone / tablet version of the Player (SDL2 + OpenGL ES 3,
# touch controls). It's on automatically for Android; turning it on for Linux
# lets you try the phone version on a computer.
option(GB_MOBILE "Build the phone/tablet Player (SDL2 + OpenGL ES 3)" OFF)
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(GB_MOBILE ON)
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
    GIT_TAG        docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ImGuizmo (move/rotate/scale gizmos drawn over the ImGui viewport).
# SOURCE_SUBDIR points at a non-existent dir so MakeAvailable only downloads the
# sources — it does NOT run ImGuizmo's own CMakeLists (which expects vcpkg).
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(imguizmo)

# Lua 5.4 — the scripting language used by Script objects. The official repo
# has no CMake build, so only download it and compile the core library below.
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE
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
    GIT_TAG        0.11.25
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(miniaudio)

# Opus — squeezes voice chat down small enough to send over the internet.
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    opus
    GIT_REPOSITORY https://github.com/xiph/opus.git
    GIT_TAG        v1.5.2
    GIT_SHALLOW    TRUE
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
    GIT_TAG        4.0.3
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(monocypher)
add_library(monocypher STATIC ${monocypher_SOURCE_DIR}/src/monocypher.c)
target_include_directories(monocypher PUBLIC ${monocypher_SOURCE_DIR}/src)

# stb_image: reading PNG / JPG pictures (decals).
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(stb)

# portable-file-dialogs: the system's own "Open file" window (Browse... buttons).
FetchContent_Declare(
    pfd
    GIT_REPOSITORY https://github.com/samhocevar/portable-file-dialogs.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
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
        GIT_TAG        1.0.1
        GIT_SHALLOW    TRUE
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
# Recorded sound effects (the rest are made in code, see Audio.cpp).
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/jump.wav"    kSoundJump    "${GB_GENERATED}/SoundJump.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/respawn.wav" kSoundRespawn "${GB_GENERATED}/SoundRespawn.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/spawn.wav"   kSoundSpawn   "${GB_GENERATED}/SoundSpawn.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/footsteps.mp3" kSoundFootsteps "${GB_GENERATED}/SoundFootsteps.h")
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
FetchContent_Declare(
    draco
    GIT_REPOSITORY https://github.com/google/draco.git
    GIT_TAG        1.5.7
    GIT_SHALLOW    TRUE
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
    src/renderer/Textures.cpp
    src/renderer/Camera.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/ShadowMap.cpp
    src/renderer/SceneRenderer.cpp
    src/renderer/ViewportRender.cpp
    src/scene/SceneNode.cpp
    src/scene/Scene.cpp
    src/scene/Player.cpp
    src/scene/PlayerVrBody.cpp
    src/scene/Emotes.cpp
    src/scene/Physics.cpp
    src/scene/NavMesh.cpp
    src/scene/Npc.cpp
    src/scene/Water.cpp
    src/scene/Terrain.cpp
    src/scene/Liquid.cpp
    src/scene/Flood.cpp
    src/scene/RigidBodies.cpp
    src/scene/Particles.cpp
    src/scene/Weather.cpp
    src/scene/LinkedModels.cpp
    src/scene/Animation.cpp
    src/scene/Ragdoll.cpp
    src/scene/Effects.cpp
    src/scene/SoundMix.cpp
    src/scene/Blast.cpp
    src/scene/Serializer.cpp
    src/scene/RobloxFile.cpp
    src/scene/RobloxMesh.cpp
    src/scene/EditMesh.cpp
    src/scene/MeshTools.cpp
    src/scene/Skin.cpp
    src/scene/MeshUV.cpp
    src/scene/Csg.cpp
    src/scripting/Luau.cpp
    src/scripting/ScriptEngine.cpp
    src/scripting/ScriptHistory.cpp
    src/scripting/LuaTypes.cpp
    src/scripting/LuaInstance.cpp
    src/scripting/ScriptRemotes.cpp
    src/scripting/ScriptActions.cpp
    src/game/GameSession.cpp
    src/game/Hud.cpp
    src/game/EmoteMenu.cpp
    src/game/GameGui.cpp
    src/game/Prompts.cpp
    src/game/ClickDetectors.cpp
    src/game/PlayCamera.cpp
    src/game/SettingsWindow.cpp
    src/game/Profile.cpp
    src/game/UpdateToast.cpp
    src/game/Badges.cpp
    src/game/Bolts.cpp
    src/online/Protocol.cpp
    src/online/MemoryStore.cpp
    src/online/OnlineClient.cpp
    src/online/AssetCache.cpp
    src/game/Catalog.cpp
    src/game/TouchControls.cpp
    src/net/Socket.cpp
    src/net/NetGame.cpp
    src/net/ChatTranslate.cpp
    src/editor/Theme.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
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

target_include_directories(gb_engine PUBLIC src/ ${miniaudio_SOURCE_DIR} ${GB_GENERATED} ${stb_SOURCE_DIR} ${pfd_SOURCE_DIR} ${GB_DRACO_INCLUDE_DIR} ${CMAKE_BINARY_DIR})
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
        GIT_TAG        release-1.1.54
        GIT_SHALLOW    TRUE
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
target_compile_definitions(gb_engine PUBLIC GLM_ENABLE_EXPERIMENTAL)
if(WIN32)
    target_link_libraries(gb_engine PUBLIC dbghelp ws2_32 bcrypt)
endif()

if(GB_MOBILE)
    # Phones and tablets get the Player only. On Android it's a library that
    # SDL's Java activity loads (it must be called "main").
    set(PLAYER_SOURCES src/player/main.cpp src/player/PlayerApp.cpp src/player/PlayerOnline.cpp src/player/PlayerSocial.cpp src/player/PlayerForum.cpp src/player/ItemRenders.cpp src/player/PlayerMail.cpp src/player/PlayerLibrary.cpp src/player/PlayerDevConsole.cpp src/player/PlayerPasses.cpp src/player/PlayerFriends.cpp src/player/PlayerLogin.cpp src/player/PlayerSafety.cpp src/player/PlayerVr.cpp src/player/LaunchLink.cpp)
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
    GIT_TAG        v0.23.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(ufbx)
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG        v1.15
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(cgltf)
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
    GIT_TAG        v0.22
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  cmake-build-disabled
)
FetchContent_MakeAvailable(meshoptimizer)

# Assimp: every other 3D format worth having (.dae, .3ds, .x, .3mf, .lwo, .ac,
# .off, .dxf, ...). Only its readers are built, not its writers or tools.
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
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
    GIT_TAG        v5.4.3
    GIT_SHALLOW    TRUE
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
    GIT_TAG        v1.4.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libwebp)
FetchContent_Declare(
    nanosvg
    GIT_REPOSITORY https://github.com/memononen/nanosvg.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
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
    src/editor/EditorImport.cpp
    src/editor/PictureImport.cpp
    src/editor/AiTools.cpp
    src/editor/AiToolsRun.cpp
    src/editor/StudioMcp.cpp
    src/editor/panels/AssistantPanel.cpp
    src/editor/EditorRibbon.cpp
    src/editor/Icons.cpp
    src/editor/panels/ViewportModeling.cpp
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
    src/editor/EditorAiActivity.cpp
    src/editor/EditorLevelGen.cpp
    src/editor/LevelGen.cpp
    src/editor/EditorBulkEdit.cpp
    src/editor/EditorBlocks.cpp
    src/editor/VisualScript.cpp
    src/editor/EditorScriptHistory.cpp
    src/editor/EditorPhysicsReplay.cpp
    src/editor/PhysicsReplay.cpp
    src/editor/EditorCompare.cpp
    src/editor/SceneDiff.cpp
    src/editor/EditorOnline.cpp
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
target_link_libraries(GutsAndBolts PRIVATE gb_engine gb_models)

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
    target_link_libraries(GutsAndBoltsServer PRIVATE ws2_32 bcrypt)
endif()

# Studio's sample plugins.
add_custom_command(TARGET GutsAndBolts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:GutsAndBolts>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/plugins"
            "$<TARGET_FILE_DIR:GutsAndBolts>/plugins"
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


# Format-level regression fixtures for Roblox's binary mesh files. These
# tests build tiny v1-v7 payloads in memory, including a generated Draco stream,
# so no proprietary user assets are needed.
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(RobloxMeshFormatTests tests/RobloxMeshFormatTests.cpp)
    target_link_libraries(RobloxMeshFormatTests PRIVATE gb_engine)
    add_test(NAME RobloxMeshFormatTests COMMAND RobloxMeshFormatTests)

    # Every sample game in games/ loads and runs for a few seconds with no script errors.
    add_executable(SampleGamesTests tests/SampleGamesTests.cpp)
    target_link_libraries(SampleGamesTests PRIVATE gb_engine)
    add_test(NAME SampleGamesTests COMMAND SampleGamesTests "${CMAKE_SOURCE_DIR}/games" 5
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # Lighting.Weather: scripts, saving, and rain stopping on roofs.
    add_executable(WeatherTests tests/WeatherTests.cpp)
    target_link_libraries(WeatherTests PRIVATE gb_engine)
    add_test(NAME WeatherTests COMMAND WeatherTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # The character: held-down ragdolls, knocked off seats, swimming.
    add_executable(CharacterTests tests/CharacterTests.cpp)
    target_link_libraries(CharacterTests PRIVATE gb_engine)
    add_test(NAME CharacterTests COMMAND CharacterTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

    # MaterialVariants to Roblox files and back.
    add_executable(MaterialVariantRobloxTests tests/MaterialVariantRobloxTests.cpp)
    target_link_libraries(MaterialVariantRobloxTests PRIVATE gb_engine)
    add_test(NAME MaterialVariantRobloxTests COMMAND MaterialVariantRobloxTests)

    # Linked models: one copy changes, the others follow.
    add_executable(LinkedModelsTests tests/LinkedModelsTests.cpp)
    target_link_libraries(LinkedModelsTests PRIVATE gb_engine)
    add_test(NAME LinkedModelsTests COMMAND LinkedModelsTests)

    # MemoryStoreService: the store's rules and the script side, played offline.
    add_executable(MemoryStoreTests tests/MemoryStoreTests.cpp)
    target_link_libraries(MemoryStoreTests PRIVATE gb_engine)
    add_test(NAME MemoryStoreTests COMMAND MemoryStoreTests "${CMAKE_SOURCE_DIR}/games/Obby of Doom.gbscene"
             WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
