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
| **10. As little design as possible** | The whole interaction model is six rules (below). Everything else is deleted. |

**The six rules:**

1. The **knob is volume** — in every listening state. (Only inside SETUP does it temporarily edit values.)
2. The **biggest button** (PTT) is **HOLD**: lock the car you're hearing / resume scanning.
3. **Type a car number** to hear that car — anywhere outside SETUP.
4. The radio **boots into SCAN** and scanning never stops unless you hold.
5. **UP/DOWN walks the car list**; `*` drops a car from the scan (lockout); `#` jumps to your favorite.
6. **EXIT always returns to SCAN.** There is no "are you sure". There is no dead end.

Everything in the firmware either serves these rules or is deleted.

---

## 4. The interaction model

### 4.1 States

```
        ┌──────────────────────────────────────────────┐
        │                                              │
   ┌────▼────┐   PTT / digits / UP▼DOWN    ┌───────────▼──────┐
   │  SCAN   │ ──────────────────────────► │     HOLD         │
   │ (boot)  │ ◄────────────────────────── │ (one car, loud)  │
   └────▲────┘        EXIT / PTT           └───────────▲──────┘
        │                                              │
        │  * (any)            EXIT                     │ digits / #
        │                                              │
   ┌────┴─────────┐                            ┌───────┴───────┐
   │    LIST      │ ◄────────────────────────► │    SETUP      │
   │ (car browser)│         M (rarely)         │ (4 pages,     │
   └──────────────┘                            │  rarely seen) │
                                               └───────────────┘
```

- **SCAN** — the default state of the radio. Continuously scans the pack (all cars + stations), unmutes the moment a signal lands, holds briefly, resumes. The screen shows the currently-heard car. Scanning is *the* behavior; there is no "scan on/off", only "hold".
- **HOLD** — one car, locked. Reached by PTT (hold what you hear), by typing a number, or by walking UP/DOWN through the car list. PTT again → back to SCAN.
- **LIST** — the full pack as a scrollable list: car number + driver, stations (Race Control, MRN, PA, WX) at the end. `*` toggles lockout. This is the only "browse" surface.
- **SETUP** — four short pages: **Pack** (series/track/session loaded, lockouts), **Audio** (mute duration, beeps off), **Display** (contrast, invert, backlight), **Info** (firmware, battery). Reached only via M. A fan can ignore it forever.

### 4.2 Key map

| Input | SCAN | HOLD | LIST | SETUP |
|---|---|---|---|---|
| **Knob** | volume | volume | volume | volume / scroll |
| **PTT** | → HOLD (lock current car) | → SCAN (resume) | → SCAN | back |
| **0–9** | type car number → HOLD | type car number → HOLD | jump to number | — |
| **UP / DOWN** | walk pack (hold preview) | walk pack | scroll list | change value |
| `*` | → LIST (or cancel a half-typed number) | lockout toggle (this car) | lockout toggle (selected) | — |
| `#` | next favorite | next favorite | next favorite | — |
| **F1** short / long | MUTE (10 s) / WX toggle | same | same | — |
| **F2** short / long | broadcast radio (MRN/PRN) / cycle scan group | same | same | — |
| **M** short / long | → SETUP / key lock | → SETUP / key lock | → SETUP / key lock | back / — |
| **EXIT** | (no-op) | → SCAN | → SCAN | back |

Notes:
- **F-key chords are abolished.** F4HWN's `F + 5`, `F + 8` etc. are ham muscle memory; race fans get one function per physical key. The keypad key labeled `F #` loses its modifier role entirely — it is simply `#`, the favorites key. That is a feature.
- **Typing is a transient sub-state** of SCAN/HOLD/LIST: while digits are half-typed, `*` deletes the last digit and EXIT cancels; a short timeout commits.
- **PTT is not TX.** The edition is receive-only (see §7). The physical PTT button becomes the HOLD button — the biggest, most thumb-accessible control does the most important thing. This is the single most important interaction decision in this document.
- **Lockout is instant and visible:** while holding a noisy car, one `*` press drops it from the scan; the LIST shows it struck through; one more `*` restores it. RE scanners hide lockout in a menu; ours is one press, because at a race the noisy channel *is* the problem in the moment.
- **MUTE** silences audio for 10 s (or until PTT) — the "driver said a bad word, kids are right here" button. Long-press F1 toggles mute indefinitely.

### 4.3 Typing a car number

Digits anywhere = "take me to that car". Two digits, done. Three digits for series with 3-digit numbers (IndyCar's 100+ era, NASCAR Trucks). A short timeout commits; a `*` or EXIT cancels. The typed digits render huge on screen as you type, so you can type *without looking* and verify with peripheral vision — this is a glanceable device.

### 4.4 What deliberately does NOT exist

- No VFO/MR split. No frequency mode. (The pack is the only world.)
- No CTCSS/DCS/Step/Offset/ScanRng/Squelch-level settings. Per-car squelch defaults are baked in by the pack tool; the fan never sees them.
- No 63-item menu. No menu index.
- No scanning "modes" (TO/CO), no scan-list juggling — the scan group concept is reduced to F2-long cycling ALL → FAVS → A/B/C.
- No TX. No VOX, no alarm, no DTMF, no scrambler, no 1750 Hz. None of it exists in this build (the base codebase keeps it for the ham editions).

Rams: *"less but better — because it concentrates on the essential aspects."*

---

## 5. Screen language (128×64, stock hardware)

### 5.1 Typography

Three sizes, two of which already exist in the codebase (`font.c`):

- **8 px** — status strip, metadata, list names (existing small fonts, incl. the 6×8 bold variant)
- **16 px** — driver names, list rows (existing `gFontBig`, 14×16)
- **32 px** — car numbers: a new dedicated *racing digits* font (0–9, plus a minimal A–Z for station tags). Big, bold, high-contrast. New bitmap font, small (10 glyphs core).

Rule: **one big thing per screen.** The eye lands on the car number. Everything else is 8 px quiet.

### 5.2 The SCAN screen (also HOLD)

```
▮▮▮▮ 14:32 ▂▄▆█              ← status strip: battery, clock, signal (8px)
┌──────┐
│  24  │ BYRON               ← car number 32px (left), driver 16px (right)
│      │ WILLIAM
│      │ HMS · CHEVY         ← team/entry 8px
└──────┘
 450.8875  T 94.8            ← freq + CTCSS, small (8px)
 ● SCAN  24 of 64            ← state + position in pack (8px)
```

- SCAN vs HOLD differ only in the bottom line: `● SCAN 24 of 64` vs `◼ HOLD 24`. One pixel row of difference — the state change is a *change of one line*, so it reads instantly without re-reading the screen.
- In SCAN, the number block updates live as the scanner lands on cars. In HOLD it is static.
- The signal glyph (`▂▄▆█`) is the only "meter". No S-units, no dBm. Race fans read bars.
- Inverted video for the currently-audible car in SCAN (blink-free, stable — no blinking anything, ever).

### 5.3 The LIST screen

```
▮▮▮▮ 14:32 ▂▄▆█
 08 JOHNSON              ← 16px number, 8px name, 2-line rows
    JIMMIE
▐24 BYRON              ▌ ← selected row, inverted
▐   WILLIAM            ▌
 19 TRUEX JR
    MARTIN
 ▴ ▾ 64 entries · * = lockout   (8px hint line)
```

- Three full rows visible; scroll indicator. Stations (RACE CTRL, MRN, PA, WX) are entries at the bottom of the list, typed in caps so they read as "not a car".
- Locked-out cars render struck-through (`̶2̶4̶` style or dimmed) — visible, reversible.
- `*` toggles the selected row. EXIT returns to SCAN.

### 5.4 The SETUP screens

Four pages, 8 px rows, UP/DOWN + knob to edit, M to move between pages. This is the only place the firmware resembles a menu, and it is deliberately boring. Fans should be able to own the radio for a year without opening it.

### 5.5 Boot sequence

Power on → 0.8 s brand/pack screen (pack name: "DAYTONA 500 — FEB 2026" + car count) → straight into SCAN. No "press any key", no menu, no confirmation. The pack screen doubles as the *identity* of the loaded data, so a fan who forgot to load a pack sees "NO PACK — plug in and load" instead of a blank radio.

Backlight: on at boot, auto-dim after 30 s (configurable), full-bright on any key. Sun readability is handled by contrast + the bold 32 px digits, not by fighting the sun with backlight.

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
             tone: 94.8, group: "A", favorite: true },
           ... 64 total ],
  stations: [ { name: "RACE CTRL",  freq: 461.200, tone: 0 },
              { name: "MRN",        fm: 101.1 },            // broadcast radio
              { name: "PA",         freq: 464.500, tone: 0 },
              { name: "WX",         noaa: 7 } ],            // NOAA channel
  lockouts: []
}
```

- Cars map to memory-channel slots; the *car number* is the channel identity — the frequency is just an attribute. (This inverts the stock mental model where a channel is a frequency.)
- Cars with multiple freqs (primary/secondary) appear as **separate flat entries** — `24 BYRON` and `24 ALT · BYRON` — exactly how RE-style packs list alternates. No multi-freq UI complexity anywhere; the pack decides. (A v1 refinement may collapse them into one entry with an alt shown in HOLD.)
- Tones (CTCSS) are *data*, not settings. The fan never sees them.

### 6.2 Open data is the moat-buster

RE sells frequency guides every season. We open-source the equivalent:

- **`race-packs/`** — a community-curated, git-tracked database: per series × track × session, with verification workflow (a contributor marks a pack "verified at the track", date-stamped). Frequency changes per season are normal; git history *is* the changelog.
- **Sourcing rule:** pack data is collected from public sources, own observation at the track, and fan submissions — never scraped from paid frequency guides (RE's guides are commercial publications; wholesale copying invites copyright/database-rights claims we don't need). Contributors attribute their sources in the pack metadata.
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

- **P0 — Validate (now, weeks):** collect real frequency data from 2–3 race weekends (Daytona, Sebring, Indy are the natural first three); interview 3–5 real fans; confirm the pack model against what a weekend actually needs. *Gate: a verified sample pack for one track.*
- **P1 — RaceScan firmware on stock K6 (weeks–months):** pack model + EEPROM layout; SCAN/HOLD/LIST/SETUP screens; key map; RX-only edition build; packtool v0; boot/backlight/power behavior. *Gate: a fan (not a ham) uses it for a full race without asking a question.*
- **P2 — Scan engine + data (months):** tone-lock landing, FOLLOW mode, adaptive squelch; web pack builder; open `race-packs/` v1 (Daytona, Sebring, Indy, Watkins Glen, Charlotte, Road America…). *Gate: community submits a verified pack for a track we don't cover.*
- **P3 — Hardware v2 (only if P1/P2 show demand):** open-hardware prototype per §10. *Gate: 100+ users of P1/P2.*

## 13. Honest risks & open questions

1. **RF reality at the track.** The K6 front end is wide open; README-grade honesty: FM RX "should be fine", but a packed NASCAR garage is a hostile RF environment (intermod, desense). We must field-test at a real weekend before promising anything. Mitigation: scan-engine tuning, antenna choice; Track B fixes it properly with band-pass filters.
2. **Flash budget.** 60 KB flash / 16 KB RAM. A dedicated RX-only edition has headroom (TX, VOX, DTMF, alarm, scrambler all go), but the 32 px font + race screens must be earned. LTO/overlay knobs exist.
3. **Hardware variants.** K5/K6/5R board revisions (this build targets the DP32G030 Cortex-M0; earlier boards differ in details), BK1080 presence — the edition must build and behave sanely across them (the base ifdefs much of this).
4. **Data curation.** Frequency lists drift season to season; bad packs make the radio useless at the worst moment. The `race-packs` repo needs a verification workflow and honest "unverified" labeling.
5. **Legal.** RX-only by design; US scanner law permits listening to the racing/broadcast bands. Two hard rules: (a) we must *never* ship a path to TX on team/business frequencies (the base codebase is TX-capable — the edition must strip it, not hide it); (b) the K6's wide RX includes US cellular bands, and receiving cellular audio is a felony (18 U.S.C. §2511 / ECPA) — the edition must band-lock cellular ranges (824–894, 1710–1755, 1850–1990, 2110–2155 MHz) so the radio physically cannot tune them.
6. **Naming.** "RaceScan" is a working title; there are existing products/apps with similar names. Trademark check before any branding.
7. **Open questions for the community:** should packs be per-series (NASCAR-only) or multi-series? Should the F2 BRD key also receive *track PA* (which at some tracks is AM broadcast)? Is FOLLOW mode a favorite-car priority or should fans pick any car? — all answerable in P0 validation.
