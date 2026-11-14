export default {
  id: 'long-shadows', name: 'Long Shadows', time: 1794537676, tag: 'Engine',
  summary: 'Sun shadows now use up to four shadow maps instead of two, so they stay sharp up close and reach much further into the distance.',
  items: [
    'On High and Epic graphics the sun draws four shadow maps of different sizes: a small sharp one near the camera and wider ones that carry shadows far away',
    'With the normal ShadowDistance of 45, shadows used to stop about 22 studs out. Now they reach about 140',
    'The maps blend into each other, so you do not see a line where one ends and the next begins',
    'The two widest maps take turns being redrawn, so four maps cost about the same as three',
    'Each map skips parts that are outside it, and shadows do not shimmer when the camera moves',
    'Medium graphics uses two maps, Low uses one, and phones never use more than two',
  ],
};
