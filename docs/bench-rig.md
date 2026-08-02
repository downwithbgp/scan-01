# The bench rig — validating Scan 01 without a race

*Status: written 2026-08 (P2 S9); every (bench) gate in the specs refers
here. The rig needs two radios and, ideally, a signal generator — no track,
no RF environment required. Everything below is host-verified already
(`tests/`, the sim, the packtool's golden test); the bench closes the loop
on the hardware-truth items: timing, audio, and the protocol.*

## The setup

- **Radio A** — the device under test, running the Scan 01 edition.
- **Radio B** — a second UV-K5/K6 (any firmware; F4HWN is fine) used as a
  transmitter, OR a signal generator with CTCSS/DCS capability.
- **The bench pack** — a pack with known channels: three tone'd cars
  (CTCSS A/B/C), one DCS car, one CSQ car, one station — built with
  `packtool build` from `race-packs/library/` entries and flashed via
  `packtool flash` (the packtool round-trip gate itself).
- A UART cable to Radio A (the k5prog port) for `packtool dump` and the
  F4HWN screenshot channel.

## The (bench) gates

### 1. Dwell, decode hold, hang (S2)

Transmit a 300 ms burst on one channel from Radio B. On Radio A, observe
(UART debug or screenshot timing):

- the walk advances ~80 ms/entry (spec §4.1);
- the candidate pauses ~200 ms (decode hold) before audio opens;
- the audio opens without a burst-edge click (unmute debounce, §4.2);
- after the burst, the audio holds ~250 ms (hang) before the walk resumes;
- a second burst within the hang window does NOT chop (the no-chop re-land).

### 2. Tone-lock landing (S3)

- With Radio B transmitting **tone A** on channel A's frequency: Radio A
  opens audio (tone match).
- Switch Radio B to **tone B** on the same frequency: Radio A must NOT
  open audio — the foreign tone skips the entry and the walk moves on.
- CSQ channel: audio opens on carrier alone.
- Open-mic case: hold Radio B's carrier on the CSQ channel > 5 s —
  Radio A skips it for one cycle (the CSQ guard, §5).

### 3. FOLLOW (S4)

Set my-driver to car 24 in the bench pack. While the walk runs, transmit
a single 200 ms burst on car 24's channel at a random moment. Measure the
gap until Radio A lands on it — must be ≤ 8 × 80 ms + the decode hold
(≤ ~840 ms worst case).

### 4. The protocol round-trip (T9/T8)

- `packtool dump --port` on Radio A → `pack.json`; `packtool validate`;
  `packtool build` → a patch; `packtool diff` against the original.
- The **capture → reboot → present** flow: long-PTT capture a car, power
  cycle, confirm the entry survives in the LIST and the dump.
- `pack_status` over UART (0x05DF) reports the pack + captured entries.

### 5. The fan hand-off (P1 acceptance)

The ultimate gate: hand the radio to someone who is not a ham and does not
know the project. They must be able to: turn it on, hear the race,
hold a car, type a number, catch and save a frequency, find the weather,
and not get lost. The sim's scenarios are the rehearsal; the hand-off is
the performance.

## What the bench cannot test

Intermod and desense in a packed grandstand, weak-car landing against real
noise, the final dwell value, and the physical ergonomics — those need the
track (P0: IRP Saturday nights, Indy 500 May 2026).
