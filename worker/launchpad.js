// Launchpad: a free program that helps creators make ambitious games. A creator applies with a game,
// staff pick the ones they want to help, and picked games get:
//   - a spot on the "Launchpad" shelf on the Games page, and a Launchpad label on the game's page
//   - more Fyxa cloud steps a day while they're in the program (at least the top club level)
//   - a help thread with staff on their application: ask about networking, speed, security, design...
//
// It runs in two stages, like a short course: "Build and test" (make it work, test it with people),
// then "Launch and grow" (make it public and get players). Staff move a game along, and finally to
// "Graduated" (its label stays, the extra help stops).
//
// It's free: nobody pays and nobody gets paid. (Money prizes or grants would need real money and
// rules for paying people, so they're not part of it.) Only on the main website: the C++ server
// says it isn't there (src/server/Server.cpp).
//
// Requests (signed-up accounts):
//   launchpad.info     {}                            -> {round, mine, help, stages}
//   launchpad.apply    {game, pitch, help}           -> {app}
//   launchpad.withdraw {id}                          -> {}                (a waiting application of yours)
//   launchpad.note     {id, text}                    -> {app}             your help thread with staff (or staff's)
//   launchpad.games    {}                            -> {games}           the shelf (anyone)
// Staff:
//   launchpad.list     {status?}                     -> {apps, round}
//   launchpad.decide   {id, accept, note}            -> {app}
//   launchpad.stage    {id, stage}                   -> {app}             build / launch / graduated
//   launchpad.round    {name, closes, open}          -> {round}           the round people apply to
import { clean } from './banlists.js';

export const HELP = {
  networking: 'Multiplayer and networking', speed: 'Making it run fast', security: 'Stopping cheaters',
  design: 'Game design and fun', scripting: 'Scripting', art: 'Models, art and sound', launch: 'Getting players',
};
export const STAGES = { build: 'Build and test', launch: 'Launch and grow', graduated: 'Graduated' };
export const kPitchMin = 40, kPitch = 1500, kNote = 1000, kNotesKept = 100, kAppsKept = 1000, kRoundName = 60;

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const newId = () => 'lp' + Array.from(crypto.getRandomValues(new Uint8Array(6)), (b) => b.toString(16).padStart(2, '0')).join('');

// Is this game in the program right now (getting the extra help)?
export const inLaunchpad = (a) => !!(a && a.launchpad && (a.launchpad.stage === 'build' || a.launchpad.stage === 'launch'));

export function installLaunchpad(Cls) {
  const P = Cls.prototype;

  P.launchpadData = function () {
    if (!this._launchpad) this._launchpad = this.getMeta('launchpad', { round: { name: 'Launchpad', open: true, closes: 0 }, apps: [] });
    return this._launchpad;
  };
  P.launchpadSave = function () {
    this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'launchpad', JSON.stringify(this.launchpadData()));
  };
  P.launchpadRoundOpen = function (t) {
    const r = this.launchpadData().round;
    return !!r.open && (!r.closes || t < r.closes);
  };
  // Does `me` have a game in the program? (Fyxa cloud gives them more steps: fyxa-cloud.js.)
  P.launchpadPerk = function (me) {
    return !!me && this.launchpadData().apps.some((x) => x.by === me.id && x.status === 'accepted' && inLaunchpad(this.assets.get(x.game)));
  };
  P.launchpadApp = function (x, me) {
    const g = this.assets.get(x.game), u = this.users.get(x.by), staff = me && this.isStaff(me);
    return { id: x.id, round: x.round, pitch: x.pitch, help: x.help, at: x.at, status: x.status, note: x.note || '',
      stage: g && g.launchpad ? g.launchpad.stage : '',
      game: g ? { id: g.id, name: g.name, access: g.access || 'public', plays: g.plays || 0 } : { id: x.game, name: '(deleted)' },
      by: u ? { id: u.id, name: u.name, userId: u.userId } : null,
      notes: (staff || (me && me.id === x.by)) ? (x.notes || []).map((n) => {
        const w = this.users.get(n.by);
        return { text: n.text, at: n.at, staff: !!n.staff, by: w ? { id: w.id, name: w.name, userId: w.userId } : null };
      }) : undefined };
  };

  P.launchpadOp = function (name, me, args) {
    const t = now(), data = this.launchpadData(), staff = this.isStaff(me);
    if (name === 'launchpad.games') {
      const games = [...this.assets.values()].filter((a) => a.kind === 'game' && a.launchpad && (a.access || 'public') === 'public' && this.canPlay(a, me))
        .sort((x, y) => (y.launchpad.since || 0) - (x.launchpad.since || 0)).slice(0, 30)
        .map((a) => Object.assign(this.publicAsset(a, me), { launchpadStage: a.launchpad.stage }));
      return okay({ games });
    }
    if (me.userId === 0) return fail('Sign up first.');
    if (name === 'launchpad.info')
      return okay({ round: Object.assign({}, data.round, { open: this.launchpadRoundOpen(t) }), help: HELP, stages: STAGES,
        mine: data.apps.filter((x) => x.by === me.id).map((x) => this.launchpadApp(x, me)), perk: this.launchpadPerk(me) });

    if (name === 'launchpad.apply') {
      if (!this.launchpadRoundOpen(t)) return fail('Launchpad isn\'t taking applications right now. Check back for the next round!');
      const g = this.assets.get(this.assets.resolve(str(args, 'game')));
      if (!g || g.kind !== 'game' || g.creator !== me.id) return fail('Pick one of your own games.');
      if (g.access === 'private' || g.access === 'friends') return fail('Make the game public (or testers only) first, so staff can play it.');
      if (data.apps.some((x) => x.game === g.id && (x.status === 'waiting' || x.status === 'accepted'))) return fail(g.name + ' already has an application.');
      if (data.apps.some((x) => x.by === me.id && x.status === 'waiting')) return fail('You already have an application waiting. One at a time!');
      const pitch = clean(str(args, 'pitch'), kPitch, true);
      if (pitch.length < kPitchMin) return fail('Tell staff a bit more: what is the game, and what makes it special? (' + kPitchMin + ' letters at least)');
      const help = Array.isArray(args.help) ? [...new Set(args.help.filter((h) => typeof h === 'string' && HELP[h]))] : [];
      if (!help.length) return fail('Pick at least one thing you want help with.');
      const app = { id: newId(), round: data.round.name, game: g.id, by: me.id, pitch, help, at: t, status: 'waiting', notes: [] };
      data.apps = [app, ...data.apps].slice(0, kAppsKept);
      this.launchpadSave();
      return okay({ app: this.launchpadApp(app, me) });
    }

    const app = data.apps.find((x) => x.id === str(args, 'id'));
    if (name === 'launchpad.withdraw') {
      if (!app || app.by !== me.id) return fail('That application doesn\'t exist (any more).');
      if (app.status !== 'waiting') return fail('Only a waiting application can be taken back.');
      app.status = 'withdrawn';
      this.launchpadSave();
      return okay({});
    }
    if (name === 'launchpad.note') {
      if (!app || (app.by !== me.id && !staff)) return fail('That application doesn\'t exist (any more).');
      if (app.status !== 'accepted' && app.status !== 'waiting') return fail('This help thread is closed.');
      const text = clean(str(args, 'text'), kNote, true);
      if (!text) return fail('Write something first.');
      const fromStaff = staff && app.by !== me.id;
      app.notes = [...(app.notes || []), { by: me.id, text, at: t, staff: fromStaff }].slice(-kNotesKept);
      const g = this.assets.get(app.game), gameName = g ? g.name : 'your game';
      if (fromStaff) this.notify(this.users.get(app.by), 'launchpad', 'Staff wrote in your Launchpad help thread for ' + gameName + '.', app.id);
      else if (app.decidedBy) this.notify(this.users.get(app.decidedBy), 'launchpad', me.name + ' wrote in the Launchpad help thread for ' + gameName + '.', app.id);
      this.launchpadSave();
      return okay({ app: this.launchpadApp(app, me) });
    }

    if (!staff) return fail('Only staff can do that.');
    if (name === 'launchpad.list') {
      const status = str(args, 'status');
      return okay({ apps: data.apps.filter((x) => !status || x.status === status).slice(0, 200).map((x) => this.launchpadApp(x, me)), round: data.round });
    }
    if (name === 'launchpad.round') {
      const rname = clean(str(args, 'name'), kRoundName) || data.round.name;
      const closes = typeof args.closes === 'number' && args.closes > 0 ? Math.trunc(args.closes) : 0;
      data.round = { name: rname, open: args.open !== false, closes };
      this.launchpadSave();
      this.staffDid(me, 'launchpad', 'Set the Launchpad round to "' + rname + '"' + (data.round.open ? '' : ' (closed)'));
      return okay({ round: data.round });
    }
    if (!app) return fail('That application doesn\'t exist (any more).');
    const g = this.assets.get(app.game);
    if (name === 'launchpad.decide') {
      if (app.status !== 'waiting') return fail('That application was already answered.');
      if (!g) return fail('That game was deleted.');
      const accept = args.accept === true, note = clean(str(args, 'note'), 300, true);
      app.status = accept ? 'accepted' : 'declined';
      app.note = note; app.decidedBy = me.id; app.decidedAt = t;
      if (accept) { g.launchpad = { round: app.round, stage: 'build', since: t }; this.saveAsset(g); }
      this.notify(this.users.get(app.by), 'launchpad', accept
        ? g.name + ' is in Launchpad! Staff will help you build it. Open Create > Launchpad for your help thread.'
        : g.name + ' wasn\'t picked for Launchpad this time.' + (note ? ' Staff said: "' + note + '"' : ' You can apply again next round.'), app.id);
      this.staffDid(me, 'launchpad', (accept ? 'Picked ' : 'Turned down ') + '"' + g.name + '" for Launchpad', app.by);
      this.launchpadSave();
      return okay({ app: this.launchpadApp(app, me) });
    }
    if (name === 'launchpad.stage') {
      const stage = str(args, 'stage');
      if (!STAGES[stage]) return fail('Pick build, launch or graduated.');
      if (app.status !== 'accepted' || !g || !g.launchpad) return fail('That game isn\'t in Launchpad.');
      g.launchpad = Object.assign({}, g.launchpad, { stage });
      this.saveAsset(g);
      this.notify(this.users.get(app.by), 'launchpad', g.name + ' moved to "' + STAGES[stage] + '" in Launchpad.', app.id);
      this.staffDid(me, 'launchpad', 'Moved "' + g.name + '" to ' + STAGES[stage] + ' in Launchpad', app.by);
      return okay({ app: this.launchpadApp(app, me) });
    }
    return fail('Unknown request.');
  };
}
