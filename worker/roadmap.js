// The public roadmap (Create > Roadmap on the website): what Guts&Bolts staff plan to add,
// what they're working on and what's done, plus ideas from developers that everyone can
// vote on. Staff answer an idea by putting it on the roadmap or saying no (with a note).
// Saved in the meta table as 'roadmap'. src/server/ServerRoadmap.cpp has the same rules.
//
// Built-in items: every file in worker/roadmap/ is one roadmap item that comes with the code
// (see worker/roadmap/README.md), so each pull request can put its own work on the roadmap
// and move it to Done, without staff typing it in. They show next to the ones staff add here,
// but staff can't change them on the website (a pull request changes the file). Only the main
// server has them, like the built-in updates.
//
//   roadmap.get       {}                                   -> {items, ideas, mine}   (anyone, even visitors)
//   roadmap.suggest   {title, text}                        (signed up; 3 open ideas each)
//   roadmap.vote      {id, on}                             (signed up; open ideas only)
//   admin.roadmapSave {id?, title, text, status, version}  (staff; no id = a new item)
//   admin.roadmapRemove {id}
//   admin.ideaAnswer  {id, answer: planned|declined|open, note}   (planned also adds it to the roadmap)
//   admin.ideaRemove  {id}                                 (spam and the like)

export const kRoadTitle = 80, kRoadText = 600, kRoadVersion = 20, kRoadNote = 300;
export const kMostRoadItems = 200, kMostIdeas = 1000, kOpenIdeasEach = 3;
export const kShownOpenIdeas = 100, kShownAnswered = 30;
export const ROAD_STATUS = ['planned', 'working', 'done'];
const kBuiltInNote = 'That one comes with the code (worker/roadmap/), so a pull request changes it, not the website.';

const fail = (error) => ({ ok: false, error });

// worker/roadmap/<id>.js files -> newest first, as {id: 'b-<id>', title, text, status, version, pr, builtIn: true}
export function builtInItems(files) {
  return files.filter((x) => x && typeof x.id === 'string' && typeof x.title === 'string' && ROAD_STATUS.includes(x.status))
    .map((x) => ({ id: 'b-' + x.id, title: x.title.slice(0, kRoadTitle), text: String(x.text || '').slice(0, kRoadText), status: x.status,
      version: String(x.version || '').slice(0, kRoadVersion), at: Number(x.time) || 0, pr: Number.isInteger(x.pr) ? x.pr : 0, builtIn: true }))
    .sort((a, b) => b.at - a.at);   // newest first
}

export class Roadmap {
  constructor(saved, builtIn = []) {
    this.builtIn = builtIn;   // from builtInItems(): not saved, they come with the code
    this.items = Array.isArray(saved?.items) ? saved.items : [];   // {id, title, text, status, version, at, idea?}
    this.ideas = Array.isArray(saved?.ideas) ? saved.ideas : [];   // {id, title, text, by, at, votes: [account ids], status, note, answeredAt?}
  }
  toJSON() { return { items: this.items, ideas: this.ideas }; }

  // What everyone sees. nameOf(accountId) -> {id, userId, name} or null.
  view(meId, nameOf) {
    const idea = (x) => ({ id: x.id, title: x.title, text: x.text, by: nameOf(x.by), at: x.at, votes: x.votes.length,
      voted: !!meId && x.votes.includes(meId), status: x.status, note: x.note || '', mine: !!meId && x.by === meId });
    const open = this.ideas.filter((x) => x.status === 'open')
      .sort((a, b) => b.votes.length - a.votes.length || b.at - a.at).slice(0, kShownOpenIdeas);
    const answered = this.ideas.filter((x) => x.status !== 'open')
      .sort((a, b) => (b.answeredAt || 0) - (a.answeredAt || 0)).slice(0, kShownAnswered);
    const items = this.items.map((x) => ({ id: x.id, title: x.title, text: x.text, status: x.status, version: x.version || '', at: x.at }));
    return { ok: true, items: [...items, ...this.builtIn],
      ideas: open.map(idea), answered: answered.map(idea),
      openIdeas: this.ideas.filter((x) => x.status === 'open').length };
  }

  // title and text: already cleaned and run through the text filter.
  suggest(meId, title, text, id, t) {
    if (!title) return fail('Give your idea a short title.');
    if (this.ideas.filter((x) => x.by === meId && x.status === 'open').length >= kOpenIdeasEach)
      return fail('You already have ' + kOpenIdeasEach + ' ideas waiting. Wait until staff answer one before adding another.');
    const same = title.toLowerCase();
    if (this.ideas.some((x) => x.status === 'open' && x.title.toLowerCase() === same))
      return fail('Someone already suggested that. Vote for it instead!');
    if (this.ideas.length >= kMostIdeas) {
      // Full: the oldest answered idea goes, or else the open one with the fewest votes.
      let drop = this.ideas.findIndex((x) => x.status !== 'open');
      if (drop < 0) {
        drop = 0;
        this.ideas.forEach((x, i) => { if (x.votes.length < this.ideas[drop].votes.length) drop = i; });
      }
      this.ideas.splice(drop, 1);
    }
    this.ideas.push({ id, title, text, by: meId, at: t, votes: [meId], status: 'open', note: '' });
    return null;
  }

  vote(meId, id, on) {
    const x = this.ideas.find((i) => i.id === id);
    if (!x) return fail('That idea isn\'t there any more.');
    if (x.status !== 'open') return fail('Staff already answered that idea, so it can\'t get votes now.');
    const had = x.votes.indexOf(meId);
    if (on && had < 0) x.votes.push(meId);
    if (!on && had >= 0) x.votes.splice(had, 1);
    return null;
  }

  // Staff: add or change a roadmap item. Returns the item (or a failure).
  save(args, newId, t) {
    const { id, title, text, status, version } = args;
    if (id && id.startsWith('b-')) return fail(kBuiltInNote);
    if (!title) return fail('Give it a title.');
    if (!ROAD_STATUS.includes(status)) return fail('Pick Planned, Working on it or Done.');
    if (id) {
      const x = this.items.find((i) => i.id === id);
      if (!x) return fail('That roadmap item isn\'t there any more.');
      Object.assign(x, { title, text, status, version });
      return x;
    }
    if (this.items.length >= kMostRoadItems) return fail('The roadmap is full (' + kMostRoadItems + ' items). Remove some old ones first.');
    const x = { id: newId, title, text, status, version, at: t };
    this.items.push(x);
    return x;
  }

  remove(id) {
    if (id.startsWith('b-')) return fail(kBuiltInNote);
    const i = this.items.findIndex((x) => x.id === id);
    if (i < 0) return fail('That roadmap item isn\'t there any more.');
    this.items.splice(i, 1);
    return null;
  }

  // Staff answer an idea. Returns the idea (or a failure); "planned" adds it to the roadmap too.
  answer(id, answer, note, newId, t) {
    const x = this.ideas.find((i) => i.id === id);
    if (!x) return fail('That idea isn\'t there any more.');
    if (!['planned', 'declined', 'open'].includes(answer)) return fail('Pick an answer.');
    if (answer === 'planned' && x.status !== 'planned') {
      if (this.items.length >= kMostRoadItems) return fail('The roadmap is full (' + kMostRoadItems + ' items). Remove some old ones first.');
      this.items.push({ id: newId, title: x.title, text: x.text, status: 'planned', version: '', at: t, idea: x.id });
    }
    x.status = answer;
    x.note = answer === 'open' ? '' : note;
    if (answer === 'open') delete x.answeredAt; else x.answeredAt = t;
    return x;
  }

  removeIdea(id) {
    const i = this.ideas.findIndex((x) => x.id === id);
    if (i < 0) return fail('That idea isn\'t there any more.');
    return this.ideas.splice(i, 1)[0];
  }
}
