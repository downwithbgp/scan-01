# Rams audit — Scan 01 edition

**Status:** v0.1 — honest self-audit against Dieter Rams' 10 principles
**Method:** each principle is scored **Strong** (designed AND evidenced), **Good**
(designed but unproven — the gate that would prove it is listed), or **At risk**
(behind). Every score cites the artifact that carries the evidence.
**Rule:** an audit that scores itself 10/10 would violate principle 6. The gaps
are the point.

---

| # | Principle | Score | Evidence | Gate that moves it up |
|---|---|---|---|---|
| 1 | **Innovative** | Strong | Car number as the identity, not the frequency (vision §4); PTT repurposed → HOLD; capture-from-the-air (long-PTT, §4.5); label-honest keys (c1a61b7); open frequency data as the product (§6.2) | none needed — but innovation must stay invisible: a feature that needs explaining is a failure |
| 2 | **Useful** | Good — unproven | Everything serves the race-day moment (vision §2); scan-first boot, WX/BRD one long-press away, lockout one gesture, FOLLOW; **the experience now starts at pack-load, not green flag — PRACTICE mode (airport aircraft band, daily-driver bands, ephemeral sandbox) and the seal make the radio survive the trip (vision §4.1)** | **P1 acceptance (hw):** a fan (not a ham) uses the radio for a full race without asking a question; **and** the same radio survives a week of kid play and boots to the intact weekend |
| 3 | **Aesthetic** | Good — unproven | Typographic system: 8/16/32 scale, one big thing per screen, whitespace as frame, no blinking (vision §5); the 36-glyph racing font renders (T4) and `screenshots/` holds real captures of every screen | the screenshots need a human design-review pass against the wireframes (hw) — pixels render, taste is unproven |
| 4 | **Understandable** | Good — unproven | Seven rules; no menus in the hot path; one function per key; the printed labels are the legend (vision §3) — the most-designed *and least-tested* principle | **P0 watch-fans** (don't interview — watch): hand a fan a loaded radio at the track, count questions. Natural venue: IRP, Saturday night |
| 5 | **Unobtrusive** | Good — unproven | No beeps, no blinking, quiet status strip, radio disappears until needed, knob is always volume (vision §3) | P0/P1 (same watch-fans gate); also decide the key-click question *deliberately* — zero audio feedback is an assumption, not a finding |
| 6 | **Honest** | Strong | RX-only, period (§7); no "PRO" branding; RF limits stated, not hidden (§13.1); `origin`/`verified` flags on every entry; unconfirmed library channels honestly flagged; the RE market claim is quoted, not hyped; digital feeds flagged and skipped (65c5d68) | T9: every release build audited for TX reachability — compiled out must mean unreachable |
| 7 | **Long-lasting** | Good — structural bet | Open data survives season changes; git history is the changelog; firmware updates via the standard flasher; open-hardware path (P3) | P2 gate: the community submits a verified pack for a track *we don't cover* — the bet is that the library outlives the firmware |
| 8 | **Thorough, to the last detail** | Strong | Byte-level EEPROM tables; CRC + magic; wear-note chunk math; factory-reset edge cases; PACK FULL; duplicate→ALT; empty-universe state; ECPA band-lock; 8-byte chunked UART writes (913fbc5, 85470d9, 04e8263) | keep it up in implementation — thoroughness is cheap in a spec and expensive in a fielded radio |
| 9 | **Environmentally friendly** | Strong — unusual for the category | The product *is* reuse: flash a $25 commodity instead of buying a new plastic scanner per season; open hardware keeps v2 repairable (vision §1, §10) | P3: hardware v2 must keep repairability (screw-down case, standard cells, KiCad sources) — a v2 that needs a new radio every season would violate the thesis |
| 10 | **As little design as possible** | Strong | The deletion list is the longest section of the design: 63-item menu, F-key chords, TX, VOX, DTMF, alarm, scrambler, VFO/frequency mode, squelch UI, scan modes, tone display, scan-position ticker — all gone (vision §4.4) | every future feature request runs the deletion test (below) |

## The pattern

- **Strong cluster (1, 6, 8, 9, 10):** the project is excellent at *subtraction and
  honesty*. The deletion list, the RX-only posture, the byte-level thoroughness, and
  the reuse thesis are all genuinely Rams.
- **Unproven cluster (2, 4, 5):** useful, understandable, unobtrusive — the three
  principles that only hardware and humans can prove. They are exactly what the
  P0/P1 gates test, and the natural venue is now local: **IRP on a Saturday night**
  (user context: Marion County, IN — a real bullring without needing a big race
  weekend).
- **Rendered but unjudged (3):** aesthetic is no longer the laggard — the
  36-glyph font and every screen of the vision render (`screenshots/`), but no
  human has reviewed them. The gate is a design-review pass of the screenshots
  against the wireframes, not more code.

## The deletion test (for every future feature)

1. Does it serve the seven rules, or add an eighth?
2. Does it need a new key, a new screen, or a new setting the fan must see?
3. Can the pack/app do it instead of the radio?
4. Would a fan at a caution flag have time to use it?

If it fails any of the four, it goes back to the backlog or the app — not the radio.
