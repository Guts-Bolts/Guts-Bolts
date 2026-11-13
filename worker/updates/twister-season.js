export default {
  id: 'twister-season', name: 'Twister Season', time: 1794538921, tag: 'Engine',
  summary: 'Real 3D clouds, supercell storms, downbursts, and tornadoes that throw your loose parts around.',
  items: [
    'Clouds are real 3D shapes now (High graphics and up): you can see their flat bottoms, lumpy tops and dark undersides, and the sun lights them from its side',
    'They come in the kinds the real sky has: Cumulus (fair-weather heaps), Stratus (a flat grey sheet), Cumulonimbus (thunderheads with an anvil on top) and Supercell. The sky picks the one that fits the weather, or you choose with Lighting.CloudType',
    'New weather: Supercell. One huge slowly turning storm with a wall cloud hanging under it, and that is where tornadoes come down',
    'Tornadoes: a funnel snakes down from the wall cloud, kicks up a dust cloud, wanders across the map and ropes out. Strength goes from EF0 to EF5',
    'The wind in one works like a real one: fastest at the edge of its core, sucking in along the ground and rushing up the middle. Loose parts get pulled in, lifted and thrown. Anchored parts stay put',
    'Players get shoved by the wind, and a strong tornado picks you up',
    'Downbursts: a column of cold rainy air drops out of a storm, hits the ground and blasts outwards in every direction',
    'Storms make these by themselves (Lighting.SevereWeather turns that off). Scripts can start their own with Lighting:SpawnTornado(position, strength, seconds) and Lighting:SpawnDownburst(position), stop them with Lighting:ClearStorms(), and read the wind anywhere with Lighting:GetWindAt(position)',
    'Everyone in a server sees the same tornado in the same place',
    'Rain runs down walls in thin trickles now, and blows the way the storm wind blows',
    'Studio: Weather has a Supercell choice, a Cloud type list and a Severe weather tickbox',
    'Low and Medium graphics keep the light painted clouds, so old PCs and phones run the same as before',
  ],
};
