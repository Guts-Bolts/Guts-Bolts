// node --import ./worker/test/register.mjs --test worker/test/
import { register } from 'node:module';
register('./loader.mjs', import.meta.url);

// The update log is gathered from worker/updates/ first (Cloudflare does the same before a deploy).
import { gatherUpdates } from '../gather-updates.mjs';
gatherUpdates();
