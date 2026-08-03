# Scan 01 — packtool card: the weekend quick card must carry the pack's
# actual cars, the pencil blanks, the stations, and the rules — and never
# the ham jargon of the moulded keypad.
import pytest

from packtool.card import card_svg
from packtool.model import Car, Pack, Station


def make_pack():
    return Pack(
        meta={"series": "NASCAR", "track": "Daytona", "session": "Race",
              "season": 2026, "date": "2026-02-15", "author": "test",
              "version": 1, "venues": ["Daytona"]},
        cars=[
            Car(number="24", driver="William Byron", team="HMS", freqs=[450.8875]),
            Car(number="29A", driver="Lewis Hamilton", team="MCL", freqs=[451.1250]),
            Car(number="100", driver="Marco Andretti", team="CGR", freqs=[453.1000]),
        ],
        stations=[
            Station(name="MRN", kind="broadcast", fm=101.1),
            Station(name="CTRL", kind="control", freq=461.2),
            Station(name="WX", kind="noaa", freq=162.55),
        ],
    )


def test_card_carries_the_weekend():
    svg = card_svg(make_pack())
    assert "Daytona" in svg and "Race" in svg
    assert "William Byron" in svg and "Lewis Hamilton" in svg
    assert ">24<" in svg and ">29A<" in svg
    assert "MY DRIVER" in svg and "FAVORITES" in svg and "GROUP" in svg
    assert "THE RULES" in svg and "HOLD 5 = WEATHER" in svg
    assert "PTT = HOLD THE CAR" in svg


def test_card_station_frequencies_radio_style():
    svg = card_svg(make_pack())
    assert "CTRL   461.200" in svg          # 3 decimals for .2
    assert "WX   NOAA 162.550" in svg
    assert "MRN   FM 101.1" in svg


def test_card_overflow_is_honest_and_stays_one_page():
    pack = make_pack()
    pack.cars = [Car(number=str(i), driver=f"Driver {i}") for i in range(40)]
    pack.stations = [Station(name=f"S{i}", kind="control", freq=450.0 + i)
                     for i in range(12)]
    svg = card_svg(pack)
    assert "+14 more in the radio" in svg    # 40 - 26 cars
    assert "+4 more in the radio" in svg     # 12 - 8 stations
    assert svg.count("<svg") == 1 and svg.rstrip().endswith("</svg>")
    # the one-page invariant: every text element lands inside the sheet
    # (the rules are the last section — the worst case for vertical space)
    import re
    ys = [int(m) for m in re.findall(r'y="(\d+)"', svg)]
    assert max(ys) <= 940


def test_card_escapes_pack_strings():
    pack = make_pack()
    pack.cars[0].driver = 'A&B <Rookie> "24"'
    pack.meta["track"] = "Daytona & Friends"
    svg = card_svg(pack)
    assert "A&amp;B &lt;Rookie&gt;" in svg
    assert "Daytona &amp; Friends" in svg
    assert "&lt;" in svg and "&amp;" in svg


def test_card_kills_the_jargon():
    svg = card_svg(make_pack())
    for jargon in ("BAND", "VOX", "VFO", "A/B", "FC", "H/M/L", "NOAA"):
        assert jargon not in svg or jargon in ("NOAA",)  # WX shows as NOAA station kind
