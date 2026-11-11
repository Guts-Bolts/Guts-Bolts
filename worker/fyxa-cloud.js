// Fyxa in the cloud: for people whose computer can't run an AI model. Studio's Fyxa sends its
// conversation here and Cloudflare's AI (the "AI" binding, Workers AI) answers.
// (The C++ server, src/server/Server.cpp, has no AI to answer with: it says so.)
//
// Running Fyxa on your own GPU (Ollama) is free and unlimited, and never comes here.
// The cloud costs Guts&Bolts money, so it has limits:
//   - Each account gets a number of AI steps a day (one step = one answer from the model; a
//     task that uses tools takes a few). Boltworks Club members get more.
//   - The whole site has a daily budget in Cloudflare "neurons" (how Workers AI is billed;
//     10,000 a day are free). When it's used up, the cloud stops until midnight UTC.
//   Staff can change all of these (admin.fyxaCloud).
//
// Requests:
//   fyxacloud.usage {}                          -> {used, limit, budgetLeft, resets}
//   fyxacloud.chat  {messages, tools}           -> {message: {role, content, tool_calls}, usage}  (Ollama's /api/chat shape)
//   admin.fyxaCloud {limits?, budget?, off?}    -> {settings}   (staff)

export const kFyxaModel = '@cf/meta/llama-4-scout-17b-16e-instruct';
// Neurons per million tokens for that model (Cloudflare's price list; check it when changing the model).
export const kNeuronsIn = 24545, kNeuronsOut = 77273;
export const kFreeNeurons = 10000;
export const kMaxBody = 400 * 1024, kMaxTokens = 2048;
// Steps a day: no club, then each club tier (worker/server.js CLUB_TIERS: bwc, tbc, obc).
export const DEFAULT_LIMITS = { free: 20, bwc: 75, tbc: 200, obc: 200 };

const fail = (error) => ({ ok: false, error });
const okay = (extra) => Object.assign({ ok: true }, extra || {});
const utcDay = (t) => new Date(t * 1000).toISOString().slice(0, 10);
const nextMidnight = (t) => (Math.floor(t / 86400) + 1) * 86400;

// Estimated neurons for one answer.
export function neuronsFor(usage) {
  const inTok = Number(usage && usage.prompt_tokens) || 0, outTok = Number(usage && usage.completion_tokens) || 0;
  return Math.ceil((inTok * kNeuronsIn + outTok * kNeuronsOut) / 1e6);
}

// Studio's (Ollama-style) messages -> what Workers AI takes. Pictures are dropped (too costly).
export function toWorkersAi(messages) {
  const out = [];
  let n = 0;
  for (const m of messages) {
    if (!m || typeof m !== 'object') continue;
    const role = ['system', 'user', 'assistant', 'tool'].includes(m.role) ? m.role : 'user';
    const content = typeof m.content === 'string' ? m.content : '';
    if (role === 'assistant' && Array.isArray(m.tool_calls) && m.tool_calls.length) {
      out.push({ role, content, tool_calls: m.tool_calls.map((c) => {
        const fn = (c && c.function) || {};
        return { id: 'call_' + (n++), type: 'function',
          function: { name: String(fn.name || ''), arguments: typeof fn.arguments === 'string' ? fn.arguments : JSON.stringify(fn.arguments || {}) } };
      }) });
    } else if (role === 'tool') {
      out.push({ role: 'tool', name: String(m.name || ''), content });
    } else out.push({ role, content });
  }
  return out;
}

// Workers AI's answer -> an Ollama-style message Studio already understands.
export function fromWorkersAi(r) {
  const choice = r && Array.isArray(r.choices) && r.choices[0] ? r.choices[0].message || {} : null;
  const text = choice ? choice.content : r && r.response;
  const calls = (choice ? choice.tool_calls : r && r.tool_calls) || [];
  const message = { role: 'assistant', content: typeof text === 'string' ? text : (text ? JSON.stringify(text) : '') };
  const toolCalls = [];
  for (const c of Array.isArray(calls) ? calls : []) {
    const fn = c && (c.function || c);
    if (!fn || !fn.name) continue;
    let args = fn.arguments;
    if (typeof args === 'string') { try { args = JSON.parse(args); } catch { args = {}; } }
    toolCalls.push({ function: { name: String(fn.name), arguments: args && typeof args === 'object' ? args : {} } });
  }
  if (toolCalls.length) message.tool_calls = toolCalls;
  return message;
}

export function installFyxaCloud(Cls) {
  const P = Cls.prototype;

  P.fyxaCloudSettings = function () {
    const s = this.getMeta('fyxacloud', {});
    return { limits: Object.assign({}, DEFAULT_LIMITS, s.limits || {}), budget: Number.isFinite(s.budget) ? s.budget : kFreeNeurons, off: !!s.off };
  };
  // Today's site-wide use: {day, neurons, steps}.
  P.fyxaCloudDay = function (t) {
    const d = this.getMeta('fyxaclouddayuse', null);
    return d && d.day === utcDay(t) ? d : { day: utcDay(t), neurons: 0, steps: 0 };
  };
  P.fyxaCloudLimit = function (me) {
    const { limits } = this.fyxaCloudSettings();
    const club = this.clubOf(me);
    return this.isStaff(me) ? Math.max(limits.obc, 500) : club ? limits[club] ?? limits.free : limits.free;
  };
  P.fyxaCloudUsed = function (me, t) { return me.fyxaCloud && me.fyxaCloud.day === utcDay(t) ? me.fyxaCloud.steps : 0; };
  P.fyxaCloudUsage = function (me, t) {
    const s = this.fyxaCloudSettings(), day = this.fyxaCloudDay(t);
    return { used: this.fyxaCloudUsed(me, t), limit: this.fyxaCloudLimit(me), club: this.clubOf(me) || '',
      budgetLeft: Math.max(0, s.budget - day.neurons), off: s.off, resets: nextMidnight(t), model: kFyxaModel };
  };

  P.fyxaCloudOp = function (name, me, args) {
    const t = Math.floor(Date.now() / 1000);
    if (name === 'admin.fyxaCloud') {
      if (!this.isStaff(me)) return fail('Only staff can change this.');
      const s = this.fyxaCloudSettings();
      if (args.limits && typeof args.limits === 'object')
        for (const k of Object.keys(DEFAULT_LIMITS))
          if (Number.isInteger(args.limits[k]) && args.limits[k] >= 0 && args.limits[k] <= 10000) s.limits[k] = args.limits[k];
      if (Number.isInteger(args.budget) && args.budget >= 0 && args.budget <= 100000000) s.budget = args.budget;
      if (typeof args.off === 'boolean') s.off = args.off;
      this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'fyxacloud', JSON.stringify(s));
      return okay({ settings: s, today: this.fyxaCloudDay(t) });
    }
    if (me.userId <= 0) return fail('Sign up to use Fyxa in the cloud. (Fyxa on your own computer with Ollama works without an account.)');
    if (name === 'fyxacloud.usage') return okay(this.fyxaCloudUsage(me, t));
    if (name !== 'fyxacloud.chat') return fail('Unknown request.');
    const u = this.fyxaCloudUsage(me, t);
    if (u.off) return fail('Fyxa in the cloud is switched off right now. Fyxa on your own computer (Ollama) still works.');
    if (!this.env.AI) return fail('Fyxa in the cloud isn\'t set up on this server. Fyxa on your own computer (Ollama) works.');
    if (u.used >= u.limit)
      return fail('That\'s your ' + u.limit + ' Fyxa cloud steps for today. They come back at midnight UTC.' +
        (u.club ? '' : ' Boltworks Club members get more.') + ' Fyxa on your own GPU (Ollama) is free and has no limit.');
    if (u.budgetLeft <= 0)
      return fail('Fyxa in the cloud has used all of today\'s AI for the whole site. It comes back at midnight UTC. Fyxa on your own GPU (Ollama) still works.');
    if (!Array.isArray(args.messages) || !args.messages.length) return fail('Nothing to answer.');
    if (JSON.stringify(args).length > kMaxBody) return fail('This conversation is too long for the cloud. Start a new one.');
    // Count the step now (so two at once can't both slip under the limit); the neurons once it answers.
    me.fyxaCloud = { day: utcDay(t), steps: u.used + 1 };
    this.saveUser(me);
    const tools = (Array.isArray(args.tools) ? args.tools : []).filter((x) => x && x.function && x.function.name).slice(0, 64)
      .map((x) => ({ type: 'function', function: { name: String(x.function.name), description: String(x.function.description || '').slice(0, 4000),
        parameters: x.function.parameters && typeof x.function.parameters === 'object' ? x.function.parameters : { type: 'object', properties: {} } } }));
    return okay({ fyxaCloud: { account: me.id, messages: toWorkersAi(args.messages), tools } });
  };

  // The second half of fyxacloud.chat: ask Workers AI (fetch() calls this, like translate()).
  P.fyxaCloudAnswer = async function (answer) {
    const job = answer.fyxaCloud;
    const t = Math.floor(Date.now() / 1000);
    let r;
    try {
      r = await this.env.AI.run(kFyxaModel, { messages: job.messages, tools: job.tools.length ? job.tools : undefined, max_tokens: kMaxTokens });
    } catch (e) {
      const me = this.users.get(job.account);   // it didn't answer: that step doesn't count
      if (me && me.fyxaCloud && me.fyxaCloud.day === utcDay(t) && me.fyxaCloud.steps > 0) { me.fyxaCloud.steps--; this.saveUser(me); this.flush(); }
      return fail('Cloudflare\'s AI didn\'t answer (' + String(e && e.message || e).slice(0, 200) + '). Try again in a bit.');
    }
    const used = neuronsFor(r && r.usage);
    const day = this.fyxaCloudDay(t);
    day.neurons += used; day.steps += 1;
    this.sql.exec('INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)', 'fyxaclouddayuse', JSON.stringify(day));
    return okay({ message: fromWorkersAi(r), usage: { neurons: used } });
  };
}
