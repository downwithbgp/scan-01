# Scan 01 — packtool: the EEPROM binary format
"""Byte-exact mirror of the firmware's pack layer (settings_pack.c): the
channel records (§4.1), names+team (§4.2), and the pack table (§4.3).
tests/test_packtool_golden.py proves the round-trip against a fixture
EEPROM image produced by the C firmware code itself.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional

from .dcs import CODE_TYPE_CTCSS, CODE_TYPE_DCS, CODE_TYPE_OFF, index_to_tone
from .model import Car, Pack, Station

PACK_TABLE_BASE = 0x1BD0
PACK_HEADER_SIZE = 0x40
PACK_CRC_LEN = 0x3A
PACK_CRC_OFF = 0x37
PACK_FLAGS_OFF = 0x39
PACK_VERSION = 0x01
PACK_MAGIC = b"SC01"

STATION_CHANNEL_BASE = 64
CAR_META_BASE = PACK_TABLE_BASE + PACK_HEADER_SIZE        # 0x1C10
STATION_META_BASE = CAR_META_BASE + 64 * 4                # 0x1D10, fixed
MAX_CARS = 64
MAX_STATIONS = 24

SEAL_FLAG = 0x01
LESSONS_MASK = 0x7E          # bits 1-6 of the header flags: 1 = lesson learned
LESSONS_ALL = 0x7E           # packtool default: a real pack arrives quiet
                             # (meta["lessons"] = 0 → the radio teaches)
ORIGIN_PACK, ORIGIN_CAPTURED, ORIGIN_MANUAL = 0, 1, 2


@dataclass
class Region:
    addr: int
    data: bytes


# ---- CRC (firmware PACK_Crc16Ccitt: CCITT-FALSE, init 0xFFFF) ----

def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _trunc(s: str, n: int) -> bytes:
    b = s.encode("utf-8", "replace")[:n]
    return b + b" " * (n - len(b))


def _trunc_nul(s: str, n: int) -> bytes:
    b = s.encode("utf-8", "replace")[:n]
    return b + b"\x00" * (n - len(b))     # the firmware's char arrays are NUL-padded


# ---- channel records (§4.1) ----

def channel_record(freq_mhz: float, tone_index: int, code_type: int,
                   narrow: bool = True, am: bool = False) -> bytes:
    hz = int(round(freq_mhz * 1_000_000))
    rec = bytearray(16)
    rec[0:4] = hz.to_bytes(4, "little")
    rec[8] = rec[9] = tone_index
    rec[10] = (code_type << 4) | code_type          # 0x11 CTCSS, 0x22 DCS, 0x00 none
    rec[11] = 0x10 if am else 0x00                  # AM << 4, offset off
    rec[12] = 0x40 | 0x04 | (0x02 if narrow else 0)  # TX_LOCK | power low | BW
    return bytes(rec)


def parse_channel_record(rec: bytes) -> dict:
    hz = int.from_bytes(rec[0:4], "little")
    return {
        "freq_mhz": hz / 1_000_000,
        "tone_index": rec[8],
        "code_type": rec[10] & 0x0F,
        "am": bool(rec[11] & 0x10),
        "narrow": bool(rec[12] & 0x02),
    }


def names_record(name: str, team: str) -> bytes:
    return _trunc(name, 10) + _trunc(team, 6)


def parse_names_record(rec: bytes) -> tuple[str, str]:
    return rec[0:10].rstrip(b" \x00").decode("utf-8", "replace"), \
        rec[10:16].rstrip(b" \x00").decode("utf-8", "replace")


# ---- CarMeta (4 B) ----

def car_meta(car: Car) -> bytes:
    num = _trunc_nul(car.number, 3)          # NUL-padded like the firmware's char array
    origin = {"captured": ORIGIN_CAPTURED, "manual": ORIGIN_MANUAL}.get(car.origin, ORIGIN_PACK)
    group = {"A": 0, "B": 1, "C": 2, "ALL": 3}.get(car.group, 0)
    flags = (group & 0x03) | ((1 << 2) if car.favorite else 0) \
        | ((origin & 0x03) << 3) | ((1 << 5) if car.verified else 0) \
        | ((car.venue & 0x01) << 6)
    return num + bytes([flags])


def parse_car_meta(meta: bytes) -> dict:
    flags = meta[3]
    return {
        "number": meta[0:3].rstrip(b" \x00").decode("utf-8", "replace"),
        "group": ("A", "B", "C", "ALL")[flags & 0x03],
        "favorite": bool(flags & (1 << 2)),
        "origin": ("pack", "captured", "manual")[(flags >> 3) & 0x03],
        "verified": bool(flags & (1 << 5)),
        "venue": (flags >> 6) & 0x01,
    }


# ---- StationMeta (10 B) ----

def station_meta(st: Station) -> bytes:
    meta = bytearray(10)
    meta[0:4] = _trunc_nul(st.name, 4)          # NUL-padded like the firmware's char array
    hz = int(round((st.freq or st.fm or 0) * 1_000_000))   # fm for broadcast
    meta[4:8] = hz.to_bytes(4, "little")
    idx, ct = _tone(st.tone)
    meta[8] = (0x80 if ct == CODE_TYPE_DCS else 0) | (idx & 0x7F)
    kind_idx = ("broadcast", "control", "pa", "officials", "safety",
                "radio", "media", "other").index(st.kind if st.kind != "noaa" else "other")
    meta[9] = (kind_idx & 0x07) | ((1 << 3) if st.digital else 0) | ((st.venue & 0x01) << 4)
    return bytes(meta)


def parse_station_meta(meta: bytes) -> dict:
    hz = int.from_bytes(meta[4:8], "little")
    kinds = ("broadcast", "control", "pa", "officials", "safety", "radio", "media", "other")
    flags = meta[9]
    return {
        "name": meta[0:4].rstrip(b" \x00").decode("utf-8", "replace"),
        "freq_mhz": hz / 1_000_000,
        "tone_index": meta[8] & 0x7F,
        "tone_is_dcs": bool(meta[8] & 0x80),
        "kind": ("noaa" if (kinds[flags & 0x07] == "other"
                       and 162.4 <= hz / 1_000_000 <= 162.55)      # band heuristic
                 else kinds[flags & 0x07]),
        "digital": bool(flags & (1 << 3)),
        "venue": (flags >> 4) & 0x01,
    }


def _tone(tone):
    """JSON tone -> (index, code_type); errors surface as (0, OFF)."""
    from .dcs import tone_to_index
    idx, ct = tone_to_index(tone)
    return (idx if idx is not None else 0), (ct if ct is not None else CODE_TYPE_OFF)


# ---- header (64 B at 0x1BD0) ----

def header(pack: Pack, car_count: int, station_count: int) -> bytes:
    meta = pack.meta
    venues = meta.get("venues") or [meta.get("track", "")]
    venue2 = venues[1] if len(venues) > 1 else ""
    h = bytearray(PACK_HEADER_SIZE)
    h[0:4] = PACK_MAGIC
    h[0x04] = PACK_VERSION
    h[0x05:0x05 + 8] = _trunc_nul(str(meta.get("series", "")), 8)
    h[0x0D:0x0D + 12] = _trunc_nul(str(meta.get("track", "")), 12)
    h[0x19:0x19 + 8] = _trunc_nul(str(meta.get("session", "")), 8)
    h[0x21] = car_count
    h[0x22] = station_count
    bitmap = bytearray(8)
    for n in pack.lockouts:
        for i, car in enumerate(pack.flattened()):
            if car.number == n:
                bitmap[i // 8] |= 1 << (i % 8)
                break
    h[0x23:0x23 + 8] = bytes(bitmap)
    h[0x2B:0x2B + 4] = _trunc_nul(str(meta.get("driver", "")), 4)
    h[0x2F:0x2F + 8] = _trunc_nul(str(venue2), 8)
    lessons = (meta.get("lessons", LESSONS_ALL) & LESSONS_MASK) if meta else LESSONS_ALL
    h[PACK_FLAGS_OFF] = (SEAL_FLAG if meta.get("sealed") else 0) | lessons
    crc = crc16_ccitt(bytes(h[:PACK_CRC_LEN]))   # CRC field reads as zero
    h[PACK_CRC_OFF] = crc & 0xFF
    h[PACK_CRC_OFF + 1] = crc >> 8
    return bytes(h)


def header_valid(h: bytes) -> bool:
    if h[0:4] != PACK_MAGIC or h[0x04] != PACK_VERSION:
        return False
    stored = h[PACK_CRC_OFF] | (h[PACK_CRC_OFF + 1] << 8)
    body = bytearray(h[:PACK_CRC_LEN])
    body[PACK_CRC_OFF] = 0
    body[PACK_CRC_OFF + 1] = 0
    return crc16_ccitt(bytes(body)) == stored


def parse_header(h: bytes) -> dict:
    return {
        "series": h[0x05:0x0D].rstrip(b" \x00").decode("utf-8", "replace"),
        "track": h[0x0D:0x19].rstrip(b" \x00").decode("utf-8", "replace"),
        "session": h[0x19:0x21].rstrip(b" \x00").decode("utf-8", "replace"),
        "car_count": h[0x21],
        "station_count": h[0x22],
        "driver": h[0x2B:0x2F].rstrip(b" \x00").decode("utf-8", "replace"),
        "venue2": h[0x2F:0x37].rstrip(b" \x00").decode("utf-8", "replace"),
        "sealed": bool(h[PACK_FLAGS_OFF] & SEAL_FLAG),
        "lessons": h[PACK_FLAGS_OFF] & LESSONS_MASK,
    }


# ---- build (pack -> regions) ----

def build(pack: Pack) -> list[Region]:
    """The region list the flasher applies: channel records, names, pack
    table. Channels beyond the used span in 0..79 are zeroed (§5.1.5)."""
    flats = pack.flattened()
    all_stations = list(pack.stations)
    ch_stations = [s for s in all_stations if s.kind != "broadcast"]
    n, m = len(flats), len(all_stations)
    if n > MAX_CARS or m > MAX_STATIONS or n + len(ch_stations) > 88:
        raise ValueError(f"capacity: {n} cars + {len(ch_stations)} stations exceeds the layout")

    regions: list[Region] = []

    # channel records: cars at 0..n-1, stations at 64..64+m-1 (broadcast
    # stations get no channel record — their fm lives in the meta only)
    records: dict[int, bytes] = {}
    for i, car in enumerate(flats):
        idx, ct = _tone(car.tone)
        records[i] = channel_record(car.freqs[0], idx, ct, car.bandwidth == "narrow",
                                    car.modulation == "am")
    for i, st in enumerate(ch_stations):
        idx, ct = _tone(st.tone)
        # mirror the firmware's station writer: wide + AM on the airband
        # (settings_pack.c station install passes narrow=false, am=band)
        records[STATION_CHANNEL_BASE + i] = channel_record(
            st.freq, idx, ct, narrow=False, am=108.0 <= (st.freq or 0) <= 137.0)
    for ch in range(80):                     # zero the stale tail of the car block
        records.setdefault(ch, bytes(16))
    for ch in sorted(records):
        regions.append(Region(ch * 16, records[ch]))

    # names + team
    for i, car in enumerate(flats):
        regions.append(Region(0x0F50 + i * 16, names_record(car.entry or car.driver, car.team)))
    for i, st in enumerate(ch_stations):
        regions.append(Region(0x0F50 + (STATION_CHANNEL_BASE + i) * 16,
                              names_record(st.name, "")))

    # pack table: the car-meta region spans the full 64-slot capacity
    # (a capture that grows the count must never shift the station metas —
    # the firmware's fixed station_meta_base), then the station metas
    table = bytearray(header(pack, n, m))
    for i, car in enumerate(flats):
        table.extend(car_meta(car))
    table.extend(bytes(4 * (MAX_CARS - n)))          # pad to the 64-slot capacity
    for st in all_stations:
        table.extend(station_meta(st))
    regions.append(Region(PACK_TABLE_BASE, bytes(table)))

    return regions


# ---- parse (EEPROM image -> pack) ----

def parse(image: bytes, addr: int = 0) -> Pack:
    def read(off: int, size: int) -> bytes:
        return image[addr + off:addr + off + size]

    h = read(PACK_TABLE_BASE, PACK_HEADER_SIZE)
    if not header_valid(h):
        raise ValueError("radio has no valid pack (magic/CRC)")
    hi = parse_header(h)

    flats: list[Car] = []
    for i in range(hi["car_count"]):
        meta = parse_car_meta(read(CAR_META_BASE + i * 4, 4))
        rec = parse_channel_record(read(i * 16, 16))
        name, team = parse_names_record(read(0x0F50 + i * 16, 16))
        car = Car(
            number=meta["number"], driver=name, team=team, entry=name,
            freqs=[rec["freq_mhz"]],
            tone=index_to_tone(rec["tone_index"], rec["code_type"]),
            group=meta["group"], favorite=meta["favorite"], origin=meta["origin"],
            verified=meta["verified"], venue=meta["venue"],
            modulation="am" if rec["am"] else "fm",
            bandwidth="narrow" if rec["narrow"] else "wide",
        )
        flats.append(car)

    # re-fold ALT entries (best-effort: "ALT " prefix on the driver)
    cars: list[Car] = []
    for car in flats:
        if car.driver.startswith("ALT ") and cars and cars[-1].number == car.number:
            cars[-1].freqs.append(car.freqs[0])
        else:
            cars.append(car)

    stations: list[Station] = []
    for i in range(hi["station_count"]):
        sm = parse_station_meta(read(STATION_META_BASE + i * 10, 10))
        st = Station(
            name=sm["name"], kind=sm["kind"],
            freq=sm["freq_mhz"] if sm["kind"] != "broadcast" else None,
            fm=sm["freq_mhz"] if sm["kind"] == "broadcast" else None,
            tone=index_to_tone(sm["tone_index"], CODE_TYPE_DCS if sm["tone_is_dcs"] else CODE_TYPE_CTCSS),
            digital=sm["digital"], venue=sm["venue"],
        )
        stations.append(st)

    lockouts = [car.number for car in flats if _locked(read(PACK_TABLE_BASE, 0x40), flats, car)]

    meta = {
        "series": hi["series"], "track": hi["track"], "session": hi["session"],
        "venues": [hi["track"], hi["venue2"]] if hi["venue2"] else [hi["track"]],
        "driver": hi["driver"], "sealed": hi["sealed"],
        "lessons": hi["lessons"],
    }
    return Pack(meta=meta, cars=cars, stations=stations, lockouts=lockouts)


def _locked(h: bytes, flats: list[Car], car: Car) -> bool:
    for i, c in enumerate(flats):
        if c is car:
            return bool(h[0x23 + i // 8] & (1 << (i % 8)))
    return False
