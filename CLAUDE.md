# Notes for working on Guts&Bolts

- **Every update goes in the update log.** When a change ships, add a new file
  `worker/updates/<id>.js` (the website's Updates page; see
  `worker/updates/README.md`) with its own cool, specific name about what changed
  (like "Glass Lagoon" for the water update), a one-line summary and a few
  plain-words lines of what changed. Never add new entries to `worker/updates.js`:
  that file only keeps the older ones. Name the file after the id, and pick an id
  nobody has used.
- **Every piece of work goes on the roadmap.** Add a file `worker/roadmap/<id>.js`
  (see `worker/roadmap/README.md`) with `status: 'working'` when you start, and set
  it to `'done'` (with the version) in the pull request that finishes it. Ideas for
  later are `'planned'`. Never type them into the website instead.
- **Keep pull requests from conflicting.** Many PRs are open at once, so avoid
  editing the same lines as everyone else:
  - Update log: one file per update (above), never the top of `worker/updates.js`.
  - Checks for a new update go in their own test file
    (`worker/test/<feature>.test.mjs`), not at the end of `updates.test.mjs`.
  - AI tool checks go in a new file in `tools/ai_tool_tests/`, not at the end of
    `tools/test_ai_tools.py`.
  - `tools/i18n/strings.txt` is made by `python3 tools/i18n/extract.py`: if it
    conflicts, take main's copy and run the script again instead of fixing it by hand.
  - Translations in `website/lang/*.json` are kept in A-Z order: after adding
    some, run `python3 tools/i18n/check.py --sort`.
  - Before pushing, merge the latest `main` into your branch and fix any conflict.
- Say "Library", never "Marketplace".
- Keep the Guts&Bolts branding; don't copy Roblox's name or logos.
- Thumbnails are real renders or 2D pictures, never AI-generated images.
- Never put anyone's personal email address in the code or config.
- The server exists twice: `worker/server.js` (Cloudflare, the main one) and
  `src/server` (C++). A rule changed in one should be changed in the other.
