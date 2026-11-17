export default {
  id: 'act-of-god', name: 'Act of God', time: 1794891861, tag: 'Engine',
  summary: 'Games can now set off lightning, tornadoes, tsunamis and earthquakes wherever they want, tuned however they want.',
  items: [
    'New Disasters service for scripts: Disasters:Lightning, :Tornado, :Tsunami and :Earthquake, each at a spot you pick',
    'Lightning can strike a chosen player or a spot, and can hurt them, ragdoll them or kill them outright',
    'Tornadoes suck people in, spin them round, carry them up and throw them. Turn on ExplodeOnImpact and they blow up when they land',
    'Tune a tornado with RotationSpeed, PullForce, LiftForce, ThrowForce and ThrowAfter, make it drift with Velocity or chase someone with :Follow(player)',
    'Tsunamis are a tall rolling wall of water that sweeps people along and smashes Breakable parts. Pick its Height, Width, Speed and how far it goes',
    'Earthquakes have a magnitude: a bigger number shakes the screen and the ground harder, knocks loose parts about and makes people stumble or ragdoll',
    'Every disaster has Hit, Thrown, Exploded, Killed and Ended events, and :Stop(), :MoveTo() and :Set() to change it while it runs',
    'The server runs every disaster, so all players see the same thing in the same place, even on game servers with no player hosting',
    'Studio has Natural Disasters spawner parts in Insert Object: set them up with attributes and they go off by themselves',
    'New Disaster Survival game template: survive random disasters on an island, round after round',
  ],
};
