# P2 tasks — Scan engine (RaceScan edition)

Each task has a verification gate. Gates marked **(hw)** need real hardware in a
real RF environment; **(bench)** gates need only two radios on a bench — the
daily development loop (spec §12); everything else is build/review/host-test-able
in this repo. Depends on P1 T3 (pack layer: `PackCar`/`PackStation` arrays,
lockout bitmap) being in place.

## S1. Scan universe module (`scan_pack.c` / `scan_pack.h` — new)
- [ ] Build the index list from the pack arrays: venue order, cars + non-broadcast
      stations, exclude lockout + digital, apply group filter (ALL/A/B/C/FAVS).
- [ ] Filter changes (F2, favorites, lockout) apply at cycle boundaries; rebuild is
      O(n) and cheap. Empty-universe state per spec §3 (no hang, "NO CARS IN GROUP"
      state line, capture still works).
- **Gate:** build; **host unit tests** for the filter logic (pure function:
      universe × filter × lockout → expected order, incl. empty-universe) in
      `tests/` (P1 T8 harness); `/prop-test` on filter composition (group/venue/
      lockout combinations).

## S2. Cycle timing (dwell / hang / decode hold / debounce)
- [ ] `SCAN_DWELL_10MS = 8` (80 ms — base comment: ≤ 60 ms misses signals,
      chFrScanner.c:361), `SCAN_HANG_10MS = 25` (250 ms),
      `SCAN_DECODE_HOLD_10MS = 20` (200 ms), `SCAN_UNMUTE_DEBOUNCE_10MS = 4`
      (40 ms) as build-time constants; timer-driven 10 ms slice, same pattern as
      `CHFRSCANNER`.
- [ ] Base resume-mode machinery bypassed (fixed carrier-drop + hang); resume-menu
      code compiled out of the edition.
- **Gate:** build; timing logic reviewed against spec §4; **(bench)** UART debug output
      of per-entry dwell timings with a second radio or signal generator; verify
      ~80 ms/entry, the 200 ms decode hold, and the 250 ms hang.

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
