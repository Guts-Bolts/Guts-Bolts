// MemoryStoreService's data for one game: quick, short-lived data every server of the game
// shares (a global leaderboard, a matchmaking queue). Everything has an expiration and goes
// away by itself; nothing is saved. src/online/MemoryStore.cpp is the same in C++ (the C++
// server and games played offline use it), so keep the two alike.
//
// Requests (server.js memOp adds who may ask): see src/online/MemoryStore.h.

export const kMemName = 128;               // map / queue names and keys
export const kMemValue = 32 * 1024;        // one value, as JSON
export const kMemMostExpiration = 45 * 24 * 3600;
export const kMemMostRange = 200;
export const memLimit = (players) => 64 * 1024 + 1024 * Math.max(0, players);   // 64 KB + 1 KB per player

const fail = (error) => ({ ok: false, error });
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const num = (args, k, fallback) => (typeof args[k] === 'number' && Number.isFinite(args[k]) ? args[k] : fallback);
const bytes = (v) => new TextEncoder().encode(JSON.stringify(v === undefined ? null : v)).length;

// Sorted maps: sort key order (none first, then numbers, then text), then by key.
const rank = (s) => (typeof s === 'number' ? 1 : typeof s === 'string' ? 2 : 0);
function compareSort(a, b) {
  const ra = rank(a), rb = rank(b);
  if (ra !== rb) return ra < rb ? -1 : 1;
  if (ra === 0) return 0;
  return a < b ? -1 : a > b ? 1 : 0;
}
const compareKeys = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
// A bound with no key stands for every key with its sort key, so it's never equal.
function compareToBound(sortKey, key, bound, lower) {
  const c = compareSort(sortKey, bound.sortKey);
  if (c !== 0) return c;
  if (typeof bound.key !== 'string') return lower ? -1 : 1;
  return compareKeys(key, bound.key);
}

export class MemoryStore {
  constructor() {
    this.maps = new Map();     // 's:' + name sorted, 'h:' + name hash -> Map(key -> {value, sortKey, expires, size})
    this.queues = new Map();   // name -> [{value, priority, expires, hiddenUntil, seq, readId, size}]
    this.used = 0;
    this.players = 0;
    this.seq = 0;
    this.reads = 0;
  }

  get empty() { return this.maps.size === 0 && this.queues.size === 0; }

  expire(now) {
    for (const [name, map] of this.maps) {
      for (const [k, it] of map) if (it.expires <= now) { this.used -= it.size; map.delete(k); }
      if (!map.size) this.maps.delete(name);
    }
    for (const [name, list] of this.queues) {
      const keep = list.filter((it) => { if (it.expires <= now) { this.used -= it.size; return false; } return true; });
      if (keep.length) this.queues.set(name, keep); else this.queues.delete(name);
    }
  }

  run(op, args, now) {
    this.expire(now);
    const queue = op === 'mem.qadd' || op === 'mem.qread' || op === 'mem.qremove';
    const name = str(args, queue ? 'queue' : 'map');
    const what = queue ? 'Queue' : args.sorted === true ? 'SortedMap' : 'HashMap';
    if (!name || name.length > kMemName) return fail('MemoryStore ' + what + ' names are 1-' + kMemName + ' letters.');
    const checkValue = (value, extra) => {
      if (bytes(value) > kMemValue) return { bad: fail('That value is too big for a MemoryStore (' + (kMemValue / 1024) + ' KB at most).') };
      const expiration = num(args, 'expiration', -1);
      if (!(expiration > 0) || expiration > kMemMostExpiration)
        return { bad: fail('MemoryStore: the expiration is in seconds, from 1 to ' + kMemMostExpiration + ' (45 days).') };
      return { expires: now + expiration, size: bytes(value) + extra };
    };
    const roomFor = (more, freed) => {
      if (this.used - freed + more > memLimit(this.players))
        return fail('This game\'s MemoryStores are full (' + (memLimit(this.players) / 1024) +
                    ' KB: 64 KB + 1 KB per player). Remove some, or use shorter expirations.');
      return null;
    };

    if (!queue) {
      const key = str(args, 'key');
      if (op !== 'mem.range' && (!key || key.length > kMemName)) return fail('MemoryStore keys are 1-' + kMemName + ' letters.');
      const sorted = args.sorted === true;
      const id = (sorted ? 's:' : 'h:') + name;
      const map = this.maps.get(id) || new Map();
      if (op === 'mem.get') {
        const it = map.get(key);
        const r = okay({ found: !!it, value: it ? it.value : null });
        if (it && it.sortKey !== null) r.sortKey = it.sortKey;
        return r;
      }
      if (op === 'mem.set') {
        if (args.value === undefined || args.value === null) return fail('MemoryStore: SetAsync needs a value (use RemoveAsync to remove one).');
        let sortKey = null;
        if (sorted && args.sortKey !== undefined && args.sortKey !== null) {
          sortKey = args.sortKey;
          if (typeof sortKey !== 'number' && typeof sortKey !== 'string') return fail('MemoryStore: a sort key is a number or text.');
          if (typeof sortKey === 'string' && sortKey.length > kMemName) return fail('MemoryStore: a sort key is at most 128 letters.');
        }
        const v = checkValue(args.value, key.length + bytes(sortKey));
        if (v.bad) return v.bad;
        const old = map.get(key);
        const bad = roomFor(v.size, old ? old.size : 0);
        if (bad) return bad;
        if (old) this.used -= old.size;
        this.used += v.size;
        map.set(key, { value: args.value, sortKey, expires: v.expires, size: v.size });
        this.maps.set(id, map);
        return okay({ created: !old });   // (SetAsync returns it)
      }
      if (op === 'mem.remove') {
        const it = map.get(key);
        if (it) { this.used -= it.size; map.delete(key); }
        if (!map.size) this.maps.delete(id);
        return okay();
      }
      if (op === 'mem.range') {
        if (!sorted) return fail('GetRangeAsync is for sorted maps.');
        const down = str(args, 'direction') === 'Descending';
        const count = Math.floor(num(args, 'count', 0));
        if (count < 1 || count > kMemMostRange) return fail('GetRangeAsync: count is 1-' + kMemMostRange + '.');
        const lower = args.lower && typeof args.lower === 'object' ? args.lower : null;
        const upper = args.upper && typeof args.upper === 'object' ? args.upper : null;
        const all = [];
        for (const [k, it] of map) {
          if (lower && compareToBound(it.sortKey, k, lower, true) <= 0) continue;
          if (upper && compareToBound(it.sortKey, k, upper, false) >= 0) continue;
          all.push([k, it]);
        }
        all.sort((a, b) => {
          const c = compareSort(a[1].sortKey, b[1].sortKey) || compareKeys(a[0], b[0]);
          return down ? -c : c;
        });
        return okay({ items: all.slice(0, count).map(([k, it]) => (it.sortKey !== null ? { key: k, value: it.value, sortKey: it.sortKey } : { key: k, value: it.value })) });
      }
      return fail('Unknown request.');
    }

    const list = this.queues.get(name) || [];
    if (op === 'mem.qadd') {
      if (args.value === undefined || args.value === null) return fail('MemoryStore: AddAsync needs a value.');
      const v = checkValue(args.value, 16);
      if (v.bad) return v.bad;
      const bad = roomFor(v.size, 0);
      if (bad) return bad;
      this.used += v.size;
      list.push({ value: args.value, priority: num(args, 'priority', 0), expires: v.expires, hiddenUntil: 0, seq: ++this.seq, readId: '', size: v.size });
      this.queues.set(name, list);
      return okay();
    }
    if (op === 'mem.qread') {
      const count = Math.floor(num(args, 'count', 1));
      if (count < 1 || count > 100) return fail('ReadAsync: count is 1-100.');
      const invisible = Math.min(3600, Math.max(1, num(args, 'invisible', 30)));
      const ready = list.filter((q) => q.hiddenUntil <= now).sort((a, b) => (a.priority !== b.priority ? b.priority - a.priority : a.seq - b.seq));
      if (!ready.length || (args.all === true && ready.length < count)) return okay({ items: [], id: null });
      const id = 'r' + (++this.reads);
      const items = [];
      for (const q of ready.slice(0, count)) {
        q.hiddenUntil = now + invisible;   // others don't see them unless they aren't removed in time
        q.readId = id;
        items.push(q.value);
      }
      return okay({ items, id });
    }
    if (op === 'mem.qremove') {
      const id = str(args, 'id');
      if (!id) return fail('RemoveAsync needs the id ReadAsync gave.');
      const keep = list.filter((q) => { if (q.readId === id && q.hiddenUntil > now) { this.used -= q.size; return false; } return true; });
      if (keep.length) this.queues.set(name, keep); else this.queues.delete(name);
      return okay();
    }
    return fail('Unknown request.');
  }
}
