# Scan 01 — the weekend pack card
#
# A printable one-page sheet for the pack: the cars of the weekend as
# number → driver (the identity mapping a race fan actually needs), pencil
# blanks — MY DRIVER / FAVORITES / GROUP, the stations with their
# frequencies, and the seven rules. The writable-strip tradition (1930s car
# radios shipped a paper strip under the preset buttons; the user wrote the
# station names themselves) recovered for the data product: the act of
# writing the legend is the act of learning it. The manual is part of the
# pack, not of a search engine.
#
#     packtool card pack.json -o card.svg
#
# One page, always: the card is a quick reference, the radio holds the
# full list — beyond the caps a "+N more in the radio" note keeps the page
# honest instead of growing it.

from __future__ import annotations

from .model import Pack

MAX_CARS = 26        # two columns × 13 rows
MAX_STATIONS = 8

# vertical budget (one page always: the rules must land ≤ 940 even at the
# caps + overflow notes — verified against the worst case in the tests)
CAR_ROW = 22
CAR_NOTE = 24
MINE_GAP = 34
BLANK_ROW = 32
ST_GAP = 36
ST_ROW = 20
ST_NOTE = 20
RULES_GAP = 44


def _freq(mhz: float | None, pad: bool = True) -> str:
    """Radio-style: 461.2 -> 461.200, 450.8875 -> 450.8875, FM 101.1."""
    if mhz is None:
        return ""
    s = f"{mhz:.4f}".rstrip("0").rstrip(".")    # 461.2000 -> 461.2; 101.0000 -> 101
    if pad and "." in s:
        dec = len(s) - s.index(".") - 1
        if dec < 3:
            s += "0" * (3 - dec)                # 461.2 -> 461.200
    return s


def _station_line(st) -> str:
    if st.kind == "broadcast":
        return f"{st.name}   FM {_freq(st.fm, pad=False)}"
    if st.kind == "noaa":
        return f"{st.name}   NOAA {_freq(st.freq)}"
    return f"{st.name}   {_freq(st.freq)}"


def _esc(text: str) -> str:
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def card_svg(pack: Pack) -> str:
    meta = pack.meta
    track = _esc(str(meta.get("track", "")))
    session = _esc(str(meta.get("session", "")))
    series = _esc(str(meta.get("series", "")))

    # ---- cars: two columns of number → driver, capped ----
    shown = pack.cars[:MAX_CARS]
    car_rows: list[str] = []
    for i, car in enumerate(shown):
        if i % 2 == 0 and i > 0:
            car_rows.append("")
        col = i % 2
        x = 90 if col == 0 else 400
        driver = _esc((car.driver or "")[:22])
        car_rows.append(
            f'<text x="{x}" y="{178 + CAR_ROW * (i // 2)}" font-size="18">'
            f'<tspan font-weight="bold">{_esc(car.number)}</tspan>'
            f'  {driver}</text>'
        )
    cars_end = 178 + CAR_ROW * ((len(shown) + 1) // 2)
    if len(pack.cars) > MAX_CARS:
        car_rows.append(
            f'<text x="90" y="{cars_end + 4}" font-size="16" fill="#666">'
            f'+{len(pack.cars) - MAX_CARS} more in the radio</text>')
        cars_end += CAR_NOTE
    else:
        cars_end += 6

    # ---- MINE: the pencil blanks ----
    mine_y = cars_end + MINE_GAP
    blanks = "\n".join(
        f'<text x="90" y="{mine_y + BLANK_ROW * i}" font-size="19" font-weight="bold">{label}:</text>'
        f'<line x1="240" y1="{mine_y + BLANK_ROW * i}" x2="540" y2="{mine_y + BLANK_ROW * i}" '
        f'stroke="#333" stroke-width="2"/>'
        for i, label in enumerate(["MY DRIVER", "FAVORITES", "GROUP"])
    )
    mine_end = mine_y + BLANK_ROW * 3

    # ---- stations ----
    st_y = mine_end + ST_GAP
    station_lines = "\n".join(
        f'<text x="90" y="{st_y + ST_ROW * i}" font-size="17">{_esc(_station_line(s))}</text>'
        for i, s in enumerate(pack.stations[:MAX_STATIONS])
    )
    st_end = st_y + ST_ROW * len(pack.stations[:MAX_STATIONS])
    if len(pack.stations) > MAX_STATIONS:
        station_lines += (
            f'\n<text x="90" y="{st_end + 2}" font-size="15" fill="#666">'
            f'+{len(pack.stations) - MAX_STATIONS} more in the radio</text>')
        st_end += ST_NOTE

    # ---- the rules: the closing legend (one page: r3 ≤ 940) ----
    r1 = st_end + RULES_GAP
    r2, r3 = r1 + 26, r1 + 52

    title = f"SCAN 01 — {track} · {session}" if track else "SCAN 01"

    return f"""\
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 960"
     font-family="'DejaVu Sans', Helvetica, Arial, sans-serif">
  <title>{title}</title>
  <rect x="20" y="20" width="600" height="920" rx="16" fill="#fff"
        stroke="#333" stroke-width="3"/>
  <text x="320" y="70" text-anchor="middle" font-size="34" font-weight="bold">SCAN 01</text>
  <text x="320" y="104" text-anchor="middle" font-size="22">
    {_esc(track)} · {_esc(session)}{" · " + series if series else ""}</text>

  <text x="90" y="140" font-size="19" font-weight="bold">THE CARS</text>
  <line x1="90" y1="152" x2="550" y2="152" stroke="#999" stroke-width="2"/>
  {chr(10).join(car_rows)}

  <text x="90" y="{mine_y - 20}" font-size="19" font-weight="bold">MINE — write it, it's yours</text>
  {blanks}

  <text x="90" y="{st_y - 20}" font-size="19" font-weight="bold">STATIONS</text>
  <line x1="90" y1="{st_y - 8}" x2="550" y2="{st_y - 8}" stroke="#999" stroke-width="2"/>
  {station_lines}

  <text x="90" y="{r1 - 24}" font-size="19" font-weight="bold">THE RULES</text>
  <text x="90" y="{r1}" font-size="17">HOLD = press and keep holding · HOLD 5 = WEATHER · HOLD 0 = FM</text>
  <text x="90" y="{r2}" font-size="17">HOLD 9 = YOUR DRIVER · HOLD * = LOCK OUT · PTT = HOLD THE CAR</text>
  <text x="90" y="{r3}" font-size="17">LONG-EXIT = HOME · TYPE A NUMBER TO JUMP TO THAT CAR</text>
</svg>
"""
