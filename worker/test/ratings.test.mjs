// Thumbs up / down on Library models: one vote each, changeable and removable, only after
// you've inserted the model, never on your own, and a fair "Best rated" sort.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

const modelFile = Buffer.from(JSON.stringify({ format: 'gbmodel', nodes: [{ id: 1, name: 'Crate', kind: 'Part' }] })).toString('base64');
function publish(s, who, name, access = 'public') {
  const r = s.call(who, 'upload', { kind: 'model', name, data: modelFile, access });
  assert.ok(r.ok, r.error);
  return s.assets.get(r.asset.id);
}
// Studio's Toolbox fetches a model with "get" to insert it.
const insert = (s, who, a) => { const r = s.call(who, 'get', { id: a.id }); assert.ok(r.ok, r.error); };

test('you can rate a model after inserting it, change your vote and take it back', () => {
  const s = makeServer();
  const maker = s.signUp('maker'), bob = s.signUp('bob');
  const crate = publish(s, maker, 'Crate');
  const first = s.call(bob, 'model.vote', { id: crate.id, vote: 1 });
  assert.equal(first.ok, false);
  assert.match(first.error, /Insert it/);
  assert.equal(s.call(bob, 'asset.info', { id: crate.id }).used, false);
  insert(s, bob, crate);
  assert.equal(s.call(bob, 'asset.info', { id: crate.id }).used, true);
  let r = s.call(bob, 'model.vote', { id: crate.id, vote: 1 });
  assert.ok(r.ok, r.error);
  assert.equal(r.asset.likes, 1);
  assert.equal(r.asset.dislikes, 0);
  assert.equal(r.asset.myVote, 1);
  // Voting again doesn't count twice.
  r = s.call(bob, 'model.vote', { id: crate.id, vote: 1 });
  assert.equal(r.asset.likes, 1);
  // Change it to a thumbs down.
  r = s.call(bob, 'model.vote', { id: crate.id, vote: -1 });
  assert.equal(r.asset.likes, 0);
  assert.equal(r.asset.dislikes, 1);
  assert.equal(r.asset.myVote, -1);
  // Take it back.
  r = s.call(bob, 'model.vote', { id: crate.id, vote: 0 });
  assert.equal(r.asset.likes, 0);
  assert.equal(r.asset.dislikes, 0);
  assert.equal(r.asset.myVote, 0);
});

test("you can't rate your own model, a private one, or something that isn't a model", () => {
  const s = makeServer();
  const maker = s.signUp('maker2'), bob = s.signUp('bob2');
  const crate = publish(s, maker, 'My Crate');
  insert(s, maker, crate);
  const own = s.call(maker, 'model.vote', { id: crate.id, vote: 1 });
  assert.equal(own.ok, false);
  assert.match(own.error, /your own/);
  const secret = publish(s, maker, 'Secret', 'private');
  assert.equal(s.call(bob, 'model.vote', { id: secret.id, vote: 1 }).ok, false);
  const game = s.addGame(maker, 'Obby');
  assert.equal(s.call(bob, 'model.vote', { id: game.id, vote: 1 }).ok, false);
});

test('the Library shows likes and votes on models', () => {
  const s = makeServer();
  const maker = s.signUp('maker3'), bob = s.signUp('bob3'), amy = s.signUp('amy3');
  const crate = publish(s, maker, 'Shown Crate');
  for (const [who, v] of [[bob, 1], [amy, -1]]) { insert(s, who, crate); assert.ok(s.call(who, 'model.vote', { id: crate.id, vote: v }).ok); }
  const r = s.call(bob, 'list', { kind: 'model' });
  const shown = r.assets.find((a) => a.id === crate.id);
  assert.equal(shown.likes, 1);
  assert.equal(shown.dislikes, 1);
  assert.equal(shown.myVote, 1);
  // Decals and audio don't have votes.
  assert.equal(s.call(bob, 'list', { kind: 'decal' }).assets.every((a) => a.likes === undefined), true);
});

test('"Best rated" puts many good votes above one perfect vote', () => {
  const s = makeServer();
  const maker = s.signUp('maker4');
  const one = publish(s, maker, 'One Vote Wonder'), many = publish(s, maker, 'Crowd Pleaser'), meh = publish(s, maker, 'Meh');
  publish(s, maker, 'Nobody Voted');
  // Votes set straight on the models (500 people inserting and voting would be slow to set up).
  Object.assign(one, { likes: 1, dislikes: 0 });
  Object.assign(many, { likes: 475, dislikes: 25 });
  Object.assign(meh, { likes: 5, dislikes: 5 });
  const r = s.call(maker, 'list', { kind: 'model', sort: 'rated' });
  assert.deepEqual(r.assets.map((a) => a.name), ['Crowd Pleaser', 'Meh', 'One Vote Wonder', 'Nobody Voted']);
});
