# Scan 01 — packtool: tone tables + value<->index mapping
"""CTCSS/DCS tables copied from the firmware's dcs.c — the channel records
store INDICES into these. test_packtool_dcs.py parses dcs.c and asserts the
copy never drifts.

CTCSS values are deci-Hz (948 = 94.8 Hz). DCS values are decimal integers
whose OCTAL form is the code (185 -> "271").
"""

CTCSS_OPTIONS = [
    670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
    948, 974, 1000, 1035, 1072, 1109, 1148, 1188, 1230, 1273,
    1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679,
    1713, 1738, 1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995,
    2035, 2065, 2107, 2181, 2257, 2291, 2336, 2418, 2503, 2541,
]

DCS_OPTIONS = [
    0x0013, 0x0015, 0x0016, 0x0019, 0x001A, 0x001E, 0x0023, 0x0027,
    0x0029, 0x002B, 0x002C, 0x0035, 0x0039, 0x003A, 0x003B, 0x003C,
    0x004C, 0x004D, 0x004E, 0x0052, 0x0055, 0x0059, 0x005A, 0x005C,
    0x0063, 0x0065, 0x006A, 0x006D, 0x006E, 0x0072, 0x0075, 0x007A,
    0x007C, 0x0085, 0x008A, 0x0093, 0x0095, 0x0096, 0x00A3, 0x00A4,
    0x00A5, 0x00A6, 0x00A9, 0x00AA, 0x00AD, 0x00B1, 0x00B3, 0x00B5,
    0x00B6, 0x00B9, 0x00BC, 0x00C6, 0x00C9, 0x00CD, 0x00D5, 0x00D9,
    0x00DA, 0x00E3, 0x00E6, 0x00E9, 0x00EE, 0x00F4, 0x00F5, 0x00F9,
    0x0109, 0x010A, 0x010B, 0x0113, 0x0119, 0x011A, 0x0125, 0x0126,
    0x012A, 0x012C, 0x012D, 0x0132, 0x0134, 0x0135, 0x0136, 0x0143,
    0x0146, 0x014E, 0x0153, 0x0156, 0x015A, 0x0166, 0x0175, 0x0186,
    0x018A, 0x0194, 0x0197, 0x0199, 0x019A, 0x01AC, 0x01B2, 0x01B4,
    0x01C3, 0x01CA, 0x01D3, 0x01D9, 0x01DA, 0x01DC, 0x01E3, 0x01EC,
]

CODE_TYPE_OFF = 0
CODE_TYPE_CTCSS = 1
CODE_TYPE_DCS = 2


def ctcss_index(hz: float) -> int | None:
    """Map a CTCSS frequency in Hz to its CTCSS_OPTIONS index."""
    deci = round(hz * 10)
    for i, v in enumerate(CTCSS_OPTIONS):
        if v == deci:
            return i
    return None


def dcs_index(octal: str) -> int | None:
    """Map a DCS code ("271") to its DCS_OPTIONS index."""
    try:
        want = int(octal, 8)
    except ValueError:
        return None
    for i, v in enumerate(DCS_OPTIONS):
        if v == want:
            return i
    return None


def tone_to_index(tone) -> tuple[int, int] | tuple[None, None]:
    """JSON tone -> (index, code_type). tone is 0/None (none), a float (CTCSS
    Hz), or a 3-digit octal string (DCS). Returns (None, None) when unknown."""
    if tone is None or tone == 0:
        return 0, CODE_TYPE_OFF
    if isinstance(tone, str):
        idx = dcs_index(tone)
        return (idx, CODE_TYPE_DCS) if idx is not None else (None, None)
    idx = ctcss_index(float(tone))
    return (idx, CODE_TYPE_CTCSS) if idx is not None else (None, None)


def index_to_tone(index: int, code_type: int):
    """Index + code type -> JSON tone (float Hz / octal str / 0)."""
    if index == 0 or code_type == CODE_TYPE_OFF:
        return 0
    if code_type == CODE_TYPE_CTCSS:
        return CTCSS_OPTIONS[index] / 10.0
    return f"{DCS_OPTIONS[index]:03o}"
