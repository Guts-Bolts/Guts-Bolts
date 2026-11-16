// Copy buttons on the product pages: <button data-copy="the text">Copy</button>.
document.querySelectorAll('[data-copy]').forEach(function (button) {
  button.addEventListener('click', function () {
    var text = button.getAttribute('data-copy');
    var label = button.textContent;
    var done = function (word) { button.textContent = word; setTimeout(function () { button.textContent = label; }, 1400); };
    if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(text).then(function () { done('Copied'); }, function () { done('Select it'); });
    else done('Select it');
  });
});

// A download that isn't out yet (ContentForge): the card starts greyed out, and we ask
// the download mirror whether the file is there. Once a release has been uploaded it
// turns on by itself, with no change to this page.
document.querySelectorAll('[data-release-check]').forEach(function (card) {
  if (!window.fetch) return;
  fetch(card.getAttribute('data-release-check'), { method: 'HEAD', cache: 'no-store' }).then(function (r) {
    if (!r.ok) return;
    card.classList.remove('off');
    card.removeAttribute('aria-disabled');
    var box = card.closest('section') || document;
    var label = box.querySelector('[data-release-label]'); if (label) label.textContent = 'Newest version · .zip';
    var text = box.querySelector('[data-release-text]'); if (text) text.textContent = 'The newest test version for Windows. Unzip it, then follow "How to start" above.';
    var sums = box.querySelector('[data-release-sums]'); if (sums) sums.hidden = false;
  }).catch(function () { /* offline: leave it greyed out */ });
});
