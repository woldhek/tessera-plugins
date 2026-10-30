"""Constants of the Cineville integration."""

from __future__ import annotations

from datetime import timedelta

DOMAIN = "cineville"

# The API behind cineville.nl (CultureKit). Public: the website itself reads it from the browser, without a key.
API_URL = "https://api.cineville.nl"
WEBSITE = "https://www.cineville.nl"
ATTRIBUTION = "Data provided by Cineville"

CONF_CITY = "city"
CONF_VENUES = "venues"
CONF_DAYS = "days"
CONF_SORT = "sort"
CONF_LANGUAGE = "language"
CONF_SOON_WEEKS = "soon_weeks"

DEFAULT_DAYS = 7
DEFAULT_SORT = "recommended"
DEFAULT_SOON_WEEKS = 4

SORTS = ["recommended", "shows", "next", "title"]
LANGUAGES = ["auto", "nl", "en"]

UPDATE_INTERVAL = timedelta(hours=1)

# How many films the short lists in the attributes hold: what a Tessera tile (or a card) shows, within the 2.6 KB a
# tile's data may take on the screen. The detailed `films` attribute holds more.
COMPACT_FILMS = 16
DETAILED_FILMS = 60

# What the screen gets as a film's highlight, a code it puts in its own words.
HIGHLIGHT_WEEK = "week"
HIGHLIGHT_TIP = "tip"
