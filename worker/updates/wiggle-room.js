export default {
  id: 'wiggle-room', name: 'Wiggle Room', time: 1793146100, tag: 'Studio',
  summary: 'Bones that move by themselves (Jiggle, LookAt and IK), ready-made rigs, and a brush to paint weights, so you can rig your own skinned meshes.',
  items: [
    'Bones now have a Bone Type: Normal, Jiggle, LookAt or IK',
    'Jiggle bones wobble on a spring when the part moves: tails, ears, hair, antennas. Stiffness, Damping and Gravity change how they bounce',
    'LookAt bones turn to face a Target (eyes, heads, turrets), and IK bones bend an arm or leg so its tip reaches a Target',
    'The Armature window can add ready-made rigs: a jiggle chain, an IK arm or leg, a look-at bone, or a whole humanoid skeleton fitted to your mesh',
    'Paint weights with a brush in Modeling mode: Add, Take Away and Smooth, with a size and strength',
    'Make Target puts a little ball at the bone\'s tip that you can drag around; tick Preview to see it all move in Studio',
    'It all works in the Player and online, and scripts can set it up too (bone.BoneType = "Jiggle", bone.Target = part)',
  ],
};
