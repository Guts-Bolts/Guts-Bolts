export default {
  id: 'three-in-one', name: 'Three In One', version: '0.8.0', time: 1794507304, tag: 'Engine',
  summary: 'Custom materials can use one packed picture for occlusion, roughness and metal, and get dark grooves that only sky light misses.',
  items: [
    'MaterialVariants have a new OrmMap: one picture with occlusion in red, roughness in green and metalness in blue',
    'Occlusion is new: gaps and grooves in a material stay darker in soft sky and bounce light, while direct sun and lamps still reach them',
    'A material with a RoughnessMap and a MetalnessMap is packed into one picture by itself the first time it is drawn, so the graphics card reads one picture instead of two',
    'Scripts can set it too: variant.OrmMap = "tiles_orm.png"',
    'Old games look the same as before',
  ],
};
