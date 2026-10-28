export default {
  id: 'no-more-reruns', name: 'No More Reruns', version: '0.7.8', time: 1793222356, tag: 'Studio',
  summary: 'The updater can\'t get Studio or the Player stuck closing and reopening over and over any more, and if an update fails it tells you why and lets you download it yourself.',
  items: [
    'Fixed: on Windows, when an update failed partway, the updater opened the old version again, which started the update again, forever',
    'The updater now waits for the app to really close before swapping files, and moves a program that\'s still open (like the Player) out of the way instead of giving up',
    'If an update doesn\'t work, the app remembers it and shows what went wrong, with "Download it myself", "Try again" and "Use this version for now" buttons, instead of trying again by itself',
    'Downloads on Windows are much faster (the hidden progress bar was slowing them down)',
    'On Mac and Linux, programs are removed before the new ones go in, so macOS doesn\'t refuse to open the updated app',
  ],
};
