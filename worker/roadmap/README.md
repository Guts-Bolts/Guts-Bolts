# The built-in roadmap, one file per item

Each `.js` file here is one item on the website's public roadmap (Create > Roadmap).
The file is named after the item's id (`vehicles.js` has `id: 'vehicles'`), and looks like this:

```js
export default {
  id: 'vehicles', title: 'Vehicles', status: 'working', version: '', pr: 0, time: 1793144800,
  text: 'Cars and boats with a seat you can drive.',
};
```

- `status` is `planned`, `working` (being worked on now) or `done`.
- `version` is the version it comes out in, if you know it (like `'0.7.9'`), or `''`.
- `pr` is the pull request's number on GitHub, or `0` if there isn't one yet. The
  website shows a link to it.
- `time` is when it was added (seconds since 1970).
- Keep `title` under 80 letters and `text` under 600, in plain words.

**Every piece of work goes on the roadmap.** When you start something, add a file
here with `status: 'working'`. When the pull request that finishes it is ready,
change it to `'done'` in that same pull request (and fill in `version`). Ideas
for later get `'planned'`.

Why a file each: like the update log (`worker/updates/`), separate files mean two
pull requests never change the same lines. `worker/gather-updates.mjs` finds every
file here by itself. Staff can still add their own items on the website; they
just can't change these ones there.
