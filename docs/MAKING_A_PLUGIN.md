# Making a plugin

This page takes you from an idea to a plugin on your own screen. It assumes you know a little C++ and YAML; you do not
need to know Tessera's code.

## The idea first

A plugin is right for something that not every screen needs and that the screen can draw on its own:

- a tile with data from a web service (the next bus, today's waste collection, the energy price of your supplier);
- a tile that draws what Home Assistant already knows in a way Tessera's own tiles do not (a day of electricity prices
  as a chart, a calendar's next event);
- a tile that only needs the screen's clock (a countdown, a timer of your own);
- hardware on the board or on its free pins (a sensor, a relay, a speaker), with settings a person can change;
- a screensaver: another look for the clock, the music or the photos in standby, or a step of its own (the rain radar
  while it rains), below in "A screensaver";
- a popup (0.18): a card over the screen for a moment that wants the eye now (a voice assistant that listens and
  answers, a timer that went off), with a close key and parts that pulse, spin or ripple
  ([FIRMWARE_API.md](FIRMWARE_API.md), "A popup").

It is the wrong tool for:

- showing an entity the way Tessera's own tile for it already does: a normal tile shows it, no plugin needed (one
  that shows it another way, a CO2 sensor as a number with a coloured bar, is the second case above);
- a better card for a kind of entity everyone has (lights, thermostats): that belongs in Tessera itself, as a pull
  request on the main repository;
- anything listed in [LIMITS.md](LIMITS.md).

## The files

```
plugins/my_idea/
  tessera-plugin.yaml          the manifest: id, version, what it may do, its tiles, its fetches
  plugin.yaml                  what a screen gets: the component, configured
  components/my_idea/
    __init__.py                the ESPHome side: schema and code generation (a few lines)
    my_idea.h                  the plugin class
    my_idea.cpp                the tiles: how they draw
  preview.yaml                 optional: the data tools/preview.py shows the tiles with (docs/PREVIEW.md)
  translations/
    en.json                    every word: part "screen" (on the glass) and part "app" (in the editor)
    nl.json                    more languages, optional
  README.md                    what it does and how to set it up; the app shows it (a > quote under the title: a notice box)
  README.nl.md                 optional, per language
  CHANGELOG.md                 what each version changed; the app shows it with an update
  store.png                    optional: its picture on the Plugins page (tools/store_image.py, docs/PUBLISHING.md)
  CHANGELOG.nl.md              optional, per language
```

Every file has a counterpart in [`template/`](../template); read it next to this page.

## Step by step

### 1. Copy the template

```sh
pip install pyyaml                  # once: the tools read YAML with PyYAML
python3 tools/new_plugin.py my_idea
```

This makes `plugins/my_idea/` with every name changed. For a plugin in a repository of your own, give a folder:
`python3 tools/new_plugin.py my_idea ../tessera-my-idea`.

### 2. Describe it in the manifest

Open `tessera-plugin.yaml`. Set the `version` (start at `0.1.0`), `maintainer` (your GitHub name), `stage` (`beta` until people use it every day, then `stable`), `topics` (below), `icon` (a name from
Tessera's icon set, see [MANIFEST.md](MANIFEST.md#icon)) and describe your tile under `tiles`:

```yaml
tiles:
  - id: next
    name: tile_name                 # a key in translations/en.json, part "app"
    icon: bus
    sizes: { min: 1x1, max: 2x2 }   # the sizes the editor offers
    memory: 900                     # what one tile costs of the screen's layout memory
    data: departures                # the fetch whose answer the tile gets (leave out for none)
    options:                        # what the inspector shows
      - { id: stop, kind: text, label: stop, hint: stop_hint }
```

[MANIFEST.md](MANIFEST.md) lists every field. If the tile needs data from the internet, add a `fetch`
([FETCH.md](FETCH.md)).

**Choosing topics.** `topics` says what the plugin is about, so a person finds it among the others: one or two of
`time`, `weather`, `calendar`, `home`, `energy`, `travel`, `money`, `sports`, `news`, `media`, `photos`, `fun`, `voice`
and `tech` ([MANIFEST.md](MANIFEST.md), "What it is about"). Put the one that fits best first, and take a second only
when people would look for it there too: the next bus is `[travel]`, the bin day `[home, calendar]`. Say what it is
about, not what it adds: whether it is tiles, a function or hardware the app reads from the manifest. None fits? Ask for
a new topic in an issue here before you publish.

### 3. Write the words

Every key you used goes into `translations/en.json`:

```json
{
  "_meta": { "name": "English" },
  "screen": { "now": "now", "minutes": "{n} min" },
  "app": { "name": "Next bus", "summary": "When the next bus leaves.", "tile_name": "Next bus", "stop": "Stop" }
}
```

`app` needs `name` and `summary` plus every key the manifest names. `screen` is yours: what the C++ shows.
[TRANSLATIONS.md](TRANSLATIONS.md) has the rules for plurals and placeholders.

### 4. Draw the tile

In `components/my_idea/my_idea.cpp` a tile is a class with four moments:

```cpp
class NextTile : public tessera::Tile {
 public:
  void create(const tessera::TileContext &c) override;   // make the labels, once
  void on_state(JsonObjectConst data) override;          // the app sent new data
  void on_tick(uint32_t epoch) override;                 // once a second: set the texts
  void on_theme() override;                              // light or dark: set the colours
};
```

and the plugin registers it:

```cpp
void MyIdea::setup() { add_tile("next", [this]() { return new NextTile(this); }); }
```

[FIRMWARE_API.md](FIRMWARE_API.md) has the whole API and the rules for drawing.

### 5. Give it settings, if a person should change something

A value a person changes after the build (a volume, a switch, a word, a test) is an ESPHome entity in `plugin.yaml`
plus one line in the manifest's `settings`. The key is the entity's name written as an id:

```yaml
# plugin.yaml
switch:
  - platform: template
    name: Show seconds              # key show_seconds
    optimistic: true
    restore_mode: RESTORE_DEFAULT_OFF
```

```yaml
# tessera-plugin.yaml
settings:
  - { key: show_seconds, label: setting_seconds, hint: setting_seconds_hint }
```

The editor shows it in the plugin's details on the screen's Plugins tab, under **Settings**, and a change takes effect
at once. A switch, number, select, text or button (a button can show a text sensor beside it as its `status`, for a
test). Read the value in your C++ from the entity itself (`id(...)->state`), and draw the same rows on the screen's own
settings page with `settings(SettingsPage&)` ([FIRMWARE_API.md](FIRMWARE_API.md), "Settings rows"), so it can be
changed without Home Assistant too.

Something the build needs (a pin, a model to build in) is an `input` or a `part` instead: the editor shows those in
the same place, under "When building", with a **Save and build** key. [MANIFEST.md](MANIFEST.md), "Settings a person
changes", has the table of which to use.

### 6. Check it

```sh
python3 tools/check.py plugins/my_idea
```

It needs Python 3 with PyYAML (`pip install pyyaml`). It runs the Tessera app's own manifest check and the rules for the
C++. Fix what it says until it prints `ok`. It also fails while the plugin still has what only the template may have:
`maintainer: your-github-name`, `<year> <your name>` in `LICENSE` (fill it in, or delete the file for a plugin in
`plugins/` of this repository, which the repository's LICENSE covers), and the README's line that it is the template.
[TESTING.md](TESTING.md) says what else it checks.

### 7. See it on every kind of screen

```sh
python3 tools/preview.py --setup       # once
python3 tools/preview.py plugins/my_idea --open
```

The bench builds the real firmware of a small, a middle, a standing and a large screen with your plugin, gives its
tiles the data of your `preview.yaml`, and shows every tile at every size, light and dark, every card, the top bar and
the settings page on one page, with what it found wrong (a word too faint in dark, a label cut, a part outside its
tile). `--screen 1024x600@7` adds a glass of your own, `--grid max` the finest grid each screen takes, `--watch` builds
again on every save. [PREVIEW.md](PREVIEW.md) has it all.

### 8. Make its picture

```sh
python3 tools/store_image.py plugins/my_idea
```

The Plugins page shows a picture of every plugin that has one: `store.png` beside the manifest, like a video's
thumbnail. `store_image.py` makes it from the bench: the plugin's name and summary (`translations/en.json`, part `app`)
large on the left, and on the right a page of the plugin on a 10-inch screen, a 4-inch square one and the CYD, drawn by
the real firmware with the data of your `preview.yaml`. Put the example that shows the plugin best first in
`preview.yaml`, keep the summary to one sentence, look at the picture, and commit it with the plugin. A picture of your
own is welcome too ([PUBLISHING.md](PUBLISHING.md), "The picture on the Plugins page").

### 9. Put it on your screen

Two ways, both a test only you see, with the label **Test**:

- **From GitHub.** Push the plugin to a repository of your own and add its link in Tessera: Plugins, **Add with a
  link**. Without a release the app tests your default branch. It builds the commit it read, and a newer push shows as
  an update on the screen's Plugins tab within the hour, which one tap builds.
- **From a folder.** Copy the plugin's folder into Home Assistant's config, next to the `esphome` folder, as
  `/config/tessera-plugins/my_idea/`. Every build takes what the folder holds: after a change, open the plugin in the
  screen's Plugins tab and press **Build again**.

Add it to a screen; the app writes the screen's plugins file and builds the screen. Place its tile in Layout. Its
settings, inputs and parts are in its details on the screen's Plugins tab. [TESTING.md](TESTING.md) has both ways step
by step, and a build on your own computer.

### 10. Write the changelog

Every version that reaches screens gets a few lines in `CHANGELOG.md` (below, "The changelog"). Write them with the
version, before you publish it.

### 11. Publish it

See [PUBLISHING.md](PUBLISHING.md): a plugin in this repository through a pull request, or in a repository of your own,
added with a link or listed in the index.

## A screensaver

A plugin can bring a screensaver (0.14): what a screen shows over the whole glass while Auto standby dims
it. Tessera's app decides when it shows; your plugin decides how it looks. [FIRMWARE_API.md](FIRMWARE_API.md), "A
screensaver", has the whole contract, [MANIFEST.md](MANIFEST.md), "savers", every field.

**Pick the kind.**

| You want | Kind | Example |
|---|---|---|
| Another clock (flip cards, a word clock, a dial) | A face of the clock: `step: clock`. It gets the temperature and the row of entities the person chose for the clock, and works on every board, the CYD too. | [`plugins/flip_clock/`](../plugins/flip_clock) |
| Another look for the music that plays, with its cover and keys | A face of the music: `step: media`, `pictures: 1` for the cover (`step:`), `taps: true` for keys. Screens with pictures only. | [`plugins/vinyl_deck/`](../plugins/vinyl_deck) |
| Another look for the photos of a folder | A face of the photos: `step: photos`, `pictures: 1` for the photo (`step:`). Screens with pictures only. | [`plugins/photo_clock/`](../plugins/photo_clock) |
| Something no step shows: the rain radar while it rains, a quote of the day, the bins on the evening before | A step of its own: `step: own`, with a `fetch` (`data`) or an entity (`domains`), and `when` to say when it has something to show. | [MANIFEST.md](MANIFEST.md), "savers" |

A face is the safer choice: a screen without your plugin shows the step's own face, and the person keeps the steps and
the order they know.

**Write it.** Start from the template as for any plugin (`python3 tools/new_plugin.py <id>`), set `api: "0.14"`,
replace `tiles` with `savers`, and in the C++ replace the tile with a `tessera::Saver` and `add_tile` with
`add_saver("<id>", tessera::SaverStep::CLOCK, ...)` (the step the manifest names). Then keep to these rules, which the
bench checks where it can:

1. Make every part in `show()`, in `context.parent`; `on_state` and `on_tick` only paint what changed. The glass draws
   only the boxes that changed, and a saver stands there all night.
2. Draw with theme roles as anywhere: a saver is always drawn in the dark look, white on black.
3. Leave the moving to the screen (it moves the whole saver a millimetre every minute), unless the saver really moves
   itself every minute; then say `moves: true`.
4. A part a finger may tap without waking the screen is made with `ui::takes_taps`, and the manifest says `taps: true`.
   Keep such parts few and large (`ui::mm(10)` or more). Everything else wakes the screen.
5. A key that changes a value of an entity (play and pause, mute, on and off) calls `tessera::wish()`, never an action
   with a value written first; it paints the key from `on_state`. A step (volume up) is an action.
6. Read every option with its default (`c.options["seconds"] | true`): the app fills in the manifest's defaults, and the
   default in the C++ keeps the saver right on an app that does not.

**Try it.** Give `preview.yaml` a `savers` part with the data of its step ([PREVIEW.md](PREVIEW.md), "Screensavers"),
and run the bench: it shows each saver in standby on every screen that shows it, with the screen light and dark,
audits it as it audits a tile, and photographs it again two minutes later to see it does not stand still. On a real
screen: add the plugin, open the screen's Settings, the Screensaver card, turn on **Show in standby**, pick your face
under **Face** in its step's drawer (or add your step with **Add from plugins**), and wait for standby.

## The changelog

`CHANGELOG.md` says, per version, what a person notices. When the app offers an update of a plugin, it shows the lines
of every version after the one the screen runs, up to the new one, so someone can see what they get before one tap
builds it. Write a line for every release:

```markdown
# Changelog

## 1.1.0 - 2026-10-09
- A departure that runs late says by how much.
- New in the inspector: **Show delays**, on by default.

## 1.0.0 - 2026-10-07
- First version: the next departures from your stop.
```

- One `## <version>` heading per version, the version exactly as in `tessera-plugin.yaml` (three numbers), optionally
  followed by ` - <date>` as `YYYY-MM-DD`. The newest version first; the first heading is the version in the manifest.
- Under each heading a few short bullet lines (`- `; a long one goes on, indented by two spaces) of what a person
  notices: what came, what changed, what went, and what they must do ("fill in the stop code again"). Not how the
  code changed.
- An optional `# Changelog` title on the first line; nothing else outside the versions.
- `CHANGELOG.nl.md`, `CHANGELOG.de.md` and so on are optional, as for the README. A translation may lag behind, but
  names only versions the English one has.

`tools/check.py` checks all of it. The app reads at most 64 KB of it, as of a README.

## How the pieces meet

```
Home Assistant config                               Tessera app (in Home Assistant)
  esphome/kitchen.yaml        <- packages:            reads tessera-plugin.yaml (index, link or folder)
    tessera_plugins: !include kitchen.plugins.yaml    writes kitchen.plugins.yaml
  esphome/kitchen.plugins.yaml                        runs the plugin's fetches, sends each tile its data
     packages: plugin_my_idea  -> plugin.yaml
     external_components       -> components/         ESPHome builds the screen with the plugin in it
                                                       the screen's hello says: plugins [my_idea]
Screen
  core (smart_display)  <- tessera::Plugin registers tile "next"
  layout tile "plugin:my_idea.next" -> core makes a NextTile in that cell, hands it its data, ticks it
```

- The screen's own YAML gets one line under `packages:` once, and every build of that screen (the app's, ESPHome
  Device Builder's, a computer that shares the folder) builds the same plugins.
- The plugins file pins every plugin to one commit, except one from a folder. A newer version reaches a screen only
  when someone updates it in the app.
- The id is the plugin's name for ever: the firmware, the layouts and the secrets know the plugin by it. Never change
  it once the plugin is on a screen.
- A screen that does not have a plugin draws its tiles as a plain card with the tile's name and "Plugin missing". It
  never fails or restarts over it.

## Where the backend lives

No plugin code runs in the Tessera app: it holds the key to all of Home Assistant and runs only its own code. Home
Assistant is the backend. Pick the first row that covers what you need:

| What you need | How | The key or account lives |
|---|---|---|
| JSON from a public web service | A `fetch` in the manifest ([FETCH.md](FETCH.md)) | An input of `kind: secret`, in the app |
| What an entity in Home Assistant has, including lists in its attributes | A tile of an entity: `domains`, `attributes`, `fields` with `as: numbers`, `has_attributes` ([MANIFEST.md](MANIFEST.md), "Tiles") | In the integration behind the entity |
| The answer of a Home Assistant action (`calendar.get_events`, `media_player.search_media`, `nordpool.get_prices_for_date`) | `permissions.ha_commands`, `tessera::send` from the C++, and `answers` to keep only the fields you need | In that integration |
| To do something (turn on a light, play on a speaker) | `tessera::action`, named in `permissions.home_assistant_actions` | In that integration |
| A voice assistant, an AI model, a service that needs OAuth or a live connection | Home Assistant's own (Assist and its conversation agents, an integration that offers entities and actions), or a Home Assistant integration you write. The plugin uses its entities and actions like any other | In that integration, never in Tessera or on the screen |

So a plugin that talks to an AI model does not ask for its key: it uses an assistant set up in Home Assistant, and a
setting picks which one. A plugin that needs something Home Assistant does not offer yet gets a small integration of
its own, which also works without Tessera and is maintained on its own.

## Names a plugin.yaml may use

`plugin.yaml` is merged into the screen's own configuration, so it can point at parts the board already has. Only these
names stay the same on every board and in every update (Tessera's docs/PROFILES.md, "What an override may rely on"):

| Name | What |
|---|---|
| `my_display` | the display |
| `ts_touch` | the touch panel |
| `gpio_backlight_pwm`, `back_light` | the output that drives the backlight, and the light on it |
| `touch_bus` | the I2C bus the touch panel is on, on every board that has one (the M5Stack Tab5: `tab5_bus`) |
| `ts_speaker`, `ts_microphone`, `ts_media_player`, `ts_camera` | a feature, from whichever plugin or board brings it (below) |

A chip on the touch panel's bus (a sensor you added beside it) takes `i2c_id: touch_bus`. Any other id of a board file
can change in an update; give your own parts ids that start with your plugin's id (`my_idea_sensor`), so they never
meet one of Tessera's. Ids that start with `ts_` are Tessera's: a plugin
makes one only for a feature it brings.

## Features: a speaker, a microphone, a media player

Some plugins make hardware work, others use it: a voice plugin needs a speaker that another plugin, or the board,
brings. A feature is how they meet. It is a promise about one ESPHome component and its id, the same on every screen
([MANIFEST.md](MANIFEST.md), "Features").

**Bringing a feature.** Name it in `provides` and make the component with the feature's id in `plugin.yaml`.
`tools/check.py` refuses a plugin that provides a feature it does not make:

```yaml
# tessera-plugin.yaml
provides: [speaker]
```

```yaml
# plugin.yaml
speaker:
  - platform: i2s_audio
    id: ts_speaker                  # the feature's id, never another
    i2s_dout_pin: GPIO9
```

A screen has one speaker, so it has one plugin (or its board) that brings it. Another plugin may bring a speaker too,
for the same board; a person then chooses one of the two for each screen.

**Needing a feature.** Name it in `requires.features` and use the id without making it. A screen has the feature from
its board or from a plugin that brings it. When it lacks it, that plugin comes along with yours: by itself when one
fits the screen, the person's choice when several do. With none the plugin does not fit that screen:

```yaml
# tessera-plugin.yaml
requires:
  features: [speaker]
```

```yaml
# plugin.yaml
my_chime:
  speaker: ts_speaker
```

**Using a feature when it is there.** A plugin that also works without it puts what uses the feature in a part with
`features`. The app offers the part, and builds it, only on a screen that has the feature: a voice plugin listens on
every screen and answers out loud only where there is a speaker.

```yaml
# tessera-plugin.yaml
parts:
  - { id: aloud, file: parts/aloud.yaml, label: part_aloud, features: [speaker], default: true }
```

**Needing another plugin.** `requires.plugins: [other_id]` for a plugin that must be on the screen too. The app adds it
with yours, in the newest version that fits the screen, in the same build, and asks one yes for both. It must be in
the index and made for one of your boards. A plugin yours needs is removed only together with yours, and one that only
came along is offered to go as well.

## Common mistakes

| What happens | Why | Fix |
|---|---|---|
| The build says "No tessera-plugin.yaml above ..." | The component is not in `components/<id>/` next to the manifest. | Keep the folder layout of the template. |
| The build says the plugin wants another plugin API | `api:` in the manifest is newer than the screen's core. | Name the lowest API whose parts you use; the core offers plugin API 0.24 now (on Tessera's dev branch; the release has an older one until the next). Or update the screen's firmware. |
| The build says a newer API "changed a name it may use" | `api:` is from before a minor that renamed something (0.4: `Plugin::on_tick` became `on_interval`). | Move the code to the new name and raise `api` ([FIRMWARE_API.md](FIRMWARE_API.md), "Versions"). |
| `check.py` says a key "belongs to the core" or "opens the screen" | `plugin.yaml` sets something a plugin never sets, such as `wifi:` or `http_request:`. | Leave it to the core; data comes through a `fetch` ([LIMITS.md](LIMITS.md), "plugin.yaml"). |
| `check.py` says "provides speaker, so it makes a speaker: with id: ts_speaker" | The plugin promises a feature it does not make, or gives it another id. | Give the component the feature's id. |
| The plugin fits none of the screens | It names other `boards`, or nothing in the index that fits the screen brings a feature it needs. | Check `boards`; a feature needs a plugin in the index (or a board) that brings it for that board. |
| A setting says "Not on this screen yet" | The screen was not built since the entity was added, or the entity's name does not give the key. | Build again; check that "Show seconds" goes with `show_seconds`. |
| A setting is grey | The screen is offline, or the entity is unavailable in Home Assistant. | Check the screen; a button that was never pressed is fine. |
| The tile shows "Plugin missing" | The screen was not built with the plugin, or the tile id differs from `add_tile("...")`. | Build again; make the ids match. |
| The tile stays empty | `on_state` got `{"wait": ...}`: the fetch is not filled in or failed. | Show the reason (see the bus plugin); check the options. |
| The screensaver shows Tessera's own face, not yours | The screen was not built with the plugin, the saver's id differs from `add_saver("...")`, or its step differs from the manifest's. | Build again; make the ids and the step match (the bench says so in its findings). |
| The screen wakes when a key of the screensaver is tapped | The key was not made with `ui::takes_taps`, or not in a call of the saver. | Call `ui::takes_taps(key)` in `show()`, and say `taps: true` in the manifest. |
| A key of the screensaver shows the old value for a moment, or flips back | The key was painted from the tap, or the action went out without `wish()`. | Call `tessera::wish()` and paint the key from `on_state` alone. |
| Text cut with dots | The label is wider than its room. | Take a smaller font from `tessera::Font`, or give the label more width. |
| Squares instead of letters or icons | A character or icon the screen's fonts do not have. | Use plain text, and icons from Tessera's set only. |
