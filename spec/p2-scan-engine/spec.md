# P2 Spec — Scan engine (RaceScan edition)

**Status:** v0.2 — spec for review
**Applies to:** the RaceScan edition of the F4HWN v4.3 base (the vendored base — repo root)
**Sources verified:** `app/chFrScanner.c` (channel-scan state machine; `scan_pause_delay_in_6_10ms = 100ms` in misc.c; fast-scan 90 ms; base comment "<= ~60 ms it misses signals (squelch response and/or PLL lock time)" at chFrScanner.c:361), `app/app.c` (`HandleIncoming` software tone gate via `g_CTCSS_Lost`/`g_CDCSS_Lost`, detection pause `scan_pause_delay_in_3_10ms` at app.c:170), `app/scanner.c` (CSS/frequency finder — kept for capture), `radio.c` (`RADIO_ConfigureSquelchAndOutputPower`, squelch tables at 0x1E00/0x1E60, `SquelchOpen*Thresh`), `settings.h` (`SCAN_RESUME_MODE`, `SCANLIST_PRIORITY_CH1/2`), `driver/bk4819.c` (noise/RSSI squelch, tone-lost flags, RSSI)
**Related:** `docs/design/hci-vision.md` (§4 states, §5.3 SCAN screen, §8), `spec/p1-pack-eeprom/spec.md` (pack arrays, lockout bitmap, groups, venue bits)

---

## 1. Scope & goals

The scan engine is the **invisible 90%** of the experience (vision §8): the radio must
*move with the race*. This spec covers:

1. The **scan universe** — what gets scanned, in what order, with what filters.
2. The **scan cycle** — dwell, candidate detection, tone gate, resume, hang; the timing
   that makes "blink and you missed it" not happen.
3. **FOLLOW mode** (v1) — never miss your driver.
4. **Squelch strategy** — v0 tuned defaults, v1 adaptive; honest about storage limits.
5. **Integration** — states, keys, capture, boot, BRD/WX.
6. What in the base gets replaced, kept, or tuned.

Out of scope: the pack format (P1 spec), screens (vision §5), packtool, hardware v2.

## 2. Design principles

- **The scan IS the action-following.** Manual switching is one or two presses; the
  default experience is the radio landing on whoever is talking.
- **One behavior, not a menu.** The base has `SCAN_RESUME_MODE` (CARRIER 250ms–20s /
  STOP / TIMEOUT 5s–2m, F4HWN-extended). RaceScan has exactly one resume behavior,
  fixed and hidden. No TO/CO/SE, no dwell settings, no squelch UI — ever.
- **Tone-lock is a software gate on hardware flags.** The BK4819's squelch-open is
  noise/RSSI-based and tone-independent. The base gates tones in software:
  `HandleIncoming` (app.c) opens audio only when the chip is *not* reporting
  `g_CTCSS_Lost` / `g_CDCSS_Lost` for the channel's code type. The scan engine reuses
  this machinery: a tone'd entry cannot open audio unless its tone is present. Not
  free — but already built, and we only have to keep it wired.
- **Click-free or it sounds broken.** Unmute debounce on landing, hang time on drop,
  and a tone-decode hold so the gate has time to decide.
- **Honest about the cheap radio:** the K6 front end is wide open (vision §13.1);
  the engine's job is to make the best of it, not to promise what the hardware can't.

## 3. The scan universe

Built from the pack arrays (`PackCar[64]`, `PackStation[24]`, P1 spec §8) at load
time, as an index list (≤ 88 × 1 B in RAM):

- **Included:** cars (venue 0 then venue 1, in pack order) + non-broadcast stations
  (kinds 1–7; kind 0/broadcast is the BK1080 — it lives in the BRD sub-state, never
  in the scan).
- **Excluded:** locked-out entries (pack lockout bitmap, P1 spec §4.3 — the base's
  `gMR_ChannelExclude` equivalent, persisted), **digital stations** (kind bit 3 —
  listed in LIST, skipped by the scan), and everything outside the current **group
  filter**.
- **Group filter** (F2 short cycles): `ALL` (default) → `A` → `B` → `C` → `FAVS` →
  `ALL`. `A/B/C` = pack-assigned (`CarMeta.flags.group`); `FAVS` = favorite-flagged
  cars only (my-driver is a favorite by definition once set).
- **Order:** pack order (compose already orders venue 0 then venue 1, P1 §5.1). Scan
  order = LIST order = predictable. No position ticker on screen (vision §5.3).
- Filter/group changes (F2, `#`, long-`*`, favorite toggles) apply **at the next
  cycle boundary**, never mid-dwell.
- **Empty universe** (group filter matches nothing): the engine scans nothing
  gracefully — SCAN shows the last state, the state line reads "NO CARS IN GROUP ·
  F2", capture still works, and F2 cycles out. Never a hang, never a blank radio.

## 4. The scan cycle

### 4.1 Dwell

- **Target: 80 ms per entry** (build-time constant, `SCAN_DWELL_10MS = 8`), vs the
  base's 100 ms (`scan_pause_delay_in_6_10ms`, misc.c) and 90 ms fast-scan. The
  base's own comment sets the floor: "90 ms .. <= ~60 ms it misses signals (squelch
  response and/or PLL lock time)" (chFrScanner.c:361). **Do not go below 60 ms.**
  64 entries → ~5.1 s full cycle; 40 entries → ~3.2 s; worst-case landing latency =
  one cycle.
- The constant is a P0-track-validation item (60–100 ms is the accepted range),
  **not a UI setting** — the fan never sees dwell.

### 4.2 Landing

Sequence per candidate:

1. **Candidate trigger:** BK4819 squelch opens (noise/RSSI-based; base
   `FUNCTION_INCOMING` path).
2. **Tone-decode hold:** pause ~200 ms (`SCAN_DECODE_HOLD_10MS = 20` — the base's
   `scan_pause_delay_in_3_10ms` at app.c:170 exists for exactly this) so the gate
   can decide. Cost: 200 ms per candidate — irrelevant during a quiet scan, correct
   during chatter.
3. **Tone gate (software, base machinery):** audio opens only if the chip is not
   reporting tone-lost for the entry's code (`g_CTCSS_Lost` / `g_CDCSS_Lost` checks
   in `HandleIncoming`, driven by the entry's Code/CodeType from the channel record).
   This is the tone-lock landing from vision §8.
4. **Unmute debounce:** audio opens only after squelch has been open ~30–50 ms
   (`SCAN_UNMUTE_DEBOUNCE_10MS = 4`) — skip the burst edge, kill the click.
5. **CSQ entries** (tone = 0): no gate — they land and open on any carrier. They
   are the open-mic risk; see §6.1.

### 4.3 Resume & hang

- **One fixed behavior:** after a transmission ends, **hold 250 ms**
  (`SCAN_HANG_10MS = 25`) — covers syllable gaps within one exchange (racing chatter
  is 2–10 s bursts with sub-second gaps) — then resume scanning from the next entry.
- The base's `SCAN_RESUME_MODE` machinery is bypassed (menu code compiled out;
  `CHFRSCANNER_Found`'s pause-delay logic replaced by the fixed hang).
- **HOLD** (PTT): scan pauses on the current car, indefinitely, until PTT again.
  HOLD is not a resume mode; it is a state (vision §4.1).
- **Last-heard** persists on the SCAN screen between transmissions (vision §5.3) and
  is what long-PTT CAPTURE pre-fills (vision §4.5).

## 5. FOLLOW mode (v1)

"I want to hear everyone, but never miss the 24." Follows the base's own priority
pattern — `SCANLIST_PRIORITY_CH1/CH2` + the `SCAN_NEXT_CHAN_SCANLIST1/2` state —
repurposed:

- **My driver** (header `myDriver`, long-9 key) is the priority entry.
- With FOLLOW on (default **on** when my-driver is set), the engine **interleaves**
  the favorite: it is revisited every N entries (N = 8, `FOLLOW_INTERLEAVE = 8`),
  tone-gated, so its worst-case silence-detection latency is 8 × 80 ms = **640 ms**
  — a caution on the 24's channel cannot hide behind a 5 s cycle.
- If the favorite is tone-locked and talking, the engine stays; otherwise it
  continues the round-robin. No preemption of an *already-landed* other car
  mid-exchange (that would chop audio — the one thing fans hate).
- When FOLLOW is off (no my-driver set), plain round-robin.

## 6. Squelch strategy

### 6.1 v0 — tuned global, hidden

- Use the base machinery (`RADIO_ConfigureSquelchAndOutputPower`, per-band tables at
  0x1E00/0x1E60, `SquelchOpen*Thresh`), with **race-tuned defaults** baked in: the
  egzumer `ENABLE_SQUELCH_MORE_SENSITIVE` class of values, chosen so weak cars land
  but the open front end doesn't land on noise.
- No squelch UI anywhere. The fan never sees "SQL 3".
- **CSQ hang guard (v1):** a CSQ entry holding the scan > 5 s continuously is skipped
  for one full cycle (open-mic protection for tone-less channels; tone'd entries
  don't need it).

### 6.2 v1 — adaptive, RAM-learned

- A **calibration pass** (auto at boot, or SETUP → Pack → Calibrate): sweep the scan
  universe, measure the noise-floor RSSI per entry, store per-entry offsets in RAM.
- **Persistence is deliberately deferred:** there is no EEPROM space for per-car
  offsets in the v0 format (CarMeta is 4 B, full). The documented candidate for a
  future format change: **channel-record byte 12 bits 3–4** (2-bit offset, 4 levels)
  — flagged in this spec, not committed. RAM-only calibration per session is the v1
  honest answer.

## 7. Signal meter

- RSSI (`BK4819_GetRSSI`) → **4 bars** on the SCAN screen (row 5, vision §5.3).
- Map per band via the base's `S0_LEVEL`/`S9_LEVEL` RSSI-bar calibration
  (`ENABLE_RSSI_BAR` machinery). No S-units, no dBm (vision: "Race fans read bars").

## 8. Integration

| Event | Behavior |
|---|---|
| Boot | Resume scan state (base `CURRENT_STATE` machinery); boot → SCAN immediately (vision §5.8) |
| PTT short | HOLD current entry / resume scan from next entry |
| PTT long | Stop scan; CAPTURE pre-filled with last-heard (freq + tone from the landed entry) |
| `*` short | Resume scan from any state |
| long-EXIT | **HOME** — LIST in RACE mode from anywhere; exits PRACTICE (vision §4.1/§4.2) |
| F2 short | Cycle group filter (universe rebuilt at next cycle boundary) |
| `#` / long-9 | Favorites cycle / my-driver jump (vision §4.2) — engine re-anchors the scan at that car |
| long-`*` (HOLD) | Lockout toggle → bitmap update → skipped from next cycle |
| BRD / WX | Scan pauses while in the sub-state; resumes on PTT/EXIT/`*` |
| **PRACTICE mode** | The scan runs against the practice bands via the kept frequency scanner (vision §4.1); all pack mutations are RAM-only stubs; boot returns to RACE |
| LIST | Browse state; scan resumes on EXIT/`*` (vision §4.1) |
| CAPTURE | Scan stopped; save-as-car adds the entry → universe rebuilt, joins current group (vision §4.5) |

## 9. Performance budget

- RAM: universe index list ≤ 88 B + filter state; no per-entry buffers; the base's
  16 KB RAM budget holds comfortably.
- Flash: replaces the `CHFRSCANNER` channel-scan path (net ≈ −2–3 KB after the
  resume-mode menu code is compiled out).
- CPU: timer-driven 10 ms slices, same pattern as the base (`CHFRSCANNER` /
  `SCANNER_TimeSlice10ms`); no busy loops.

## 10. Base seams

| Concern | Base seam | RaceScan |
|---|---|---|
| Channel scan | `app/chFrScanner.c` | universe + timing + landing replaced (BK4819 calls kept) |
| Dwell | `scan_pause_delay_in_6_10ms` = 100 ms (misc.c); fast-scan 90 ms; base comment: ≤ 60 ms misses signals (chFrScanner.c:361) | `SCAN_DWELL_10MS` = 8 (80 ms, build-time) |
| Resume | `SCAN_RESUME_MODE` (F4HWN: CARRIER/STOP/TIMEOUT) | fixed carrier-drop + 250 ms hang; menu code compiled out |
| Landing | squelch-open → `FUNCTION_INCOMING` (app.c) | candidate + 200 ms decode hold + 30–50 ms unmute debounce |
| Tone-lock | software gate in `HandleIncoming` via `g_CTCSS_Lost`/`g_CDCSS_Lost` (app.c) | keep + decode hold; CSQ entries get the hang guard (v1) |
| Priority | `SCANLIST_PRIORITY_CH1/2` state | FOLLOW interleave (v1) |
| Lockout | `gMR_ChannelExclude` (RAM, base resets at boot) | pack lockout bitmap (persisted, P1 spec §4.3) |
| Squelch | `RADIO_ConfigureSquelchAndOutputPower`, 0x1E00/0x1E60 tables | race-tuned defaults (v0); RAM-learned offsets (v1) |
| RSSI meter | `ENABLE_RSSI_BAR`, S0/S9 | 4-bar display |
| CSS finder | `app/scanner.c` (`SCANNER_*`) | **kept** — capture's tone-find on typed frequencies + PRACTICE-mode frequency scanning (vision §4.1) |

## 11. Open questions (track-validation items)

1. **Dwell 80 ms** — the base's own floor is ~60 ms (chFrScanner.c:361); validate 80
   vs 90 vs 100 at a real track (too slow = "blink and you missed it", too fast =
   missed signals).
2. **Unmute debounce 30–50 ms + tone-decode hold 200 ms** — do they kill the
   burst-edge click and gate correctly without eating the first syllable of fast
   radio talk?
3. **FOLLOW interleave N=8 (640 ms worst case)** — right default, or should the
   favorite be revisited every 4 entries (320 ms) at the cost of longer normal
   cycles?
4. **CSQ hang guard threshold (5 s)** — is a continuous 5 s CSQ hold almost always
   an open mic in racing, or do spotters hold that long legitimately?
5. **Adaptive squelch calibration** — auto at boot (fast, but calibrates against
   whatever is in the air) vs SETUP-triggered (accurate, but a step the fan must
   know)? v1 decision.

## 12. Development & test strategy (the between-weekends loop)

Race weekends cannot drive the development loop (vision §1, "The radio lives between races"). The engine is
verifiable daily:

- **Bench rig:** two K6s and (ideally) a signal generator. Tone-lock, dwell, hang,
  debounce, FOLLOW, and the CSQ hang guard are all testable with one radio
  transmitting test bursts — no track, no RF environment required. Most of the
  S-task (hw) gates are actually **bench gates**.
- **Host-testable engine:** the universe/filter logic (S1) and the timing/state
  machines (S2/S5) are pure logic — unit + property tests in `tests/` (P1 T8
  harness), run on every commit. `/prop-test` on filter composition and the
  hang-guard state machine.
- **Demo pack as test fixture:** the flash-resident fallback pack (P1 §8) doubles
  as the daily test universe — deterministic channels, tone'd (FRS/marine are CSQ;
  add a bench tone) and mixed, so bench tests run against a stable set.
- **Track-validation remains only for what a bench cannot produce:** intermod and
  desense in a packed grandstand, weak-car landing against real noise, the final
  dwell value, and the fan-acceptance gate (vision §12 P1 gate).
