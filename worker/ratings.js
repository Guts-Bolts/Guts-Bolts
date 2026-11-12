// Content levels: a game's creator answers a few plain questions about what's in it, and the answers
// give it a level (Mild, Moderate, Strong or Extreme). src/server/ServerRatings.cpp has the same rules.
//
// Everyone on Guts&Bolts is an adult (18+, Terms section 2), so these are NOT age ratings and never
// say a game is fine for kids. They tell adults how intense a game is, so they can skip what they
// don't want to see.
//
// Why questions and not "pick a level": two creators with the same kind of game get the same
// level, and players can see exactly why a game got its level.
//
// How it works:
//   - The creator (or staff) answers the questions on the game's Configure page. The highest level
//     any answer needs is the rating. Changing the answers changes the rating straight away.
//   - Every change is written in the game's rating history: when, who, from what to what. Staff
//     can fix a wrong rating the same way (the history then says staff changed it).
//   - Games nobody has rated yet say "Not rated" (and show everywhere, like before).
//   - Each player can pick the highest level they want to see (Settings). Games above it
//     are left out of game lists for them. Their own games always show.
//
// Requests:
//   game.rating      {game}               -> {rating, answers, history, questions}   (anyone who can see the game)
//   game.rate        {game, answers}      -> {rating, history}                        (the creator or staff)
//   rating.limit     {limit?}             -> {limit}                                  your highest rating ('' = show all)
import { clean } from './banlists.js';

// The questions. Each answer says how intense it is (0 = mild ... 3 = extreme).
// (Keep in step with kQuestions in src/server/ServerRatings.cpp.)
export const QUESTIONS = [
  { id: 'violence', text: 'Is there fighting or weapons?', answers: [
    { v: 'none', label: 'No', level: 0 },
    { v: 'cartoon', label: 'Cartoon fighting, no blood (bonk, knock out, fall apart)', level: 0 },
    { v: 'realistic', label: 'Realistic weapons, or some blood', level: 2 },
    { v: 'gore', label: 'Lots of blood or gore', level: 3 },
  ] },
  { id: 'scary', text: 'Is it scary?', answers: [
    { v: 'none', label: 'No', level: 0 },
    { v: 'mild', label: 'A bit spooky (dark places, jump scares that are more funny than scary)', level: 1 },
    { v: 'intense', label: 'Real horror (meant to scare people)', level: 2 },
  ] },
  { id: 'language', text: 'Does the game itself swear? (Not counting what players type in chat.)', answers: [
    { v: 'none', label: 'No', level: 0 },
    { v: 'mild', label: 'Mild words or crude jokes', level: 1 },
    { v: 'strong', label: 'Strong swearing', level: 3 },
  ] },
  { id: 'romance', text: 'Is there dating or romance?', answers: [
    { v: 'none', label: 'No', level: 0 },
    { v: 'mild', label: 'Holding hands, dating, a kiss', level: 1 },
    { v: 'suggestive', label: 'Suggestive things', level: 3 },
  ] },
  { id: 'substances', text: 'Are alcohol, smoking or drugs in it?', answers: [
    { v: 'none', label: 'No', level: 0 },
    { v: 'mentioned', label: 'Talked about or seen in the background', level: 2 },
    { v: 'used', label: 'Characters use them', level: 3 },
  ] },
  { id: 'chance', text: 'Can players spend Bolts on random prizes?', answers: [
    { v: 'none', label: 'No', level: 0 },
    { v: 'shown', label: 'Yes, and the chances are shown', level: 2 },
    { v: 'gambling', label: 'It plays like a casino (betting, slots)', level: 3 },
  ] },
];
export const RATINGS = ['mild', 'moderate', 'strong', 'extreme'];
export const RATING_NAMES = { mild: 'Mild', moderate: 'Moderate', strong: 'Strong', extreme: 'Extreme' };
export const kHistoryKept = 50;

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const levelOf = (rating) => Math.max(0, RATINGS.indexOf(rating));

// The rating some answers give. Every question needs an answer: '' if one is missing or wrong.
export function ratingFor(answers) {
  if (!answers || typeof answers !== 'object') return '';
  let level = 0;
  for (const q of QUESTIONS) {
    const a = q.answers.find((x) => x.v === answers[q.id]);
    if (!a) return '';
    level = Math.max(level, a.level);
  }
  return RATINGS[level];
}

// Can `me` see a game with this rating in lists? (Their own games always.)
export function ratingAllowed(a, me) {
  if (!a || a.kind !== 'game' || !me || !me.ratingLimit || a.creator === me.id) return true;
  if (!a.rating) return true;   // not rated yet
  return levelOf(a.rating) <= levelOf(me.ratingLimit);
}

export function installRatings(Cls) {
  const P = Cls.prototype;

  P.ratingHistory = function (a) {
    return (a.ratingLog || []).map((h) => {
      const u = this.users.get(h.by);
      return { at: h.at, from: h.from || '', to: h.to, staff: !!h.staff, by: u ? { id: u.id, name: u.name, userId: u.userId } : null };
    });
  };

  P.ratingOp = function (name, me, args) {
    if (name === 'rating.limit') {
      if ('limit' in args) {
        if (me.userId === 0) return fail('Sign up to pick a rating limit.');
        const limit = str(args, 'limit');
        if (limit && !RATINGS.includes(limit)) return fail('Pick Mild, Moderate, Strong, Extreme or "show everything".');
        me.ratingLimit = limit;
        this.saveUser(me);
      }
      return okay({ limit: me.ratingLimit || '' });
    }
    const a = this.assets.get(this.assets.resolve(str(args, 'game')));
    if (!a || a.kind !== 'game' || !this.canPlay(a, me)) return fail('That game doesn\'t exist (any more).');
    if (name === 'game.rating')
      return okay({ rating: a.rating || '', answers: a.ratingAnswers || {}, history: this.ratingHistory(a), questions: QUESTIONS });

    if (name === 'game.rate') {
      const staff = this.isStaff(me);
      if (a.creator !== me.id && !staff) return fail('Only the game\'s creator can answer its content questions.');
      const answers = {};
      for (const q of QUESTIONS) answers[q.id] = typeof (args.answers || {})[q.id] === 'string' ? clean(args.answers[q.id], 20) : '';
      const rating = ratingFor(answers);
      if (!rating) return fail('Answer every question first.');
      const from = a.rating || '';
      a.ratingAnswers = answers;
      if (rating !== from) {
        a.rating = rating;
        a.ratingLog = [{ at: now(), by: me.id, from, to: rating, staff: staff && a.creator !== me.id }, ...(a.ratingLog || [])].slice(0, kHistoryKept);
        if (staff && a.creator !== me.id)
          this.notify(this.users.get(a.creator), 'rating', 'Staff changed the content level of ' + a.name + ' to ' + RATING_NAMES[rating] + '.', a.id);
      }
      this.saveAsset(a);
      return okay({ rating, history: this.ratingHistory(a) });
    }
    return fail('Unknown request.');
  };
}
