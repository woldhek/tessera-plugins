"""From the API's showtimes and films to the lists the sensors show. No Home Assistant in here, so it is tested on
its own (tests/test_films.py)."""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
from datetime import date, datetime, time, timedelta, timezone, tzinfo
from typing import Any

from .const import COMPACT_FILMS, HIGHLIGHT_TIP, HIGHLIGHT_WEEK, WEBSITE

# The order of a highlight in "recommended": Cineville's film of the week first, then a tip of its editors.
_RANK = {HIGHLIGHT_WEEK: 0, HIGHLIGHT_TIP: 1, "": 2}


def parse_moment(text: str | None) -> datetime | None:
    """An ISO 8601 moment of the API ("2026-10-30T19:15:00.000Z"), aware."""
    if not text:
        return None
    try:
        moment = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None
    return moment if moment.tzinfo else moment.replace(tzinfo=timezone.utc)


def parse_day(text: str | None) -> date | None:
    if not text:
        return None
    try:
        return date.fromisoformat(text[:10])
    except ValueError:
        return None


def city_names(venue: dict) -> list[str]:
    """The cities a theater counts for. A festival in two cities writes "Utrecht, Amsterdam"."""
    city = ((venue.get("address") or {}).get("city") or "").strip()
    return [c.strip() for c in city.split(",") if c.strip()]


def cities(venues: list[dict]) -> list[str]:
    """Every city with a theater that shows, sorted."""
    found = {c for v in venues if not v.get("isHidden") for c in city_names(v)}
    return sorted(found, key=str.casefold)


def venues_in(venues: list[dict], city: str) -> list[dict]:
    """The theaters of a city that show, by name."""
    want = city.casefold()
    found = [v for v in venues if not v.get("isHidden") and any(c.casefold() == want for c in city_names(v))]
    return sorted(found, key=lambda v: (v.get("name") or "").casefold())


def highlight(production: dict, today: date) -> tuple[str, str]:
    """The film's highlight today, as (code, author): ("week", ""), ("tip", "Emin") or ("", ""). Like the website, a
    highlight counts from its start date up to and including its end date, and always when it has none."""
    best = ("", "")
    for item in (production.get("attributes") or {}).get("highlights") or []:
        kind = {"filmOfTheWeek": HIGHLIGHT_WEEK, "tip": HIGHLIGHT_TIP}.get(item.get("type") or "")
        if not kind:
            continue
        start, end = parse_day(item.get("startDate")), parse_day(item.get("endDate"))
        if (start and today < start) or (end and today > end):
            continue
        if _RANK[kind] < _RANK[best[0]]:
            best = (kind, (item.get("author") or "").strip())
    return best


def _directors(production: dict) -> str:
    names = [(d or "").strip() for d in (production.get("attributes") or {}).get("directors") or []]
    return ", ".join(n for n in names if n)


def _poster(production: dict) -> str | None:
    assets = production.get("assets") or {}
    for key in ("poster", "cover"):
        url = (assets.get(key) or {}).get("url")
        if url:
            return url
    return None


@dataclass
class Film:
    """A film and its showtimes in the chosen theaters."""

    id: str
    title: str
    slug: str = ""
    directors: str = ""
    year: int | None = None
    duration: int | None = None
    premiere: date | None = None
    highlight: str = ""
    tip_by: str = ""
    description: str = ""
    poster: str | None = None
    shows: int = 0
    shows_today: int = 0
    next_start: datetime | None = None
    next_venue: str = ""
    venues: list[str] = field(default_factory=list)
    tags: set[str] = field(default_factory=set)

    @property
    def url(self) -> str:
        return f"{WEBSITE}/films/{self.slug}" if self.slug else WEBSITE

    def detail(self, zone: tzinfo) -> dict[str, Any]:
        """Everything about the film, for a dashboard or an automation."""
        return {
            "title": self.title,
            "directors": self.directors,
            "year": self.year,
            "duration": self.duration,
            "premiere": self.premiere.isoformat() if self.premiere else None,
            "highlight": self.highlight,
            "tip_by": self.tip_by,
            "shows": self.shows,
            "shows_today": self.shows_today,
            "next_start": self.next_start.astimezone(zone).isoformat() if self.next_start else None,
            "next_venue": self.next_venue,
            "venues": self.venues,
            "preview": "preview" in self.tags,
            "classic": "classic" in self.tags,
            "description": self.description,
            "poster": self.poster,
            "url": self.url,
        }


def film_of(production: dict, today: date) -> Film:
    attributes = production.get("attributes") or {}
    code, author = highlight(production, today)
    year = attributes.get("releaseYear")
    duration = attributes.get("duration")
    return Film(
        id=production.get("id") or "",
        title=(production.get("title") or "").strip(),
        slug=production.get("slug") or "",
        directors=_directors(production),
        year=year if isinstance(year, int) else None,
        duration=duration if isinstance(duration, int) else None,
        premiere=parse_day(attributes.get("premiereDate")),
        highlight=code,
        tip_by=author,
        description=((production.get("localizableAttributes") or {}).get("shortDescription") or "").strip(),
        poster=_poster(production),
    )


def now_showing(events: list[dict], productions: list[dict], venue_names: dict[str, str], now: datetime,
                zone: tzinfo) -> list[Film]:
    """The films of these showtimes, each with how often it shows, how often today, and when and where next. A
    showtime that already began is still counted for the day (today's list), but is not the next one."""
    today = now.astimezone(zone).date()
    by_id = {p.get("id"): p for p in productions if p.get("id")}
    films: dict[str, Film] = {}
    venues_of: dict[str, list[str]] = defaultdict(list)
    for event in events:
        pid = event.get("productionId")
        production = by_id.get(pid)
        start = parse_moment(event.get("startDate"))
        if not production or start is None or event.get("isHidden"):
            continue
        film = films.get(pid)
        if film is None:
            film = films[pid] = film_of(production, today)
            if not film.title:
                del films[pid]
                continue
        film.shows += 1
        if start.astimezone(zone).date() == today:
            film.shows_today += 1
        venue = venue_names.get(event.get("venueId") or "", "")
        if venue and venue not in venues_of[pid]:
            venues_of[pid].append(venue)
        if start >= now and (film.next_start is None or start < film.next_start):
            film.next_start = start
            film.next_venue = venue
        film.tags.update((event.get("attributes") or {}).get("tags") or [])
    for pid, film in films.items():
        film.venues = venues_of[pid]
    return list(films.values())


def sort_films(films: list[Film], how: str) -> list[Film]:
    far = datetime.max.replace(tzinfo=timezone.utc)
    if how == "shows":
        key = lambda f: (-f.shows, f.title.casefold())  # noqa: E731
    elif how == "next":
        key = lambda f: (f.next_start or far, f.title.casefold())  # noqa: E731
    elif how == "title":
        key = lambda f: f.title.casefold()  # noqa: E731
    else:  # recommended: Cineville's picks first, then what shows most
        key = lambda f: (_RANK.get(f.highlight, 2), -f.shows, f.title.casefold())  # noqa: E731
    return sorted(films, key=key)


def coming_soon(productions: list[dict], events: list[dict], today: date, weeks: int) -> list[Film]:
    """Films with a premiere after today and within `weeks`, soonest first; `shows` counts their previews in the
    chosen theaters (showtimes before the premiere)."""
    last = today + timedelta(weeks=weeks)
    previews: dict[str, int] = defaultdict(int)
    for event in events:
        if event.get("productionId"):
            previews[event["productionId"]] += 1
    out = []
    for production in productions:
        film = film_of(production, today)
        if not film.title or film.premiere is None or not (today < film.premiere <= last):
            continue
        film.shows = previews.get(film.id, 0)
        out.append(film)
    out.sort(key=lambda f: (f.premiere, _RANK.get(f.highlight, 2), -f.shows, f.title.casefold()))
    return out


def epoch(moment: datetime | None) -> int:
    return int(moment.timestamp()) if moment else 0


def day_epoch(day: date | None, zone: tzinfo) -> int:
    """Noon of a day in the zone: a moment that is that day wherever the screen is."""
    return int(datetime.combine(day, time(12), zone).timestamp()) if day else 0


def compact(films: list[Film], kind: str, zone: tzinfo) -> dict[str, Any]:
    """The short parallel lists a Tessera screen reads (lists of texts and numbers, not objects), at most
    COMPACT_FILMS long. `kind` is "now" or "soon"."""
    top = films[:COMPACT_FILMS]
    out: dict[str, Any] = {
        "cineville_list": kind,
        "titles": [f.title for f in top],
        "highlights": [f.highlight for f in top],
        "directors": [f.directors for f in top],
        "shows": [f.shows for f in top],
    }
    if kind == "now":
        out["shows_today"] = [f.shows_today for f in top]
        out["next_start"] = [epoch(f.next_start) for f in top]
        out["next_venue"] = [f.next_venue for f in top]
    else:
        out["premieres"] = [day_epoch(f.premiere, zone) for f in top]
    return out
