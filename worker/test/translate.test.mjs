// In-game chat translation: lines said in another language come back in yours, through
// the chat filter, and the same line is only translated once.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeServer } from './helpers.mjs';

function withTranslator(s, said = 'hello friends') {
  const asked = [];
  s.env.AI = { run: async (model, input) => { asked.push({ model, input }); return { translated_text: said }; } };
  return asked;
}
// What the website's /api does with a chat.translate answer (fetch() in server.js).
async function translate(s, me, args) {
  const r = s.call(me, 'chat.translate', args);
  return r.ok && r.translate ? s.translate(r) : r;
}

test('a line in Russian comes back in English', async () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  const asked = withTranslator(s);
  const r = await translate(s, alice, { text: 'привет друзья', from: 'ru', to: 'en' });
  assert.ok(r.ok, r.error);
  assert.equal(r.text, 'hello friends');
  assert.equal(asked.length, 1);
  assert.deepEqual(asked[0].input, { text: 'привет друзья', source_lang: 'ru', target_lang: 'en' });
  // A second reader of the same line: remembered, so it isn't translated again.
  const again = await translate(s, s.signUp('bob'), { text: 'привет друзья', from: 'ru', to: 'en' });
  assert.equal(again.text, 'hello friends');
  assert.equal(asked.length, 1);
});

test('translations go through the chat filter', async () => {
  const s = makeServer();
  withTranslator(s, 'add me at https://example.com/me');
  const r = await translate(s, s.signUp('alice'), { text: 'добавь меня', from: 'ru', to: 'en' });
  assert.ok(r.ok, r.error);
  assert.doesNotMatch(r.text, /example\.com/);
});

test('same language, unknown languages, and no translator', async () => {
  const s = makeServer();
  const alice = s.signUp('alice');
  let r = s.call(alice, 'chat.translate', { text: 'hi', from: 'en', to: 'en' });
  assert.ok(r.ok);
  assert.equal(r.text, 'hi');
  assert.ok(!s.call(alice, 'chat.translate', { text: 'hi', from: 'xx', to: 'en' }).ok);
  assert.ok(!s.call(alice, 'chat.translate', { text: '', from: 'ru', to: 'en' }).ok);
  r = s.call(alice, 'chat.translate', { text: 'привет', from: 'ru', to: 'en' });
  assert.ok(!r.ok);
  assert.match(r.error, /isn't turned on/);
});

test('when the translator fails, the answer says so (the app shows the line as it was)', async () => {
  const s = makeServer();
  s.env.AI = { run: async () => { throw new Error('out of free use'); } };
  const r = await translate(s, s.signUp('alice'), { text: 'hola', from: 'es', to: 'en' });
  assert.ok(!r.ok);
});
