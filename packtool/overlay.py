# Scan 01 — the honest-face overlay sheet
#
# A printable SVG of the UV-K5 front that tells the truth about the buttons:
# the moulded keypad prints ham jargon (1 BAND, 2 A/B, 3 VFO/MR, 4 FC,
# 6 H/M/L, 7 VOX, 8 R) that Scan 01 ignores — the overlay replaces every
# label with what the firmware actually does. Plain digits, the four action
# keys (0 FM, 5 WX, 9 CALL, * SCAN) tinted and tagged with the hold
# convention, PTT renamed HOLD (the hero), side keys MUTE/GROUP, and the
# six rules as the bottom legend. Print at any scale; cut to the keypad or
# keep the whole sheet as the quick card.
#
# The dial-legend tradition, recovered: the legend lives on the device, in
# the position of the thing it controls (docs/design/hci-vision.md §3/§4).

OVERLAY_SVG = """\
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 980"
     font-family="'DejaVu Sans', Helvetica, Arial, sans-serif">
  <title>Scan 01 — honest-face overlay</title>
  <!-- radio body -->
  <rect x="60" y="40" width="520" height="900" rx="44" fill="#f7f7f5"
        stroke="#333" stroke-width="4"/>
  <!-- screen -->
  <rect x="120" y="84" width="400" height="196" rx="14" fill="#1c1c1c"/>
  <text x="320" y="168" text-anchor="middle" fill="#59d95c" font-size="44"
        font-family="monospace" font-weight="bold">SCAN 01</text>
  <text x="320" y="222" text-anchor="middle" fill="#888" font-size="20">RACE SCANNER</text>

  <!-- PTT (left side) — the hero button finally has a name -->
  <rect x="26" y="470" width="30" height="240" rx="12" fill="#f8d7da"
        stroke="#a33" stroke-width="3"/>
  <text x="41" y="600" text-anchor="middle" fill="#7a1f1f" font-size="24"
        font-weight="bold" transform="rotate(90 41 600)">HOLD</text>

  <!-- side keys (right side) -->
  <rect x="584" y="470" width="30" height="96" rx="12" fill="#d9ead3"
        stroke="#385" stroke-width="3"/>
  <text x="599" y="528" text-anchor="middle" fill="#274d3a" font-size="17"
        font-weight="bold" transform="rotate(90 599 528)">MUTE</text>
  <rect x="584" y="596" width="30" height="96" rx="12" fill="#d9ead3"
        stroke="#385" stroke-width="3"/>
  <text x="599" y="654" text-anchor="middle" fill="#274d3a" font-size="17"
        font-weight="bold" transform="rotate(90 599 654)">GROUP</text>

  <!-- EXIT / M row -->
  <rect x="124" y="336" width="176" height="76" rx="10" fill="#eee" stroke="#333" stroke-width="3"/>
  <text x="212" y="372" text-anchor="middle" font-size="26" font-weight="bold">EXIT</text>
  <text x="212" y="398" text-anchor="middle" font-size="15" fill="#555">hold = HOME</text>
  <rect x="340" y="336" width="176" height="76" rx="10" fill="#eee" stroke="#333" stroke-width="3"/>
  <text x="428" y="372" text-anchor="middle" font-size="26" font-weight="bold">M</text>
  <text x="428" y="398" text-anchor="middle" font-size="15" fill="#555">hold = LOCK</text>

  <!-- keypad: 3 x 4. Plain digits where the moulding prints ham jargon.
       The four action keys are tinted and tagged HOLD. -->
  <g id="keypad">
    <rect x="124" y="436" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="184" y="492" text-anchor="middle" font-size="40" font-weight="bold">1</text>
    <rect x="260" y="436" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="320" y="492" text-anchor="middle" font-size="40" font-weight="bold">2</text>
    <rect x="396" y="436" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="456" y="492" text-anchor="middle" font-size="40" font-weight="bold">3</text>

    <rect x="124" y="534" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="184" y="590" text-anchor="middle" font-size="40" font-weight="bold">4</text>
    <rect x="260" y="534" width="120" height="82" rx="10" fill="#d6e6f7" stroke="#356" stroke-width="3"/>
    <text x="320" y="572" text-anchor="middle" font-size="34" font-weight="bold">5</text>
    <text x="320" y="598" text-anchor="middle" font-size="17" fill="#234">WX · HOLD</text>
    <rect x="396" y="534" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="456" y="590" text-anchor="middle" font-size="40" font-weight="bold">6</text>

    <rect x="124" y="632" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="184" y="688" text-anchor="middle" font-size="40" font-weight="bold">7</text>
    <rect x="260" y="632" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="320" y="688" text-anchor="middle" font-size="40" font-weight="bold">8</text>
    <rect x="396" y="632" width="120" height="82" rx="10" fill="#d6e6f7" stroke="#356" stroke-width="3"/>
    <text x="456" y="670" text-anchor="middle" font-size="34" font-weight="bold">9</text>
    <text x="456" y="696" text-anchor="middle" font-size="17" fill="#234">CALL · HOLD</text>

    <rect x="124" y="730" width="120" height="82" rx="10" fill="#d6e6f7" stroke="#356" stroke-width="3"/>
    <text x="184" y="768" text-anchor="middle" font-size="34" font-weight="bold">*</text>
    <text x="184" y="794" text-anchor="middle" font-size="17" fill="#234">SCAN · HOLD</text>
    <rect x="260" y="730" width="120" height="82" rx="10" fill="#d6e6f7" stroke="#356" stroke-width="3"/>
    <text x="320" y="768" text-anchor="middle" font-size="34" font-weight="bold">0</text>
    <text x="320" y="794" text-anchor="middle" font-size="17" fill="#234">FM · HOLD</text>
    <rect x="396" y="730" width="120" height="82" rx="10" fill="#fff" stroke="#333" stroke-width="3"/>
    <text x="456" y="768" text-anchor="middle" font-size="34" font-weight="bold">#</text>
    <text x="456" y="794" text-anchor="middle" font-size="17" fill="#555">FAV</text>
  </g>

  <!-- the legend: the seven rules as the bottom strip -->
  <g id="legend" font-size="21" fill="#222">
    <text x="320" y="856" text-anchor="middle" font-weight="bold">HOLD = press and keep holding</text>
    <text x="320" y="888" text-anchor="middle">HOLD 5 = WEATHER · HOLD 0 = FM · HOLD 9 = YOUR DRIVER</text>
    <text x="320" y="920" text-anchor="middle">PTT = HOLD THE CAR · HOLD * = LOCK OUT · LONG-EXIT = HOME</text>
  </g>
</svg>
"""
