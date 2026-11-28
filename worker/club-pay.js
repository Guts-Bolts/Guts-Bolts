// Paid Boltworks Club memberships and Bolts packs, on the game server's side.
//
// The money part is in stripe.js: Stripe tells the Worker a payment went through (the webhook,
// signature-checked), and the Worker hands the job to this server on https://gb.internal/stripe-event,
// an address only the Worker can reach. This file:
//   - adds bought Bolts (once per payment, even if Stripe sends the same event twice), paid by card
//     or wallet through Stripe, or with Bitcoin through Blockonomics (bitcoin.js uses the same address),
//   - takes bought Bolts back when a card payment is refunded or charged back (never twice),
//   - keeps each person's paid memberships in u.subs ({ <Stripe subscription id>: { tier, at } }),
//     so clubOf (server.js) shows their badge, and takes it away when the membership ends,
//   - answers club.manage: a link to Stripe's own page for changing or cancelling a membership,
//   - cancels the Stripe membership when an account is banned for good or deleted (cancelStripeSubs),
//   - tells checkout whether someone gets the early-list discount (job kind 'early'; for good),
//   - Boltworks Club items: catalog items staff mark "Boltworks Club: <tier>" (a.meta.clubGift) are
//     free for members of that tier or a higher one, and are handed out by themselves (giveClubItems).
//     Members keep them after their membership ends, like any other gift.
//
// Staff-given tiers (signed grants, PR #257) still work as before; someone gets whichever is best.
// Only the Cloudflare server takes payments, so the C++ server (src/server) has nothing to mirror.

import { STRIPE_API_VERSION, formBody } from './stripe.js';

const TIERS = ['obc', 'tbc', 'bwc'];   // best first, like CLUB_TIERS in server.js
const NAMES = { bwc: 'Boltworks Club', tbc: 'Turbo Boltworks Club', obc: 'Overclocked Boltworks Club' };
const RANK = { bwc: 1, tbc: 2, obc: 3 };
// Does someone with tier `have` count as a member of tier `need` or higher?
export const clubAtLeast = (have, need) => !!RANK[need] && (RANK[have] || 0) >= RANK[need];
// When paid memberships launched (PR #429 merged, 2026-11-24 23:53 UTC): the early list closed then.
export const EARLY_LIST_CLOSED = 1795564380;
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
      // stripe:<checkout session> for cards and wallets, blockonomics:<intent> for Bitcoin (bitcoin.js).
      const bitcoin = typeof job.ref === 'string' && /^blockonomics:pi_[A-Za-z0-9]{8,80}$/.test(job.ref);
      if (!Number.isInteger(n) || n <= 0 || n > 100000 || typeof job.ref !== 'string' || !(job.ref.startsWith('stripe:') || bitcoin)) return fail('Bad Bolts job.');
      if (this.hasRef(u, job.ref)) return okay({ already: true });
      this.add(u, n, 'Bought ' + n + ' Bolts' + (bitcoin ? ' with Bitcoin' : ''), job.ref);
      this.notify(u, 'bolts', 'Thanks! ' + n + ' Bolts were added to your wallet.');
      this.flush();
      return okay();
    }
    // A card payment for Bolts was refunded or charged back (stripe.js takeBackWork): take them back.
    // job.bolts is how many should be gone in total, so repeats and a second partial refund only
    // take the difference. The balance can go below zero if they were already spent.
    if (job.kind === 'boltsBack') {
      const n = Number(job.bolts), pi = String(job.pi || '');
      if (!Number.isInteger(n) || n <= 0 || n > 100000 || !/^pi_\w+$/.test(pi)) return fail('Bad take-back job.');
      const tag = 'stripeback:' + pi;
      const taken = u.ledger.reduce((t, e) => t + (typeof e[3] === 'string' && e[3].startsWith(tag + ':') ? -e[0] : 0), 0);
      const more = n - taken;
      if (more <= 0) return okay({ already: true });
      const chargeback = job.why === 'chargeback';
      this.add(u, -more, (chargeback ? 'Charged back: ' : 'Refunded: ') + more + ' bought Bolts taken back', tag + ':' + (taken + more));
      this.notify(u, 'bolts', chargeback
        ? 'A Bolts payment was charged back by your bank, so ' + more + ' Bolts were taken back. Contact support if this is a mistake.'
        : 'Your Bolts payment was refunded, so ' + more + ' Bolts were taken back.');
      this.flush();
      return okay({ taken: more });
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
      if (after) this.giveClubItems(u);
      else if (!after && before) this.notify(u, 'club', 'Your ' + NAMES[before] + ' membership has ended. Thanks for supporting Guts&Bolts!');
      this.saveUser(u);
      this.flush();
      return okay();
    }
    if (job.kind === 'early') return okay({ early: this.earlyDiscountFor(u) });
    return fail('Unknown job.');
  };

  // The early-list discount: people who registered on the Bolts page teaser (boltworks.register)
  // before memberships launched (EARLY_LIST_CLOSED). It's for good: every membership they start
  // gets the coupon (STRIPE_COUPON_EARLY; a "forever" coupon keeps it on every renewal).
  P.earlyDiscountFor = function (u) {
    if (!u || !String(this.env && this.env.STRIPE_COUPON_EARLY || '').trim()) return false;
    return this.getMeta('boltworksWaitlist', []).some((e) => e.id === u.id && (e.at || 0) < EARLY_LIST_CLOSED);
  };

  // Stop someone's paid memberships: their tier goes now, and Stripe is told to cancel (no more charges).
  // For accounts banned for good or deleted. Answers how many it stopped.
  P.cancelStripeSubs = function (u, why) {
    const t = Math.floor(Date.now() / 1000), key = String(this.env && this.env.STRIPE_SECRET_KEY || '').trim();
    let n = 0;
    for (const [sub, s] of Object.entries(u.subs || {})) {
      if (!s || !s.tier) continue;
      u.subs[sub] = { tier: '', at: Math.max(t, s.at || 0) };
      n++;
      if (!key) { console.log('Stripe: no key, so ' + sub + ' must be cancelled by hand (' + why + ')'); continue; }
      this.trackBackgroundTask(fetch('https://api.stripe.com/v1/subscriptions/' + encodeURIComponent(sub), {
        method: 'DELETE', headers: { authorization: 'Bearer ' + key, 'stripe-version': STRIPE_API_VERSION },
      }).then(async (r) => {
        if (!r.ok) throw new Error((await r.json().catch(() => ({}))).error?.message || 'HTTP ' + r.status);
        console.log('Stripe: cancelled ' + sub + ' (' + why + ')');
      }), 'Stripe cancel ' + sub + ' (' + why + ')');
    }
    if (n) this.saveUser(u);
    return n;
  };

  // Hand out every Boltworks Club item someone's tier gets (only.length: just those items).
  // Answers how many they got.
  P.giveClubItems = function (u, only) {
    const tier = this.clubOf(u);
    if (!tier || !u.userId || u.userId <= 0 || u.banned) return 0;
    let n = 0;
    for (const a of only || this.assets.values()) {
      const need = a && a.meta && a.meta.clubGift;
      if (!need || !clubAtLeast(tier, need) || a.limited || u.owned.includes(a.id)) continue;
      u.owned.push(a.id);
      this.giveBundle(u, a);
      a.sales = (a.sales || 0) + 1;
      this.saveAsset(a);
      this.notify(u, 'gift', 'Boltworks Club gave you ' + a.name + '!', a.id);
      n++;
    }
    if (n) this.saveUser(u);
    return n;
  };

  // Staff made an item a Boltworks Club item: give it to everyone who already qualifies.
  P.giveClubItemToMembers = function (a) {
    let n = 0;
    for (const u of this.users.values()) n += this.giveClubItems(u, [a]);
    return n;
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
