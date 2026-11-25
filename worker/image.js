// G&B Image: put a picture online and get a link to share it (in a forum post, a Discord
// chat, a game's page, anywhere). Its own product on the site, in the Creation group;
// website/app/image.js draws its pages and has the free picture editor (crop, turn, resize,
// colours) that works in your browser before you upload. src/server/ServerImage.cpp has the
// same rules.
//
// How it's different from Art: Art is a gallery for showing off work (faves, comments,
// categories). Image is a quick place to keep pictures and share links to them: screenshots,
// pictures for a forum post, a game's logo, a meme.
//
// How it works:
//   - An image is one picture (PNG, JPG, GIF or WebP, up to kMostBytes) with a title. The
//     website also sends a small preview (thumb) for the lists.
//   - "public" images show in the Recent list; "unlisted" ones only open with their link (the
//     id is long and random so nobody can guess it). Both can be reported.
//   - Like Art and decals, pictures from people who aren't Verified wait for a staff check
//     before anyone else can see them ('pending'). Verified creators and staff go straight up.
//     Fyxa also looks at every new picture and flags anything bad for staff (server.js).
//   - The uploader can change the title and who can see it; the uploader or staff can delete it.
//   - Uploading is free. Not-Verified people can upload kPerDay a day (Verified: kPerDayVerified),
//     and keep up to kMostEach.
//
// Requests:
//   image.upload  {title, data, thumb, visibility, w, h}  -> {image}
//   image.list    {page, by}            -> {images, more, page}   public, checked ones, newest first (guests too)
//   image.mine    {}                    -> {images}               yours, unlisted and waiting ones too
//   image.get     {id, quiet}           -> {image}                (guests too; counts a view)
//   image.picture {id, thumb}           -> {data, type}           (guests too; the website uses /imgpic/<id>)
//   image.edit    {id, title, visibility} -> {image}              (the uploader)
//   image.delete  {id}                  -> {}                     (the uploader or staff)
//   image.pending {}                    -> {images}               (staff: waiting for a check)
//   image.review  {id, ok, note}        -> {image}                (staff)
// GET /imgpic/<id> (and /imgpic/<id>/thumb) is the picture itself, for <img> tags and links.
import { clean } from './banlists.js';
import { artType, ART_MIME } from './art.js';

export const kTitle = 80, kMostBytes = 5 * 1024 * 1024, kThumbBytes = 200 * 1024, kPageSize = 30;
export const kPerDay = 20, kPerDayVerified = 100, kMostEach = 1000, kMostSide = 20000;
export const IMAGE_LOOK_ONLY = ['image.list', 'image.get', 'image.picture'];
export const IMAGE_COSTLY = { 'image.upload': [30, 90, 3600] };

const okay = (extra) => Object.assign({ ok: true }, extra || {});
const fail = (error) => ({ ok: false, error });
const str = (args, k) => (typeof args[k] === 'string' ? args[k] : '');
const now = () => Math.floor(Date.now() / 1000);
const hex = (n) => Array.from(crypto.getRandomValues(new Uint8Array(n)), (b) => b.toString(16).padStart(2, '0')).join('');
const side = (v) => Math.max(0, Math.min(kMostSide, Math.trunc(Number(v) || 0)));

function fromB64(s) {
  const bin = atob(String(s || '').replace(/^data:[^,]+,/, ''));
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}
function toB64(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s);
}

export function installImage(Cls) {
  const P = Cls.prototype;

  P.imageInit = function () {
    if (this.images) return;
    this.sql.exec('CREATE TABLE IF NOT EXISTS images (id TEXT PRIMARY KEY, data TEXT NOT NULL)');
    this.images = new Map();
    for (const r of this.sql.exec('SELECT id, data FROM images')) this.images.set(r.id, JSON.parse(r.data));
  };
  P.imageSave = function (m) { this.sql.exec('INSERT OR REPLACE INTO images (id, data) VALUES (?, ?)', m.id, JSON.stringify(m)); };
  // Waiting (or turned-down) pictures: only the uploader and staff.
  P.imageCanSee = function (m, me) { return !!m && (!m.review || (!!me && (m.by === me.id || this.isStaff(me)))); };
  P.imageJson = function (m, me, full = false) {
    const u = this.users.get(m.by);
    const out = { id: m.id, title: m.title, type: m.type, visibility: m.visibility, w: m.w || 0, h: m.h || 0, size: m.size,
      at: m.at, views: m.views || 0, thumb: !!m.thumb, review: m.review || undefined,
      by: u ? { id: u.id, name: u.name, userId: u.userId, verified: this.isVerified(u) } : { id: m.by, name: '?', userId: 0, verified: false } };
    if (!full) return out;
    const mine = !!me && m.by === me.id, staff = !!me && this.isStaff(me);
    return Object.assign(out, { mine, canDelete: mine || staff, reviewNote: (mine || staff) ? m.reviewNote || undefined : undefined });
  };
  // For report.send (server.js).
  P.imageReportTarget = function (id, me) {
    this.imageInit();
    const m = this.images.get(String(id || ''));
    if (!m || !this.imageCanSee(m, me)) return 'That picture isn\'t there any more.';
    if (m.by === me.id) return 'You can\'t report yourself.';
    return { target: m.id, about: m.by, copy: { subject: 'Image: ' + m.title, body: '(a picture: /imgpic/' + m.id + ')', at: m.at } };
  };
  P.imageReportName = function (target) {
    this.imageInit();
    const m = this.images.get(String(target || ''));
    return m ? m.title : '(deleted)';
  };
  // GET /imgpic/<id> (and /imgpic/<id>/thumb).
  P.imagePictureResponse = function (path) {
    const [id, which] = path.split('/').map((x) => { try { return decodeURIComponent(x || ''); } catch { return ''; } });
    const r = this.imageOp('image.picture', null, { id: id.replace(/\.(png|jpe?g|gif|webp)$/i, ''), thumb: which === 'thumb' });
    if (!r.ok) return new Response('No picture.', { status: 404 });
    return new Response(r.bytes, { headers: { 'content-type': ART_MIME[r.type] || 'application/octet-stream',
      'cache-control': 'public, max-age=86400', 'x-content-type-options': 'nosniff', 'content-security-policy': "default-src 'none'; sandbox" } });
  };

  P.imageOp = function (name, me, args) {
    this.imageInit();
    const t = now();
    if (name === 'image.list') {
      let by = null;
      if (args.by !== undefined && args.by !== '') {
        by = this.findPerson(args.by);
        if (!by) return fail('There\'s nobody with that name or number.');
      }
      const page = Math.max(1, Math.min(1000, Math.trunc(Number(args.page) || 1)));
      const list = [...this.images.values()].filter((m) => m.visibility === 'public' && !m.review && (!by || m.by === by.id))
        .sort((x, y) => y.at - x.at);
      const from = (page - 1) * kPageSize;
      return okay({ images: list.slice(from, from + kPageSize).map((m) => this.imageJson(m, me)), more: list.length > from + kPageSize, page });
    }
    const m = this.images.get(str(args, 'id'));
    if (name === 'image.picture') {
      if (!this.imageCanSee(m, me)) return fail('No picture.');
      const thumb = args.thumb === true && m.thumb;
      const bytes = this.readFile((thumb ? 'imagethumb:' : 'image:') + m.id);
      if (!bytes) return fail('No picture.');
      const r = okay({ type: thumb ? m.thumb : m.type });
      Object.defineProperty(r, 'bytes', { value: bytes, enumerable: false });   // (for /imgpic, not sent as JSON)
      if (me) r.data = toB64(bytes);
      return r;
    }
    if (name === 'image.get') {
      if (!this.imageCanSee(m, me)) return fail('That picture isn\'t here. It may have been deleted.');
      if (args.quiet !== true && (!me || m.by !== me.id)) { m.views = (m.views || 0) + 1; this.imageSave(m); }
      return okay({ image: this.imageJson(m, me, true) });
    }

    if (!me || me.userId === 0) return fail('Sign up to upload pictures.');
    const staff = this.isStaff(me), verified = this.isVerified(me);
    if (name === 'image.mine') {
      const list = [...this.images.values()].filter((x) => x.by === me.id).sort((x, y) => y.at - x.at);
      return okay({ images: list.map((x) => this.imageJson(x, me)) });
    }
    if (name === 'image.upload') {
      const today = Math.floor(t / 86400);
      if (me.imageDay !== today) { me.imageDay = today; me.imagesToday = 0; }
      const most = verified || staff ? kPerDayVerified : kPerDay;
      if (me.imagesToday >= most) return fail('You\'ve uploaded ' + most + ' pictures today. Come back tomorrow!');
      if ([...this.images.values()].filter((x) => x.by === me.id).length >= kMostEach) return fail('You have ' + kMostEach + ' pictures. Delete some old ones first.');
      let data, thumb = null;
      try { data = fromB64(str(args, 'data')); } catch { return fail('The picture got scrambled. Try again.'); }
      const type = artType(data);
      if (!type) return fail('Pictures must be .png, .jpg, .gif or .webp.');
      if (data.length > kMostBytes) return fail('That picture is too big (the most is ' + (kMostBytes >> 20) + ' MB).');
      if (str(args, 'thumb')) {
        try { thumb = fromB64(str(args, 'thumb')); } catch { thumb = null; }
        if (!thumb || !artType(thumb) || thumb.length > kThumbBytes) thumb = null;   // (the lists use the full picture instead)
      }
      const img = { id: 'im-' + hex(6), by: me.id, title: clean(str(args, 'title'), kTitle) || 'Untitled', type, thumb: thumb ? artType(thumb) : '',
        visibility: str(args, 'visibility') === 'unlisted' ? 'unlisted' : 'public', w: side(args.w), h: side(args.h),
        size: data.length, at: t, views: 0 };
      if (!verified && !staff) img.review = 'pending';
      this.writeFile('image:' + img.id, data);
      if (thumb) this.writeFile('imagethumb:' + img.id, thumb);
      this.images.set(img.id, img);
      this.imageSave(img);
      me.imagesToday += 1;
      this.saveUser(me);
      return okay({ image: this.imageJson(img, me, true) });
    }
    if (name === 'image.pending') {
      if (!staff) return fail('Only staff can do that.');
      return okay({ images: [...this.images.values()].filter((x) => x.review === 'pending').sort((x, y) => x.at - y.at).map((x) => this.imageJson(x, me)) });
    }

    if (!this.imageCanSee(m, me)) return fail('That picture isn\'t here (any more).');
    if (name === 'image.edit') {
      if (m.by !== me.id) return fail('Only the person who uploaded it can change it.');
      m.title = clean(str(args, 'title'), kTitle) || 'Untitled';
      if (args.visibility !== undefined) m.visibility = str(args, 'visibility') === 'unlisted' ? 'unlisted' : 'public';
      this.imageSave(m);
      return okay({ image: this.imageJson(m, me, true) });
    }
    if (name === 'image.delete') {
      if (m.by !== me.id && !staff) return fail('Only the person who uploaded it, or staff, can delete it.');
      this.images.delete(m.id);
      this.sql.exec('DELETE FROM images WHERE id = ?', m.id);
      this.sql.exec('DELETE FROM files WHERE id = ? OR id = ?', 'image:' + m.id, 'imagethumb:' + m.id);
      return okay();
    }
    if (name === 'image.review') {
      if (!staff) return fail('Only staff can do that.');
      if (!m.review) return fail('That isn\'t waiting for a check any more.');
      const owner = this.users.get(m.by);
      if (args.ok === true) {
        delete m.review; delete m.reviewNote;
        this.notify(owner, 'image', 'Your picture "' + m.title + '" passed the staff check. Its link works for everyone now!', m.id);
      } else {
        m.review = 'rejected'; m.reviewNote = clean(str(args, 'note'), 200);
        this.notify(owner, 'image', 'Your picture "' + m.title + '" didn\'t pass the staff check' + (m.reviewNote ? ': ' + m.reviewNote : '.'), m.id);
      }
      this.imageSave(m);
      return okay({ image: this.imageJson(m, me, true) });
    }
    return fail('Unknown request.');
  };
}
