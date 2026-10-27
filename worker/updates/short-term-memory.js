export default {
  id: 'short-term-memory', name: 'Short-Term Memory', time: 1793191000, tag: 'Engine',
  summary: 'MemoryStoreService works now: quick data that every server of a game shares, like global leaderboards and matchmaking queues.',
  items: [
    'Scripts can use MemoryStoreService sorted maps, hash maps and queues, the same way as in Roblox',
    'Every running server of an online game sees the same data, so you can make a leaderboard across all servers or a queue that puts players into matches',
    'Everything has an expiration and cleans itself up, and each game gets 64 KB plus 1 KB for every player',
    'When you test in Studio or play alone it still works, the data just stays on your computer',
  ],
};
