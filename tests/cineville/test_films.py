"""The parts of the Cineville integration that need no Home Assistant: the client (with a fake aiohttp) and the lists.

    python3 -m pytest tests/cineville
"""

from __future__ import annotations

import asyncio
from datetime import date, datetime, timezone
import importlib.util
import sys
import types
from pathlib import Path
from zoneinfo import ZoneInfo

import pytest

from fixtures import (AKIRA, CINECENTER, COWARD, EVENTS_PAGE_1, EVENTS_PAGE_2, EYE, KRITERION, PRIMETIME,
                      PRODUCTIONS, SOON, SUDDEN, VENUES, WILD_HORSE)

ROOT = Path(__file__).resolve().parents[2] / "custom_components" / "cineville"
AMS = ZoneInfo("Europe/Amsterdam")


# ---- A fake aiohttp, so the client runs without it and without the network ----

class FakeResponse:
    def __init__(self, status, data):
        self.status, self._data = status, data

    async def json(self, content_type=None):
        return self._data

    async def __aenter__(self):
        return self

    async def __aexit__(self, *exc):
        return False


class FakeSession:
    """Answers by (method, path) and records every request."""

    def __init__(self, answers):
        self.answers, self.calls = answers, []

    def request(self, method, url, json=None, headers=None, timeout=None):
        path = url.replace("https://api.cineville.nl", "")
        self.calls.append((method, path, json, headers))
        key = (method, path.split("?")[0] if method == "GET" else path)
        answer = self.answers.get(key)
        if callable(answer):
            answer = answer(json)
        return FakeResponse(200, answer) if answer is not None else FakeResponse(404, {})


aiohttp = types.ModuleType("aiohttp")
aiohttp.ClientError = type("ClientError", (Exception,), {})
aiohttp.ClientSession = FakeSession
aiohttp.ClientTimeout = lambda total=None: total
sys.modules.setdefault("aiohttp", aiohttp)


def load(name):
    """A module of the integration, inside a bare package (its __init__ needs Home Assistant)."""
    package = sys.modules.get("cineville") or types.ModuleType("cineville")
    package.__path__ = [str(ROOT)]
    sys.modules["cineville"] = package
    spec = importlib.util.spec_from_file_location(f"cineville.{name}", ROOT / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


const = load("const")
films = load("films")
api = load("api")


def run(coro):
    return asyncio.run(coro)


# ---- Cities and theaters ----

def test_cities_leave_hidden_theaters_out_and_split_two_cities():
    venues = VENUES["_embedded"]["venues"]
    assert films.cities(venues) == ["Amsterdam", "Arnhem", "Bergen (NH)", "Utrecht"]
    names = [v["name"] for v in films.venues_in(venues, "amsterdam")]
    assert names == ["Cinecenter", "Eye", "Kaboom Animation Festival", "Kriterion"]


# ---- The client ----

def test_events_follow_the_next_link_with_the_same_body():
    session = FakeSession({
        ("POST", "/events/search"): EVENTS_PAGE_1,
        ("POST", EVENTS_PAGE_1["_links"]["next"]["href"]): EVENTS_PAGE_2,
    })
    client = api.CinevilleApi(session, "en-GB")
    start = datetime(2026, 10, 29, 23, 0, tzinfo=timezone.utc)
    end = datetime(2026, 11, 5, 23, 0, tzinfo=timezone.utc)
    events = run(client.events([KRITERION, EYE], start, end))
    assert len(events) == 7
    assert len(session.calls) == 2
    body = session.calls[0][2]
    assert body["venueId"] == {"in": [KRITERION, EYE]}
    assert body["startDate"] == {"gte": "2026-10-29T23:00:00.000Z", "lt": "2026-11-05T23:00:00.000Z"}
    assert body["sort"] == {"startDate": "asc"} and body["page"] == {"limit": 100}
    assert session.calls[1][2] == body
    assert session.calls[0][3]["locale"] == "en-GB"


def test_productions_ask_once_per_id_and_in_hundreds():
    asked = []

    def answer(body):
        asked.append(body["id"]["in"])
        return {"_embedded": {"productions": [{"id": i} for i in body["id"]["in"]]}, "_links": {}}

    session = FakeSession({("POST", "/productions/search"): answer})
    ids = [f"p{i}" for i in range(150)] + ["p3", None, ""]
    out = run(api.CinevilleApi(session).productions(ids))
    assert len(out) == 150
    assert [len(a) for a in asked] == [100, 50]


def test_venues_and_a_failing_answer():
    session = FakeSession({("GET", "/venues"): VENUES})
    assert len(run(api.CinevilleApi(session).venues())) == 7
    with pytest.raises(api.CinevilleError):
        run(api.CinevilleApi(FakeSession({})).venues())


def test_no_theaters_asks_nothing():
    session = FakeSession({})
    assert run(api.CinevilleApi(session).events([], datetime.now(timezone.utc), datetime.now(timezone.utc))) == []
    assert session.calls == []


# ---- Highlights ----

def test_highlight_follows_its_dates_and_prefers_the_film_of_the_week():
    by_id = {p["id"]: p for p in PRODUCTIONS["_embedded"]["productions"]}
    assert films.highlight(by_id[SUDDEN], date(2026, 10, 30)) == ("week", "")
    assert films.highlight(by_id[SUDDEN], date(2026, 11, 5)) == ("tip", "Emin")      # the week is over
    assert films.highlight(by_id[SUDDEN], date(2026, 10, 27)) == ("tip", "Emin")     # not yet begun
    assert films.highlight(by_id[AKIRA], date(2026, 10, 30)) == ("", "")
    assert films.highlight(by_id[WILD_HORSE], date(2026, 10, 30)) == ("", "")       # upcomingFilm: no pick


# ---- Now showing ----

EVENTS = EVENTS_PAGE_1["_embedded"]["events"] + EVENTS_PAGE_2["_embedded"]["events"]
PRODS = PRODUCTIONS["_embedded"]["productions"]
NAMES = {KRITERION: "Kriterion", EYE: "Eye", CINECENTER: "Cinecenter"}
# Friday 30 October, 15:00 in Amsterdam: Coward at Cinecenter (11:00) and All of a Sudden (14:30) began already.
NOW = datetime(2026, 10, 30, 14, 0, tzinfo=timezone.utc)


def test_now_showing_counts_and_next_showtime():
    found = {f.id: f for f in films.now_showing(EVENTS, PRODS, NAMES, NOW, AMS)}
    coward = found[COWARD]
    assert (coward.shows, coward.shows_today) == (3, 2)
    assert coward.next_start == datetime(2026, 10, 30, 18, 15, tzinfo=timezone.utc)
    assert coward.next_venue == "Kriterion"
    assert coward.venues == ["Cinecenter", "Kriterion", "Eye"]
    sudden = found[SUDDEN]
    assert (sudden.shows, sudden.shows_today, sudden.next_start) == (1, 1, None)   # today's only show began
    assert sudden.highlight == "week"
    assert found[PRIMETIME].directors == "Lance Oppenheim"
    assert found[PRIMETIME].shows_today == 0
    assert "classic" in found[AKIRA].tags and "preview" in found[WILD_HORSE].tags
    assert found[AKIRA].poster is None
    assert found[COWARD].url == "https://www.cineville.nl/films/coward"


def test_a_showtime_of_an_unknown_film_is_left_out():
    events = EVENTS + [{"id": "x", "productionId": "gone", "venueId": EYE, "startDate": "2026-10-30T20:00:00Z"}]
    assert len(films.now_showing(events, PRODS, NAMES, NOW, AMS)) == 5


def test_sorts():
    found = films.now_showing(EVENTS, PRODS, NAMES, NOW, AMS)
    titles = lambda how: [f.title for f in films.sort_films(found, how)]  # noqa: E731
    assert titles("recommended") == ["All of a Sudden", "Primetime", "Coward", "Akira (re-release)", "Wild Horse Nine"]
    assert titles("shows")[0] == "Coward"
    assert titles("next") == ["Coward", "Akira (re-release)", "Primetime", "Wild Horse Nine", "All of a Sudden"]
    assert titles("title") == ["Akira (re-release)", "All of a Sudden", "Coward", "Primetime", "Wild Horse Nine"]


def test_compact_lists_for_the_screen():
    found = films.sort_films(films.now_showing(EVENTS, PRODS, NAMES, NOW, AMS), "recommended")
    out = films.compact(found, "now", AMS)
    assert out["cineville_list"] == "now"
    assert out["titles"][:3] == ["All of a Sudden", "Primetime", "Coward"]
    assert out["highlights"][:3] == ["week", "tip", ""]
    assert out["shows"][2] == 3 and out["shows_today"][2] == 2
    assert out["next_start"][0] == 0                                            # none left
    assert out["next_start"][2] == int(datetime(2026, 10, 30, 18, 15, tzinfo=timezone.utc).timestamp())
    assert out["next_venue"][2] == "Kriterion"
    assert {len(v) for k, v in out.items() if isinstance(v, list)} == {5}
    for value in out.values():
        if isinstance(value, list):
            assert all(isinstance(x, (str, int)) for x in value)               # texts and numbers, no objects


def test_compact_is_at_most_sixteen():
    many = [films.Film(id=str(i), title=f"Film {i}") for i in range(40)]
    assert len(films.compact(many, "now", AMS)["titles"]) == const.COMPACT_FILMS == 16


def test_detail_is_plain_json():
    import json
    found = films.now_showing(EVENTS, PRODS, NAMES, NOW, AMS)
    detail = [f.detail(AMS) for f in found]
    json.dumps(detail)
    coward = next(d for d in detail if d["title"] == "Coward")
    assert coward["next_start"] == "2026-10-30T19:15:00+01:00"


# ---- Coming soon ----

def test_coming_soon_within_the_weeks_with_previews():
    soon = films.coming_soon(SOON["_embedded"]["productions"], EVENTS, date(2026, 10, 30), 4)
    assert [f.title for f in soon] == ["Wild Horse Nine", "The Devils (re-release)"]   # June 2027 is too far
    assert soon[0].shows == 1                                                          # its preview at Kriterion
    out = films.compact(soon, "soon", AMS)
    assert out["cineville_list"] == "soon"
    assert out["premieres"][0] == int(datetime(2026, 11, 5, 12, 0, tzinfo=AMS).timestamp())
    assert "next_start" not in out
