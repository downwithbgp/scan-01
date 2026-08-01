# uv-k6-firmware — open race-scanner firmware

Modern, purpose-built firmware for the Quansheng UV-K5/K6 (and family), reimagined for
**race fans** — NASCAR, IMSA, IndyCar. The market is cornered by Racing Electronics and
their rental/purchase prices; this project is the open-source answer: the $25 commodity
radio is already a race scanner, the firmware and the frequency data are free.

- **Design vision:** [docs/design/hci-vision.md](docs/design/hci-vision.md) — the HCI
  reimagination (car-number-first interaction, PTT→HOLD, scan-first boot, weekend
  "packs" of open frequency data, and the hardware-v2 vision). Start here.
- **Reference base:** F4HWN v4.3 source (`uv-k5-firmware-custom-4.3`, Apache-2.0) is
  kept locally in `F4HWN/` (not committed; ~90 MB) as the fork base — see the
  "Where this lands in the F4HWN v4.3 codebase" section of the vision doc.

## Status

Design phase. No firmware code yet. Next: P0 validation (real frequency data from a
race weekend, fan interviews), then the RaceScan edition spec.

## License

Derived work will follow the base's Apache-2.0 license (see `F4HWN/` LICENSE).
