# Scan 01 — packtool: compose (multi-event weekend assembly)
"""`packtool compose events.json` merges event packs + library stations into
one weekend pack (spec §7): each event maps to a venue (0 or 1, v0 max 2),
entries are venue-tagged so the LIST dividers work, duplicate frequencies
are flagged, and the capacity trade-off is reported with trim options.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field

from .model import Car, Pack, Station, ValidationError, validate


@dataclass
class ComposeReport:
    warnings: list = field(default_factory=list)
    trims: list = field(default_factory=list)


def compose(events: list[dict], library_stations: list[Station] | None = None,
            series: str = "", track: str = "", session: str = "",
            season: int = 0, date: str = "", author: str = "") -> tuple[Pack, ComposeReport]:
    """events: [{"pack": path-or-Pack, "venue": 0|1, "name": "IRP"}...]."""
    report = ComposeReport()
    cars: list[Car] = []
    stations: dict[tuple[str, float], Station] = {}
    venues: list[str] = []

    for ev in events:
        pack = ev["pack"] if isinstance(ev["pack"], Pack) else Pack.load(ev["pack"])
        venue = int(ev.get("venue", 0))
        name = str(ev.get("name", pack.meta.get("track", f"venue {venue}")))
        if venue >= 2:
            report.warnings.append(f"{name}: venue {venue} folds into venue 1 (v0 max 2)")
            venue = 1
        while len(venues) <= venue:
            venues.append("")
        venues[venue] = name
        for c in pack.cars:
            c = Car(**{**c.__dict__, "venue": venue})
            cars.append(c)
        for s in pack.stations:
            s = Station(**{**s.__dict__, "venue": venue})
            key = (s.name, s.freq or s.fm or 0.0)
            if key in stations:
                report.warnings.append(f"station {s.name} duplicated across events — keeping the first")
            else:
                stations[key] = s

    for s in library_stations or []:
        key = (s.name, s.freq or s.fm or 0.0)
        stations.setdefault(key, s)

    # dedupe cars by number (across events) — keep both as ALT when freqs differ
    seen: dict[str, Car] = {}
    for c in cars:
        if c.number in seen:
            prev = seen[c.number]
            if prev.freqs[0] != c.freqs[0]:
                prev.freqs.append(c.freqs[0])
                report.warnings.append(
                    f"car {c.number} appears in two events with different freqs — saved as ALT")
        else:
            seen[c.number] = c

    meta = {"series": series, "track": track, "session": session,
            "season": season, "date": date, "author": author, "version": 1,
            "venues": venues}
    pack = Pack(meta=meta, cars=list(seen.values()),
                stations=list(stations.values()), lockouts=[])

    try:
        report.warnings += [w.text for w in validate(pack)]
    except ValidationError as e:
        report.warnings.append(str(e))

    # capacity trade-off
    n_cars = len(pack.flattened())
    n_stations = len([s for s in pack.stations if s.kind != "broadcast"])
    if n_cars + n_stations > 88:
        overflow = n_cars + n_stations - 88
        report.trims.append(
            f"{n_cars} entries + {n_stations} stations = {n_cars + n_stations} > 88: "
            f"drop {overflow} non-essential stations (broadcast stations are free) "
            f"or move the second venue to its own pack")
    return pack, report


def compose_from_file(events_path: str, library_paths: list[str] | None = None,
                      **meta) -> tuple[Pack, ComposeReport]:
    with open(events_path) as f:
        events = json.load(f)
    lib: list[Station] = []
    for path in library_paths or []:
        p = Pack.load(path)
        lib += p.stations
    return compose(events, lib, **meta)
