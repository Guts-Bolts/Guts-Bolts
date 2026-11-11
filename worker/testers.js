// Early testers: before a game goes public, its creator picks a few people to play it first and say
// what they think (src/server/ServerTesters.cpp has the same rules).
//
// How it works:
//   - A game's access can be 'testers' (next to public, friends and private). Then only the creator,
//     staff and the people on its tester list can find and play it.
//   - The creator adds up to kMostTesters people by name or number. Each one gets a notification.
//   - A tester can leave one note: 1 to 5 stars and a few words. Sending again replaces their note.
//     Only the creator (and staff) read the notes, so testers can be honest.
//   - When the creator makes the game public, every tester is told it's out.
//   - The list stays when the game goes public, so the creator can go back to 'testers' for the next update.
//
// Requests (all need a signed-up account):
//   game.testers      {game}                     -> {testers, notes}    (the creator or staff)
//   game.addTester    {game, who}                -> {testers}           (the creator)
//   game.removeTester {game, who}                -> {testers}           (the creator; their note goes too)
//   game.testerNote   {game, stars, text}        -> {note}              (a tester)
//   game.amTester     {game}                     -> {tester, note}      am I on this game's tester list?
import { clean } from './banlists.js';

export const kMostTesters = 25, kNoteText = 500;

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);

export const isTester = (a, me) => !!a && !!me && Array.isArray(a.testers) && a.testers.includes(me.id);

export function installTesters(Cls) {
  const P = Cls.prototype;

  P.testerPerson = function (id) {
    const u = this.users.get(id);
    return u ? { id: u.id, name: u.name, username: u.username || '', userId: u.userId } : { id, name: '?', username: '', userId: 0 };
  };
  P.testerList = function (a) {
    const notes = a.testerNotes || {};
    return (a.testers || []).map((id) => Object.assign(this.testerPerson(id), { noted: !!notes[id] }));
  };
  P.testerNotesOf = function (a) {
    const notes = a.testerNotes || {};
    return Object.keys(notes).map((id) => ({ who: this.testerPerson(id), stars: notes[id].stars, text: notes[id].text, at: notes[id].at }))
      .sort((x, y) => y.at - x.at);
  };
  // The game went from testers-only to public: tell everyone who tested it.
  P.testersLaunched = function (a, me) {
    for (const id of a.testers || [])
      this.notify(this.users.get(id), 'testers', a.name + ', the game you tested for ' + me.name + ', is out for everyone now.', a.id);
  };

  P.testerOp = function (name, me, args) {
    if (me.userId === 0) return fail('Sign up to test games.');
    const a = this.assets.get(this.assets.resolve(str(args, 'game')));
    if (!a || a.kind !== 'game') return fail('That game doesn\'t exist (any more).');
    const mine = a.creator === me.id;

    if (name === 'game.amTester') {
      const n = (a.testerNotes || {})[me.id];
      return okay({ tester: isTester(a, me), note: n ? { stars: n.stars, text: n.text, at: n.at } : null });
    }
    if (name === 'game.testerNote') {
      if (!isTester(a, me)) return fail('Only the game\'s testers can leave a tester note.');
      const stars = typeof args.stars === 'number' ? Math.trunc(args.stars) : 0;
      if (stars < 1 || stars > 5) return fail('Pick 1 to 5 stars.');
      const text = clean(str(args, 'text'), kNoteText, true);
      if (!text) return fail('Write a few words: what was fun, and what wasn\'t?');
      const had = !!(a.testerNotes || {})[me.id];
      a.testerNotes = Object.assign({}, a.testerNotes || {}, { [me.id]: { stars, text, at: now() } });
      this.saveAsset(a);
      if (!had) this.notify(this.users.get(a.creator), 'testers', me.name + ' left a tester note on ' + a.name + ' (' + stars + ' stars).', a.id);
      return okay({ note: a.testerNotes[me.id] });
    }

    if (!mine && !(this.isStaff(me) && name === 'game.testers')) return fail('Only the game\'s creator can choose its testers.');
    if (name === 'game.testers') return okay({ testers: this.testerList(a), notes: this.testerNotesOf(a), most: kMostTesters });

    const who = this.findPerson(args.who);
    if (!who) return fail('There\'s nobody with that name or number.');
    if (name === 'game.addTester') {
      if (who.id === me.id) return fail('You made it, so you can always play it.');
      if (who.userId === 0) return fail('Guests can\'t be testers. Ask them to sign up first.');
      const list = a.testers || [];
      if (list.includes(who.id)) return fail(who.name + ' is already a tester.');
      if (list.length >= kMostTesters) return fail('A game can have ' + kMostTesters + ' testers at most. Take someone off first.');
      a.testers = [...list, who.id];
      this.saveAsset(a);
      this.notify(who, 'testers', me.name + ' picked you to test ' + a.name + ' before anyone else. Play it and leave a note!', a.id);
      return okay({ testers: this.testerList(a) });
    }
    if (name === 'game.removeTester') {
      if (!(a.testers || []).includes(who.id)) return fail(who.name + ' isn\'t a tester of this game.');
      a.testers = a.testers.filter((x) => x !== who.id);
      if (a.testerNotes && a.testerNotes[who.id]) { const n = Object.assign({}, a.testerNotes); delete n[who.id]; a.testerNotes = n; }
      this.saveAsset(a);
      return okay({ testers: this.testerList(a) });
    }
    return fail('Unknown request.');
  };
}
