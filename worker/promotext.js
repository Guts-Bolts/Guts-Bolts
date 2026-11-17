// Promotional text: a short line on a game's listing, separate from its name, for what's going on
// right now ("Halloween event: 2x candy!", "New map: the docks", "Ranked mode this weekend").
// Creators no longer have to change the game's name to tell people about an update.
// (src/server/ServerPromoText.cpp has the same rules.)
//
//   - Up to kPromoLength letters, one line, through the same filter as everything else.
//   - It ends by itself after 1 to kPromoMostDays days (7 if not said), so old news doesn't hang
//     around. Clearing it (text '') takes it off at once.
//   - At most kPromoChangesPerDay changes a day, so a listing can't flicker.
//   - Shown on game cards (Home, Games, search) and on the game's page, under the name.
//
//   game.promo {game, text, days?} -> {promo: {text, until} | null}   (the game's creator, or staff)
//   Every game in a list answers with promo: {text, until} while it's running (publicAsset).
import { filterText } from './textfilter.js';

export const kPromoLength = 80, kPromoMostDays = 30, kPromoDefaultDays = 7, kPromoChangesPerDay = 10;

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const now = () => Math.floor(Date.now() / 1000);

// One line: no line breaks or control characters, spaces squeezed, at most kPromoLength.
export function cleanPromo(text) {
  return String(typeof text === 'string' ? text : '').replace(/[\u0000-\u001f\u007f]+/g, ' ').replace(/\s+/g, ' ').trim().slice(0, kPromoLength).trim();
}

// The game's promotional text while it's running, else null.
export function activePromo(a, t = now()) {
  const p = a && a.promo;
  return p && p.text && p.until > t ? { text: p.text, until: p.until } : null;
}

export function installPromoText(Cls) {
  const P = Cls.prototype;

  // Every game answer carries its running promotional text.
  const base = P.publicAsset;
  P.publicAsset = function (a, me) {
    const r = base.call(this, a, me);
    if (a && a.kind === 'game') { const p = activePromo(a); if (p) r.promo = p; }
    return r;
  };

  P.promoTextOp = function (name, me, args) {
    const a = this.assets.get(this.assets.resolve(typeof args.game === 'string' ? args.game : ''));
    if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
    if (a.creator !== me.id && !this.isStaff(me)) return fail('Only the game\'s creator can change its promotional text.');
    const t = now(), day = Math.floor(t / 86400);
    const log = a.promoLog && a.promoLog.day === day ? a.promoLog : { day, n: 0 };
    if (log.n >= kPromoChangesPerDay && !this.isStaff(me)) return fail('That\'s ' + kPromoChangesPerDay + ' changes today. Try again tomorrow.');
    const text = cleanPromo(args.text);
    if (!text) {
      a.promo = null;
    } else {
      if (filterText(text) !== text) return fail('The promotional text has words that aren\'t allowed.');
      if (text.toLowerCase() === String(a.name || '').toLowerCase()) return fail('Say something new: that\'s already the game\'s name.');
      const asked = typeof args.days === 'number' && Number.isFinite(args.days) ? Math.round(args.days) : kPromoDefaultDays;
      const days = Math.min(Math.max(asked, 1), kPromoMostDays);
      a.promo = { text, until: t + days * 86400, set: t };
    }
    log.n++;
    a.promoLog = log;
    this.saveAsset(a);
    return okay({ promo: activePromo(a, t) });
  };
}
