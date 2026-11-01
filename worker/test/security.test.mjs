import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { withSecurityHeaders } from '../security.js';

test('HTML responses receive a restrictive CSP and baseline security headers', () => {
  const request = new Request('https://gutsandbolts.net/');
  const response = withSecurityHeaders(new Response('<!doctype html>', {
    headers: { 'content-type': 'text/html; charset=utf-8' },
  }), request);
  const csp = response.headers.get('content-security-policy');
  assert.ok(csp);
  assert.match(csp, /script-src 'self'/);
  assert.match(csp, /script-src-attr 'none'/);
  assert.match(csp, /object-src 'none'/);
  assert.match(csp, /frame-ancestors 'none'/);
  assert.doesNotMatch(csp, /script-src[^;]*unsafe-inline/);
  assert.equal(response.headers.get('x-content-type-options'), 'nosniff');
  assert.equal(response.headers.get('x-frame-options'), 'DENY');
  assert.equal(response.headers.get('referrer-policy'), 'strict-origin-when-cross-origin');
  assert.match(response.headers.get('strict-transport-security'), /includeSubDomains/);
});

test('non-HTML responses get baseline headers but no document CSP', () => {
  const response = withSecurityHeaders(new Response('{}', {
    headers: { 'content-type': 'application/json' },
  }), new Request('https://gutsandbolts.net/api'));
  assert.equal(response.headers.get('content-security-policy'), null);
  assert.equal(response.headers.get('x-content-type-options'), 'nosniff');
});

test('workers.dev hosts do not receive HSTS for the Guts and Bolts domain', () => {
  const response = withSecurityHeaders(new Response('ok'), new Request('https://project-guts-and-bolts.workers.dev/'));
  assert.equal(response.headers.get('strict-transport-security'), null);
});

test('homepage uses an external module instead of inline scripts', () => {
  const html = readFileSync(new URL('../../website/index.html', import.meta.url), 'utf8');
  assert.match(html, /<script type="module" src="\/home\.js"><\/script>/);
  assert.doesNotMatch(html, /<script(?![^>]*\bsrc=)[^>]*>/i);
});

test('browser signing key persistence encrypts before writing to localStorage', () => {
  const source = readFileSync(new URL('../../website/app/gb.js', import.meta.url), 'utf8');
  assert.match(source, /generateKey\(\{ name: 'AES-GCM', length: 256 \}, false, \['encrypt', 'decrypt'\]\)/);
  assert.match(source, /localStorage\.setItem\(KEY, 'v1\.'/);
  assert.doesNotMatch(source, /localStorage\.setItem\(KEY, hex\(secret\)\)/);
});
