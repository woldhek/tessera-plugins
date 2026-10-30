"""A small client for api.cineville.nl, the API behind cineville.nl.

The website reads the same endpoints from the browser without a key:

- GET  /venues                 every theater, with its address (the city)
- POST /events/search          showtimes, filtered by theater and time, sorted, paged by a `next` link
- POST /productions/search     films, by id or by premiere date

A search takes a JSON body of filters (`{"venueId": {"in": [...]}, "startDate": {"gte": "..."}}`), `sort` and
`page.limit` (at most 100). Each answer is HAL: the items under `_embedded.<kind>`, the next page in `_links.next.href`,
which is posted the same body again.
"""

from __future__ import annotations

import asyncio
from datetime import date, datetime, timezone
import logging
from typing import Any

import aiohttp

from .const import API_URL

_LOGGER = logging.getLogger(__name__)

PAGE = 100
# A city as large as Amsterdam has some 1500 showtimes in a week: 15 pages. This bounds a runaway.
MAX_PAGES = 60
TIMEOUT = aiohttp.ClientTimeout(total=30)


class CinevilleError(Exception):
    """The API could not be reached or answered something unexpected."""


def iso(moment: datetime) -> str:
    """A moment as the API writes it: UTC with milliseconds and a Z."""
    return moment.astimezone(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")


class CinevilleApi:
    """The few calls the integration needs."""

    def __init__(self, session: aiohttp.ClientSession, locale: str = "nl-NL") -> None:
        self._session = session
        self._locale = locale

    async def _request(self, method: str, path: str, body: dict[str, Any] | None = None) -> dict[str, Any]:
        url = path if path.startswith("http") else f"{API_URL}{path}"
        headers = {"accept": "application/json", "locale": self._locale}
        try:
            async with self._session.request(method, url, json=body, headers=headers, timeout=TIMEOUT) as response:
                if response.status != 200:
                    raise CinevilleError(f"{method} {path}: HTTP {response.status}")
                data = await response.json(content_type=None)
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
            raise CinevilleError(f"{method} {path}: {err}") from err
        if not isinstance(data, dict):
            raise CinevilleError(f"{method} {path}: not a JSON object")
        return data

    async def _all(self, method: str, path: str, kind: str, body: dict[str, Any] | None = None) -> list[dict]:
        """Every item of a paged answer, following `_links.next`."""
        items: list[dict] = []
        url: str | None = path
        for _ in range(MAX_PAGES):
            if url is None:
                break
            data = await self._request(method, url, body)
            items.extend((data.get("_embedded") or {}).get(kind) or [])
            nxt = ((data.get("_links") or {}).get("next") or {}).get("href")
            url = nxt or None
        else:
            _LOGGER.warning("Stopped after %s pages of %s", MAX_PAGES, path)
        return items

    async def venues(self) -> list[dict]:
        """Every theater (the hidden ones too: the caller leaves those out)."""
        return await self._all("GET", f"/venues?page[limit]={PAGE}", "venues")

    async def events(self, venue_ids: list[str], start: datetime, end: datetime) -> list[dict]:
        """The showtimes of these theaters from `start` up to `end`, soonest first."""
        if not venue_ids:
            return []
        body = {
            "productionId": {"isNull": False},
            "isHidden": {"eq": False},
            "venueId": {"in": venue_ids},
            "startDate": {"gte": iso(start), "lt": iso(end)},
            "sort": {"startDate": "asc"},
            "page": {"limit": PAGE},
        }
        return await self._all("POST", "/events/search", "events", body)

    async def productions(self, ids: list[str]) -> list[dict]:
        """The films with these ids, a hundred at a time."""
        out: list[dict] = []
        unique = list(dict.fromkeys(i for i in ids if i))
        for at in range(0, len(unique), PAGE):
            body = {"id": {"in": unique[at : at + PAGE]}, "page": {"limit": PAGE}}
            out.extend(await self._all("POST", "/productions/search", "productions", body))
        return out

    async def coming_soon(self, after: date) -> list[dict]:
        """Films whose premiere in the Netherlands is after `after`: Cineville's own "Verwacht" list."""
        body = {
            "premiereDate": {"gt": after.isoformat()},
            "productionTypeId": {"eq": "film"},
            "page": {"limit": PAGE},
        }
        return await self._all("POST", "/productions/search", "productions", body)
