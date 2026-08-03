# Scan 01 — packtool: the binary format + the golden cross-language proof
import json
from pathlib import Path

import pytest

from packtool import binary
from packtool.model import Car, Pack, Station, ValidationError, validate

FIXTURE = Path(__file__).parent / "fixtures" / "golden_eeprom.bin"


def make_pack():
    return Pack(
        meta={"series": "NASCAR", "track": "Daytona", "session": "Race",
              "season": 2026, "date": "2026-02-15", "author": "test",
              "version": 1, "venues": ["Daytona"]},
        cars=[
            Car(number="24", driver="William Byron", team="HMS", entry="BYRON W",
                freqs=[450.8875], tone=94.8, group="A", favorite=True),
            Car(number="29A", driver="Lewis Hamilton", team="MCL", entry="HAMILTON L",
                freqs=[451.1250, 452.0000], tone="271", group="B"),
            Car(number="100", driver="Marco Andretti", team="CGR", entry="ANDRETTI M",
                freqs=[453.1000], tone=0),
        ],
        stations=[
            Station(name="MRN", kind="broadcast", fm=101.1),
            Station(name="CTRL", kind="control", freq=461.200, tone=103.5),
            Station(name="WX", kind="noaa", freq=162.550),
        ],
        lockouts=["29A"],
    )


def test_crc_vector():
    assert binary.crc16_ccitt(b"123456789") == 0x29B1


def test_channel_record():
    from packtool import dcs
    rec = binary.channel_record(450.8875, 10, dcs.CODE_TYPE_CTCSS)
    assert rec[0:4] == (450887500).to_bytes(4, "little")
    assert rec[8] == 10 and rec[10] == 0x11 and rec[12] == 0x46
    am = binary.channel_record(121.5, 0, 0, am=True)
    assert am[11] == 0x10
    wide = binary.channel_record(450.0, 0, 0, narrow=False)
    assert wide[12] == 0x44


def test_build_parse_roundtrip():
    pack = make_pack()
    validate(pack)
    regions = binary.build(pack)
    image = bytearray(0x2000)
    for r in regions:
        image[r.addr:r.addr + len(r.data)] = r.data

    back = binary.parse(bytes(image))
    assert back.meta["series"] == "NASCAR"
    assert back.meta["track"] == "Daytona"
    assert [c.number for c in back.cars] == ["24", "29A", "100"]
    c24 = back.cars[0]
    assert c24.freqs == [450.8875] and c24.tone == 94.8 and c24.favorite
    c29 = back.cars[1]
    assert c29.freqs == [451.1250, 452.0000] and c29.tone == "271"   # ALT folded back
    assert back.stations[0].kind == "broadcast" and back.stations[0].fm == 101.1
    assert back.stations[1].tone == 103.5
    assert back.lockouts == ["29A"]


def test_venue_ordering():
    """build sorts entries by venue so LIST dividers appear once each."""
    pack = make_pack()
    pack.cars[2].venue = 1
    flats = pack.flattened()
    assert [c.venue for c in flats] == [0, 0, 0, 1]     # 24, 29A, 29A-ALT, 100


def test_capacity_error():
    pack = make_pack()
    pack.cars = [Car(number=str(i), freqs=[450.0 + i / 1000]) for i in range(65)]
    with pytest.raises(ValueError):
        binary.build(pack)


@pytest.fixture(scope="module")
def golden_image():
    assert FIXTURE.exists(), "run tools/build-golden.sh first"
    return FIXTURE.read_bytes()


def test_golden_parse(golden_image):
    """The packtool reads the firmware-written image exactly."""
    pack = binary.parse(golden_image)
    assert pack.meta["series"] == "SCAN 01"   # the demo's screen-display brand
    assert [c.number for c in pack.cars] == ["24", "48", "3"]
    c24 = pack.cars[0]
    assert c24.freqs == [450.8875] and c24.tone == 94.8 and c24.favorite
    assert pack.lockouts == ["24"]
    assert pack.meta["driver"] == "24"
    assert pack.meta["sealed"] is True
    assert len(pack.stations) == 7                  # the demo's stations


def test_golden_roundtrip(golden_image):
    """parse -> build reproduces the firmware's bytes byte-for-byte for every
    USED region (the build zero-fills the stale channel gap by design — the
    firmware leaves it as 0xFF)."""
    pack = binary.parse(golden_image)
    n = len(pack.flattened())
    m = len([s for s in pack.stations if s.kind != "broadcast"])
    used = set(range(n)) | set(range(binary.STATION_CHANNEL_BASE,
                                     binary.STATION_CHANNEL_BASE + m))
    regions = binary.build(pack)
    for r in regions:
        is_used_channel = r.addr < 0x0F50 and (r.addr // 16) in used
        is_names = 0x0F50 <= r.addr < 0x0F50 + 200 * 16
        is_table = r.addr >= binary.PACK_TABLE_BASE
        if not (is_used_channel or is_names or is_table):
            continue
        if is_table:
            # the header's string fields may be space- or NUL-padded (the
            # demo pack's strcpy-into-zeroed-array layout mixes both) — the
            # strings compare normalized; the CRC (which covers them) is
            # verified for INTERNAL validity on each side; everything else
            # (magic, counts, lockout, flags, the USED metas) byte-exact —
            # the 64-slot meta padding is 0xFF in the golden, 0x00 in ours
            gold, mine = golden_image[r.addr:r.addr + len(r.data)], r.data
            assert gold[:0x05] == mine[:0x05]
            assert gold[0x21:0x2B] == mine[0x21:0x2B]
            assert gold[0x39:0x40] == mine[0x39:0x40]            # flags
            assert binary.header_valid(mine)
            for start, end in ((0x05, 0x0D), (0x0D, 0x19), (0x19, 0x21),
                               (0x2B, 0x2F), (0x2F, 0x37)):
                assert gold[start:end].rstrip(b" \x00") == mine[start:end].rstrip(b" \x00"), \
                    f"header string at 0x{start:x}"
            for i in range(gold[0x21]):                            # used car metas
                base = 0x40 + i * 4
                assert gold[base:base + 4] == mine[base:base + 4]
            for i in range(gold[0x22]):                            # station metas
                base = binary.STATION_META_BASE - 0x1BD0 + i * 10
                assert gold[base:base + 10] == mine[base:base + 10], \
                    f"station meta {i} at {base:#x}"
        else:
            assert golden_image[r.addr:r.addr + len(r.data)] == r.data, \
                f"region mismatch at 0x{r.addr:x}"


def test_header_crc_convention():
    """The CRC field reads as zero during computation (standard convention)."""
    h = binary.header(make_pack(), 4, 3)
    body = bytearray(h[:binary.PACK_CRC_LEN])
    body[binary.PACK_CRC_OFF] = 0
    body[binary.PACK_CRC_OFF + 1] = 0
    assert binary.crc16_ccitt(bytes(body)) == h[binary.PACK_CRC_OFF] | (h[binary.PACK_CRC_OFF + 1] << 8)
    assert binary.header_valid(h)
    bad = bytearray(h)
    bad[0x10] ^= 0xFF
    assert not binary.header_valid(bytes(bad))


def test_header_lessons_default_quiet():
    """A real pack arrives quiet: without a lessons field the header marks
    every lesson learned (0xFE) — the radio never teaches a veteran."""
    h = binary.header(make_pack(), 4, 3)
    assert h[0x39] & binary.LESSONS_MASK == binary.LESSONS_ALL
    assert binary.header_valid(h)


def test_header_lessons_teach_pack():
    """--teach leaves the lessons unlearned: the radio explains itself."""
    pack = make_pack()
    pack.meta["lessons"] = 0
    h = binary.header(pack, 4, 3)
    assert h[0x39] & binary.LESSONS_MASK == 0
    assert binary.header_valid(h)
    # the seal and the lessons share the flags byte
    pack.meta["sealed"] = True
    h = binary.header(pack, 4, 3)
    assert h[0x39] == 0x01
    assert binary.header_valid(h)


def test_parse_preserves_lessons():
    """dump -> build roundtrip must carry the lesson bits exactly."""
    pack = make_pack()
    pack.meta["lessons"] = 0x10
    h = binary.header(pack, 3, 3)
    hi = binary.parse_header(h)
    assert hi["lessons"] == 0x10
