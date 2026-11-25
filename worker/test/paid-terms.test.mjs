// The Terms of Service match what the site sells (Boltworks Club memberships and Bolts packs, stripe.js).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const app = readFileSync(new URL('../../website/app/app.js', import.meta.url), 'utf8');
const terms = app.slice(app.indexOf('pages.terms = '), app.indexOf('</div>`);', app.indexOf('pages.terms = ')));

test('the Terms no longer say nothing is for sale', () => {
  assert.doesNotMatch(terms, /no paid subscriptions/);
  assert.doesNotMatch(terms, /can't buy them with real money/);
  assert.doesNotMatch(terms, /If paid features are ever added/);
});

test('the Terms explain paying, renewing, cancelling and refunds', () => {
  assert.match(terms, /<h3>Paying with a card<\/h3>/);
  for (const words of [/Stripe/, /renew by themselves/, /Cancel any time/, /Bought Bolts are final/, /charged back/, /can't cash them out/])
    assert.match(terms, words);
  assert.match(app, /See <a href="#\/terms">the Terms<\/a>, section 4/);   // the Bolts page points here
});
