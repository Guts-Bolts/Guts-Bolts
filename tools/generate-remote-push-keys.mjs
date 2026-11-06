import { generateKeyPairSync, randomBytes } from 'node:crypto';

const b64url = (value) => Buffer.from(value).toString('base64url');
const { publicKey, privateKey } = generateKeyPairSync('ec', { namedCurve: 'prime256v1' });
const spki = publicKey.export({ type: 'spki', format: 'der' });
const rawPublic = spki.subarray(spki.length - 65);
const pkcs8Private = privateKey.export({ type: 'pkcs8', format: 'der' });
const encryptionKey = randomBytes(32);

console.log('\nGuts & Bolts remote push keys\n');
console.log('REMOTE_VAPID_PUBLIC_KEY (Cloudflare secret):');
console.log(b64url(rawPublic));
console.log('\nREMOTE_VAPID_PRIVATE_KEY (Cloudflare secret):');
console.log(b64url(pkcs8Private));
console.log('\nREMOTE_PUSH_ENCRYPTION_KEY (Cloudflare secret):');
console.log(b64url(encryptionKey));
console.log('\nSet the private values with:');
console.log('  npx wrangler secret put REMOTE_VAPID_PRIVATE_KEY');
console.log('  npx wrangler secret put REMOTE_PUSH_ENCRYPTION_KEY');
console.log('\nSet REMOTE_VAPID_PUBLIC_KEY as a Worker variable, then deploy.');
console.log('Keep this output private. Never commit the private key or encryption key.\n');
