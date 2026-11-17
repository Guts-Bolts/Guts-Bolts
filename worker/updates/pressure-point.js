export default {
  id: 'pressure-point', name: 'Pressure Point', time: 1794926000, tag: 'Engine',
  summary: 'Get shot in an artery and the blood spurts out with every heartbeat while you stagger about and bleed to death. And dead bodies finally lie still.',
  items: [
    'A shot through the neck, the inside of a thigh or the inside of an upper arm opens an artery',
    'Blood spurts out in pulses, one for every heartbeat: fast and strong at first, weaker as the blood runs out, and a few last beats after death',
    'The hurt character staggers about like a real active ragdoll: lurching one way, the legs hurrying after it, hunched over with a hand pressed on the wound',
    'They get slower and weaker, then the legs give out: they drop, curl up round the wound and bleed to death (about 10 seconds)',
    'The server decides it all, so every player sees the same spurts and the same stagger',
    'For game makers: workspace.ArterialBleeding (on by default, also in Game Settings), Humanoid:CutArtery(part, seconds), Humanoid:StopBleeding() for a medkit or tourniquet, Humanoid.BleedingOut, Humanoid.Artery and Humanoid.BleedOutTime',
    'A character with an "Arteries" attribute set to false never gets one (robots, bosses)',
    'Fixed dead bodies twitching, thrashing and getting flung across the map: a body pushed out of a wall or floor is no longer thrown out of it',
    'Dead bodies stop moving: a short last struggle, then they settle and lie completely still (they still fall if the floor under them goes away)',
    'Fixed ragdolls spinning and jittering on other players\' screens: body parts lying on their side now turn smoothly between updates',
  ],
};
