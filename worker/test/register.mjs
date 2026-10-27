// node --import ./worker/test/register.mjs --test worker/test/
import { register } from 'node:module';
register('./loader.mjs', import.meta.url);
