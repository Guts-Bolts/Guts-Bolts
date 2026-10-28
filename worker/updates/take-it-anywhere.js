export default {
  id: 'take-it-anywhere', name: 'Take It Anywhere', version: '0.7.7', time: 1793159400, tag: 'Studio',
  summary: 'File > Export As saves your game, or just what you picked, as almost any 3D file: .obj, .fbx, .glb, Roblox .mesh, .stl and more.',
  items: [
    'New Export As window (File > Export As..., the Export button on the HOME tab, or right-click > Export As...). Pick the whole game or only what you selected, then a file type.',
    'New file types: Wavefront .obj (with its .mtl colours), Autodesk .fbx, glTF .glb and .gltf, Roblox\'s own .mesh, .stl for 3D printing, .ply and Collada .dae. The Roblox model (.rbxmx) and place (.rbxlx) are still there.',
    'Each part keeps its colour and its name, and Models stay grouped (in .fbx, .glb and .dae). 1 stud comes out as 1 unit, so things are the right size in Blender, Unity and Godot.',
    'A Roblox .mesh joins everything you picked into one shape with the colours baked onto its corners, ready for a MeshPart.',
    'Choose the name and folder, or press Browse... for your computer\'s own Save window.',
    'Importing an .obj now uses the colours from its .mtl file too.',
    'The Assistant and AI apps can export with the new export_model tool.',
  ],
};
