# Cineville for Home Assistant

What plays in the Cineville theaters of a city, and what comes soon, as two sensors. Made for the Tessera plugin
`cineville` (`plugins/cineville` in this repository), and useful on its own in a dashboard or an automation.

It reads api.cineville.nl, the public service the Cineville website itself reads: no account and no key.

## Install

1. In HACS, open the menu at the top right, choose **Custom repositories**, and add
   `https://github.com/woldhek/tessera-plugins` with the type **Integration**.
2. Find **Cineville** in HACS, download it, and restart Home Assistant.
3. **Settings → Devices & services → Add integration → Cineville**: choose a city, then (if you like) only some of its
   theaters, how many days ahead, the order, how far "Coming soon" looks ahead, and the language of the descriptions.
   Set up another city, or another choice of theaters, by adding the integration again.

## The sensors

Each city makes a device with two sensors; the state of each is its number of films.

- **Now showing** (`sensor.cineville_<city>_now_showing`): the films with a showtime in the chosen theaters and days.
- **Coming soon** (`sensor.cineville_<city>_coming_soon`): the films that open in the Netherlands in the coming weeks,
  soonest first, with how many previews the chosen theaters have.

Their attributes:

| Attribute | What |
|---|---|
| `films` | Up to 60 films: `title`, `directors`, `year`, `duration`, `premiere`, `highlight` (`week`, `tip` or empty), `tip_by`, `shows`, `shows_today`, `next_start`, `next_venue`, `venues`, `preview`, `classic`, `description`, `poster`, `url` |
| `titles`, `highlights`, `directors`, `shows` | The first 16 films as short lists, for a Tessera screen |
| `shows_today`, `next_start` (seconds since 1970), `next_venue` | The same, for "Now showing" |
| `premieres` (noon of the day it opens, seconds since 1970) | The same, for "Coming soon" |
| `cineville_list` | `now` or `soon` |
| `city`, `venues` | The city and the theaters it counts |

"Recommended first" puts Cineville's own picks on top: the film of the week, then the tips of its editors, then what
shows most. The lists are kept out of the recorder's history. It asks Cineville once an hour.

## Tests

`python3 -m pytest tests/cineville` runs the client and the lists against answers of the API, without Home Assistant.
