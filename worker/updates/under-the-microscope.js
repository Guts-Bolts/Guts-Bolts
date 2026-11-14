export default {
  id: 'under-the-microscope', name: 'Under the Microscope', time: 1794367500, tag: 'Engine',
  summary: 'Engine builders can now switch on Tracy, a super-detailed profiler, to hunt down slow frames.',
  items: [
    'A new build switch (GB_TRACY) adds the free Tracy profiler, for people working on the engine itself',
    'It shows every frame as a timeline: shadows, drawing, physics, and each script by name',
    'It is off in normal builds, so players, phones and the web Player are not changed at all',
    'Even when it is on, it only records while the Tracy viewer on the same computer is connected',
    'Game makers keep using the Live Profiler (View menu in Studio, Ctrl+F6 in the Player)',
  ],
};
