# Docs

| Page | Read it when |
|---|---|
| [MAKING_A_PLUGIN.md](MAKING_A_PLUGIN.md) | You start. The files, the steps, how the pieces meet, common mistakes. |
| [MANIFEST.md](MANIFEST.md) | You write `tessera-plugin.yaml`. Every field and limit. |
| [FIRMWARE_API.md](FIRMWARE_API.md) | You write the C++. The API, the life of a tile, drawing, rules. |
| [FETCH.md](FETCH.md) | Your tile needs data from a web service. |
| [TRANSLATIONS.md](TRANSLATIONS.md) | You add words or a language. |
| [PREVIEW.md](PREVIEW.md) | You want to see it on every kind of screen, without one: the bench. |
| [TESTING.md](TESTING.md) | You want it on a screen. |
| [PUBLISHING.md](PUBLISHING.md) | Others should get it. |
| [LIMITS.md](LIMITS.md) | You wonder whether your idea fits. |

For AI assistants: [../AGENTS.md](../AGENTS.md) has the rules and the order to read these in, and
[../llms.txt](../llms.txt) lists every page.

## Words these pages use

The docs use a few plain words of their own. In the usual terms:

| These docs say | The usual term |
|---|---|
| The app | The Tessera app in Home Assistant (a Home Assistant add-on), with its editor in the browser |
| The core | Tessera itself: the firmware every screen runs (its ESPHome component `smart_display`) and the app |
| The glass, on the glass | The display; shown on the display right now |
| A tile | A widget in one or more cells of a page's grid |
| A card | Two things: the drawn box a tile lives in on the page, and a screen of a plugin's own that opens over the page (a modal view) |
| A hand's width | The width of Tessera's own cards that open over the page, narrower than the whole display |
| Moments | Lifecycle callbacks: `on_ready`, `on_interval`, `on_standby`, `before_update`, `on_press`, `on_step` and the others |
| A tick, `on_tick` | The once-a-second update callback of a tile, a card or a screensaver |
| Standby | The display dimmed or off after a while without a touch |
| The screensaver | What the display shows over all of it in standby, instead of the dimmed tiles (a screen saver) |
| A step | One of the screensaver's sources in the order a person puts them: the music, the photos, a camera, the clock, or a plugin's step of its own; the first that has something to show is shown |
| A face | Another look for a step the screen already has (a plugin's clock, player or photo view), drawn from that step's data |
| A tap, a hold | A click and a long press on the touch screen |
| The top bar | The status bar at the top of a page |
| The library, the inspector | The editor's palette of tiles, and its properties panel for the chosen tile |
| Layout memory | The memory budget a screen has for the tiles of its pages |
| A fetch | An HTTP GET of JSON that the app makes for a plugin and maps to fields |
| A feature | A capability (a speaker, a camera) promised as one ESPHome component with a fixed id |
| A part | An optional ESPHome package of a plugin, turned on per screen |
| The index | The plugin registry, `index.json`, which the app reads |
| The origin | The repository (and folder) a plugin comes from |
| Words, texts | Translatable strings |
| The build | An ESPHome compile of a screen's firmware, followed by an update of the screen |
| The CYD | The "Cheap Yellow Display" ESP32-2432S028, the smallest board Tessera runs on: 2.8 inches, 4 MB of flash, no PSRAM |
