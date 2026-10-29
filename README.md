# Tessera plugins

Plugins add something to a [Tessera](https://github.com/MaxGramser/homeassistant_espscreen) touch screen that not
everyone needs: a tile with the next bus, hardware on one board, a feature of your own. A plugin is built into the
firmware of the screens you choose, and the Tessera app in Home Assistant gives its tiles their data.

This repository holds:

| Folder | What |
|---|---|
| [`plugins/`](plugins) | The plugins Tessera ships, one folder each. Every folder is a complete plugin. |
| [`template/`](template) | A small plugin that works, to start your own from. |
| [`docs/`](docs) | How plugins work and how to make one, step by step. |
| [`tools/`](tools) | `check.py` (check a plugin), `preview.py` (the bench: see it on every kind of screen), `new_plugin.py` (start one), `store_image.py` (its picture on the Plugins page), `build_index.py` (write the index), `build_likes.py` (copy the likes, ratings and homes). |
| `index.json` | The list the Tessera app reads. Written by `tools/build_index.py`, never by hand. |
| `blocked.yaml` | Plugin versions the app refuses to build. |
| `featured.yaml` | The plugins Tessera recommends to start with. |
| `transfers.yaml` | Plugin ids that moved to another repository. |
| `likes.json` | How many people like each plugin, copied from Tessera's website every hour by `tools/build_likes.py`. |
| `stats.json` | Per plugin its likes, rating, ratings and the homes that use it, copied with the likes. |

## The plugins

| Plugin | What it does | Boards |
|---|---|---|
| [Public transport (NL)](plugins/ov_departures) | The next bus, tram, metro or ferry from your stop, live from OVapi, counted down on the screen. | All |
| [Waste collection](plugins/waste_collection) | When the next bin goes out, from a calendar in Home Assistant, on a tile, a card and in the top bar. | All |
| [Voice assistant](plugins/voice_assist) | Home Assistant's Assist on the screen: a wake word on the screen itself, the answer on its speaker, sounds while it listens and thinks. | With a microphone and a speaker |
| [Screen camera](plugins/screen_camera) | The screen's own camera as a camera in Home Assistant, with a camera in the top bar while it is watched. | With a camera |
| [Tap sound](plugins/tap_sound) | A short, soft tick on the screen's speaker for every tap. | With a speaker |
| [Cinema](plugins/cinema) | The films in your country's cinemas, or coming soon, as a cover flow of posters with their rating. | All |
| [New music](plugins/new_music) | The newest albums and singles of the artists you pick, as a cover flow you can play from. | All |
| [Picture frame](plugins/picture_frame) | A picture on a tile: a person's photo, an image entity, what plays, or any picture at an address. | All |
| [Guition relays](plugins/guition_relays) | The relays of the Guition 4-inch wallbox base as switches in Home Assistant and on the screen. | Guition 4-inch |
| [Bluetooth proxy](plugins/bt_proxy) | Home Assistant reaches Bluetooth devices near the screen; shares the radio with Wi-Fi. | With Bluetooth (the ESP32-P4 boards) |
| [Flip clock](plugins/flip_clock) | A screensaver clock of two flip cards, with the date and the outside temperature under them. | All |
| [Vinyl deck](plugins/vinyl_deck) | The music that plays as a record in its sleeve on the screensaver, with keys for play and the volume. | With pictures |
| [Photo clock](plugins/photo_clock) | Your photos over the whole screensaver, with the time and the date small in a corner. | With pictures |

## Using a plugin

1. Open Tessera in Home Assistant and go to **Plugins**, or to the **Plugins** tab of a screen. The tabs Tiles,
   Screensavers, Functions and Hardware say what a plugin adds, and In use what your screens run. Topics narrow the list, and it is
   sorted by most liked unless you choose newest or name.
2. Open a plugin, read what it does and what it may do, tick the screens it goes on, and apply. What it needs comes
   along: a plugin it names, or the plugin that brings a speaker the screen lacks. Each screen builds its firmware once
   with all of it.
3. In **Layout**, place the plugin's tile from the library's **Plugins** group and set its options in the inspector.
   A screensaver of a plugin is chosen in the screen's **Settings**, on the **Screensaver** card: under **Face** in a
   step's drawer, or as a step of its own with **Add from plugins**.

A plugin that is not in the list can be added with a link to its GitHub repository (**Add with a link**). Every screen
stays on the commit it was built with: a newer version shows as an update on the screen's Plugins tab, and one tap
builds it.

Plugins are new and only in the dev version of the app for now (the app added with `#dev` at the end of the repository
URL, or a local copy). The plugin API is 0.24 (an older one in the release until the next), and a plugin names the version it was written for. While the API is 0.x
a minor may still change a name: the core lists the minors that did (only 0.4 so far), a plugin written before one no
longer builds and says so, and Tessera's own plugins move with it ([docs/FIRMWARE_API.md](docs/FIRMWARE_API.md),
"Versions").

## Making a plugin

```sh
git clone https://github.com/MaxGramser/tessera-plugins
cd tessera-plugins
pip install pyyaml                           # the check needs PyYAML
python3 tools/new_plugin.py my_idea          # plugins/my_idea/, copied from template/
python3 tools/check.py plugins/my_idea       # the same check the app does
python3 tools/preview.py --setup             # once: what the bench needs (docs/PREVIEW.md)
python3 tools/preview.py plugins/my_idea --open   # every tile, card and setting on every kind of screen
```

Then read [docs/MAKING_A_PLUGIN.md](docs/MAKING_A_PLUGIN.md). It goes through every file, how to try the plugin on
your own screen before anyone else sees it, and how it gets into the list. Every version gets a few lines in the
plugin's `CHANGELOG.md`: the app shows them when it offers the update.

A plugin can also live in a repository of your own. To try it, push it and add the repository's link in the app: a
repository without a release is a test of its default branch. To share it, list it once with a pull request that adds a
three-line file to `community/`; after that every release you publish reaches the app by itself within the hour,
without a pull request here ([docs/PUBLISHING.md](docs/PUBLISHING.md)).

Working with an AI assistant? Point it at [AGENTS.md](AGENTS.md) first: it has the rules and the order to read the
docs in.

## What a plugin can and cannot do

- **On the screen** a plugin is an ESPHome component written in C++. It draws its own tiles, cards and screensavers
  with the screen's fonts, colours and sizes, and hears about the screen's moments (start, a tick, standby, an update).
- **In the app** a plugin is only a description. The app reads its manifest and carries out what it asks with its own
  code: fetch JSON from a web service it names, keep a secret key, show its options and its README. The app never runs
  a plugin's code.
- Through the plugin API a plugin draws only its own tiles. It cannot change how other tiles look, block taps, or
  reach your home network through the app.
- On the screen nothing fences a plugin in: its C++ is part of the firmware and can do what the firmware can. A plugin
  of the community is checked automatically, not reviewed, so add one from a maker you trust
  ([docs/PUBLISHING.md](docs/PUBLISHING.md), "What listing means").

[docs/LIMITS.md](docs/LIMITS.md) has the full list, with the way around where there is one.

## Licence

The tools, the template and Tessera's own plugins are MIT ([LICENSE](LICENSE)), unless a plugin's folder has a
LICENSE of its own (Screen camera is GPL-3.0-only). A plugin is built into firmware that is AGPL-3.0, so every
plugin's licence must go with AGPL-3.0 (`license` in [docs/MANIFEST.md](docs/MANIFEST.md), "Who and what").
