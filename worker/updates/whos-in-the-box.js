export default {
  id: 'whos-in-the-box', name: 'Who\'s in the Box?', version: '0.7.8', time: 1793198000, tag: 'Engine',
  summary: 'Scripts can find every part inside a zone, the same way Roblox games do.',
  items: [
    'workspace:GetPartsInPart(part) gives every part that overlaps it, using the real shapes (a turned part is turned, a ball is round)',
    'workspace:GetPartBoundsInBox(cframe, size) and GetPartBoundsInRadius(position, radius) are quicker checks for a box or a ball',
    'OverlapParams picks what counts: leave things out or only look at some, a most-parts limit, collision groups and CanCollide',
    'Handy for damage zones, shop areas, traps and "is anyone standing here?" checks',
  ],
};
