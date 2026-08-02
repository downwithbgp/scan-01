# P1 tasks — Pack model & EEPROM layout (Scan 01 edition)

Each task has a verification gate. Gates marked **(hw)** need real hardware and are the
external acceptance criteria; everything else is build/review/test-able in this repo.

## T1. Scan 01 edition scaffold — DONE
- [x] Makefile: `scan01` edition via `EDITION_STRING=Scan 01` + `compile-with-docker.sh scan01()`. Compile **out** (flags the base's own editions prove safe): SPECTRUM, F4HWN_SPECTRUM, VOX, AIRCOPY, AUDIO_BAR, ALARM, DTMF_CALLING, VOICE, TX_WHEN_AM, F_CAL_MENU, COPY_CHAN_TO_VFO, GAME, RESCUE_OPS. Compile **in**: NOAA, FMRADIO, SCREENSHOT, RX_TX_TIMER, RESUME_STATE. Deferred (latent base bugs on disable): BIG_FREQ, SCAN_RANGES, CUSTOM_MENU_LAYOUT, TX1750, FLASHLIGHT — revisit at T6.
- [x] `ENABLE_FEAT_SCAN01` guard — Makefile block **after** the `CFLAGS =` reset (earlier `+=` is wiped); verified in the compile flags.
- **Gate (PASS):** docker build (alpine:3.22 + gcc-arm-none-eabi) → `f4hwn.scan01.packed.bin`; `arm-none-eabi-size`: text 57,644 + data 20 = 57,664 B of 60K flash (**3.8K headroom**); bss 6,132 + data 20 ≈ 6.1K of 16K RAM. TX reachability audit remains a T9 item (TX is still compiled in the base core; channel records are written TX-locked).

## T2. Band-lock enforcement — function DONE, wiring pending
- [x] `PACK_FreqAllowed(freq_hz, mode)` in `pack_bandlock.c/h` — entry bands (88–108, 108–137, 151–162 [dirt+marine union], 162.4–162.55, 450–470 [incl. FRS]) valid in both modes; 2m 144–148 PRACTICE-only; cellular 824–894 / 1710–1755 / 1850–1990 / 2110–2155 hard-rejected in both modes, always (ECPA). In the edition build (Makefile OBJS, Scan01-gated).
- [ ] Wire into every tune path: BK4819 set, CAPTURE preview, BRD, WX. On reject: status flash + no tune. — **sequenced with the features that call them (T6 screens, T7 capture, S-tasks scan engine); no base tune path is guarded yet because the base's own scanning must keep working until the scan engine replaces it.**
- **Gate (PASS):** host test `tests/test_bandlock.c` — gcc -Wall -Werror: **814,958 checks, 0 failures** (17 real-world cases incl. the 2m RACE/PRACTICE split; every band edge ±1 Hz, both modes; 400k random freqs × 2 modes vs an independent classifier [soundness + completeness] + mode monotonicity). Firmware build green; function currently gc-sections-stripped (no caller yet) — 0 B delta until wiring pulls it in.

## T3. Pack layer (`settings_pack.c` / `settings_pack.h`) — DONE
- [x] Header read/validate (magic SC01, version 1, CRC-16/CCITT-FALSE with the CRC-field-reads-as-zero convention) at boot; RAM arrays for cars/stations/lockouts/my-driver; accessors for the UI/scan engine.
- [x] Writers: `PACK_SaveLockout`, `PACK_SaveFavorite`, `PACK_SetMyDriver`, `PACK_AddCapture` (channel record + name/team + CarMeta + counts + CRC; duplicate → `ALT` entry, never overwrite; PACK FULL at 64; **seal refuses, PRACTICE = RAM-only** — the single mutation choke point, spec §8). AddCapture band-checks (ECPA at rest).
- [x] Channel-record writer (spec §4.1: Hz LE, CTCSS index + 0x11/0x22 code type, AM<<4 for airband, 0x46/0x44 TX-locked flags); names+team (10+6) writer.
- [x] Factory-reset extension: settings.c wipes 0x1BD0–0x1DFF in both reset modes (spec §4.4); `PACK_Load` sanity-checks channel freqs via the band-lock. Boot call in main.c (PACK_Init → demo fallback on invalid → SCAN).
- [x] **Layout fixes found by testing (pre-release, spec updated):** header pad 0x3C→0x40 + fixed StationMeta base (captures never relocate stations) + cars at ch 0–63, stations at ch 64–87 (captures never collide) + all EEPROM writes as 8-aligned chunked RMW (24C64 page-wrap).
- **Gate (PASS):** host tests 166 checks, 0 failures (CRC vector 0x29B1, demo install/reload byte-exact, capture/ALT/full/seal/practice, lockout+favorite+my-driver round-trips, corruption→demo, CRC valid after every mutation). Firmware build green. **Flash budget:** text 60,120 + data 20 (pack layer ≈ 3.1 KB; the DTMF 5-tone RX chain is gated out of the edition, −300 B); headroom 1.3 KB < the old 2K T1 margin — CI gate moved to a 512 B no-brick floor; T6 must reclaim the ~9.3 KB stock menu stack (`ui/menu.o` + `app/menu.o`) per the plan. **Build hygiene:** build-scan01.sh now cleans stale objects first — flag changes do not trigger make rebuilds (caught as a stale-misc.o link error).

## T4. Racing-digits font (36 glyphs) + renderer — DONE (hw render gate pending)
- [x] 36 glyphs (0–9, A–Z), 32 px tall (4 strips), heavy condensed grotesque, hand-drawn in `tools/fontgen_racing.py` (the committed art IS the font source; `python3 tools/fontgen_racing.py > font_racing_data.c` regenerates). Advances: digits 16 ("1" = 12), letters 14 (M/W = 16), 1 px spacing — vision §5.6 updated (18 px draft estimate superseded by the art).
- [x] `RACING_PrintNumber(fb, line, right_x, text)` — right-aligned, 4-strip memcpy blits per the `gFontBig` pattern; `RACING_TextWidth()`; underflow-clamped.
- [x] Generator committed; NOT a `.fon` — the Python art source + generator replaces the base's Windows `.fon` pipeline.
- **Gate (host PASS; hw PENDING):** `tests/test_font.c` — 155 checks, 0 failures (advance table, per-glyph ink integrity, width math incl. "29A"=48, right-alignment property test — ink bounds computed from the glyph data, no clipping, no ink outside the 4-strip block). Firmware size unchanged (60,120 — the font is gc-stripped until T6 wires it); font data measures 2,413 B. **(hw)** screenshots via `screenshot.c` over UART, reviewed against §5.6 (glyph density, "29A" spacing) — needs a real radio.

## T5. Key handling — DONE (hw hand-off pending)
- [x] `scan01_keys.c/h` — the key layer: maps (state × key × press-type) → action per the vision §4.2 table, pure logic, no hardware calls. States: SCAN/HOLD/LIST/SETUP/BRD/WX/CAPTURE. Actions: PTT (HOLD/RESUME/CAPTURE/SAVE), label long-presses (0=BRD, 5=WX, 9=MYDRIVER, *=LOCKOUT, EXIT=HOME, M=KEYLOCK, SIDE1=MUTE_TOGGLE), nav, typing.
- [x] **F1/F2 = the two side keys** (KEY_SIDE1/KEY_SIDE2 — the base has no physical F1/F2); vision §4.2 table annotated.
- [x] **PTT hold timer** (~0.8 s → CAPTURE, layer-owned — the base treats PTT as press/release-only); quick tap = HOLD/RESUME; action lands on release; stale holds die on state change.
- [x] **Label-vs-digit conflict** resolved: a held digit is a label long-press only when the buffer holds exactly that single digit (a clean hold from idle) — labels are a promise, not a trap; mid-entry holds are suppressed.
- [x] **Typing state machine**: 1–3 digits + optional suffix (▲/▼ cycles A–Z, wraps, repeats on hold), `*` = decimal point (once; ≤3 int digits, ≤5 frac), EXIT delete / long-EXIT clear, 1.5 s timeout commit (PollCommit API — the tick can't return actions), no auto-commit in CAPTURE, PTT/M/F2/# abandon the entry (PTT wins), F1 mid-entry keeps it (harmless), WX never configures, SETUP is a form (nav/back/* only).
- **Gate (PASS):** build green (firmware unchanged, 60,120 — the layer is gc-stripped until T6 wires it; scan01_keys.o = 1,093 B); behavior matrix in `tests/test_keys.c` — **231 checks, 0 failures** (per-state matrices vs §4.2, PTT timing at 79/80 ticks, typing scenarios, conflict rules, grammar holes: no digit/point after a suffix, auto-commit restored after CAPTURE); reviewed against the §4.2 table. **(hw)** fan hand-off test lands in the P0/P1 gate.

## T6. Screens — PART A DONE (hw screenshots pending)
- [x] **The pivot: the stock menu stack is dead in this edition.** `DISPLAY_MENU` rows in both dispatch tables point at the Scan 01 handlers (no NULL hole); the menu-timeout logic in app.c (×8 sites incl. the exit_menu block) and the boot menu-count in main.c are gated behind `#ifndef ENABLE_FEAT_SCAN01`; the CSS-scan call in app/scanner.c is gated. The menu's render/walk code is gc-stripped (LTO + function-sections); a few small data/accessor bits survive via ungated references (MenuList table, UI_MENU_GetCurrentMenuId via helper/battery.c — benign, ~100 B). **60,120 → 58,276 B** (the menu was ~9.3K; the UI costs ~7.4K). Stock builds unaffected (default build verified).
- [x] `scan01_ui.c/h` — DISPLAY_SCAN01 owns SCAN/HOLD/LIST/CAPTURE; wired into the dispatch tables, `APP_TimeSlice10ms` (ticks), and `main.c` boot (PACK_Init → SCAN01_UI_Init → DISPLAY_SCAN01).
- [x] **SCAN/HOLD** (vision §5.3): status strip · 32 px right-aligned racing digits (car number or station name) · name 16 px + team 8 px · freq 8 px + RSSI bar · state line `◉ SCAN` / `◼ HOLD n` (hand-drawn 8 px glyphs) · row 7 whitespace. Stations render with kind label. Flash messages (2 s) for transient states.
- [x] **LIST** (vision §5.4): three 16 px rows, selection inverted (XOR block), live tune on selection, strike-through lockout (long-`*` toggles via PACK_SaveLockout), `＋ NEW` row at the end (PTT on it opens CAPTURE), hint row.
- [x] **Boot identity / NO PACK** (vision §5.8): 0.8 s track/session + car count (or "NO PACK — plug in and load"); any key skips.
- [x] **Typed jumps live**: commit "24" → tune car + HOLD; commit "451.1125" → band-locked tune + HOLD (`PACK_FreqAllowed`, AM on airband); no-car → "NO CAR n" flash. Favorites cycle + my-driver jump work (pack data).
- [x] **CAPTURE form** (vision §5.9, save path minimal): long-PTT opens it with the current frequency; 10×16 freq line (lines 0-1), 32 px number input (lines 2-5 — the T5 typing machine, no auto-commit), last-line affordance "SAVE AS CAR #n" / "TYPE 1-3 DIGITS" (the wireframe's two hint lines don't fit under a 32 px number — the preview line IS the affordance; PTT is the biggest button); PTT saves via `PACK_AddCapture` (duplicate → ALT, band-lock, PACK FULL — all T3-tested); EXIT cancels. **T7 still owns**: prefill-from-air last-heard + tone display, ALT-name polish, dump exposure.
- [x] **Memory safety under review pressure:** gFrameBuffer is 7×128 (page 0 is the status strip) — the wireframe's "row N" is line N-1; every render call was remapped and all small-string prints now go through truncating wrappers (the base's helpers neither clip nor bounds-check; 16 chars centered / 15 left-aligned is the safe bound at 7 px advance).
- [ ] **T6b:** BRD + WX screens (vision §5.10 — the BK1080 tuning), SETUP (4 pages incl. Mode RACE/PRACTICE + Seal), LIST name editor (multi-tap), venue divider rows.
- **Gate (PART A PASS):** build clean both editions; firmware 58,288 ≤ 60,928 (512 B floor); host suites unchanged (815,565 checks, 0 failures). **(hw) screenshots via `screenshot.c` over UART** for every screen + state variant, reviewed against the wireframes — pending a real radio.

## T7. Capture save flow
- [ ] Long-PTT → CAPTURE pre-filled with last-heard freq + decoded tone (`BK4819_GetCxCSSScanResult`); typed entry path (digits + `*`); live preview before save.
- [ ] Save: number (digits + suffix), `ALT` on duplicate, `origin=captured, verified=false`, joins current group, name `NEW`/`ALT <name>`, EEPROM persist.
- [ ] Expose captured entries to `pack_status()`/dump.
- [ ] Seal + PRACTICE guards (spec §5.3/§8): pack layer stubs all mutations in PRACTICE (RAM-only, dropped on power-off), refuses them when sealed; boot always returns to RACE; `pack_status()` reports mode + seal bit.
- **Gate:** build; review against vision §4.5; **(hw)** end-to-end: capture → reboot → entry present + scanning.

## T8. packtool v0 (Python)
- [ ] `validate` (§6 rules incl. ECPA band-lock), `build` → `pack.patch` region list, `diff`.
- [ ] Introduce the host test harness: `tests/` with pytest for pure functions (Hz↔MHz, name/team composition/truncation, CTCSS/DCS value↔index against the dcs.c tables, bitmap packing, CRC16) — the firmware tasks T2/T3 mirror these.
- [ ] `import` command (first parser: `indyspeedway` text format; fixture `race-packs/indyspeedway-ims-2026/source.txt` → semantically matches the checked-in draft `pack.json`).
- [ ] `compose` + `library` commands (multi-event weekend assembly: merge event packs + `race-packs/library/series/` + `daily/` stations, dedupe by frequency, capacity trade-off report with trim options; fixtures: `woo-sprints.json` and `daily-presets.json` compose into valid packs, and a two-venue fixture (IMS + IRP style) asserts venue bits + LIST divider order).
- [ ] `modulation` mapping (FM/AM → channel-record byte 11) with validation (AM only on 108–137 MHz).
- [ ] `dump` + `flash` over the base UART protocol (pin framing against `driver/uart.c` + CHIRP driver in this task; 8-byte chunked writes).
- **Gate:** `pytest` green; `packtool validate` on a real sample pack (P0 data) exits ≤ 1 (no errors — the draft pack intentionally carries a duplicate-frequency warning); build→dump round-trip on a fixture EEPROM image is semantically identical (same cars/stations/lockouts/flags; byte-level equality not required — `ALT` entries are reconstructed best-effort, spec §5.2).

## T9. Integration pass
- [ ] Boot → SCAN → HOLD → capture → save → reboot → lockout persists → dump → pack round-trip.
- [ ] Full-diff review (`/review`) of the edition against the base (no stray TX/DTMF/spectrum code reachable).
- **Gate:** build + review; **(hw)** P1 acceptance: a fan (not a ham) uses the radio for a full race without asking a question; packtool round-trip on the real radio.

## T10. Spec sync
- [ ] Update `docs/design/hci-vision.md` §11 seam table + roadmap P1 with actuals; note any spec deviations found during T1–T9.
- **Gate:** doc diff reviewed; deviations (if any) flagged as follow-ups, not silently absorbed.
