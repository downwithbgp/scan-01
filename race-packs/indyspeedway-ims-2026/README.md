# indyspeedway-ims-2026 — draft sample pack

**Status: DRAFT — NOT track-verified.** This is the first real data artifact (P0), transcribed
from the public indyspeedway.com "Scanner Frequencys" page (2026 season, updated 2026-03-05).
Per the sourcing rule it is attributed, not scraped from a paid guide.

## What's in it
- **26 IndyCar cars** (2026, primary frequencies only — the page shows no tone column for
  the 2026 field; IndyCar rulebook 7.4.1.3 mandates a unique DCS/DPL code per car, so tones
  must be captured at the track).
- **17 IMS stations**: PA (3), NBC/media (3), IndyCar Radio (4, incl. 454.0000 "analog with
  interference"), race control (2, tones 103.5 CTCSS / 032 DCS), safety (2 — SAF2 is
  **digital**, flagged `digital: true` and skipped by the Scan 01 scan), race director,
  officials, observers.

## What this data already changed in the spec
- **DCS is first-class** (IndyCar mandates it), not a v0 warning.
- **Narrowband 12.5 kHz default** for cars (rulebook 7.4.1.1).
- **Station cap raised 16 → 24** (IMS alone needs 17).
- **New station kinds**: `safety`, `radio` (MRN/PRN on UHF — MRN is 454.2 at IMS, not FM), `media`.
- **Digital stations**: flagged and skipped by the scan.

## Verification needed (P0 gate)
- Field-verify every car frequency + capture the DCS codes at the track (May 2026).
- Confirm PA/NBC/Radio feeds still current on race weekend.
- Known source quirks (kept in `source.txt` for the import parser): INDY NXT car# count
  mismatch, Xfinity duplicated #87, NASCAR blocks have no tone data.

## Files
- `pack.json` — draft pack (packtool `validate` + `build` should pass; expect the
  duplicate-frequency warning for cars 30/47 at 468.2625).
- `source.txt` — curated raw extract for the `packtool import` parser tests.
