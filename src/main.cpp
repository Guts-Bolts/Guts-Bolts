#include "Application.h"
#include "core/CrashHandler.h"
#include "editor/StudioMcp.h"
#include "scripting/ScriptEngine.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    CrashHandler::install();

    LaunchOptions opts;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--open"))       opts.openFile   = next();
        else if (!std::strcmp(argv[i], "--play"))       opts.play       = true;
        else if (!std::strcmp(argv[i], "--screenshot")) opts.screenshot = next();
        else if (!std::strcmp(argv[i], "--frames"))     opts.frames     = std::atoi(next());
        else if (!std::strcmp(argv[i], "--hold"))       opts.holdKey    = next();
        else if (!std::strcmp(argv[i], "--team-host"))  opts.teamHost   = true;
        else if (!std::strcmp(argv[i], "--team-join"))  opts.teamJoin   = next();
        else if (!std::strcmp(argv[i], "--test-add-part")) opts.testAddPart = next();
        else if (!std::strcmp(argv[i], "--test-premades")) opts.testPremades = next();
        else if (!std::strcmp(argv[i], "--test-tools")) opts.testTools = next();
        else if (!std::strcmp(argv[i], "--test-analysis")) opts.testAnalysis = true;
        else if (!std::strcmp(argv[i], "--test-fix-pictures")) opts.testFixPictures = true;
        else if (!std::strcmp(argv[i], "--test-keys"))  opts.testKeys   = next();
        else if (!std::strcmp(argv[i], "--test-select")) opts.testSelect = next();
        else if (!std::strcmp(argv[i], "--test-mesh")) opts.testMesh = next();
        else if (!std::strcmp(argv[i], "--test-anim")) opts.testAnim = true;
        else if (!std::strcmp(argv[i], "--test-collide")) opts.testCollide = true;
        else if (!std::strcmp(argv[i], "--test-mouse")) opts.testMouse = next();
        else if (!std::strcmp(argv[i], "--test-command")) opts.testCommand = next();
        else if (!std::strcmp(argv[i], "--test-insert")) opts.testInsert = next();
        else if (!std::strcmp(argv[i], "--export-roblox")) opts.exportRoblox = next();
        else if (!std::strcmp(argv[i], "--test-snapshot")) opts.testSnapshot = next();
        else if (!std::strcmp(argv[i], "--test-drop")) opts.testDrop = next();
        else if (!std::strcmp(argv[i], "--test-multiplayer")) opts.testMultiplayer = next();
        else if (!std::strcmp(argv[i], "--emote-pictures")) opts.emotePictures = next();
        else if (!std::strcmp(argv[i], "--mcp")) {
            // An AI app started us as its MCP server: pass messages on to the Studio that's open (no window).
            int port = i + 1 < argc && argv[i + 1][0] != '-' ? std::atoi(argv[i + 1]) : StudioMcp::kDefaultPort;
            return StudioMcp::runStdioBridge(port);
        }
        else if (argv[i][0] != '-')                     opts.openFile   = argv[i];  // double-clicked file
    }

    // Tests drive pretend input from Lua (__gb_testInput), and only tests.
    ScriptEngine::s_testHooks = !opts.testCommand.empty();

    try {
        Application app(opts);
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
