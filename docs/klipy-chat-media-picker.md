# KLIPY GIF and sticker picker (chats and forum)

## How it fits together
- The picker is `website/app/media-picker.js`. It is used by the chat dock (`chatdock.js`) and
  the forum composer and comment boxes (`app.js`).
- The app key is a public browser key (not a secret). It is `DEFAULT_KLIPY_KEY` in
  `media-picker.js`; a page can override it with `window.GB_KLIPY_APP_KEY`. It is not in an
  inline `<script>` in `index.html`, because the site's security rules don't run inline scripts.
- The site's security rules (`worker/security.js`, `connect-src`) allow `https://api.klipy.com`,
  so the visitor's own browser does the search. KLIPY requires that: never proxy the API, cache,
  mirror or re-host its pictures.
- A picked picture goes into the message as `[media:https://static.klipy.com/...gif]`.
  `website/app/embeds.js` shows it as the picture. Only KLIPY's servers, over https, with a
  picture file ending (gif, png, jpg, webp) count (`safeMediaUrl`).
- Both servers' text filters let these tokens through and still block every other link:
  `MEDIA_TOKEN` in `worker/textfilter.js` and the `media` rule in `src/core/TextFilter.cpp`.

## Picker
- GIFs and Stickers tabs, trending first, search as you type, quick search words, more on scroll.
- Search placeholder is `Search KLIPY`, with visible `Powered by KLIPY`.
- Content filter is `medium`; also set KLIPY's partner dashboard keyword/block settings.
- The medium size (`md`) is sent; the small one (`sm`) is used for the grid.
- There is no "paste any picture link" box any more: only KLIPY pictures can be sent.

## Limits
- Test keys have 100 API requests an hour; ask KLIPY for production access.
- A forum post or comment can carry up to 4 GIFs; a chat message carries one.
