import { test } from 'node:test';
import assert from 'node:assert/strict';
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
