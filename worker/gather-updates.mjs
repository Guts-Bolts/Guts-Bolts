// Gathers the update log and the built-in roadmap. Every file in worker/updates/ is one
// update (see the note at the top of worker/updates.js), and every file in worker/roadmap/
// is one roadmap item (see worker/roadmap/README.md), so two pull requests never change the
// same lines and can't get in each other's way. This writes worker/updates-gathered.js and
// worker/roadmap-gathered.js, which list them all. Those files aren't kept in git: they're
// made fresh every time, by Cloudflare before it puts the site online (the "build" in
// wrangler.jsonc), by `wrangler dev`, and by the tests before they start (build.yml).
//
//   node worker/gather-updates.mjs
import { readdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const here = new URL('.', import.meta.url);

function gather(folder) {
  const files = readdirSync(new URL(folder + '/', here)).filter((f) => /^[a-z0-9-]+\.js$/.test(f)).sort();
  const lines = [`// Made by worker/gather-updates.mjs from the files in worker/${folder}/. Don't edit it: it's remade every time.`];
  files.forEach((f, i) => lines.push(`import u${i} from './${folder}/${f}';`));
  lines.push(`export default [${files.map((_, i) => 'u' + i).join(', ')}];`, '');
  writeFileSync(new URL(folder + '-gathered.js', here), lines.join('\n'));
  return files;
}

export const gatherUpdates = () => gather('updates');
export const gatherRoadmap = () => gather('roadmap');

if (process.argv[1] === fileURLToPath(import.meta.url))
  console.log(`Gathered ${gatherUpdates().length} update(s) from worker/updates/ and ${gatherRoadmap().length} roadmap item(s) from worker/roadmap/.`);
