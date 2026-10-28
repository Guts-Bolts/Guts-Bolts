# The web Player (Safari on iPhone, and any other browser)

A free way for iPhone and iPad players to play without the App Store: the same
Player, built for the web with [Emscripten](https://emscripten.org) (C++ turned
into WebAssembly, drawing with WebGL 2). It's a web page, so nobody pays Apple
anything. On an iPhone: open the page in Safari, tap Share, then
**Add to Home Screen**, and it opens full screen like an app.

When work lands on `main`, the "Deploy website" workflow builds it and puts it at
**gutsandbolts.net/play/**. The page talks to the website it came from (`/api` and
`/ws`), so a local `wrangler dev` copy talks to itself.

`/play/?game=Obby%20of%20Doom` jumps straight into a game that comes with the
Player; `/play/?page=games` opens a page.

## Build it yourself

```sh
git clone https://github.com/emscripten-core/emsdk && cd emsdk
./emsdk install 3.1.74 && ./emsdk activate 3.1.74 && source ./emsdk_env.sh
cd .. && web/build.sh          # -> web/out (index.html, .js, .wasm, .data)
```

Serve `web/out` from any web server (opening the file directly doesn't work).

## How it differs from the phone apps

- The browser runs the frames (`emscripten_set_main_loop` in `PlayerApp::run`).
- Requests to the server use the browser's `fetch` (`OnlineClient.cpp`), and game
  connections use the browser's WebSocket (`Socket.cpp`): a web page has no
  sockets or threads of its own.
- Water is simulated on the CPU (WebGL 2 has no compute shaders), like on iPhone.
- The games, catalog and pictures that come with the Player are packed into
  `GutsAndBoltsPlayer.data`.

## Not done yet

- Typing: the iPhone keyboard doesn't pop up for text boxes yet.
- Saving: your login and settings live in memory, so they're gone when the tab
  closes (next: keep `/gb` in the browser's IndexedDB).
- Only tested in Chrome so far; it needs a try on a real iPhone.
