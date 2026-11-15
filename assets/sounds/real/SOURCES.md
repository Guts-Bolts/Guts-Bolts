# Candidate CC0 sound libraries

This file tracks vetted sources for the next built-in sound refresh. The links below
open playable catalogs or official asset pages so contributors can audition individual
sounds before importing them. A library's general licence statement is not a substitute
for checking the licence and provenance of each selected file at import time.

## Approved candidate sources

| Source | Licence stated by publisher | Best candidates for Guts & Bolts | Preview / download |
|---|---|---|---|
| FoleyKit | Rendered sounds are CC0; generator code is MIT | Soft UI feedback, nature ambience, rain, wind, water, birds, footsteps, machinery | https://rudqlsdla.github.io/foleykit/ |
| Kenney Audio | CC0 1.0 on the individual Kenney packs; five original OGG files imported | Interface clicks, open/close, toggle and notification feedback | https://kenney.nl/assets/interface-sounds |
| Kaino SFX | CC0 1.0 | Nature/weather, ambience beds, footsteps, doors, household sounds, vehicles, gentle interface feedback | https://kaino.dev/library/sfx |
| Lots of Sounds | CC0 1.0 for catalog recordings | Birds, wind, rain, water, room tone, machinery and everyday Foley | https://www.lotsofsounds.com/sounds |
| SpritePilot | CC0 1.0 | Interface clicks, footsteps, movement, water, and ambient loops | https://spritepilot.com/free-game-assets/sound-effects (150 sounds; OGG-only ZIP: https://spritepilot.com/free-assets/sound-effects/spritepilot-sound-effects-ogg.zip?v=2qulu) |

## Import policy

- Prefer natural, non-combat sounds for the default engine palette.
- Do not import flesh impacts, weapon sounds, gore sounds or exaggerated combat stingers
  as general-purpose defaults.
- Preview every candidate before importing it. A catalogue description alone is not
  sufficient evidence that a sound fits the engine.
- Keep the original filename, creator/source, source-page URL, licence, and any
  processing notes in the asset credits.
- Use Ogg Vorbis for bundled effects where practical. Keep loops gapless and test the
  loop boundary after conversion.
- Measure loudness and peak level after conversion. Do not normalize every clip by peak
  alone: a quiet recording with one sharp transient can become disproportionately loud.
- Keep source archives out of the runtime build; only include the selected, reviewed
  audio files.

## Current status

A first five-file Kenney UI subset is now present in the runtime asset directory and
is automatically included by the existing CMake OGG asset glob. The files are copied
unchanged and their exact upstream filenames are recorded in CREDITS.md. This is a
metadata/provenance-based first pass, not a claim that the recordings have been
auditioned; listening review and desktop/mobile playback checks are still required.
The other four source libraries remain candidate sources only. Lots of Sounds currently
requires an account for catalog downloads.
