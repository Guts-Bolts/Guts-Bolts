// Gathers the update log. Every file in worker/updates/ is one update (see the note
// at the top of worker/updates.js), so two pull requests never change the same lines
// and can't get in each other's way. This writes worker/updates-gathered.js, which
// lists them all. That file isn't kept in git: it's made fresh every time, by
// Cloudflare before it puts the site online (the "build" in wrangler.jsonc), by
// `wrangler dev`, and by the tests before they start (worker/test/register.mjs).
//
//   node worker/gather-updates.mjs
import { readdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = new URL('.', import.meta.url);

export function gatherUpdates() {
  const files = readdirSync(new URL('updates/', here)).filter((f) => /^[a-z0-9-]+\.js$/.test(f)).sort();
  const lines = ['// Made by worker/gather-updates.mjs from the files in worker/updates/. Don\'t edit it: it\'s remade every time.'];
  files.forEach((f, i) => lines.push(`import u${i} from './updates/${f}';`));
  lines.push(`export default [${files.map((_, i) => 'u' + i).join(', ')}];`, '');
  writeFileSync(new URL('updates-gathered.js', here), lines.join('\n'));
  return files;
}

if (process.argv[1] === fileURLToPath(import.meta.url))
  console.log(`Gathered ${gatherUpdates().length} update(s) from worker/updates/.`);
