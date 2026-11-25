// Polls: a question with 2 to 6 answers that people pick one of. They sit on a forum thread
// (forum.post {poll}) or on a group wall post (groups.post {poll}). src/server/ServerPolls.cpp
// has the same rules.
//
//   - The question is up to kPollQuestion bytes, each answer up to kPollOption, both go through
//     the word filter like any other post. Answers that are empty or the same as another are dropped.
//   - A poll closes after 1 day, 3 days, 1 week or 30 days (hours: POLL_HOURS). After that the
//     results stay, but nobody can vote.
//   - One vote each. You can change it, or take it back (option -1), until the poll closes.
//   - Everyone sees the counts. Who picked what is never shown (only "your vote" to yourself).
//
// A poll as kept: { q, options: [text...], votes: {accountId: index}, at, until }
export const kPollQuestion = 120, kPollOption = 60, kPollMostOptions = 6, kPollMostVotes = 100000;
export const POLL_HOURS = [24, 72, 168, 720];
const kPollDefaultHours = 168;

// The poll someone asked for, tidied: { poll } or { error }, or { poll: null } when they didn't ask
// for one. say(text, maxBytes) is the server's filter (server.js `say`).
export function makePoll(input, say, t) {
  if (input === undefined || input === null || input === false || input === '') return { poll: null };
  if (typeof input !== 'object' || Array.isArray(input)) return { error: 'That poll doesn\'t look right.' };
  const q = say(typeof input.question === 'string' ? input.question : '', kPollQuestion);
  if (!q) return { error: 'Give your poll a question.' };
  const raw = Array.isArray(input.options) ? input.options.slice(0, 20) : [];
  const options = [];
  for (const o of raw) {
    const text = say(typeof o === 'string' ? o : '', kPollOption);
    if (text && !options.some((x) => x.toLowerCase() === text.toLowerCase())) options.push(text);
  }
  if (options.length < 2) return { error: 'A poll needs at least 2 different answers.' };
  if (options.length > kPollMostOptions) return { error: 'A poll can have at most ' + kPollMostOptions + ' answers.' };
  const hours = input.hours === undefined ? kPollDefaultHours : Math.trunc(Number(input.hours) || 0);
  if (!POLL_HOURS.includes(hours)) return { error: 'Pick how long the poll stays open: 1 day, 3 days, 1 week or 30 days.' };
  return { poll: { q, options, votes: {}, at: t, until: t + hours * 3600 } };
}

// What people see: the counts, their own vote, and whether it's closed.
export function pollJson(poll, meId, t) {
  if (!poll) return null;
  const counts = poll.options.map(() => 0);
  for (const v of Object.values(poll.votes || {})) if (v >= 0 && v < counts.length) counts[v] += 1;
  const mine = meId && poll.votes && Object.prototype.hasOwnProperty.call(poll.votes, meId) ? poll.votes[meId] : -1;
  return { question: poll.q, options: poll.options.map((text, i) => ({ text, votes: counts[i] })),
    total: counts.reduce((n, c) => n + c, 0), mine, until: poll.until, closed: t >= poll.until };
}

// Vote (option = an answer's number from 0), or take it back (option -1). '' when it worked.
export function pollVote(poll, meId, option, t) {
  if (!poll) return 'There\'s no poll there.';
  if (t >= poll.until) return 'This poll has closed.';
  const n = Number.isInteger(option) ? option : NaN;
  if (n === -1) { if (poll.votes) delete poll.votes[meId]; return ''; }
  if (!(n >= 0 && n < poll.options.length)) return 'Pick one of the answers.';
  poll.votes = poll.votes || {};
  if (!(meId in poll.votes) && Object.keys(poll.votes).length >= kPollMostVotes) return 'This poll is full.';
  poll.votes[meId] = n;
  return '';
}

// The words in a poll, for Fyxa's look at new posts (server.js).
export const pollWords = (input) => (input && typeof input === 'object' && !Array.isArray(input)
  ? [typeof input.question === 'string' ? input.question : '', ...(Array.isArray(input.options) ? input.options.filter((o) => typeof o === 'string') : [])].join('\n')
  : '');
