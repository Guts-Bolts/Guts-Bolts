# SpritePilot CC0 sound import manifest

**Status:** candidate list only. These recordings have not yet been downloaded or auditioned in this repository. Do not wire them into default sound IDs until they pass listening and technical checks.

- Publisher: SpritePilot
- Catalogue: 150 sound effects across 10 groups
- Licence: CC0 1.0, according to the publisher
- Format: OGG-only archive, approximately 4.6 MB; 48 kHz mono source files
- Catalogue / individual previews: https://spritepilot.com/free-game-assets/sound-effects
- OGG archive: https://spritepilot.com/free-assets/sound-effects/spritepilot-sound-effects-ogg.zip?v=2qulu

## First-pass candidates

Selected by catalogue description for a calmer general-purpose engine palette. These are not endorsements of the unheard recordings.

| Catalogue name | Intended use | Catalogue duration |
|---|---|---:|
| Jump | Character movement | 0.8 s |
| Footsteps on grass | Grass surface movement | 3.0 s |
| Footsteps on stone | Stone surface movement | 2.93 s |
| Footsteps on wood | Wood surface movement | 2.96 s |
| Steps on metal | Metal surface movement | 3.0 s |
| Steps on sand | Sand surface movement | 3.0 s |
| Steps in snow | Snow surface movement | 3.0 s |
| Splash | Water interaction | 1.92 s |
| Swim | Swimming movement | 2.99 s |
| Button click | UI click | 0.15 s |
| Tap | UI interaction | 0.5 s |
| Toggle | UI toggle | 0.1 s |
| Menu open | UI transition | 0.46 s |
| Menu close | UI transition | 0.47 s |
| Confirm | UI confirmation | 1.0 s |
| Notification | UI notification | 0.96 s |
| Unlock | UI unlock | 0.83 s |
| Typing | Text-entry feedback | 2.97 s |
| Slider tick | UI slider feedback | 0.85 s |
| Popup | UI popup | 0.25 s |
| Page turn | Document / inventory interaction | 0.84 s |
| Message | UI message | 0.97 s |
| Select | UI selection | 0.11 s |
| Rain | Weather ambience loop | 13.2 s |
| Wind | Weather ambience loop | 13.2 s |
| Forest | Outdoor ambience loop | 13.2 s |
| Cave | Cave ambience loop | 13.2 s |
| Ocean | Water ambience loop | 13.2 s |
| River | Water ambience loop | 13.2 s |
| Night | Night ambience loop | 13.2 s |
| Desert wind | Desert ambience loop | 13.2 s |
| City at night | Urban ambience loop | 13.2 s |
| Rain on a roof | Weather ambience loop | 13.2 s |

## Deliberately excluded from the default palette

The catalogue also contains combat, weapon, gore-adjacent, explosive, and harsh impact effects. Those are not part of this first-pass general-purpose import set. Specialized game projects can add their own sounds without making them the engine's default.

## Import and validation checklist

For each chosen recording:

1. Download the individual OGG from the catalogue archive.
2. Listen to the complete clip at normal playback volume; reject harsh, distorted, excessively loud, or contextually unsuitable recordings.
3. Check codec, sample rate, channel count, duration, peak, RMS/LUFS, leading/trailing silence, and clipping.
4. For ambience, verify loop continuity and listen across multiple loop boundaries.
5. Preserve the upstream filename and record the exact source page, CC0 1.0 licence, and any processing in `CREDITS.md`.
6. Convert or rename only after review; keep a traceable mapping from upstream filename to G&B asset filename.
7. Add only accepted clips to the runtime asset set and map them to existing sound IDs without breaking compatibility.
