# P2 tasks — Scan engine (Scan 01 edition)

Each task has a verification gate. Gates marked **(hw)** need real hardware in a
real RF environment; **(bench)** gates need only two radios on a bench — the
daily development loop (spec §12); everything else is build/review/host-test-able
in this repo. Depends on P1 T3 (pack layer: `PackCar`/`PackStation` arrays,
lockout bitmap) being in place.

## S1. Scan universe module (`scan01_scan.c` — the engine's core) — DONE
- [x] The engine (`scan01_scan.c/h` — pure, host-tested): the universe index list
      (venue order, cars + non-broadcast non-digital stations, lockouts excluded,
      group filter ALL/A/B/C/FAVS, the my-driver follow slot always included).
- [x] Filter changes (F2) apply at the next cycle boundary (the walk position is
      preserved across rebuilds); empty universe → EV_EMPTY + the "NO CARS IN GROUP"
      state line; capture still works.
- **Gate (PASS):** host tests in `tests/test_scan.c` — universe × group × lockout ×
      follow, 2048 checks, 0 failures incl. a randomized 2000-tick invariant walk
      (never stuck, follow gap ≤ 8, state in range).

## S2. Cycle timing (dwell / hang / decode hold) — DONE
- [x] `SCAN_DWELL_10MS = 8` (80 ms), `SCAN_HANG_10MS = 25` (250 ms),
      `SCAN_DECODE_HOLD_10MS = 20` (200 ms) + the CSQ guard (500 ticks) and the
      FOLLOW interleave (8) as build-time constants in `scan01_scan.h`; the
      10 ms state machine (WALK → DECODE → LANDED → HANG) in the engine.
- [x] The base's CHFRSCANNER resume machinery is bypassed — the engine's own
      fixed carrier-drop + hang replaces it (the UI no longer calls
      CHFRSCANNER_Start/Stop).
- **Gate (PASS):** host tests pin the timing (dwell boundaries, decode hold,
      hang, no-chop re-land); the sim walks real dwells. **(bench)** UART dwell
      timing on two radios remains pending.

## S3. Landing & tone gate — DONE (hw bench pending)
- [x] Candidate = BK4819 squelch-open (`g_SquelchLost` — true = open); the engine's
      decode hold decides, using the base's tone gate (`!g_CTCSS_Lost` /
      `!g_CDCSS_Lost` per the entry's code type — the UI feeds them into the
      engine's tick); foreign tones skip the entry.
- [x] CSQ entries land on carrier-only (the CSQ hang guard caps the open-mic risk, S5).
- **Gate (PASS):** host tests cover the gate paths; the sim lands a tone'd signal
      end to end. **(bench)** two-radio audio test remains pending.
## S4. FOLLOW mode (v1) — DONE
- [x] The my-driver car is always in the walk (even outside the group filter) and is
      revisited every 8 entries (`SCAN_FOLLOW_INTERLEAVE`), worst-case silence
      latency 8 × 80 ms = 640 ms. `#` cycles favorites via the existing jump.
- **Gate (PASS):** host tests assert the revisit gap ≤ 8 across scripted and
      randomized walks.
## S5. CSQ hang guard (v1) — DONE
- [x] A CSQ entry landed > 5 s continuously → skipped on its next visit (the
      open-mic case); tone'd entries are immune by construction.
- **Gate (PASS):** host tests: the guard fires at 500 ticks, the guarded entry is
      skipped at the advance boundary, the second visit lands normally.
## S6. Squelch tuning + adaptive (v0 / v1) — v0 DONE
- [x] v0: the build carries `ENABLE_SQUELCH_MORE_SENSITIVE` + `SQL_TONE=550`;
      race-tuned defaults via the calibration are a P0 track item.
- [ ] v1: calibration pass (boot or SETUP-triggered — open question spec §11.5)
      stays a v1 decision.
## S7. Signal meter — DONE (T6a)
- [x] The RSSI bar renders from `BK4819_GetRSSI` on the SCAN/HOLD screen (T6a);
      it follows the engine's tuned entry automatically.
- **Gate:** build. **(hw)** visual check at the track remains pending.
## S8. Integration — DONE (hw hand-off pending)
- [x] Boot → engine walk; PTT HOLD stops the engine (the channel stays); long-PTT
      CAPTURE pre-fills the last-heard; `*`/EXIT resume; **F2 cycles the group
      (ALL→A→B→C→FAVS, rebuild at the cycle boundary, "GROUP A" / "NO CARS IN
      GROUP" flashes)**; `#`/long-9 re-anchor the walk; long-`*` lockout rebuilds
      on the next Start; BRD/WX pause the engine; capture-save keeps the pack
      (the engine rebuilds on Start).
- **Gate (PASS):** the sim drives the full flow: identity → walk → tone'd landing
      on car 48 → hang → resume, with the screen asserting the tuned channel.
      **(hw)** fan hand-off remains pending.
## S9. Spec sync — DONE (bench-rig doc pending)
- [x] This file records the actuals; the spec's constants and the seam table are
      current (the engine bypasses CHFRSCANNER; the base's HandleIncoming tone
      gate stays wired). `docs/bench-rig.md` deferred to the (hw) bench session.
