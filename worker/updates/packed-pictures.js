export default {
  id: 'packed-pictures', name: 'Pre-Squeezed', time: 1794700000, tag: 'Engine',
  summary: 'Game pictures can now be packed ahead of time, so they load faster and take up to 4 times less graphics memory.',
  items: [
    'A packed picture is already squeezed into a format the graphics card reads as it is, with all its smaller copies made ahead of time, so loading it is just copying it over',
    'Pictures you download are packed in the background the first time they load, so every later load is quick',
    'Creators can pack a whole games folder at once with the new gbpack tool',
    'Computers get BC7 (and BC5 for bump maps), phones get ETC2, and a device that can read neither (like a Mac) loads the normal picture as before',
    'Packed pictures look almost exactly like the originals: we checked them side by side',
  ],
};
