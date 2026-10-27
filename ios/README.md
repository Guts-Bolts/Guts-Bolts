# Guts & Bolts iOS Player

The iOS target reuses the shared C++/SDL2 mobile Player. The target is an iPhone/iPad app bundle (iOS 15+) and is intended to become a real game client, not a separate engine implementation.

## Current implementation

- CMake creates `GutsAndBoltsPlayer.app` and declares the `gutsandbolts://` URL scheme.
- The build packages `games/`, `catalog/`, and `assets/` in the app's `Resources` directory.
- On first launch and after a version change, those resource trees are copied into the app's writable sandbox. A failed copy is not stamped as complete, so the next launch can retry.
- Runtime URL events received as SDL drop-file events are queued into the Player's existing launch-link parser. Link IDs and query values still pass through `LaunchLink::parse` validation.
- The SDL mobile window handles background/foreground events and mutes audio while backgrounded. Shared touch controls already provide a virtual stick, camera gestures, and gameplay buttons.
- The iOS target uses the existing OpenGL ES 3.0 renderer and CPU liquid-solver fallback.
- The app fills the whole screen with the status bar hidden. `src/core/IosPlatform.mm` reads UIKit's safe-area insets and hands them to ImGui as the main viewport's work-area insets, so the site, menus and touch controls stay clear of the notch, rounded corners and home bar.
- Games lock the app to landscape (`AppWindow::lockLandscape` sets SDL's orientation hint and asks UIKit to turn now on iOS 16+; iOS 15 turns when the phone is tipped). The site allows portrait and landscape again. `UIRequiresFullScreen` is set so iPad honours the lock.
- The home-screen icon comes from `ios/Assets.xcassets` (one 1024x1024 icon, made from `Icon.png` on a dark background, since iOS icons can't be see-through). The bundle version comes from the CMake project version.

This is still a porting build, not a claim of release readiness. CI compilation cannot verify real UIKit URL delivery, device-specific rendering, microphone permissions, voice chat, or multiplayer reliability.

## Build on macOS

Install Xcode and CMake, then build the simulator app:

```sh
cmake -S . -B build-ios-sim -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ios-sim --config Release --target GutsAndBoltsPlayer
```

For an iPhone/iPad device build (compile only; signing is configured separately in Xcode):

```sh
cmake -S . -B build-ios-device -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphoneos \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ios-device --config Release --target GutsAndBoltsPlayer
```

Use Xcode to select a development team and provisioning profile before installing on a physical device. Never commit signing credentials.

## Put it on your own iPhone for free

You don't need the paid Apple Developer Program ($99 a year) to play on your own phone. A normal, free Apple ID works. You do need a Mac.

The catches with a free Apple ID: the app stops opening after **7 days** (plug the phone in and press Run again to fix it), you can have at most 3 apps like this on one phone, and you can't send the app to friends. Each person installs it from their own Mac.

1. On the Mac, install **Xcode** from the App Store (free), then open it once so it finishes setting up. Install CMake too (`brew install cmake`, or the download from cmake.org).
2. In Xcode, open **Xcode > Settings > Accounts**, press **+** and sign in with your Apple ID.
3. Download the code (`git clone https://github.com/Guts-Bolts/Guts-Bolts.git`) and, in that folder, make the Xcode project. Swap `yourname` for anything that's yours, because the app id has to be one nobody else has used:

   ```sh
   cmake -S . -B build-ios-device -G Xcode \
     -DCMAKE_SYSTEM_NAME=iOS \
     -DCMAKE_OSX_SYSROOT=iphoneos \
     -DCMAKE_OSX_ARCHITECTURES=arm64 \
     -DGB_IOS_BUNDLE_ID=com.yourname.gutsandbolts
   open build-ios-device/GutsAndBolts.xcodeproj
   ```

4. In Xcode, click **GutsAndBoltsPlayer** in the list on the left, then the **Signing & Capabilities** tab. Tick **Automatically manage signing** and pick **Your Name (Personal Team)** as the Team. (If you know your Team ID you can pass `-DGB_IOS_TEAM=ABCDE12345` in step 3 instead.)
5. Plug the iPhone into the Mac with a cable and tap **Trust** on the phone. On iOS 16 or newer, turn on **Settings > Privacy & Security > Developer Mode** (the phone restarts).
6. At the top of Xcode, pick **GutsAndBoltsPlayer** and your iPhone, then press the **Run** button (the triangle). The first build takes a while.
7. The first time, the phone says the developer isn't trusted. Go to **Settings > General > VPN & Device Management**, tap your Apple ID and tap **Trust**. Then open Guts & Bolts from the home screen.

Then go through the checklist below and write down anything that looks wrong.

## Device acceptance checklist

1. Check the home-screen icon, then launch to the home screen and verify built-in games, catalog content, shaders, textures, and other bundled assets load.
2. Background the app during gameplay, wait, then resume; confirm audio and rendering recover and the simulation does not crash.
3. Rotate between supported orientations and test safe-area layout on an iPhone and iPad: nothing under the notch or home bar, and starting a game turns the screen to landscape (leaving it lets the site turn back).
4. Open a valid `gutsandbolts://play/<game-id>` link both when the app is closed and when it is already running. Confirm invalid IDs/queries are rejected by the normal parser.
5. Test the virtual stick, camera drag, jump/action buttons, chat, and menus with simultaneous touches.
6. Test login, joining a public/private game, disconnect/reconnect, and voice-chat permission behavior on actual devices.
7. Check memory use, frame pacing, thermal throttling, and repeated suspend/resume on a lower-end supported device.

The CI workflow compiles both simulator and device targets without signing. Testing on your own device works with a free Apple ID (above); App Store and TestFlight need the paid Apple Developer Program, which the project isn't using.
