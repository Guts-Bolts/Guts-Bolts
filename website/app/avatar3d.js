// Avatars in 3D, drawn like the game draws them: the same body model
// (player.obj, a copy of assets/models/player.obj), the same face and hats
// (Player::buildRig / Player::applyHat), your colours and the clothes you wear.
//
// One hidden WebGL canvas does all the drawing (browsers only allow a few);
// big avatars copy each frame onto their own canvas, small ones become images.

const SCALE = 0.5;   // the model is Roblox-sized; characters in the game are half that
const PART_OF = { Torso: 'torso', Head: 'head', Left_Arm: 'leftArm', Right_Arm: 'rightArm', Left_Leg: 'leftLeg', Right_Leg: 'rightLeg' };
const HAT_COLORS = { 1: [30, 30, 36], 2: [204, 46, 41], 3: [240, 190, 40], 4: [219, 51, 158] };

// --- the body model --------------------------------------------------------------

let modelPromise = null;
function loadModel() {
  if (!modelPromise) {
    modelPromise = fetch(new URL('player.obj', import.meta.url)).then((r) => {
      if (!r.ok) throw new Error('no model');
      return r.text();
    }).then(parseObj).then((parts) => {
      for (const [name, part] of Object.entries(parts)) {
        if (name === 'Head') continue;
        const cx = (part.min[0] + part.max[0]) / 2;
        layOut(part, name === 'Torso' ? TORSO_UV : cx < 0 ? RIGHT_LIMB_UV : LEFT_LIMB_UV);
      }
      const hd = parts.Head;
      if (hd) headBox = { c: hd.min.map((m, i) => (m + hd.max[i]) / 2), s: hd.min.map((m, i) => hd.max[i] - m) };
      const to = parts.Torso;
      if (to) torsoBox = { c: to.min.map((m, i) => (m + to.max[i]) / 2), s: to.min.map((m, i) => to.max[i] - m) };
      return parts;
    });
  }
  return modelPromise;
}

// The clothing template (the classic 585 x 559 shirt / pants picture): where each
// side of each body part goes, in pixels. The same numbers as PlayerModel.cpp, so a
// shirt looks the same here as in the game.
const TEMPLATE_W = 585, TEMPLATE_H = 559;
const TORSO_UV = { front: [231, 74, 128, 128], back: [427, 74, 128, 128], right: [165, 74, 64, 128],
  left: [361, 74, 64, 128], up: [231, 8, 128, 64], down: [231, 204, 128, 64] };
const RIGHT_LIMB_UV = { front: [217, 355, 64, 128], back: [85, 355, 64, 128], right: [151, 355, 64, 128],
  left: [19, 355, 64, 128], up: [217, 289, 64, 64], down: [217, 485, 64, 64] };
const LEFT_LIMB_UV = { front: [308, 355, 64, 128], back: [440, 355, 64, 128], right: [506, 355, 64, 128],
  left: [374, 355, 64, 128], up: [308, 289, 64, 64], down: [308, 485, 64, 64] };

// Give each corner a place on the template: each triangle goes on the side of the
// part's box it faces, seen from outside, the same way up as the character.
// (The character faces +Z, so its right side is -X.)
const CAP_BAND = 0.1, CAP_FLAT = 0.95;   // how near the top / bottom counts as its end; how flat a face there must be
function layOut(part, L) {
  const c = part.min.map((m, i) => (m + part.max[i]) / 2), sz = part.min.map((m, i) => Math.max(part.max[i] - m, 1e-6));
  const P = part.pos, uv = [];
  for (let t = 0; t < P.length; t += 9) {
    const e1 = [P[t + 3] - P[t], P[t + 4] - P[t + 1], P[t + 5] - P[t + 2]];
    const e2 = [P[t + 6] - P[t], P[t + 7] - P[t + 1], P[t + 8] - P[t + 2]];
    let n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
    let a = n.map(Math.abs);
    // Only faces at the very top / bottom use the top / bottom boxes (like the game's
    // PlayerModel layOut): a curvy part's up- or down-facing faces in the middle, or
    // sloped ones at the ends, use the side they lean towards, so they don't show the
    // shirt's empty top / bottom boxes.
    const mid = [0, 1, 2].map((i) => (P[t + i] + P[t + 3 + i] + P[t + 6 + i]) / 3 - c[i]).map((m, i) => m / sz[i]);
    const len = Math.hypot(...n) || 1;
    const cap = a[1] >= CAP_FLAT * len && (n[1] > 0 ? mid[1] > 0.5 - CAP_BAND : mid[1] < CAP_BAND - 0.5);
    if (!cap && a[0] < 1e-4 * len && a[2] < 1e-4 * len) { n = [mid[0], 0, mid[2]]; a = n.map(Math.abs); }   // flat, in the middle
    for (let k = 0; k < 3; ++k) {
      const p = [0, 1, 2].map((i) => Math.max(-0.5, Math.min(0.5, (P[t + k * 3 + i] - c[i]) / sz[i])));
      let r, s0, t0;   // s0: 0 = left of the picture, t0: 0 = top
      if (cap) {
        if (n[1] > 0) { r = L.up; s0 = p[0] + 0.5; t0 = p[2] + 0.5; } else { r = L.down; s0 = p[0] + 0.5; t0 = 0.5 - p[2]; }
      } else if (a[0] >= a[2]) {
        if (n[0] < 0) { r = L.right; s0 = p[2] + 0.5; t0 = 0.5 - p[1]; } else { r = L.left; s0 = 0.5 - p[2]; t0 = 0.5 - p[1]; }
      } else if (n[2] > 0) { r = L.front; s0 = p[0] + 0.5; t0 = 0.5 - p[1]; } else { r = L.back; s0 = 0.5 - p[0]; t0 = 0.5 - p[1]; }
      const x = r[0] + 0.5 + s0 * (r[2] - 1), y = r[1] + 0.5 + t0 * (r[3] - 1);
      uv.push(x / TEMPLATE_W, 1 - y / TEMPLATE_H);   // pictures are loaded bottom row first
    }
  }
  part.uv = uv;
}

// Each named object -> triangles (positions + normals), in game units.
function parseObj(text) {
  const v = [], vn = [], parts = {};
  let cur = null;
  for (const line of text.split('\n')) {
    const w = line.trim().split(/\s+/);
    if (w[0] === 'o') { cur = parts[w[1]] = { pos: [], nrm: [], min: [1e9, 1e9, 1e9], max: [-1e9, -1e9, -1e9] }; }
    else if (w[0] === 'v') v.push([+w[1] * SCALE, +w[2] * SCALE, +w[3] * SCALE]);
    else if (w[0] === 'vn') vn.push([+w[1], +w[2], +w[3]]);
    else if (w[0] === 'f' && cur) {
      const corners = w.slice(1).map((c) => c.split('/').map((x) => parseInt(x, 10)));
      for (let i = 1; i + 1 < corners.length; ++i) {
        for (const c of [corners[0], corners[i], corners[i + 1]]) {
          const p = v[c[0] - 1], n = c[2] ? vn[c[2] - 1] : [0, 1, 0];
          cur.pos.push(...p); cur.nrm.push(...n);
          for (let k = 0; k < 3; ++k) { cur.min[k] = Math.min(cur.min[k], p[k]); cur.max[k] = Math.max(cur.max[k], p[k]); }
        }
      }
    }
  }
  return parts;
}

// --- simple shapes (the game's Cube / Cylinder / Sphere, 1 unit across) ------------

function cube() {
  const pos = [], nrm = [];
  const faces = [[[1, 0, 0], [0, 1, 0], [0, 0, 1]], [[-1, 0, 0], [0, 1, 0], [0, 0, -1]], [[0, 1, 0], [0, 0, 1], [1, 0, 0]],
    [[0, -1, 0], [0, 0, -1], [1, 0, 0]], [[0, 0, 1], [-1, 0, 0], [0, 1, 0]], [[0, 0, -1], [1, 0, 0], [0, 1, 0]]];
  for (const [n, u, t] of faces) {
    const c = (a, b) => n.map((x, i) => x * 0.5 + u[i] * a * 0.5 + t[i] * b * 0.5);
    const q = [c(-1, -1), c(1, -1), c(1, 1), c(-1, -1), c(1, 1), c(-1, 1)];
    for (const p of q) { pos.push(...p); nrm.push(...n); }
  }
  return { pos, nrm };
}
function cylinder(seg = 28) {
  const pos = [], nrm = [];
  for (let i = 0; i < seg; ++i) {
    const a0 = (i / seg) * Math.PI * 2, a1 = ((i + 1) / seg) * Math.PI * 2;
    const x0 = Math.cos(a0) * 0.5, z0 = Math.sin(a0) * 0.5, x1 = Math.cos(a1) * 0.5, z1 = Math.sin(a1) * 0.5;
    const n0 = [Math.cos(a0), 0, Math.sin(a0)], n1 = [Math.cos(a1), 0, Math.sin(a1)];
    pos.push(x0, -0.5, z0, x1, 0.5, z1, x1, -0.5, z1, x0, -0.5, z0, x0, 0.5, z0, x1, 0.5, z1);
    nrm.push(...n0, ...n1, ...n1, ...n0, ...n0, ...n1);
    pos.push(0, 0.5, 0, x1, 0.5, z1, x0, 0.5, z0); nrm.push(0, 1, 0, 0, 1, 0, 0, 1, 0);
    pos.push(0, -0.5, 0, x0, -0.5, z0, x1, -0.5, z1); nrm.push(0, -1, 0, 0, -1, 0, 0, -1, 0);
  }
  return { pos, nrm };
}
function sphere(rings = 14, seg = 24) {
  const pos = [], nrm = [];
  const pt = (r, s) => {
    const th = (r / rings) * Math.PI, ph = (s / seg) * Math.PI * 2;
    return [Math.sin(th) * Math.cos(ph), Math.cos(th), Math.sin(th) * Math.sin(ph)];
  };
  for (let r = 0; r < rings; ++r) for (let s = 0; s < seg; ++s) {
    const a = pt(r, s), b = pt(r + 1, s), c = pt(r + 1, s + 1), d = pt(r, s + 1);
    for (const p of [a, b, c, a, c, d]) { pos.push(p[0] * 0.5, p[1] * 0.5, p[2] * 0.5); nrm.push(...p); }
  }
  return { pos, nrm };
}
const SHAPES = { cube: cube(), cylinder: cylinder(), sphere: sphere() };

// --- what to draw for one avatar ------------------------------------------------

const clamp255 = (x) => Math.max(0, Math.min(255, Number(x) | 0)) / 255;
const colorOf = (c, fallback) => (Array.isArray(c) && c.length === 3 ? c : fallback).map(clamp255);

// Pieces: { shape (model part or simple shape), at, size, color, shine, glow }.
function pieces(model, av, items) {
  const a = av || {};
  const def = { head: [245, 205, 48], torso: [13, 105, 172], leftArm: [245, 205, 48], rightArm: [245, 205, 48], leftLeg: [75, 151, 75], rightLeg: [75, 151, 75] };
  const col = {};
  for (const k of Object.keys(def)) col[k] = colorOf(a[k], def[k]);
  const hatsToDraw = [];
  const builtInHat = a.hat | 0;
  const builtInTint = Array.isArray(a.hatColor) && a.hatColor[0] >= 0 ? colorOf(a.hatColor) : null;
  for (const it of items || []) {
    const c = it.meta && Array.isArray(it.meta.color) ? colorOf(it.meta.color) : null;
    if (c) {
      if (it.kind === 'shirt') { col.torso = c; col.leftArm = c; col.rightArm = c; }
      if (it.kind === 'pants') { col.leftLeg = c; col.rightLeg = c; }
    }
    if (it.kind === 'hat' && !isModelAccessory(it)) {
      hatsToDraw.push({ style: Number(it.meta && it.meta.style) || 2, tint: c });
    }
  }
  // Keep the built-in hat unless a Studio accessory explicitly replaces it.
  const hasModelHat = (items || []).some((it) => isModelAccessory(it) && (it.kind === 'hat' || it.kind === 'hair'));
  if (builtInHat && !hasModelHat) hatsToDraw.unshift({ style: builtInHat, tint: builtInTint });
  const out = [];
  // A new body (a bundle made in Studio's Bundles window) and single body parts replace
  // only the parts they have, like the game (Player::applyBody): the bundle's parts first,
  // then each single part over its own slot (the last one worn wins). Until their meshes
  // have loaded (or if they can't), the normal part shows.
  const slotPiece = new Map();   // 'torso', 'leftArm'... -> the piece to draw instead
  const take = (it, only) => {
    const entry = accessories.get(it.id);
    for (const p of (entry && entry.pieces) || []) {
      const key = bodyPartKey(p.bodyPart);
      if (key && (!only || key === only)) slotPiece.set(key, p);
    }
  };
  for (const it of items || []) if (isBodyBundle(it)) take(it);
  for (const it of items || []) if (isBodyPartItem(it)) take(it, bodyPartKey(it.meta.bodyPart));
  // A Headless Head bundle (meta.headless): no head, so no face either (Player::setHeadless).
  const headless = (items || []).some((it) => it && it.kind === 'bundle' && it.meta && it.meta.headless);
  // Clothing pictures (Player::applyClothing): the shirt on the torso and arms, the
  // pants on the legs (and on the torso when there's no shirt).
  const shirt = clothOf(items, 'shirt'), pants = clothOf(items, 'pants');
  const clothFor = { torso: shirt ? 1 : pants ? 2 : 0, leftArm: shirt ? 1 : 0, rightArm: shirt ? 1 : 0, leftLeg: pants ? 2 : 0, rightLeg: pants ? 2 : 0 };
  for (const [name, part] of Object.entries(model)) {
    const key = PART_OF[name];
    if (!key || (key === 'head' && headless)) continue;
    const look = { color: col[key], face: name === 'Head', torso: name === 'Torso', cloth: clothFor[key] || 0 };
    const instead = slotPiece.get(key);
    out.push(instead ? Object.assign({}, instead, look) : Object.assign({ mesh: part }, look));
  }
  // The face is a flat picture painted onto the front of the head (see FS / faceTexture).
  const head = model.Head;
  const hc = head ? head.min.map((m, i) => (m + head.max[i]) / 2) : [0, 2.325, 0];
  const hs = head ? head.min.map((m, i) => head.max[i] - m) : [0.72, 0.65, 0.72];
  // Stack classic hats vertically so multiple owned hats remain visible together.
  const top = hc[1] + hs[1] * 0.5, w = hs[0] / 0.72;
  const H = (shape, p, size, c, extra = {}) => out.push(Object.assign({ shape, at: [p[0] * w, p[1], p[2] * w], size: [size[0] * w, size[1], size[2] * w], color: c }, extra));
  hatsToDraw.slice(0, 12).forEach(({ style, tint }, index) => {
    const lift = index * 0.16, hatDefault = HAT_COLORS[style] ? HAT_COLORS[style].map(clamp255) : [0.1, 0.1, 0.1];
    const main = (normal) => tint || normal;
    if (style === 1) {
      H('cylinder', [0, top + 0.025 + lift, 0], [1.0, 0.05, 1.0], main(hatDefault));
      H('cylinder', [0, top + 0.325 + lift, 0], [0.62, 0.55, 0.62], main(hatDefault));
      H('cylinder', [0, top + 0.11 + lift, 0], [0.64, 0.1, 0.64], [0.75, 0.12, 0.12]);
    } else if (style === 2) {
      H('sphere', [0, top - 0.05 + lift, 0], [0.78, 0.5, 0.78], main([0.85, 0.15, 0.15]));
      H('cube', [0, top - 0.03 + lift, 0.46], [0.52, 0.04, 0.36], main([0.85, 0.15, 0.15]));
    } else if (style === 3) {
      H('cylinder', [0, top + 0.14 + lift, 0], [0.74, 0.28, 0.74], main([1.0, 0.78, 0.2]), { shine: 1 });
      H('cube', [0, top + 0.14 + lift, 0.37], [0.12, 0.12, 0.05], [0.9, 0.1, 0.2], { glow: 1 });
    } else if (style === 4) {
      const hair = main([0.86, 0.2, 0.62]);
      H('sphere', [0, top - 0.1 + lift, -0.05], [0.8, 0.46, 0.82], hair);
      H('cube', [0, top - 0.3 + lift, -0.3], [0.74, 0.42, 0.16], hair);
      H('sphere', [0, top + 0.12 + lift, -0.3], [0.36, 0.34, 0.36], hair);
      H('sphere', [0, top - 0.14 + lift, -0.5], [0.26, 0.56, 0.26], hair);
    }
  });
  for (const it of items || []) {
    const a = isModelAccessory(it) && accessories.get(it.id);
    if (a && a.pieces) out.push(...a.pieces);
  }
  return out;
}

// --- accessories made in Studio (hats, hair, face / neck / shoulder / waist things) ---
// Their shape comes from /wear/<id> (the same "gbaccessory" file the game wears), placed
// from the feet the way the game does (Transform::matrix: position, then Z, Y, X turns,
// then size), so a hat sits on the website's avatar exactly where it sits in a game.
const ACCESSORY_KINDS = ['hat', 'hair', 'faceacc', 'neck', 'shoulder', 'waist'];
const isModelAccessory = (it) => it && ACCESSORY_KINDS.includes(it.kind) && it.meta && it.meta.model;
// Studio body bundles use the same /wear/<id> asset endpoint as model accessories, but
// their geometry replaces the avatar body rather than being attached as a hat/accessory.
const isBodyBundle = (it) => it && it.kind === 'bundle' && it.meta && it.meta.body === true;
const BODY_PART_KEYS = {
  head: 'head', torso: 'torso',
  leftarm: 'leftArm', 'left-arm': 'leftArm', 'left arm': 'leftArm',
  rightarm: 'rightArm', 'right-arm': 'rightArm', 'right arm': 'rightArm',
  leftleg: 'leftLeg', 'left-leg': 'leftLeg', 'left leg': 'leftLeg',
  rightleg: 'rightLeg', 'right-leg': 'rightLeg', 'right leg': 'rightLeg',
};
const bodyPartKey = (value) => BODY_PART_KEYS[String(value || '').replace(/_/g, '-').trim().toLowerCase()] || null;
const isBodyPartItem = (it) => it && it.meta && bodyPartKey(it.meta.bodyPart) && it.meta.bodyPartModel !== false;
const accessories = new Map();   // item id -> { ready, pieces }
function trs(pos, rot, size) {
  const r = (d) => (Number(d) || 0) * Math.PI / 180;
  const [x, y, z] = (pos || [0, 0, 0]).map(Number), [sx, sy, sz] = (size || [1, 1, 1]).map(Number);
  const T = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1];
  const cz = Math.cos(r(rot && rot[2])), snz = Math.sin(r(rot && rot[2]));
  const cy = Math.cos(r(rot && rot[1])), sny = Math.sin(r(rot && rot[1]));
  const cx = Math.cos(r(rot && rot[0])), snx = Math.sin(r(rot && rot[0]));
  const Rz = [cz, snz, 0, 0, -snz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
  const Ry = [cy, 0, -sny, 0, 0, 1, 0, 0, sny, 0, cy, 0, 0, 0, 0, 1];
  const Rx = [1, 0, 0, 0, 0, cx, snx, 0, 0, -snx, cx, 0, 0, 0, 0, 1];
  const S = [sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1];
  return mul(T, mul(Rz, mul(Ry, mul(Rx, S))));
}
// A custom mesh ("v" = x,y,z,..., "f" = corner lists) -> triangles. Flat-shaded, or with
// "smooth" (Studio's Smooth shading, and every imported hat) each corner gets the average
// of the faces around it, like the game does, so round hats look round.
// With "uv" (u,v for each corner of each face, like an imported Roblox hat) it keeps them,
// so the hat's picture lands where it does in the game.
function meshShape(m) {
  const v = m.v || [], pos = [], nrm = [], uv = [];
  const uvs = Array.isArray(m.uv) && m.uv.length === (m.f || []).length ? m.uv : null;
  const faceNormal = (a, b, c) => {
    const e1 = [v[b] - v[a], v[b + 1] - v[a + 1], v[b + 2] - v[a + 2]], e2 = [v[c] - v[a], v[c + 1] - v[a + 1], v[c + 2] - v[a + 2]];
    return [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]];
  };
  let smooth = null;   // corner (vertex index * 3) -> normal
  if (m.smooth) {
    smooth = new Float64Array(v.length);
    for (const f of m.f || [])
      for (let i = 1; i + 1 < f.length; ++i) {
        const n = faceNormal(f[0] * 3, f[i] * 3, f[i + 1] * 3);   // (bigger faces count more)
        for (const k of [f[0], f[i], f[i + 1]]) for (let j = 0; j < 3; ++j) if (k * 3 + j < v.length) smooth[k * 3 + j] += n[j];
      }
  }
  (m.f || []).forEach((f, fi) => {
    const fu = uvs && Array.isArray(uvs[fi]) && uvs[fi].length === f.length * 2 ? uvs[fi] : null;
    for (let i = 1; i + 1 < f.length; ++i) {
      const a = f[0] * 3, b = f[i] * 3, c = f[i + 1] * 3;
      const n = faceNormal(a, b, c);
      for (const k of [a, b, c]) {
        pos.push(v[k], v[k + 1], v[k + 2]);
        const sn = smooth && [smooth[k], smooth[k + 1], smooth[k + 2]];
        nrm.push(...(sn && Math.hypot(...sn) > 1e-12 ? sn : n));
      }
      if (fu) for (const j of [0, i, i + 1]) uv.push(fu[j * 2], fu[j * 2 + 1]);
    }
  });
  return uvs ? { pos, nrm, uv } : { pos, nrm };
}
// A part's picture ("gb:123" / "123"): the decal's number, or '' for none.
const pictureOf = (t) => (typeof t === 'string' && /^(gb:)?[\w-]{1,60}$/.test(t.trim()) && !/[\\/.]/.test(t) ? t.trim().replace(/^gb:/, '') : '');
// One accessory file -> pieces already placed (positions from the feet).
function accessoryPieces(acc) {
  const out = [];
  const walk = (node, parent) => {
    const m = mul(parent, trs(node.pos, node.rot, node.size));
    const shape = node.kind === 'Part' && (node.transparency || 0) < 0.99
      ? (node.shape === 'Mesh' && node.mesh ? meshShape(node.mesh) : SHAPES[{ Cube: 'cube', Sphere: 'sphere', Cylinder: 'cylinder' }[node.shape]])
      : null;
    if (shape) {
      // Normals turn with the inverse-transpose; for these small parts the turn is enough
      // once they're normalised (sizes are positive), so scale them by 1/size per axis.
      const pos = [], nrm = [];
      for (let i = 0; i < shape.pos.length; i += 3) {
        const x = shape.pos[i], y = shape.pos[i + 1], z = shape.pos[i + 2];
        pos.push(m[0] * x + m[4] * y + m[8] * z + m[12], m[1] * x + m[5] * y + m[9] * z + m[13], m[2] * x + m[6] * y + m[10] * z + m[14]);
        // normal: columns scaled by 1 / |column|^2 (the inverse-transpose of rotation x scale)
        const l0 = m[0] * m[0] + m[1] * m[1] + m[2] * m[2] || 1, l1 = m[4] * m[4] + m[5] * m[5] + m[6] * m[6] || 1, l2 = m[8] * m[8] + m[9] * m[9] + m[10] * m[10] || 1;
        const nx = shape.nrm[i] / l0, ny = shape.nrm[i + 1] / l1, nz = shape.nrm[i + 2] / l2;
        nrm.push(m[0] * nx + m[4] * ny + m[8] * nz, m[1] * nx + m[5] * ny + m[9] * nz, m[2] * nx + m[6] * ny + m[10] * nz);
      }
      const tex = shape.uv ? pictureOf(node.texture) : '';
      out.push({ mesh: tex ? { pos, nrm, uv: shape.uv } : { pos, nrm }, tex,
        color: colorOf((node.color || [0.6, 0.6, 0.6]).map((c) => c * 255), [150, 150, 150]),
        shine: node.material === 'Neon' ? 0 : 0, glow: node.material === 'Neon' ? 1 : 0 });
    }
    for (const c of node.children || []) walk(c, m);
  };
  const I = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
  if (acc && acc.format === 'gbaccessory' && acc.node) walk(acc.node, I);
  else if (acc && Array.isArray(acc.nodes)) for (const n of acc.nodes) walk(n, I);   // gear: the tool's parts
  else if (acc && acc.format === 'gbbundle' && acc.body && typeof acc.body === 'object') {
    // Body bundles store each replacement part as indexed mesh data plus its transform,
    // not as the gbaccessory node tree. Expand those meshes into the same world-space
    // triangles used by the avatar renderer.
    for (const [name, part] of Object.entries(acc.body)) {
      if (!part || !Array.isArray(part.v) || !Array.isArray(part.f)) continue;
      const shape = meshShape(part), pos = [], nrm = [];
      const at = part.pos || [0, 0, 0], size = part.size || [1, 1, 1];
      for (let i = 0; i < shape.pos.length; i += 3) {
        pos.push(at[0] + shape.pos[i] * size[0], at[1] + shape.pos[i + 1] * size[1], at[2] + shape.pos[i + 2] * size[2]);
        const nx = shape.nrm[i] / (size[0] || 1), ny = shape.nrm[i + 1] / (size[1] || 1), nz = shape.nrm[i + 2] / (size[2] || 1);
        const l = Math.hypot(nx, ny, nz) || 1;
        nrm.push(nx / l, ny / l, nz / l);
      }
      out.push({ mesh: { pos, nrm, uv: shape.uv }, color: [0.8, 0.81, 0.83], bodyPart: name });
    }
  }
  return out;
}
function loadAccessory(id) {
  if (!accessories.has(id)) {
    const entry = { pieces: null, ready: null };
    entry.ready = fetch('/wear/' + encodeURIComponent(id)).then((r) => (r.ok ? r.json() : null))
      .then((acc) => { entry.pieces = accessoryPieces(acc); }).catch(() => { entry.pieces = []; });
    accessories.set(id, entry);
  }
  return accessories.get(id);
}
// Start (and wait for) the accessories some items need, and their pictures.
async function loadAccessories(items) {
  const list = (items || []).filter((it) => isModelAccessory(it) || isBodyBundle(it) || isBodyPartItem(it)).map((it) => loadAccessory(it.id));
  await Promise.all(list.map((e) => e.ready));
  await Promise.all(list.flatMap((e) => (e.pieces || []).filter((p) => p.tex).map((p) => decalTexture(p.tex).ready)));
}

// Everything in one list of triangles: position, normal, colour, shine, and the
// clothing picture's place (uv) and which one (0 none, 1 shirt, 2 pants).
const STRIDE = 13;
function build(list) {
  const data = [];
  const accTex = [];   // up to 4 accessory pictures per draw (aCloth 3..6)
  for (const p of list) {
    let slot = -1;
    if (p.tex) {
      slot = accTex.indexOf(p.tex);
      if (slot < 0 && accTex.length < 4) { accTex.push(p.tex); slot = accTex.length - 1; }
    }
    const src = p.mesh || SHAPES[p.shape];
    const at = p.at || [0, 0, 0], size = p.size || [1, 1, 1];
    const rc = Math.cos(p.roll || 0), rs = Math.sin(p.roll || 0);   // turned about Z
    for (let i = 0; i < src.pos.length; i += 3) {
      let lx = src.pos[i] * size[0], ly = src.pos[i + 1] * size[1];
      if (p.roll) [lx, ly] = [lx * rc - ly * rs, lx * rs + ly * rc];
      const x = p.mesh ? src.pos[i] : at[0] + lx;
      const y = p.mesh ? src.pos[i + 1] : at[1] + ly;
      const z = p.mesh ? src.pos[i + 2] : at[2] + src.pos[i + 2] * size[2];
      let nx = src.nrm[i], ny = src.nrm[i + 1], nz = src.nrm[i + 2];
      if (!p.mesh) { nx /= size[0]; ny /= size[1]; nz /= size[2]; }
      if (p.roll) [nx, ny] = [nx * rc - ny * rs, nx * rs + ny * rc];
      const l = Math.hypot(nx, ny, nz) || 1;
      const k = (i / 3) * 2;
      const u = p.mesh && src.uv ? src.uv[k] : 0, v = p.mesh && src.uv ? src.uv[k + 1] : 0;
      data.push(x, y, z, nx / l, ny / l, nz / l, p.color[0], p.color[1], p.color[2], p.face ? 3 : p.torso ? 4 : p.glow ? 2 : p.shine ? 1 : 0,
        u, v, p.mesh && src.uv ? (slot >= 0 ? 3 + slot : p.cloth || 0) : 0);
    }
  }
  const out = new Float32Array(data);
  out.accTex = accTex;
  return out;
}

// --- the one WebGL canvas --------------------------------------------------------

const VS = `
attribute vec3 aPos; attribute vec3 aNrm; attribute vec3 aCol; attribute float aShine;
attribute vec2 aUV; attribute float aCloth;
uniform mat4 uMvp; uniform mat3 uRot;
varying vec3 vNrm; varying vec3 vCol; varying float vShine; varying float vY;
varying vec3 vPos; varying vec3 vModelNrm; varying vec2 vUV; varying float vCloth;
void main() {
  vNrm = uRot * aNrm; vCol = aCol; vShine = aShine; vY = aPos.y; vUV = aUV; vCloth = aCloth;
  vPos = aPos; vModelNrm = aNrm;
  gl_Position = uMvp * vec4(aPos, 1.0);
}`;
const FS = `
precision mediump float;
varying vec3 vNrm; varying vec3 vCol; varying float vShine; varying float vY;
varying vec3 vPos; varying vec3 vModelNrm; varying vec2 vUV; varying float vCloth;
uniform float uShadow;
uniform sampler2D uShirt, uPants; // shirt / pants pictures (the template layout)
uniform sampler2D uAcc0, uAcc1, uAcc2, uAcc3;   // accessories' own pictures (an imported hat's texture)
uniform sampler2D uFace;       // the face picture
uniform vec3 uHeadC, uHeadS;   // the head's middle and size
uniform sampler2D uTShirt;     // a T-shirt picture (on the front of the torso)
uniform float uHasTShirt;
uniform vec3 uTorsoC, uTorsoS; // the torso's middle and size
void main() {
  if (uShadow > 0.5) { gl_FragColor = vec4(0.0, 0.0, 0.0, 0.22 * vCol.r); return; }
  vec3 n = normalize(vNrm);
  vec3 base = vCol;
  // Clothing: the picture over the body colour (see-through bits show the body).
  if (vCloth > 2.5) {
    vec4 px = vCloth < 3.5 ? texture2D(uAcc0, vUV) : vCloth < 4.5 ? texture2D(uAcc1, vUV) : vCloth < 5.5 ? texture2D(uAcc2, vUV) : texture2D(uAcc3, vUV);
    base = mix(base, px.rgb, px.a);
  } else if (vCloth > 0.5) {
    vec4 px = vCloth < 1.5 ? texture2D(uShirt, vUV) : texture2D(uPants, vUV);
    base = mix(base, px.rgb, px.a);
  }
  if (vShine > 3.5) {
    // The torso: a T-shirt picture flat on its front (like the game does).
    vec3 mn = normalize(vModelNrm);
    if (uHasTShirt > 0.5 && mn.z > 0.0) {
      vec4 px = texture2D(uTShirt, (vPos.xy - uTorsoC.xy) / uTorsoS.xy + 0.5);
      base = mix(base, px.rgb, px.a * smoothstep(0.35, 0.6, mn.z));
    }
  } else if (vShine > 2.5) {
    // The head: its face picture, flat and seen straight on, painted onto the front
    // of the round head (like the game does).
    vec3 mn = normalize(vModelNrm);
    if (mn.z > 0.0) {
      vec4 px = texture2D(uFace, (vPos.xy - uHeadC.xy) / uHeadS.xy + 0.5);
      base = mix(base, px.rgb, px.a * smoothstep(0.0, 0.3, mn.z));
    }
  }
  vec3 key = normalize(vec3(-0.45, 0.75, 0.6));
  float diff = max(dot(n, key), 0.0);
  float fill = max(dot(n, normalize(vec3(0.6, 0.2, 0.4))), 0.0) * 0.25;
  float sky = 0.5 + 0.5 * n.y;
  vec3 h = normalize(key + vec3(0.0, 0.0, 1.0));
  bool shiny = vShine > 0.5 && vShine < 1.5;
  float spec = pow(max(dot(n, h), 0.0), shiny ? 40.0 : 24.0) * (shiny ? 0.8 : 0.18);
  vec3 c = base * (0.32 + 0.18 * sky + 0.62 * diff + fill) + vec3(spec);
  if (vShine > 1.5 && vShine < 2.5) c = vCol * 1.6;
  gl_FragColor = vec4(pow(c, vec3(0.95)), 1.0);
}`;

let gl = null, glCanvas = null, prog = null, vbo = null, shadowVbo = null, loc = null;

function initGl() {
  if (gl) return gl;
  glCanvas = document.createElement('canvas');
  gl = glCanvas.getContext('webgl', { antialias: true, alpha: true, premultipliedAlpha: false }) ||
       glCanvas.getContext('experimental-webgl');
  if (!gl) return null;
  const sh = (type, src) => { const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s); return s; };
  prog = gl.createProgram();
  gl.attachShader(prog, sh(gl.VERTEX_SHADER, VS));
  gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FS));
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) { gl = null; return null; }
  loc = {
    pos: gl.getAttribLocation(prog, 'aPos'), nrm: gl.getAttribLocation(prog, 'aNrm'),
    col: gl.getAttribLocation(prog, 'aCol'), shine: gl.getAttribLocation(prog, 'aShine'),
    mvp: gl.getUniformLocation(prog, 'uMvp'), rot: gl.getUniformLocation(prog, 'uRot'),
    shadow: gl.getUniformLocation(prog, 'uShadow'),
    face: gl.getUniformLocation(prog, 'uFace'),
    headC: gl.getUniformLocation(prog, 'uHeadC'), headS: gl.getUniformLocation(prog, 'uHeadS'),
    tshirt: gl.getUniformLocation(prog, 'uTShirt'), hasTShirt: gl.getUniformLocation(prog, 'uHasTShirt'),
    torsoC: gl.getUniformLocation(prog, 'uTorsoC'), torsoS: gl.getUniformLocation(prog, 'uTorsoS'),
    uv: gl.getAttribLocation(prog, 'aUV'), cloth: gl.getAttribLocation(prog, 'aCloth'),
    shirt: gl.getUniformLocation(prog, 'uShirt'), pants: gl.getUniformLocation(prog, 'uPants'),
    acc: [0, 1, 2, 3].map((i) => gl.getUniformLocation(prog, 'uAcc' + i)),
  };
  vbo = gl.createBuffer();
  // A soft round shadow under the feet (rings fading out).
  const d = [];
  const ring = (r0, r1, a0, a1) => {
    for (let i = 0; i < 32; ++i) {
      const t0 = (i / 32) * Math.PI * 2, t1 = ((i + 1) / 32) * Math.PI * 2;
      const P = (r, t, a) => [Math.cos(t) * r, 0.001, Math.sin(t) * r * 0.8, 0, 1, 0, a, 0, 0, 0, 0, 0, 0];
      d.push(...P(r0, t0, a0), ...P(r1, t0, a1), ...P(r1, t1, a1), ...P(r0, t0, a0), ...P(r1, t1, a1), ...P(r0, t1, a0));
    }
  };
  ring(0, 0.55, 1, 0.8); ring(0.55, 0.95, 0.8, 0);
  shadowVbo = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, shadowVbo);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(d), gl.STATIC_DRAW);
  return gl;
}

// Column-major 4x4 helpers.
function perspective(fovY, aspect, near, far) {
  const f = 1 / Math.tan(fovY / 2), nf = 1 / (near - far);
  return [f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, (far + near) * nf, -1, 0, 0, 2 * far * near * nf, 0];
}
function mul(a, b) {
  const o = new Array(16).fill(0);
  for (let c = 0; c < 4; ++c) for (let r = 0; r < 4; ++r) for (let k = 0; k < 4; ++k) o[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
  return o;
}

// --- face pictures ---------------------------------------------------------------

// The classic smiley, drawn flat (the same as the game's built-in face picture):
// two oval eyes and a U-shaped smile, see-through around them. The picture covers
// the whole front of the head.
function classicFaceCanvas() {
  const n = 256, c = document.createElement('canvas');
  c.width = c.height = n;
  const g = c.getContext('2d');
  const X = (x) => (x + 0.5) * n, Y = (y) => (0.5 - y) * n;   // head units -> pixels (y up)
  g.fillStyle = '#000';
  for (const ex of [-0.1, 0.1]) {
    g.beginPath(); g.ellipse(X(ex), Y(0.16), 0.0325 * n, 0.065 * n, 0, 0, Math.PI * 2); g.fill();
  }
  g.strokeStyle = '#000'; g.lineWidth = 0.055 * n; g.lineCap = 'round'; g.lineJoin = 'round';
  g.beginPath();
  for (let k = 0; k <= 40; ++k) {
    const x = -0.2 + 0.4 * k / 40, y = -0.27 + 0.22 * Math.pow(Math.abs(x) / 0.2, 1.7);
    if (k) g.lineTo(X(x), Y(y)); else g.moveTo(X(x), Y(y));
  }
  g.stroke();
  return c;
}

const faceTextures = new Map();   // '' = the classic smiley, else a face item's id
function faceTexture(id) {
  if (faceTextures.has(id)) return faceTextures.get(id);
  const tex = gl.createTexture();
  const upload = (img) => {
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  };
  const entry = { tex, ready: null };
  if (!id) { upload(classicFaceCanvas()); entry.ready = Promise.resolve(); }
  else {
    upload(classicFaceCanvas());   // until the picture arrives
    entry.ready = new Promise((resolve) => {
      const img = new Image();
      img.onload = () => { upload(img); resolve(); };
      img.onerror = () => resolve();
      img.src = '/thumb/' + encodeURIComponent(id);
    });
  }
  faceTextures.set(id, entry);
  return entry;
}
// Which face an avatar wears: a face item from the catalog, or the classic smiley.
const faceOf = (items) => { const f = (items || []).find((i) => i && i.kind === 'face'); return f ? f.id : ''; };
// Which T-shirt (a picture on the front of the torso), if any.
const teeOf = (items) => { const f = (items || []).find((i) => i && i.kind === 'tshirt'); return f ? f.id : ''; };
// Shirt / pants pictures (only ones made from the template: older items are just a colour).
const clothOf = (items, kind) => { const f = (items || []).find((i) => i && i.kind === kind && i.meta && i.meta.image); return f ? f.id : ''; };
const teeTextures = new Map();   // pictures by item id: T-shirts, shirts and pants
function teeTexture(id) {
  if (teeTextures.has(id)) return teeTextures.get(id);
  const tex = gl.createTexture();
  const entry = { tex, loaded: false, ready: null };
  entry.ready = new Promise((resolve) => {
    const img = new Image();
    img.onload = () => {
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      entry.loaded = true;
      resolve();
    };
    img.onerror = () => resolve();
    img.src = '/thumb/' + encodeURIComponent(id);
  });
  teeTextures.set(id, entry);
  return entry;
}

// Decal pictures by number (an accessory's texture), from /decal/<id>.
const decalTextures = new Map();
function decalTexture(id) {
  if (decalTextures.has(id)) return decalTextures.get(id);
  const tex = gl.createTexture();
  const entry = { tex, loaded: false, ready: null };
  entry.ready = new Promise((resolve) => {
    const img = new Image();
    img.onload = () => {
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);   // (the game's pictures are bottom row first too)
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      entry.loaded = true;
      resolve();
    };
    img.onerror = () => resolve();
    img.src = '/decal/' + encodeURIComponent(id);
  });
  decalTextures.set(id, entry);
  return entry;
}

// A 1 x 1 see-through picture: clothing that hasn't loaded shows the body colour.
let blankTex = null;
function blankTexture() {
  if (!blankTex) {
    blankTex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, blankTex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([0, 0, 0, 0]));
  }
  return blankTex;
}

let headBox = null;   // the head's middle and size (from the model)
let torsoBox = null;  // ... and the torso's

function draw(buffer, count, w, h, yaw, pitch, face = '', tee = '', shirt = '', pants = '', cam = null) {
  if (glCanvas.width !== w || glCanvas.height !== h) { glCanvas.width = w; glCanvas.height = h; }
  gl.viewport(0, 0, w, h);
  gl.clearColor(0, 0, 0, 0);
  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
  gl.useProgram(prog);
  // Look at the middle of the character from the front, a little from above.
  const cy = Math.cos(yaw), sy = Math.sin(yaw), cp = Math.cos(pitch), sp = Math.sin(pitch);
  // model: turn about Y; view: tilt about X, then back off.
  const rotY = [cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1];
  const at = cam && cam.at ? cam.at : [0, 1.45, 0];   // what's in the middle of the picture
  const center = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -at[0], -at[1], -at[2], 1];
  const tilt = [1, 0, 0, 0, 0, cp, -sp, 0, 0, sp, cp, 0, 0, 0, 0, 1];
  const back = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -(cam && cam.dist ? cam.dist : 7.2), 1];
  const proj = perspective(0.42, w / h, 0.5, 50);
  const view = mul(back, mul(tilt, center));
  const mvp = mul(proj, mul(view, rotY));
  const vr = mul(tilt, rotY);   // normals only turn
  gl.uniformMatrix4fv(loc.mvp, false, new Float32Array(mvp));
  gl.uniformMatrix3fv(loc.rot, false, new Float32Array([vr[0], vr[1], vr[2], vr[4], vr[5], vr[6], vr[8], vr[9], vr[10]]));
  const attrs = () => {
    const S = STRIDE * 4;
    gl.enableVertexAttribArray(loc.pos); gl.vertexAttribPointer(loc.pos, 3, gl.FLOAT, false, S, 0);
    gl.enableVertexAttribArray(loc.nrm); gl.vertexAttribPointer(loc.nrm, 3, gl.FLOAT, false, S, 12);
    gl.enableVertexAttribArray(loc.col); gl.vertexAttribPointer(loc.col, 3, gl.FLOAT, false, S, 24);
    gl.enableVertexAttribArray(loc.shine); gl.vertexAttribPointer(loc.shine, 1, gl.FLOAT, false, S, 36);
    if (loc.uv >= 0) { gl.enableVertexAttribArray(loc.uv); gl.vertexAttribPointer(loc.uv, 2, gl.FLOAT, false, S, 40); }
    if (loc.cloth >= 0) { gl.enableVertexAttribArray(loc.cloth); gl.vertexAttribPointer(loc.cloth, 1, gl.FLOAT, false, S, 48); }
  };
  // Shadow first (no depth), then the body.
  gl.disable(gl.DEPTH_TEST);
  gl.enable(gl.BLEND);
  gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
  gl.bindBuffer(gl.ARRAY_BUFFER, shadowVbo);
  attrs();
  gl.uniform1f(loc.shadow, 1);
  if (!(cam && cam.noShadow)) gl.drawArrays(gl.TRIANGLES, 0, 32 * 12);
  gl.disable(gl.BLEND);
  gl.enable(gl.DEPTH_TEST);
  gl.uniform1f(loc.shadow, 0);
  gl.activeTexture(gl.TEXTURE0);
  gl.bindTexture(gl.TEXTURE_2D, faceTexture(face).tex);
  gl.uniform1i(loc.face, 0);
  if (headBox) { gl.uniform3fv(loc.headC, headBox.c); gl.uniform3fv(loc.headS, headBox.s); }
  const t = tee ? teeTexture(tee) : null;
  gl.activeTexture(gl.TEXTURE1);
  gl.bindTexture(gl.TEXTURE_2D, t && t.loaded ? t.tex : faceTexture('').tex);
  gl.uniform1i(loc.tshirt, 1);
  gl.uniform1f(loc.hasTShirt, t && t.loaded ? 1 : 0);
  if (torsoBox) { gl.uniform3fv(loc.torsoC, torsoBox.c); gl.uniform3fv(loc.torsoS, torsoBox.s); }
  // Clothing pictures (a see-through stand-in until they've loaded).
  const clothTex = (id) => { const c = id ? teeTexture(id) : null; return c && c.loaded ? c.tex : blankTexture(); };
  gl.activeTexture(gl.TEXTURE2);
  gl.bindTexture(gl.TEXTURE_2D, clothTex(shirt));
  gl.uniform1i(loc.shirt, 2);
  gl.activeTexture(gl.TEXTURE3);
  gl.bindTexture(gl.TEXTURE_2D, clothTex(pants));
  gl.uniform1i(loc.pants, 3);
  for (let i = 0; i < 4; ++i) {   // accessories' pictures
    const id = buffer.accTex && buffer.accTex[i];
    const d = id ? decalTexture(id) : null;
    gl.activeTexture(gl.TEXTURE4 + i);
    gl.bindTexture(gl.TEXTURE_2D, d && d.loaded ? d.tex : blankTexture());
    gl.uniform1i(loc.acc[i], 4 + i);
  }
  gl.activeTexture(gl.TEXTURE0);
  gl.bindBuffer(gl.ARRAY_BUFFER, vbo);
  gl.bufferData(gl.ARRAY_BUFFER, buffer, gl.DYNAMIC_DRAW);
  attrs();
  gl.drawArrays(gl.TRIANGLES, 0, count);
}

export function supported() { return !!initGl(); }

// Build only the catalog item's geometry. Never include the avatar body in the catalog viewer.
function itemOnlyPieces(model, item) {
  if (!item) return [];
  if (item.kind === 'gear') return accessories.get(item.id)?.pieces || [];
  if (isModelAccessory(item)) return accessories.get(item.id)?.pieces || [];
  // Classic built-in hats are procedural shapes; discard the mannequin's body meshes.
  if (item.kind === 'hat' || item.kind === 'hair') {
    return pieces(model, MANNEQUIN_GREY, [item]).filter((p) => !p.mesh);
  }
  return [];
}

// --- avatars on the page ---------------------------------------------------------

// A turnable 3D avatar inside `el` (replacing what's there once it's ready).
// Returns { set(avatar, items) } to change it, or null (no WebGL: keep the 2D one).
export async function mountAvatar(el, avatar, items = [], opts = {}) {
  if (!initGl()) return null;
  let model;
  try { model = await loadModel(); } catch { return null; }
  if (!el.isConnected) return null;
  const width = opts.width || 220, height = opts.height || Math.round(width * 1.25);
  const canvas = document.createElement('canvas');
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  canvas.width = Math.round(width * dpr);
  canvas.height = Math.round(height * dpr);
  canvas.style.width = width + 'px';
  canvas.style.height = height + 'px';
  canvas.style.cursor = 'grab';
  canvas.style.touchAction = 'none';
  canvas.title = 'Drag to rotate horizontally and vertically. Double-click to reset.';
  canvas.setAttribute('role', 'img');
  canvas.setAttribute('aria-label', 'Avatar in 3D');
  const ctx = canvas.getContext('2d');
  let buffer = build(opts.itemOnly ? itemOnlyPieces(model, items[0]) : pieces(model, avatar, items)), count = buffer.length / STRIDE;
  let face = faceOf(items), tee = teeOf(items), shirt = clothOf(items, 'shirt'), pants = clothOf(items, 'pants');
  let curAv = avatar, curIts = items;
  const whenLoaded = () => {   // repaint as the pictures (and Studio accessories) arrive
    const want = curIts;
    const gearReady = opts.itemOnly && want[0] && want[0].kind === 'gear' ? loadAccessory(want[0].id).ready : Promise.resolve();
    Promise.all([loadAccessories(want), gearReady]).then(() => {
      if (!canvas.isConnected || want !== curIts) return;
      buffer = build(opts.itemOnly ? itemOnlyPieces(model, curIts[0]) : pieces(model, curAv, curIts)); count = buffer.length / STRIDE;
      paint();
    });
    faceTexture(face).ready.then(() => { if (canvas.isConnected) paint(); });
    for (const id of [tee, shirt, pants]) if (id) teeTexture(id).ready.then(() => { if (canvas.isConnected) paint(); });
  };
  let yaw = opts.yaw ?? -0.35, pitch = opts.pitch ?? 0.12, spin = 0, dragging = false, lastX = 0, lastY = 0, frame = 0;
  const autoSpin = opts.autoSpin === true;
  const paint = () => {
    draw(buffer, count, canvas.width, canvas.height, yaw, pitch, face, tee, shirt, pants, opts.cam || null);
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(glCanvas, 0, 0);
  };
  const loop = () => {
    frame = 0;
    if (!canvas.isConnected) return;
    if (!dragging) {
      if (autoSpin && Math.abs(spin) < 0.0005) yaw += 0.006;
      else if (Math.abs(spin) > 0.0005) { yaw += spin; spin *= 0.92; }
    }
    paint();
    if (autoSpin || dragging || Math.abs(spin) > 0.0005) frame = requestAnimationFrame(loop);
  };
  const kick = () => { if (!frame) frame = requestAnimationFrame(loop); };
  canvas.addEventListener('pointerdown', (e) => {
    dragging = true; lastX = e.clientX; lastY = e.clientY; spin = 0;
    canvas.setPointerCapture(e.pointerId); canvas.style.cursor = 'grabbing'; kick();
  });
  canvas.addEventListener('pointermove', (e) => {
    if (!dragging) return;
    const dx = e.clientX - lastX, dy = e.clientY - lastY;
    lastX = e.clientX; lastY = e.clientY;
    yaw += dx * 0.012; pitch = Math.max(-1.45, Math.min(1.45, pitch + dy * 0.012)); spin = dx * 0.012;
  });
  const up = () => { dragging = false; canvas.style.cursor = 'grab'; kick(); };
  canvas.addEventListener('pointerup', up);
  canvas.addEventListener('pointercancel', up);
  canvas.addEventListener('dblclick', () => { yaw = opts.yaw ?? -0.35; pitch = opts.pitch ?? 0.12; spin = 0; kick(); });
  el.replaceChildren(canvas);
  paint();
  whenLoaded();
  if (autoSpin) kick();
  return {
    set(av, its = []) {
      curAv = av; curIts = its;
      buffer = build(opts.itemOnly ? itemOnlyPieces(model, its[0]) : pieces(model, av, its)); count = buffer.length / STRIDE;
      face = faceOf(its);
      tee = teeOf(its);
      shirt = clothOf(its, 'shirt');
      pants = clothOf(its, 'pants');
      paint();
      whenLoaded();
    },
  };
}

// A still picture of an avatar (for friends lists): a data: URL, or null.
const pictureCache = new Map();
export async function avatarPicture(avatar, items = [], size = 96) {
  if (!initGl()) return null;
  let model;
  try { model = await loadModel(); } catch { return null; }
  const key = JSON.stringify([avatar, (items || []).map((i) => i.id), size]);
  if (pictureCache.has(key)) return pictureCache.get(key);
  await loadAccessories(items);
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const buffer = build(pieces(model, avatar, items));
  const face = faceOf(items), tee = teeOf(items), shirt = clothOf(items, 'shirt'), pants = clothOf(items, 'pants');
  await faceTexture(face).ready;   // (a still picture: wait for the face and clothes)
  for (const id of [tee, shirt, pants]) if (id) await teeTexture(id).ready;
  draw(buffer, buffer.length / STRIDE, Math.round(size * dpr), Math.round(size * 1.25 * dpr), -0.35, 0.12, face, tee, shirt, pants);
  const url = glCanvas.toDataURL('image/png');
  pictureCache.set(key, url);
  return url;
}

// A catalog item's picture, drawn here from the item itself (never an uploaded picture):
// worn by a plain grey mannequin and framed on where it's worn (the head for hats, hair
// and faces), or, for gear, the tool on its own. A data: URL, or null.
const MANNEQUIN_GREY = { head: [204, 207, 212], torso: [204, 207, 212], leftArm: [204, 207, 212], rightArm: [204, 207, 212],
  leftLeg: [189, 191, 199], rightLeg: [189, 191, 199], hat: 0 };
const HEAD_KINDS = ['hat', 'hair', 'face', 'faceacc'], UPPER_KINDS = ['neck', 'shoulder'];
// Where to look for a turnable view of an item being worn (a little wider than its picture).
export function itemCamera(kind) {
  return HEAD_KINDS.includes(kind) ? { at: [0, 2.25, 0], dist: 4.4 } : UPPER_KINDS.includes(kind) ? { at: [0, 1.9, 0], dist: 5.2 } : { at: [0, 1.45, 0], dist: 7.2 };
}
export async function itemPicture(item, size = 150) {
  if (!item || !initGl()) return null;
  const key = 'item:' + JSON.stringify([item.id, item.meta || {}, size]);
  if (pictureCache.has(key)) return pictureCache.get(key);
  const dpr = Math.min(2, window.devicePixelRatio || 1);
  const w = Math.round(size * dpr), h = Math.round(size * dpr);
  let url = null;
  if (item.kind === 'gear') {
    const entry = loadAccessory(item.id);
    await entry.ready;
    const list = entry.pieces || [];
    if (!list.length) return null;
    // Frame it: the middle of its parts, far enough back to fit them.
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const p of list) for (let i = 0; i < p.mesh.pos.length; i += 3)
      for (let k = 0; k < 3; ++k) { lo[k] = Math.min(lo[k], p.mesh.pos[i + k]); hi[k] = Math.max(hi[k], p.mesh.pos[i + k]); }
    const mid = lo.map((v, k) => (v + hi[k]) / 2);
    const moved = list.map((p) => {   // turn about its own middle
      const pos = p.mesh.pos.slice();
      for (let i = 0; i < pos.length; i += 3) for (let k = 0; k < 3; ++k) pos[i + k] -= mid[k];
      return Object.assign({}, p, { mesh: { pos, nrm: p.mesh.nrm, uv: p.mesh.uv } });
    });
    const span = Math.hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) || 1;
    await Promise.all(list.filter((p) => p.tex).map((p) => decalTexture(p.tex).ready));
    const buffer = build(moved);
    await faceTexture('').ready;
    draw(buffer, buffer.length / STRIDE, w, h, -0.6, 0.35, '', '', '', '', { at: [0, 0, 0], dist: span * 2.6, noShadow: true });
    url = glCanvas.toDataURL('image/png');
  } else {
    let model;
    try { model = await loadModel(); } catch { return null; }
    const items = [item];
    await loadAccessories(items);
    const buffer = build(pieces(model, MANNEQUIN_GREY, items));
    const face = faceOf(items), tee = teeOf(items), shirt = clothOf(items, 'shirt'), pants = clothOf(items, 'pants');
    await faceTexture(face).ready;
    for (const id of [tee, shirt, pants]) if (id) await teeTexture(id).ready;
    const cam = HEAD_KINDS.includes(item.kind) ? { at: [0, 2.4, 0], dist: 3.3 }
      : UPPER_KINDS.includes(item.kind) ? { at: [0, 2.0, 0], dist: 4.4 } : { at: [0, 1.45, 0], dist: 7.0 };
    draw(buffer, buffer.length / STRIDE, w, h, item.kind === 'face' ? 0 : -0.35, 0.1, face, tee, shirt, pants, cam);
    url = glCanvas.toDataURL('image/png');
  }
  pictureCache.set(key, url);
  return url;
}
