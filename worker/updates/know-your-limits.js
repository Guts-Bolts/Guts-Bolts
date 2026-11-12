export default {
  id: 'know-your-limits', name: 'Know Your Limits', time: 1794470989, tag: 'Engine',
  summary: 'The groundwork for the photoreal graphics upgrade: frames that reuse their memory, and an honest notice when your device has to skip a feature.',
  items: [
    'Lists the renderer only needs for one frame now come out of one block of memory that is reused every frame, instead of asking the system again each time',
    'One place in the engine now decides what each device can do, so every new graphics feature asks the same question',
    'Macs, iPhones, iPads and the web Player can\'t run features that do their maths on the graphics card: Settings now lists which ones, what you get instead, and why',
    'The first time you play on such a device, a short notice tells you once',
    'Windows, Linux and newer Android phones are not affected',
  ],
};
