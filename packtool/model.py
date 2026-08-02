# Scan 01 — packtool: the pack model + validation
"""The logical pack (spec §3) + §6 validation. Pure data — no firmware
dependencies beyond the tone tables (dcs.py).
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from typing import Any, Optional

from .dcs import CODE_TYPE_CTCSS, tone_to_index

MAX_CARS = 64
MAX_STATIONS = 24
MAX_VENUES = 2

KINDS = {"broadcast", "control", "pa", "officials", "safety", "radio", "media", "noaa", "other"}

NUMBER_RE = re.compile(r"^[1-9]\d{0,2}[A-Z]?$")  # 1-3 digits + optional suffix, no leading zero

# entry bands (spec §6): MHz (low, high, note)
BANDS = [
    (88.000, 108.000, "broadcast"),
    (108.000, 137.000, "airband (AM only)"),
    (151.000, 160.000, "VHF dirt-track"),
    (156.000, 162.000, "marine VHF"),
    (162.400, 162.550, "NOAA"),
    (450.000, 470.000, "UHF racing / FRS-GMRS"),
    (462.550, 467.725, "FRS/GMRS"),
]
ECPA_BANDS = [(824.0, 894.0), (1710.0, 1755.0), (1850.0, 1990.0), (2110.0, 2155.0)]


class ValidationError(Exception):
    """Hard validation failure: the pack cannot be built."""


class PackWarning:
    def __init__(self, text: str):
        self.text = text


@dataclass
class Car:
    number: str
    driver: str = ""
    team: str = ""
    entry: str = ""
    freqs: list = field(default_factory=list)      # MHz floats, 1-2
    tone: Any = 0
    group: str = "A"
    favorite: bool = False
    origin: str = "pack"
    verified: bool = True
    venue: int = 0
    modulation: str = "fm"
    bandwidth: str = "narrow"

    @classmethod
    def from_json(cls, j: dict) -> "Car":
        return cls(
            number=str(j["number"]),
            driver=str(j.get("driver", "")),
            team=str(j.get("team", "")),
            entry=str(j.get("entry", "")),
            freqs=[float(f) for f in j.get("freqs", [])],
            tone=j.get("tone", 0),
            group=str(j.get("group", "A")),
            favorite=bool(j.get("favorite", False)),
            origin=str(j.get("origin", "pack")),
            verified=bool(j.get("verified", True)),
            venue=int(j.get("venue", 0)),
            modulation=str(j.get("modulation", "fm")),
            bandwidth=str(j.get("bandwidth", "narrow")),
        )

    def to_json(self) -> dict:
        j = {"number": self.number, "freqs": [round(f, 5) for f in self.freqs]}
        if self.driver:
            j["driver"] = self.driver
        if self.team:
            j["team"] = self.team
        if self.entry:
            j["entry"] = self.entry
        if self.tone:
            j["tone"] = self.tone
        if self.group != "A":
            j["group"] = self.group
        if self.favorite:
            j["favorite"] = True
        if self.origin != "pack":
            j["origin"] = self.origin
        if not self.verified:
            j["verified"] = False
        if self.venue:
            j["venue"] = self.venue
        if self.modulation != "fm":
            j["modulation"] = self.modulation
        if self.bandwidth != "narrow":
            j["bandwidth"] = self.bandwidth
        return j


@dataclass
class Station:
    name: str
    kind: str = "other"
    freq: Optional[float] = None          # MHz (non-broadcast)
    fm: Optional[float] = None            # MHz (broadcast)
    tone: Any = 0
    digital: bool = False
    venue: int = 0

    @classmethod
    def from_json(cls, j: dict) -> "Station":
        return cls(
            name=str(j["name"]),
            kind=str(j.get("kind", "other")),
            freq=float(j["freq"]) if "freq" in j else None,
            fm=float(j["fm"]) if "fm" in j else None,
            tone=j.get("tone", 0),
            digital=bool(j.get("digital", False)),
            venue=int(j.get("venue", 0)),
        )

    def to_json(self) -> dict:
        j = {"name": self.name, "kind": self.kind}
        if self.freq is not None:
            j["freq"] = round(self.freq, 5)
        if self.fm is not None:
            j["fm"] = round(self.fm, 5)
        if self.tone:
            j["tone"] = self.tone
        if self.digital:
            j["digital"] = True
        if self.venue:
            j["venue"] = self.venue
        return j


@dataclass
class Pack:
    meta: dict = field(default_factory=dict)
    cars: list = field(default_factory=list)
    stations: list = field(default_factory=list)
    lockouts: list = field(default_factory=list)

    @classmethod
    def from_json(cls, j: dict) -> "Pack":
        return cls(
            meta=dict(j.get("meta", {})),
            cars=[Car.from_json(c) for c in j.get("cars", [])],
            stations=[Station.from_json(s) for s in j.get("stations", [])],
            lockouts=[str(n) for n in j.get("lockouts", [])],
        )

    @classmethod
    def load(cls, path: str) -> "Pack":
        with open(path) as f:
            return cls.from_json(json.load(f))

    def to_json(self) -> dict:
        return {
            "meta": self.meta,
            "cars": [c.to_json() for c in self.cars],
            "stations": [s.to_json() for s in self.stations],
            "lockouts": self.lockouts,
        }

    def save(self, path: str):
        with open(path, "w") as f:
            json.dump(self.to_json(), f, indent=2)
            f.write("\n")

    # ---- the flattened entry list (spec §5.1: alts become entries) ----

    def flattened(self) -> list[Car]:
        """Cars + ALT duplicates for freqs[1], venue-sorted (stable)."""
        out: list[Car] = []
        for c in self.cars:
            base = Car(**{**c.__dict__, "freqs": [c.freqs[0]]})
            out.append(base)
            if len(c.freqs) > 1:
                alt = Car(**{**c.__dict__, "freqs": [c.freqs[1]]})
                alt.driver = "ALT " + alt.driver if alt.driver else "ALT"
                alt.entry = ""
                out.append(alt)
        out.sort(key=lambda c: c.venue)
        return out


def _band_ok(mhz: float, is_broadcast: bool) -> Optional[str]:
    for lo, hi, note in BANDS:
        if lo <= mhz <= hi:
            if note == "broadcast" and not is_broadcast:
                return None  # broadcast freqs only via fm
            return None
    for lo, hi in ECPA_BANDS:
        if lo <= mhz <= hi:
            return f"cellular band {mhz:.3f} (ECPA §2511)"
    return f"{mhz:.4f} MHz outside the entry bands"


def validate(pack: Pack, allow_warnings: bool = True) -> list[PackWarning]:
    """§6 validation. Raises ValidationError on the first hard error;
    returns the warnings list."""
    warnings: list[PackWarning] = []

    seen_numbers: set[str] = set()
    for c in pack.flattened():
        if not NUMBER_RE.match(c.number):
            raise ValidationError(f"bad car number {c.number!r} (1-3 digits + optional letter, no leading zero)")
        if c.driver.startswith("ALT "):
            continue                        # ALT duplicates are exempt (§6)
        if c.number in seen_numbers:
            raise ValidationError(f"duplicate car number {c.number}")
        seen_numbers.add(c.number)
        if not c.freqs:
            raise ValidationError(f"car {c.number}: no frequencies")
        for f in c.freqs:
            if not (450.0 <= f <= 470.0):
                raise ValidationError(f"car {c.number}: frequency {f} outside 450-470 MHz")
        for f in c.freqs:
            idx, ct = tone_to_index(c.tone)
            if ct and idx is None:
                raise ValidationError(f"car {c.number}: tone {c.tone} not in the base tables")
            if ct == CODE_TYPE_CTCSS and idx == 0 and c.tone:
                warnings.append(PackWarning(
                    f"car {c.number}: tone 67.0 Hz collides with 'no tone' (index 0) — dropped"))
        if c.modulation == "am":
            if not any(108.0 <= f <= 137.0 for f in c.freqs):
                raise ValidationError(f"car {c.number}: AM only valid on 108-137 MHz")
        elif c.modulation != "fm":
            raise ValidationError(f"car {c.number}: unknown modulation {c.modulation!r}")
        if c.venue < 0 or c.venue >= MAX_VENUES:
            warnings.append(PackWarning(f"car {c.number}: venue {c.venue} folds into venue 1 (v0 supports 2)"))

    if len(pack.cars) > MAX_CARS:
        raise ValidationError(f"{len(pack.cars)} cars > {MAX_CARS}")
    flat = pack.flattened()
    if len(flat) > MAX_CARS:
        raise ValidationError(f"{len(flat)} entries after flattening > {MAX_CARS}")

    names = set()
    for s in pack.stations:
        if s.kind not in KINDS:
            raise ValidationError(f"station {s.name!r}: unknown kind {s.kind!r}")
        if len(s.name) > 4:
            raise ValidationError(f"station {s.name!r}: name > 4 chars")
        if s.name in names:
            raise ValidationError(f"duplicate station name {s.name!r}")
        names.add(s.name)
        if s.kind == "broadcast":
            if s.fm is None:
                raise ValidationError(f"station {s.name!r}: broadcast needs fm")
            bad = _band_ok(s.fm, True)
        else:
            if s.freq is None:
                raise ValidationError(f"station {s.name!r}: needs freq (non-broadcast)")
            bad = _band_ok(s.freq, False)
        if bad:
            raise ValidationError(f"station {s.name!r}: {bad}")
        for num in [s.freq, s.fm]:
            if num is not None and _band_ok(num, s.kind == "broadcast") is None and 88.0 <= num <= 108.0 and s.kind != "broadcast":
                pass  # broadcast-band freq on a non-broadcast station is a warning
        idx, ct = tone_to_index(s.tone)
        if ct and idx is None:
            raise ValidationError(f"station {s.name!r}: tone {s.tone} not in the base tables")
        if s.venue < 0 or s.venue >= MAX_VENUES:
            warnings.append(PackWarning(f"station {s.name!r}: venue folds into venue 1"))

    if len(pack.stations) > MAX_STATIONS:
        raise ValidationError(f"{len(pack.stations)} stations > {MAX_STATIONS}")
    if len(flat) + len([s for s in pack.stations if s.kind != "broadcast"]) > 88:
        raise ValidationError("cars + non-broadcast stations > 88 channels")

    # cross-entry duplicate frequencies (IndyCar 7.4.1.2: unique per car)
    freqs_seen: dict[float, list[str]] = {}
    for c in flat:
        for f in c.freqs:
            freqs_seen.setdefault(round(f, 5), []).append(c.number)
    for f, nums in freqs_seen.items():
        if len(nums) > 1:
            warnings.append(PackWarning(f"frequency {f} shared by {', '.join(nums)}"))

    return warnings
