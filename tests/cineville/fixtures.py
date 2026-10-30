"""Answers of api.cineville.nl as they came on 30 October 2026, cut down to a few theaters and films. The shapes, ids
and values are the API's own; only the lists are shorter and long texts shorter."""

KRITERION = "d3a2eac6-6b6f-4f46-8a25-4a78a6774587"
EYE = "a1467fa6-102e-45f7-8a5d-a5665d828dd2"
CINECENTER = "ef8693b3-d6cb-4d63-a031-8de614022160"

COWARD = "122379ea-28f2-4152-8aad-280d98bec3f9"
SUDDEN = "db2148e7-9d83-4118-a636-7064e9a2d29b"
PRIMETIME = "29059902-d977-4f31-982d-540e7b1d66dd"
AKIRA = "4b1f0c39-2f0e-4b8e-9a8a-6c7d6e1f2a11"
WILD_HORSE = "5c2e1d4a-3f1f-4c9f-8b9b-7d8e7f2a3b22"

VENUES = {
    "count": 7,
    "totalCount": 7,
    "_links": {"self": {"href": "/venues?page[limit]=100"}},
    "_embedded": {"venues": [
        {"id": "7a440a8b-5789-4f6f-89d4-9cff300d97a0", "name": "Focus Filmtheater", "slug": "focus-filmtheater-arnhem",
         "isHidden": False, "address": {"city": "Arnhem"}},
        {"id": CINECENTER, "name": "Cinecenter", "slug": "cinecenter", "isHidden": False,
         "address": {"city": "Amsterdam"}},
        {"id": KRITERION, "name": "Kriterion", "slug": "kriterion", "isHidden": False,
         "address": {"city": "Amsterdam"}},
        {"id": EYE, "name": "Eye", "slug": "eye", "isHidden": False, "address": {"city": "Amsterdam"}},
        {"id": "2debd383-e231-4d60-ba12-b4e046b641ba", "name": "Cinedans", "slug": "cinedans", "isHidden": True,
         "address": {"city": "Amsterdam"}},
        {"id": "645f5bcc-59f6-42d0-96d2-7bb04e8653ad", "name": "Kaboom Animation Festival",
         "slug": "kaboom-animation-festival", "isHidden": False, "address": {"city": "Utrecht, Amsterdam"}},
        {"id": "8b980b84-b7c6-4725-aa2e-451eb9f99b43", "name": "Cinebergen", "slug": "cinebergen", "isHidden": False,
         "address": {"city": "Bergen (NH)"}},
    ]},
}


def _event(id_, production, venue, start, tags=None):
    return {"id": id_, "productionId": production, "productionTypeId": "film", "venueId": venue,
            "startDate": start, "endDate": start, "isHidden": False,
            "attributes": {"tags": tags} if tags else {}, "localizableAttributes": {}}


# Two pages, as /events/search pages them: the first names the second in _links.next.
EVENTS_PAGE_1 = {
    "count": 4,
    "totalCount": 7,
    "_links": {"self": {"href": "/events/search?page[limit]=100"},
               "next": {"href": "/events/search?page[limit]=4&page[after]=eyJzdGFydERhdGUiOiIyMDI2In0"}},
    "_embedded": {"events": [
        _event("2be8f3a6-9762-4c06-afbf-702207e94c7b", COWARD, CINECENTER, "2026-10-30T10:00:00.000Z"),
        _event("e1", SUDDEN, KRITERION, "2026-10-30T13:30:00.000Z"),
        _event("e2", COWARD, KRITERION, "2026-10-30T18:15:00.000Z"),
        _event("e3", AKIRA, EYE, "2026-10-30T20:45:00.000Z", ["classic"]),
    ]},
}
EVENTS_PAGE_2 = {
    "count": 3,
    "totalCount": 7,
    "_links": {"self": {"href": "/events/search?page[limit]=4&page[after]=eyJzdGFydERhdGUiOiIyMDI2In0"}},
    "_embedded": {"events": [
        _event("e4", PRIMETIME, CINECENTER, "2026-10-31T19:00:00.000Z"),
        _event("e5", COWARD, EYE, "2026-11-01T15:00:00.000Z"),
        _event("e6", WILD_HORSE, KRITERION, "2026-11-01T20:00:00.000Z", ["preview"]),
    ]},
}


def _production(id_, slug, title, directors, year, premiere, highlights=(), poster=True):
    return {
        "id": id_, "slug": slug, "title": title, "productionTypeId": "film",
        "attributes": {"directors": list(directors), "duration": 120, "releaseYear": year, "premiereDate": premiere,
                       "highlights": list(highlights), "spokenLanguages": ["en"], "cast": []},
        "localizableAttributes": {"shortDescription": f"About {title}."},
        "assets": {"poster": {"url": f"https://culturekit-assets.imgix.net/1/assets/{id_}.jpg"}} if poster else {},
    }


PRODUCTIONS = {
    "count": 5,
    "totalCount": 5,
    "_links": {"self": {"href": "/productions/search?page[limit]=100"}},
    "_embedded": {"productions": [
        _production(COWARD, "coward", "Coward", ["Lukas Dhont"], 2026, "2026-10-22"),
        _production(SUDDEN, "all-of-a-sudden", "All of a Sudden", ["Ryusuke Hamaguchi"], 2026, "2026-10-29", [
            {"type": "filmOfTheWeek", "author": "", "endDate": "2026-11-04", "startDate": "2026-10-28"},
            {"type": "tip", "author": "Emin", "endDate": None, "startDate": None},
        ]),
        # The API writes a director with a newline after the name now and then.
        _production(PRIMETIME, "primetime", "Primetime", ["Lance Oppenheim\n"], 2026, "2026-10-29", [
            {"type": "tip", "author": "Emin", "endDate": None, "startDate": None},
        ]),
        _production(AKIRA, "akira-re-release", "Akira (re-release)", ["Katsuhiro Ôtomo"], 1988, "2026-09-10", [
            # A film of the week of an earlier week: no longer a highlight.
            {"type": "filmOfTheWeek", "author": "", "endDate": "2026-09-16", "startDate": "2026-09-10"},
        ], poster=False),
        _production(WILD_HORSE, "wild-horse-nine", "Wild Horse Nine", ["Martin McDonagh"], 2026, "2026-11-05", [
            {"type": "upcomingFilm", "author": "", "endDate": None, "startDate": None},
        ]),
    ]},
}

SOON = {
    "count": 3,
    "totalCount": 3,
    "_links": {},
    "_embedded": {"productions": [
        _production("s1", "the-devils-re-release", "The Devils (re-release)", ["Ken Russell"], 1971, "2026-11-12"),
        _production(WILD_HORSE, "wild-horse-nine", "Wild Horse Nine", ["Martin McDonagh"], 2026, "2026-11-05"),
        _production("s3", "where-is-the-friends-house", "Where is the Friend’s House? (re-release)",
                    ["Abbas Kiarostami"], 1987, "2027-06-24"),
    ]},
}
