export default {
  id: 'everybody-bleeds', name: 'Everybody Bleeds the Same', version: '0.8.1', time: 1794892400, tag: 'Engine',
  summary: 'Blood shows up again on PC, and every player in a server sees the very same blood.',
  items: [
    'Fixed blood not showing at all on PC: pools, splats and stains were being drawn upside down, so the graphics card skipped them',
    'Fast drops of blood no longer sink into the floor and hide under it',
    'Blood is server-sided now: the server decides where every spray, pool, smear and footprint goes, and sends it to everyone',
    'Everyone sees the same pools in the same places, even players who join later',
    'Blood stains on a body land on the same spot of that arm or leg on every screen',
    'Getting hurt on your own computer (a fall, a trap) or by a server script makes blood everyone can see',
    'Footprints from walking through blood show for everyone too',
  ],
};
