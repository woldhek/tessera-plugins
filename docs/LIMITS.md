# What a plugin can and cannot do

## Works

| Idea | How |
|---|---|
| The next bus from your stop, with your line chosen in the editor | A tile, a `fetch`, an option with `options_from` (the `ov_departures` plugin). |
| Today's waste collection from your municipality's API | A tile and a `fetch`. |
| Hourly energy prices of your own supplier | A `fetch` (up to 48 items) and a tile that draws them. |
| A day of electricity prices per quarter of an hour from Home Assistant (Nord Pool, ENTSO-e, Tibber, ...) | A tile with `domains`, `has_attributes` and `fields` with `as: numbers` for a sensor's attributes, or `answers` for an action's answer. |
| A countdown to a date | A tile on the screen's clock alone (the template). |
| A sensor or a relay on the board's free pins | An ESPHome component with `inputs` of kind `gpio`; anything ESPHome can do. |
| Waking the screen when someone stands in front of it | A plugin with a radar sensor and `on_interval`. |
| Settings a person changes in the editor, a test with its result ("Heard: Okay Nabu") | ESPHome entities in `plugin.yaml` named in `settings`; a button with a `status`. |
| Talking to an AI model or a voice assistant | Home Assistant's Assist and its conversation agents (OpenAI, Anthropic); the plugin picks the assistant, the key stays in Home Assistant. |
| Something over the whole screen for a moment: what the voice assistant heard and answers, a timer that went off, a question | A popup (plugin API 0.18): the alert's card with your parts in it, as high as they are, a close key in its corner, `ui::animate` for a part that moves while something goes on (the `voice_assist` plugin). It lights a dark screen. |
| Several stops, artists, calendars or tickers on one tile, picked in the editor or found by typing | An option with `multiple` (and `search`), and a fetch with `each` that asks once per value and shows one list, or `{name\|,}` for a service that takes them all in one ask (plugin API 0.12, [FETCH.md](FETCH.md)). |
| A card with a list whose rows switch their entity (the lights that are on, the open locks) | A card's `on_tap` and `tessera::tap_point()` to find the row, then `tessera::action` (0.12, [FIRMWARE_API.md](FIRMWARE_API.md), "A card"). |
| Playing an album, a playlist or a radio station on a tap | `tessera::action` with several fields of any type: `media_id`, `media_type`, `artist`, a list of speakers (0.12, [FIRMWARE_API.md](FIRMWARE_API.md), "Actions"). |
| A clock of your own design in standby, on every screen (the CYD too) | A face of the screensaver's clock: a saver of `step: clock` (plugin API 0.14, the `flip_clock` plugin). It gets the temperature and the row of entities the person chose for the clock. |
| Another look for the music or the photos in standby, with the cover or the photo | A face of `step: media` or `photos` with `pictures` and the address `step:` (the `vinyl_deck` and `photo_clock` plugins), on screens with pictures. |
| Play, pause, mute and the volume from the screensaver without waking the screen | Parts made with `ui::takes_taps` (`taps: true`); play, pause and mute with `tessera::wish`, the volume with `tessera::action` (the `vinyl_deck` plugin). |
| A screensaver step of your own: the rain radar while it rains, a quote of the day, the bins on the evening before | A saver of `step: own` with a `fetch` (`data`) or an entity (`domains`), and `when` to say when it has something to show; the app goes on to the next step while it has nothing. |
| Tonight's films with their posters, a person's photo, a logo on a tile | Places for pictures on the tile (`ui::picture`, the manifest's `pictures`, plugin API 0.10): the app fetches each picture from a host the manifest names, or Home Assistant's own, and the screen shows them ([FIRMWARE_API.md](FIRMWARE_API.md), "Pictures on a tile"). |

## Works with a way around

| Idea | Way around |
|---|---|
| An API that needs OAuth | A Home Assistant integration that offers the data as an entity; then a normal tile. |
| An API that gives XML or GTFS-realtime | The Home Assistant integration for it. |
| Messages from outside (a webhook, a bot) | A webhook in Home Assistant that sets an entity. |
| A server, an account or a live connection of the plugin's own (a realtime voice session, a cloud service with OAuth) | A Home Assistant integration of your own; the plugin uses its entities and actions. |

## Cannot, on purpose

| Idea | Why |
|---|---|
| Change how all light tiles look | A plugin draws only its own tiles. A better card for everyone belongs in Tessera itself. |
| Block taps (a child lock) or scroll freely | Touch and navigation stay with the core. |
| Run code in the Tessera app or the editor | The app holds the key to all of Home Assistant; it runs only its own code. |
| Reach your home network through the app | A fetch goes only to public hosts the manifest names. |
| Fetch from the screen itself | The app does it once for every screen, without holding up drawing. A picture too: the screen names its address, the app fetches it. |
| Set the screen's Wi-Fi, API, updates or log, or open a port on it | They belong to the core and the screen's own YAML (below, "plugin.yaml"). |
| Change without a build | A plugin is part of the firmware: every change builds the screen. |
| Decide when the screensaver shows, or keep it on the glass | Standby, its level, the night hours and the order of the steps are the person's and the core's. A saver says what it shows, never when. |
| A screensaver in the screen's light look, or a light glass in standby | A saver is drawn in the dark look on a black glass, so the glass gives as little light as it can at night. |
| Swipes or scrolling on a screensaver | A finger taps a part made with `ui::takes_taps`, or it wakes the screen. |
| A screensaver that reads any entity it likes | A face reads its step's contract, a step of its own its own entity and fetch, through the app. |

## Cannot, yet

Not in the plugin API so far: video on a plugin's tile (live cameras reach a screen through Tessera's own camera tiles), a message to
the app other than a Home Assistant command its manifest names (`permissions.ha_commands`), an option that holds
entities of Home Assistant (a tile has one entity through `domains`), an option of several numbers or of objects of
your own, a pause between the asks of a fetch with `each` (a service that allows one ask a second), and a Python part of
a plugin in the app (the app runs only its own code, see above). For a screensaver: a face of the camera step, an
animation of its own faster than `on_tick`'s second, video, and more
than four savers per plugin. For a popup: pictures, scrolling, and an Assist answer before Home Assistant has it whole.
A settings page on the screen, a card, top bar items, tap actions, tiles of an entity,
pictures on a tile, a card and a screensaver, screensavers (faces of the clock, the music and the photos, and steps of
their own), popups, questions to the app, sounds on the screen's speaker and a camera of the screen's own for Home Assistant
(the features `camera` and `camera_sensor`) are all there ([FIRMWARE_API.md](FIRMWARE_API.md)).

## plugin.yaml

`plugin.yaml` and the file of every part are merged into the screen's own configuration. `tools/check.py` refuses a
file that sets one of these keys at its top level:

| Keys | Why |
|---|---|
| `esphome`, `esp32`, `esp8266`, `rp2040`, `psram` | The chip, the board and how it starts belong to the core. |
| `wifi`, `ethernet`, `network`, `improv_serial`, `esp32_improv` | How the screen joins the network is in the screen's own YAML. |
| `api`, `ota`, `logger` | Home Assistant's connection, updates and the log belong to the screen's own YAML and the core. |
| `external_components`, `packages`, `substitutions` | The app writes these for every plugin; a plugin's inputs reach `plugin.yaml` as substitutions already. |
| `web_server`, `captive_portal`, `mqtt`, `wireguard`, `socket`, `udp`, `packet_transport` | They open the screen to the network. |
| `http_request`, `online_image` | They fetch from the screen; data comes through the app's `fetch`. One exception: a file that holds only ESPHome's update of the Wi-Fi co-processor of an ESP32-P4 (`update:` with `platform: esp32_hosted`, `type: http` and a `source` on `https://esphome.github.io/esp-hosted-firmware/manifest/`) and `http_request:` without settings. It installs only when someone presses Install in Home Assistant. |

Two more rules for the ids it makes: one that starts with `ts_` only for a feature the plugin provides, and for each
feature it provides, that feature's component with exactly its id ([MANIFEST.md](MANIFEST.md), "Features").

## Physical limits

- **Flash**: a 4 MB board (the CYD) has little room left; the app shows how much a plugin takes there (`flash_kb`).
- **Memory**: every tile counts in the screen's layout memory (`memory`), and a screensaver its `memory` while it shows.
- **Pictures**: the CYD, the Waveshare 3.5-inch and the Hosyond 4-inch show none, so they show no music or photos in
  standby, and no face of those steps; the Waveshare 4.3, 5 and 7-inch never go into standby, so they show no
  screensaver at all.
- **Time**: the screen draws, takes taps and runs plugins in one loop; `on_tick` and `on_state` must be quick.
