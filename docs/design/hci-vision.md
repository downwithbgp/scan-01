# HCI Vision — RaceScan Edition

**Status:** v0.1 — design vision, not a spec
**Scope:** HCI reimagination for race-fan scanner firmware on Quansheng UV-K5/K6 hardware, plus the hardware v2 vision it implies
**Reference base:** F4HWN v4.3 (`uv-k5-firmware-custom-4.3`, Apache-2.0, kept locally in `F4HWN/`)
**Design north star:** Dieter Rams — *"as little design as possible"* — applied to a device used in 110 dB grandstands, in direct sun, one-handed, by people who do not know what a VFO is.

---

## 1. Why this exists

Racing Electronics owns the race-scanner market. A weekend rental runs $50–80; a "pro" scanner purchase $300–600; the frequency guides are sold separately, every season, per track. The hardware is a cheap FM receiver in a plastic brick. The data is a frequency list. Both can be open.

The Quansheng UV-K6 is a $25 commodity radio that already contains everything a race scanner needs:

| Need | UV-K6 hardware |
|---|---|
| Team comms, race control, officials (450–470 MHz analog FM, CTCSS) | BK4819 FM RX, full coverage |
| MRN / PRN / IMSA Radio (commercial FM) | BK1080 FM broadcast RX (dedicated chip; on models without it the BRD feature is compiled out) |
| Weather / rain delays (NOAA 162 MHz) | Wide RX covers 162.400–162.550 |
| Direct car-number entry | Full 16-key front keypad |
| Loud, one-handed use | Handheld shape, side PTT + 2 side keys, top knob |
| All-day scanning | 1600–3500 mAh Li-ion (base firmware already supports 3500 mAh packs), USB-C charging on later revisions |

The firmware is the product. The frequency data is the product. The radio is a commodity. That inverts RE's business model: they sell the radio + the data + the markup; we give away the software and the data and let fans keep using the $25 radio they may already own (or that costs less than one weekend of rentals).

**The disruption thesis is not a cheaper scanner. It is a free scanner that already exists inside a cheap radio.**

---

## 2. The user, the moment, the failure mode

### Persona: "the aunt with the cooler"

Not a ham. Not a scanner hobbyist. A NASCAR/IMSA/IndyCar fan who:
- knows the **car numbers** of their favorite drivers, not their frequencies;
- is in a grandstand: sun glare, noise, one hand holding a drink;
- wants to *follow the race*, not *operate a radio*;
- may have used a rented Racing Electronics scanner before, or never used a scanner at all.

### The moment

Green flag drops. The fan wants to hear their driver's team. They need it **now**, with **zero** learning curve, **without looking away from the track for more than a second**.

### The failure mode of current firmware

Stock Quansheng and F4HWN are ham-radio firmware: VFO/MR modes, 63-entry menus, F-key chord combinations, jargon ("CTCSS", "STEP", "Offset", "Wide/Narrow", "VOX", "ScanRng"). Every one of those concepts is invisible to a race fan — but worse, they are *present*, competing for the same screen and the same buttons. A fan who opens the menu by accident is lost. A fan who can't find "scan" during a caution is angry.

Rams: *"Good design makes a product understandable… it explains itself."* The current UI explains ham radio. The RaceScan edition must explain **racing**.

---

## 3. Design principles (Rams → this project)

| Rams principle | What it means here |
|---|---|
| **1. Innovative** | Car-number-first interaction. PTT repurposed as HOLD (the hero button). Open frequency packs as a product. |
| **2. Useful** | Boots into SCAN. Weather + broadcast one key away. MUTE for family-friendly listening. |
| **3. Aesthetic** | Typographic screen: one huge car number, generous whitespace, quiet status strip. No decoration. |
| **4. Understandable** | Three states (SCAN / HOLD / LIST), zero menus in the hot path, plain English labels. Every key does one thing. |
| **5. Unobtrusive** | No beeps. The radio disappears until you need it. The volume knob is *always* the volume knob. |
| **6. Honest** | Receive-only edition (no fake TX). Honest about the K6's RF limits at a packed track. No "PRO" branding. |
| **7. Long-lasting** | Open data survives season changes. Firmware updates via the standard flasher. Hardware v2 is open hardware. |
| **8. Thorough, to the last detail** | Per-channel squelch defaults, pocket key-lock, mute-timer behavior, hour-7 battery behavior, scan resume rules. |
| **9. Environmentally friendly** | Reuse a $25 commodity instead of buying a new plastic scanner per season. Repairable open hardware later. |
| **10. As little design as possible** | The whole interaction model is seven rules (below). Everything else is deleted. |

**The seven rules:**

1. The **knob is volume** — in every listening state. (Only inside SETUP does it temporarily edit values.)
2. The **biggest button** (PTT) is **HOLD**: lock the car you're hearing / resume scanning.
3. **Type a car number** (1–3 digits, optional suffix letter — "29A") to hear that car — anywhere outside SETUP.
4. The radio **boots into SCAN** and scanning never stops unless you hold.
5. **UP/DOWN walks the car list**; `*` (printed label: SCAN) always returns to scanning; long-`*` while holding a car locks it out; long-press the printed label on any key — 0 (FM) = broadcast, 5 (NOAA) = weather, 9 (CALL) = your driver, # (F) = favorites.
6. **EXIT always returns to SCAN.** There is no "are you sure". There is no dead end.
7. **One press to follow, two to jump.** PTT follows what you hear; two digits jump to any car. The only longer path is CAPTURE — and it is a hold, a number, and a press, not a hunt.

Everything in the firmware either serves these rules or is deleted.

---

## 4. The interaction model

### 4.1 States

```
        ┌──────────────────────────────────────────────────────┐
        │                                                      │
   ┌────▼────┐   PTT · digits · ▲▼          ┌─────────────────▼──────┐
   │  SCAN   │ ───────────────────────────► │        HOLD            │
   │ (boot)  │ ◄─────────────────────────── │ (one car, loud)        │
   └────▲────┘   PTT · EXIT · *             └─────────────────▲──────┘
        │                 ▲                                   │
        │  ▲▼             │ long-PTT                          │ digits · ▲▼
        │                 │                                   │
   ┌────┴─────────┐  ┌────┴────────────┐                      │
   │    LIST      │  │    CAPTURE      │──────────────────────┘
   │ (car browser)│  │ (enter + save)  │   PTT = save → HOLD on new car
   └────▲─────────┘  └─────────────────┘   EXIT = cancel
        │      M
        │      │
   ┌────┴──────────┐
   │    SETUP      │
   │ (4 pages)     │
   └───────────────┘
```

Solid arrows: one press. Long-press PTT (from SCAN, HOLD, or LIST) opens CAPTURE pre-filled with the last-heard frequency; ＋ NEW in LIST opens it empty. EXIT and `*` return to SCAN from any state. BRD (long-0) and WX (long-5) are HOLD-like sub-states reachable from any listening state; PTT, EXIT, or `*` returns to SCAN (§5.10).

- **SCAN** — the default state of the radio. Continuously scans the pack (all cars + stations), unmutes the moment a signal lands, holds briefly, resumes. The screen shows the currently-heard car. Scanning is *the* behavior; there is no "scan on/off", only "hold".
- **HOLD** — one car, locked. Reached by PTT (hold what you hear), by typing a number, or by walking UP/DOWN through the car list. PTT again → back to SCAN.
- **LIST** — the full pack as a scrollable list: car number + driver, stations (Race Control, MRN, PA, WX) at the end. Long-`*` toggles lockout. This is the only "browse" surface.
- **SETUP** — four short pages: **Pack** (series/track/session loaded, lockouts), **Audio** (mute duration, beeps off), **Display** (contrast, invert, backlight), **Info** (firmware, battery). Reached only via M. A fan can ignore it forever.

### 4.2 Key map

| Input (printed label) | SCAN | HOLD | LIST | SETUP |
|---|---|---|---|---|
| **Knob** | volume | volume | volume | volume / scroll |
| **PTT** (short / long) | → HOLD / → CAPTURE (§4.5) | → SCAN / → CAPTURE | → SCAN / → CAPTURE | back / — |
| **0–9** (short) | type car number → HOLD (or frequency: `*` = point) | type car number → HOLD | jump to number | — |
| **0** long (label FM) | → BRD (broadcast) | → BRD | → BRD | — |
| **5** long (label NOAA) | → WX (weather) | → WX | → WX | — |
| **9** long (label CALL) | jump to my driver | same | same | — |
| **UP / DOWN** | → LIST (browse) | prev / next car | scroll list | change value |
| `*` short / long (label SCAN) | resume scan (no-op) / — | → SCAN / lockout this car | → SCAN / lockout selected | → SCAN / — |
| `#` (label F) | cycle favorites | cycle favorites | cycle favorites | — |
| **F1** short / long | MUTE 10 s / toggle MUTE | same | same | — |
| **F2** short / long | cycle scan group / — | same | same | — |
| **M** short / long | → SETUP / key lock | → SETUP / key lock | → SETUP / key lock | back / — |
| **EXIT** | (no-op) | → SCAN | → SCAN | back |

Notes:
- **F-key chords are abolished.** F4HWN's `F + 5`, `F + 8` etc. are ham muscle memory; race fans get one function per physical key. The keypad key labeled `F #` loses its modifier role entirely — it is simply `#`, the favorites key. That is a feature.
- **The printed labels are the long-press legend.** The keypad is physically printed — M, ▲▼, EXIT, 1 BAND … 0 FM, * SCAN, # F. We cannot relabel keys, so the design *uses* the labels: a short press is always a digit or nav; a long press performs the printed action — hold 0 (FM) for broadcast, hold 5 (NOAA) for weather, hold 9 (CALL) for your driver, press * (SCAN) to scan, # (F) for favorites. Keys whose labels are ham jargon to a fan (BAND, A/B, VFO/MR, FC, H/M/L, VOX, R) are plain digits, free to repurpose. The hardware already explains itself; we just have to listen to it.
- **Label long-presses are deliberate.** A label action needs a sustained hold (~0.8 s) and is suppressed while a digit entry is pending — a quick tap on 5 is always the digit, so typing car 50 can never open weather. The label is a promise, not a trap.
- **Inside BRD/WX** (§5.10): ▲▼ = presets/channels, digits + `*` = tune any frequency, PTT/EXIT/`*` = back to SCAN.
- **Typing is a transient sub-state** of SCAN/HOLD/LIST: while typing, EXIT deletes the last character (long-EXIT clears all), a short timeout commits, and `*` inserts the decimal point — turning the entry into a frequency (§4.5).
- **PTT is not TX.** The edition is receive-only (see §7). The physical PTT button becomes the HOLD button — the biggest, most thumb-accessible control does the most important thing. This is the single most important interaction decision in this document.
- **Lockout is instant and visible:** while holding a noisy car, one long-`*` press drops it from the scan; the LIST shows it struck through; one more long-`*` restores it. Lockouts persist in EEPROM across reboots and export with the pack. RE scanners hide lockout in a menu; ours is one gesture, because at a race the noisy channel *is* the problem in the moment.
- **MUTE** silences audio for 10 s (or until PTT) — the "driver said a bad word, kids are right here" button. Long-press F1 toggles mute indefinitely.

### 4.3 Typing a car number

Digits anywhere = "take me to that car". Two digits, done; three for series with 3-digit numbers. Car numbers are alphanumeric: **1–3 digits plus an optional suffix letter** — "29A" and "13F" are different cars from "29" and "13". After the digits, ▲/▼ cycles the suffix letter A–Z (long-EXIT clears it). A short timeout commits; EXIT deletes the last character. There is exactly **one** ambiguity rule in the car world: **digits are a car number; `*` inserts the decimal point and makes the entry a frequency** (§4.5). If the entry matches no car, the radio asks "tune as frequency?" instead of guessing (placement by band: 450–470 UHF, 162 NOAA, 88–108 broadcast). The typed digits render huge on screen as you type, so you can type *without looking* and verify with peripheral vision — this is a glanceable device.

### 4.4 What deliberately does NOT exist

- No VFO/MR split, no ham "frequency mode" (bands, steps, offsets, dual watch). The pack is the only world — *but the world grows*: direct frequency entry exists solely as the **CAPTURE** path (§4.5), where a typed or caught frequency immediately becomes a pack entry. There is no parked frequency that lives outside the pack.
- No CTCSS/DCS/Step/Offset/ScanRng/Squelch-level settings. Per-car squelch defaults are baked in by the pack tool; the fan never sees them.
- No 63-item menu. No menu index.
- No scanning "modes" (TO/CO), no scan-list juggling — the scan group concept is reduced to F2 cycling ALL → FAVS → A/B/C.
- No TX. No VOX, no alarm, no DTMF, no scrambler, no 1750 Hz. None of it exists in this build (the base codebase keeps it for the ham editions).

### 4.5 On-the-go capture: entering and saving frequencies at the track

Packs are wrong sometimes, teams switch frequencies mid-race, and a friend with a better scanner will say "try 451.1125". The radio must be able to learn — without a laptop, without a menu, without ham-radio concepts.

**Path A — catch it from the air (the primary path).** Long-press PTT in SCAN or HOLD: CAPTURE opens pre-filled with the last-heard frequency and its decoded tone (the BK4819 already decodes CTCSS — `BK4819_GetCxCSSScanResult`). Zero typing to save something you just heard. *(Implementation note: the base treats PTT as press/release-only, so a hold-timer on PTT must be added — the same mechanism the base already uses for F1/F2/M long-presses.)* With no pack loaded there is no last-heard to pre-fill; that path is typed entry (below).

**Path B — type it.** Digits with `*` as the decimal point (`451` `*` `1125`) open CAPTURE as a live preview — you hear the frequency immediately, before saving anything. (Longer plain entries that match no car get the same "tune as frequency?" prompt, §4.3.)

**The save flow (shared):** type the car number in the 32 px racing digits — digits, then ▲/▼ for the suffix letter if the car has one ("29A") — then PTT saves. The new entry:

- joins the current scan group — the fan hears it again without doing anything else;
- is born `origin: captured, verified: false` (§6.1) — honest data, upgraded only when confirmed;
- gets the name "NEW" by default; renaming happens in LIST (M on the row → name editor — a small new multi-tap keypad component; the base's input box is digits-only, so this is new P1 code);
- **duplicate numbers never overwrite:** if the number already exists in the pack, the radio offers "24 EXISTS — PTT = add 24 ALT · EXIT = cancel", appending an alternate entry (§6.1's flat model). Pack data is never silently destroyed.

The CAPTURE form has **no auto-commit timeout** — the car-jump timeout of §4.3 is disabled here; the form stays until PTT saves or EXIT cancels.

EXIT cancels. Nothing is written until PTT confirms — an accidental long-press is harmless.

**The empty-radio story:** capture works even with no pack loaded — typed entry (Path B) and the LIST "＋ NEW" slot work on an empty radio, so a fan who shows up with a blank radio can build a mini-pack from scratch, one frequency at a time. The radio is never useless.

**Capture feeds the open data:** packtool's dump reads captured entries back and turns them into pack submissions — own observation at the track is the most legitimate source a community database can have (§6.2).

Rams: *"less but better — because it concentrates on the essential aspects."*

---

## 5. Screen language (128×64, stock hardware)

### 5.1 The type inventory (verified against the base)

Everything renders on the 8 px hardware line (`gFrameBuffer[7][128]`: one status line + seven 8 px frame lines, `driver/st7565.h`). Fonts are stored as strips of 8-row columns and blitted with one `memcpy` per frame line — the `gFontBig` pattern in `ui/helper.c`. **Nothing in the renderer needs to change for a new font; only a new data table.**

| Font | Size | Glyphs | Flash | Origin | Role in RaceScan |
|---|---|---|---|---|---|
| `gFontSmall` | 6×8 | 94 | ~560 B | base (`font.c`) | status strip, metadata, LIST names |
| `gFontSmallBold` | 6×8 | 94 | ~560 B | base, opt-in | emphasis, lockout hints |
| `gFontBig` | 7×16 | 94 | ~1.3 KB | base | LIST row numbers, boot screen |
| `gFontBigDigits` | 10×16 | 11 (0–9, −) | ~220 B | base | CAPTURE frequency line (§5.9) |
| `gFontSmallDigits` | 7×8 | 11 | ~80 B | base | status-bar digits |
| `gFont3x5` | 3×5 | 96 | ~290 B | base | unused (spectrum-analyzer-only) |
| **`gFontRacingDigits` (new)** | **18×32** | **36 (0–9, A–Z)** | **~2.2 KB** | **new** | **the car number** |

The one new font costs ~2.2 KB of flash — and the edition is deleting TX, VOX, DTMF, alarm, and scrambler anyway. Typography is not a memory problem; it is a design problem.

### 5.2 The scale: 8 / 16 / 32

One size per job, ratio 1:2:4, every size a multiple of the 8 px line:

- **8 px** — the quiet voice: status strip, frequency, hints, LIST names. Present, never shouting.
- **16 px** — the explanatory voice: the driver's last name on SCAN, row numbers in LIST.
- **32 px** — the loud voice: the car number. One size only. There is no "medium".

Why this works on this display: it is 1-bit — no anti-aliasing, no gray. Legibility comes from *weight and size*, not rendering tricks. In direct sun, at arm's length, with the backlight fighting glare, the 32 px digits are the only thing the fan must read; everything else is bonus. Hierarchy by size ratio, not by decoration.

### 5.3 The SCAN screen (also HOLD) — the pixel budget

```
row 0   ▮▮▮▮ 14:32 ▂▄▆█                 status strip (8px): battery · clock · signal · lock
row 1 ┐
row 2 │        24    BYRON             number zone 32px (right-aligned) · last name 16px
row 3 │              HMS · CHEVY       team/entry 8px
row 4 ┘
row 5   450.8875 ─────────── ▂▂▄▆      freq (8px) · signal bar (right)
row 6   ◉ SCAN                         state (8px): ◉ SCAN / ◼ HOLD 24 / ▸ LIST
row 7                                  whitespace — the frame
```

- **The number is right-aligned in its zone** — the door-number convention: "5", "24", "100" all anchor to the same edge, so a changing number never jitters.
- **No box around the number.** The whitespace is the frame. The number is the only 32 px object on the screen; it does not need a border to be found.
- **Last name at 16 px, team at 8 px.** "BYRON" is the identity; the first name lives in LIST. No tone display — tones are pack data, not fan information (§6.1).
- **SCAN vs HOLD = one changed line.** `◉ SCAN` vs `◼ HOLD 24`; the rest of the screen is identical. No "24 of 64" position ticker — during a scan it would change constantly, and constant change is noise.
- **No inverted video on this screen.** The audible car is identified by the number itself; invert is reserved for LIST selection.
- Row 7 stays empty. A screen that is never full reads as calm.

### 5.4 The LIST screen

```
row 0   ▮▮▮▮ 14:32 ▂▄▆█
row 1┐  24  BYRON · WILLIAM            ← selected row: inverted 16px block
row 2┘
row 3┐  08  JOHNSON · JIMMIE
row 4┘
row 5┐  19  TRUEX JR · MARTIN          ← struck through when locked out
row 6┘
row 7   ▴ 64 cars · hold * = lockout   hint (8px)
```

- Rows are 16 px: the number in `gFontBig` (7×16) leads, the full name in 8 px sits beside it (vertically centered). The number still leads — it is the identity.
- Three rows visible plus a scroll hint. Stations (RACE CTRL, MRN, PA, WX) are entries at the bottom, typed in caps so they read as "not a car". A final "＋ NEW" row opens CAPTURE empty (§4.5).
- Lockout = a strike-through line across the row. One long-`*` press toggles it. Visible, reversible, no sub-menu.

### 5.5 The SETUP screens

Four pages, 8 px rows (seven visible), UP/DOWN + knob to edit, M to move between pages. This is the only place the firmware resembles a menu, and it is deliberately boring — no big type, no icons, just text. Fans should be able to own the radio for a year without opening it.

### 5.6 The racing digits font (new)

The only new font in the whole edition. Spec:

- **36 glyphs: 0–9 plus A–Z.** Car numbers are alphanumeric — "29A", "13F" (§4.3) — and the suffix letter renders at the same 32 px height, door-number style. Station tags stay 8 px caps.
- **Metrics:** 32 px tall (4 strips of 8), cap height ~24 px; digits 18 px advance ("1" narrowed to 12 px), suffix letters 14 px. "29A" = 18+18+14 + spacing ≈ 54 px — leaves room for the name block.
- **Style:** heavy, slightly condensed grotesque — the feel of a race-car door number. Uniform stroke weight (1-bit: no thin parts), open counters so "0", "8", "9" never fill in on a coarse LCD.
- **Storage & rendering:** 4 strips × width bytes per glyph, blitted with four `memcpy`s into consecutive frame lines — exactly the `gFontBig` pattern, no renderer changes.
- **Cost:** ~36 × 4 × ~16 ≈ 2.2 KB flash. Generated through the existing `utils/` font pipeline (`.fon` → C tables).
- **Always right-aligned** (§5.3), so 1-, 2-, and 3-character numbers sit on the same edge.

### 5.7 Screen-space principles (Rams on a 128×64)

1. **One big thing per screen.** SCAN: the number. LIST: the selected row. SETUP: nothing — it is utility and should look it.
2. **Whitespace is a frame, not waste.** The empty row and the empty zone around the number do the work a border would do.
3. **No blinking. Ever.** State is a static glyph, not an animation. Blinking is how cheap radios scream; this one does not scream.
4. **The status strip is quiet and constant:** battery, clock, signal, lock — four items, 8 px, never more. No mode icons (Vox, DWR, PTT-mode…): those are ham concepts. An optional "clean mode" (SETUP → Display) removes even this strip.
5. **Invert is a selection tool only** — the LIST row. Not decoration, not alerts.
6. **Everything sits on the 8 px grid; every font is a multiple of 8 px tall.** No half-line layouts, no pixel soup.

### 5.8 Boot sequence

Power on → 0.8 s brand/pack screen (pack name: "DAYTONA 500 — FEB 2026" + car count) → straight into SCAN. No "press any key", no menu, no confirmation. The pack screen doubles as the *identity* of the loaded data, so a fan who forgot to load a pack sees "NO PACK — plug in and load" instead of a blank radio.

Backlight: on at boot, auto-dim after 30 s (configurable), full-bright on any key. Sun readability is handled by contrast + the bold 32 px digits, not by fighting the sun with backlight.

### 5.9 The CAPTURE screen

```
row 0   ▮▮▮▮ 14:32 ▂▄▆█
row 1┐  451.1125 · T 94.8          freq in gFontBigDigits (16px) · tone 8px
row 2┘
row 3┐  24                         car-number input, 32 px racing digits
row 4┘
row 5   SAVE AS CAR #24            preview of what will be saved
row 6   PTT = save · EXIT = cancel
row 7                               whitespace
```

- The frequency line reuses the base's 10×16 `gFontBigDigits` via `UI_DisplayFrequency`, which also draws the decimal point inline (the glyph table holds 0–9 + − only) — no new font for frequency typing; the 32 px racing digits handle the number input, so the new font does both jobs.
- One screen, two moments: it opens pre-filled (caught from the air) or empty (typed), and it is *live* — you hear what you are about to save before you commit.
- The tone shows here only: it is part of what gets saved, so it reads as confirmation, not configuration (§6.1: tones are data, not settings).

### 5.10 The BRD and WX screens

Two label-honest sub-states, one gesture each: long-0 (printed FM) and long-5 (printed NOAA). Both are HOLD-like: PTT, EXIT, or `*` returns to SCAN; the knob is still volume; the loud voice is the station identity, not the frequency.

```
BRD (broadcast — MRN/PRN/IMSA Radio):
row 0   ▮▮▮▮ 14:32 ▂▄▆█
row 1┐
row 2│  MRN          101.1           station name 32px · freq 16px
row 3│
row 4┘
row 5   BRD · 3 of 8 presets
row 6   ▲▼ = presets · PTT = back
row 7

WX (weather — NOAA):
row 0   ▮▮▮▮ 14:32 ▂▄▆█
row 1┐
row 2│  WX 7         162.550         channel 32px · freq 16px
row 3│
row 4┘
row 5   NOAA · alerts on this channel
row 6   ▲▼ = channels · PTT = back
row 7
```

- BRD presets come from the pack (`stations` with `fm:`); UP/DOWN walks them; digits with `*` tune any frequency. WX cycles the seven NOAA channels — the fan never configures anything.
- The 32 px racing digits handle the station/channel identity here too: the same font, both jobs.

---

## 6. The weekend pack (data model + open data)

### 6.1 What a pack is

A **pack** = one race weekend's listening world: every car, every station, the lockouts, the display order. It is loaded into the radio's EEPROM (8 KB — a 64-car pack with names, freqs, and tones fits comfortably in the channel map) via the existing PC/UART path (CHIRP driver / k5prog-style tooling).

```json
Pack {
  meta:  { series: "NASCAR Cup", track: "Daytona", session: "Race",
           season: 2026, date: "2026-02-15", author, version },
  cars:  [ { number: "24", driver: "William Byron", team: "Hendrick Motorsports",
             entry: "HMS · CHEVY", freqs: [450.8875, 451.1125],
             tone: 94.8, group: "A", favorite: true, origin: "pack", verified: true },
           ... { number: "29A", driver: "…", … } ],
  stations: [ { name: "RACE CTRL",  freq: 461.200, tone: 0 },
              { name: "MRN",        fm: 101.1 },            // broadcast radio
              { name: "PA",         freq: 464.500, tone: 0 },
              { name: "WX",         noaa: 7 } ],            // NOAA channel
  lockouts: []
}
```

- Cars map to memory-channel slots; the *car number* is the channel identity — the frequency is just an attribute. (This inverts the stock mental model where a channel is a frequency.)
- Car numbers are alphanumeric strings: 1–3 digits plus an optional suffix letter ("29A", "13F"). The number string is the identity — "29" and "29A" are different cars with different entries.
- Cars with multiple freqs (primary/secondary) appear as **separate flat entries** — `24 BYRON` and `24 ALT · BYRON` — exactly how RE-style packs list alternates. No multi-freq UI complexity anywhere; the pack decides. (A v1 refinement may collapse them into one entry with an alt shown in HOLD.)
- Tones (CTCSS) are *data*, not settings. Outside the CAPTURE screen (§5.9) the fan never sees them — there, the captured tone is shown as confirmation of what gets saved, not as a configuration value.
- Every entry carries `origin` (`pack` \| `captured` \| `manual`) and `verified` (`true`/`false`). Pack entries ship verified by their author; on-radio captures are born `captured`/`false` and are upgraded only when someone confirms them at the track. This is the raw material of the data pipeline (§6.2).

### 6.2 Open data is the moat-buster

RE sells frequency guides every season. We open-source the equivalent:

- **`race-packs/`** — a community-curated, git-tracked database: per series × track × session, with verification workflow (a contributor marks a pack "verified at the track", date-stamped). Frequency changes per season are normal; git history *is* the changelog.
- **Sourcing rule:** pack data is collected from public sources, own observation at the track, and fan submissions — never scraped from paid frequency guides (RE's guides are commercial publications; wholesale copying invites copyright/database-rights claims we don't need). Contributors attribute their sources in the pack metadata.
- **Capture is the best source.** A fan who catches a frequency on the radio and exports it has done own-observation field work — the most legitimate data there is. The pipeline moves entries `captured` → `reported` → `verified`; the packtool dump (over the PC cable) is a data-submission tool as much as a backup.
- **`packtool`** — a small CLI (later a web page) that compiles a pack JSON → EEPROM image, flashes over the USB/serial cable, and can *record* a live session ("while scanning, mark this car") to build a pack from scratch at the track.

This is the part that makes the whole thing sustainable: hardware is commodity, firmware is free, and the *data* — the thing RE monetizes hardest — is open and community-owned.

---

## 7. Receive-only: the honest product decision

- The RaceScan edition **cannot transmit. Period.** PTT is HOLD; TX paths are compiled out.
- Why: (a) the target user has no license and must never be able to key up on team/business frequencies — this is a liability and a moral hazard, not a feature; (b) it frees the biggest physical button for the most important fan action; (c) it saves flash and RAM for the scan engine.
- The base codebase keeps full TX for the ham editions (F4HWN's existing model). RaceScan is a strict *edition*, built from the same source tree via the Makefile — exactly how F4HWN already ships RescueOps vs. Broadcast vs. Game.

---

## 8. Scan engine behavior (the invisible 90%)

Most of the *experience* is the scan engine, and it must be tuned for racing, not for ham repeaters:

- **Dwell:** racing chatter is bursts of 2–10 s with gaps. Dwell should be short (~1.5–2 s) with fast resume after a gap — the F4HWN/egzumer fast-scan work is the starting point.
- **No squelch crash:** mute/unmute must be click-free (audio ramp), or the radio sounds broken in a quiet grandstand.
- **Signal-based landing:** decide "this car is talking" on signal + tone-lock (CTCSS match), not just squelch — so a car with an open mic doesn't monopolize the scan (RE scanners do tone-lock natively; we get it from the BK4819's CTCSS decoder).
- **FOLLOW mode (v1):** your favorite car is priority — the scan returns to it after any other car's transmission ends. "I want to hear everyone, but never miss the 24."
- **Adaptive per-car squelch (v1):** packtool stores per-car noise-floor offsets so a weak car doesn't get drowned by a strong neighbor; tuned from a "calibrate at the track" pass in LIST.

## 9. Power & audio behavior

- **All-day scanning:** RX duty is low; target is 6+ hours on a 2200 mAh pack with the backlight strategy above. Hour-7 behavior matters: battery % in the status strip from boot, low-battery warning at 20%, auto-dim aggressively at 10%. A fan with a dead scanner at lap 150 will never forgive us.
- **Audio:** BK4819 → existing amp. Volume taper biased to low levels (quiet listening in the car/at home) with headroom for the grandstand. MUTE is a hard, instant cut (see §4.2).
- **Headphones/earbuds:** the K6 jack drives earbuds fine; stereo-to-mono is handled in the pack (standard). No virtual surround gimmicks — honest mono.

---

## 10. Hardware v2 vision (shape, buttons, screen)

The stock K6 is good enough to ship the firmware **today**. But the user-visible HCI ceiling is the 128×64 screen and the shared keypad. If the firmware proves the concept, the natural endgame is a purpose-built open-hardware scanner. Design targets, *not commitments*:

**Form and shape**
- A handheld "brick" scanner (RE3000-like), not a ham transceiver: flat-ish, grippy, clip for the belt/shirt, **speaker grille on the front** (a radio clipped to a shirt plays toward the face).
- The body tapers toward the bottom so it sits in a cup holder and in a hand the same way.
- Knob on top, in the index-finger arc; HOLD button on the left side in the PTT position (thumb), bigger and softer than a PTT.
- Keypad recessed ~1 mm to prevent pocket presses (Rams: thorough down to the last detail).

**Buttons** (11 total, no chords, no long-press overload)
- Knob **with push** = volume + push-to-mute.
- HOLD (big, side) · MUTE · SCAN/LIST · WX · BRD · `#` favorites · M/SETUP · EXIT · UP/DOWN · digits 0–9.

**Screen**
- 240×160 (or 160×128) **transflective** monochrome LCD (Sharp memory LCD or STN): sunlight-readable *without* backlight, ~µA idle, huge digits (48 px car numbers), a real 5-row scan list, and a tiny spectrum/waterfall strip as a *glanceable "who's talking" surface* — the one thing 128×64 can't show.
- Framebuffer cost: 240×160×1 bit = 4.8 KB — still fits a 16 KB-RAM-class MCU with care.

**Radio front-end (the honest hardware fix)**
- The K6's wide-open front end is the *known* weakness at a race track (intermod, desense near team trailers and broadcast towers). Track B adds **450–470 MHz band-pass + FM-broadcast band-pass** filtering — this is the real RF win, not the screen.

**Audio**
- 1–2 W amp with AGC, larger speaker, headphone jack.

**Compute**
- Keep the BK4819 (proven, cheap, FM+AM). A Cortex-M0+ class MCU with 128 KB flash / 16–32 KB RAM (e.g., STM32G0-class or a CH32) is plenty.
- USB-C for charging + pack loading (the pack pipeline already exists).

**Open hardware**
- KiCad sources, CC-BY-SA. The firmware's display/input HAL (see §11) is written so Track A code ports to Track B with a driver swap, not a rewrite.

**The honest framing:** Track B is only worth building if Track A is loved. The firmware-first roadmap below keeps that option open without betting on it.

---

## 11. Where this lands in the F4HWN v4.3 codebase

The base is a fork of the F4HWN lineage (Apache-2.0) and already has the right seams:

| Concern | F4HWN v4.3 seam | RaceScan change |
|---|---|---|
| Edition mechanism | Makefile `EDITION_STRING`, `ENABLE_FEAT_F4HWN*` flags, per-edition builds (RescueOps etc.) | add a **RaceScan** edition target; TX/VOX/DTMF/alarm compiled out |
| App states | `app/` state machine (`app.c`, per-feature apps) | replace hot path with SCAN/HOLD/LIST/SETUP states |
| Rendering | `ui/` (`ui.c`, `main.c`, `status.c`, fonts in `font.c`) | new 32 px racing-digits font; race screens in `ui/` |
| Channels/settings | EEPROM layout in `settings.c`, CHIRP driver ecosystem | pack model mapped onto the channel map; packtool generates images |
| Scan | `ui/scanner.c`, `app/scanner.c`, egzumer fast-scan + scan-list work | race-tuned dwell, tone-lock landing, FOLLOW priority |
| Key handling | `driver/keyboard.c` full 4×4 matrix + PTT + SIDE1/2 | PTT→HOLD, F1/F2 dedicated, F-key chords removed |
| Broadcast FM / NOAA | `ENABLE_FMRADIO` (BK1080), `ENABLE_NOAA` | first-class BRD + WX actions |
| PC tooling | CHIRP driver, k5prog, UART | packtool + open `race-packs/` data repo |

## 12. Roadmap

- **P0 — Validate (now, weeks):** collect real frequency data from 2–3 race weekends (Daytona, Sebring, Indy are the natural first three); interview 3–5 real fans; confirm the pack model against what a weekend actually needs; validate the digit-entry rules and the long-press label actions (§4.2/§4.3/§4.5) with real fans — watch them, don't interview them; simplify anything that confuses. *Gate: a verified sample pack for one track.*
- **P1 — RaceScan firmware on stock K6 (weeks–months):** pack model + EEPROM layout; SCAN/HOLD/LIST/SETUP screens; key map (label-honest long-press actions: 0=BRD, 5=WX, 9=my driver, *=SCAN) incl. suffix letters; BRD/WX sub-states; **on-the-go capture (long-PTT catch, typed entry, save-as-car, duplicate→ALT rule, unverified flag, packtool export, multi-tap name editor)**; RX-only edition build; packtool v0; boot/backlight/power behavior. *Gate: a fan (not a ham) uses it for a full race without asking a question.*
- **P2 — Scan engine + data (months):** tone-lock landing, FOLLOW mode, adaptive squelch; web pack builder; open `race-packs/` v1 (Daytona, Sebring, Indy, Watkins Glen, Charlotte, Road America…). *Gate: community submits a verified pack for a track we don't cover.*
- **P3 — Hardware v2 (only if P1/P2 show demand):** open-hardware prototype per §10. *Gate: 100+ users of P1/P2.*

## 13. Honest risks & open questions

1. **RF reality at the track.** The K6 front end is wide open; README-grade honesty: FM RX "should be fine", but a packed NASCAR garage is a hostile RF environment (intermod, desense). We must field-test at a real weekend before promising anything. Mitigation: scan-engine tuning, antenna choice; Track B fixes it properly with band-pass filters.
2. **Flash budget.** 60 KB flash / 16 KB RAM. A dedicated RX-only edition has headroom (TX, VOX, DTMF, alarm, scrambler all go), but the 32 px font + race screens must be earned. LTO/overlay knobs exist.
3. **Hardware variants.** K5/K6/5R board revisions (this build targets the DP32G030 Cortex-M0; earlier boards differ in details), BK1080 presence — the edition must build and behave sanely across them (the base ifdefs much of this).
4. **Data curation.** Frequency lists drift season to season; bad packs make the radio useless at the worst moment. The `race-packs` repo needs a verification workflow and honest "unverified" labeling.
5. **Legal.** RX-only by design; US scanner law permits listening to the racing/broadcast bands. Two hard rules: (a) we must *never* ship a path to TX on team/business frequencies (the base codebase is TX-capable — the edition must strip it, not hide it); (b) the K6's wide RX includes US cellular bands, and receiving cellular audio is a felony (18 U.S.C. §2511 / ECPA) — the edition must band-lock cellular ranges (824–894, 1710–1755, 1850–1990, 2110–2155 MHz) so the radio physically cannot tune them.
6. **Naming.** "RaceScan" is a working title; there are existing products/apps with similar names. Trademark check before any branding.
7. **Open questions for the community:** should packs be per-series (NASCAR-only) or multi-series? Should the long-0 BRD action also receive *track PA* (which at some tracks is AM broadcast)? Is FOLLOW mode a favorite-car priority or should fans pick any car? — all answerable in P0 validation.
