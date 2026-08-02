# Scan 01 — packtool: the indyspeedway.com text importer
"""Parses the IMS scanner-frequency extract (race-packs/indyspeedway-ims-2026/
source.txt): the columnar CAR#/DRIVER/PRIMARY blocks and the station blocks.
Column pairing zips the shortest lists and warns on mismatch (the source is
known to drift); station labels map to the draft pack's naming (PA1.., RAD1..,
CTR1.., SAF1/SAF2, DIR, OFF, OBS). Tones: '(tone 103.5)' = CTCSS, '(tone 032)'
= DCS octal, '(digital)' = digital feed.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from ..model import Car, Pack, Station

_HEADER = re.compile(r"^## (.+)$")

# ordered most-specific first: "INDYCAR OBSERVERS" is officials, not radio
STATION_KINDS = {
    "observers": "officials",
    "race director": "officials",
    "race control": "control",
    "ims safety": "safety",
    "officials": "officials",
    "p.a": "pa",
    "indycar": "radio",
    "nascar": "control",
    "safety": "safety",
    "nbc": "media",
    "fire": "safety",
    "mrn": "radio",
}


@dataclass
class _Block:
    title: str
    lines: list = field(default_factory=list)


def _split_blocks(text: str) -> list[_Block]:
    blocks: list[_Block] = []
    cur = None
    for raw in text.splitlines():
        line = raw.rstrip()
        m = _HEADER.match(line)
        if m:
            cur = _Block(m.group(1).strip())
            blocks.append(cur)
        elif cur is not None and line.strip():
            cur.lines.append(line)
    return blocks


def _tokens(line: str) -> list[str]:
    return line.split()


def _parse_car_block(lines: list[str]) -> list[Car]:
    """The columnar CAR#/DRIVER/PRIMARY(/SECONDARY) block: each column
    list is collected positionally across its continuation lines, then the
    three are zipped (shortest wins — the source is known to drift)."""
    numbers: list[str] = []
    drivers: list[str] = []
    freqs: list[float] = []
    freqs2: list[float] = []
    cur = None
    for line in lines:
        m = re.match(r"^(CAR#?|DRIVER|PRIMARY|SECONDARY):\s*(.*)$", line)
        if m:
            cur = m.group(1)
            vals = _tokens(m.group(2))
        elif cur and line.startswith(" "):
            vals = _tokens(line)
        else:
            continue
        if cur == "CAR#":
            numbers += vals
        elif cur == "DRIVER":
            drivers += vals
        elif cur == "PRIMARY":
            freqs += [float(f) for f in vals]
        elif cur == "SECONDARY":
            freqs2 += [float(f) for f in vals]

    n = min(len(numbers), len(drivers), len(freqs))
    if len(numbers) != len(drivers) or len(numbers) != len(freqs):
        print(f"import: column mismatch — cars {len(numbers)}, drivers "
              f"{len(drivers)}, primaries {len(freqs)}; zipping to {n}")
    out: list[Car] = []
    for i in range(n):
        c = Car(number=numbers[i], driver=drivers[i], freqs=[freqs[i]])
        if i < len(freqs2):
            c.freqs.append(freqs2[i])
        out.append(c)
    return out


_TONE = re.compile(r"\(tone\s+([0-9.]+|[0-9]{3})\)")
_DIGITAL = re.compile(r"\(digital\)")


_NOISE_LABELS = {"note", "source", "extract", "page", "indianapolis", "ims",
                 "scanner", "nascar", "updated", "original"}


def _parse_stations(lines: list[str]) -> list[Station]:
    out: list[Station] = []
    family_counts: dict[str, int] = {}
    for line in lines:
        m = re.match(r"^([A-Za-z0-9 /.'()-]+?):\s+(.+)$", line)
        if not m:
            continue
        label, rest = m.group(1).strip(), m.group(2)
        label_l = label.lower()
        if label_l in _NOISE_LABELS:
            continue
        kind = next((v for k, v in STATION_KINDS.items() if k in label_l), "other")
        # each freq starts an entry; tones/digital flags attach to the
        # PRECEDING freq ("468.8250 (tone 103.5)  467.0125 (tone 032)")
        entries: list[dict] = []
        cur: dict | None = None
        pending_tone = False
        for tok in rest.split():
            if pending_tone:
                raw = tok.rstrip(")")
                if cur is not None:
                    cur["tone"] = float(raw) if "." in raw else raw.zfill(3)
                pending_tone = False
                continue
            tm = _TONE.search(tok)
            if tm:
                raw = tm.group(1)
                if cur is not None:
                    cur["tone"] = float(raw) if "." in raw else raw.zfill(3)
                tok = _TONE.sub("", tok)
            if tok == "(tone":
                pending_tone = True
                continue
            if _DIGITAL.search(tok) and cur is not None:
                cur["digital"] = True
                tok = _DIGITAL.sub("", tok)
            if re.match(r"^\d+\.\d{4}$", tok):
                cur = {"freq": float(tok), "tone": 0, "digital": False}
                entries.append(cur)
        # family-wide numbering: "NBC" then "NBC (crew)" -> NBC, NBC2, NBC3
        family = next((k for k in STATION_KINDS if k in label_l), label_l)
        for e in entries:
            family_counts[family] = family_counts.get(family, 0) + 1
            name = _station_name(label, family_counts[family] - 1,
                                 family_counts[family] if _is_numbered(family) else 1)
            out.append(Station(name=name, kind=kind, freq=e["freq"],
                               tone=e["tone"], digital=e["digital"]))
    return out


def _is_numbered(family: str) -> bool:
    return family not in ("safety", "fire", "race director", "officials", "observers")


def _station_name(label: str, index: int, count: int) -> str:
    l = label.upper().replace("'", "").replace("/", " ")
    if "P.A" in l or "PA" in l:
        return f"PA{index + 1}"
    if "NBC" in l:
        return f"NBC{index + 1}" if count > 1 else "NBC"
    if "RADIO" in l:
        return f"RAD{index + 1}"
    if "CONTROL" in l:
        return f"CTR{index + 1}"
    if "SAFETY 2" in l:
        return "SAF2"
    if "SAFETY" in l or "FIRE" in l:
        return "SAF1" if "SAFETY" in l else "FIRE"
    if "DIRECTOR" in l:
        return "DIR"
    if "OFFICIALS" in l:
        return "OFF"
    if "OBSERVERS" in l:
        return "OBS"
    if "NASCAR" in l:
        return f"NAS{index + 1}"
    if "MRN" in l:
        return f"MRN{index + 1}" if count > 1 else "MRN"
    return label[:4].upper()


def import_text(text: str, series: str = "IndyCar", track: str = "IMS",
                session: str = "Indy 500", season: int = 2026,
                date: str = "2026-03-05", author: str = "imported from indyspeedway.com") -> Pack:
    blocks = _split_blocks(text)
    cars: list[Car] = []
    stations: list[Station] = []

    for b in blocks:
        title = b.title.lower()
        if "indyc" in title and "nxt" not in title and "station" not in title:
            cars = _parse_car_block(b.lines)
        elif "station" in title:
            stations = _parse_stations(b.lines)
        elif "nascar ims channels" in title:
            stations += _parse_stations(b.lines)

    meta = {"series": series, "track": track, "session": session,
            "season": season, "date": date, "author": author, "version": 1}
    return Pack(meta=meta, cars=cars, stations=stations, lockouts=[])
