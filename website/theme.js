// Light and dark mode for every Guts&Bolts page.
//
// By default the site follows your device (dark if your phone or computer is set to
// dark). The round button in the top bar flips between light and dark, and the site
// remembers your pick on this device (localStorage 'gb.theme').
//
// How it works: every dark-mode rule in our CSS sits inside
// "@media (prefers-color-scheme: dark)". When you pick a mode we rewrite just that
// condition in the loaded stylesheets: "all" to force the dark rules on, "not all" to
// switch them off. So a dark rule only has to be written once, and both your device
// setting and the button use it.
//
// Load this as a plain <script> right after the stylesheets in <head>, so the right
// colours are there before the page is drawn (no white flash in dark mode).
(function () {
  var KEY = 'gb.theme';
  var DARK = /\(\s*prefers-color-scheme\s*:\s*dark\s*\)/i;
  var root = document.documentElement;
  var originals = [];   // [media rule, its original condition]
  var seen = typeof WeakSet === 'function' ? new WeakSet() : null;
  var deviceDark = window.matchMedia ? window.matchMedia('(prefers-color-scheme: dark)') : null;

  function saved() { try { var v = localStorage.getItem(KEY); return v === 'light' || v === 'dark' ? v : ''; } catch (e) { return ''; } }
  function save(v) { try { if (v) localStorage.setItem(KEY, v); else localStorage.removeItem(KEY); } catch (e) { /* private window */ } }

  function collect(rules) {
    for (var i = 0; i < rules.length; i++) {
      var r = rules[i];
      if (r.media && r.cssRules) {
        if (DARK.test(r.media.mediaText) && !(seen && seen.has(r))) {
          if (seen) seen.add(r);
          originals.push([r, r.media.mediaText]);
        }
        collect(r.cssRules);
      }
    }
  }
  function scan() {
    var sheets = document.styleSheets;
    for (var i = 0; i < sheets.length; i++) {
      var rules; try { rules = sheets[i].cssRules; } catch (e) { continue; }   // another site's sheet
      if (rules) collect(rules);
    }
  }

  function isDark() { var p = saved(); return p ? p === 'dark' : !!(deviceDark && deviceDark.matches); }

  function apply() {
    var pick = saved();
    if (pick) root.setAttribute('data-theme', pick); else root.removeAttribute('data-theme');
    root.classList.toggle('is-dark', isDark());
    for (var i = 0; i < originals.length; i++) {
      var rule = originals[i][0], text = originals[i][1];
      var want = !pick ? text : pick === 'dark' ? (text.replace(DARK, 'all').replace(/^\s*all\s*$/, 'all')) : 'not all';
      if (rule.media.mediaText !== want) rule.media.mediaText = want;
    }
    var btn = document.querySelector('.theme-toggle');
    if (btn) {
      var label = isDark() ? 'Switch to light mode' : 'Switch to dark mode';
      btn.setAttribute('aria-label', label); btn.title = label;
      btn.setAttribute('aria-pressed', isDark() ? 'true' : 'false');
    }
  }

  function toggle() { save(isDark() ? 'light' : 'dark'); scan(); apply(); }

  var SUN = '<svg class="sun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><circle cx="12" cy="12" r="4.5"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>';
  var MOON = '<svg class="moon" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M20.5 14.6A8.5 8.5 0 0 1 9.4 3.5a8.5 8.5 0 1 0 11.1 11.1z"/></svg>';

  function addButton() {
    if (document.querySelector('.theme-toggle')) return;
    var bar = document.querySelector('.topbar .wrap');
    if (!bar) return;
    var btn = document.createElement('button');
    btn.type = 'button'; btn.className = 'theme-toggle'; btn.innerHTML = SUN + MOON;
    btn.addEventListener('click', toggle);
    bar.appendChild(btn);
    apply();
  }

  scan(); apply();
  if (deviceDark) {
    var onChange = function () { if (!saved()) apply(); };
    if (deviceDark.addEventListener) deviceDark.addEventListener('change', onChange); else if (deviceDark.addListener) deviceDark.addListener(onChange);
  }
  // Stylesheets added later (or still loading) get the same treatment.
  window.addEventListener('load', function () { scan(); apply(); });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { scan(); addButton(); apply(); });
  else addButton();

  window.gbTheme = { toggle: toggle, isDark: isDark, set: function (v) { save(v === 'light' || v === 'dark' ? v : ''); scan(); apply(); } };
})();
