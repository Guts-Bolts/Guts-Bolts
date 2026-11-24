// Paid Boltworks Club memberships and Bolts packs, on the game server's side.
//
// The money part is in stripe.js: Stripe tells the Worker a payment went through (the webhook,
// signature-checked), and the Worker hands the job to this server on https://gb.internal/stripe-event,
// an address only the Worker can reach. This file:
//   - adds bought Bolts (once per payment, even if Stripe sends the same event twice),
//   - keeps each person's paid memberships in u.subs ({ <Stripe subscription id>: { tier, at } }),
//     so clubOf (server.js) shows their badge, and takes it away when the membership ends,
//   - answers club.manage: a link to Stripe's own page for changing or cancelling a membership.
//
// Staff-given tiers (signed grants, PR #257) still work as before; someone gets whichever is best.
// Only the Cloudflare server takes payments, so the C++ server (src/server) has nothing to mirror.

import { STRIPE_API_VERSION, formBody } from './stripe.js';

const TIERS = ['obc', 'tbc', 'bwc'];   // best first, like CLUB_TIERS in server.js
const NAMES = { bwc: 'Boltworks Club', tbc: 'Turbo Boltworks Club', obc: 'Overclocked Boltworks Club' };
const okay = (o = {}) => Object.assign({ ok: true }, o);
const fail = (error) => ({ ok: false, error });

// Where Stripe's page sends people back to: this site, or a local test copy.
export function returnUrl(origin) {
  const o = String(origin || '');
  return (/^https:\/\/([a-z0-9-]+\.)*gutsandbolts\.net$/.test(o) || /^http:\/\/(localhost|127\.0\.0\.1)(:\d{1,5})?$/.test(o)
    ? o : 'https://gutsandbolts.net') + '/app/#/bolts';
}

export function installClubPay(Server) {
  const P = Server.prototype;

  // The best tier someone pays for right now ('' if none).
  P.paidTierOf = function (u) {
    const have = new Set(Object.values(u && u.subs || {}).map((s) => s && s.tier).filter(Boolean));
    return TIERS.find((t) => have.has(t)) || '';
  };

  // A job from the webhook (stripe.js stripeWork). Answers { ok } or { ok: false, retry? }.
  P.stripeEvent = function (job) {
    if (!job || typeof job !== 'object') return fail('Bad job.');
    const u = /^\d{1,12}$/.test(String(job.user || '')) ? this.findUserId(Number(job.user)) : null;
    if (!u) return fail('No account with user number ' + job.user + '.');   // (no retry: it won't appear later)
    if (job.kind === 'bolts') {
      const n = Number(job.bolts);
      if (!Number.isInteger(n) || n <= 0 || n > 100000 || typeof job.ref !== 'string' || !job.ref.startsWith('stripe:')) return fail('Bad Bolts job.');
      if (this.hasRef(u, job.ref)) return okay({ already: true });
      this.add(u, n, 'Bought ' + n + ' Bolts', job.ref);
      this.notify(u, 'bolts', 'Thanks! ' + n + ' Bolts were added to your wallet.');
      this.flush();
      return okay();
    }
    if (job.kind === 'club') {
      const sub = String(job.sub || ''), tier = String(job.tier || ''), at = Number(job.at) || 0;
      if (!/^sub_\w+$/.test(sub) || (tier && !TIERS.includes(tier))) return fail('Bad club job.');
      u.subs = u.subs || {};
      const old = u.subs[sub];
      // Stripe doesn't promise to send events in order: never let an older one undo a newer one.
      // (Same second: keep the membership on, since "started" and "paid" often share a second.)
      if (old && (at < old.at || (at === old.at && !tier && old.tier))) return okay({ stale: true });
      const before = this.paidTierOf(u);
      u.subs[sub] = { tier, at };
      if (/^cus_\w+$/.test(String(job.customer || ''))) u.stripeCustomer = String(job.customer);
      const after = this.paidTierOf(u);
      if (after && after !== before) this.notify(u, 'club', 'Welcome to ' + NAMES[after] + '! Your badge is on.');
      else if (!after && before) this.notify(u, 'club', 'Your ' + NAMES[before] + ' membership has ended. Thanks for supporting Guts&Bolts!');
      this.saveUser(u);
      this.flush();
      return okay();
    }
    return fail('Unknown job.');
  };

  // club.manage {origin?}: Stripe's own page for changing or cancelling. The link is made after
  // the reply (clubPortalAnswer), since it needs a call to Stripe.
  P.clubManageOp = function (me, args) {
    if (!me.stripeCustomer) return fail('You haven\'t bought a membership with a card yet.');
    if (!String(this.env && this.env.STRIPE_SECRET_KEY || '').trim()) return fail('Payments aren\'t set up on this server.');
    return okay({ stripePortal: { customer: me.stripeCustomer, returnUrl: returnUrl(args && args.origin) } });
  };

  P.clubPortalAnswer = async function (answer) {
    const job = answer.stripePortal;
    try {
      const r = await fetch('https://api.stripe.com/v1/billing_portal/sessions', {
        method: 'POST',
        headers: { authorization: 'Bearer ' + String(this.env.STRIPE_SECRET_KEY).trim(), 'stripe-version': STRIPE_API_VERSION,
          'content-type': 'application/x-www-form-urlencoded' },
        body: formBody({ customer: job.customer, return_url: job.returnUrl }),
      });
      const s = await r.json().catch(() => ({}));
      if (!r.ok || typeof s.url !== 'string') {
        const why = s.error && typeof s.error.message === 'string' ? ' Stripe says: ' + s.error.message.slice(0, 300) : '';
        return fail('Couldn\'t open the membership page.' + why);
      }
      return okay({ url: s.url });
    } catch {
      return fail('Couldn\'t reach Stripe. Try again in a bit.');
    }
  };
}
