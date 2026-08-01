# P1 tasks — Pack model & EEPROM layout (RaceScan edition)

Each task has a verification gate. Gates marked **(hw)** need real hardware and are the
external acceptance criteria; everything else is build/review/test-able in this repo.

## T1. RaceScan edition scaffold — DONE
- [x] Makefile: `racescan` edition via `EDITION_STRING=RaceScan` + `compile-with-docker.sh racescan()`. Compile **out** (flags the base's own editions prove safe): SPECTRUM, F4HWN_SPECTRUM, VOX, AIRCOPY, AUDIO_BAR, ALARM, DTMF_CALLING, VOICE, TX_WHEN_AM, F_CAL_MENU, COPY_CHAN_TO_VFO, GAME, RESCUE_OPS. Compile **in**: NOAA, FMRADIO, SCREENSHOT, RX_TX_TIMER, RESUME_STATE. Deferred (latent base bugs on disable): BIG_FREQ, SCAN_RANGES, CUSTOM_MENU_LAYOUT, TX1750, FLASHLIGHT — revisit at T6.
- [x] `ENABLE_FEAT_RACESCAN` guard — Makefile block **after** the `CFLAGS =` reset (earlier `+=` is wiped); verified in the compile flags.
- **Gate (PASS):** docker build (alpine:3.22 + gcc-arm-none-eabi) → `f4hwn.racescan.packed.bin`; `arm-none-eabi-size`: text 57,644 + data 20 = 57,664 B of 60K flash (**3.8K headroom**); bss 6,132 + data 20 ≈ 6.1K of 16K RAM. TX reachability audit remains a T9 item (TX is still compiled in the base core; channel records are written TX-locked).

## T2. Band-lock enforcement
- [ ] `PACK_FreqAllowed(freq_hz, mode)` — valid entry bands in both modes: 450000000–470000000, 162400000–162550000, 151000000–160000000, 108000000–137000000 (airband, AM only), 156000000–162000000 (marine), 462550000–467725000 (FRS/GMRS), 88000000–108000000 (broadcast path only); PRACTICE additionally permits frequency entry/scanning across that set plus 144000000–148000000 (2m); hard-reject 824–894 / 1710–1755 / 1850–1990 / 2110–2155 MHz (ECPA) in **both** modes.
- [ ] Wire into every tune path: BK4819 set, CAPTURE preview, BRD, WX. On reject: status flash + no tune.
- **Gate:** pure function — host unit test (`tests/test_bandlock.c` or Python mirror) with boundary cases; `/prop-test` on the filter (random freqs, assert reject/allow classes + boundaries).

## T3. Pack layer (`settings_pack.c` / `settings_pack.h` — new)
- [ ] Header read/validate (magic "RSPK", version, CRC16-CCITT) at boot; RAM arrays for cars/stations/lockouts/my-driver.
- [ ] Writers: `PACK_SaveLockout`, `PACK_SaveFavorite`, `PACK_SetMyDriver`, `PACK_AddCapture` (writes channel record + name/team + CarMeta + counts + CRC; duplicate → `ALT` entry, never overwrite).
- [ ] Channel-record writer using the base's packing (values per spec §4.1: Hz frequency, CTCSS index + `0x11` code type, TX_LOCK set).
- [ ] Factory-reset extension: wipe 0x1BD0–0x1DFF in both reset modes (spec §4.4); `PACK_Load` sanity-checks channel frequencies.
- [ ] Boot-state integration: valid pack → identity screen + SCAN; invalid/missing → **demo-pack fallback** (flash-resident daily presets) → SCAN; "NO PACK" only as a SETUP diagnostic (capture still usable).
- **Gate:** build; CRC/bitmap/conversion logic host-tested in `tests/` (harness introduced in T8); `/prop-test` on CRC + bitmap round-trips; code review against spec §4.3 byte table.

## T4. Racing-digits font (36 glyphs) + renderer
- [ ] `gFontRacingDigits[36]`: 0–9, A–Z, 32 px tall (4 strips), digits 18 px advance ("1" = 12), letters 14 px (vision §5.6).
- [ ] `UI_PrintRacingNumber(zone, string)` — right-aligned, 4-strip blit per the `gFontBig` pattern; NUL-terminated input.
- [ ] Generate via the existing `utils/` pipeline; commit the `.fon` source.
- **Gate:** build + size report (~2.2 KB); **(hw)** render test screens dumped via `screenshot.c` over UART and reviewed against the §5.6 spec (glyph density, "29A" spacing).

## T5. Key handling
- [ ] **Long-EXIT = HOME**: from any state, any mode → LIST in RACE mode (exits PRACTICE, discarding the practice session); while typing, long-EXIT still clears the entry first (base convention).
- [ ] Long-press timers: PTT hold (~0.8 s → CAPTURE), label long-presses (0/5/9/M/*/F1/F2), suppressed while a digit entry is pending (spec: labels are a promise, not a trap).
- [ ] Typing state machine: 1–3 digits + optional suffix letter (▲/▼ cycles A–Z), `*` = decimal point, EXIT delete / long-EXIT clear, timeout commit, no-car → "tune as frequency?" prompt.
- [ ] Key map per vision §4.2 table (short = digit/nav; long = printed label).
- **Gate:** build; behavior matrix (state × key × press-type) reviewed against the §4.2 table; **(hw)** fan hand-off test in P0/P1 gate.

## T6. Screens
- [ ] SCAN / HOLD (vision §5.3 pixel budget — status strip, 32 px number zone, name 16 px, team 8 px, freq+signal, state line, whitespace row).
- [ ] LIST (vision §5.4 — 16 px rows, gFontBig numbers, strike-through lockout, ＋ NEW row, **venue divider rows ("— IRP —") when the entry venue bit flips**).
- [ ] CAPTURE (vision §5.9), BRD + WX (vision §5.10), SETUP (4 pages — incl. **Mode: RACE/PRACTICE** and **Seal** on the Pack page, vision §4.1), boot identity / NO PACK (vision §5.8).
- [ ] LIST name editor (multi-tap keypad input, new component — the base input box is digits-only; vision §4.5).
- **Gate:** build; **(hw)** screenshots via `screenshot.c` over UART for every screen + state variant; reviewed against the vision wireframes (pixel budgets exact).

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
