export default {
  id: 'no-more-small-talk', name: 'No More Small Talk', version: '0.8.0', time: 1794248548, tag: 'Studio',
  summary: 'Studio\'s AI tools now speak the newest MCP rules (July 28, 2026), and older AI apps still work.',
  items: [
    'AI apps no longer need a "hello" handshake before using Studio\'s tools: every request just says which rules it uses',
    'New server/discover question tells an AI app which MCP versions Studio and gutsandbolts.net/mcp understand',
    'Requests now carry headers that must match what\'s inside them, and mismatches are turned away with a clear error',
    'Works the same on the website (for connected apps like Lyreto) and in Studio itself, including GutsAndBolts --mcp',
    'AI apps that use the older 2025 rules keep working exactly like before',
  ],
};
