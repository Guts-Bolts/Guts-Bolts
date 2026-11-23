# KLIPY Chat Media Picker

## Setup
1. Create a KLIPY API app/key in the KLIPY Partner Panel.
2. In `website/app/index.html`, set `window.GB_KLIPY_APP_KEY` to the public app key.
3. Keep the key in the browser configuration only. KLIPY requires standard API requests and media loading to originate from the end-user browser; do not proxy the API, cache, mirror, or re-host its media.
4. Test with the key's testing tier (100 API requests/hour), then request production access from KLIPY as needed.

## Picker
- Trending GIFs, GIF search, and sticker browsing are included.
- Search input placeholder is `Search KLIPY`.
- Visible attribution: `Powered by KLIPY`.
- Content filter is set to `medium`; KLIPY content-filter and partner-dashboard keyword/block settings should also be configured.
- Results use provider URLs as delivered and are attached through the existing validated media token flow.
- Manual HTTPS media attachment remains available without a configured key.

## Limits
- This integration only supports formats the existing chat media renderer can display (GIF, PNG, JPEG, WEBP).
- It does not proxy requests, cache or retain media, or transform provider URLs.
- API keys are client-side app keys; never use private credentials or server secrets here.
