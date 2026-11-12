// Profile backgrounds: put a picture behind your avatar on your profile, like a room for it to
// stand in. Any decal from the Library works (decals are free, and a staff check looks at every
// one before it shows), so creators can make backgrounds for everyone by uploading decals.
// src/server/ServerBackgrounds.cpp has the same rules.
//
//   profile.background {decal}   -> {background}    set yours ('' takes it off); the decal's id or number
// The profile request answers with `background` ({id, num, name} or null) for the person shown.
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });

export function installBackgrounds(Cls) {
  const P = Cls.prototype;

  // Someone's background, if it's still a decal that passed its staff check.
  P.backgroundOf = function (u) {
    const a = u && u.background ? this.assets.get(u.background) : null;
    return a && a.kind === 'decal' && !a.review ? { id: a.id, num: a.num || 0, name: a.name } : null;
  };

  P.backgroundOp = function (name, me, args) {
    if (me.userId === 0) return fail('Sign up to pick a profile background.');
    const want = typeof args.decal === 'string' ? args.decal.trim().replace(/^gb:/, '') : String(args.decal ?? '');
    if (!want) {
      delete me.background;
      this.saveUser(me);
      return okay({ background: null });
    }
    const a = this.assets.get(this.assets.resolve(want));
    if (!a || a.kind !== 'decal') return fail('That isn\'t a decal. Pick a decal from the Library (or upload your own).');
    if (a.review) return fail('That decal is still waiting for its staff check.');
    me.background = a.id;
    this.saveUser(me);
    return okay({ background: this.backgroundOf(me) });
  };
}
