# Instructions for AI assistants (and a summary for people)

You are helping someone make or change a plugin for Tessera, the ESP32 touch screens for Home Assistant. Read this
file first, then the docs in the order below. Everything here is checked by `tools/check.py`; run it before you say a
plugin is done.

## What a plugin is, in five sentences

1. A plugin is a folder: a manifest (`tessera-plugin.yaml`), an ESPHome package (`plugin.yaml`), an ESPHome component
   in C++ (`components/<id>/`), texts (`translations/<language>.json`), a `README.md` and a `CHANGELOG.md`.
2. The screen builds the component into its firmware; the component registers tile types (and cards, top bar items,
   screensavers, ...) with the core through
   `tessera::Plugin` (`components/smart_display/plugin_api.h` in the Tessera repository; a plugin includes it as
   `esphome/components/smart_display/plugin_api.h`).
3. The Tessera app (in Home Assistant) reads only the manifest: it shows the plugin, its options and its README, and
   carries out the plugin's `fetch` (JSON from a web service) with its own code.
4. A tile of a plugin is `plugin:<plugin id>.<tile id>` in a screen's layout. The core gives it a card's drawing area
   while its page is on the glass, hands it the data the app sent (`on_state`), and ticks it once a second (`on_tick`).
5. The plugin API is 0.24 on Tessera's dev branch; the release, Tessera 0.4.89 (firmware 0.57.0), has 0.14 until the
   next one. A manifest names the lowest minor whose
   parts it uses: `api: "0.8"`, `"0.9"` for `Font::DIGITS`, `ui::covers`, `Tone::GOOD`, `ui::set_fill` and
   `ui::tone_color`, `"0.10"` for pictures on a tile, `ui::text` and `tap_point()`, `"0.11"` for `on_press` and
   `on_step`, `"0.12"` for options that keep lists (`multiple`, `search`), a fetch with `each` or `{name|,}`,
   `tessera::action` with a JSON object, and a card's `on_tap`, `"0.13"` for pictures on a card, `"0.14"` for screensavers
   (`savers`, `add_saver`, `ui::takes_taps`, `tessera::wish`), `"0.15"` for a tile's options of kind `entity` (entities
   the person chooses per tile, sent under `entities`), `"0.16"` for `tessera::wake`, and `"0.17"` for pictures in one
   system (`Placeholder::NONE`, `on_picture`, the addresses `cover:`, `camera:`, `entity:` and `step:`, and
   `tessera::capturing` for a plugin that uses the screen's own camera), `"0.18"` for a popup (`show_popup`, `Popup`,
   `ui::animate`), `"0.19"` for `tessera::camera_turn`, `"0.20"` for `Capture::SENSING` (the camera only sensing,
   the dot a ring), `"0.21"` for `tessera::squeeze` (a busy screen's governor), and `"0.22"` for `tessera::own_frame` (the screen's own
   camera on a tile of its own), `"0.23"` for `Capture::SOUND` (someone hears the microphone: the live tiles slow). A plugin builds on a core with the
   same major and at least its minor, unless a minor after its own is listed as a break: while the major is 0 a name may
   change its meaning, and the core lists those minors (only 0.4 so far); a form a minor takes away fails in the
   compiler at its call instead. So a plugin of 0.7 builds on a core of 0.24, one of 0.3
   does not (`docs/FIRMWARE_API.md`, "Versions"). 0.24 took away a second form of four things
   (`on_touch`, the key and value `tessera::action`, `saver:picture`, `memory: { bytes }`): one form for each thing. A tile's entity of a domain Tessera draws no tile for (`plant`,
   `todo`) reaches `TileContext::entity` and `tessera::action` only with the firmware after 0.56.0, on Tessera's dev
   branch until its next release (`docs/FIRMWARE_API.md`, "A tile of an entity").

## Where each kind of thing lives

| The plugin needs | Use | Read |
|---|---|---|
| Data from a web service | A `fetch` the app carries out, mapped to fields | docs/FETCH.md |
| Data Home Assistant already has (a sensor, a calendar, prices in attributes) | A tile of an entity: `domains`, `attributes`, `fields`, `has_attributes` | docs/MANIFEST.md, "Tiles" |
| Pictures on a tile (posters, a photo, a logo) | `ui::picture` and `ui::set_picture` with the tile's `pictures` in the manifest; the app fetches them from a host of `permissions.network` or from Home Assistant | docs/FIRMWARE_API.md, "Pictures on a tile" |
| The answer of a Home Assistant action (`calendar.get_events`, `nordpool.get_prices_for_date`) | `permissions.ha_commands`, `tessera::send`, and `answers` to map it | docs/MANIFEST.md, "Answers"; docs/FIRMWARE_API.md |
| To do something in Home Assistant | `tessera::action`, named in `permissions.home_assistant_actions` | docs/FIRMWARE_API.md |
| A setting a person changes | An ESPHome entity in `plugin.yaml`, named in `settings` | docs/MANIFEST.md, "Settings a person changes" |
| A value or key filled in once | `inputs` (`scope: all` for every screen, `screen` per screen) | docs/MANIFEST.md, "What a person fills in" |
| Hardware on the board | ESPHome components in `plugin.yaml`, `boards`, optional `parts` | docs/MAKING_A_PLUGIN.md |
| A speaker, microphone or media player for other plugins | `provides`, and the component with the feature's id (`ts_speaker`) in `plugin.yaml` | docs/MAKING_A_PLUGIN.md, "Features" |
| A speaker, microphone or media player another plugin or the board brings | `requires.features`, or a part with `features` when the plugin also works without it | docs/MANIFEST.md, "Features" |
| Another plugin on the same screen | `requires.plugins`: the app adds it with yours | docs/MANIFEST.md, "What it needs and brings" |
| A screensaver: another clock, player or photo view in standby, or a step of its own (the rain radar while it rains) | `savers` in the manifest (a face: `step: clock`, `media` or `photos`; a step of its own: `step: own` with `data` or `domains` and `when`), `tessera::Saver` and `add_saver`; the step's cover or photo through `saver:picture`; keys through `ui::takes_taps` and `tessera::wish` | docs/FIRMWARE_API.md, "A screensaver"; docs/MANIFEST.md, "savers" |
| Something over the screen for a moment: a voice assistant's question and answer, a timer that went off | A popup (0.18): `tessera::show_popup` with a `tessera::Popup`, a close key in its corner, `ui::animate` for what moves | docs/FIRMWARE_API.md, "A popup" |
| A server, an account, a live session (OAuth, a websocket, a voice model) | A Home Assistant integration of its own, which the plugin reaches through its entities and actions | docs/MAKING_A_PLUGIN.md, "Where the backend lives" |

## Read in this order

1. [docs/MAKING_A_PLUGIN.md](docs/MAKING_A_PLUGIN.md): the files, the steps, trying it on a screen.
2. [docs/MANIFEST.md](docs/MANIFEST.md): every field of `tessera-plugin.yaml` and its limits.
3. [docs/FIRMWARE_API.md](docs/FIRMWARE_API.md): the C++ API, the life of a tile, the drawing rules.
4. [docs/FETCH.md](docs/FETCH.md): data from a web service, the map language, the limits.
5. [docs/PREVIEW.md](docs/PREVIEW.md): the bench, to see what the plugin draws on every kind of screen.
6. [docs/TRANSLATIONS.md](docs/TRANSLATIONS.md), [docs/TESTING.md](docs/TESTING.md),
   [docs/PUBLISHING.md](docs/PUBLISHING.md), [docs/LIMITS.md](docs/LIMITS.md) when you need them.

The two plugins to copy from: [`template/`](template) (a tile without data, the smallest complete plugin) and
[`plugins/ov_departures/`](plugins/ov_departures) (a tile with data from a web service, a list of choices and a
countdown). For the other parts: [`plugins/waste_collection/`](plugins/waste_collection) (a tile of an entity, a card,
a tap action, a top bar item, settings), [`plugins/voice_assist/`](plugins/voice_assist) (features it needs, sounds of
its own, ESPHome components of its own, a top bar item in colour, a popup with parts that move) and [`plugins/screen_camera/`](plugins/screen_camera)
(a board's hardware through a feature only a board brings, and a driver beside the plugin's own component) and
[`plugins/picture_frame/`](plugins/picture_frame) (pictures on a tile: an entity's own picture or an address, cover or
contain, with the name over it). Screensavers: [`plugins/flip_clock/`](plugins/flip_clock) (a face of the clock, no
pictures, every board), [`plugins/vinyl_deck/`](plugins/vinyl_deck) (a face of the music: the cover, keys that take taps,
`wish()`) and [`plugins/photo_clock/`](plugins/photo_clock) (a face of the photos, the photo over the whole glass).

## Rules that are never optional

- **Start from the template**: `python3 tools/new_plugin.py <id>`. Do not write the folder from memory. For a
  repository of its own, `python3 tools/new_plugin.py <id> ../<repo>`: it adds an AGENTS.md and CLAUDE.md there that
  point back to this repository's rules and tools.
- **Topics say what it is about**: one or two of the list in `docs/MANIFEST.md`, "What it is about", always. Never a topic of your own; ask for a new one in an issue. What the plugin adds (tiles, a function, hardware)
  is no topic: the app reads it from the manifest.
- **A feature is a promise.** A plugin that `provides` a speaker makes `speaker:` with `id: ts_speaker`; one that needs
  it uses `ts_speaker` and never makes it. Ids that start with `ts_` are Tessera's, for nothing else.
- **plugin.yaml adds, it never takes over**: no `wifi`, `api`, `ota`, `logger`, `esphome`, `http_request`,
  `web_server`, `packages`, `external_components` or the other keys of `docs/LIMITS.md`, "plugin.yaml" (whose one
  exception is ESPHome's update of an ESP32-P4's Wi-Fi co-processor, in a file of its own).
- **The id is the same everywhere, and for ever**: the folder in `plugins/`, `id:` in the manifest, the component
  folder `components/<id>/`, the YAML key in `plugin.yaml` and the C++ namespace `esphome::<id>`. Lowercase letters,
  digits and `_`, starting with a letter, at most 32. Screens, layouts and secrets know the plugin by it: never rename
  it. A fork that keeps the id is another plugin of that id (another origin) and never gets the original's secrets.
- **No words in code or manifest.** Every name, label and hint in the manifest is a key into
  `translations/en.json`, part `app`. Every word on the screen is a key of part `screen`, read with
  `plugin->text("key")`. English is complete; other languages may lack keys.
- **Draw like the rest of the screen**: sizes with `tessera::ui::mm()` / `ui::px()`, colours with a theme role
  (`theme::INK`, `theme::MUTED`, `theme::ACCENT`, ...), only the screen's fonts (`tessera::Font`). Never a colour as a
  number, never a font of your own, never an LVGL click handler (use `Tile::on_tap`), never a timer or task (use
  `on_tick`), never a network connection from the screen (data comes through `fetch`, a picture through
  `ui::set_picture`: the app fetches both).
- **Nothing exists off the glass.** Make every LVGL object in `create()` inside `context.parent`. The core deletes them;
  a destructor only frees your own memory.
- **Paint, do not rebuild.** `on_state` and `on_tick` set texts and positions with `ui::set_text`, `ui::set_font`,
  `ui::set_color`, which change a property only when it differs.
- **Name what the plugin may do** in `permissions`: every host a fetch reaches in `network`, every Home Assistant entity
  the screen reads in `read_entities`, every action it calls in `home_assistant_actions`. A plugin with `network` has
  the attribute `cloud` and a `privacy` link.
- **A screensaver** (0.14) keeps its own rules, as much as a tile keeps the ones above:
  - **Draw only in `show()`**: every part in `context.parent`, the stage the screen moves; nothing outside it.
  - **Paint, do not rebuild, in `on_tick`** and `on_state`: set only what changed (the digits of a new minute, the dot
    of a second). The glass draws only the boxes that changed, and a saver stands there all night.
  - **`ui::takes_taps` only for parts that must work without waking the screen** (a player's keys), at least
    `ui::mm(10)` across, with `taps: true` in the manifest. Every other part lets the finger wake the screen.
  - **`tessera::wish()` for every value a finger changes** (play and pause, mute, on and off), with the key painted
    from `on_state` only; never an action with a value written into the saver first. A step (volume up) is
    `tessera::action`. Name every action, the wish's too, in `permissions.home_assistant_actions`.
  - **No timers, tasks or animations of its own**: `on_tick`, once a second, is its clock.
  - **The dark look** is the saver's whatever the screen's: draw with theme roles, never a light ground over the glass.
  - **`moves: true` only when the saver really moves itself** every minute; otherwise the screen moves it. The bench
    warns when a saver looks the same two minutes later.
  - **A face draws only from its step's contract**, a step of its own from its fetch and entity, through the app; read
    every option with its default (`c.options["seconds"] | true`).
- **A popup** (0.18) keeps its own rules:
  - **Lay it out from the top; the card follows** its parts up to `context.height`. Hide a part that has nothing to say
    instead of leaving a gap; show the first or the last lines of a long text yourself, it never scrolls.
  - **The close key is the core's**, in the room's top right corner: keep your first row `context.corner` from the
    right, and give an icon or disc at the top left `context.key` as its size, so the two stand on one line.
  - **Motion only here**, with `ui::animate` and `ui::still`, never a timer or `lv_anim` of your own; keep moving parts
    small and still them when their moment is over.
  - **It lights the screen**: show one because a person did something, or behind a setting of the plugin's own, on by
    default only when a person asked for what it shows (the voice assistant's Show popup).
- **Memory inside the chip is scarce** (since API 0.10): keep texts and lists a tile holds as `tessera::Text` and
  `tessera::List`, which take PSRAM; a `std::string` member stays in the memory Wi-Fi needs (docs/FIRMWARE_API.md,
  "Memory").
- **Memory**: the manifest's `memory` for a tile is what one tile costs in the screen's layout memory. Keep it honest:
  a few hundred bytes for a handful of labels, about 1200 for a list.
- **Secrets stay in the app.** An API key is an input of `kind: secret`; it goes into a fetch's header or query, never
  into the URL path, never to the screen, never into YAML. A key for a service Home Assistant talks to (OpenAI,
  Anthropic, Spotify) stays in that Home Assistant integration; the plugin never asks for it.
- **Settings are ESPHome entities.** A value a person changes after the build (a volume, a switch, a word, a test
  button) is a template entity in `plugin.yaml` and a line in the manifest's `settings`, whose `key` is the entity's
  name written as an id ("Tap sound" is `tap_sound`). Never a setting of the plugin's own in the app, never a global
  the editor cannot see.
- **Home Assistant is the backend.** No plugin code runs in the Tessera app. What needs a server goes into Home
  Assistant: an existing integration, or one of your own.
- **Plain English in READMEs**, with a `## Set up` section of numbered steps a person can follow in the Tessera app.
  A quote (`> ...`) right under the title is a notice: the app shows it as a box at the top of the plugin's details,
  for what the plugin costs the screen or what to do before adding it (the Bluetooth proxy's README has one).
- **A picture for the Plugins page.** Every plugin has a `store.png` beside its manifest, made with
  `tools/store_image.py` once the bench passes, and made again when what the plugin shows changes. It is a thumbnail:
  the name and the `summary` of `translations/en.json` (part `app`) large on the left, the plugin on three screens on
  the right. So keep the summary to one sentence of what a person gets (about 110 characters at most), and put the
  example that shows the plugin best first in `preview.yaml`: the picture lays its pages out with the first examples
  first. Look at the picture before you commit it (docs/PUBLISHING.md, "The picture on the Plugins page").
- **A changelog line for every version.** Raise `version` and add its `## <version>` heading at the top of
  `CHANGELOG.md`, with a few bullet lines of what a person notices and what they must do. The app shows them when it
  offers the update (docs/MAKING_A_PLUGIN.md, "The changelog").

## Branches

Two branches, as Tessera has, and each has its own `index.json`:

- **main** is what every Tessera app reads. A plugin here builds on the firmware Tessera has released.
- **dev** is what Tessera's dev app reads (it falls back on main while dev has no index). A plugin that needs what only
  Tessera's dev firmware has, such as a newer plugin API, goes to dev: there the dev app installs and updates it the
  ordinary way, on screens that build Tessera's dev. On main it would fit no screen until Tessera's next release, and
  the index has one version per plugin, so nobody could install it in the meantime.
- **The release:** Tessera's release takes this repository along (`tools/release.py publish` there, with
  `tools/release_plugins.py`; its docs/RELEASING.md, "The release", step 6). Before it publishes, it refuses when the
  check is not green on dev, a plugin of dev's index does not fit the release's plugin API, or dev does not merge into
  main by itself (outside `index.json`, `likes.json` and `stats.json`). Right after, it merges dev into main, builds main's index
  against the released core and checks it (every plugin fits, every plugin dev offered is there, the check is green on
  main), and moves dev up to main. Nothing of that is done by hand, unless a step failed: then Tessera's
  `tools/release.py plugins --yes` finishes it. A change that needs no newer firmware goes to main and is then merged
  into dev, so dev never lacks what main has. Never rebase dev: screens are pinned to its commits.
- **The index** of each branch is checked against the Tessera its app builds (`TESSERA_REF`: main against Tessera's
  main, dev against its dev), and built by `.github/workflows/index.yml` after a push to it (the hourly run, for
  community releases, is main's), or by hand: `GITHUB_TOKEN=$(gh auth token) python3 tools/build_index.py`, then
  commit `index.json` on that branch.

## When you are done

```sh
pip install pyyaml                      # once: the check reads YAML (Homebrew's Python refuses: use the bench's,
                                        # tools/.cache/preview-venv/bin/python tools/check.py, which has it)
python3 tools/check.py plugins/<id>     # must print "<id>: ok"
python3 tools/preview.py plugins/<id> --strict   # the bench: exit 0, no warning in its findings (docs/PREVIEW.md)
python3 tools/store_image.py plugins/<id>        # its picture on the Plugins page: plugins/<id>/store.png
```

**The bench is how you see what you draw.** Run it after every change to the C++, the manifest or `preview.yaml`, not
only at the end, and with `--strict` (exit 2 while a finding is a warning). The first build of a screen takes minutes:
give it a timeout of 20 minutes or run it in the background; `--board cyd` alone is the quick round while the C++
changes. Never pass `--window` or `--watch`, which wait for a person. Read the findings it prints (also in
`.preview/summary.json`), then look at `.preview/overview.png` and the pictures of the smallest and the largest screen,
light and dark (`.preview/<screen>/tile-...png`, `card-...png`): a finding catches what can be measured, a picture what
looks wrong. Give the plugin a `preview.yaml` with data it really gets, so the pictures show it working, not waiting. A
plugin that needs or brings audio or a camera does not build on the bench: say so. docs/PREVIEW.md, "For an
assistant", has the exit codes, the files and the fields of `summary.json`.

**The picture comes last.** When the bench ends without a warning, run `tools/store_image.py`: it writes `store.png`
from the 10-inch, the 4-inch square and the CYD. `--from plugins/<id>/.preview` takes the pictures of the bench run you
just made (its four default screens include those three), so it takes seconds; without it, it benches them again.
Then open `store.png` and look: the name on one line or two, the summary whole (it is cut with an ellipsis only when
three lines of the smallest size do not hold it), and pages that show the plugin with data, not waiting. What it does
with each kind of plugin: tiles fill the three screens; a card shows on the square screen; a plugin with only settings
shows its settings page with its icon; items in the top bar get a loupe over the bar; a plugin of hardware the bench
cannot build (audio, a camera, Bluetooth) gets its icon, large, which is right for it. `check.py` reminds you while
`store.png` is missing, and checks it once it is there (a PNG of 16:9, at most 1 MB). Commit it with the plugin; a
plugin in a repository of its own publishes it with its next release.

The check also fails while the folder still has the template's placeholders: `maintainer: your-github-name`, the
LICENSE's `<year> <your name>`, and the README's line that it is the template.

Then build it on a real screen ([docs/TESTING.md](docs/TESTING.md)): push it to GitHub and add the repository's link
in the app (without a release that is a test of the default branch, and a newer push shows as an update), or copy the
folder into Home Assistant's config as `tessera-plugins/<id>/`. A plugin that passes the check but was never on a
screen is not done: say so, and say which board you did not try.
