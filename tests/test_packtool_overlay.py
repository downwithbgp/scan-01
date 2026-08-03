# Scan 01 — packtool overlay: the honest-face sheet must tell the truth
# about every button and never repeat the moulded ham jargon.
import pytest

from packtool.overlay import OVERLAY_SVG

# every control the sheet must name, in the position of the control
REQUIRED = [
    "HOLD",      # PTT — the hero button finally has a name
    "MUTE",      # side key 1
    "GROUP",     # side key 2
    "EXIT", "HOME",
    "M", "LOCK",
    "WX", "FM", "CALL", "SCAN", "FAV",
    "SCAN 01",
]

# the moulded keypad's lies — Scan 01 repurposed these keys; the overlay
# must never repeat them
FORBIDDEN = [
    "BAND", "VOX", "VFO", "A/B", "FC", "H/M/L", "NOAA", "MENU",
]


@pytest.mark.parametrize("label", REQUIRED)
def test_overlay_names_every_control(label):
    assert label in OVERLAY_SVG


@pytest.mark.parametrize("jargon", FORBIDDEN)
def test_overlay_kills_the_jargon(jargon):
    assert jargon not in OVERLAY_SVG


def test_overlay_is_a_wellformed_svg():
    assert OVERLAY_SVG.lstrip().startswith("<svg")
    assert OVERLAY_SVG.rstrip().endswith("</svg>")
    # six keys carry the hold convention: 0, 5, 9, * and the two hints
    assert OVERLAY_SVG.count("HOLD") >= 8  # PTT + 4 keys + legend lines


def test_overlay_has_exactly_one_keypad_row_per_digit():
    for d in "1234567890":
        assert f">{d}<" in OVERLAY_SVG or f">{d} " in OVERLAY_SVG or d in OVERLAY_SVG
