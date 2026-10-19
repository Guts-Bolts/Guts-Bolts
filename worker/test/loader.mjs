// Lets the worker's code load in plain Node for the tests: Cloudflare's
// "cloudflare:workers" module and the files the worker bundles (the crypto
// WebAssembly, the example games, pictures) are swapped for stand-ins.
export async function resolve(spec, ctx, next) {
  if (spec === 'cloudflare:workers')
    return { url: 'data:text/javascript,export class DurableObject { constructor(ctx, env) { this.ctx = ctx; this.env = env; } }', shortCircuit: true };
  if (spec.endsWith('gbcrypto.wasm')) {   // the real one, so hashes and codes work
    const file = new URL(spec, ctx.parentURL).pathname;
    return { url: 'data:text/javascript,' + encodeURIComponent('import { readFileSync } from "node:fs"; export default new WebAssembly.Module(readFileSync('
      + JSON.stringify(file) + '));'), shortCircuit: true };
  }
  if (spec.endsWith('.gbemotes')) {   // the real emotes, as text (the server seeds the catalog from them)
    const file = new URL(spec, ctx.parentURL).pathname;
    return { url: 'data:text/javascript,' + encodeURIComponent('import { readFileSync } from "node:fs"; export default readFileSync('
      + JSON.stringify(file) + ', "utf8");'), shortCircuit: true };
  }
  if (/\.(wasm|gbscene|png|jpg|bin)$/.test(spec)) return { url: 'data:text/javascript,export default null', shortCircuit: true };
  return next(spec, ctx);
}
