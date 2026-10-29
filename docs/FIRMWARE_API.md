# The firmware API (plugin API 0.24)

A plugin's code runs on the screen as an ESPHome component. It talks to Tessera's core through one header:

```cpp
#include "esphome/components/smart_display/plugin_api.h"
```

The header lives in the Tessera repository at `components/smart_display/plugin_api.h`; it is the reference, and this
page explains it. Everything is in namespace `tessera`.

A manifest names the plugin API it was written for: `api: "0.8"`, or `"0.9"` up to `"0.24"` for the parts those
brought; the plugin API 0.24 is the one the core offers now (on Tessera's dev branch; the release has an older one until
the next). Which cores a plugin builds on is under "Versions" at the end of this page.

A plugin adds to the screen in a few kinds of parts: tiles on a page, cards over the page, tap actions for Tessera's own
tiles, items in the top bar, rows on the settings page, (0.14) screensavers, which the screen shows over the whole
glass in standby, and (0.18) a popup over the screen for a moment that wants the eye now. Each has its section below.

## The component: `__init__.py`

```python
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import smart_display
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display"]

my_idea_ns = cg.esphome_ns.namespace("my_idea")
MyIdea = my_idea_ns.class_("MyIdea", cg.Component)

CONFIG_SCHEMA = cv.Schema({cv.GenerateID(): cv.declare_id(MyIdea)}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
```

`smart_display.register_plugin(var, __file__)` finds `tessera-plugin.yaml` above the component folder and:

- checks that the manifest's `api` fits the core's plugin API ("Versions", below), and stops the build with one
  sentence when not;
- hands the C++ object the plugin's id and version (`plugin_id()`, `plugin_version()`);
- hands it each tile's `memory` from the manifest, which the screen counts in its layout budget;
- tells it which of its screensavers move themselves (`moves: true` in the manifest's `savers`, 0.14);
- hands it the texts of `translations/<language>.json`, part `screen`, in the language the screen is built in, with
  English for every key that language lacks (`text("key")`).

A plugin with configuration of its own (a pin, a name) adds it to `CONFIG_SCHEMA` as any ESPHome component does, and
`plugin.yaml` passes the values in (`pin: ${PIN}`, from the manifest's `inputs`).

## The plugin: `tessera::Plugin`

```cpp
class MyIdea : public Component, public tessera::Plugin {
 public:
  void setup() override { add_tile("next", [this]() { return new NextTile(this); }); }
  float get_setup_priority() const override { return setup_priority::DATA; }
};
```

| Member | What |
|---|---|
| `add_tile(id, make)` | A tile type, by its id in the manifest. Call it in `setup()`. `make` returns a new tile object; the core deletes it. |
| `add_card(id, make, wide)` | A card, by its id in the manifest's `cards`. `wide`: as wide as the glass instead of a hand's width. |
| `add_tap_action(id, run)` | A tap action for Home Assistant's own tiles, by its id in `tap_actions`. |
| `add_bar_item(id, read)` | An item for the top bar, by its id in `bar_items`. |
| `add_saver(id, step, make)` | A screensaver (0.14), by its id in the manifest's `savers`; `step` is the manifest's (`SaverStep::CLOCK`, `MEDIA`, `PHOTOS` or `OWN`). `make` returns a new saver object; the core deletes it. |
| `text(key)` | A text of part `screen` of the plugin's translations, `""` for an unknown key. |
| `plugin_id()`, `plugin_version()` | From the manifest. |
| `memory(tile)` | A tile type's `memory` from the manifest. |

The moments every plugin can hear (override what you need; each has an empty default):

| Moment | When |
|---|---|
| `on_ready()` | The screen's interface is up. |
| `on_interval(now_ms)` | Every 250 ms, the screen's own interval, with `millis()`. Keep it short: the screen draws and takes taps in the same loop. A tile's or a card's `on_tick` is another thing: once a second, with the clock. |
| `on_standby(dark)` | The screen dimmed or went dark (`true`), or woke up (`false`). |
| `before_update()` | A firmware update starts: let go of large buffers. |
| `settings(page)` | Once, when the interface is up: add rows to the screen's settings page (below). Return true when you added some. |
| `on_message(message)` | An answer of the app to `tessera::send()` (below). |
| `on_cards_closed()` | The cards closed: Back, standby, Back to page 1, another card. |
| `on_alert()` | An alert is about to show, a doorbell for example. |
| `on_press()` | A finger came down on something that takes a tap or a drag: a tile, a key, a slider, a row of the settings, the top bar (0.11). At once, before the screen knows whether it becomes a tap, a hold or a swipe: the moment for a click or a buzz that answers the finger. Not for the push that wakes a dimmed screen, nor between the tiles. Start a sound, never wait for one. |
| `on_step(step)` | A value under the finger stepped (0.11): a dragged slider, dial or seek bar passed a step (every 5 % of its range, or every position of one with 20 or fewer), or a key held down stepped once more. Once for every step, two at most for one move, as a detent clicks; not for the jump of a slider to where the finger lands. `step.direction` is +1 towards the larger end of the range, -1 towards the smaller, 0 when not known; `step.edge` says it reached either end, for a firmer click there. Play a click with `SoundPlayer::add()` (below), which spaces a run evenly. Every slider and dial of the screen says its steps, a plugin's own LVGL sliders on its cards too, without a line of code. |

The moments are the plugin's, whatever it shows. A screensaver of the plugin has moments of its own (`show`,
`on_state`, `on_tick`, `on_tap`, `on_hold`; "A screensaver", below). `on_standby(true)` comes within a quarter of a second of the
screen dimming, about when a saver shows, and `on_standby(false)` as it wakes: do not count on the order of the two. A
press on a saver's part that takes taps gives the plugins their `on_press`; the touch that wakes the screen does not.

## A tile: `tessera::Tile`

One tile object per card on the glass. A board with PSRAM keeps the cards of every page, so the object lives as long as
the layout; a board without (the CYD) makes a new object when its page comes back. Never keep anything a tile needs
across objects in a global: what a tile shows comes from its options and its data.

```cpp
class NextTile : public tessera::Tile {
 public:
  explicit NextTile(const tessera::Plugin *plugin) : plugin_(plugin) {}
  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;
};
```

### The life of a tile

1. **`create(context)`**: once, first. Make every LVGL object inside `context.parent`, read the options.
2. **`on_state(data)`**: right after `create()`, and again whenever the app sent new data, the next time the card is
   drawn. Keep what you need from `data` in members; the JSON is gone after the call.
3. **`on_tick(epoch)`**: right after every `on_state()`, and then once a second while the card is on the glass. Set the
   texts here, from the members and the clock.
4. **`on_theme()`**: the screen went light or dark. Set every colour again.
5. **`on_tap()`**: a short tap on the card, after the touch filter (a finger that moved or was too short never reaches
   it). The tile's `tap` option set to "none" in the editor keeps taps away. `tessera::tap_point()` (0.10) says where
   the finger was, in the tile's own pixels (`0,0` the top left of `context.parent`), so a list knows which row, a
   row of posters which film; `-1,-1` when the screen could not say.
6. **Destructor**: the card shows something else. The core deletes the LVGL objects; free only memory of your own.

A change of the tile's size or options makes a new object: `create()` never has to handle a resize.

### `TileContext`

| Field | What |
|---|---|
| `parent` | The card's drawing area. Everything goes in here. |
| `width`, `height` | Its size in pixels, the card's padding already off. |
| `columns`, `rows` | The grid cells the tile covers (1x1, 2x1, ...). |
| `name` | The name the tile got in the editor, `""` for none. Valid during `create()` only: copy it. |
| `entity` | The Home Assistant entity the tile belongs to (a manifest tile with `domains`), `""` for none. Copy it. An entity of a domain Tessera draws no tile for needs the firmware after 0.56.0 ("A tile of an entity"). |
| `tile` | The tile's index in the layout: pass it to `tessera::open_card` so the card follows the tile. |
| `options` | The tile's options, the manifest's defaults filled in. Read with ArduinoJson: `c.options["walk"] \| 0`. A list option (0.12, `multiple`) is an array: of texts, or for a choice of picks, `{"value", "label"}` with the words the person saw (`for (JsonVariantConst p : c.options["artists"].as<JsonArrayConst>()) p["label"]`); a choice found by `search` without `multiple` is one pick, or `""` while none is chosen. Copy what you keep. |

### What `on_state` gets

For a tile with `data: <fetch>` in the manifest, the mapped answer of that fetch ([FETCH.md](FETCH.md)):

```json
{ "items": [ { "line": "15", "to": "Station Sloterdijk", "at": 1791386619 } ] }
```

or, for a map without `items`, the fields of one object. Two more keys can be there:

| Key | Meaning |
|---|---|
| `"stale": true` | The service did not answer the last time; this is the last good answer. Say so if it matters. |
| `"wait": "<why>"` | There is no answer to show: `not_filled` (an option the URL needs is empty), `asking` (the first answer is on its way), `failed` (the service did not answer and there is no older answer), `too_large`, `refused`. |

A tile with `domains` also gets its entity, and is sent again whenever that entity changes in Home Assistant:

```json
{ "state": "off", "name": "Waste", "attributes": { "message": "Paper", "start_time": 1791410400 } }
```

`attributes` holds only the ones the manifest's `attributes` names and the `fields` it takes out of them, a text
cut at 48 bytes. A list comes whole when the tile's data fits its 2.6 KB: two days of 96 prices
do. Lists that do not fit together are shortened, all to the same length, from the end. An
attribute whose name ends in `_at`, `_time` or `date` and holds a moment comes as seconds since 1970, so the tile says it
in the screen's words (`tessera::date_text`, `days_from_today`). `"wait": "wrong_entity"` means the entity chosen in the
editor is of another domain than the manifest names.

A tile with options of kind `entity` (0.15, [MANIFEST.md](MANIFEST.md), "Tiles") also gets each entity the person
chose for one, under `entities` and the option's id, and is sent again whenever one of them changes:

```json
{ "entities": {
    "grid":  { "state": "120", "name": "Grid", "attributes": { "unit_of_measurement": "W" }, "updated": 1792389600 },
    "solar": { "state": "850", "name": "Solar", "attributes": { "unit_of_measurement": "W" }, "updated": 1792389540 } } }
```

Each is bounded as the tile's own entity is, with `updated`: when Home Assistant last heard from it, in seconds since
1970, so a tile of several sources can say how old its values are. They share what the tile's own entity and data leave
of the 2.6 KB, so one with long attributes comes as `{"wait": "too_large"}` alone. An option nobody filled in, or one
of another domain, is not there: say what to choose until it is.

A tile without `data` and without `domains` gets `{}`, and so does a tile with `domains` before a person has chosen
its entity in the editor: say what to do (a word such as "Choose a sensor") until `state` is there. `state` is always
text as Home Assistant has it: `"1043.6"` for a sensor's number (read it with `atof` or `strtof`), or `"unavailable"`
and `"unknown"` while the entity has no value.

## A tile of an entity

In the manifest, `domains: [calendar]` and `attributes: [message, start_time]`. The editor then
offers the entities of those domains in the tile's inspector, also of a domain Tessera itself draws no tile for. To act on
the entity, as a tap on one of Tessera's tiles would:

```cpp
tessera::action("light.toggle", entity_);                                   // returns false when it was not sent
tessera::action("climate.set_temperature", entity_, "temperature", "21");
```

The screen must be allowed to perform actions, and the manifest names each action under
`permissions.home_assistant_actions`.

### Actions

An action with several fields, of any JSON type (0.12), takes a JSON object:

```cpp
JsonDocument d;
d["media_id"] = album.title;          // a text arrives as the text: "1989" stays a text
d["media_type"] = "album";
d["artist"] = album.artist;
tessera::action("music_assistant.play_media", player_, d);   // false when it was not sent
```

A text arrives in Home Assistant as the text, a number as a number, `true` and `false` as themselves, a list or an
object whole: `hs_color` as `[20, 100]`, several speakers as an `entity_id` list. Nothing in it ever runs as a template,
also a text from the internet with `{{` in it. `entity` is the action's `entity_id`, whose tiles on the screen wait for
the answer as for a tap on them; pass `""` when the fields name their own target (an `entity_id` list, an `area_id`) or
the action has none. Up to 16 fields, each named with `a-z`, `0-9` and `_`, together at most 1 KB; `entity_id` in the
fields beside a non-empty `entity` is refused. When it is not sent, the screen's log says why. Without fields,
`tessera::action(service, entity)`, it sends only the entity (0.24 took away a second form with one field as a key and
a value: write that field in the object).

An entity of a domain Tessera draws no tile for (`plant`, `todo`, `calendar`, ...) works the same, with the firmware
after 0.56.0: up to 0.56.0 the tile gets that entity's state, name and attributes but an empty `entity`, and
`tessera::action` on it returns false. The fix is on Tessera's dev branch until its next release. To try it now, build
the preview with `TESSERA_REF=dev` ([PREVIEW.md](PREVIEW.md)) and a screen from the dev app (the repository URL with
`#dev` at the end). `tessera::send` to the app works with any entity on every firmware.

## A card

A card is a screen of its own over the page, opened by a tile's `on_tap`, a tap action or a settings row. Tessera draws
its frame: the page's ground, a round back key at the top left and the title in the middle; the card draws everything
under it. One card is open at a time, and it closes as Tessera's own cards close: Back, standby, Back to page 1, another
card.

```cpp
class Upcoming : public tessera::Card {
 public:
  void open(const tessera::CardContext &c) override;   // make the parts in c.parent (c.width x c.height)
  void on_state(JsonObjectConst data) override;         // the tile it was opened from changed
  void on_tick(uint32_t epoch) override;                // once a second while open
  void on_theme() override;
  void on_tap() override;                               // a tap on the card's room (0.12); tessera::tap_point() says where
  bool on_back() override { return false; }             // true: stay open (you went back a step of your own)
};

add_card("upcoming", [this]() { return new Upcoming(this); });
tessera::open_card(plugin_id(), "upcoming", entity_, tile_);   // from a tile's on_tap
```

**A tap on the card** (0.12): `on_tap()` hears a short tap on the card's room (`c.parent`), after the same touch filter
as a tile's: a finger that moved, or was too short, never reaches it, and a bounce on the same spot within 600 ms is one
tap (a tap on another spot is not). `tessera::tap_point()` says where the finger was, in the card's own pixels (`0,0` the
top left of `c.parent`), so a list knows its row: switch the entity of that row with `tessera::action`. The back key in
the top bar is `on_back`, never `on_tap`. The objects `tessera::ui` makes take no taps, so a tap on a label or a block
reaches the card; an LVGL object of your own takes the tap itself when it is clickable (`lv_obj_create` makes it so:
`lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE)`), and an LVGL slider on the card keeps its drags. The card may close
itself or open another from `on_tap`, as from any of its calls.

`CardContext` has `parent`, `width`, `height`, `entity` (what it was opened for, `""` for none) and `tile` (-1 for
none). `on_state` gets a plugin tile's data, or `{"state", "name"}` of one of Tessera's tiles. The title is the tile's
name unless `open_card` passes one; `tessera::close_card()` closes it as Back does.

## A tap action

A tap action lets a person set one of Tessera's own tiles to do something of the plugin on a tap: a sensor tile that
opens the plugin's card. The manifest's `tap_actions` names the domains it is for; the editor offers it in the tap
choices of those tiles on a screen that runs the plugin. A hold still opens the tile's own card, and a screen without
the plugin does nothing on the tap.

```cpp
add_tap_action("upcoming", [this](const tessera::TapContext &c) {
  tessera::open_card(plugin_id(), "upcoming", c.entity, c.tile);   // c.entity, c.name, c.tile
});
```

## A top bar item

The manifest's `bar_items` lists them; a person places one on a page's top bar in the editor, under "From plugins".
The screen asks the plugin what it shows every few seconds and draws the bar again when that changed:

```cpp
add_bar_item("soon", [this]() {
  tessera::BarItem item;           // shown, icon (a codepoint of Tessera's set), text (a few words), tone
  if (tomorrow_) { item.shown = true; item.icon = 0xF044C; item.text = "Tomorrow: Paper"; }
  return item;
});
```

An item that is not shown takes no room. A screen without the plugin draws nothing for it.

**Its colour (0.8).** `item.tone` colours the icon by what it means, and the core picks the colour in both looks:
`Tone::NORMAL` the bar's own grey, `Tone::ACCENT` the screen's blue (it listens, it is on), `Tone::BUSY` Home
Assistant's amber (it works on something), `Tone::ALERT` its red (a camera that streams, something that went wrong),
`Tone::GOOD` its green (0.9: a level that is fine, a full battery).
The words keep the bar's colour. `ui::set_tone(label, tone, role)` gives a tile's icon the same colours (`role` for
`NORMAL`).

**Icon only.** A person can choose to show an item's icon without its words (Show, Icon only), so give every item an
icon that says it alone.

## Settings rows

Rows on the screen's own settings page (hold the top bar): Settings, then Plugins, then the plugin's page, drawn exactly
as Tessera's rows. Keep the values where Home Assistant sees them too: an ESPHome entity of `plugin.yaml` (a template
switch, number or select with `restore_value`, a text, a button). Name the same entities in the manifest's `settings`
and the editor shows them in the plugin's details on the screen's Plugins tab ([MANIFEST.md](MANIFEST.md), "Settings a
person changes"). These rows on the glass talk to the entities directly, so they work while Home Assistant is away.

```cpp
bool settings(tessera::SettingsPage &page) override {
  page.icon = "\U000F044C";                       // the row's icon in the list of plugins; the title is the name
  page.toggle(text("in_bar"), [this] { return in_bar_->state; },
              [this](bool on) { on ? in_bar_->turn_on() : in_bar_->turn_off(); });
  page.number(text("days_ahead"), 0, 3, 1, "", [this] { return (int) days_->state; },
              [this](int v) { auto call = days_->make_call(); call.set_value(v); call.perform(); });
  page.card(text("open_card"), "\U000F00ED", "upcoming");   // a row that opens a card
  return true;
}
```

Also `choice(label, {words...}, read, write)`, `action(label, icon, run, confirm, text)` and `info(label, text)`. At
most twelve rows. An action with `confirm` asks once, as Restart does. With `text` it says how it is going on its
right, read again every second while the page shows: a test that runs, a countdown. `.active(running)` after it
lights the row in the accent while `running` says so, the way a row that asks is lit; a tap on it is yours to
stop what runs.

```cpp
page.action(text("tone"), "\U000F0387", [this] { tone_ ? stop() : play_tone(); }, nullptr,
            [this]() -> std::string { return tone_ ? text("playing") : ""; })
    .active([this] { return tone_; });
```

## Questions to the app

A plugin may ask Home Assistant something the screen cannot: the events of a calendar, a sensor's history. The app asks
it on the plugin's behalf, only a command the manifest names under `permissions.ha_commands`, and logs every one.

```cpp
JsonDocument request;
request["ask"] = "call_service:calendar.get_events";      // or a websocket command: "history/history_during_period"
request["data"]["entity_id"] = "calendar.waste";
request["data"]["duration"]["days"] = 28;
asked_ = tessera::send(this, request.as<JsonObjectConst>());   // 0 when it could not be sent

void on_message(JsonObjectConst m) override {             // {"re": asked_, "ok": true, "result": {...}}
  if ((m["re"] | 0u) != asked_) return;                    //  or {"re", "ok": false, "error": "not_allowed"}
}
```

`call_service:<domain>.<service>` sends the action with `return_response` and hands back its response; `entity_id`,
`device_id` and `area_id` go as the target, the rest as the action's data. A request is at most 512 bytes; an answer at
most about 3 KB: longer texts are cut to 48 bytes, and lists shortened from the end only when it does not fit. An answer the manifest maps under `answers` comes as its fields: a day of prices as one
list of numbers instead of a list of objects ([MANIFEST.md](MANIFEST.md), "Answers").

## Sounds

A plugin that needs the feature `speaker` can bring sounds of its own (0.8): WAV files of 16-bit samples, mono or
stereo, in its component's folder. `smart_display.sound()` builds one into the firmware in `__init__.py`, and checks it
(a file that is not such a WAV stops the build with the reason); `tessera::SoundPlayer` from `plugin_sound.h` plays
them on `ts_speaker`, one at a time, at any rate the file has:

```python
from pathlib import Path
from esphome.components import smart_display, speaker

TICK = Path(__file__).parent / "sounds" / "tap.wav"
# in CONFIG_SCHEMA: cv.Required("speaker"): cv.use_id(speaker.Speaker), and a check that runs smart_display.read_sound(TICK)
# in to_code:
cg.add(var.set_speaker(await cg.get_variable(config["speaker"])))
cg.add(var.set_tick(smart_display.sound(config[CONF_ID], "tick", TICK)))
```

```cpp
#include "esphome/components/smart_display/plugin_sound.h"

tessera::Sound tick_;           // set_tick() keeps it
tessera::SoundPlayer sounds_;   // set_speaker() hands it the speaker

void loop() override { sounds_.loop(); }       // hands the speaker what it takes now, never waits
void on_press() override { sounds_.play(tick_); }  // since API 0.11
```

`play()` starts a sound in place of the one that plays, `stop()` ends it, `playing(sound)` says which one plays.
`add(sound, span_ms)` (0.11) plays a sound after the ones handed to it before, followed by silence until `span_ms` after
its start, so a run of clicks comes out evenly spaced however late the screen's loop comes round while it draws; two at
most wait, more are left out so a run never trails behind the finger. Tap sound plays each step with `add(step.edge ? tick_ : step_, 30)`. A
sound follows the screen's volume. On a board whose microphone and speaker share one bus (`AUDIO_HALF_DUPLEX`), a sound
waits while something listens, and the player drops it when the speaker took nothing of it for a second.

## Pictures on a tile

A tile can show pictures (0.10): a film's poster beside its title, a person's photo, a logo. The plugin lays out places
and names an address for each; the screen never fetches anything itself. Tessera's app fetches each picture once, cuts
or fits it to its place, and sends all the places of a tile as one picture, the way a camera tile or an album cover
reaches the screen; the screen keeps it as long as the tile is there and draws each place its part.

```cpp
void create(const tessera::TileContext &c) override {
  if (!ui::pictures()) return;                 // a board without the memory for pictures (the CYD) shows none
  for (int i = 0; i < 4; ++i) {
    poster_[i] = ui::picture(c.parent);        // a soft block until the picture is there
    lv_obj_set_size(poster_[i], w, h);
    lv_obj_set_pos(poster_[i], i * (w + gap), 0);
  }
}
void on_state(JsonObjectConst data) override {
  JsonArrayConst posters = data["posters"];    // the addresses, next to the titles
  for (int i = 0; i < 4; ++i)
    if (poster_[i]) ui::set_picture(poster_[i], posters[i] | "", tessera::Fit::COVER);
}
```

| Function | What |
|---|---|
| `ui::pictures()` | Whether this screen shows pictures at all. A board without the memory for them (the CYD, the Waveshare 3.5-inch, the Hosyond 4-inch) shows none: lay the tile out without the places then, as you would without a picture. |
| `ui::picture(parent)` | A place for a picture, sized and placed by you as any object (`lv_obj_set_size`, `lv_obj_set_pos`), in `create()`. A soft block (`theme::TRACK`) until its picture is there. Its corners are a key's; a radius of your own (`lv_obj_set_style_radius`) is taken over by the picture. |
| `ui::picture(parent, Placeholder::NONE)` (0.17) | The same place without its block: nothing shows until the picture is there. For places that lie over each other (a cover flow), where a block between two pictures shows as a grey bar. |
| `ui::live(place, on)` (0.17) | A tile's place with the address `camera:<entity>` of the tile's own entity shows the camera live on a screen that streams (the ESP32-P4 boards), as Tessera's camera tile set to Live does: while the place is on the glass, the page stands still and nothing lies over it. Everywhere else, and until its first live picture, it shows its stills. Lay nothing over a live place, not even an empty label: what LVGL draws over it stops the stream. Only on a tile's places; a card's and a saver's show stills. |
| `on_picture(place, PictureState)` (0.17) | On a `Tile`, a `Card` and a `Saver`: what came of a place's picture. `SHOWN`, it is on the place; `NONE`, the app has no picture at that address now (the screen asks again later, each time after a longer while); `FAILED`, the download broke off. Draw a word of your own on `NONE` or `FAILED` where a place without a picture needs one. |
| `ui::set_picture(place, url, fit)` | The picture a place shows. `Fit::COVER` fills the place and cuts away what is over (a poster in a poster's frame); `Fit::CONTAIN` shows the whole picture within it, on the card's own colour (a logo). The same address again does nothing, so call it on every `on_state`; another address asks the app again; `""` takes the picture away. |
| `ui::set_picture(place, url, fit, turn, mirror)` | The same, turned in perspective and with a reflection, as the covers of a cover flow. `Turn::LEFT` faces the left (its left edge far: a cover right of the middle one), `Turn::RIGHT` the right; the app draws the whole picture into the place, its near edge the place's height and its far edge 70 % of it. `mirror` puts a reflection under the picture that fades out: the place's lower part holds it, `MIRROR_PERCENT` (30) of the picture's height, so a place for a picture `h` high with its reflection is `h * 130 / 100` high. An older app draws the picture flat. |
| `ui::slide_pictures(dx, ms)` | Slides every place of the tile `dx` pixels sideways over `ms` (300 by default) and keeps them there until the tile's next picture is here, then they are back in their places with it, in one frame. Call it beside the `set_picture` calls of a change, in a call of the tile (`on_tap`, `on_tick`), so a row of posters that turns shows which way at once. |

Where a picture may come from, checked by the app for every address:

- a host the manifest names under `permissions.network` (`image.tmdb.org`), over https or http, as a fetch: public
  hosts only, no redirects, at most 2 MB a picture;
- for a tile of an entity (`domains`), a picture of Home Assistant's own: the entity's `entity_picture` when it starts
  with `/api/image/serve/`, `/api/image_proxy/` or `/local/` (a person's photo, an image entity), fetched with the
  app's own token;
- (0.17) what the screen already shows of the tile's own entity, by name instead of an address: `cover:<entity>`, a
  media player's cover, `camera:<entity>`, a camera's picture (the newest frame of its stream while one runs), and
  `entity:<entity>`, the entity's own picture. The app answers them exactly as it answers Tessera's own tiles, and they
  need no `permissions.network` and no `attributes: [entity_picture]`. Only the entity the tile, card or step shows.

The manifest's `pictures` of the tile says how many places it has at most ([MANIFEST.md](MANIFEST.md), "Tiles"); the
app paints no more. A place that lies outside the card, or has no size, gets no picture.

**Places may overlap.** They are the layers of the card: a place made later lies over one made earlier, and the app
paints them in that order, so a rounded corner or the transparent part of a turned picture shows what lies under it.
Make the places from the back to the front (the outermost covers of a cover flow first, the middle one last). Another
address keeps the picture a place shows until the next one is there, so a row of posters turns without a flash of
blocks. Pictures count in nothing the
layout memory counts: the screen keeps them in its picture store, within the same budget as the covers and cameras,
and a tile of eight posters is one picture there. A frame whose picture did not come stays the card's colour, and the
screen asks again later, each time after a longer while.

### Pictures on a card

A card over the page shows pictures the same way (0.13): the same calls, made in `open()`, in the card's room.

```cpp
void open(const tessera::CardContext &c) override {
  if (ui::pictures()) {
    cover_ = ui::picture(c.parent);                       // a place, sized and placed as any object
    lv_obj_set_size(cover_, side, side);
    lv_obj_set_pos(cover_, 0, 0);
    ui::set_picture(cover_, cover_url(album), tessera::Fit::COVER);
  }
  // ... the title, the artist, a key that plays it, beside or under the cover
}
void show(const Album &next) { ui::set_picture(cover_, cover_url(next), tessera::Fit::COVER); }   // a card that steps on
```

- Name how many places the card has in the manifest, `cards: [{ id: release, name: card_name, pictures: 1 }]`
  ([MANIFEST.md](MANIFEST.md), "Cards"). The app paints only for a card that has them, from a host of
  `permissions.network`, or Home Assistant's own pictures when the card was opened for an entity of the screen's layout
  (`open_card` with that entity).
- The card asks while it is open, with the priority of Tessera's own media card, and its picture leaves the store when
  the card closes (Back, standby, another card). It shares the screen's picture budget: what the page under the card
  shows stays, a page out of sight makes room first.
- The same address as on the tile, such as the cover the tile shows at that moment, is a picture the app already has:
  it only paints it at the card's size, so the card shows it about as soon as it opens. Use the very same URL.
- `set_picture` with another address keeps the picture the place shows until the next one is there, as on a tile;
  `ui::slide_pictures`, `Turn` and `mirror` work on a card as on a tile.
- `ui::pictures()` is false on a board without the memory for pictures (the CYD): lay the card out without the place.

## A screensaver (0.14)

The screensaver is what a screen shows over the whole glass while Auto standby dims it, instead of the dimmed tiles. A
person puts its steps in an order in the editor (the Screensaver card of a screen's Settings): the music that plays,
the photos of a folder, a camera, the clock. Tessera's app picks the first step that has something to show and sends
it to the screen. Tessera decides **when** a step shows; a plugin decides **how** it looks. A plugin adds to that list
in two ways, never beside it:

- **A face of a step** (`SaverStep::CLOCK`, `MEDIA` or `PHOTOS`; `step: clock`, `media` or `photos` in the manifest):
  another look for a step the screen already has, drawn from the same data Tessera's own face draws from (the step's
  contract, below). The person picks it under Face in that step's drawer. A screen that lacks the plugin (it left, its
  build failed, older firmware) shows the step's own face: never a black glass.
- **A step of its own** (`SaverStep::OWN`, `step: own`): a place in the order, drawn from the plugin's fetch (`data`)
  and its entity (`domains`, `attributes`, `fields`), as a plugin tile is. The manifest's `when` says when it has
  something to show: `always`, `data` (its fetch has an answer) or `{state: [...]}` (its entity is in one of those
  states). While it has nothing, the app goes on to the next step.

```cpp
class Dial : public tessera::Saver {
 public:
  void show(const tessera::SaverContext &c) override {        // make every part, once, in c.parent
    time_ = ui::label(c.parent, Font::DIGITS, theme::INK);
    lv_obj_set_width(time_, c.width);
    lv_obj_set_style_text_align(time_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(time_, 0, (c.height - ui::line_height(Font::DIGITS)) / 2);
  }
  void on_state(JsonObjectConst data) override { /* the step's contract: data["temperature"] */ }
  void on_tick(uint32_t epoch) override {                      // paint what changed
    if (epoch) ui::set_text(time_, tessera::clock_text(epoch));
  }
 private:
  lv_obj_t *time_{};
};

add_saver("dial", tessera::SaverStep::CLOCK, []() { return new Dial(); });   // in setup()
```

| Part | What |
|---|---|
| `Plugin::add_saver(id, step, make)` (0.14) | A screensaver, by its id in the manifest's `savers`; `step` must be the manifest's. Call it in `setup()`. |
| `Saver` (0.14) | One object while the saver is on the glass, deleted when it leaves. It never exists off the glass. |
| `show(context)` (0.14) | Once, first: make every part in `context.parent`. |
| `on_state(data)` (0.14) | Right after `show()`, and again whenever what the step knows changed (a new track, the next photo, a new temperature, a wish of a finger): its contract, below. |
| `on_tick(epoch)` (0.14) | Right after the first `on_state()`, then once a second while it is on the glass, on every board, with the screen's clock (0 until it is set). |
| `on_tap()` (0.14) | A short tap on a part made with `ui::takes_taps`; `tessera::tap_point()` says where, in the glass's pixels. |
| `on_hold()` (0.14) | A finger held on such a part for a second and a half (as volume down held mutes on Tessera's own player). `tap_point()` says where. |
| `SaverContext` (0.14) | `parent` (the whole glass: everything goes in here), `width` and `height` (the glass in pixels), `step`, `level` (the backlight in percent, the standby or the night level, never 0), `night` (the night levels hold now: a face may go darker still) and `options` (the person's options of the saver; valid during `show()` only, copy what you keep). |
| `ui::takes_taps(part)` (0.14) | A part a finger may tap without waking the screen: its taps go to `on_tap`, a hold to `on_hold`. Call it in `show()`, for a saver of the manifest's `taps: true` (the screen refuses it for any other, and logs why). |
| `tessera::drift(epoch)` (0.14) | Where Tessera's slow circle against burn-in stands this minute, an offset of at most a millimetre. For a saver of `moves: true` that moves only some of its parts. |
| manifest `backdrop: true` (0.14) | A face of the music or the photos gets the step's own picture under it, over the whole glass: the cover, or the photo, moving slowly when the person chose Motion: Slow (a pixel every 1.5 s; the 10-inch screens keep it still). The saver's glass is clear and it draws only what goes over the picture; it needs no `step:` place for it. The way to make a slideshow face (the `photo_clock` plugin). |
| `tessera::wish(entity, field, value)` (0.14) | A value a finger changed (`Wish::PLAYING` "1" or "0", `Wish::MUTED` "1" or "0", `Wish::ON_OFF` "on" or "off"): it shows at once and Home Assistant squares it. False when it was not sent. |
| `"step:"` (0.17) | The address of the step's own picture for `ui::set_picture`: the cover of the music on the glass, the photo of the photos step. A plugin of 0.14 to 0.16 names it `step:`, which every app still reads. |

**When a saver shows, and when it goes.** Only in standby, with a backlight level above 0, without Sleep pressed in Home
Assistant, with the cards closed and no alert on the glass. It goes, and its object is deleted, when a finger wakes the
screen, an alert comes (a doorbell), an update starts (`before_update` first), the level goes to 0, a new layout comes,
or the app sends another step. A new word for the same saver (a new track, the next photo) does not make a new object:
it comes to `on_state`, so the saver paints it in place. A saver has no `on_theme`: it never changes look.

**The dark look.** A saver is drawn in the dark look whatever the screen's own look, on a black glass, so the glass gives
as little light as it can: every role gives its dark value in every call of a saver (`theme::INK` is white, `CARD` a
dark grey). Draw with roles as anywhere else; a light block over the whole glass defeats the point.

**It moves, or the screen moves it.** A saver stands on the glass for hours, so nothing of it may stand still on the
same pixels. The screen moves the whole saver up to a millimetre every minute, along a slow circle, the way Tessera's
own clock faces move. A saver that moves itself (a clock that turns, a picture that changes every minute) says so with
`moves: true` in the manifest, and the screen leaves it where it draws. Say it only when the saver really moves: the
bench photographs every saver twice, two minutes apart, and warns when the two are the same.

**Taps.** Nothing of a saver takes a tap unless it is made with `ui::takes_taps`, and only a saver of the manifest's
`taps: true` makes such parts. A tap on such a part goes through the touch guard every key of the screen passes, comes
to `on_tap()`, and the screen stays in standby; a hold of a second and a half comes to `on_hold()` instead. A tap
anywhere else wakes the screen, the saver goes, and that touch does nothing else (it never switches the tile under it).
Keep such parts few and at least `ui::mm(10)` across: a person taps them in the dark. Take `tessera::tap_point()` to
know which part was tapped when there are several (compare it with `lv_obj_get_coords` of each). Never an LVGL event of
your own.

**Values a finger changes: `wish()`.** A key that changes what an entity shows (play and pause, mute, on and off) calls
`tessera::wish(entity, field, value)`, never `tessera::action` with a value written into the saver first. The value then
shows at once, on the saver (its `on_state` gets it: the music's `state` and `muted`) and on every tile and card of that
entity; the action goes out at once, taps while it is on its way fold into one, and Home Assistant's report, answer or
silence squares it: a refusal or no answer puts the old value back. It is the rule every control of the screen follows
(Tessera's docs/OPTIMISTIC.md), so a key of a plugin never shows something the screen cannot know and never holds a
value of its own. Paint the key from `on_state`, never from the tap. A step that has nothing to show (volume up and
down) goes as an action: `tessera::action("media_player.volume_up", entity)`. Name every action the saver calls under
`permissions.home_assistant_actions` (the wish's too: `media_player.media_play_pause` for `PLAYING`,
`media_player.volume_mute` for `MUTED`, the entity's own `turn_on` and `turn_off` for `ON_OFF`, such as
`light.turn_on`). Where the wish shows at once: for the player of the music step on the glass, on the saver itself;
for any other entity (a step of its own's light), on the tiles of the layout that show it, while the saver itself shows
the change when Home Assistant reports it, in `on_state`.

**Pictures.** A saver lays out places with `ui::picture` and `ui::set_picture` as a tile does ("Pictures on a tile"),
in `show()`, and the manifest's `pictures` of the saver says how many (at most 8). The address `step:` is the
step's own picture, which the app cuts to the place: the cover of the player on the glass for a face of the music, the
photo on the glass for a face of the photos. The contract hands it as `picture` (`""` while there is none), and the
next cover or photo comes under the same address by itself. Any other address is on a host of `permissions.network`,
or Home Assistant's own picture of a step of its own's entity. The music and the photos steps show only on a screen
with pictures (`ui::pictures()`), so a face of them never runs on the CYD; a face of the clock and a step of its own
do, and lay themselves out without places there.

**The contract of each step**: what `on_state` gets. A field may be added later; within major 0 none is renamed or
taken away.

| Step | `on_state` gets |
|---|---|
| media | `entity`, `name` (the player's), `title`, `artist`, `album`, `state` (`playing`, `paused`, ...), `features` (Home Assistant's `supported_features`: PAUSE 1, VOLUME_SET 4, VOLUME_MUTE 8, VOLUME_STEP 1024, PLAY 16384), `muted`, `volume` (0 to 1), `duration`, `position`, `position_at` (seconds since 1970), `picture` (`step:` while there is a cover), `mark` (the cover's mark), `colour` (`#rrggbb`, the cover's own) |
| clock | `temperature` (`21°`, `""` for none), `items` (`[{icon, text, since?}]`: the clock's row of entities, each with a codepoint of Tessera's set, its words, or the moment it counts from) |
| photos | `picture` (`step:`), `mark`, `index`, `count`, `album` |
| own | the fetch's answer and the entity's `state`, `name`, `attributes` and fields, as a plugin tile's `on_state` |

**Options.** `SaverContext::options` holds what the person set in the editor, as the app keeps it: an option the
person never touched may be missing, so read every option with its default (`c.options["seconds"] | true`).

**Memory.** The manifest's `memory` of a saver is what it costs while it is on the glass, as a tile's: about 100 bytes
a label, 80 a block, plus its members. A saver is made in PSRAM by itself (`operator new`), as a tile is.

**What a saver cannot do.**

- **No swipes and no scrolling.** A finger either taps a part that takes taps or wakes the screen.
- **No animation of its own** beyond what `on_tick` paints once a second: no `lv_anim`, no timer, no task. Paint, do not
  rebuild, in `on_tick`: the glass draws only the boxes that changed, and a whole saver drawn again every second costs
  the screen what it needs for Wi-Fi and touch.
- **No entities of its own choosing.** A face reads its step's contract; a step of its own reads its entity and its
  fetch, through the app, as a tile does. It cannot read any other entity of Home Assistant.
- **No video and no live camera.** A picture comes once per address (a cover per track, a photo per photo, a camera's
  still for `camera:`); the camera step has no plugin faces yet.
- **No black glass.** A saver shows something: a step of its own with nothing to show says so with `when`, and the app
  goes on to the next step. A dark saver over a bright glass is not possible either: the level is the person's.
- **No card, no other page.** A saver opens nothing; the screen wakes for that.

The examples: [`plugins/flip_clock/`](../plugins/flip_clock) (a face of the clock that paints only what changed each
minute, no pictures, every board), [`plugins/vinyl_deck/`](../plugins/vinyl_deck) (a face of the music: the cover by
`step:`, keys with `ui::takes_taps`, play and mute with `wish()`, the volume with `action()`) and
[`plugins/photo_clock/`](../plugins/photo_clock) (a face of the photos: the photo over the whole glass, the time in a
corner).

## A popup (0.18)

A popup is the card an alert of Home Assistant shows, in the middle of the glass over the dimmed screen, with what your
plugin draws in it: for a moment that wants the eye now, such as a voice assistant that listens, thinks and answers, a
timer that went off or a question to answer. One shows at a time, of any plugin, over the page, a card, the settings
and the screensaver, under an alert. Its top right corner holds a close key, the round key of every card's top bar;
keep the first row of your parts `context.corner` from the right.

```cpp
class Asked : public tessera::Popup {
 public:
  void open(const tessera::PopupContext &c) override {
    title_ = tessera::ui::label(c.parent, tessera::Font::HEADLINE, theme::INK);
    lv_obj_set_width(title_, c.width);
    tessera::ui::set_text(title_, "Tea is ready");
  }
  void on_tap() override { tessera::close_popup(plugin); }
  static tessera::Plugin *plugin;
 private:
  lv_obj_t *title_{};
};
// From one of the plugin's own calls:
tessera::show_popup(this, []() { return new Asked(); });
```

| Part | What it does |
|---|---|
| `show_popup(plugin, make, width)` | Shows the popup `make` returns, in place of the one shown now; the core deletes it. `PopupWidth::ALERT` (the default) is as wide as an alert's card on this glass, `PopupWidth::WIDE` the glass less its edge (a list, a row of keys). It lights a dark screen as `wake()` does: a popup is something to see. |
| `close_popup(plugin)`, `popup_shown(plugin)` | Closes your popup (no `on_dismiss`), and says whether one of yours shows. |
| `open(context)` | Make every part in `context.parent`. `context.width` is the room's width, `context.height` the most it may grow to on this glass, `context.key` the close key's size (give a first part of yours, a disc or an icon in the room's top left corner, that size: it then stands on one line with the key, as far from the card's top and side), `context.corner` how far the first row keeps from the right for the key. |
| `on_interval(now_ms)`, `on_tick(epoch)`, `on_theme()` | Every 250 ms (words that come one by one), once a second, and when the look changes. |
| `on_tap()` | A tap on the card, through the touch filter; `tap_point()` in the room's pixels. |
| `on_dismiss()` | It goes by itself: its close key, a tap beside the card, the screen going dark, an update, an alert, another plugin's popup. The last call before it is deleted. |
| `ui::animate(part, Motion, period_ms, delay_ms)`, `ui::still(part)` | A part that pulses (`Motion::PULSE`, its opacity), spins (`SPIN`, around its middle) or ripples (`RIPPLE`, growing by half while it fades), again and again, until `still` or until it is deleted. A delay makes parts move one after another. |

Rules of a popup:

- **Lay it out from the top; the card follows.** The card is as high as what is in its room, up to `context.height`,
  and stays in the middle: hide a part that has nothing to say (a hidden part takes no room) instead of leaving a gap.
  What lies below `context.height` is cut off, so show the first or the last lines of a long text yourself. It never
  scrolls.
- **Sizes as everywhere**: `ui::px`, `ui::mm`, the screen's fonts and line heights, theme roles. The same popup is
  right on the CYD, a 4-inch square and a 10-inch tablet, lying down or standing up.
- **Motion only in a popup**, whose moment is short. A tile, a card and a screensaver stand still: the screen draws a
  moving part again in every frame. Keep a part that moves small, and still it when its moment is over.
- **Shown by what a person did, or with a setting.** A popup lights the screen. A plugin that shows one without a
  person asking (a timer, a doorbell of its own) has a setting for it, as a plugin that wakes the screen does.
- **A popup may close itself** in any of its own calls; it leaves the glass at once and is deleted once that call
  returned. Code of the plugin that keeps a pointer to its popup forgets it in `on_dismiss` and before `close_popup`.

The voice assistant (`plugins/voice_assist`, `voice_popup.h`) is the example: a disc in the colour of where a question
is, rings that ripple while it listens, an arc that turns and dots that pulse while Assist thinks, and under them what
was heard and the answer as it is spoken, its last lines when it is long.

## Memory

A screen has two kinds of memory. Inside the chip there are a few tens of kilobytes, shared with Wi-Fi and the network
stack, and when they run out the screen loses Wi-Fi or restarts. A board with PSRAM (every board that shows pictures)
has megabytes beside it. A plain `std::string`, `std::vector` or `new` always takes the memory inside the chip: the
boards are built so that only what asks for PSRAM gets it.

So from API 0.10 on:

- **Your tile and card objects are in PSRAM by themselves.** `tessera::Tile` and `tessera::Card` take PSRAM in their
  `operator new`, so what they hold directly (numbers, `char` arrays, pointers, a `Film films[16]`) is there already.
- **Keep texts and lists a tile holds as `tessera::Text` and `tessera::List<T>`.** They work as a `std::string` and a
  `std::vector` (`data()`, `size()`, `c_str()`, `+=`, `push_back`, `==` against another `Text` or a quoted text) and take
  PSRAM first, the memory inside the chip on a board without it. `roomy::plain(text)` makes a `std::string` of one for
  `ui::set_text`, `roomy::text(s)` the other way, `roomy::same(a, b)` compares the two kinds.
- **A short text you build to show is fine as a `std::string`** in a function: it is gone when the function returns.
- The core keeps your tile's options and data, and parses them for `on_state`, in PSRAM as well.

```cpp
struct Film { char title[52]; char poster[48]; float rating; };   // fixed fields: one block for the whole list
tessera::List<Film> films_;     // PSRAM
tessera::Text wait_;            // PSRAM
```

`tools/check.py` says so (a hint, not a failure) when a component of a plugin of API 0.10 or later keeps a member as a
`std::string` or `std::vector`. A plugin of an older API keeps building as it is.

## Drawing: `tessera::ui`

The screen's look is the same on every tile, so a plugin draws with the core's sizes, colours and fonts.

| Function | What |
|---|---|
| `ui::mm(n)` | `n` millimetres of glass in this board's pixels. For anything a finger touches (at least 7 mm). |
| `ui::px(n)` | A size of the reference look (a 4-inch Guition at 170 dpi) in this board's pixels. For paddings and gaps. |
| `ui::large()` | The standard look (4 inches and up), or the compact one (the 2.8-inch CYD). |
| `ui::font(Font)` | The screen's font. |
| `ui::line_height(Font)`, `ui::text_width(text, Font)` | To lay out before setting. |
| `ui::covers(text, Font)` (0.9) | Whether the font has every character of the text; a character it lacks draws as nothing. |
| `ui::label(parent, Font, Role)` | A one-line label, cut with dots when too long. From 0.9 it is one line of its font high, and `ui::set_font` keeps it so; before, give it that height yourself (`lv_obj_set_size(label, width, ui::line_height(font))`), or a long text wraps onto a second line under the tile. |
| `ui::set_text(label, text)`, `ui::set_font(label, Font)`, `ui::set_color(label, Role)`, `ui::set_tone(label, Tone, Role)` | Change it only when it differs: call them on every tick for free. |
| `ui::set_fill(block, Tone, Role)` (0.9) | A block's fill (a bar, a dot) in a tone's colour, or the role for `NORMAL`; only when it differs. |
| `ui::tone_color(Tone, Role)` (0.9) | A tone's colour in this look, for a part the two above do not reach. |
| `ui::text(parent, Font, Role)` | A label of several lines (0.10): wraps at its width, ends in dots at its height (two lines to start with; set your own). For a description, a title that may take two lines. |
| `ui::block(parent, Role)` | A rounded block with the radius of a key (a badge, a bar). |
| `ui::picture(parent)`, `ui::set_picture(place, url, fit)`, `ui::pictures()`, `ui::slide_pictures(dx, ms)` | Pictures on a tile (0.10), on a card (0.13) and on a screensaver (0.14), above. |
| `ui::picture(parent, Placeholder::NONE)`, `on_picture(place, PictureState)`, `ui::live(place)` (0.17) | A place without its block, what came of a place's picture, and a tile's place that streams its camera, above. |
| `tessera::capturing(plugin, Capture::CAMERA, on)` (0.17) | The plugin uses the screen's own camera on the screen itself ("The screen's own camera", below); `Capture::SENSING` (0.20) when it only senses and nobody watches; `Capture::SOUND` (0.23) while someone hears what the screen's microphone hears (its video with sound, a call): no dot, but the screen keeps its live tiles at a quarter while it lasts, so they do not starve the conversation's uplink. |
| `tessera::squeeze(Work)` (0.21) | How much of its work a busy screen asks the plugin to give up now: 0 none, 1 half, 2 three quarters, for `Work::OWN_CAMERA` (the screen's own camera's video), `Work::SENSING` (a light level, motion) and `Work::BACKGROUND` (work done ahead). Any task may ask; the screen sets it while a finger is on the glass and from what it measures, and it goes back by itself. |
| `tessera::offer_own_frames()`, `own_tile_wants()`, `own_frame(OwnFrame)` (0.22) | The screen's own camera on a live tile of its own: a plugin with the camera offers its frames once in its setup (the hello then says `own_camera`, and the app answers such a tile `own:` instead of a link); while a tile of it is on the glass, `own_tile_wants()` is true and the plugin hands each frame, RGB565 or the P4's YUV420 as the camera gave it, to `own_frame()` on its camera's task. The core copies what it shows into the tile's card at its pace and returns at once; a frame it cannot take goes by. |
| `tessera::camera_turn()` (0.19) | How far the screen's own camera turns its picture to stand upright, 0 or 180 ("The screen's own camera", below). |
| `ui::takes_taps(part)` (0.14) | A part of a screensaver a finger may tap without waking the screen ("A screensaver", above). |
| `ui::animate(part, Motion, period_ms, delay_ms)`, `ui::still(part)` (0.18) | A part of a popup that pulses, spins or ripples while something goes on, and stops ("A popup", above). Only in a popup. |
| `ui::color(Role)` | A colour by its role. |
| `ui::icon(codepoint)` | An icon of Tessera's set as text, for a label in `Font::ICON` or `ICON_SMALL`. |

Fonts (`tessera::Font`), largest first. Take the largest that fits.

| Font | Used for |
|---|---|
| `DIGITS` (0.9) | The largest: the digits of a climate card's setpoint (64 px on the standard look, 40 on the compact). Only `0`-`9`, `-`, `.`, `,`, `°` and `%`: take it where `ui::covers(value, Font::DIGITS)`, else step down to `VALUE`. It is a font every screen has already, so it costs no flash. |
| `VALUE` | The big number of a card. |
| `HEADLINE` | Large words. |
| `TITLE` | A card's name (bold). |
| `BODY_LARGE` | Words on a key. |
| `BODY` | A card's second line. |
| `ICON`, `ICON_SMALL` | Icons of Tessera's set. |

Colours are theme roles (`theme::Role`, from `theme.h`): each has a light and a dark value, so a tile follows the
screen's look. The ones a tile needs most:

| Role | For |
|---|---|
| `theme::INK` | Text, the most important. |
| `theme::INK_SOFT`, `theme::MUTED`, `theme::SUBTLE` | Less important text, in that order. |
| `theme::ACCENT` | The one thing that stands out (a badge, a bar). |
| `theme::ON_ACCENT` | Text on the accent. |
| `theme::CARD`, `theme::RAISED`, `theme::LINE`, `theme::TRACK` | Surfaces and lines inside a card. |
| `theme::MARK_GREEN`, `MARK_AMBER`, `MARK_BLUE`, `MARK_PURPLE` | The four colours of the Tessera mark, the same in light and dark: for a dot or a block that names a kind (a bin, a line). |

A part can take a colour by what it means, in both looks: a tone (`tessera::Tone`). `GOOD` (0.9) is Home Assistant's
green, `BUSY` its amber, `ALERT` its red, `ACCENT` the look's accent, each made readable on the card. A label takes it
with `ui::set_tone(label, tone, normal)`, a block (a bar, a dot) with `ui::set_fill(block, tone, normal)` (0.9); `normal`
is the role for `NORMAL`. A level that is good, moderate or bad is `GOOD`, `BUSY` and `ALERT`. Never a colour as a
number. Before 0.9 a block has no colour by meaning: `TRACK` and `ACCENT`, or the mark's colours where a kind fits.

## The rest of the core

| Function | What |
|---|---|
| `tessera::epoch()` | The screen's clock, seconds since 1970; 0 until Home Assistant set it. |
| `tessera::local_time(epoch)` | That moment in the screen's time zone: `year, month, day, hour, minute, second, weekday`. |
| `tessera::clock_text(epoch)` | A time of day as the screen writes it (24 hours or AM/PM, as its settings say). |
| `tessera::format(text, n)` | `{n}` filled in; with `"1 day \| {n} days"` the form that fits `n` in the screen's language. |
| `tessera::fill(text, name, value)` | `{name}` filled in. |
| `tessera::refresh()` | Draw the plugin's cards again in the next pass, after a change outside `on_state` and `on_tick`. |
| `tessera::days_from_today(epoch)` | 0 today, 1 tomorrow, -1 yesterday, on the screen's calendar. `INT32_MIN` while the clock is not set. |
| `tessera::date_text(epoch)` | A day as the top bar writes it: "Fri 9 Oct", "vr 9 okt". |
| `tessera::days_text(days)` | "Tomorrow", "In 3 days" in the screen's language; `""` for today (bring your own word) and the past. |
| `tessera::action(service, entity, key, value)` | A Home Assistant action on an entity (see "A tile of an entity"). |
| `tessera::action(service, entity, fields)` | The same with a JSON object of fields of any type (0.12, "Actions"). |
| `tessera::wish(entity, field, value)` | A value a finger changed (0.14): it shows at once and Home Assistant squares it ("A screensaver", "Values a finger changes"). |
| `tessera::open_card(plugin_id, card, entity, tile, title)`, `close_card()` | Open or close a card. |
| `tessera::show_popup(plugin, make, width)`, `close_popup(plugin)`, `popup_shown(plugin)` | Show, close or ask after a popup (0.18, "A popup"). |
| `tessera::tap_point()` | Where the finger of the tile's `on_tap` was (0.10), or of a card's (0.12), in its own pixels; `-1,-1` when unknown. Valid during `on_tap`. |
| `tessera::send(plugin, request)` | Ask the app (see "Questions to the app"). |

## Rules

`tools/check.py` refuses code that breaks the first six.

1. **No colour as a number** (`0xFF8800`, `lv_color_hex`): use a role.
2. **No font of your own**: use `tessera::Font`. Every font costs flash, and the 4 MB boards are close to full.
3. **No LVGL click handlers** (`LV_EVENT_CLICKED`, `LV_EVENT_PRESSED`): use `on_tap`, which has the touch filter in
   front of it; on a screensaver, `ui::takes_taps` and the saver's `on_tap`.
4. **No timers, tasks or waits** (`lv_timer_create`, `xTaskCreate`, `delay`): use `on_tick` and `on_state`. A plugin
   that streams (sound to a speaker) may use its ESPHome component's own `loop()`, as long as nothing in it waits. A
   driver of hardware beside the plugin's own component, one that uses no plugin API and no LVGL, may run a task of
   its own the way ESPHome's drivers do (a camera captures on the other core); rules 1 to 4 are for the code that
   draws and takes taps, rules 5 and 6 for all of it.
5. **No network from the screen** (`http_request`, an HTTP client): data comes through the app's `fetch`.
6. **No walk of the heap** (`heap_caps_get_largest_free_block`, `heap_caps_get_info`): it shifts the picture of an
   RGB panel while it runs.
7. **Make objects in `create()` only** (a card's `open()`, a screensaver's `show()`). `on_state` and `on_tick` change
   what exists; they create and delete nothing.
8. **Keep `memory` honest.** Each LVGL label is about 100 bytes, a block about 80, plus your members.
9. **Icons from Tessera's set only.** Another codepoint shows as nothing.
10. **Text from `translations/`.** No words in the code.

## The examples

- [`template/components/my_plugin/my_plugin.cpp`](../template/components/my_plugin/my_plugin.cpp): a number of days, a
  line of text, the largest font that fits, light and dark. About 90 lines.
- [`plugins/ov_departures/components/ov_departures/ov_departures.cpp`](../plugins/ov_departures/components/ov_departures/ov_departures.cpp):
  data from a fetch, a single-cell layout and a list layout, badges, a countdown every second, waiting states.
- [`plugins/waste_collection/components/waste_collection/waste_collection.cpp`](../plugins/waste_collection/components/waste_collection/waste_collection.cpp):
  most of the API in one plugin: a tile of a calendar entity, a card that asks Home Assistant for the coming events, a
  tap action, a top bar item, two settings rows backed by ESPHome entities of `plugin.yaml`, and an input of kind entity.
- [`plugins/voice_assist/components/voice_assist/voice_assist.cpp`](../plugins/voice_assist/components/voice_assist/voice_assist.cpp):
  ESPHome's own components (`micro_wake_word`, `voice_assistant`) from `plugin.yaml` on the features it needs, a tile
  and a top bar item whose icon changes tone, settings rows and sounds of its own.
- [`plugins/screen_camera/`](../plugins/screen_camera): a board's hardware through a feature only a board brings
  (`camera_sensor`), a driver beside the plugin's own component, and a top bar item that follows the camera.
- [`plugins/tap_sound/components/tap_sound/tap_sound.cpp`](../plugins/tap_sound/components/tap_sound/tap_sound.cpp): the
  smallest plugin with sounds: a tick in `on_press` as a finger lands, a softer one in `on_step` for every step of a
  dragged slider.
- [`plugins/picture_frame/`](../plugins/picture_frame): pictures on a tile, an entity's own or one at an address.
- [`plugins/flip_clock/components/flip_clock/flip_clock.cpp`](../plugins/flip_clock/components/flip_clock/flip_clock.cpp):
  the smallest screensaver, a face of the clock that paints only what changed.
- [`plugins/vinyl_deck/components/vinyl_deck/vinyl_deck.cpp`](../plugins/vinyl_deck/components/vinyl_deck/vinyl_deck.cpp):
  a face of the music with the step's cover, keys that take taps in standby, `wish()` and `action()`.
- [`plugins/photo_clock/components/photo_clock/photo_clock.cpp`](../plugins/photo_clock/components/photo_clock/photo_clock.cpp):
  a face of the photos, the photo over the whole glass with the time in a corner.

## Not in the API yet

A plugin's own messages beyond Home Assistant commands, faces of the screensaver's camera step (a plugin has faces
of the clock, the music and the photos, and steps of its own, 0.14), pictures in a popup, and a popup that scrolls.
Home Assistant gives an ESPHome screen the words of an Assist answer in one piece at its end, never while an AI writes
them, so a popup cannot show an answer before it is done. Pictures are on tiles (0.10), cards (0.13) and
screensavers (0.14). A plugin can bring a camera (0.8): the feature `camera`, an ESPHome `esp_video_camera` with the id
`ts_camera` that Home Assistant shows as any ESPHome camera, made from a board's camera sensor (the feature
`camera_sensor`), as [`plugins/screen_camera/`](../plugins/screen_camera) does. Each will raise the minor.

## The screen's own camera

A screen shows when its own camera is in use: a small green dot in its top right corner, over everything, the
screensaver too. The core draws it, and no plugin and no setting hides it. It shows by itself while Home Assistant
takes pictures or a stream of `ts_camera` (the core listens to the camera when a plugin's manifest provides
`camera`). A plugin that uses the camera on the screen itself, for what never leaves the screen (a light level, motion),
says so (0.17):

```cpp
tessera::capturing(this, tessera::Capture::CAMERA, true);    // from the first picture it reads
tessera::capturing(this, tessera::Capture::CAMERA, false);   // once it reads none any more
```

Say `true` before the first picture is read and `false` only once none is read any more; a plugin that reads a
picture every few seconds keeps it `true` all that time. The dot is the person's way to know: a plugin never reads the
camera without it.

What the plugin does with the picture decides which (0.20): `Capture::CAMERA` while someone sees what the camera sees
(a live view on the glass, a video), `Capture::SENSING` while the camera only senses and no picture leaves the screen
or reaches a person (a light level, motion). The dot is solid while anything watches, Home Assistant's pictures and
streams too, and a ring while the camera only senses. Each is on and off on its own, so a plugin that does both says
both:

```cpp
tessera::capturing(this, tessera::Capture::SENSING, true);   // light level and motion from now on
tessera::capturing(this, tessera::Capture::CAMERA, watched); // and whether someone watches as well
```

A plugin that drives the camera turns its picture upright (0.19): `tessera::camera_turn()` says how far, 0 or 180,
from how the board mounts its sensor against the glass (the board file's `CAMERA_ORIENTATION`) and the screen turned
half way round from lying down; a quarter turn (a screen stood up) changes nothing, and the picture stays lying down.
Read it now and then from the plugin's own calls, since the app turns a screen without a build, and turn the sensor's
readout by it, so the pictures, a video and whatever reads the frames all stand the same way (Screen camera's
`FrameHub::turn`).

## Versions

A manifest names the plugin API it was written for: `api: "0.8"` up to `"0.24"`; the plugin API 0.24 is the one the core offers now (on Tessera's dev branch; the release has an older one until the next). When a
screen is built, `smart_display.register_plugin()` compares it with the core's and stops the build with one sentence
when it does not fit. A plugin fits a core when all three hold:

1. the major is the same;
2. its minor is at most the core's;
3. no minor the core lists as a break lies after the plugin's own minor, up to the core's.

**From 1.0 on** a minor only adds, so the third rule never stops a plugin: only a break raises the major.

**While the major is 0** a minor may still change a name or a signature as the API settles, or take a form away: a
plugin that still uses it fails its build with the compiler's error at that call (0.24). A minor after which an older
plugin would still build but work wrongly is listed in `PLUGIN_API_BREAKS`, in `screen_manager/app/plugin_manifest.py`
and in `components/smart_display/__init__.py` (a test keeps the two equal), and such a plugin is refused in one
sentence. Today the list holds one: 0.4, where `Plugin::on_tick` changed its meaning and the old one became
`on_interval`. So with a core of 0.24:

| The manifest says | Builds? | Why |
|---|---|---|
| `api: "0.24"` | Yes | The core's own. |
| `api: "0.5"` up to `"0.23"` | Yes | Same major, an older minor, and no break after it (unless it uses a form 0.24 took away). |
| `api: "0.3"` | No | 0.4 changed a name it may use; the build says so in one sentence and asks to update the plugin. |
| `api: "0.25"` | No | Newer than the core; the build asks to update the plugin or the screen's firmware. |
| `api: "1.0"` | No | Another major. |

`tools/check.py` applies the same rules against the core it checks with (`api_fits` of `plugin_manifest.py`), so a
manifest that names an API this core cannot build fails there first. Tessera's own plugins move with every break in the
same release.

Name the lowest minor whose parts you use: the plugin then builds on every core from that minor on. What each minor
brought:

| Minor | What it brought |
|---|---|
| 0.1 | Tiles (`Tile`, `add_tile`, `TileContext`), the drawing helpers, the clock, and the plugin's moments: `on_ready`, `on_standby`, `before_update` and one every 250 ms. |
| 0.2 | Tiles of an entity (`TileContext::entity`, `tessera::action`), cards (`Card`, `add_card`, `open_card`, `close_card`), tap actions, top bar items, settings rows, questions to the app (`tessera::send`, `on_message`), date words such as `date_text` and `days_text`, `on_cards_closed` and `on_alert`. |
| 0.3 | `on_touch`, and a settings action that says how it is going (its `text`) with `.active()`. |
| 0.4 | A break: `Plugin::on_tick` (every 250 ms, with `millis()`) became `on_interval`, so that `on_tick` everywhere means once a second with the clock. |
| 0.5 | The app's side only: lists of an entity bounded by bytes instead of 16 items, a tile's `fields`, `answers`, the field kind `numbers`, `has_attributes`. |
| 0.6 | The app's side only: settings of kind text and button, a button's `status`, settings found by their name in the entity registry, and each plugin's settings in its details on the screen's Plugins tab. |
| 0.7 | The manifest's side only: `topics`, features (`provides`, `requires.features`), parts that need a feature, and plugins that come along with the one that needs them. |
| 0.8 | The features `camera` (`ts_camera`) and `camera_sensor`, the first feature only a board brings; a top bar item's `tone` and `ui::set_tone`; sounds of a plugin's own (`plugin_sound.h`, `smart_display.sound()`). |
| 0.9 | `Font::DIGITS`, the large digits of a climate card's setpoint, and `ui::covers()`; `Tone::GOOD`, `ui::set_fill()` and `ui::tone_color()`, the tones for a block; `ui::label` one line high. |
| 0.10 | Pictures on a tile: `ui::picture`, `ui::set_picture`, `ui::pictures`, `Fit`, `Turn` and a reflection, places that overlap, `ui::slide_pictures`, and the manifest's `pictures` of a tile; `ui::text`, a label of several lines; `tessera::tap_point()`, where a tile was tapped; memory in PSRAM: `tessera::Text`, `tessera::List`, and tiles and cards made there. |
| 0.11 | `on_press`, a finger landing on something that takes a tap or a drag, at once (for a click that answers the finger; `on_touch` comes as the finger lets go), `on_step(Step)`, a value under the finger stepping (a dragged slider, dial or seek bar, a held key), and `SoundPlayer::add()`, sounds evenly spaced one after another. |
| 0.12 | A card's `on_tap()`, with `tessera::tap_point()` in the card's own pixels. Values of JSON: an option that keeps several values (`multiple`: texts, or picks `{value, label}`), a choice found by searching (`search`), a fetch once per value of a list (`each`, `{ each: label }`) or with the list joined (`{name\|,}`), and `tessera::action(service, entity, fields)` with fields of any JSON type. |
| 0.13 | Pictures on a card over the page: `ui::picture`, `ui::set_picture` and `ui::slide_pictures` in a card's calls, and the manifest's `pictures` of a card. |
| 0.14 | Screensavers: `Saver`, `SaverContext`, `SaverStep`, `Plugin::add_saver`, a face of the clock, the music or the photos or a step of its own, and the manifest's `savers`; `ui::takes_taps`, a part of a saver a finger taps in standby; the address `saver:picture`, the step's own picture (`step:` from 0.17); `tessera::wish()` and `Wish`, a value a finger changed. `tools/check.py` refuses these parts in a plugin of an older `api`. |
| 0.15 | Nothing new in the C++: a tile's options of kind `entity` (the manifest) bring the chosen entities to `on_state` under `entities` ("What `on_state` gets"). `tools/check.py` refuses such an option in a plugin of an older `api`. |
| 0.16 | `tessera::wake(plugin)`: the screen wakes as Home Assistant's Wake button does, for a plugin that senses someone come (an option of its own, off by default; at most once a second or two, from the screen's loop). |
| 0.17 | Pictures in one system: `ui::picture(parent, Placeholder::NONE)`, a place without its block; `on_picture(place, PictureState)` on a tile, a card and a saver; the addresses `cover:`, `camera:` and `entity:` of the tile's own entity, and `step:` for the step's own picture (`saver:picture` still read until 0.24); `ui::live`, a tile's place that streams its camera; and `tessera::capturing()` with the camera's dot. |
| 0.18 | Popups: `Popup`, `PopupContext`, `PopupWidth`, `tessera::show_popup`, `close_popup` and `popup_shown`; `ui::animate` with `Motion` and `ui::still`, a part that pulses, spins or ripples. `tools/check.py` refuses these parts in a plugin of an older `api`. |
| 0.19 | `tessera::camera_turn()`, how far the screen's own camera turns its picture to stand upright, 0 or 180; `hw::caps()`, what the screen's hardware can do in one record (a live camera stream, a speaker, a microphone, echo cancellation, a camera of its own, how many live tiles, its memory). |
| 0.20 | `Capture::SENSING`: the screen's own camera used for what never leaves the screen and nobody watches (a light level, motion); the dot is a ring then, and solid while anything watches. |
| 0.21 | `tessera::squeeze(Work)`: the screen's governor asks a plugin to give up half or three quarters of its video, its sensing or its background work while a finger is on the glass or the screen shows signs of trouble. |
| 0.22 | `tessera::offer_own_frames`, `own_tile_wants`, `own_frame`: the screen's own camera drawn on a live tile of its own from the camera's frames, with no encoder, app or network between. |
| 0.23 | `Capture::SOUND` for `tessera::capturing`: someone hears what the screen's microphone hears (its video with sound, a call); the screen keeps its live tiles at a quarter while it lasts, so a conversation goes before the tiles on the sending side too. |
| 0.24 | One form for each thing; taken away, each beside a newer form: `on_touch` (use `on_press`), `tessera::action(service, entity, key, value)` (use `tessera::action(service, entity, fields)`, or no fields), the address `saver:picture` (use `step:`) and a tile's `memory: { bytes: n }` (write the number). `ui::picture` and `ui::set_picture` are one signature each, with defaults. |

A minor of the app's or the manifest's side only still counts: a plugin that uses what it brought needs an app that
knows it ([MANIFEST.md](MANIFEST.md), "Which minor brought a field").
