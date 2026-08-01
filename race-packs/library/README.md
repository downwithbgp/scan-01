# race-packs/library — the frequency library

The **library** is the answer to "how many stations can the radio hold?" — all of them,
because the radio never holds the library. The radio holds **one weekend's subset**
(≤ 64 cars / ≤ 24 stations, see the budget formula in `spec/p1-pack-eeprom/spec.md` §4.3);
the library holds everything, and packtool's `compose` command assembles the subset.

## Layout

```
library/
  series/     — national touring series stock channels (same at every track in the series)
                e.g. woo-sprints.json: Race Control, Raceceiver, PA, unconfirmed channels
  tracks/     — per-track frequencies (event packs reference these or carry them inline)
                v0: populated from event packs; per-track entries appear as they get verified
```

## Conventions

- **Entries use the pack station schema** (`name`, `kind`, `freq` or `fm`, `tone`,
  `digital`) plus library fields: `verified`, `verified_date`, `source`, `notes`.
- **Sourcing rule (vision §6.2):** public sources (RadioReference wiki, FCC license
  databases, own observation at the track) only — never scraped from paid guides.
  Attribution in `source` on every entry.
- **`verified: false` entries compose into packs with a warning**; only field-verified
  entries (date-stamped) are silent. Unconfirmed channels exist so fans can try them,
  honestly labeled.
- **Track frequencies** often sit in 151–160 MHz (dirt-track VHF) — the spec's
  station validation allows 151–160 in addition to 450–470 / 162.4–162.55 / 88–108.

## How `packtool compose` uses it

```
packtool compose events.json   # events: [ {pack: "race-packs/indyspeedway-ims-2026"}, ... ]
                               #          {series: "World of Outlaws", tracks: ["Putnamville"]} ]
```

1. Load each event pack (cars + stations) and each referenced series' stations.
2. Merge, dedupe by frequency (keep the first, warn on repeats — e.g. 454.000 raceceiver
   vs a track feed on the same freq).
3. Report the capacity trade-off (`52 + 4·cars + 10·stations ≤ 560`): if the weekend
   exceeds the caps, list what to trim (drop alt freqs, drop a series, drop unverified
   stations) and let the user pick — this is the "Brickyard + IRP" / "Daytona 24 + short
   track" weekend case.
4. Emit the pack; `flash` writes it over USB.

The desktop app (PC/Mac/Linux) is packtool with a GUI on top of exactly this flow:
browse the library → pick the races you're attending this weekend → compose → flash.
