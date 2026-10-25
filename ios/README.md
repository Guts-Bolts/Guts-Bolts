# Guts & Bolts iOS Player

Initial iOS platform target for the shared C++/SDL2 Player.

## Status

- CMake recognizes iOS and configures the existing mobile Player as an iPhone/iPad app bundle.
- The bundle declares the `gutsandbolts://` URL scheme and microphone usage description.
- Writable player data is directed to SDL's per-app preference directory instead of the read-only app bundle.
- This is a porting foundation, not a verified release. It still requires a real Xcode/device build and testing of SDL lifecycle handling, URL delivery, bundled asset extraction, graphics compatibility, audio, multiplayer, and signing.

## Build on macOS

Install Xcode and CMake, then run from the repository root:

```sh
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=iphonesimulator \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ios --config Release --target GutsAndBoltsPlayer
```

Use `-DCMAKE_OSX_SYSROOT=iphoneos` for a device build. Device installation and
TestFlight/App Store distribution require signing and provisioning in Xcode.
Never commit signing credentials.
