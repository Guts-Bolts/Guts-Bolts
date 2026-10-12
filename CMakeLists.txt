cmake_minimum_required(VERSION 3.20)
project(GutsAndBolts VERSION 0.7.1 LANGUAGES C CXX)

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
    src/server/ServerFriends.cpp
    src/server/ServerSocial.cpp
    src/server/ServerAccounts.cpp
    src/server/ServerRelay.cpp
    src/server/ServerData.cpp
    src/server/ServerSafety.cpp
    src/server/ServerForum.cpp
    src/server/ServerItems.cpp
    src/online/Protocol.cpp
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
if(ANDROID)
    set(GB_MOBILE ON)
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

file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SOURCES
    "${lua_SOURCE_DIR}/lua.c"       # stand-alone interpreter
    "${lua_SOURCE_DIR}/luac.c"      # stand-alone compiler
    "${lua_SOURCE_DIR}/onelua.c"    # amalgamation (would duplicate symbols)
    "${lua_SOURCE_DIR}/ltests.c")   # internal test hooks
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES C_STANDARD 99)
if(NOT WIN32)
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
add_library(gb_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
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
gb_embed("${CMAKE_SOURCE_DIR}/assets/templates/shirt_template.png" kShirtTemplate "${GB_GENERATED}/ShirtTemplate.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/templates/pants_template.png" kPantsTemplate "${GB_GENERATED}/PantsTemplate.h")
# Recorded sound effects (the rest are made in code, see Audio.cpp).
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/jump.wav"    kSoundJump    "${GB_GENERATED}/SoundJump.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/respawn.wav" kSoundRespawn "${GB_GENERATED}/SoundRespawn.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/spawn.wav"   kSoundSpawn   "${GB_GENERATED}/SoundSpawn.h")
gb_embed("${CMAKE_SOURCE_DIR}/assets/sounds/footsteps.mp3" kSoundFootsteps "${GB_GENERATED}/SoundFootsteps.h")
add_custom_target(gb_version ALL
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${GB_GENERATED}
            -DVERSION=${PROJECT_VERSION} -P ${CMAKE_SOURCE_DIR}/cmake/GenVersion.cmake
    BYPRODUCTS "${GB_GENERATED}/Version.h"
    COMMENT "Checking version")

# ---------------------------------------------------------------------------
# The engine: scene, physics, scripting, rendering, game session.
# Shared by the editor (GutsAndBolts) and the player app (GutsAndBoltsPlayer).
# ---------------------------------------------------------------------------
add_library(gb_engine STATIC
    src/core/CrashHandler.cpp
    src/core/Log.cpp
    src/core/Settings.cpp
    src/core/Pad.cpp
    src/core/Paths.cpp
    src/core/FileDialog.cpp
    src/core/Audio.cpp
    src/core/Voice.cpp
    src/core/UpdateChecker.cpp
    src/core/Account.cpp
    src/core/TextFilter.cpp
    src/scene/ObjImport.cpp
    src/scene/PlayerModel.cpp
    src/renderer/Shader.cpp
    src/renderer/Mesh.cpp
    src/renderer/Textures.cpp
    src/renderer/Camera.cpp
    src/renderer/Framebuffer.cpp
    src/renderer/ShadowMap.cpp
    src/renderer/SceneRenderer.cpp
    src/scene/SceneNode.cpp
    src/scene/Scene.cpp
    src/scene/Player.cpp
    src/scene/Physics.cpp
    src/scene/NavMesh.cpp
    src/scene/Npc.cpp
    src/scene/Water.cpp
    src/scene/Terrain.cpp
    src/scene/Liquid.cpp
    src/scene/LiquidGpu.cpp
    src/scene/Flood.cpp
    src/scene/RigidBodies.cpp
    src/scene/Particles.cpp
    src/scene/Animation.cpp
    src/scene/Ragdoll.cpp
    src/scene/Effects.cpp
    src/scene/Blast.cpp
    src/scene/Serializer.cpp
    src/scene/RobloxFile.cpp
    src/scene/RobloxMesh.cpp
    src/scene/EditMesh.cpp
    src/scene/Csg.cpp
    src/scripting/Luau.cpp
    src/scripting/ScriptEngine.cpp
    src/scripting/LuaTypes.cpp
    src/scripting/LuaInstance.cpp
    src/scripting/ScriptRemotes.cpp
    src/game/GameSession.cpp
    src/game/Hud.cpp
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
    src/online/OnlineClient.cpp
    src/online/AssetCache.cpp
    src/game/Catalog.cpp
    src/game/TouchControls.cpp
    src/net/Socket.cpp
    src/net/NetGame.cpp
    src/editor/Theme.cpp
)
if(GB_MOBILE)
    target_sources(gb_engine PRIVATE src/core/AppWindowSDL.cpp)
    target_compile_definitions(gb_engine PUBLIC GB_MOBILE GB_GLES)
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

target_include_directories(gb_engine PUBLIC src/ ${miniaudio_SOURCE_DIR} ${GB_GENERATED} ${stb_SOURCE_DIR} ${pfd_SOURCE_DIR})
add_dependencies(gb_engine gb_version)
find_package(Threads REQUIRED)
target_link_libraries(gb_engine PUBLIC Threads::Threads ${CMAKE_DL_LIBS} opus)
if(APPLE)
    target_link_libraries(gb_engine PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()
target_link_libraries(gb_engine PUBLIC
    gb_imgui
    ${GB_GLM}
    lua_static
    nlohmann_json::nlohmann_json
    monocypher
    gb_zstd
    mbedtls mbedx509 mbedcrypto
)
if(NOT GB_MOBILE)
    target_link_libraries(gb_engine PUBLIC glfw GLEW::GLEW OpenGL::GL)
endif()
target_compile_definitions(gb_engine PUBLIC GLM_ENABLE_EXPERIMENTAL)
if(WIN32)
    target_link_libraries(gb_engine PUBLIC dbghelp ws2_32 bcrypt)
endif()

if(GB_MOBILE)
    # Phones and tablets get the Player only. On Android it's a library that
    # SDL's Java activity loads (it must be called "main").
    set(PLAYER_SOURCES src/player/main.cpp src/player/PlayerApp.cpp src/player/PlayerOnline.cpp src/player/PlayerSocial.cpp src/player/PlayerForum.cpp src/player/ItemRenders.cpp src/player/PlayerMail.cpp src/player/PlayerLibrary.cpp src/player/PlayerDevConsole.cpp src/player/PlayerPasses.cpp src/player/PlayerFriends.cpp src/player/PlayerLogin.cpp src/player/PlayerSafety.cpp src/player/LaunchLink.cpp)
    if(ANDROID)
        add_library(main SHARED ${PLAYER_SOURCES})
        target_link_libraries(main PRIVATE gb_engine)
    else()
        add_executable(GutsAndBoltsPlayer ${PLAYER_SOURCES})
        target_link_libraries(GutsAndBoltsPlayer PRIVATE gb_engine)
        add_custom_command(TARGET GutsAndBoltsPlayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/games"
                    "$<TARGET_FILE_DIR:GutsAndBoltsPlayer>/games"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/catalog"
                    "$<TARGET_FILE_DIR:GutsAndBoltsPlayer>/catalog"
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
add_library(gb_models STATIC ${ufbx_SOURCE_DIR}/ufbx.c)
target_include_directories(gb_models PUBLIC ${ufbx_SOURCE_DIR} ${cgltf_SOURCE_DIR})

set(EDITOR_SOURCES
    src/main.cpp
    src/Application.cpp
    src/editor/Editor.cpp
    src/editor/ModelImport.cpp
    src/editor/EditorImport.cpp
    src/editor/AiTools.cpp
    src/editor/AiToolsRun.cpp
    src/editor/StudioMcp.cpp
    src/editor/panels/AssistantPanel.cpp
    src/editor/EditorRibbon.cpp
    src/editor/Icons.cpp
    src/editor/panels/ViewportModeling.cpp
    src/editor/panels/ViewportTerrain.cpp
    src/editor/EditorModes.cpp
    src/editor/EditorSolid.cpp
    src/editor/EditorAutosave.cpp
    src/editor/EditorOnline.cpp
    src/editor/Thumbnailer.cpp
    src/editor/Plugins.cpp
    src/editor/Premades.cpp
    src/editor/TeamCreate.cpp
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
