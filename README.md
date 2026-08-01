# uv-k6-firmware — open race-scanner firmware

Modern, purpose-built firmware for the Quansheng UV-K5/K6 (and family), reimagined for
**race fans** — NASCAR, IMSA, IndyCar, dirt track. The market is cornered by Racing
Electronics and their rental/purchase prices; this project is the open-source answer:
the $25 commodity radio is already a race scanner, the firmware and the frequency data
are free. An **event scanner for every day**: racing is the wedge, daily use drives
development (vision §1).

- **Design vision:** [docs/design/hci-vision.md](docs/design/hci-vision.md) — the HCI
  reimagination (car-number-first interaction, PTT→HOLD, RACE/PRACTICE modes, the seal,
  HOME via long-EXIT, weekend "packs" of open frequency data). Start here.
- **Specs:** [spec/p1-pack-eeprom/](spec/p1-pack-eeprom/) (pack format + EEPROM layout,
  task list T1–T10) · [spec/p2-scan-engine/](spec/p2-scan-engine/) (scan engine, S1–S9).
- **Data:** [race-packs/](race-packs/) — event packs (IndyCar/IMS 2026 draft) and the
  frequency library (WoO series stations, daily presets).
- **Rams audit:** [docs/design/rams-audit.md](docs/design/rams-audit.md).

## Status

Design + specs complete; **implementation started (T1: edition scaffold done)**.
The firmware source is the vendored F4HWN v4.3 base (Apache-2.0, upstream:
armel/uv-k5-firmware-custom), trimmed (CMSIS reduced to the build's two include dirs;
old `archive/` binaries dropped) and living at the repo root.

## Building the RaceScan edition

Docker is the supported toolchain path (no native arm-none-eabi needed):

```
docker build --build-arg ALPINE_TAG=3.22 -t uvk5 .
./compile-with-docker.sh racescan    # → compiled-firmware/f4hwn.racescan.packed.bin
```

or directly: `docker run -v "$PWD:/app" uvk5 /bin/bash /app/build-racescan.sh`.

**T1 gate (passes):** text 57,644 B + data 20 of 60K flash (~3.8K headroom);
RAM ~6.1K of 16K. The edition defines `ENABLE_FEAT_RACESCAN` (Makefile, after the
`CFLAGS =` reset — earlier `+=` gets wiped) for edition-specific code.

Flag policy: only flags the base's own editions prove safe are disabled
(SPECTRUM/VOX/AIRCOPY/AUDIO_BAR); several "creative" disables (`ENABLE_BIG_FREQ=0`,
`ENABLE_SCAN_RANGES=0`) expose latent unconditional-reference bugs in the base and are
deferred until the screens (T6) actually replace that code.

## License

Derived work follows the base's Apache-2.0 license (see LICENSE).
