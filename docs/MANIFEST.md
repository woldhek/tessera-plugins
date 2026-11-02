# The manifest: tessera-plugin.yaml

The manifest tells the Tessera app what a plugin is, what it may do and what to show. It holds no code and no words:
names, labels and hints are keys into the plugin's `translations/<language>.json`, part `app`.

The app checks every manifest with the same code before it shows a plugin (`screen_manager/app/plugin_manifest.py` in
the Tessera repository; `tools/check.py` runs that very file). This page describes what that code checks, field by
field. The check stops at the first problem and names where it is, such as `tiles[0].options[2].default: must be
between 0 and 30`.

- A field the check does not know is an error, at every level, so a typo shows up at once. A field whose name starts
  with `x-` is ignored, for notes of your own.
- A list that is left out is an empty list.
- `tools/check.py` is strict: it refuses a topic or a feature it does not know, and requires `topics`. The app reads the
  index leniently, so a newer index never hides a plugin: an unknown topic is left out, an unknown feature a plugin
  needs is kept (the plugin then fits no screen), and an unknown feature a plugin brings is left out.

## A complete example

This is `plugins/ov_departures/tessera-plugin.yaml` exactly as it is in this repository (version 1.2.3), and it passes
the check:

```yaml
# The plugin's manifest: who made it, what it may do, and what the app shows. No code and no words: every name, label
# and hint is a key into translations/<language>.json, part "app". docs/MANIFEST.md has every field.
id: ov_departures
version: 1.2.3
api: "0.7"
icon: bus
maintainer: MaxGramser
license: MIT
stage: example
topics: [travel]
requires:
  esphome: 2026.6.2
boards: any
flash_kb: 12

permissions:
  network: [v0.ovapi.nl]
attributes: [cloud]
privacy: https://github.com/MaxGramser/tessera-plugins/tree/main/plugins/ov_departures#privacy

tiles:
  - id: next
    name: tile_name
    icon: bus
    sizes: { min: 1x1, max: 3x2 }
    memory: 1200
    data: departures
    example: tile_example
    # How the editor draws the tile from its data (it cannot run the C++): the first departure's line in a badge,
    # where it goes, and the minutes until it leaves.
    preview: { badge: "{line}", title: "{to}", countdown: at }
    options:
      - { id: stop, kind: text, label: stop, hint: stop_hint }
      - { id: line, kind: choice, options_from: lines, label: line, hint: line_hint }
      - { id: walk, kind: number, min: 0, max: 30, step: 1, unit: min, default: 0, label: walk, hint: walk_hint }
      - { id: late, kind: toggle, default: true, label: late, hint: late_hint }

fetch:
  # The departures of one stop, every minute. OVapi answers with every vehicle that passes the stop in the next hour
  # or so, keyed by journey; the add-on keeps the ones still to come, of the chosen line (all lines when none is
  # chosen), soonest first, and sends six of them to the screen. The screen counts down on its own clock.
  - id: departures
    url: http://v0.ovapi.nl/tpc/{stop}
    every: 60s
    map:
      items: "$.{stop}.Passes[*]"
      fields:
        line: LinePublicNumber
        to: DestinationName50
        at: { path: ExpectedDepartureTime, as: epoch, tz: Europe/Amsterdam }
        plan: { path: TargetDepartureTime, as: epoch, tz: Europe/Amsterdam }
        state: TripStopStatus
      where: { line: "{line}" }
      skip: { state: [PASSED, CANCEL] }
      sort: at
      limit: 6
  # The lines that stop there, for the choice in the editor's inspector.
  - id: lines
    url: http://v0.ovapi.nl/tpc/{stop}
    every: 1h
    map:
      items: "$.{stop}.Passes[*]"
      value: LinePublicNumber
      label: [LinePublicNumber, DestinationName50]
```

It names `api: "0.7"` and builds on today's core, which offers plugin API 0.24: no minor after 0.7 changed a name
([FIRMWARE_API.md](FIRMWARE_API.md), "Versions"). The fetch is plain `http` because OVapi offers nothing else and the
fetch carries no secret.

## The fields

### Words used on this page

| Word | Means |
|---|---|
| An id | 1 to 32 of `a-z`, `0-9` and `_`, starting with a letter. The ids of the tiles, the options of one tile, the inputs, the parts, the fetches, the cards, the tap actions and the top bar items are each unique in their list. |
| A text key | 1 to 48 of `a-z`, `0-9` and `_`, starting with a letter. `translations/en.json`, part `app`, must have a text that is not empty for every key the manifest names, and for `name` and `summary`. |
| An icon | A Material Design Icons name, lowercase words joined with `-` (`bus-clock`), from Tessera's icon set ([below](#icon)). |
| A domain | A Home Assistant domain, `a-z` and `_` only (`calendar`, `media_player`). |
| A feature | One of the features in the table under "Features" below. |

### Who and what

| Field | Required | What |
|---|---|---|
| `id` | yes | The plugin's id. Unique in the index, the same as its folder and its component, and never changed. |
| `version` | yes | Three numbers, `1.0.0`. Raise it for every change that reaches screens, and give it a `## 1.0.0` heading with its lines at the top of `CHANGELOG.md` ([MAKING_A_PLUGIN.md](MAKING_A_PLUGIN.md#the-changelog)). |
| `api` | yes | The plugin API it was written for, as major and minor in quotes: `"0.8"`; the plugin API 0.24 is the one the core offers now (on Tessera's dev branch; the release has an older one until the next). Without quotes YAML reads a number, which the check refuses. It builds on a core with the same major and at least that minor, unless a minor after its own up to the core's is listed as a break (0.4, where a name changed its meaning; a form a minor takes away, as 0.24 did four, fails in the compiler at its call instead); `tools/check.py` refuses an `api` newer than the core it checks against, and one from before such a minor ([FIRMWARE_API.md](FIRMWARE_API.md), "Versions"). |
| `icon` | yes | <a id="icon"></a>A Material Design Icons name from Tessera's icon set (`screen_manager/app/tile_icons.py` in the Tessera repository, such as `bus`, `train`, `calendar`, `thermometer`, `lightbulb`). The screen's icon font holds only that set. Tiles and top bar items take it when they name no icon of their own. |
| `maintainer` | yes | The GitHub name of whoever looks after the plugin, 1 to 39 characters. `tools/check.py` refuses the template's `your-github-name` in any folder but `template/`. |
| `license` | yes | An SPDX name that goes with AGPL-3.0: `MIT`, `Apache-2.0`, `BSD-2-Clause`, `BSD-3-Clause`, `ISC`, `MPL-2.0`, `LGPL-2.1-or-later`, `LGPL-3.0-or-later`, `GPL-3.0-or-later`, `GPL-3.0-only`, `AGPL-3.0-or-later`, `AGPL-3.0-only`, `Unlicense`, `0BSD`, `CC0-1.0`. |
| `stage` | no | How far along it is, in your word: `stable` (ready for every day), `beta` (works, still finding its feet) or `example` (there to show what a plugin can do and to learn from). The app shows a Beta or Example badge. Default: `beta`. |
| `topics` | yes, for `tools/check.py` | What it is about: one or two of the topics below, `[travel]` or `[home, calendar]`. The Plugins page shows them as chips to filter by. |

The plugin's name and one-line summary are not fields: they are `name` and `summary` in `translations/en.json`, part
`app`, which every plugin must have.

### What it is about: `topics`

One or two, the first the one that fits best; a topic named twice counts once. `tools/check.py` and the index require
at least one.

| Topic | What belongs there |
|---|---|
| `time` | Clocks, countdowns, timers, alarms, the date. |
| `weather` | Forecasts, rain, wind, air quality, pollen, tides. |
| `calendar` | Agendas, events, birthdays, holidays, days something happens. |
| `home` | The house itself: rooms, appliances, chores, the bins, who is home. |
| `energy` | Electricity and gas prices, solar panels, batteries, what the house uses. |
| `travel` | Public transport, traffic, flights, charging stations, the way to work. |
| `money` | Stocks, currencies, crypto, a budget. |
| `sports` | Scores, fixtures, standings, workouts. |
| `news` | Headlines, feeds, messages of a service. |
| `media` | Music, radio, podcasts, TV, what is playing. |
| `photos` | Pictures, albums, a picture of the day. |
| `fun` | Games, quotes, trivia, something to smile at. |
| `voice` | Speaking and listening: a voice assistant, a speaker, a microphone, sounds. |
| `tech` | The network, servers, printers, devices and how they are doing. |

There is no catch-all. A plugin that fits none asks for a new topic here, in an issue or a pull request: a topic is
added in the Tessera app too, so it comes with a core release. `tools/check.py` refuses a topic it does not know. The app
reads leniently: a topic it does not know yet is left out, never the plugin, and a plugin without topics shows under
none.

What a plugin adds is no topic: the app reads it from the manifest ("The type of plugin", below).

### What it needs and brings

| Field | Default | What |
|---|---|---|
| `requires.esphome` | none | The oldest ESPHome it builds with, written as ESPHome writes its versions: `2026.6.2` (a year of four digits, then two numbers). |
| `requires.psram` | `false` | `true` or `false`. `true` for a plugin that needs a board with PSRAM (not the CYD). |
| `requires.plugins` | `[]` | Ids of plugins that must be on the screen too, at most 8. They come along: the app adds them from the index with the one that needs them, each in the newest version that fits the screen, in the same build, and asks one yes for all of them. Each must be in the index and made for at least one of the plugin's boards (`any` fits every board); no plugin needs itself, and plugins never need each other in a circle (`tools/check.py` checks the last three across the index). |
| `requires.features` | `[]` | Features the screen must have, from the table below. A screen has a feature from its board or from a plugin that brings it. When it lacks one, the plugin of the index that brings it and fits the screen comes along; with several the person chooses one, with none the plugin does not fit that screen. At least one plugin in the index (or a board) must bring each, for one of the plugin's boards. |
| `provides` | `[]` | Features the plugin brings for others, from the table below. A plugin cannot need a feature it brings itself, and never brings `camera_sensor`. |
| `boards` | `any` | `any`, or a list of 1 to 32 board keys from Tessera's `boards.yaml` (`[cyd, guition]`) for a plugin with hardware of one board. |
| `flash_kb` | `0` | About how much flash it adds, in KB, a number from 0 to 8192. The app uses it to say whether it fits a 4 MB board. |

### Features: `provides` and `requires.features`

A feature is a promise about one ESPHome component and its id. A plugin that needs a speaker finds it as `ts_speaker`,
whichever plugin or board brings it, the way ESPHome's own voice assistant takes whatever speaker it is given.

| Feature | ESPHome component | Id | Since |
|---|---|---|---|
| `speaker` | `speaker:` | `ts_speaker` | 0.7 |
| `microphone` | `microphone:` | `ts_microphone` | 0.7 |
| `media_player` | `media_player:` | `ts_media_player` | 0.7 |
| `camera` | `esp_video_camera:` | `ts_camera` | 0.8 |
| `camera_sensor` | none: a board's own | `${CAMERA_I2C}`, the I2C bus of the sensor | 0.8 |

- **Bringing one.** `provides: [speaker]`, and `plugin.yaml` makes that component with exactly that id
  (`speaker: - platform: i2s_audio` with `id: ts_speaker`). `tools/check.py` checks it.
- **Needing one.** `requires.features: [speaker]`, and `plugin.yaml` uses the id (`speaker: ts_speaker`) without
  making it.
- **Using one when it is there.** A part with `features: [speaker]` (below): the plugin works without it, and does more
  on a screen that has one.
- **One of each per screen.** A screen has one speaker: one plugin or its board brings it. Two plugins in the index may
  bring the same feature; a person then chooses one per screen, and the app never puts a second one on it.
- **Coming along, and going.** What a plugin needs comes along when it is added, in one build. A plugin another one
  needs is removed only together with it (the app asks), and a plugin that only came along is offered to go as well.
- **Boards.** A board may bring a feature itself: the reTerminal D1001 brings a speaker, a microphone and a media
  player (Tessera's `features/audio.yaml`). A plugin that brings one the board has does not fit that screen.
- **Only a board.** `camera_sensor` is hardware: the camera's sensor, which the board powers and whose I2C bus it names
  in the substitution `CAMERA_I2C`, and the pin that clocks it in `CAMERA_XCLK_PIN` (-1 when the sensor has its own
  clock). A plugin needs it (`requires.features: [camera_sensor]`) and uses `i2c_id: ${CAMERA_I2C}`; no plugin brings
  it, and the check refuses it under `provides`.
- **`ts_` is Tessera's.** An id that starts with `ts_` belongs to the core (`ts_touch`) or to a feature. A plugin makes
  one only for a feature it provides.

`tools/check.py` refuses a feature it does not know. An app that does not know a feature yet keeps one a plugin needs,
which then fits no screen, and leaves out one a plugin brings.

### What it may do

A plugin's C++ is built into the screen's firmware and runs with every right the firmware has, so a plugin names what
it does. The app shows it before anyone adds the plugin, and asks again when an update asks for more (a change of
`permissions` or `attributes`).

| Field | What |
|---|---|
| `permissions.network` | The hosts its fetches reach, at most 8: public host names only (`api.example.org`, written in lowercase by the check), never an address and nothing on a home network (`localhost`, `homeassistant`, names ending in `.local`, `.lan`, `.home`, `.internal` or `.localdomain`). A fetch to any other host is refused. |
| `permissions.read_entities` | Home Assistant entities the screen itself reads (an ESPHome `homeassistant` sensor), at most 32. `"{calendar}"` stands for the entity a person chose in the input `calendar`. |
| `permissions.home_assistant_actions` | Home Assistant actions the screen calls (`tessera::action`), at most 32. |
| `permissions.ha_commands` | Home Assistant commands the app may ask on the plugin's behalf (`tessera::send`), at most 8: `call_service:<domain>.<service>` for an action that answers (`call_service:calendar.get_events`), or a websocket command of lowercase words joined with `/` (`history/history_during_period`). A websocket command whose first word is one of `auth`, `config`, `subscribe_events`, `subscribe_trigger`, `execute_script`, `call_service`, `fire_event`, `render_template`, `supervisor`, `hassio`, `get_config`, `lovelace`, `person`, `backup`, `cloud`, `application_credentials`, `repairs`, `blueprint`, `trace` or `validate_config` is refused: those read or change Home Assistant itself. Every one asked is logged. |
| `attributes` | Any of `cloud` (uses a service outside the home), `commercial`, `ai-developed`. A plugin with `permissions.network` has `cloud`. |
| `privacy` | An `https://` link to what the service sees. Required with `cloud` or `permissions.network`. A section of the README is fine. |

**What is enforced, and what is the plugin's word.** The app enforces two of these: a fetch reaches only the hosts of
`permissions.network`, and `tessera::send` reaches only the commands of `permissions.ha_commands`. The screen does not
check `read_entities` or `home_assistant_actions`: code built into the firmware can do what the firmware can. Those two
are the maker's honest description, which a person reads before adding the plugin ([PUBLISHING.md](PUBLISHING.md),
"What listing means").

### What a person fills in: `inputs`

Asked when the plugin is added to a screen. At most 8.

```yaml
inputs:
  - { id: api_key, kind: secret, scope: all, label: api_key, hint: api_key_hint }
  - { id: pin, kind: gpio, scope: screen, label: pin }
  - { id: calendar, kind: entity, domains: [calendar], label: calendar }
```

| Field | What |
|---|---|
| `id`, `kind`, `label` | Required. `label` is a text key. |
| `kind` | `secret` (kept by the app, never shown again, never on the screen or in YAML; used in a fetch), `text`, `gpio` (a free pin of the board), or `entity` (an entity of `domains`, for the plugin's own `homeassistant` sensors). |
| `domains` | For `entity`, and required there; never on another kind. At most 8 domains, `[calendar]`. The editor offers the entities of those domains. |
| `scope` | `all`: asked once for every screen. `screen`: per screen. Default: `all` for a secret, `screen` for the rest. |
| `hint` | Text key under the field. |

A `text`, `gpio` or `entity` input reaches the screen's build as a substitution in capitals: input `pin` is `${PIN}` in
`plugin.yaml`. A `secret` never does. Give each a default in `plugin.yaml` (`defaults: { PIN: GPIO22 }`) so a build
without the value still works.

An `entity` input is built into the screen: the same entity for every tile of the plugin on that screen, and another
one is a new build. An entity a person chooses per tile, which the app sends and follows without a build, is a tile's
option of kind `entity` (0.15, below).

A fetch may name an input as a placeholder, but the app fills in only secrets there (a secret with `scope: screen` is
the one filled in for the tile's screen). A value a fetch needs that is not a secret is a tile's option.

### Optional parts: `parts`

A part is an extra ESPHome file a person turns on per screen (large test tools, for example). At most 4.

```yaml
parts:
  - { id: tests, file: parts/tests.yaml, label: tests, hint: tests_hint, flash_kb: 440, default: false }
  - { id: answer_aloud, file: parts/aloud.yaml, label: aloud, features: [speaker], default: true }
```

| Field | What |
|---|---|
| `id`, `file`, `label` | Required. `file` is a `.yaml` file inside the plugin, its path of `a-z`, `0-9`, `_`, `-` and `/` without `..` (`parts/tests.yaml`). `tools/check.py` checks that it is there and holds to the rules of `plugin.yaml` ([LIMITS.md](LIMITS.md), "plugin.yaml"). |
| `hint` | Text key under the part. |
| `flash_kb` | About how much flash it adds, in KB, 0 to 8192. Default 0. |
| `default` | `true`: on when the plugin is added. Default `false`. |
| `features` | Features the part uses (0.7): it is offered, and built, only on a screen that has each. A voice plugin answers out loud with a part that needs a speaker, and listens without one. |

### Tiles: `tiles`

At most 8.

| Field | Required | What |
|---|---|---|
| `id` | yes | The tile type's id; `add_tile("<id>", ...)` in the C++ uses the same. A layout calls it `plugin:<plugin>.<id>`. |
| `name` | yes | Text key: what the library and the inspector call it. |
| `icon` | no | From Tessera's icon set; the plugin's icon when left out. The editor shows it, and a screen without the plugin draws it on its placeholder. |
| `sizes` | yes | `{ min: 1x1, max: 2x2 }`, columns x rows, each 1 to 9; `min` is not larger than `max` in either. The editor offers the sizes in between that fit the screen's grid. `full: true` is accepted, but neither the app nor the editor uses it yet. |
| `memory` | yes | What one tile costs of the screen's layout memory, a whole number of bytes from 64 to 16384. The screen and the app add it to the layout's budget, and the build refuses a value outside that range too. About 400 for a few labels, 1200 for a list. |
| `domains` | no | Up to 8 domains of the entity the tile belongs to (0.2), `[calendar]`, as a tap action and an input of kind `entity` name theirs. The inspector offers the entities of those domains, also of a domain Tessera draws no tile for; the tile gets the entity's state, name and `attributes`, again at every change. Its entity id in `TileContext::entity`, for such a domain, needs the firmware after 0.56.0 ([FIRMWARE_API.md](FIRMWARE_API.md), "A tile of an entity"). |
| `attributes` | no | With `domains`: the attributes the tile gets, at most 16, each a name of `a-z`, `0-9` and `_` of up to 48 characters, not starting with a digit. One named `..._at`, `..._time` or `...date` that holds a moment comes as seconds since 1970. A list comes whole when it fits: texts and numbers, not objects. |
| `fields` | no | With `domains` (0.5): values taken out of the entity's attributes with the paths and kinds of a fetch's map ([FETCH.md](FETCH.md)), 1 to 8, each named apart from `attributes`, `state` and `name`. `{ prices: { path: "raw_today[*].value", as: numbers } }` turns a list of 96 objects into one list of 96 numbers, which fits where the objects would not. |
| `has_attributes` | no | With `domains` (0.5): attributes an entity must have to be offered in the inspector, at most 8, named as `attributes`. `[raw_today]` lists the price sensor instead of every sensor in the house. It names attributes only: there is no way
yet to offer the sensors of one `device_class` (every CO2 sensor), so a common attribute such as `unit_of_measurement`
still offers every sensor that has one. Only the list: the tile keeps its entity when an attribute is gone for a while (an entity that is unavailable has none). |
| `data` | no | The id of the fetch whose answer the tile gets in `on_state`. |
| `pictures` | no | Places for pictures on the tile (0.10), 1 to 8: how many the tile makes at most with `ui::picture` ([FIRMWARE_API.md](FIRMWARE_API.md), "Pictures on a tile"). The app paints a picture only for a tile that has this, at most this many, and only from a host of `permissions.network` or, for a tile with `domains`, Home Assistant's own pictures of its entity (`entity_picture`, or from 0.17 `cover:`, `camera:` and `entity:` of that entity, which need no `attributes`). A plugin whose pictures come from a host names it in `permissions.network`, which gives it the attribute `cloud` and asks for a `privacy` link. Default: 0, no pictures. |
| `example` | no | Text key: a line the editor shows on the tile's placeholder. |
| `preview` | no | How the editor draws the tile from its data, so the page in the editor looks like the glass (it cannot run your C++). Only with `data` or `domains`. Any of `badge`, `title`, `value` and `countdown`, each a text of 1 to 64 characters. `badge`, `title` and `value` are templates of the first item's fields (`"{line}"`, `"{to}"`; for a tile of an entity `{state}`, `{name}`, its `attributes` and its `fields`), and every `{name}` in them must be such a field. `countdown` names a field with `as: epoch` (for a tile of an entity also an attribute named `..._at`, `..._time` or `...date`), and the editor counts down to it in whole minutes (the editor's clock moves every 30 seconds). `value` or `countdown`, not both. Without a preview the editor shows the icon, the name and `example`. |
| `options` | no | At most 12 options the inspector shows, below. |

An option:

| Field | What |
|---|---|
| `id`, `kind`, `label` | Required. `kind` is `text`, `choice`, `number`, `toggle` or (0.15) `entity`; `label` is a text key. |
| `hint` | Text key under the option. |
| `default` | The value when nothing is chosen. It must fit the option: `true` or `false` for a toggle, a number from `min` to `max` for a number, a text of at most 64 characters for a text or a choice, and for a choice with `choices` one of their values (or `""`). |
| `choices` | For `choice`: `[{ value: "a", label: key }]`, at most 48, each with both: `value` a text of 1 to 64 characters, `label` a text key. |
| `options_from` | For `choice`: the id of a fetch of this plugin that fills the list (its `map` has `value` and `label`). A choice has either `choices` or `options_from`, never both or neither. |
| `multiple` | For `text` and `choice` (0.12): the option keeps a list of up to this many values, 2 to 16, in the order the person put them (they drag them into another). A text list keeps texts; a choice keeps picks (below). Its default is `[]`; a text list may name a list of texts. A fetch uses a list through `each` or `{name\|,}` ([FETCH.md](FETCH.md)). |
| `search` | For `choice` with `options_from` (0.12): `true` finds the choices by what the person types. The editor sends it, from two characters on, as `{search}` of that fetch: `url: https://api.deezer.com/search/artist?q={search}`. Without `multiple` the option keeps one pick. No option of the tile may be called `search`. |
| `min`, `max`, `step`, `unit` | For `number`: the range (both required, `min` less than `max`), the step (more than 0, default 1), and a unit of up to 8 characters. |
| `domains`, `attributes` | For `entity` (0.15): the domains whose entities the person may choose, 1 to 8 (`[sensor]`), and the attributes the tile reads of the chosen one, as a tile's own `attributes`. Only in a tile's options, and only with `api: "0.15"` or later. |

A field that belongs to another kind is an error (`min` on a text option, `choices` on a number). A text option's value
is at most 64 characters. The tile gets every option in `context.options` of `create()`, with the defaults filled in.
Options are also the placeholders of the tile's fetch: `{stop}` in a URL is the option `stop`.

**What an option keeps** (0.12). A text, number or toggle keeps that value; a choice its value. A choice with `multiple`,
or one found by `search`, keeps **picks**: `{"value": "9635624", "label": "Billie Eilish"}`, the label being the words the
person saw in the list. A list from a fetch may be gone or other next week, so the words are kept with the value; for the
plugin's own `choices` the label is the choice's text in the editor's language. Each value and label is at most 64
characters, and a list holds each value once. All options of a tile together are at most 1280 bytes of JSON: they travel
to the screen with the tile's data in one message, and the editor refuses a value that would not fit.

An option of kind `entity` (0.15) keeps the id of the entity chosen, `"sensor.grid_power"`, or `""`. The inspector offers
the entities of its `domains` in a searchable list. The app sends the chosen one with the tile's data under `entities`
and the option's id, with its state, its name, the named attributes and `updated`, and sends the tile again when it
changes ([FIRMWARE_API.md](FIRMWARE_API.md), "What `on_state` gets"). Another choice needs no build. A tile of several
sources (an energy dashboard: the grid, the solar panels, the battery) has one such option for each:

```yaml
options:
  - { id: grid, kind: entity, domains: [sensor], attributes: [unit_of_measurement], label: grid }
  - { id: solar, kind: entity, domains: [sensor], attributes: [unit_of_measurement], label: solar }
```

```yaml
options:
  - { id: artists, kind: choice, multiple: 8, options_from: artist_search, search: true, label: artists }
  - { id: tickers, kind: text, multiple: 6, label: tickers, default: [AAPL] }
```

```json
{ "artists": [ { "value": "9635624", "label": "Billie Eilish" }, { "value": "27", "label": "Daft Punk" } ],
  "tickers": [ "AAPL", "ASML" ] }
```

### Fetches: `fetch`

At most 8. [FETCH.md](FETCH.md) explains them in full.

| Field | Required | What |
|---|---|---|
| `id` | yes | The fetch's id; a tile names it in `data`, an option in `options_from`. Every fetch named there must be described here. |
| `url` | yes | At most 512 characters. `https://`, or `http://` for a fetch that carries no secret. The host is fixed (no placeholder in it), one of `permissions.network`, with no user, no password and no port other than 80 or 443. Placeholders such as `{stop}` name an option of a tile that uses the fetch (through `data` or `options_from`) or an input; `{name\|,}` joins the values of a list option (0.12), and `{search}` is what the person types for a choice with `search`. A secret may stand in the query, never in the path. |
| `headers` | no | Up to 8, each a text of at most 512 characters, with placeholders: `{ Authorization: "Bearer {api_key}" }`. |
| `every` | yes | How old an answer may get: a whole number with `s`, `m`, `h` or `d` (`30s`, `5m`, `1h`, `1d`). At least 30 seconds. |
| `each` | no | An option with `multiple` of every tile that uses the fetch (0.12): one ask per value, `{<option>}` in the URL being that value, and the answers come together as one list ([FETCH.md](FETCH.md), "Once per value"). Not for a fetch that fills a list of choices. |
| `map` | yes | What of the answer reaches the screen. |

The map's fields, in short ([FETCH.md](FETCH.md), "The map", has them in full):

| Field | What |
|---|---|
| `items` | Where the list is, a path that starts with `$`. Required for a fetch that fills a list of choices. |
| `fields` | 1 to 8 fields, each a path or `{ path, as, tz }`. `as` is `text` (default), `number`, `epoch` or `numbers` (0.5); `tz` only with `as: epoch`. In a fetch with `each`, `{ each: value }` or `{ each: label }` (0.12) is the value whose ask brought the item. Required for a fetch a tile shows. |
| `where`, `skip` | Keep, or drop, the items whose field (named by its key) is one of the values: a value or a list of values, texts or numbers, which may hold placeholders. Only with `items`. |
| `sort` | A field, with `-` in front for the other way round. Only with `items`. |
| `limit` | How many items at most, a whole number from 1 to 48. Default 48. Only with `items`. |
| `value`, `label` | For a fetch that fills a list of choices, instead of `fields`: the path, or a list of paths, of what the option stores and of what the list shows. `label` is `value` when left out. |

A fetch that fills a list of choices (an option's `options_from` names it) maps `value` and `label`; every other fetch
maps `fields`.

### Answers: `answers`

What the screen gets of the answer to a command of `permissions.ha_commands` (0.5): its fields, with the paths and kinds
of a fetch's map. A command without an entry here comes as Home Assistant answered it, bounded.

```yaml
permissions:
  ha_commands: ["call_service:nordpool.get_prices_for_date"]
answers:
  - command: call_service:nordpool.get_prices_for_date
    fields:
      prices: { path: "[*][*].price", as: numbers }    # 96 prices of a quarter of an hour
      start: { path: "[*][0].start", as: epoch }       # when the first one starts
```

`command` and `fields` are required. The command is one of `permissions.ha_commands`, mapped once; `fields` holds 1 to
8 fields. `on_message` then gets `{"re": 1, "ok": true, "result": {"prices": [83.1, 81.4, ...], "start": 1791410400}}`:
about 700 bytes instead of 8 KB of objects, of which a quarter would have fitted. At most 8 entries.

### Cards: `cards`

A screen of the plugin's own over the page ([FIRMWARE_API.md](FIRMWARE_API.md), "A card"). At most 8.

```yaml
cards:
  - { id: upcoming, name: card_name }          # wide: true for the whole width of the glass
  - { id: release, name: release_name, pictures: 1 }   # a place for a picture (0.13)
```

| Field | What |
|---|---|
| `id` | Required. `add_card("<id>", ...)` in the C++ uses the same. |
| `name` | Required. Text key: what the editor calls it. On the screen the card's title is the tile's name, or what `open_card` passes. |
| `wide` | `true`: as wide as the glass (a picture, a timeline); `false` (default): the width of Tessera's own cards. |
| `pictures` | Places for pictures on the card (0.13), 1 to 8: how many the card makes at most with `ui::picture` ([FIRMWARE_API.md](FIRMWARE_API.md), "Pictures on a card"). The app paints only for a card that has this, from a host of `permissions.network`, or Home Assistant's own pictures when the card was opened for an entity of the screen's layout. Default: 0, no pictures. |

### Tap actions: `tap_actions`

Something a person can set one of Tessera's own tiles to do on a tap, in the tile's inspector. At most 8.

```yaml
tap_actions:
  - { id: upcoming, label: tap_upcoming, domains: [sensor], card: upcoming }
```

| Field | What |
|---|---|
| `id` | Required. `add_tap_action("<id>", ...)` in the C++ uses the same. |
| `label` | Required. Text key: the choice in the inspector's tap list. |
| `domains` | Required. 1 to 16 domains: the kinds of tile it is offered for. |
| `card` | Optional: the id of a card of this plugin that it opens, for the editor's description. |

### Top bar items: `bar_items`

At most 4.

```yaml
bar_items:
  - { id: soon, label: bar_soon, icon: recycle, example: bar_example }
```

| Field | What |
|---|---|
| `id` | Required. `add_bar_item("<id>", ...)` in the C++ uses the same. |
| `label` | Required. Text key: the item's name under "From plugins" when adding to a top bar. |
| `icon` | From Tessera's set; the plugin's icon when left out. |
| `example` | Text key: what the editor's mockup of the bar shows. |

### Screensavers: `savers`

A plugin's screensavers (0.14, [FIRMWARE_API.md](FIRMWARE_API.md), "A screensaver"): what a screen shows over the whole
glass in standby. Each is either a **face** of a step of Tessera's own screensaver (`step: clock`, `media` or `photos`),
which draws what that step knows and which a person picks under Face in the step's drawer, or a **step of its own**
(`step: own`), a place in the screensaver's order that draws from the plugin's fetch and its entity, as a tile does. At
most 4. A manifest with `savers` names `api: "0.14"` or later.

| Field | Required | What |
|---|---|---|
| `id` | yes | The saver's id; `add_saver("<id>", step, ...)` in the C++ uses the same. The app and the screen call it `plugin:<plugin>.<id>`. |
| `name` | yes | Text key: what the Screensaver card calls it, under Face or in the list of steps. |
| `icon` | no | From Tessera's icon set; the plugin's icon when left out. |
| `step` | yes | `clock`, `media` or `photos` for a face of that step, `own` for a step of its own. The C++ registers it with the same step (`SaverStep::CLOCK`, `MEDIA`, `PHOTOS`, `OWN`). |
| `memory` | yes | What the saver costs while it is on the glass, a whole number of bytes from 64 to 16384, as a tile's `memory`: about 100 a label, 80 a block. |
| `pictures` | no | Places for pictures, 0 to 8 (default 0): how many the saver makes at most with `ui::picture`. The address `step:` is the step's own picture (the cover of the music, the photo of the photos), for a face of `media` or `photos`; any other address is on a host of `permissions.network`, or Home Assistant's own picture of a step of its own's entity. |
| `taps` | no | `true` when the saver has parts made with `ui::takes_taps`, which a finger taps without waking the screen (default `false`): keys of a player, a switch. Say it whenever the code calls `ui::takes_taps`. |
| `moves` | no | `true` when the saver moves itself so nothing stands on the same pixels for hours (a clock that turns, a picture that changes every minute); the screen then leaves it where it draws. Default `false`: the screen moves the whole saver up to a millimetre every minute. |
| `backdrop` | no | (0.14) `true` for a face of the music or the photos that draws over the step's own picture: the screen shows the cover or the photo over the whole glass under the saver, the photo moving slowly when the person chose Motion: Slow, and the saver's glass is clear. Only for `step: media` or `photos`. |
| `options` | no | At most 12 options the Screensaver card shows for it, as a tile's (above). The saver reads them in `SaverContext::options`; an option the person never touched may be missing there, so the C++ reads each with its default. |
| `example` | no | Text key: a line the editor shows for it. |
| `data` | only `own` | The id of the fetch whose answer the step gets in `on_state`. |
| `domains`, `attributes`, `fields` | only `own` | The entity the step belongs to, as a tile's (above): the Screensaver card offers the entities of those domains, and the step gets its state, name, attributes and fields. `attributes` and `fields` need `domains`. |
| `when` | only `own` | When the step has something to show: `always` (default), `data` (its fetch has an answer; needs `data`), or `{ state: [rainy, pouring] }` (its entity is in one of these states; needs `domains`, at most 16 states). While it has nothing, the app goes on to the next step. |

A step of its own has `data`, `domains` or both. A face has none of `data`, `domains`, `attributes`, `fields` and
`when`: it draws from its step, whose contract is in [FIRMWARE_API.md](FIRMWARE_API.md), "A screensaver".

A face of the clock, which every screen has (the CYD too):

```yaml
api: "0.14"
savers:
  - id: flip
    name: saver_name
    step: clock
    memory: 1000
    example: saver_example
    options:
      - { id: seconds, kind: toggle, label: seconds, hint: seconds_hint, default: true }
```

A face of the music, with the cover and keys (a screen with pictures only):

```yaml
api: "0.14"
permissions:
  home_assistant_actions: [media_player.media_play_pause, media_player.volume_mute, media_player.volume_up,
                           media_player.volume_down]
savers:
  - { id: deck, name: saver_name, step: media, memory: 1500, pictures: 1, taps: true }
```

A face of the photos:

```yaml
api: "0.14"
savers:
  - { id: photo, name: saver_name, step: photos, memory: 500, pictures: 1 }
```

A step of its own, from a weather entity, shown while it rains:

```yaml
api: "0.14"
savers:
  - id: rain
    name: saver_rain
    step: own
    memory: 800
    domains: [weather]
    attributes: [temperature, precipitation]
    when: { state: [rainy, pouring, lightning-rainy] }
```

A step of its own, from a fetch, shown while the fetch has an answer:

```yaml
api: "0.14"
permissions:
  network: [api.example.org]
attributes: [cloud]
privacy: https://example.org/privacy
fetch:
  - { id: quote, url: "https://api.example.org/today", every: 1h, map: { fields: { text: quote, by: author } } }
savers:
  - { id: quote, name: saver_quote, step: own, memory: 600, data: quote, when: data }
```

### Settings a person changes: `settings`

A setting is an ESPHome entity of the plugin's `plugin.yaml`. Home Assistant sees it like any entity of the screen, the
screen can show it on its own settings page, and the Tessera editor shows it in the plugin's details on the screen's
Plugins tab, under **Settings**, "On this screen". A change there takes effect at once, through Home Assistant, without
a build. At most 8.

```yaml
# tessera-plugin.yaml
settings:
  - { key: tap_sound, label: setting_tap_sound, hint: setting_tap_sound_hint }
  - { key: speaker_volume, label: setting_volume }
  - { key: test_wake_word, label: setting_test, hint: setting_test_hint, status: test_wake_word_result }
```

```yaml
# plugin.yaml: the entities those keys name
switch:
  - platform: template
    name: Tap sound                 # key tap_sound
    optimistic: true
    restore_mode: RESTORE_DEFAULT_ON
number:
  - platform: template
    name: Speaker volume            # key speaker_volume
    optimistic: true
    min_value: 0
    max_value: 100
    step: 5
    unit_of_measurement: "%"
button:
  - platform: template
    name: Test wake word            # key test_wake_word
    on_press:
      - text_sensor.template.publish: { id: my_plugin_test_result, state: "Listening" }
text_sensor:
  - platform: template
    name: Test wake word result     # status test_wake_word_result
    id: my_plugin_test_result
```

| Field | What |
|---|---|
| `key` | Required. The entity's `name` in `plugin.yaml` written as an id, the way ESPHome does it: lowercase, a space becomes `_`, anything but letters, digits, `_` and `-` becomes `_`. "Tap sound" is `tap_sound`, "Mic gain (dB)" is `mic_gain__db_`. The check takes 1 to 64 of `a-z`, `0-9` and `_`, so a name with a `-` gives a key it refuses: give settings plain names and the key is obvious. |
| `label`, `hint` | `label` is required. Text keys: the row's name and the line under it. |
| `status` | For a button (0.6): the key of a text sensor, written as `key`, whose state the row shows beside the button, so a test can say how it went ("Heard: Okay Nabu"). The editor reads it again a few times after a press. The check accepts it on any setting, but the editor shows it only beside a button. |

The kind of row follows from the entity's platform:

| Platform in `plugin.yaml` | The editor shows |
|---|---|
| `switch` | A switch |
| `number` | A number with its min, max, step and unit, with - and + |
| `select` | Its options as buttons, a dropdown from six options on (at most 48) |
| `text` (0.6) | A text field, saved with Enter or when it is left, within the entity's `min_length` and `max_length`; hidden when its `mode` is `password` |
| `button` (0.6) | A **Run** key, with its `status` beside it |

**How the app finds the entity.** It looks in Home Assistant's entity registry for an entity of that screen's device
whose name in ESPHome gives the key (0.6). A person who renames the entity in Home Assistant keeps the setting; so does
a screen with a name other than you expected. Do not build on Home Assistant's `unique_id` or the YAML `id`: neither is
what the key means. An entity Home Assistant's registry does not have is found by the end of its entity id.

**Settings, inputs or parts?**

| What the person changes | Use | A change |
|---|---|---|
| A value the plugin reads while it runs: a volume, a switch, a word, a test | `settings` with an ESPHome entity | Takes effect at once |
| A key or value the same for every screen, used by the app (a fetch's API key) | `inputs` with `scope: all` | Kept in the app; a secret never reaches the screen |
| A value the build needs: a pin, a model to build in | `inputs` with `scope: screen` | Builds the screen again |
| An optional piece of firmware | `parts` | Builds the screen again |

The editor shows all of them together in the plugin's details on the screen's Plugins tab: "On this screen" (the
settings), "For every screen" (inputs with `scope: all`) and "When building" (inputs with `scope: screen` and the
parts), with a **Save and build** key that lights up when something differs from what the screen was built with.

The app changes only an entity a plugin on that screen names here, of that screen's own device. A `status` is only
shown, never set.

## The type of plugin

The app shows a plugin as one of four types, one tab each on the Plugins page. A maker does not write it: the app
reads it from the manifest, so it is never wrong.

| Type | When |
|---|---|
| Tiles | It has `tiles`. |
| Screensavers | No tiles, and it has `savers` (0.14). |
| Hardware | No tiles, and it brings a feature (`provides`), asks for a pin (an input of kind `gpio`), or names its `boards`. |
| Functions | The rest: something for the whole screen, such as top bar items, tap actions, a voice or a sound. |

## Which minor brought a field

An app older than the minor that brought a field does not know it, and refuses a manifest that uses it, as it refuses a
typo. So a plugin names in `api` at least the minor of every field it uses; the check does not work that out for you.
These are the fields whose minor the core records:

| Minor | Manifest |
|---|---|
| 0.2 | Tiles of an entity (`domains`, `attributes`), `cards`, `tap_actions`, `bar_items`, `permissions.ha_commands` |
| 0.5 | A tile's `fields` and `has_attributes`, `answers`, the field kind `numbers`, a field path that starts with a list step (`[*].price`) |
| 0.6 | Settings of kind text and button, a button's `status`, settings found by their name in the entity registry |
| 0.7 | `topics`, `provides`, `requires.features`, a part's `features`, plugins of `requires.plugins` that come along |
| 0.8 | The features `camera` and `camera_sensor` |
| 0.10 | A tile's `pictures` |
| 0.12 | An option's `multiple` and `search`, a fetch's `each`, `{name\|,}` and `{search}` in a URL, a field `{ each: value }` |
| 0.13 | A card's `pictures` |
| 0.14 | `savers`, with every field of a saver |
| 0.15 | An option of kind `entity` in a tile's `options`, with `domains` and `attributes` |

What each minor brought to the C++ is in [FIRMWARE_API.md](FIRMWARE_API.md), "Versions".

## Limits at a glance

| What | Most |
|---|---|
| An id | 32 characters |
| A text key | 48 characters |
| `maintainer` | 39 characters |
| Topics | 2 |
| Plugins a plugin needs | 8 |
| Board keys in `boards` | 32 |
| `flash_kb` (the plugin's, a part's) | 8192 |
| Hosts in `permissions.network` | 8 |
| Entities in `read_entities`, actions in `home_assistant_actions` | 32 each |
| Home Assistant commands (`ha_commands`) | 8 |
| Inputs | 8, each with up to 8 `domains` |
| Parts | 4 |
| Tiles per plugin | 8 |
| A tile's size | 9 x 9 cells |
| A tile's `memory` | 64 to 16384 bytes |
| A tile's `domains` | 8 |
| A tile's `pictures`, a card's | 8, each from an address of at most 512 characters, at most 2 MB |
| Attributes of a tile's entity | 16, and 8 `fields`, 8 `has_attributes` |
| A text of a `preview` | 64 characters |
| Options per tile | 12 |
| Choices of an option | 48, each value 64 characters |
| A text option's value | 64 characters |
| Values of a list option (`multiple`) | 16, each value and label 64 characters |
| A tile's options together | 1280 bytes of JSON |
| What a person types in a search | 64 characters, asked from 2 |
| A number option's unit | 8 characters |
| Fetches | 8 |
| A fetch's URL | 512 characters |
| A fetch's headers | 8, each 512 characters |
| How often a fetch asks | every 30 seconds |
| Fields per mapped item | 8 |
| Items in a mapped list (`limit`) | 48 |
| Answers mapped | 8 |
| Cards, tap actions | 8 each; a tap action's `domains` 16 |
| Top bar items | 4 |
| Screensavers | 4; each 64 to 16384 bytes of `memory`, 8 `pictures`, 12 options, 8 `domains`, 16 `attributes`, 16 states in `when` |
| Settings | 8; a key or `status` 64 characters; a select's options 48 |
| Bytes of a tile's data on the wire | about 2.6 KB (the last items are dropped to fit) |
| Text of a mapped field | 48 bytes |
| An answer of a web service | 64 KB, in 10 seconds |
