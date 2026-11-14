export default {
  id: 'clean-cut', name: 'Clean Cut', time: 1794614759, tag: 'Engine',
  summary: 'Any part can now be cut in two with a flat cut, and both halves keep their real shape. Parts made of flesh show skin, fat, muscle and bone where they were cut.',
  items: [
    'New part:Slice(point, normal) for scripts: it cuts a part along a flat line and gives back the two halves',
    'The halves keep the real shape: half a barrel is still round, and a plank cut on a slant has a slanted end',
    'Blocks, balls, cylinders and custom meshes can all be cut, and a half can be cut again and again',
    'The cut end is closed off, so you never see inside a hollow shell',
    'Give a part the attribute Flesh = true and the cut shows layers: skin on the outside, a thin band of fat, red muscle and pale bone in the middle, and it bleeds',
    'Give a part an InsideColor to pick what the inside looks like, such as pale wood inside dark bark',
    'The cut-off half comes loose and is pushed away, and the other half stays put if the part was anchored',
    'Flesh insides follow the Gore setting: with Gore off, a Flesh part cuts like anything else',
  ],
};
