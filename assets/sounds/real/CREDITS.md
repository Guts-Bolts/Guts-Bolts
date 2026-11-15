# Real sound effects

Recorded sounds the engine uses for hits, breaking, footsteps, fire, breathing,
splashes, thunder and bullets going past. All of them are **CC0** (public domain: free to
use, change and share, no credit needed). We still say where they came from,
because it's nice. Existing recordings were cut shorter, made mono and saved as Ogg;
the Kenney interface subset below is preserved as its original Ogg files.

| Files | Pack | By | Where |
|---|---|---|---|
| `hit_metal_*`, `hit_glass_*`, `hit_stone_1`, `break_glass_*`, `break_wood_*`, `break_stone_*` | 75 CC0 breaking / falling / hit sfx | rubberduck | https://opengameart.org/content/75-cc0-breaking-falling-hit-sfx |
| `hit_stone_2`, `step_wood_*`, `step_wet_*` | 100 CC0 SFX #2 | rubberduck | https://opengameart.org/content/100-cc0-sfx-2 |
| `hit_wood_*` | 35 wooden cracks/hits/destructions | qubodup | https://opengameart.org/content/35-wooden-crackshitsdestructions |
| `hit_flesh_*` | 8 wet squish, slurp impacts | qubodup | https://opengameart.org/content/8-wet-squish-slurp-impacts |
| `step_stone_*`, `step_sand_*` | Fantozzi's Footsteps (Grass/Sand & Stone) | qubodup | https://opengameart.org/content/fantozzis-footsteps-grasssand-stone |
| `step_snow_*` | 42 Snow and Gravel Footsteps | qubodup | https://opengameart.org/content/42-snow-and-gravel-footsteps |
| `step_mud_*` | 25 CC0 mud sfx | rubberduck | https://opengameart.org/content/25-cc0-mud-sfx |
| `step_metal_*` | Metal footsteps on concrete | Thimras | https://opengameart.org/content/metal-footsteps-on-concrete |
| `splash_*` | 40 CC0 water / splash / slime SFX | rubberduck | https://opengameart.org/content/40-cc0-water-splash-slime-sfx |
| `fire_loop` | Fire Crackling | AntumDeluge | https://opengameart.org/content/fire-crackling |
| `breath_tired` | Breathing Tired | mikeask | https://opengameart.org/content/breathing-tired |
| `thunder_*` | Rain + Long Thunder | wuxiascrub | https://opengameart.org/content/rain-long-thunder |
| `quake_rumble` | Earthquake and Open Sesame | mieki256 | https://opengameart.org/content/earthquake-and-open-sesame |
| `storm_wind` | Wind Whoosh Loop | SketchMan3 | https://opengameart.org/content/wind-whoosh-loop |
| `whiz_*` | Swishes Sound Pack | artisticdude | https://opengameart.org/content/swishes-sound-pack |
| `click_1`, `open_1`, `close_1`, `toggle_1`, `notification_1` | Interface Sounds 1.0 (original OGG files; CC0-1.0) | Kenney | https://kenney.nl/assets/interface-sounds |

The five Kenney files are mapped as follows: `click_1.ogg` = `click_001.ogg`,
`open_1.ogg` = `open_001.ogg`, `close_1.ogg` = `close_001.ogg`,
`toggle_1.ogg` = `toggle_001.ogg`, and `notification_1.ogg` = `bong_001.ogg`.
The upstream pack's CC0 notice is included in its source repository's review record:
https://github.com/hipdiscovery/Free-Sound-Effects/blob/master/docs/SOURCES.md

Adding one: drop a short mono `.ogg` named `<sound>_<n>.ogg` in this folder (all the
`_<n>` takes of a sound are picked from at random) and list it here. CMake bakes every
file in this folder into the program by itself.
