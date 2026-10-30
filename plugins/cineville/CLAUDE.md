# CLAUDE.md - Tessera Cineville Plugin

## Project Overview
A Tessera plugin (`cineville`) that shows what plays in the Cineville theaters of a city, and what comes soon, from the
Cineville integration for Home Assistant ([woldhek/ha-cineville](https://github.com/woldhek/ha-cineville), installed
through HACS). The tile belongs to one of that integration's sensors; the screen fetches nothing itself and needs no key.

- **Plugin ID:** `cineville`
- **Target Runtime:** ESPHome / C++ (`tessera::Tile`, `tessera::Card`), plugin API 0.10.
- **Sizes:** `1x1` up to `4x4`; a card (`films`) shows the whole list a page at a time.

## How the data flows
1. The integration asks `api.cineville.nl` every hour (`POST /events/search` for the showtimes of the chosen theaters
   and days, `POST /productions/search` for the films and for "Coming soon", `GET /venues` for the cities and theaters).
2. It makes two sensors per city, "Now showing" and "Coming soon". Their attributes hold up to 16 films as parallel
   lists of texts and numbers (`titles`, `highlights`, `directors`, `shows`, `shows_today`, `next_start` in seconds
   since 1970, `next_venue`, `premieres`), plus `cineville_list` (`now` or `soon`), which the manifest's
   `has_attributes` uses to offer only these sensors.
3. Tessera sends the tile those attributes (the manifest's `attributes`) at every change; `FilmList::read` turns them
   into a `tessera::List<Film>`, and `FilmList::pick` applies the tile's options (today only, picks only) and drops
   films whose next showtime has passed, once a minute.

## Directory Structure
```
plugins/cineville/
├── tessera-plugin.yaml        # Manifest: a tile of a sensor, two options, a card
├── plugin.yaml                # The ESPHome component
├── components/cineville/      # __init__.py, cineville.h, cineville.cpp
├── translations/              # en.json, nl.json
├── preview.yaml               # The two sensors, with the films of a real week in Amsterdam
├── README.md, CHANGELOG.md
```

## Essential Commands
- **Validate Plugin:** `python3 tools/check.py plugins/cineville`
- **Preview UI:** `python3 tools/preview.py plugins/cineville --strict`
- **Store Image:** `python3 tools/store_image.py plugins/cineville --from plugins/cineville/.preview`
- **Integration and its tests:** https://github.com/woldhek/ha-cineville
