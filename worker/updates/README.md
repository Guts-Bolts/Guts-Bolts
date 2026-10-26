# The update log, one file per update

Each `.js` file here is one entry on the website's Updates page. The file is named
after the update's id (`crowd-control.js` has `id: 'crowd-control'`), and looks like this:

```js
export default {
  id: 'glass-lagoon', name: 'Glass Lagoon', time: 1793037789, tag: 'Engine',
  summary: 'Water you can actually see through.',
  items: [
    'One short line for each thing that changed, in plain words',
  ],
};
```

Why a file each: when every pull request added its entry at the top of one big
file, they all changed the same lines, so merging one made all the others
conflict. Separate files never touch each other.

`worker/gather-updates.mjs` finds every file here by itself; you don't add it to
a list anywhere. The older updates are still in `worker/updates.js`.
