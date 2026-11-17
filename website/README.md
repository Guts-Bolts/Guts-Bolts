# Guts&Bolts website

Hosted on **Cloudflare Workers** (free), from this repo. It has two parts:

- **The front page** (`index.html`, `img/`): what Guts&Bolts is, with screenshots
  and download links.
- **The site** (`app/`): the same pages as the Player app, in a browser. People
  can sign up and log in, browse games, buy from the catalog, upload decals,
  audio, clothes, plugins and games, rename their games, change their avatar
  (shared with the app), and use friends, people, groups and Bolts. Staff get
  a Staff page. Game cards show the picture Studio uploads when publishing
  (served at `/thumb/<game id>`).

## The Guts&Bolts server lives here too

The same Cloudflare Worker also **is** the Guts&Bolts server
(`worker/server.js`, a Durable Object with its own database), so everything
stays online when your computer is off:

- `/api`: signed requests (from the website and the apps).
- `/ws`: WebSockets for the apps' multiplayer relay.

Settings, in the dashboard under the Worker's **Settings → Variables and
Secrets** (kept across deploys):

- `OFFICIAL`: your staff account's ID. That account becomes user #1, "Guts".
- `SERVER_NAME`: the name the apps show (default "Guts&Bolts").
- `GB_SERVER`: only set this to use a server on a computer instead (host:port).

**Your password never leaves the browser.** Every request is signed in the page
with the account's own key, just like the apps do (the crypto is the same
Monocypher library, compiled to WebAssembly: `app/gbcrypto.wasm`). The Worker
only carries sealed requests and can't change them.


## Put it online (one time)

1. Make a free account at <https://dash.cloudflare.com/sign-up>.
2. In the dashboard: **Workers & Pages → Create → Import a repository**, and
   pick `Project-Guts-And-Bolts`.
3. Leave the build command empty. The deploy command is `npx wrangler deploy`.
4. Deploy. You get an address like `project-guts-and-bolts.<you>.workers.dev`.

From then on, every change merged into `main` goes live on its own.

## Change it

- Front page text: `index.html`. Pictures: `img/`.
- Products: `products.html` lists everything G&B makes. Each download app has its own page in `products/` (`sysfix.html`, `contentforge.html`); all of them share `products/product.css` (the site's red and white, dark mode through `theme.js`) and `products/product.js` (copy buttons, and turning on a download once it's on the mirror). The Worker serves `/products`, `/products/Sysfix` and `/products/ContentForge`. The apps also show on the website's Home from `APPS` in `app/products.js`.
- Site pages: `app/app.js` (one function per page), look: `app/app.css`.
- The crypto (`app/gbcrypto.wasm`) is built by `tools/webcrypto/build.sh`. You only
  need to rebuild it if `tools/webcrypto/gbcrypto.c` changes.

## Try it on your computer

```sh
npx wrangler dev
```

Point an app at it with the server address `http://localhost:8787`.

Then open <http://localhost:8787/app/>.
