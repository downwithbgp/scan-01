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

## S3. Landing & tone gate
- [ ] Candidate = BK4819 squelch-open (base `FUNCTION_INCOMING` path); keep the
      software tone gate (`HandleIncoming` `g_CTCSS_Lost`/`g_CDCSS_Lost` checks)
      wired for tone'd entries; unmute after debounce.
- [ ] Verify CSQ entries land and open on carrier-only (open-mic risk, spec §6.1).
- **Gate:** build; **(bench)** two-radio test: tone'd transmitter opens audio only on
      tone match; CSQ transmitter opens on carrier; no burst-edge click.

## S4. FOLLOW mode (v1)
- [ ] My-driver (header `myDriver`) becomes the priority entry; interleave revisit
      every 8 entries (`FOLLOW_INTERLEAVE = 8`), tone-gated; no preemption of an
      already-landed exchange. Default ON when my-driver is set.
- **Gate:** build; **(hw)** with a favorite transmitting intermittently, worst-case
      latency ≈ 640 ms (measured via UART timestamps), no mid-exchange chopping.

## S5. CSQ hang guard (v1)
- [ ] CSQ entry holding the scan > 5 s continuously → skipped for one full cycle.
- **Gate:** host unit test (timer state machine, pure logic); **(hw)** open-mic test.

## S6. Squelch tuning + adaptive (v0 / v1)
- [ ] v0: race-tuned squelch defaults via `RADIO_ConfigureSquelchAndOutputPower`
      (egzumer `ENABLE_SQUELCH_MORE_SENSITIVE` class of values, baked in; no UI).
- [ ] v1: calibration pass (boot or SETUP-triggered — open question spec §11.5)
      measuring per-entry noise-floor RSSI into RAM offsets.
- **Gate:** build; **(hw)** at a real track: weak-car landing without noise-landing;
      v1 calibration improves a known-weak entry. (Track test = P0/P1 external gate.)

## S7. Signal meter
- [ ] RSSI → 4 bars on the SCAN screen (row 5), mapped per band via S0/S9
      (`ENABLE_RSSI_BAR` machinery).
- **Gate:** build; **(hw)** screenshots via `screenshot.c` over UART at known RSSI
      levels (signal generator or second radio at varying distance).

## S8. Integration
- [ ] Boot → SCAN resume; PTT HOLD/resume; long-PTT CAPTURE pre-fill (last-heard);
      `*` resume; F2 group cycle; `#`/long-9 favorite re-anchor; long-`*` lockout;
      BRD/WX pause/resume; capture-save → universe rebuild; empty-universe state;
      **PRACTICE mode: frequency-scan on practice bands via the kept CSS scanner, RAM-only
      mutations, boot returns to RACE**.
- [ ] Full-diff review (`/review`) of the engine against the base.
- **Gate:** build + review; **(hw)** P1 acceptance: a fan uses the radio for a full
      race without asking a question; scan never visibly "misses" a caution.

## S9. Spec sync
- [ ] Update vision §8 and the P1 spec with actuals; note deviations (dwell value,
      FOLLOW key, calibration trigger) as follow-ups.
- **Gate:** doc diff reviewed; deviations flagged, not silently absorbed.
