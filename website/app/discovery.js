// Homepage discovery shelves. Keep this separate from the main app so discovery
// can evolve without duplicating the app's game-listing or authentication logic.
import { call } from './gb.js';

const view = document.getElementById('view');
let generation = 0;
let loading = false;

function isHome() {
  const path = location.hash.split('?')[0];
  return (path === '' || path === '#' || path === '#/') &&
    !!view && (!!view.querySelector('.home') || !!view.querySelector('.welcome'));
}

function makeCard(game) {
  const link = document.createElement('a');
  link.className = 'card';
  link.href = '#/game/' + encodeURIComponent(game.id);

  const picture = document.createElement('div');
  picture.className = 'gb-discovery-picture';
  if (game.thumb) {
    const img = document.createElement('img');
    img.src = '/thumb/' + encodeURIComponent(game.id) + '?v=' + encodeURIComponent(game.thumb);
    img.alt = '';
    img.loading = 'lazy';
    img.onerror = () => {
      img.remove();
      picture.textContent = String(game.name || 'G').slice(0, 1).toUpperCase();
    };
    picture.append(img);
  } else {
    picture.textContent = String(game.name || 'G').slice(0, 1).toUpperCase();
  }

  const name = document.createElement('div');
  name.className = 'name';
  name.textContent = game.name || 'Untitled game';

  const creator = document.createElement('div');
  creator.className = 'by';
  creator.textContent = 'by ' + (game.group?.name || game.creatorName || 'Unknown creator');

  const details = document.createElement('div');
  details.className = 'by';
  const visits = Number(game.plays || 0);
  details.textContent = visits.toLocaleString() + ' visits';

  const genres = document.createElement('div');
  genres.className = 'by small';
  genres.textContent = Array.isArray(game.genres) ? game.genres.join(' · ') : '';

  link.append(picture, name, creator, details);
  if (genres.textContent) link.append(genres);
  return link;
}

function makeShelf(title, sort, games) {
  if (!games.length) return null;
  const section = document.createElement('section');
  section.className = 'box gb-discovery';
  const heading = document.createElement('h2');
  heading.className = 'boxhead';
  heading.textContent = title;

  const more = document.createElement('a');
  more.href = '#/games?sort=' + sort;
  more.textContent = 'See all »';
  heading.append(' ', more);

  const grid = document.createElement('div');
  grid.className = 'grid';
  games.forEach((game) => grid.append(makeCard(game)));
  section.append(heading, grid);
  return section;
}

async function loadShelves(target, token) {
  if (loading || target.querySelector('.gb-discovery')) return;
  loading = true;
  try {
    const [newest, updated] = await Promise.all([
      call('list', { kind: 'game', sort: 'new', limit: 6 }),
      call('list', { kind: 'game', sort: 'updated', limit: 6 }),
    ]);
    if (token !== generation || !isHome() || !target.isConnected) return;

    const shelves = [
      makeShelf('New Releases', 'new', newest.ok ? newest.assets || [] : []),
      makeShelf('Recently Updated', 'updated', updated.ok ? updated.assets || [] : []),
    ].filter(Boolean);
    if (!shelves.length) return;

    const home = target.querySelector('.home-right') || target;
    const anchor = home.querySelector('.featured-box');
    const after = anchor || home.lastElementChild;
    if (after && after.parentNode === home) {
      for (const shelf of shelves) after.after(shelf);
    } else {
      shelves.forEach((shelf) => home.append(shelf));
    }
  } catch {
    // Discovery is supplemental; never prevent the main homepage from working.
  } finally {
    loading = false;
  }
}

function schedule() {
  if (!isHome()) {
    generation++;
    return;
  }
  const token = ++generation;
  const target = view;
  queueMicrotask(() => {
    if (token === generation && isHome()) loadShelves(target, token);
  });
}

if (view) {
  const observer = new MutationObserver(() => {
    if (isHome() && !view.querySelector('.gb-discovery')) schedule();
    else if (!isHome()) generation++;
  });
  observer.observe(view, { childList: true, subtree: true });
  window.addEventListener('hashchange', schedule);
  schedule();
}
