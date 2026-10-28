#!/usr/bin/env bash
# Builds the web Player into web/out (see web/README.md). Needs Emscripten's emsdk:
#   git clone https://github.com/emscripten-core/emsdk && cd emsdk
#   ./emsdk install 3.1.74 && ./emsdk activate 3.1.74 && source ./emsdk_env.sh
set -euo pipefail
cd "$(dirname "$0")/.."
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DGB_VR=OFF
cmake --build build-web --target GutsAndBoltsPlayer -j"$(nproc 2>/dev/null || echo 4)"
rm -rf web/out && mkdir -p web/out
cp web/index.html build-web/GutsAndBoltsPlayer.js build-web/GutsAndBoltsPlayer.wasm build-web/GutsAndBoltsPlayer.data web/out/
ls -la web/out
