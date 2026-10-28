#include "PlayerApp.h"
#include "LaunchLink.h"
#ifdef GB_MOBILE
#include <SDL.h>   // on Android, SDL starts the app through this main()
#endif
#include "../core/CrashHandler.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

// Guts&BoltsPlayer — the platform app.
//   GutsAndBoltsPlayer                 open the home screen
//   GutsAndBoltsPlayer MyGame.gbscene  jump straight into a game
int main(int argc, char** argv) {
    CrashHandler::install();
    LaunchLink::listen();   // Mac: links arrive as messages (start listening before the window opens)

    PlayerOptions opts;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if      (!std::strcmp(argv[i], "--page"))       opts.page       = next();
        else if (!std::strcmp(argv[i], "--screenshot")) opts.screenshot = next();
        else if (!std::strcmp(argv[i], "--frames"))     opts.frames     = std::atoi(next());
        else if (!std::strcmp(argv[i], "--hold"))       opts.holdKey    = next();
        else if (!std::strcmp(argv[i], "--join"))       opts.join       = next();
        else if (!std::strcmp(argv[i], "--host"))       opts.host       = true;
        else if (!std::strcmp(argv[i], "--online-play"))    opts.onlinePlay = true;
        else if (!std::strcmp(argv[i], "--online-game"))    opts.onlineGame = next();
        else if (!std::strcmp(argv[i], "--private-server")) opts.privateServer = true;
        else if (!std::strcmp(argv[i], "--join-code"))      opts.joinCode = next();
        else if (!std::strcmp(argv[i], "--test-signup"))    opts.testSignup = next();
        else if (!std::strcmp(argv[i], "--test-login"))     opts.testLogin = next();
        else if (!std::strcmp(argv[i], "--test-rename"))    opts.testRename = next();
        else if (!std::strcmp(argv[i], "--test-op"))        opts.testOps.push_back(next());
        else if (!std::strcmp(argv[i], "--test-publish"))   opts.testPublish = next();
        else if (!std::strcmp(argv[i], "--create-tab"))     opts.createTab = std::atoi(next());
        else if (!std::strcmp(argv[i], "--test-tools"))     opts.testTools = next();
        else if (!std::strcmp(argv[i], "--camera-yaw"))     opts.cameraYaw = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--say"))        opts.say        = next();
        else if (!std::strcmp(argv[i], "--dev-command")) opts.devCommand = next();
        else if (!std::strcmp(argv[i], "--guest"))      opts.guest      = true;
        else if (!std::strcmp(argv[i], "--test-click")) opts.testClick  = next();
        else if (!std::strcmp(argv[i], "--test-type"))  opts.testType   = next();
        else if (!std::strcmp(argv[i], "--test-wheel")) opts.testWheel  = (float)std::atof(next());
        else if (!std::strcmp(argv[i], "--create-staff-account")) opts.createStaff = true;
        else if (!std::strcmp(argv[i], "--touch-test")) opts.touchTest  = next();
        else if (!std::strcmp(argv[i], "--pad-test"))   opts.padTest    = next();
        else if (!std::strcmp(argv[i], "--test-make-items")) opts.testItems = true;
        else if (!std::strcmp(argv[i], "--test-grant"))  opts.testGrantFor = next();
        else if (!std::strcmp(argv[i], "--test-redeem")) opts.testRedeem = next();
        else if (!std::strcmp(argv[i], "--test-bolts-code")) opts.testBoltsFor = next();
        else if (!std::strcmp(argv[i], "--test-redeem-bolts")) opts.testRedeemBolts = next();
        else if (!std::strcmp(argv[i], "--test-buy")) opts.testBuy = next();
        else if (!std::strcmp(argv[i], "--online-test")) opts.onlineTest = next();
        else if (!std::strcmp(argv[i], "--test-clothes")) opts.testClothes = next();
        else if (!std::strcmp(argv[i], "--test-accessory")) opts.testAccessory = next();
        else if (!std::strcmp(argv[i], "--test-face")) opts.testFace = next();
        else if (!std::strcmp(argv[i], "--studio-test")) opts.studioTest = next();   // Studio's "Test with N players"
        else if (!std::strcmp(argv[i], "--studio-input-trace")) opts.studioInputTrace = next(); // Studio's per-player input trace
        else if (!std::strcmp(argv[i], "--vr"))          opts.vr = true;   // straight into the VR headset
        else if (!std::strcmp(argv[i], "--vr-test-hands")) opts.vrTestHands = next();
        else if (!std::strcmp(argv[i], "--name"))        opts.testName = next();
        else if (!std::strncmp(argv[i], "gutsandbolts:", 13)) opts.launchUrl = argv[i];   // the website's Play button
        else if (argv[i][0] != '-')                     opts.game       = argv[i];
    }

    try {
#ifdef __EMSCRIPTEN__
        // In a browser run() hands the frames to the page and main() ends early, so the
        // app can't live on main()'s stack: it stays alive until the tab closes.
        (new PlayerApp(opts))->run();
#else
        PlayerApp app(opts);
        app.run();
#endif
    } catch (const std::exception& e) {
#ifdef __EMSCRIPTEN__
        showWebError(e.what());
#else
        std::cerr << "Fatal: " << e.what() << "\n";
#endif
        return 1;
    }
    return 0;
}
