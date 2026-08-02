# The bench rig — validating Scan 01 without a race (and without transmitting)

*Status: rewritten 2026-08. The project has ONE radio and NO licence to
transmit. That is fine: receiving is always legal, the racing band
(450–470 MHz, FCC Part 90) is off-limits even to hams, and the rig below
never radiates — it either listens to real ambient signals or injects
conducted test signals straight into the antenna port (standard service-
monitor practice; nothing intentional radiates, so no licence applies).*

## The setup (what exists)

- **The radio** — one UV-K5/K6 running the Scan 01 edition.
- **The bench pack** — a pack with known channels (three tone'd cars, one
  DCS car, one CSQ car, one station) built with `packtool build` and
  flashed via `packtool flash` (that round-trip is itself a gate).
- **A UART cable** — for `packtool dump`, `pack_status` (0x05DF), and the
  F4HWN screenshot channel.
- **Ambient signals, free and legal to receive** — Indianapolis has plenty:
  NOAA 162.550, broadcast FM, the 2 m repeaters (146.94 / 146.76 — Skywarn),
  and IND aircraft on 118–137 MHz AM. These close the passive gates now.

## Phase 1 — the passive bench (no money, no licence, do this first)

Real signals can't be scripted, but they close most of the hardware gates:

- **Real-glass screenshots** — every screen, every state, via the F4HWN
  screenshot channel, reviewed against the sim's pixel budgets.
- **The receive path** — NOAA in WX, broadcast in BRD, aircraft in
  PRACTICE (AM), a repeater in SCAN. Audio clean, no clicks.
- **Capture on real signals** — long-PTT on NOAA, save as a car, reboot,
  confirm it survives; `packtool dump` round-trip; `pack_status`.
- **The fan hand-off, part 1** — hand the radio to someone who is not a
  ham; the passive bench is a real radio with real signals to listen to.

## Phase 2 — the active timing tests: DEFERRED TO THE TRACK (decision 2026-08)

The precise timing gates — dwell, decode hold, tone-lock, hang, FOLLOW,
the CSQ guard — need a *controlled* RF stimulus. The project has no
generator and no licence, and the decision is to validate the timing on
**real track signals** (IRP Saturday nights, Indy 500 May 2026) with the
sim as the reference until then. The timing logic itself is host-verified
(2048 engine checks + the sim's landing scenario); the track confirms the
constants on real RF.

If the timing questions become blocking before the track, the legal
conducted option remains open: a signal generator (or a HackRF-class SDR)
through an attenuator into the antenna port — nothing radiates, so no
licence applies — with CTCSS/DCS on exactly 450.000–470.000 MHz. The
measurement checklist (from the S-records) would be:

1. **Dwell / decode hold / hang (S2):** scripted 300 ms bursts — the walk
   advances ~80 ms/entry; the candidate pauses ~200 ms; audio opens
   without a burst-edge click; the carrier drop holds ~250 ms; a second
   burst within the hang does not chop.
2. **Tone-lock (S3):** tone A opens audio on channel A; switching the
   generator to tone B on the same frequency must NOT open audio — the
   foreign tone skips the entry. CSQ opens on carrier alone; > 5 s of
   continuous carrier on the CSQ channel triggers the guard skip.
3. **FOLLOW (S4):** with my-driver set, a scripted 200 ms burst on the
   priority channel must land within ≤ 8 × 80 ms + the decode hold
   (≤ ~840 ms worst case).

## Phase 3 — the licenced option (optional, 2 m / 70 cm only)

A licenced amateur friend can legally transmit on 2 m (144–148 MHz) and
70 cm (420–450 MHz — their licence covers those bands, NOT 450–470).
The K5/K6 front end covers 400–470 MHz, so tone-lock behavior at
440.000 MHz is representative of the racing band. Useful for audio-
quality and tone tests when no generator is at hand. Never use this
path on 450–470 MHz — Part 90, no amateur allocation.

## Phase 4 — the track (the final validator)

Intermod and desense in a packed grandstand, weak-car landing against
real noise, the final dwell value, and the ergonomics — only the track
can answer (P0: IRP Saturday nights, Indy 500 May 2026). The sim's
scenarios are the rehearsal; the track is the performance.

## The fan hand-off (P1 acceptance)

The ultimate gate: hand the radio to someone who is not a ham and does
not know the project. They must be able to: turn it on, hear the race,
hold a car, type a number, catch and save a frequency, find the weather,
and not get lost. Part 1 happens on the passive bench; the full
performance happens at the track.
