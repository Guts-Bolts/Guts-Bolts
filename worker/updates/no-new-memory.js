export default {
  id: 'no-new-memory', name: 'Nothing New Under the Sun', time: 1794691650, tag: 'Engine',
  summary: 'The game no longer asks the computer for fresh memory thousands of times a frame, so busy scenes run smoother.',
  items: [
    'In a busy test scene the game used to ask for new memory about 6,000 times every frame. Now it is about 170',
    'Physics keeps its lists from frame to frame and just empties them, instead of throwing them away and making new ones',
    'Checking what touches what, and finding a part by its id, now use flat lists that never need to grow once they are big enough',
    'Soft, squishy and skinned shapes refill their mesh on the graphics card instead of making a brand new one every frame',
    'Looks and plays exactly the same, with fewer little hiccups',
  ],
};
