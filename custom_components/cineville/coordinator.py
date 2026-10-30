"""Fetches what plays in the chosen theaters, every few hours."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime, timedelta
import logging

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed
from homeassistant.util import dt as dt_util

from .api import CinevilleApi, CinevilleError
from .const import (
    CONF_CITY,
    CONF_DAYS,
    CONF_LANGUAGE,
    CONF_SOON_WEEKS,
    CONF_SORT,
    CONF_VENUES,
    DEFAULT_DAYS,
    DEFAULT_SOON_WEEKS,
    DEFAULT_SORT,
    DOMAIN,
    UPDATE_INTERVAL,
)
from .films import Film, coming_soon, now_showing, sort_films, venues_in

_LOGGER = logging.getLogger(__name__)


@dataclass
class CinevilleData:
    city: str
    venues: list[str]
    now: list[Film] = field(default_factory=list)
    soon: list[Film] = field(default_factory=list)
    updated: datetime | None = None


def locale_for(hass: HomeAssistant, language: str) -> str:
    if language == "auto":
        language = "nl" if (hass.config.language or "").lower().startswith("nl") else "en"
    return "nl-NL" if language == "nl" else "en-GB"


def setting(entry: ConfigEntry, key: str, default):
    """An option, else what the setup chose, else the default."""
    if key in entry.options:
        return entry.options[key]
    return entry.data.get(key, default)


class CinevilleCoordinator(DataUpdateCoordinator[CinevilleData]):
    config_entry: ConfigEntry

    def __init__(self, hass: HomeAssistant, entry: ConfigEntry) -> None:
        super().__init__(hass, _LOGGER, config_entry=entry, name=f"{DOMAIN} {entry.data[CONF_CITY]}",
                         update_interval=UPDATE_INTERVAL)
        self.api = CinevilleApi(async_get_clientsession(hass), locale_for(hass, setting(entry, CONF_LANGUAGE, "auto")))

    async def _async_update_data(self) -> CinevilleData:
        entry = self.config_entry
        city = entry.data[CONF_CITY]
        chosen = set(setting(entry, CONF_VENUES, []) or [])
        days = int(setting(entry, CONF_DAYS, DEFAULT_DAYS))
        weeks = int(setting(entry, CONF_SOON_WEEKS, DEFAULT_SOON_WEEKS))
        how = setting(entry, CONF_SORT, DEFAULT_SORT)
        zone = dt_util.get_default_time_zone()
        now = dt_util.now()
        # From the start of today, so today's list counts the showtimes that already began.
        start = dt_util.start_of_local_day(now)
        end = start + timedelta(days=days)
        try:
            venues = venues_in(await self.api.venues(), city)
            if chosen:
                venues = [v for v in venues if v.get("id") in chosen]
            names = {v["id"]: v.get("name") or "" for v in venues if v.get("id")}
            events = await self.api.events(list(names), start, end)
            productions = await self.api.productions([e.get("productionId") for e in events])
            soon_productions = await self.api.coming_soon(now.date())
        except CinevilleError as err:
            raise UpdateFailed(str(err)) from err
        if not names:
            _LOGGER.warning("No Cineville theaters left in %s for this entry", city)
        films = sort_films(now_showing(events, productions, names, now, zone), how)
        return CinevilleData(
            city=city,
            venues=list(names.values()),
            now=films,
            soon=coming_soon(soon_productions, events, now.date(), weeks),
            updated=now,
        )
