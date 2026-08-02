# Scan 01 — packtool: tone tables must match the firmware's dcs.c
import re
from pathlib import Path

from packtool import dcs

SRC = Path(__file__).parent.parent / "dcs.c"


def _table(name: str) -> list[int]:
    text = SRC.read_text()
    m = re.search(rf"const uint16_t {name}\[\d+\] = \{{(.*?)\}};", text, re.S)
    assert m, name
    return [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))]


def test_tables_match_dcs_c():
    assert dcs.CTCSS_OPTIONS == _table("CTCSS_Options")
    assert dcs.DCS_OPTIONS == _table("DCS_Options")


def test_ctcss_mapping():
    assert dcs.ctcss_index(94.8) == 10          # CTCSS_Options[10] == 948
    assert dcs.ctcss_index(67.0) == 0
    assert dcs.ctcss_index(254.1) == 49
    assert dcs.ctcss_index(101.0) is None


def test_dcs_mapping():
    assert dcs.dcs_index("271") is not None
    assert dcs.DCS_OPTIONS[dcs.dcs_index("271")] == 0x00B9
    assert dcs.dcs_index("999") is None
    assert dcs.dcs_index("xyz") is None


def test_tone_roundtrip():
    for i, v in enumerate(dcs.CTCSS_OPTIONS):
        if i == 0:
            continue                        # index 0 = 'no tone' in the record
        tone = v / 10.0
        idx, ct = dcs.tone_to_index(tone)
        assert (idx, ct) == (i, dcs.CODE_TYPE_CTCSS)
        assert dcs.index_to_tone(idx, ct) == tone
    for i, v in enumerate(dcs.DCS_OPTIONS):
        if i == 0:
            continue                        # DCS index 0 (023) = record code 0 = none
        tone = f"{v:03o}"
        idx, ct = dcs.tone_to_index(tone)
        assert (idx, ct) == (i, dcs.CODE_TYPE_DCS)
        assert dcs.index_to_tone(idx, ct) == tone
    assert dcs.tone_to_index(0) == (0, dcs.CODE_TYPE_OFF)
    assert dcs.tone_to_index(None) == (0, dcs.CODE_TYPE_OFF)
