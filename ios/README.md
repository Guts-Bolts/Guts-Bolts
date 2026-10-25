# Guts & Bolts iOS Player

The iOS target reuses the shared C++/SDL2 mobile Player. The target is an iPhone/iPad app bundle (iOS 15+) and is intended to become a real game client, not a separate engine implementation.

## Current implementation

- CMake creates `GutsAndBoltsPlayer.app` and declares the `gutsandbolts://` URL scheme.
- The build packages `games/`, `catalog/`, and `assets/` in the app's `Resources` directory.
- On first launch and after a version change, those resource trees are copied into the app's writable sandbox. A failed copy is not stamped as complete, so the next launch can retry.
- Runtime URL events received as SDL drop-file events are queued into the Player's existing launch-link parser. Link IDs and query values still pass through `LaunchLink::parse` validation.
- The SDL mobile window handles background/foreground events and mutes audio while backgrounded. Shared touch controls already provide a virtual stick, camera gestures, and gameplay buttons.
- The iOS target uses the existing OpenGL ES 3.0 renderer and CPU liquid-solver fallback.

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

## Device acceptance checklist

1. Launch to the home screen and verify built-in games, catalog content, shaders, textures, and other bundled assets load.
2. Background the app during gameplay, wait, then resume; confirm audio and rendering recover and the simulation does not crash.
3. Rotate between supported orientations and test safe-area layout on an iPhone and iPad.
4. Open a valid `gutsandbolts://play/<game-id>` link both when the app is closed and when it is already running. Confirm invalid IDs/queries are rejected by the normal parser.
5. Test the virtual stick, camera drag, jump/action buttons, chat, and menus with simultaneous touches.
6. Test login, joining a public/private game, disconnect/reconnect, and voice-chat permission behavior on actual devices.
7. Check memory use, frame pacing, thermal throttling, and repeated suspend/resume on a lower-end supported device.

The CI workflow compiles both simulator and device targets without signing. Physical-device testing and App Store/TestFlight distribution still require Apple signing and provisioning.
