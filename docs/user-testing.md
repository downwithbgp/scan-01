# User testing guide — Scan 01

Scan 01 is the race-scanner edition of the UV-K5/K6 firmware: it turns a
$25 handheld into a race scanner (car numbers instead of frequencies, a
weekend "pack" of channels, weather and broadcast keys). This guide takes
a tester from a fresh radio to a full pass in about 45 minutes.

**Safety first:** the firmware is receive-only. There is no transmit path —
PTT is a hold/capture key, not a key-up. Listening to public radio traffic
(racing bands, NOAA, airband, 2 m ham, FRS) is legal in the US, and US
cellular bands are hard-blocked in the firmware. Nothing you press can
break the radio.

## What you need

- A Quansheng UV-K5 or UV-K6 (any K5-family clone).
- A USB programming cable — Baofeng/Kenwood style, into the radio's SP/MIC
  jack (the same cable CHIRP uses).
- A computer: Windows for flashing (the flasher GUI is Windows-only);
  the optional pack step needs Python 3.
- The firmware binary `scan01.packed.bin` — from the **user-testing**
  release: <https://github.com/downwithbgp/scan-01/releases/tag/user-testing>
  (a rolling pre-release, rebuilt on every push to main). Or build it
  yourself (README, "Building").

## Step 1 — Install the firmware (~5 min)

1. Radio **off**. Plug the cable into the SP/MIC jack and the PC.
2. Run a UV-K5 flasher. The base firmware this edition is built from
   documents **k5prog-win** (Windows GUI:
   <https://github.com/OneOfEleven/k5prog-win>). It powers the radio on
   and writes the flash; follow its port-selection instructions.
3. Select `scan01.packed.bin` (the "packed" format is what the standard
   upload tools require) and flash.
4. Power-cycle the radio.

A failed flash is recoverable — just run the flasher again.

## Step 2 — First boot

Power on. You should see, in order:

1. A brief (0.8 s) boot screen with the pack identity ("DAILY — DEMO" on a
   fresh radio — the built-in demo pack: airband guard 121.5, UNICOM 122.8,
   marine 16/9, FRS 1/7/14). Any key skips it.
2. Straight into **SCAN**, no menus, no "press any key": the scan walks
   the pack's channels and stops on any signal it finds.
3. The bottom line is the state line: "SCAN", "HOLD", the group filter,
   or a transient message ("NO CAR 77", "BAND LOCKED", …).

## The key legend

One function per key — the **printed label is the long-press action**:

| Key | Short press | Long press |
|---|---|---|
| `*` (SCAN) | resume scanning | in LIST/HOLD: lockout toggle (drop this car) |
| `#` (F) | cycle favorites | — |
| `0` (FM) | types 0 | broadcast FM (BRD) |
| `5` (NOAA) | types 5 | weather (WX) |
| `9` (CALL) | types 9 | my driver (FOLLOW) |
| PTT | hold / resume | capture (~0.8 s) |
| `M` | SETUP | keylock (not implemented yet) |
| `▲` / `▼` | in SCAN: open LIST · in LIST/HOLD/BRD/WX: walk | volume (while listening) · edit value (in SETUP) |
| `EXIT` | back / resume | HOME: LIST from anywhere (RACE) |
| `F1` | mute (3–30 s, SETUP → AUDIO) | persistent mute |
| `F2` | group filter: ALL → A → B → C → FAVS | — |

Typing a car number jumps to that car; typing a frequency (with the
decimal point via `*`) tunes it directly.

## Step 3 — Smoke test (~10 min, no signals needed)

Tick each check. The demo pack is loaded on a fresh radio.

- [ ] Power on → boot screen → SCAN starts by itself.
- [ ] `▲` → LIST opens. With the demo pack it shows only "+ NEW": the
      LIST is the car list, and the demo pack has no cars yet (the
      stations live in the SCAN walk, not the LIST). `EXIT` resumes.
- [ ] Type `77`: big digits appear; after a pause the radio flashes
      "NO CAR 77" (no car 77 in the demo pack — expected).
- [ ] Type `462.5625` (`*` for the point): the radio tunes FRS 1 and
      shows HOLD.
- [ ] **Capture a car (no signal needed):** still on 462.5625, long-PTT
      (~0.8 s) → CAPTURE opens pre-filled with the frequency. Type `77`,
      PTT to save — the radio flashes "SAVED 77" and returns to SCAN.
- [ ] `▲` → LIST again: it now shows "77" and "+ NEW". The selected row
      is inverted; `▲`/`▼` scroll.
- [ ] In LIST, long-`*` on car 77: it is marked (a line under the row)
      and the scan skips it. Long-`*` again: unmarked. `EXIT` resumes.
- [ ] Type `146.940` in RACE mode: "BAND LOCKED" — 2 m ham is
      PRACTICE-only by design (see Step 4).
- [ ] Long-`0` → BRD (FM broadcast): opens on a default station (101.1)
      and flashes "NO PRESETS" — no shipped pack has FM presets yet, so
      that flash is expected. PTT, `*`, or `EXIT` → back to SCAN.
- [ ] Long-`5` → WX (NOAA). `▲`/`▼` walk the 7 channels. PTT / `*` /
      `EXIT` → back.
- [ ] `M` → SETUP, page PACK 1/4 (MODE, SEAL, DRIVER, car/station
      counts). `M` pages through AUDIO 2/4, DISPLAY 3/4, INFO 4/4.
      Hold `▲`/`▼` edits the focused row (try DISPLAY → INVERT, then
      set it back). `EXIT` leaves. INFO shows the firmware version —
      note it for the report.
- [ ] PTT in SCAN → HOLD (stays on the current entry). PTT again →
      resumes.
- [ ] **Favorites & my driver (with your captured car 77):** `#` → "NO
      FAVS" (no shipped pack flags favorites — expected). `M` → PACK
      page → short-press `▼` to move the focus to DRIVER (a hold on the
      MODE row would flip the mode), then hold `▲`/`▼` until it shows 77
      → `EXIT` → long-`9` jumps to car 77 (HOLD); PTT resumes. The scan
      re-anchors at your driver and revisits it every 8 entries
      (~640 ms — never miss your driver).
- [ ] Long-`EXIT` from anywhere → LIST (HOME).

## Step 4 — Signal tests (~20 min, real signals)

Frequencies are Indianapolis-appropriate; substitute your own local ones
freely.

- [ ] **NOAA weather:** long-`5`; `▲` until you hear the local station
      (Indianapolis: **162.550**). Audio clean, no clicks.
- [ ] **FM broadcast:** long-`0` — opens FM on the default 101.1 and
      flashes "NO PRESETS" (expected until a pack ships FM presets). If
      a station is strong enough on that frequency you'll hear it.
- [ ] **Airband (AM):** `M` → PACK page → hold `▲`/`▼` on MODE — release
      as soon as it flips — until it reads **PRACTICE** → `EXIT`. Type a
      frequency in 118.000–136.000 and listen to IND arrivals — AM
      audio. In PRACTICE everything you change vanishes on power-off
      (the radio boots back to RACE).
- [ ] **2 m repeaters:** still in PRACTICE, type **146.940** or
      **146.760** — the local Skywarn repeaters. Listen-only, always legal.
- [ ] **Scan landing (the core feature):** back in RACE (M → PACK →
      MODE → hold `▲`/`▼` back to RACE → `EXIT`). Have a friend key an
      **FRS walkie** on FRS 1 (**462.5625**) — the scan should land on
      F1, open audio, and resume ~250 ms after they stop. Two quick
      bursts in a row should not chop the audio. (A friend's licence-free
      FRS walkie is the cleanest stimulus; this radio must never be the
      one transmitting.)
- [ ] **Capture:** long-`5` → WX → `▲` to 162.550 → long-PTT → CAPTURE
      opens pre-filled with the frequency (and the live tone decode).
      Type a number, PTT to save. It appears in LIST as a car — "ALT"
      if the number already exists, "NEW" otherwise. Power-cycle →
      still there (RACE mode persists).
- [ ] **Group filter:** `F2` flashes ALL → A → B → C → FAVS. With the
      demo pack, A/B/C keep the stations in the walk (only cars are
      filtered), and FAVS narrows to favorites plus your driver — with
      DRIVER set to 77 the walk shrinks to car 77. (On a fresh radio
      with no driver and no favorites, FAVS reads "NO CARS IN GROUP".)
      `F2` again returns to ALL.
- [ ] **Mute & volume:** `F1` mutes for the configured duration (long:
      until you un-mute); hold `▲`/`▼` while listening to change the
      volume.

## Step 5 — Report results

Report each problem as a GitHub issue on this repo, one issue per
problem, with:

- **Radio** — model (UV-K5 / UV-K6 / clone) and firmware version
  (SETUP → INFO).
- **Pack** — the boot-screen identity line.
- **Did** — the exact keys you pressed, in order.
- **Saw** — what the screen and audio did.
- **Expected** — what this checklist said should happen.

Everything green is a report too: "all N checks passed" is worth as much
as a bug report.

## What is NOT expected to work yet (don't report these)

- **Tone-lock on real tone'd race channels.** The timing validation is
  deferred to a real race weekend (docs/bench-rig.md). CSQ landing
  (FRS, airband, marine) is the path you can validate at home.
- **Racing-band traffic (450–470 MHz)** is quiet away from a track — the
  IndyCar pack loads and scans, but there is nothing to hear on a Tuesday.
- **Car-number jumps** need a pack with cars — the demo pack is stations
  only. Load the IndyCar pack (below) or capture a car first.
- **Keylock (M long)** is stubbed — it flashes "KEYLOCK T6C" and locks
  nothing.
- **FM presets in BRD** — no shipped pack has broadcast stations yet, so
  BRD opens on a default 101.1 and flashes "NO PRESETS".

## Optional — load a real pack

With Python 3, the cable, and this repo checked out — **radio ON** for this
step (the pack goes over the radio's own UART, unlike firmware flashing):

    pip install pyserial    # once — only dump/flash need it
    python3 -m packtool build race-packs/indyspeedway-ims-2026/pack.json -o /tmp/ims.patch
    python3 -m packtool flash /tmp/ims.patch --port /dev/ttyUSB0

(`python3 -m packtool --help` lists the commands.) The IndyCar/IMS 2026
pack has 26 cars and 17 stations; the boot screen shows its identity.
Note that 2 m ham channels cannot live in a pack — they are band-locked
in RACE mode and exist only as PRACTICE-mode tuning.
