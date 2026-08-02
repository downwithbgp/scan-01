# Scan 01 — packtool: import + compose against the real data fixtures
import json
from pathlib import Path

import pytest

from packtool.compose import compose
from packtool.importers.indyspeedway import import_text
from packtool.model import Pack, Station, ValidationError, validate

ROOT = Path(__file__).parent.parent
SOURCE = ROOT / "race-packs" / "indyspeedway-ims-2026" / "source.txt"
DRAFT = ROOT / "race-packs" / "indyspeedway-ims-2026" / "pack.json"


def test_import_matches_draft():
    pack = import_text(SOURCE.read_text())
    draft = Pack.load(str(DRAFT))

    assert pack.meta["track"] == draft.meta["track"]
    got = {c.number: c for c in pack.cars}
    want = {c.number: c for c in draft.cars}
    assert set(got) == set(want), f"car numbers differ: {set(got) ^ set(want)}"
    for num, c in want.items():
        assert got[num].freqs == c.freqs, f"car {num}: freqs {got[num].freqs} != {c.freqs}"
        # the flat token stream cannot recover multi-word name boundaries —
        # numbers+freqs pair exactly, drivers are best-effort tokens until a
        # roster-based resolver lands (P0; the extract itself notes this)
        assert got[num].driver, f"car {num}: a driver token was imported"

    gs = {(s.name, s.freq or s.fm): s for s in pack.stations}
    ws = {(s.name, s.freq or s.fm): s for s in draft.stations}
    missing = set(ws) - set(gs)
    assert not missing, f"stations missing from the import: {missing}"
    for key in set(ws) & set(gs):
        assert ws[key].kind == gs[key].kind, f"station {key}: kind"
        assert ws[key].tone == gs[key].tone, f"station {key}: tone"
        assert ws[key].digital == gs[key].digital, f"station {key}: digital"

    validate(pack)                              # the import is buildable


def test_compose_two_venues():
    """The Brickyard + IRP style weekend: two venue bits + divider order."""
    event_a = Pack(meta={"track": "IMS"},
                   cars=[_car("24", 450.8875, 0), _car("3", 451.1000, 0)],
                   stations=[Station(name="CTRL", kind="control", freq=461.2)])
    event_b = Pack(meta={"track": "IRP"},
                   cars=[_car("17", 452.5000, 1)],
                   stations=[Station(name="PA", kind="pa", freq=151.5)])
    pack, report = compose(
        [{"pack": event_a, "venue": 0, "name": "IMS"},
         {"pack": event_b, "venue": 1, "name": "IRP"}],
        series="IndyCar", track="IMS", session="Weekend", season=2026)
    assert pack.meta["venues"] == ["IMS", "IRP"]
    assert [c.venue for c in pack.flattened()] == [0, 0, 1]     # divider once
    assert pack.stations[0].venue == 0 and pack.stations[1].venue == 1
    validate(pack)


def test_compose_dedupes_and_caps():
    a = Pack(meta={"track": "A"}, cars=[_car("24", 450.8875, 0)],
             stations=[Station(name="PA", kind="pa", freq=450.0)])
    b = Pack(meta={"track": "B"}, cars=[_car("24", 452.0000, 1)],
             stations=[Station(name="PA", kind="pa", freq=450.0)])   # dup
    pack, report = compose([{"pack": a, "venue": 0}, {"pack": b, "venue": 1}])
    assert len(pack.stations) == 1
    assert pack.cars[0].freqs == [450.8875, 452.0000]              # ALT
    assert any("ALT" in w for w in report.warnings)
    assert any("duplicated" in w for w in report.warnings)

    big = Pack(meta={"track": "C"}, cars=[_car(str(i), 450.0 + i / 1000, 0) for i in range(60)],
               stations=[Station(name=f"S{i}", kind="control", freq=460.0 + i / 1000)
                         for i in range(30)])
    pack, report = compose([{"pack": big, "venue": 0}])
    assert report.trims, "capacity trade-off must be reported"


def test_library_fixtures_compose():
    """The checked-in library fixtures build into valid packs."""
    for path in [ROOT / "race-packs" / "library" / "series" / "woo-sprints.json",
                 ROOT / "race-packs" / "library" / "daily" / "daily-presets.json"]:
        pack = Pack.load(str(path))
        validate(pack)                          # they are buildable as-is


def _car(number, freq, venue):
    from packtool.model import Car
    return Car(number=number, driver=f"Driver {number}", freqs=[freq], venue=venue)
